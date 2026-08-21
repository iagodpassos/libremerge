// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: shared ICU helpers (defined in win_codepage.cpp).
#pragma once

#include <string>
#include <unicode/ucnv.h>

std::string lmCodepageToConverterName(unsigned codepage);
UConverter *lmOpenConverter(unsigned codepage);
