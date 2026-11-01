// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
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

#ifndef Q_OS_MACOS
/** The application's own KDE color scheme, read by Breeze and by Plasma's
    platform theme (KColorSchemeManager sets it). */
constexpr const char *kKdeColorScheme = "KDE_COLOR_SCHEME_PATH";
#endif

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

/** The application-wide light palette, in the tones of Fusion's own,
    for a style whose standard palette is not light. */
QPalette lightPalette()
{
	const QColor text(Qt::black);
	const QColor disabled(0xbe, 0xbe, 0xbe);
	QPalette palette;
	palette.setColor(QPalette::Window, QColor(0xef, 0xef, 0xef));
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, Qt::white);
	palette.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
	palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::Button, QColor(0xef, 0xef, 0xef));
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::BrightText, Qt::white);
	palette.setColor(QPalette::Light, Qt::white);
	palette.setColor(QPalette::Midlight, QColor(0xca, 0xca, 0xca));
	palette.setColor(QPalette::Mid, QColor(0xb8, 0xb8, 0xb8));
	palette.setColor(QPalette::Dark, QColor(0x9f, 0x9f, 0x9f));
	palette.setColor(QPalette::Shadow, QColor(0x76, 0x76, 0x76));
	palette.setColor(QPalette::Highlight, QColor(0x30, 0x8c, 0xc6));
	palette.setColor(QPalette::HighlightedText, Qt::white);
	palette.setColor(QPalette::Link, QColor(0x00, 0x00, 0xff));
	palette.setColor(QPalette::LinkVisited, QColor(0xff, 0x00, 0xff));
	for (const QPalette::ColorRole role : { QPalette::WindowText,
			QPalette::Text, QPalette::ButtonText })
		palette.setColor(QPalette::Disabled, role, disabled);
	palette.setColor(QPalette::Disabled, QPalette::Highlight,
		QColor(0x91, 0x91, 0x91));
	return palette;
}
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
/** The scheme asked of Qt: none for System, which follows the desktop. */
Qt::ColorScheme requestedScheme(ThemeMode mode)
{
	switch (mode)
	{
	case ThemeMode::Light: return Qt::ColorScheme::Light;
	case ThemeMode::Dark: return Qt::ColorScheme::Dark;
	case ThemeMode::System: break;
	}
	return Qt::ColorScheme::Unknown;
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
	// our own request to the platform echoes back while applying
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
	qApp->styleHints()->setColorScheme(requestedScheme(m_mode));
#endif
#else
	// back to the platform's palette first; replace it only when it does
	// not match, so a GNOME or KDE theme that agrees stays as it is
	static const QPalette platformToolTips = QToolTip::palette();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	QStyleHints *hints = qApp->styleHints();
	hints->unsetColorScheme();
#endif
	qApp->setProperty(kKdeColorScheme, QVariant());
	QApplication::setPalette(QPalette());
	const bool wanted = dark();
	const bool matches = wanted == paletteIsDark(qApp->palette());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	// Qt's KDE and GNOME themes hand the desktop's scheme to Fusion, whose
	// standard palette follows it: on a dark KDE desktop, Light came out
	// dark (issue #13). Asked for, the scheme also stays when the
	// desktop's changes later
	hints->setColorScheme(requestedScheme(m_mode));
#endif
	if (matches)
	{
		QToolTip::setPalette(platformToolTips);
	}
	else
	{
		// Plasma's own platform theme ignores the scheme asked for, and
		// Breeze's standard palette is the desktop's color scheme: on a
		// dark Plasma, Light came out dark. A standard palette that is
		// not light gives way to ours
		QPalette palette = wanted ? darkPalette()
			: QApplication::style()->standardPalette();
		if (!wanted && paletteIsDark(palette))
			palette = lightPalette();
		// Breeze paints the menu bar and the toolbars in the header colors
		// of a KDE color scheme, not in the palette: the desktop's, white
		// text on our light gray, unless the application names its own
		// scheme, as KColorSchemeManager does
		const QString scheme = QStandardPaths::locate(
			QStandardPaths::GenericDataLocation,
			wanted ? QStringLiteral("color-schemes/BreezeDark.colors")
				: QStringLiteral("color-schemes/BreezeLight.colors"));
		if (!scheme.isEmpty())
			qApp->setProperty(kKdeColorScheme, scheme);
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

QColor markerColor(int color)
{
	static const QColor light[4] = {
		QColor(255, 255, 127), QColor(127, 255, 127), QColor(255, 127, 127), QColor(127, 127, 255),
	};
	// "VS Dark" has the search's gray-blue and a blue-violet third color,
	// next to its selection's blue: the match found was hard to tell from
	// the others. The search's is a dark green here (where upstream's light
	// yellow would be one with the differences' gold) and the third color a
	// purple, both as far from the selection and the differences as
	// WinMerge's light colors are from its own
	static const QColor dark[4] = {
		QColor(40, 112, 52), QColor(42, 161, 152), QColor(211, 54, 130), QColor(136, 80, 148),
	};
	const int index = qBound(0, color, 3);
	return Theme::instance()->dark() ? dark[index] : light[index];
}

} // namespace lm
