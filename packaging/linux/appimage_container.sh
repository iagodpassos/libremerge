#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs INSIDE a Debian 12 container (see packaging/appimage_in_docker.sh):
# /src is the source tree (read-only), /qt holds the Qt build_qt.sh made,
# /out receives the AppImage. Builds the AppImage, then runs the selftest
# suite and the desktop portal checks against the AppImage itself.
set -euo pipefail

ARCH="$(uname -m)"
QT_TARBALL="$(ls /qt/qt-*-"$ARCH".tar.zst | tail -1)"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq > /dev/null
# the app's build dependencies, and the libraries the Qt build links
# (their -dev packages bring them, and the headers Qt's CMake packages
# look for)
apt-get install -y -qq --no-install-recommends \
  g++ ninja-build cmake pkg-config curl ca-certificates file patchelf \
  binutils desktop-file-utils zstd xz-utils \
  libpoco-dev libboost-dev libicu-dev re2c lemon \
  zlib1g-dev libbz2-dev liblzma-dev libzstd-dev \
  libgl-dev libegl-dev libgles-dev libdrm-dev libgbm-dev \
  libfontconfig-dev libfreetype-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev \
  libxrender-dev libsm-dev libice-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
  libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
  libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
  libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
  libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev \
  libxkbcommon-x11-dev libwayland-dev \
  libdbus-1-dev libglib2.0-dev libpcre2-dev libdouble-conversion-dev \
  libpng-dev libjpeg-dev \
  dbus python3-dbus python3-gi > /dev/null

mkdir -p /opt
zstd -dc "$QT_TARBALL" | tar -C /opt -xf -
export QT_PREFIX=/opt/qt

bash /src/packaging/make_appimage.sh /src /tmp/work > /tmp/build.log 2>&1 \
  || { tail -40 /tmp/build.log; exit 1; }
APPIMAGE="$(ls /tmp/work/LibreMerge-*-"$ARCH".AppImage)"

# the AppImage as a user gets it, extracted (no FUSE in a container; the
# magic bytes zeroed so binfmt emulators run it)
cp "$APPIMAGE" /tmp/validate.AppImage
dd if=/dev/zero of=/tmp/validate.AppImage bs=1 count=3 seek=8 conv=notrunc status=none
chmod +x /tmp/validate.AppImage
(cd /tmp && ./validate.AppImage --appimage-extract > /dev/null)
export QT_QPA_PLATFORM=offscreen
# the data folder a desktop session's home always has: Qt 6.12's
# moveToTrash creates the Trash folder in it, not the folder itself, and
# the copies that overwrite go through the trash
mkdir -p "$HOME/.local/share"
bash /src/packaging/run_selftests.sh /tmp/squashfs-root/AppRun 2>&1 | grep -v "^ok: " || true
/tmp/squashfs-root/AppRun --version
for mode in "" --legacy; do
  dbus-run-session -- python3 /src/packaging/fake_portal.py $mode -- \
    /tmp/squashfs-root/AppRun --selftest-portal 2>/dev/null | tr '\n' ' '
  echo "portal $mode exit ${PIPESTATUS[0]}"
done
cp "$APPIMAGE" /out/
echo "BUILT $(basename "$APPIMAGE")"
