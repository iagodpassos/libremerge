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
#include <wchar.h>

/* Minimal Win32 type vocabulary (tchar_t == char on POSIX builds) */
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef unsigned short VARTYPE;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef char *LPTSTR;
typedef const char *LPCTSTR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* Win32 file attribute bits (kept: DirItem stores them verbatim) */
#define FILE_ATTRIBUTE_READONLY  0x00000001
#define FILE_ATTRIBUTE_HIDDEN    0x00000002
#define FILE_ATTRIBUTE_SYSTEM    0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE   0x00000020
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define INVALID_FILE_ATTRIBUTES  ((DWORD)-1)

/* Win32 codepage API over ICU — implemented in ports/win_codepage.cpp */
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_THREAD_ACP 3
#define CP_UTF7 65000
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define WC_COMPOSITECHECK 0x00000200
#define WC_DISCARDNS 0x00000010
#define WC_SEPCHARS 0x00000020
/* upstream's TCHAR->tchar_t mass rename also hit the WC_DEFAULTCHAR token */
#define WC_DEFAULtchar_t 0x00000040
UINT GetACP(void);
BOOL IsValidCodePage(UINT codepage);
int MultiByteToWideChar(UINT codepage, DWORD flags, const char *src, int srclen,
                        wchar_t *dst, int dstlen);
int WideCharToMultiByte(UINT codepage, DWORD flags, const wchar_t *src, int srclen,
                        char *dst, int dstlen, const char *defaultChar,
                        BOOL *usedDefaultChar);

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

/* Win32 system colors: no equivalent on POSIX; the GUI layer owns colors.
   Used only when importing option files that reference system colors. */
static inline unsigned long GetSysColor(int index)
{
	(void)index;
	return 0;
}

/* Case-insensitive memory compare (MSVC _memicmp), used by markdown.cpp */
#ifdef __cplusplus
#include <cctype>
static inline int _memicmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	for (size_t i = 0; i < n; ++i)
	{
		int d = tolower(pa[i]) - tolower(pb[i]);
		if (d != 0)
			return d < 0 ? -1 : 1;
	}
	return 0;
}
#endif

/* MBCS legacy: the internal codepage is UTF-8, never a Windows DBCS page */
static inline int _getmbcp(void) { return 0; }
static inline int IsDBCSLeadByte(int ch) { (void)ch; return 0; }

/* GetStringTypeW: only reached from dead branches when tchar_t is char */
#define CT_CTYPE1 1
#define C1_UPPER  0x0001
#define C1_LOWER  0x0002
#define C1_DIGIT  0x0004
#define C1_SPACE  0x0008
#define C1_PUNCT  0x0010
#define C1_CNTRL  0x0020
#define C1_BLANK  0x0040
#define C1_XDIGIT 0x0080
#define C1_ALPHA  0x0100
static inline int GetStringTypeW(int infoType, const void *src, int count, WORD *charType)
{
	(void)infoType; (void)src; (void)count;
	if (charType != NULL)
		*charType = 0;
	return 0;
}

#include <sys/stat.h>
static inline DWORD GetFileAttributes(const char *path)
{
	struct stat lm_st;
	if (stat(path, &lm_st) != 0)
		return INVALID_FILE_ATTRIBUTES;
	{
		DWORD attrs = 0;
		if (S_ISDIR(lm_st.st_mode))
			attrs |= FILE_ATTRIBUTE_DIRECTORY;
		if ((lm_st.st_mode & S_IWUSR) == 0)
			attrs |= FILE_ATTRIBUTE_READONLY;
		if (attrs == 0)
			attrs = FILE_ATTRIBUTE_NORMAL;
		return attrs;
	}
}

/* Misc Win32/MSVC CRT leftovers */
#define _swab(src, dst, n) swab((src), (dst), (n))
#define _strdup strdup
static inline int DeleteFile(const char *path)
{
	return unlink(path) == 0;
}
static inline int MoveFile(const char *from, const char *to)
{
	return rename(from, to) == 0; /* <stdio.h> */
}
#include <string.h>
#define CopyMemory(dst, src, n) memcpy((dst), (src), (n))

/* Locale identity: POSIX builds have a single internal codepage (UTF-8). */
#define LOCALE_USER_DEFAULT 0x0400
#define LOCALE_IDEFAULTANSICODEPAGE 0x1004
static inline unsigned GetThreadLocale(void) { return 0; }
static inline unsigned GetOEMCP(void) { return 65001; }
static inline int GetLocaleInfo(unsigned lcid, int lctype, char *buf, int len)
{
	(void)lcid; (void)lctype; (void)buf; (void)len;
	return 0; /* callers fall back to GetACP() */
}
static inline unsigned long GetLastError(void)
{
	return 0;
}

/* diffutils' wide stat: with tchar_t == char this is plain stat() */
#define mywstat stat

/* MSVC "secure" CRT */
#define sprintf_s snprintf
#define sscanf_s sscanf
#define localtime_s(ptm, ptt) localtime_r((ptt), (ptm))

/* Win32 profile (INI) API, implemented in ports/win_ini.cpp.
   With tchar_t == char these signatures match the engine call sites. */
#ifdef __cplusplus
extern "C" {
#endif
int WritePrivateProfileString(const char *appName, const char *keyName,
                              const char *value, const char *fileName);
#ifdef __cplusplus
}
#endif
static inline int lm_ctime_s(char *buf, size_t bufsize, const time_t *t)
{
	(void)bufsize; /* ctime_r requires a buffer of at least 26 bytes */
	return ctime_r(t, buf) ? 0 : -1;
}
#define ctime_s(buf, size, t) lm_ctime_s((buf), (size), (t))

#endif /* !_WIN32 */
