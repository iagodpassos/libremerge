// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>

namespace lm
{

class PortalAppearance;

enum class ThemeMode
{
	System,
	Light,
	Dark,
};

/** Application theme: WinMerge-style light or a dark equivalent, either
    fixed or following the system appearance (the default). Chosen in
    Options > General. */
class Theme : public QObject
{
	Q_OBJECT
public:
	static Theme *instance();

	ThemeMode mode() const { return m_mode; }
	void setMode(ThemeMode mode);

	/** The resolved appearance. */
	bool dark() const;

	/** Make the whole application follow the resolved appearance, not
	    only the content areas: on macOS the window appearance is asked
	    of the system; elsewhere a dark or light palette replaces the
	    platform's when the two disagree (an AppImage's is always light).
	    Runs before every changed(); call it once at startup. */
	void applyToApplication();

signals:
	void changed();

private:
	Theme();
	/** The system's appearance changed: follow it in System mode. */
	void systemChanged();

	ThemeMode m_mode = ThemeMode::System;
	bool m_applying = false;
	PortalAppearance *m_portal = nullptr; // Linux only
};

/** The difference palette (WinMerge defaults in light mode, a matching
    dark set otherwise). */
struct DiffColors
{
	QColor diff, diffDeleted;
	QColor selDiff, selDiffDeleted;
	QColor trivial, trivialDeleted;
	QColor wordDiff, wordDiffDeleted;
	QColor selWordDiff, selWordDiffDeleted;
	QColor moved, selMoved;
};

const DiffColors &diffColors();

/** The background of what the search marker marks (upstream's
    COLORINDEX_MARKERBKGND0): WinMerge's default in light mode, the one of
    its "VS Dark" color scheme otherwise. */
QColor searchMarkerColor();

} // namespace lm
