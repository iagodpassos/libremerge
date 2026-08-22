// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CompareOptions.h"

namespace lm
{

/** Install the application-wide options manager the engine expects
    (GetOptionsMgr()), backed by QSettings for persistence, and register
    the comparison option defaults. */
void installEngineOptions();

/** Build the diffutils options from the current global option values. */
DIFFOPTIONS currentDiffOptions();

} // namespace lm
