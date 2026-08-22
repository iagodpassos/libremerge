# Vendored code provenance

LibreMerge vendors the comparison engine of **WinMerge**.

- **Origin repository:** https://github.com/WinMerge/winmerge
- **Commit:** `5531fb6001725a1a9c0a94d9c6a99c1d3c53996e` (master, 2026-08-21)
- **License of vendored code:** GPL-2.0-or-later (original SPDX headers preserved in every file). `Externals/xdiff` is LGPL-2.1 (from git's LibXDiff fork). `Src/diffutils` is GPLv2+ (© Free Software Foundation).
- **LibreMerge as a whole:** GPL-3.0-or-later (a valid downstream license of GPL-2.0-or-later + compatible with Qt 6 / LGPLv3).

## Vendored paths (upstream layout preserved)

| Local path | Upstream path | Role |
|---|---|---|
| `engine/Src/diffutils/` | `Src/diffutils/` | GNU diff fork — line-based file diff engine |
| `engine/Externals/xdiff/` | `Externals/xdiff/` | git's xdiff (histogram/patience algorithms) |
| `engine/Src/CompareEngines/` | `Src/CompareEngines/` | Folder-compare methods (byte, binary, time/size, existence, quick) |
| `engine/Src/FilterEngine/` | `Src/FilterEngine/` | Filter expression language (re2c lexer, lemon parser) |
| `engine/Src/*.{cpp,h}` | `Src/*.{cpp,h}` | Diff orchestration: `DiffWrapper`, `DiffList`, `DirScan`, `DiffContext`, `FolderCmp`, `DirTravel`, moved-block detection, `stringdiffs`, file/line/substitution filters, `codepage_detect` |
| `engine/Src/Common/` | `Src/Common/` | Portable utilities: `unicoder`, `UnicodeString`, `UniFile`, `paths`, `OptionsMgr`/`IniOptionsMgr`, `charsets`, `TempFile` support |
| `tests/` | `Testing/GoogleTest/` | Upstream unit tests for the engine + `TestData/` |

## Deliberately excluded (and why)

- **All MFC GUI code** (`*Dlg`, `*Bar`, `*Menu`, `*View`, `*Frm`, `*Doc`, `Src/Common` widgets): MFC is Windows-only and non-redistributable. The UI is rebuilt in Qt.
- `Src/CompareEngines/ImageCompare.*`: thin wrapper around the WinIMerge Win32 DLL.
- **frhed** (hex editor): GPL-2.0-**only**, incompatible with GPLv3 distribution.
- **7-Zip `Rar*` codecs**: unRAR license restriction is GPL-incompatible (archive support will use libarchive, without RAR).
- **winwebdiff**: depends on WebView2 (proprietary, Windows-only).
- `*.vcxitems` / `*.sln`: Visual Studio build files, replaced by CMake.

## Local modification policy

- Original copyright and SPDX headers are never removed.
- Portability changes are kept as small and mechanical as possible, marked in code where non-obvious, and summarized below — so the engine can be diffed against (and eventually contributed back to) upstream.

### Modifications so far

All vendored-file changes are `_WIN32`/`_UNICODE` guards or portable fixes,
kept small so the engine stays diffable against upstream. The build also
force-includes `ports/posix_compat.h` on POSIX (MSVC/Win32 name mapping),
so most portability lives outside the vendored files entirely.

**Guarded includes/sections** (`#ifdef _WIN32` around windows.h and
Windows-only code paths): `diffutils/src/util.c`, `unicoder.cpp`,
`UniFile.cpp` (GetCompressedFileSize fallback), `OptionsMgr.cpp`,
`IniOptionsMgr.cpp`, `stringdiffs.cpp`, `DirTravel.cpp`, `TempFile.cpp`
(WMrunning via kill() on POSIX), `FolderStats.cpp` (+ POSIX readdir
branch), `DirScan.cpp` (Plugins/MergeAppCOMClass), `DiffWrapper.cpp`
(SyntaxColors include, ToWindowsPath call), `DiffContext.cpp`
(CVersionInfo), `FileFlags.cpp`, `Exceptions.h` came pre-guarded.

**POSIX additions inside vendored files:**
- `DirTravel.cpp`: POSIX `LoadFiles` (readdir + fnmatch + stat)
- `FolderStats.cpp`: POSIX `ScanFolder`
- `cio.h`: `ssize_t` alias in the POSIX branch
- `cio.cpp`: POSIX branch fixes — `tfopen_s` was misnamed `fopen_s`, and
  both open helpers returned stale errno on success (upstream bug in a
  branch that never compiled before)
- `TFile.h`: `wpath()` accessor on POSIX (returns the narrow path)
- `paths.h`: `/dev/null` as the native null device, `/` trailing slash,
  `constexpr const` fix
- `unicoder.cpp`: the conversion pivot uses `xchar_t` (UTF-16 code unit:
  `wchar_t` on Windows, `char16_t` elsewhere) with the ports-layer ICU
  API — Win32 casts packed UTF-16LE buffers to `wchar_t*`, which only
  works when `wchar_t` is 2 bytes; POSIX normalization/case-mapping route
  to ICU (`ports/normalize_icu.cpp`)
- `stringdiffs.cpp`: UTF-8 character break iterator
  (`ports/lm_utf8_break.h`) replaces the UChar-based one when tchar_t is
  char; static `linelen` renamed `linelen_dw` (collides with coretools
  under tchar_t == char)
- `Logger.h/.cpp`: `std::string` overload is `_UNICODE`-only (same
  signature as `String` otherwise)
- `UnicodeString.h`: `%lld`/`%llu` instead of MSVC `%I64d`/`%I64u` on
  POSIX; `UnicodeString.cpp` `format_arg_list` handles both truncation
  conventions and copies the va_list per attempt
- `OptionsMgr.h`: declares `GetOptionsMgr()` (upstream gets it from the
  MFC app layer)

**Portable fixes (bugs on all platforms, hidden by Win32 behavior):**
- `DirItem::SetFile`: no longer appends a trailing dot to extensionless
  filenames (Win32 strips trailing dots at open time, POSIX does not)
- `strdiff::Compare`: result normalized to -1/0/1 (libc++/libstdc++
  return the character difference; MSVC returns the sign)
- `FileFilterHelper.cpp` / `FileFilter.cpp`: filter match paths
  canonicalized with `paths::ToWindowsPath` — `.flt` regexes use `\\` as
  the separator and existing filters keep working unchanged

**Test-suite adaptations** are listed in the commit history; the most
notable is that upstream `paths_test.cpp` (Windows path semantics) is
compiled only on Windows, with `paths_posix_test.cpp` covering the POSIX
implementation.
