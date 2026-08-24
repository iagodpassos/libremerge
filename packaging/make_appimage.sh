#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a self-contained Linux AppImage. Meant to run INSIDE a Debian 12
# (bookworm) container/system - the oldest base we build on, which sets
# the glibc floor of the resulting AppImage.
#
#   packaging/make_appimage.sh <source-dir> <work-dir>
#
# Produces <work-dir>/LibreMerge-<version>-<arch>.AppImage with Qt, ICU,
# Poco and the wayland+xcb platform plugins bundled (the .deb lesson:
# platform plugins are dlopen'd and easy to miss).
set -euo pipefail

SRC="${1:?usage: make_appimage.sh <source-dir> <work-dir>}"
WORK="${2:?usage: make_appimage.sh <source-dir> <work-dir>}"
ARCH="$(uname -m)"
export VERSION="0.7.3"

mkdir -p "$WORK"
cd "$WORK"

cmake -S "$SRC" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target LibreMerge
rm -rf AppDir
DESTDIR="$PWD/AppDir" cmake --install build

for tool in linuxdeploy linuxdeploy-plugin-qt; do
  if [ ! -x "$tool-$ARCH.AppImage" ]; then
    curl -fsSL -o "$tool-$ARCH.AppImage" \
      "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$ARCH.AppImage"
    # zero the AppImage magic at ELF offset 8: binfmt emulators
    # (Docker's Rosetta/QEMU) refuse to exec binaries carrying it
    dd if=/dev/zero of="$tool-$ARCH.AppImage" bs=1 count=3 seek=8 \
      conv=notrunc status=none
    chmod +x "$tool-$ARCH.AppImage"
  fi
done

# no FUSE inside containers; qmake6 is Debian's Qt 6 qmake
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake6
# wayland+xcb for real sessions, offscreen so the selftest suite can run
# headless against the AppImage itself (CI, containers)
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so"
./linuxdeploy-"$ARCH".AppImage --appdir AppDir --plugin qt --output appimage

ls -la LibreMerge*.AppImage
