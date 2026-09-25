// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

class MainWindow;

namespace lm
{

/** Register the macOS Services provider so "Compare with LibreMerge"
    in the Finder context menu routes the selected files here. */
void installMacServices(MainWindow *window);

/** A native menu's items as "title<TAB>1|0", the flag telling whether
    the item carries a submenu; the application menu when menuTitle is
    empty, otherwise the top-level menu with that title (for tests: Qt
    merges menu actions into the application menu by role). */
QStringList appMenuItemsForTest(const QString &menuTitle = QString());

} // namespace lm
