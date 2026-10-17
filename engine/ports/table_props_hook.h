// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: where MakeTablePropertiesByFileName gets its
// answer. WinMerge reads the file patterns of Options > Compare > Table
// from its options; LibreMerge keeps that page's settings in the Qt
// application, which registers the function that applies them at startup.
// Without one, no file reads as a table.

#pragma once

#include "TableProps.h"

namespace lm
{

using TablePropertiesFunc = ::TableProps (*)(const String& path);

void SetTablePropertiesHook(TablePropertiesFunc func);

} // namespace lm
