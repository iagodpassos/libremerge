// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: folder-compare image engine. Ports upstream's
// Src/CompareEngines/ImageCompare.cpp (which drives the WinIMerge Win32
// DLL) onto a pluggable hook so libremerge-engine stays Qt-free: the Qt
// app registers a comparator built on engine/ports/imgmerge at startup
// (see image_compare_hook.h). The 2-way/3-way DIFFCODE state machine below
// mirrors upstream line by line.
#ifndef _WIN32

#include "pch.h"
#include "CompareEngines/ImageCompare.h"
#include "DiffContext.h"
#include "DiffItem.h"
#include "PathContext.h"
#include "image_compare_hook.h"

namespace lm
{

static ImageCompareFunc g_imageCompareHook = nullptr;

void SetImageCompareHook(ImageCompareFunc func)
{
	g_imageCompareHook = func;
}

ImageCompareFunc GetImageCompareHook()
{
	return g_imageCompareHook;
}

} // namespace lm

namespace CompareEngines
{

ImageCompare::ImageCompare(CDiffContext& ctxt)
	: m_pImgMergeWindow(nullptr)
	, m_colorDistanceThreshold(ctxt.m_dColorDistanceThreshold)
	, m_preferWICDecoder(false)
	, m_hModule(nullptr)
	, m_ctxt(ctxt)
{
}

ImageCompare::~ImageCompare() = default;

int ImageCompare::compare_files(const String& file1, const String& file2, const IAbortable *piAbortable) const
{
	lm::ImageCompareFunc hook = lm::GetImageCompareHook();
	if (!hook)
		return DIFFCODE::CMPERR;
	return hook(file1, file2, m_colorDistanceThreshold, piAbortable);
}

void ImageCompare::CompareFiles(DIFFITEM& di) const
{
	PathContext files;
	m_ctxt.GetComparePaths(di, files);

	int result = DIFFCODE::CMPERR;

	switch (files.GetSize())
	{
	case 2:
		if (!di.diffcode.exists(0) || !di.diffcode.exists(1))
			result = DIFFCODE::DIFF;
		else
			result = compare_files(files[0], files[1], m_ctxt.GetAbortable());
		break;

	case 3:
		{
			unsigned code10 = (!di.diffcode.exists(1) || !di.diffcode.exists(0)) ?
				DIFFCODE::DIFF : compare_files(files[1], files[0], m_ctxt.GetAbortable());
			unsigned code12 = (!di.diffcode.exists(1) || !di.diffcode.exists(2)) ?
				DIFFCODE::DIFF : compare_files(files[1], files[2], m_ctxt.GetAbortable());

			if (code10 == DIFFCODE::CMPABORT || code12 == DIFFCODE::CMPABORT)
			{
				result = DIFFCODE::CMPABORT;
				break;
			}

			unsigned code02 = DIFFCODE::SAME;
			if (code10 == DIFFCODE::SAME && code12 == DIFFCODE::SAME)
				result = DIFFCODE::SAME;
			else if (code10 == DIFFCODE::SAME && code12 == DIFFCODE::DIFF)
				result = DIFFCODE::DIFF | DIFFCODE::DIFF3RDONLY;
			else if (code10 == DIFFCODE::DIFF && code12 == DIFFCODE::SAME)
				result = DIFFCODE::DIFF | DIFFCODE::DIFF1STONLY;
			else if (code10 == DIFFCODE::DIFF && code12 == DIFFCODE::DIFF)
			{
				code02 = (!di.diffcode.exists(0) || !di.diffcode.exists(2)) ?
					DIFFCODE::DIFF : compare_files(files[0], files[2], m_ctxt.GetAbortable());
				if (code02 == DIFFCODE::SAME)
					result = DIFFCODE::DIFF | DIFFCODE::DIFF2NDONLY;
				else
					result = DIFFCODE::DIFF;
			}

			if (code02 == DIFFCODE::CMPABORT)
				result = DIFFCODE::CMPABORT;
			if (code10 == DIFFCODE::CMPERR || code12 == DIFFCODE::CMPERR || code02 == DIFFCODE::CMPERR)
				result = DIFFCODE::CMPERR;
		}
		break;
	}

	di.diffcode.diffcode &= ~(DIFFCODE::TEXTFLAGS | DIFFCODE::TYPEFLAGS | DIFFCODE::COMPAREFLAGS | DIFFCODE::COMPAREFLAGS3WAY);
	di.diffcode.diffcode |= DIFFCODE::FILE | DIFFCODE::IMAGE | result;
}

} // namespace CompareEngines

#endif // !_WIN32
