// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QToolTip>

#ifdef LM_HAVE_PORTAL
#include "PortalAppearance.h"
#endif

namespace lm
{

namespace
{
const QString kThemeKey = QStringLiteral("Appearance/Theme");

/** A window darker than its text means a dark palette. */
bool paletteIsDark(const QPalette &palette)
{
	return palette.color(QPalette::Window).lightness()
		< palette.color(QPalette::WindowText).lightness();
}

#ifndef Q_OS_MACOS
/** The application-wide dark palette, in the tones of the content areas
    (a #1e1e1e editor inside #2a2a2a). */
QPalette darkPalette()
{
	const QColor text(0xdc, 0xdc, 0xdc);
	const QColor disabled(0x7a, 0x7a, 0x7a);
	QPalette palette;
	palette.setColor(QPalette::Window, QColor(0x2d, 0x2d, 0x2d));
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
	palette.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
	palette.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3a, 0x3a));
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
	palette.setColor(QPalette::Light, QColor(0x4a, 0x4a, 0x4a));
	palette.setColor(QPalette::Midlight, QColor(0x3e, 0x3e, 0x3e));
	palette.setColor(QPalette::Mid, QColor(0x24, 0x24, 0x24));
	palette.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
	palette.setColor(QPalette::Shadow, QColor(0x0c, 0x0c, 0x0c));
	palette.setColor(QPalette::Highlight, QColor(0x26, 0x4f, 0x78));
	palette.setColor(QPalette::HighlightedText, QColor(0xe6, 0xe6, 0xe6));
	palette.setColor(QPalette::Link, QColor(0x6c, 0xb4, 0xff));
	palette.setColor(QPalette::LinkVisited, QColor(0xb4, 0x8c, 0xff));
	for (const QPalette::ColorRole role : { QPalette::WindowText,
			QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText })
		palette.setColor(QPalette::Disabled, role, disabled);
	palette.setColor(QPalette::Disabled, QPalette::Highlight,
		QColor(0x3a, 0x3a, 0x3a));
	return palette;
}
#endif
} // namespace

Theme *Theme::instance()
{
	static Theme theme;
	return &theme;
}

Theme::Theme()
{
	const QString value =
		QSettings().value(kThemeKey, QStringLiteral("system")).toString();
	m_mode = value == QStringLiteral("light") ? ThemeMode::Light
		: value == QStringLiteral("dark") ? ThemeMode::Dark
		: ThemeMode::System;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
		this, &Theme::systemChanged);
#endif
#ifdef LM_HAVE_PORTAL
	m_portal = new PortalAppearance(this);
	connect(m_portal, &PortalAppearance::schemeChanged,
		this, &Theme::systemChanged);
#endif
}

void Theme::setMode(ThemeMode mode)
{
	if (mode == m_mode)
		return;
	m_mode = mode;
	QSettings().setValue(kThemeKey,
		mode == ThemeMode::Light ? QStringLiteral("light")
		: mode == ThemeMode::Dark ? QStringLiteral("dark")
		: QStringLiteral("system"));
	applyToApplication();
	emit changed();
}

void Theme::systemChanged()
{
	// our own request to the platform (macOS) echoes back while applying
	if (m_mode != ThemeMode::System || m_applying)
		return;
	applyToApplication();
	emit changed();
}

bool Theme::dark() const
{
	switch (m_mode)
	{
	case ThemeMode::Light: return false;
	case ThemeMode::Dark: return true;
	case ThemeMode::System:
		break;
	}
#ifdef LM_HAVE_PORTAL
	// the desktop's own setting, when a portal publishes it
	const PortalAppearance::Scheme portal = m_portal->scheme();
	if (portal != PortalAppearance::Unavailable)
		return portal == PortalAppearance::PreferDark;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	const Qt::ColorScheme scheme = qApp->styleHints()->colorScheme();
	if (scheme != Qt::ColorScheme::Unknown)
		return scheme == Qt::ColorScheme::Dark;
#endif
	// no color-scheme answer: a dark platform palette is the only signal
	// (applyToApplication puts the platform's back before asking)
	return paletteIsDark(qApp->palette());
}

void Theme::applyToApplication()
{
	m_applying = true;
#ifdef Q_OS_MACOS
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	// the application's appearance (menus, dialogs, title bars and the
	// native controls) follows a fixed choice instead of the system's
	QStyleHints *hints = qApp->styleHints();
	switch (m_mode)
	{
	case ThemeMode::Light: hints->setColorScheme(Qt::ColorScheme::Light); break;
	case ThemeMode::Dark: hints->setColorScheme(Qt::ColorScheme::Dark); break;
	case ThemeMode::System: hints->unsetColorScheme(); break;
	}
#endif
#else
	// back to the platform's palette first; replace it only when it does
	// not match, so a GNOME or KDE theme that agrees stays as it is
	static const QPalette platformToolTips = QToolTip::palette();
	QApplication::setPalette(QPalette());
	const bool wanted = dark();
	if (wanted == paletteIsDark(qApp->palette()))
	{
		QToolTip::setPalette(platformToolTips);
	}
	else
	{
		const QPalette palette = wanted ? darkPalette()
			: QApplication::style()->standardPalette();
		QApplication::setPalette(palette);
		QToolTip::setPalette(palette);
	}
#endif
	m_applying = false;
}

const DiffColors &diffColors()
{
	// WinMerge's defaults (Src/OptionsDiffColors.cpp)
	static const DiffColors light = {
		QColor(239, 203, 5), QColor(192, 192, 192),
		QColor(239, 119, 116), QColor(240, 192, 192),
		QColor(251, 242, 191), QColor(233, 233, 233),
		QColor(241, 226, 173), QColor(255, 170, 130),
		QColor(255, 160, 160), QColor(200, 129, 108),
		QColor(228, 155, 82), QColor(248, 112, 78),
	};
	// same roles, tuned for light text on a #1e1e1e editor
	static const DiffColors dark = {
		QColor(90, 78, 10), QColor(58, 58, 58),
		QColor(122, 52, 50), QColor(84, 62, 62),
		QColor(72, 68, 40), QColor(50, 50, 50),
		QColor(130, 108, 36), QColor(160, 84, 48),
		QColor(168, 88, 88), QColor(150, 78, 52),
		QColor(140, 90, 45), QColor(150, 70, 45),
	};
	return Theme::instance()->dark() ? dark : light;
}

} // namespace lm
