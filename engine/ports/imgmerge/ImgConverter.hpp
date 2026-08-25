// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge port layer: stub for the WinIMerge ImgConverter (upstream it
// renders PDF/SVG/EMF/WMF through Windows-only APIs — WIC, Direct2D,
// GDI+). With this stub those formats simply report "not supported";
// rendering SVG via QtSvg / PDF via QtPdf is a candidate follow-up.

#pragma once

#include "image.hpp"

class ImgConverter
{
public:
	static bool isSupportedImage(const wchar_t *) { return false; }
	bool isValid() const { return false; }
	bool load(const wchar_t *) { return false; }
	void close() {}
	void render(Image&, int, float) {}
	unsigned getPageCount() const { return 1; }
};
