# WinMerge's item icons

The icons of the folder comparison's items are WinMerge's own, from
[`Src/res`](https://github.com/WinMerge/winmerge/tree/master/Src/res) of the
WinMerge repository (GPL-2.0-or-later, as the engine LibreMerge takes from
WinMerge). Each `.ico` file is here as one PNG per size it holds
(`<name>-<size>.png`, 16 to 48 pixels), converted without change from the
size's image of the most colors (32-bit with alpha, or 8-bit where WinMerge
has no more), so that no image format plugin is needed to read them. The names are WinMerge's; which
image an item gets follows `GetColImage` (`DirActions.cpp`), see
`lm::DiffImage` in `app/Icons.h`.
