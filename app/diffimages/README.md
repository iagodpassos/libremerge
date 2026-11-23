# WinMerge's item icons

The icons of the folder comparison's items are WinMerge's own, from
[`Src/res`](https://github.com/WinMerge/winmerge/tree/700da377c/Src/res) of the
WinMerge repository at commit `700da377c`, the one the engine is synced to
(GPL-2.0-or-later, as the engine LibreMerge takes from WinMerge). Each `.ico`
file is here as one PNG per size it holds (`<name>-<size>.png`, 16 to 48
pixels), converted without change from the size's image of the most colors
(32-bit with alpha, or 8-bit where WinMerge has no more), so that no image
format plugin is needed to read them. `lfolder.ico` keeps its 24-pixel image
in a 24×25 bitmap whose last row is empty: the PNG has its first 24 rows.

The names are WinMerge's and so is the list, every image of
`DiffImageListUtils::InitializeDiffImageList` but the web page ones;
which image an item gets follows `GetColImage` (`DirActions.cpp`), see
`lm::DiffImage` in `app/Icons.h`.
