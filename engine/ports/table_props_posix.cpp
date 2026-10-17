// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: upstream's Src/TableProps.cpp reads the table
// file patterns from WinMerge's options; here the application answers
// through a hook (see table_props_hook.h).
#ifndef _WIN32

#include "pch.h"
#include "TableProps.h"
#include "table_props_hook.h"

namespace lm
{

static TablePropertiesFunc g_tablePropertiesHook = nullptr;

void SetTablePropertiesHook(TablePropertiesFunc func)
{
	g_tablePropertiesHook = func;
}

} // namespace lm

TableProps MakeTablePropertiesByFileName(const String& path)
{
	if (lm::g_tablePropertiesHook != nullptr)
		return lm::g_tablePropertiesHook(path);
	// upstream's answer for a file no pattern takes, with the defaults of
	// the quote and newline settings
	return { false, 0, '"', true };
}

#endif // !_WIN32
