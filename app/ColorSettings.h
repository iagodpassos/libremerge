// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringList>

namespace lm
{

/** WinMerge's COLORSETTINGS (Options::DiffColors): the colors of the
    differences, as the options hold them. A text color the options leave
    none (CLR_NONE) is an invalid QColor: the line keeps its own colors,
    its syntax ones included. */
struct DiffColors
{
	QColor diff, diffDeleted, diffText;
	QColor selDiff, selDiffDeleted, selDiffText;
	QColor trivial, trivialDeleted, trivialText;
	QColor moved, movedDeleted, movedText;
	QColor selMoved, selMovedDeleted, selMovedText;
	QColor snp, snpDeleted, snpText;
	QColor selSnp, selSnpDeleted, selSnpText;
	QColor wordDiff, wordDiffDeleted, wordDiffText;
	QColor selWordDiff, selWordDiffDeleted, selWordDiffText;
};

/** WinMerge's DIRCOLORSETTINGS (Options::DirColors), with whether the
    folder comparison colors its items at all (OPT_DIRCLR_USE_COLORS). */
struct DirColors
{
	bool use = true;
	QColor equal, equalText;
	QColor diff, diffText;
	QColor notExistAll, notExistAllText;
	QColor filtered, filteredText;
	QColor margin;
};

/** Register the color options with WinMerge's defaults (OptionsInit:
    DiffColors, DirColors, SyntaxColors, CustomColors, the color schemes
    and the system colors of a scheme) and answer the engine's
    GetSysColor. Once, from installEngineOptions. */
void initColorOptions();
/** Read the colors from the options again: after a scheme is imported,
    and when Options is accepted. */
void reloadColors();

const DiffColors &diffColors();
const DirColors &dirColors();
/** A COLORINDEX_* of the editor (SyntaxColors): with "default text
    coloring" (OPT_CLR_DEFAULT_TEXT_COLORING) the text colors, the ones
    that follow the system (IsThemeableColorIndex), are the system's. */
QColor syntaxColor(int colorIndex);
bool syntaxBold(int colorIndex);
/** The background of what a marker marks (COLORINDEX_MARKERBKGND0 to 3:
    the search's, then the Marker dialog's three). */
QColor markerColor(int color);
/** A COLORREF of the options as a color: none (CLR_NONE) invalid, a
    system color (0x80000000 | index) the system's. */
QColor colorFromColorRef(int colorRef);
int colorRefFromColor(const QColor &color);

/** GetSysColor: a color scheme's own system colors when it switches them
    on (OPT_SYSCOLOR_HOOK_ENABLED, WinMerge's SysColorHook), the
    application palette's otherwise. A COLOR_* of ports/windows.h. */
QColor systemColor(int index);
/** The palette with the scheme's system colors in it, when the scheme
    has them on; false (and the palette as it was) otherwise. */
bool applySystemColors(QPalette *palette);
/** Whether the colors in effect bring system colors of their own. */
bool hasSystemColors();
/** The application palette before the scheme's system colors went into
    it, which a scheme's import takes the system colors from (the Theme
    sets it as it applies the palette). */
void setBasePalette(const QPalette &palette);

/** WinMerge's ColorSchemes: the schemes LibreMerge ships (the bundle's
    Resources/ColorSchemes on macOS, share/libremerge/ColorSchemes
    elsewhere), then the user's own, in the application's data folder. */
namespace ColorSchemes
{
QStringList folders();
QString privateFolder();
bool isPrivatePath(const QString &path);
/** The file of a scheme, the first folder's that has it; empty for none. */
QString path(const QString &name);
/** The names of the schemes of all the folders, sorted. */
QStringList names();
} // namespace ColorSchemes

/** The scheme each mode takes (OPT_COLOR_SCHEME, OPT_COLOR_SCHEME_DARK). */
QString colorSchemeName(bool dark);
void setColorSchemeName(bool dark, const QString &name);
/** Import a scheme's file into the options (COptionsMgr::ImportOptions,
    the system colors as the system has them, not the scheme's), and read
    the colors again. */
bool importColorScheme(const QString &path);
/** OnSaveCurrentScheme: the colors in a scheme file. */
bool saveColorScheme(const QString &path);
/** The colors are a mode's scheme (OPT_COLOR_MODE_EFFECTIVE): at startup
    and when the light or dark mode in effect changes, the colors become
    the scheme of the mode now in effect (CMergeApp::InitInstance,
    CMainFrame::OnSettingChange). True when one was imported. */
bool syncColorSchemeWithMode(bool dark);
/** The mode the colors are of (OPT_COLOR_MODE_EFFECTIVE). */
bool colorsAreDark();
void setColorsAreDark(bool dark);

} // namespace lm
