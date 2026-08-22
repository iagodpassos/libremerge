// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace lm
{
/** Install the application-wide options manager the engine expects
    (GetOptionsMgr()). In-memory for now; persistent settings are a
    later Phase 1 item. */
void installEngineOptions();
}
