// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>

namespace lm
{

/** Toolbar icons, drawn with QPainter so they stay crisp at any DPI.
    The style follows WinMerge's classic toolbar: gold difference bars,
    blue navigation arrows, green refresh. */
enum class Icon
{
	FirstDiff,
	PrevDiff,
	NextDiff,
	LastDiff,
	CopyRight,
	CopyLeft,
	CopyAllRight,
	CopyAllLeft,
	Undo,
	Redo,
	Swap,
	Refresh,
	Save,
	Options,
	Find,
	TreeView,
	DeleteLeft,
	DeleteRight,
	DeleteBoth,
	DiffPane,
};

QIcon icon(Icon id);

/** Restyle every toolbar under root for the current theme and re-render
    the icons of actions tagged with their lm::Icon in QAction::data(). */
void applyToolbarTheme(QWidget *root);

} // namespace lm
