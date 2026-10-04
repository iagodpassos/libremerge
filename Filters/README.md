# Preset file filters

The file filters LibreMerge ships, listed under Tools > Filters > File
Filters, and the template a new filter starts from (`FileFilter.tmpl`).

They are WinMerge's own, unchanged, from its `Filters` folder (commit
5531fb60, GPL-2.0-or-later): the two programs read the same filter format,
so a `.flt` file written for one works in the other. Their patterns use
Windows path separators (`d: \\.git$`); LibreMerge matches them against
paths written that way on every platform.

The folder is installed next to the application (`Contents/Resources/Filters`
in the macOS bundle, `share/libremerge/Filters` elsewhere). Filters a user
creates or installs go to a folder of their own.
