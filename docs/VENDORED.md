# Vendored code provenance

LibreMerge vendors the comparison engine of **WinMerge**.

- **Origin repository:** https://github.com/WinMerge/winmerge
- **Commit:** `94a7a3fc5efee0f8b45d08a354299453509467b5` (master, 2026-10-17)
- **License of vendored code:** GPL-2.0-or-later (original SPDX headers preserved in every file). `Externals/xdiff` is LGPL-2.1 (from git's LibXDiff fork). `Src/diffutils` is GPLv2+ (© Free Software Foundation).
- **LibreMerge as a whole:** GPL-3.0-or-later (a valid downstream license of GPL-2.0-or-later + compatible with Qt 6 / LGPLv3).

## Vendored paths (upstream layout preserved)

| Local path | Upstream path | Role |
|---|---|---|
| `engine/Src/diffutils/` | `Src/diffutils/` | GNU diff fork — line-based file diff engine |
| `engine/Externals/xdiff/` | `Externals/xdiff/` | git's xdiff (histogram/patience algorithms) |
| `engine/Src/CompareEngines/` | `Src/CompareEngines/` | Folder-compare methods (byte, binary, time/size, existence, quick) |
| `engine/Src/FilterEngine/` | `Src/FilterEngine/` | Filter expression language (re2c lexer, lemon parser) |
| `engine/Src/*.{cpp,h}` | `Src/*.{cpp,h}` | Diff orchestration: `DiffWrapper`, `DiffList`, `DirScan`, `DiffContext`, `FolderCmp`, `DirTravel`, moved-block detection, `stringdiffs`, file/line/substitution filters, `codepage_detect`, `TableProps` (the table layout `DiffWrapper` takes), `TreeSitterParser` (Tree-sitter highlighting and comment detection) |
| `engine/Src/Common/` | `Src/Common/` | Portable utilities: `unicoder`, `UnicodeString`, `UniFile`, `paths`, `OptionsMgr`/`IniOptionsMgr`, `charsets`, `TempFile` support |
| `tests/` | `Testing/GoogleTest/` | Upstream unit tests for the engine + `TestData/` |
| `engine/Externals/tree-sitter/` | `Externals/tree-sitter/` | [Tree-sitter](https://github.com/tree-sitter/tree-sitter)'s runtime, 0.25.10, as WinMerge vendors it (`lib.c` builds it) — MIT, its Unicode headers under ICU's license |
| `engine/Externals/winimerge/src/WinIMergeLib/` | [WinMerge/winimerge](https://github.com/WinMerge/winimerge) `src/WinIMergeLib/` @ `da639cdf` (the commit WinMerge pins) | Image-compare core: `ImgDiffBuffer.hpp` (block diff, overlay, blink, wipe), `ImgMergeBuffer.hpp` (copy diff, undo, save), `Diff.hpp` (embedded xdiff for insertion/deletion alignment) — GPL-2.0-or-later |

The WinIMerge image wrapper (`image.hpp`, FreeImagePlus-based) and
`ImgConverter.hpp` (WIC/Direct2D/GDI+, Windows-only) are **not** vendored;
`engine/ports/imgmerge/` reimplements the same API over `QImage` (FreeImage
is unmaintained as a system package — Homebrew deprecates it in 2027 over
open CVEs). The vendored algorithm headers therefore run on identical BGRA
scanlines and produce the same diffs; format support follows Qt's decoders
instead of FreeImage's (writing GIF and multi-page files back is not
supported, unlike WinMerge).

## Tree-sitter grammars

WinMerge colors files through Tree-sitter too, in the order its highlight
mode sets (Options > Editor; by default "Built-in first", Tree-sitter for
the languages its built-in parsers lack), with grammars built apart, in
[WinMerge/tree-sitter-grammars](https://github.com/WinMerge/tree-sitter-grammars)
(pinned at `f3b6918a`, release 0.2.1). LibreMerge builds the same
grammars from the same commits, downloaded at configure time with their
SHA-256 checked (`engine/TreeSitterGrammars.cmake`), as modules that the
engine's `TreeSitterRegistry` loads from a `TreeSitterGrammars` folder,
and bundles their queries as that repository's `build-grammars.ps1` does:
the bundled `.scm` files match the ones of WinMerge's release 0.2.1 byte
for byte, line endings aside.

| Grammar repository | Version | Grammars built | License |
|---|---|---|---|
| ionide/tree-sitter-fsharp | 0.3.12 | `fsharp`, `fsharp_signature` | MIT |
| justinmk/tree-sitter-ini | v1.4.0 | `ini` | Apache-2.0 |
| tree-sitter-grammars/tree-sitter-markdown | v0.5.3 | `markdown`, `markdown_inline` | MIT |
| tree-sitter-grammars/tree-sitter-properties | v0.3.0 | `properties` | MIT |
| tree-sitter-grammars/tree-sitter-toml | v0.7.0 | `toml` | MIT |
| tree-sitter-grammars/tree-sitter-xml | v0.7.0 | `xml`, `dtd` | MIT |
| tree-sitter-grammars/tree-sitter-yaml | v0.7.2 | `yaml` | MIT |
| tree-sitter/tree-sitter-bash | v0.25.1 | `bash` | MIT |
| tree-sitter/tree-sitter-cpp | v0.23.4 | `cpp` | MIT |
| tree-sitter/tree-sitter-c-sharp | v0.23.5 | `c-sharp` | MIT |
| tree-sitter/tree-sitter-css | v0.25.0 | `css` | MIT |
| tree-sitter/tree-sitter-c | v0.24.2 | `c` | MIT |
| tree-sitter/tree-sitter-go | v0.25.0 | `go` | MIT |
| tree-sitter/tree-sitter-html | v0.23.2 | `html` | MIT |
| tree-sitter/tree-sitter-javascript | v0.25.0 | `javascript` | MIT |
| tree-sitter/tree-sitter-java | v0.23.5 | `java` | MIT |
| tree-sitter/tree-sitter-json | v0.24.8 | `json` | MIT |
| tree-sitter/tree-sitter-php | v0.25.0 | `php`, `php_only` | MIT |
| tree-sitter/tree-sitter-python | v0.25.0 | `python` | MIT |
| tree-sitter/tree-sitter-ruby | v0.23.1 | `ruby` | MIT |
| tree-sitter/tree-sitter-rust | v0.24.2 | `rust` | MIT |
| tree-sitter/tree-sitter-typescript | v0.23.2 | `typescript`, `tsx` | MIT |

The license of each repository, and the runtime's, travel in
`TreeSitterGrammars/licenses`. tree-sitter-typescript's `flow` grammar
stays out: WinMerge builds it from the `tsx` sources, so its module holds
`tree_sitter_tsx` and never loads. Like `build-grammars.ps1`, the build
puts WinMerge's own queries for C and C++ in place of the bundled ones:
`c-highlights.scm` and `cpp-highlights.scm` of that repository's
`custom_queries`, written by WinMerge's maintainer (the repository states
no license), kept in `engine/Externals/tree-sitter-grammars/custom_queries`.

## Translations

LibreMerge mirrors WinMerge's interface, so its translations take
WinMerge's wording wherever WinMerge has the text. They come from the
catalogs of the same commit, `Translations/WinMerge/*.po`, GPL like the
rest:

| Local file | Upstream catalog | Upstream maintainer and translators |
|---|---|---|
| `app/i18n/libremerge_ca.ts` | `Catalan.po` | Jordi Vilar, Pere Orga |
| `app/i18n/libremerge_de.ts` | `German.po` | Tim Gerundt; René Nicolaus, Winfried Peter, Jörg Schneider |
| `app/i18n/libremerge_es.ts` | `Spanish.po` | Dean Grimm, Jesús M. Delgado 'MacK', Mario Angel, Nelson Ariza, P0rsche-911 |
| `app/i18n/libremerge_fr.ts` | `French.po` | Lolo S.; Gil Andre, Laurent Ganier, Dominique Saussereau, Jean-F Jolin, Need74, t3chnob0y |
| `app/i18n/libremerge_pt_BR.ts` | `Brazilian.po` | as listed in the catalog's header |

In the files built from WinMerge's catalogs (German, Spanish, Catalan,
French)
every text that is not WinMerge's as it stands carries a translator
comment, which Qt Linguist shows next to it:

- `After WinMerge's "…".` marks WinMerge's translation of that text,
  fitted to a LibreMerge label that differs from it in the accelerator,
  the ending or the shortcut, or naming LibreMerge where WinMerge's
  translators named their application; a few accelerators moved to a
  letter that is free in the LibreMerge menu or dialog the label sits
  in;
- `LibreMerge's own text: not in WinMerge's catalog.` marks a text
  translated for LibreMerge, in WinMerge's vocabulary. These are the
  ones a native speaker's review matters most for;
- `WinMerge's catalog leaves this text in English: translated for
  LibreMerge.` marks a text WinMerge has but its translators have not
  reached. The Catalan catalog has many of them;
- `Changed in a native speaker's review; WinMerge's is "…".` marks a
  text of WinMerge's that a native speaker's review replaced, and an own
  text a review reworded says so in its note.

| Catalog | WinMerge's, unchanged | Fitted | LibreMerge's own | Left in English upstream | Changed in review |
|---|---|---|---|---|---|
| German | 623 | 165 | 245 | 0 | 3 |
| Spanish | 611 | 179 | 243 | 3 | 0 |
| Catalan | 309 | 174 | 245 | 308 | 0 |
| French | 627 | 182 | 227 | 0 | 0 |

The texts WinMerge's translators wrote stay as they are, the odd ones
included: a text follows WinMerge's wording until a review says
otherwise. The German catalog was reviewed by @KrokusPokus in issue #8:
the Defaults button of the Options takes "Voreinstellungen", KDE's word
and Qt's own for the button that restores defaults; the hint of the
path fields on the selection screen became shorter in every language;
and File > Recent Files or Folders reads "Kürzlich verwendet" (in the
menu of the path bar over the panes too), where WinMerge's "Kürzliche
Dateien oder Ordner" reads oddly, the Shift+Delete hint of the path
fields likewise "kürzlich verwendeten Pfad". In the binary comparison's
own dialogs, the values at the caret read "Anzeige der Werte an der
Cursorposition:" and the font size "Schriftgröße in Punkten:".

The French catalog keeps WinMerge's French typography in LibreMerge's own
texts too: a space before a colon or a question mark, and the panes
named Gauche, Milieu and Droite. WinMerge's French gives no accelerator
to the six Compare 3-way items of the View menu, to "Go to Line", to the
search's "Don't wrap end of file" and to the Table page's delimiter, and
gives the two Copy All items of the Merge menu the same letter, so these
take one here.

## WinMerge behaviors corrected in LibreMerge's own code

Parts of WinMerge's editor are written again in `app/`, after upstream's
code. Where that code has an evident slip, the rewrite corrects it and
says so in a comment:

- The tab size of Options > Editor reaches the open comparisons as the
  dialog is accepted. Upstream sets it only as files load
  (`CMergeEditView::DocumentsLoaded`), so they kept the old size until
  reloaded.
- Tab with "Insert spaces" (`CCrystalEditView::OnEditTab`) inserts spaces
  up to the next tab stop from the cursor's column. Upstream counts from
  the cursor's character, which a tab before the cursor puts out of step.
- Shift+Tab without a selection (`OnEditUntab`) goes back to the tab stop
  before the cursor counting columns as the cursor's column is counted.
  Upstream counts one per character there, so a wide character left the
  cursor off the tab stop.

Where upstream falls back on Windows' line ending, LibreMerge falls back
on the one of macOS and Linux:

- A file without a line break and an untitled pane end their lines as
  Unix does; upstream's take Windows' (`CRLFSTYLE::DOS`). This decides
  too whether such a file is asked about as one whose lines end otherwise
  (`IDS_SUGGEST_IGNOREEOL`).
- In a file whose lines end in more than one way, a line typed in ends as
  the line it was split from, or as most of the file's lines; upstream's
  take Windows' ending (`GetDefaultEol` of `CRLFSTYLE::MIXED`).

## Deliberately excluded (and why)

- **All MFC GUI code** (`*Dlg`, `*Bar`, `*Menu`, `*View`, `*Frm`, `*Doc`, `Src/Common` widgets): MFC is Windows-only and non-redistributable. The UI is rebuilt in Qt.
- `Src/CompareEngines/ImageCompare.cpp`: thin wrapper around the WinIMerge Win32 DLL. LibreMerge keeps the engine Qt-free with a hook (`ports/image_compare_stub.cpp`) that the Qt app fills in with the ported image core at startup.
- `Src/FileTransform.cpp`: the plugins it runs are COM/ActiveX, Windows-only. `ports/file_transform_posix.cpp` carries over its pipeline parsing and its line expressions (the `le:` items, which a table comparison's Ignore This Column puts in the prediffer), and refuses a plugin by name.
- WinMerge/tree-sitter-grammars' `build-grammars.ps1` (MSVC and PowerShell): the grammars build with CMake instead (see Tree-sitter grammars above).
- `Src/TableProps.cpp`: it reads the Compare > Table patterns from WinMerge's options; LibreMerge keeps that page's settings in the Qt app, which answers `MakeTablePropertiesByFileName` through a hook (`ports/table_props_posix.cpp`).
- **frhed** (hex editor): GPL-2.0-**only**, incompatible with GPLv3 distribution. The binary comparison (`app/HexView.*`) is LibreMerge's own editor, written after the behavior of the heksedit control WinMerge embeds without taking any of its code; WinMerge's own `CHexMergeDoc`, `CHexMergeView` and `CHexMergeFrame` (GPL-2.0-or-later) are what `app/HexCompareView.*` follows.
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
`stringdiffs.cpp`, `DirTravel.cpp`, `TempFile.cpp`
(WMrunning via kill() on POSIX), `FolderStats.cpp` (+ POSIX readdir
branch), `DirScan.cpp` (Plugins/MergeAppCOMClass), `DiffWrapper.cpp`
(SyntaxColors include, ToWindowsPath call), `DiffContext.cpp`
(CVersionInfo), `FileFlags.cpp`, `Exceptions.h` came pre-guarded.

**winimerge (`engine/Externals/winimerge`):**
- `ImgDiffBuffer.hpp` (`DataForDiff`): `LM_IMAGE_SCANLINES_TOPDOWN` branch —
  the Qt image backend stores scanlines top-down and contiguous, so
  `reverse()` is a no-op and `data()` starts at row 0 (FreeImage stores
  bottom-up, which is what the original flip-then-walk trick compensated)
- `Diff.hpp` (`xdl_prepare_ctx`): the `const Data& data` declaration moved
  above the first `goto` — Clang/GCC reject a jump that bypasses the
  initialization of a reference; MSVC accepted it

**POSIX additions inside vendored files:**
- `DirTravel.cpp`: POSIX `LoadFiles` (readdir + fnmatch + stat)
- `FolderStats.cpp`: POSIX `ScanFolder`
- `cio.h`: `ssize_t` alias in the POSIX branch
- `cio.cpp`: POSIX branch fixes — `tfopen_s` was misnamed `fopen_s`, and
  both open helpers returned stale errno on success (upstream bug in a
  branch that never compiled before)
- `TFile.h`: `wpath()` accessor on POSIX (returns the narrow path)
- `DirScan.cpp`: the compare `ThreadPool` asks for an 8 MiB stack on
  POSIX. Upstream's workers get Windows' 1 MiB default, and
  `BinaryCompare` keeps two 256 KiB read buffers on the stack; macOS
  gives secondary threads 512 KiB, so on Intel Macs any folder compare
  reaching `BinaryCompare` overflowed (arm64 had about 12 KiB to spare)
- `diffutils/src/util.c` (`message5`): no console output on POSIX.
  diff(1)'s messages, such as "Binary files ... differ" for every binary
  pair of a folder compare, went nowhere in WinMerge's windowed app but
  reach the terminal a POSIX app was started from
- `paths.h`: `/dev/null` as the native null device, `/` trailing slash,
  `constexpr const` fix
- `unicoder.cpp`: the conversion pivot uses `xchar_t` (UTF-16 code unit:
  `wchar_t` on Windows, `char16_t` elsewhere) with the ports-layer ICU
  API — Win32 casts packed UTF-16LE buffers to `wchar_t*`, which only
  works when `wchar_t` is 2 bytes; POSIX normalization/case-mapping route
  to ICU (`ports/normalize_icu.cpp`)
- `stringdiffs.cpp`: UTF-8 character break iterator
  (`ports/lm_utf8_break.h`) replaces the UChar-based one when tchar_t is
  char
- `DiffWrapper.cpp`: static `linelen` renamed `linelen_dw` (collides with
  coretools under tchar_t == char)
- `Logger.h/.cpp`: `std::string` overload is `_UNICODE`-only (same
  signature as `String` otherwise)
- `UnicodeString.h`: `%lld`/`%llu` instead of MSVC `%I64d`/`%I64u` on
  POSIX; `UnicodeString.cpp` `format_arg_list` handles both truncation
  conventions and copies the va_list per attempt
- `OptionsMgr.h`: declares `GetOptionsMgr()` (upstream gets it from the
  MFC app layer)

**`TreeSitterParser.{h,cpp}`** (WinMerge's Tree-sitter bridge): the wide
strings become `String`, `tchar_t` and `_T()` (the same types on Windows),
so the text goes to Tree-sitter in the editor's encoding, UTF-8 here, and
its byte offsets count `sizeof(tchar_t)` per character; on POSIX the
grammar modules are `tree-sitter-<language>.so`, loaded with `dlopen()`,
the folder is listed with `std::filesystem` and the default one is next
to the program (`env::GetProgPath()`). Three things it does unlike the
Tree-sitter hosts its queries are written for are corrected (marked
`LibreMerge:` in the code):
- the queries' text predicates (`#eq?`, `#match?`, `#any-of?`, with their
  `not-` and `any-` forms) are checked, so a pattern colors only the nodes
  it names; upstream reads `#set!` alone, and javascript's rule for
  `require` gave every TypeScript identifier the function color
- of nested captures the inner one colors, and where it ends the outer
  one's color comes back, across an injection too; upstream let the higher
  color win where captures start together and gave back normal text, so
  only `a` of a properties key `a.b` took the key's color, and the text of
  a TOML multi-line string took its key's
- a chunk of the line cache writes only its own lines; upstream wrote a
  capture's every line, leaving unsorted blocks in a chunk built before

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
