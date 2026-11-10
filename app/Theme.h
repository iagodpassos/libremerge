// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>

#include "ColorSettings.h"

namespace lm
{

class PortalAppearance;

enum class ThemeMode
{
	System,
	Light,
	Dark,
};

/** Application theme: WinMerge's color mode, light or dark, either fixed
    or following the system appearance (the default), each mode with its
    color scheme (ColorSettings). Chosen in Options > Colors > Color
    Schemes. */
class Theme : public QObject
{
	Q_OBJECT
public:
	static Theme *instance();

	ThemeMode mode() const { return m_mode; }
	void setMode(ThemeMode mode);

	/** The resolved appearance. */
	bool dark() const;
	/** The system's own appearance, as "Follow system" would resolve it
	    (WinMergeDarkMode::GetEffectiveColorMode), whatever mode is in
	    effect. */
	bool systemDark() const;

	/** Make the whole application follow the resolved appearance, not
	    only the content areas: on macOS the window appearance is asked
	    of the system; elsewhere a dark or light palette replaces the
	    platform's when the two disagree (an AppImage's is always light).
	    Runs before every changed(); call it once at startup. */
	void applyToApplication();

	/** The colors changed (a color scheme imported, Options accepted):
	    the palette takes the scheme's system colors again, and every
	    view its colors, through changed(). */
	void colorsChanged();

signals:
	void changed();

protected:
	/** A palette the desktop hands over later does not undo a fixed choice. */
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	Theme();
	/** The system's appearance changed: follow it in System mode. */
	void systemChanged();
	/** The scheme's system colors on the application palette, where
	    LibreMerge owns it (not on macOS, whose native controls follow
	    the system's appearance), when they agree with the mode. */
	void applySchemePalette();

	ThemeMode m_mode = ThemeMode::System;
	bool m_applying = false;
	bool m_reapplying = false;
	bool m_kdeDesktop = false;
	PortalAppearance *m_portal = nullptr; // Linux only
};

} // namespace lm
