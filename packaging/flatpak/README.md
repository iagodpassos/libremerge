# Flatpak

`io.github.iagodpassos.libremerge.yml` builds LibreMerge on the KDE runtime
(Qt 6, ICU and libarchive come from it; POCO is bundled; Boost, re2c and
lemon are only there for the build).

Build and install for the current user, from this folder:

```sh
flatpak install flathub org.kde.Platform//6.11 org.kde.Sdk//6.11 org.flatpak.Builder
flatpak run org.flatpak.Builder --user --install --force-clean build-dir io.github.iagodpassos.libremerge.yml
flatpak run io.github.iagodpassos.libremerge
```

Flathub's linter:

```sh
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest io.github.iagodpassos.libremerge.yml
```

On a release: bump `tag` and `commit` of the `libremerge` module, and copy
any change of `engine/TreeSitterGrammars.cmake` to the grammar sources (the
build takes the archives from them, as it has no network).

As git's diff tool, the command is
`flatpak run io.github.iagodpassos.libremerge`; the manifest shares `/tmp`
for the files git hands over:

```sh
git difftool --extcmd="flatpak run io.github.iagodpassos.libremerge"
```

Inside the sandbox, deleting to the Trash and the copies that write over a
file go through the desktop's Trash portal (`org.freedesktop.portal.Trash`),
which takes the file opened by LibreMerge: a symbolic link, or a file it
cannot open for writing, is not sent there.
