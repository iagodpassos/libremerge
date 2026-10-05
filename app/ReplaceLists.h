// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <QString>
#include <QStringList>

class QWidget;

/**
 * WinMerge's replace lists (ReplaceListHelper): tab separated files of
 * replacements, plain or by regular expression, that a line filter
 * applies with replaceWithList() and regexReplaceWithList(). They are
 * the user's own, kept next to the settings: under ReplaceLists and
 * RegexReplaceLists of ~/Library/Application Support/LibreMerge on
 * macOS, of ~/.config/LibreMerge elsewhere.
 */
namespace lm
{

/** The most lists the filter menu shows of each kind. */
constexpr int kMaxReplaceLists = 20;

QString replaceListFolder(bool regex);
/** The lists of the folder, which is made when it is not there yet. */
QStringList replaceLists(bool regex);
/** Write a new list with upstream's template in it. */
bool createReplaceListTemplate(const QString &path, bool regex);
/** A list's path as a filter expression carries it: the home folder is
    written "%HOME%", which the engine expands, so that the expression
    does not name the user (upstream writes %APPDATA% and %USERPROFILE%
    for the same reason). */
QString replaceListPathForExpression(const QString &path);
/** ReplaceListHelper::CreateAndSelectReplaceListFile: ask where the new
    list goes, write the template there and open it in the text editor.
    Nothing when the question was cancelled or the file could not be
    written. */
std::optional<QString> createAndSelectReplaceList(QWidget *parent, bool regex);
/** Show a kind's folder in the file manager. */
void openReplaceListFolder(bool regex);

/** Another place for both folders, an empty one meaning the usual place
    (for tests). */
void setReplaceListBaseForTest(const QString &base);
/** What answers "where does the new list go" and what opens a file or a
    folder, instead of the dialog and the desktop (for tests; an empty
    function gives them back). */
void setReplaceListChooserForTest(std::function<QString(const QString &folder, bool regex)> chooser);
void setReplaceListOpenerForTest(std::function<void(const QString &path)> opener);

} // namespace lm
