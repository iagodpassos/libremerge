// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QAbstractItemView;

namespace lm
{

/** Call on a list or tree whose rows carry check boxes: where the
    platform style fails to draw them (macOS 27, see the .cpp) the view
    paints its own. Nothing changes anywhere else. */
void ensureItemCheckBoxes(QAbstractItemView *view);

/** Whether the platform style was found to leave item check boxes out
    (for tests). */
bool itemCheckBoxesNeedHelp();

} // namespace lm
