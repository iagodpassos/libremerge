// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class MainWindow;

namespace lm
{

/** Register the macOS Services provider so "Compare with LibreMerge"
    in the Finder context menu routes the selected files here. */
void installMacServices(MainWindow *window);

} // namespace lm
