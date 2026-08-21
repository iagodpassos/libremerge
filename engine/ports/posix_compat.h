// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: maps MSVC CRT names used by the vendored WinMerge
// engine to their POSIX equivalents. Force-included on non-Windows builds so
// the vendored sources stay pristine (diffable against upstream).
#pragma once

#ifndef _WIN32

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>

#define _read read
#define _write write
#define _open open
#define _close close
#define _lseek lseek

#ifndef O_BINARY
#define O_BINARY 0
#endif
#define _O_BINARY O_BINARY
#define _O_RDONLY O_RDONLY

#include <stdlib.h>
#include <strings.h>

#define _stricmp strcasecmp
#define _strnicmp strncasecmp

/* Win32 leftovers in the C diff core */
#define VOID void
#define STATUS_ACCESS_VIOLATION 0xC0000005L
static inline void RaiseException(unsigned long code, unsigned long flags,
                                  unsigned long nargs, const void *args)
{
	(void)code; (void)flags; (void)nargs; (void)args;
	abort();
}

/* MSVC "secure" CRT */
#define sprintf_s snprintf
static inline int lm_ctime_s(char *buf, size_t bufsize, const time_t *t)
{
	(void)bufsize; /* ctime_r requires a buffer of at least 26 bytes */
	return ctime_r(t, buf) ? 0 : -1;
}
#define ctime_s(buf, size, t) lm_ctime_s((buf), (size), (t))

#endif /* !_WIN32 */
