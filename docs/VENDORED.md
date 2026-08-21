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

- (none yet — pristine copy of upstream)
