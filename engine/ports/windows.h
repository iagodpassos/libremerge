// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: the part of <windows.h> the vendored color
// options use (SyntaxColors, OptionsDirColors, OptionsCustomColors): the
// COLORREF type, RGB() and the system color indexes GetSysColor() takes.
// GetSysColor itself is in posix_compat.h, answered by the application.
#pragma once

#ifndef _WIN32

typedef unsigned char BYTE;
typedef unsigned int COLORREF;

#ifndef RGB
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((unsigned int)((BYTE)(g)) << 8)) \
	| (((unsigned int)(BYTE)(b)) << 16)))
#endif

// Windows' system color indexes (WinUser.h), which the color schemes
// name as 0x80000000 | index
enum
{
	COLOR_SCROLLBAR = 0,
	COLOR_BACKGROUND = 1,
	COLOR_ACTIVECAPTION = 2,
	COLOR_INACTIVECAPTION = 3,
	COLOR_MENU = 4,
	COLOR_WINDOW = 5,
	COLOR_WINDOWFRAME = 6,
	COLOR_MENUTEXT = 7,
	COLOR_WINDOWTEXT = 8,
	COLOR_CAPTIONTEXT = 9,
	COLOR_ACTIVEBORDER = 10,
	COLOR_INACTIVEBORDER = 11,
	COLOR_APPWORKSPACE = 12,
	COLOR_HIGHLIGHT = 13,
	COLOR_HIGHLIGHTTEXT = 14,
	COLOR_BTNFACE = 15,
	COLOR_BTNSHADOW = 16,
	COLOR_GRAYTEXT = 17,
	COLOR_BTNTEXT = 18,
	COLOR_INACTIVECAPTIONTEXT = 19,
	COLOR_BTNHIGHLIGHT = 20,
	COLOR_3DDKSHADOW = 21,
	COLOR_3DLIGHT = 22,
	COLOR_INFOTEXT = 23,
	COLOR_INFOBK = 24,
	COLOR_HOTLIGHT = 26,
	COLOR_GRADIENTACTIVECAPTION = 27,
	COLOR_GRADIENTINACTIVECAPTION = 28,
	COLOR_MENUHILIGHT = 29,
	COLOR_MENUBAR = 30,
	COLOR_DESKTOP = COLOR_BACKGROUND,
	COLOR_3DFACE = COLOR_BTNFACE,
	COLOR_3DSHADOW = COLOR_BTNSHADOW,
	COLOR_3DHIGHLIGHT = COLOR_BTNHIGHLIGHT,
	COLOR_3DHILIGHT = COLOR_BTNHIGHLIGHT,
	COLOR_BTNHILIGHT = COLOR_BTNHIGHLIGHT,
};

#endif
