// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include "CompareOptions.h"

class FilterList;

namespace lm
{

/** Install the application-wide options manager the engine expects
    (GetOptionsMgr()), backed by QSettings for persistence, and register
    the comparison option defaults. */
void installEngineOptions();

/** Build the diffutils options from the current global option values. */
DIFFOPTIONS currentDiffOptions();

/** The enabled line filters (Tools > Filters) as the engine's filter
    list: differences whose lines all match become trivial. Null when no
    expression is enabled. */
std::shared_ptr<FilterList> currentLineFilters();

/** Turn every ignore option off except whitespace, which takes the
    given OPT_CMP_IGNORE_WHITESPACE value; in memory only, the saved
    settings stay as they are (for tests). */
void setCompareOptionsForTest(int ignoreWhitespace);

} // namespace lm
