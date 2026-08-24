# LibreMerge

**A free, open-source differencing and merging tool for macOS (and Linux) — bringing the WinMerge experience beyond Windows.**

LibreMerge reuses the battle-tested comparison engine of [WinMerge](https://winmerge.org/) — GNU diffutils + git xdiff, folder scanning, file filters, moved-block detection, Unicode handling — and rebuilds the user interface natively with Qt 6, since the original UI is written in MFC and cannot leave Windows. If you know WinMerge, you already know LibreMerge: the layout, the colors and the workflow are intentionally the same.

📦 [Releases](https://github.com/iagodpassos/libremerge/releases) · ☕ [Buy me a coffee](https://buymeacoffee.com/iagodpassos)

![File comparison](docs/screenshots/file-compare-light.png)

## Features

- **Side-by-side file comparison and merging** (2-way and 3-way) with the classic WinMerge look: gold difference blocks, word-level highlights inside lines, gray filler keeping the panes aligned line by line
- **Free editing with live remerge**: type directly in the panes, copy differences (or all of them) in either direction, undo/redo, recompare with F5; files are saved with their original encoding, BOM and line endings preserved
- **Folder comparison** with a hierarchical tree or flat list, background scanning with progress and cancel, file masks / regex / expression filters, copy between sides and delete to Trash
- **Diff pane** showing the current difference per file, **location pane** minimap, per-pane headers and status bars (line/column, encoding, EOL)
- **CSV/TSV table comparison** as side-by-side grids: cell-level difference highlighting, delimiter auto-detection (`,` `;` tab `|`), quoted fields, first-row headers
- **Line filters** (regular expressions that mark matching differences as trivial) and **moved block detection**
- **Find and replace** in the compare panes (⌘F, wrap-around, match case)
- **Syntax highlighting** for 47 languages, via WinMerge's own parsers
- **Light and dark themes** (or follow the system), **English and Brazilian Portuguese** interface following the system language
- Drag & drop files or folders onto the window or the Dock icon; a WinMerge-style "Select Files or Folders" screen with history, read-only flags and swap
- Comparison options carried over from WinMerge: ignore whitespace/case/blank lines/EOL/numbers, diff algorithm selection (Myers, minimal, patience, histogram)

| Dark theme | Folder comparison |
| --- | --- |
| ![Dark theme](docs/screenshots/file-compare-dark.png) | ![Folder comparison](docs/screenshots/folder-compare-light.png) |

## Install (macOS)

**Requirements**: macOS 12 or later, Intel or Apple Silicon (universal binary).

Download `LibreMerge-<version>.dmg` from the [releases page](https://github.com/iagodpassos/libremerge/releases), open it and drag LibreMerge to Applications.

LibreMerge is not yet notarized by Apple, so the first launch needs one extra step: right-click the app → **Open** (or allow it under **System Settings → Privacy & Security**). If macOS still refuses ("app is damaged"), clear the download quarantine once:

```sh
xattr -d com.apple.quarantine /Applications/LibreMerge.app
```

You can also launch comparisons from the terminal:

```sh
LibreMerge left.txt right.txt          # 2-way file compare
LibreMerge base.txt mine.txt theirs.txt  # 3-way
LibreMerge dirA/ dirB/                 # folder compare
```

## Build from source (macOS / Linux)

Dependencies: a C++17 compiler, CMake ≥ 3.21, Ninja, Qt 6, POCO, ICU and Boost (headers). On macOS: `brew install cmake ninja qt poco icu4c boost googletest`. On Debian/Ubuntu see the package list in [`.github/workflows`](.github/workflows).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build        # engine test suite (369 tests)
./build/app/LibreMerge        # (on macOS: build/app/LibreMerge.app)
```

Linux binaries are not distributed yet — the application builds and the engine test suite passes on Ubuntu (see CI), and packaging (AppImage/Flatpak) is planned.

## Known limitations

- Binary files are detected and refused — binary/hex comparison is planned, not implemented
- Recompare (F5) may rebuild the alignment and clear the undo history of that moment
- 3-way comparison is available for files, not folders yet
- Archive (zip/7z) and image comparison from WinMerge are not ported yet
- Table comparison copies whole differences (no per-cell editing or undo yet)

## Support

If LibreMerge saves you time, you can [buy me a coffee](https://buymeacoffee.com/iagodpassos). ☕

## AI-assisted development

This port was built with the assistance of **Claude Fable 5**, an AI model by [Anthropic](https://www.anthropic.com): the engine portability layer, the Qt interface and the release tooling came out of AI-assisted pair-programming sessions. Everything that ships is validated against WinMerge's own engine test suite (369 tests) and an application selftest suite, on macOS and Linux CI.

## Relationship to WinMerge, license

LibreMerge is an independent port based on the comparison engine of [WinMerge](https://winmerge.org/), © Dean P. Grimm / Thingamahoochie Software and the WinMerge contributors, licensed GPL-2.0-or-later. Vendored engine sources keep their original headers; the port layer and the Qt application are new code. See [docs/VENDORED.md](docs/VENDORED.md) for the exact provenance and local-change policy.

LibreMerge as a whole is distributed under the **GNU GPL v3.0 or later** ([LICENSE](LICENSE)).

**LibreMerge is not affiliated with or endorsed by the WinMerge project.** The name and icon are original to this project.

## Roadmap

Binary/hex compare, archive support, 3-way folders, HTML reports, notarized builds, Linux packages (AUR first), and — if it proves itself — offering the portable engine fixes back to WinMerge upstream ([WinMerge/winmerge#141](https://github.com/WinMerge/winmerge/issues/141) tracks the idea of a cross-platform UI).

Issues and pull requests are welcome.
