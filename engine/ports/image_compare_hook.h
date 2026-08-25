// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: pluggable backend for the folder-compare image
// engine. libremerge-engine stays Qt-free; the Qt application registers a
// comparator built on the ported WinIMerge core (engine/ports/imgmerge) at
// startup. Without a registered hook, comparing "as images" reports CMPERR
// exactly like WinMerge does when WinIMergeLib.dll is missing.

#pragma once

#include "UnicodeString.h"

class IAbortable;

namespace lm
{

// Returns DIFFCODE::SAME / DIFF / CMPERR / CMPABORT
using ImageCompareFunc = int (*)(const String& file1, const String& file2,
	double colorDistanceThreshold, const IAbortable *piAbortable);

void SetImageCompareHook(ImageCompareFunc func);
ImageCompareFunc GetImageCompareHook();

} // namespace lm
