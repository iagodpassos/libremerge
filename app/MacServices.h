// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

class MainWindow;
class QMenu;

namespace lm
{

/** Register the macOS Services provider so "Compare with LibreMerge"
    in the Finder context menu routes the selected files here. */
void installMacServices(MainWindow *window);

/** Hand macOS the application's Help menu (NSApplication's helpMenu),
    which it tops with its search field: the field finds the items of
    every menu by their titles and runs the one picked. Qt does not tell
    macOS which menu that is. */
void setHelpMenu(QMenu *menu);

/** The title of the menu macOS has for the Help menu, empty when it has
    none (for tests). */
QString helpMenuTitleForTest();

/** A native menu's items as "title<TAB>1|0", the flag telling whether
    the item carries a submenu; the application menu when menuTitle is
    empty, otherwise the top-level menu with that title (for tests: Qt
    merges menu actions into the application menu by role). */
QStringList appMenuItemsForTest(const QString &menuTitle = QString());

/** Bring the application to the front, so macOS installs the active
    window's menu bar (for tests launched from a terminal). */
void activateAppForTest();

/** Leave unanswered what asks the application to quit from outside: the
    Dock's Quit, a script, a tool that closes applications without a window
    (for tests: a selftest shows none, and AppKit ends a process asked to
    quit there and then, its checks unfinished, with the exit code of a
    success). */
void ignoreQuitRequestsForTest();

} // namespace lm
