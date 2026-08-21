// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: stand-in for MSVC <tchar.h> on POSIX systems.
// tchar_t/_T come from crystaledit's ctchar.h (BSL-1.0), which is the
// engine's own portable tchar layer.
#pragma once

#ifdef _WIN32
#error "ports/tchar.h must not be picked up on Windows builds"
#endif

#include "crystaledit/editlib/utils/ctchar.h"

typedef tchar_t TCHAR;

/* narrow-build tchar aliases used by the vendored tests */
#include <unistd.h>
#define _taccess access
#define _trmdir rmdir
#define _tmkdir(path) mkdir((path), 0777)
#define _tremove remove
#define _tfopen fopen
#define _tcslen strlen
#define _tcscmp strcmp
#define _tcsicmp strcasecmp
