# LibreMerge

**A free, open-source differencing and merging tool for macOS (and, in the future, Linux) — bringing the WinMerge experience beyond Windows.**

LibreMerge reuses the battle-tested comparison engine of [WinMerge](https://winmerge.org/) (file diff, folder diff, filters, moved-block detection) and rebuilds the user interface with Qt 6, since the original UI is written in MFC and cannot leave Windows.

> **Status: early development — Phase 0.**
> The current goal is to extract WinMerge's comparison engine into a portable,
> CMake-built library (`libremerge-engine`) that compiles and passes the
> upstream unit tests on macOS (arm64) and Linux. No usable GUI exists yet.

## Why

There is no macOS tool — at any price — that occupies the quadrant WinMerge owns on Windows: **native, broad-scope (text/folder/table/hex/image/archives), and free**. The free/open-source segment on macOS is actually shrinking. LibreMerge aims to fill that gap.

## Roadmap

1. **Phase 0 — Engine**: portable `libremerge-engine` library (diff engine, folder scan, filters), validated by the upstream GoogleTest suite, CI on macOS + Linux. *(in progress)*
2. **Phase 1 — MVP**: Qt 6 GUI with 2-way text compare/merge and 2-way folder compare with filters. Distributed as a notarized `.dmg` and a Homebrew cask.
3. **Phase 2 — Gap killers**: CSV/TSV table compare, hex compare, archive support, 3-way merge, moved-block UI, tree-sitter syntax highlighting, line/substitution filters, HTML reports and patches, Finder integration, Linux packaging.

Note: being GPL software, LibreMerge will **not** be distributed through the Mac App Store (the store's terms are incompatible with the GPL). Distribution channels are GitHub Releases and Homebrew.

## License and heritage

LibreMerge is licensed under the **GNU General Public License v3.0 or later** (see [LICENSE](LICENSE)).

It is a derivative work of **WinMerge**, © 1996–2026 Dean P. Grimm / Thingamahoochie Software and the WinMerge contributors, licensed under GPL-2.0-or-later. Original SPDX headers are preserved in all vendored files; see [docs/VENDORED.md](docs/VENDORED.md) for the exact origin (repository, commit) and the list of local modifications.

**LibreMerge is not affiliated with or endorsed by the WinMerge project.** "WinMerge" and the WinMerge logo belong to the WinMerge project; LibreMerge uses neither.
