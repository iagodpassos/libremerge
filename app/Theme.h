// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>

namespace lm
{

enum class ThemeMode
{
	System,
	Light,
	Dark,
};

/** Application theme: WinMerge-style light content (the default) or a
    dark equivalent, either fixed or following the system appearance. */
class Theme : public QObject
{
	Q_OBJECT
public:
	static Theme *instance();

	ThemeMode mode() const { return m_mode; }
	void setMode(ThemeMode mode);

	/** The resolved appearance for content areas. */
	bool dark() const;

signals:
	void changed();

private:
	Theme();
	ThemeMode m_mode = ThemeMode::System;
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

} // namespace lm
