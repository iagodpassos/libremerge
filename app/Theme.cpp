// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"

#include "Theme.h"

#ifdef Q_OS_MACOS
#include "MacServices.h"
#endif

#include <QApplication>
#include <QMenuBar>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
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

/** The application-wide light palette, Fusion's own, whatever the style. */
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
	palette.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 128));
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
	palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0xef, 0xef, 0xef));
	palette.setColor(QPalette::Disabled, QPalette::Dark, QColor(0xbe, 0xbe, 0xbe));
	palette.setColor(QPalette::Disabled, QPalette::Shadow, QColor(0xb1, 0xb1, 0xb1));
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
#ifndef Q_OS_MACOS
	qApp->installEventFilter(this);
	m_kdeDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
		.split(QLatin1Char(':')).contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
#endif
}

/** Plasma's platform theme hands the application the palette of a color
    scheme the desktop switches to, and Qt's own themes their new one: a
    fixed choice stays, whether it had replaced the platform's palette or
    kept it, and the menu bar takes the colors again. System mode follows
    the scheme through systemChanged. */
bool Theme::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange
		&& !m_applying && !m_reapplying && m_mode != ThemeMode::System)
	{
		// once the desktop's change is through
		m_reapplying = true;
		QTimer::singleShot(0, this, [this]() {
			m_reapplying = false;
			if (m_mode != ThemeMode::System)
				applyToApplication();
		});
	}
	return QObject::eventFilter(watched, event);
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
	// on a KDE desktop the palette goes first: Plasma's platform theme no
	// longer updates its scheme while the application names one of its own
	// (KDE_COLOR_SCHEME_PATH), its palette it does, and the scheme is only
	// that palette's lightness
	const Qt::ColorScheme scheme = qApp->styleHints()->colorScheme();
	if (!m_kdeDesktop && scheme != Qt::ColorScheme::Unknown)
		return scheme == Qt::ColorScheme::Dark;
#endif
	// no color-scheme answer: a dark platform palette is the only signal
	// (applyToApplication puts the platform's back before asking)
	return paletteIsDark(qApp->palette());
}

bool Theme::systemDark() const
{
	if (m_mode == ThemeMode::System)
		return dark();
#ifdef Q_OS_MACOS
	return lm::systemAppearanceIsDark();
#else
#ifdef LM_HAVE_PORTAL
	const PortalAppearance::Scheme portal = m_portal->scheme();
	if (portal != PortalAppearance::Unavailable)
		return portal == PortalAppearance::PreferDark;
#endif
	// without the desktop's portal its setting is out of reach while a
	// mode is fixed: the platform palette was replaced by the mode's
	return dark();
#endif
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
		// not the style's standard palette for Light: Plasma's own platform
		// theme ignores the scheme asked for and Breeze's is the desktop's
		// color scheme, so that on a dark Plasma Light came out dark, and a
		// style that keeps QStyle's has Windows 2000's gray
		const QPalette palette = wanted ? darkPalette() : lightPalette();
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
	// Breeze gives the menu bar the scheme's colors once, and updates them
	// only for a main window with top toolbars of its own, which ours are
	// not: back to the application's, or its text stays that of the theme
	// before, over the header of the new one
	for (QWidget *window : QApplication::topLevelWidgets())
		for (QMenuBar *bar : window->findChildren<QMenuBar *>())
			bar->setPalette(QPalette());
	// the system's colors, before a scheme's go in
	lm::setBasePalette(qApp->palette());
#endif
	// the colors of the mode now in effect (OPT_COLOR_MODE_EFFECTIVE):
	// its scheme imported when the mode changed, then the scheme's system
	// colors on the palette
	lm::syncColorSchemeWithMode(dark());
	applySchemePalette();
	m_applying = false;
}

void Theme::applySchemePalette()
{
#ifndef Q_OS_MACOS
	QPalette palette = qApp->palette();
	if (!lm::applySystemColors(&palette))
		return;
	// a scheme's dark system colors in light mode, or light ones in dark
	// mode, stay off the window: the mode decides it, and what decides the
	// mode on KDE is the palette's lightness (the views take the scheme's
	// colors all the same)
	if (paletteIsDark(palette) != dark())
		return;
	QApplication::setPalette(palette);
	QToolTip::setPalette(palette);
	for (QWidget *window : QApplication::topLevelWidgets())
		for (QMenuBar *bar : window->findChildren<QMenuBar *>())
			bar->setPalette(QPalette());
#endif
}

void Theme::colorsChanged()
{
	applyToApplication();
	emit changed();
}

} // namespace lm
