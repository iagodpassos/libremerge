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

/** WinMerge's images of the items of a folder comparison (its DIFFIMG_*
    indices into the list DiffImageListUtils::InitializeDiffImageList
    makes), with WinMerge's own icons (app/diffimages). */
enum class DiffImage
{
	LUnique,      ///< lfile: a file on the left only
	MUnique,      ///< mfile: in the middle only
	RUnique,      ///< rfile: on the right only
	LMissing,     ///< lmissing: in 3-way, everywhere but on the left
	MMissing,     ///< mmissing: everywhere but in the middle
	RMissing,     ///< rmissing: everywhere but on the right
	Diff,         ///< notequalfile
	Same,         ///< equalfile
	File,         ///< fileunknown: a file with no result
	BinSame,      ///< equalbinary
	BinDiff,      ///< binarydiff
	LDirUnique,   ///< lfolder
	MDirUnique,   ///< mfolder
	RDirUnique,   ///< rfolder
	LDirMissing,  ///< lmissingfolder
	MDirMissing,  ///< mmissingfolder
	RDirMissing,  ///< rmissingfolder
	Skip,         ///< fileskip
	DirSkip,      ///< folderskip
	DirDiff,      ///< notequalfolder
	DirSame,      ///< equalfolder
	Dir,          ///< folder: a folder with no result
	Error,        ///< error
	DirUp,        ///< folderup: the ".." item
	DirUpDisable, ///< folderup_disable: a ".." that goes nowhere
	Abort,        ///< aborted: an item a cancel stopped comparing
	TextDiff,     ///< notequaltextfile
	TextSame,     ///< equaltextfile
	ImageDiff,    ///< notequalimage
	ImageSame,    ///< equalimage
};

QIcon diffImage(DiffImage image);

/** Restyle every toolbar under root for the current theme and re-render
    the icons of actions tagged with their lm::Icon in QAction::data(). */
void applyToolbarTheme(QWidget *root);

} // namespace lm
