// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QKeySequence>
#include <QString>

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
	CopyRightNext,
	CopyLeftNext,
};

QIcon icon(Icon id);

/** A toolbar button's tooltip: its text and, when it has one, the menu
    shortcut as the platform writes it ("\u2325\u2192" on macOS,
    "Alt+Right" elsewhere). */
QString toolTip(const QString &text, const QKeySequence &shortcut = QKeySequence());

/** Restyle every toolbar under root for the current theme and re-render
    the icons of actions tagged with their lm::Icon in QAction::data(). */
void applyToolbarTheme(QWidget *root);

} // namespace lm
