// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <QString>
#include <QStringList>

class FileFilterHelper;
struct FileFilterErrorInfo;

namespace lm
{

/**
 * WinMerge's global file filter (theApp.GetGlobalFileFilter()): the one
 * mask or filter expression every folder comparison goes by, with the
 * preset filter files (.flt) it may name as "pf:<name>". It is chosen on
 * the "Select Files or Folders" screen and in Tools > Filters, and kept
 * as OPT_FILEFILTER_CURRENT. It belongs to the GUI thread: a comparison
 * takes a copy.
 */

/** Load the preset filters and the saved mask. The mask the folder
    comparison's own field kept until 0.9.7 is taken over once. */
void installFileFilters();

/** The folder of the preset filters shipped with the application, and
    the user's own, where New and Install put theirs. */
QString sharedFiltersDir();
QString userFiltersDir();
/** Other folders for both, an empty one meaning the usual place, and
    the presets read again (for tests). */
void setFiltersDirsForTest(const QString &shared, const QString &user);

/** The mask or expression in use. */
QString fileFilterMask();
/** Take another one and save it; an empty one is "*.*" (what
    COpenView::OnOK does with the screen's filter field). */
void setFileFilterMask(const QString &mask);

/** The global filter itself: the Filters page's New, Install and Delete
    change its presets along with the page's own copy. */
FileFilterHelper *globalFileFilter();
/** A copy of it, preset files changed on disk read again first: for a
    folder comparison (CDirDoc::InitDiffContext) and for the Filters
    dialog to edit. */
std::shared_ptr<FileFilterHelper> cloneFileFilter();
/** The dialog's OK: the edited copy becomes the global filter and its
    mask is saved. */
void adoptFileFilter(const FileFilterHelper &edited);
/** Read the preset files of both folders into a helper
    (FileFilterHelper::LoadAllFileFilters, with this application's
    folders). */
void loadFilterFiles(FileFilterHelper *helper);

/** What is wrong with a mask or expression once the helper has parsed
    it, a line per error as WinMerge words them (FormatFilterErrorSummary);
    empty when it is valid. The helper keeps the mask. */
QStringList fileFilterErrors(FileFilterHelper *helper, const QString &mask);
QString formatFilterError(const FileFilterErrorInfo &error);

/** The mask fields' shared history (WinMerge's "Files\Ext"), the latest
    first. The folder window's filter bar keeps one of its own
    ("Files\DisplayExt"). */
QString fileFilterHistoryKey();
QString displayFilterHistoryKey();
QStringList fileFilterHistory(const QString &key = fileFilterHistoryKey());
void rememberFileFilter(const QString &mask,
	const QString &key = fileFilterHistoryKey());

} // namespace lm
