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
	PrevDiff,
	NextDiff,
	CopyRight,
	CopyLeft,
	Refresh,
	Save,
	TreeView,
	DeleteLeft,
	DeleteRight,
	DeleteBoth,
	DiffPane,
};

QIcon icon(Icon id);

} // namespace lm
