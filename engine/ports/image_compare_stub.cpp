// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: stub for the image compare engine. Upstream's
// implementation drives the WinIMerge Win32 DLL; a native image compare is a
// Phase 2 item. Until then, comparing files "as images" reports an error
// result instead of pretending to compare.
#ifndef _WIN32

#include "pch.h"
#include "CompareEngines/ImageCompare.h"
#include "DiffItem.h"

namespace CompareEngines
{

ImageCompare::ImageCompare(CDiffContext& ctxt)
	: m_pImgMergeWindow(nullptr)
	, m_colorDistanceThreshold(0.0)
	, m_hModule(nullptr)
	, m_ctxt(ctxt)
{
}

ImageCompare::~ImageCompare() = default;

void ImageCompare::CompareFiles(DIFFITEM& di) const
{
	di.diffcode.diffcode |= DIFFCODE::CMPERR;
}

int ImageCompare::compare_files(const String& file1, const String& file2, const IAbortable *piAbortable) const
{
	(void)file1; (void)file2; (void)piAbortable;
	return -1;
}

} // namespace CompareEngines

#endif // !_WIN32
