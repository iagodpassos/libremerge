#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the Qt the AppImage ships, from Qt's own sources, INSIDE a Debian 12
# (bookworm) container:
#
#   docker run --rm --platform linux/<arm64|amd64> \
#     -v "$PWD/packaging/linux/build_qt.sh":/build_qt.sh:ro \
#     -v "$PWD/deps/linux":/out debian:12 bash /build_qt.sh /out
#
# Leaves /out/qt-<version>-<arch>.tar.zst, a Qt prefix for /opt/qt that
# packaging/appimage_in_docker.sh unpacks. It only has to run again for
# another Qt version.
#
# Why not Qt's official binaries: they are built on Ubuntu 24.04 and need
# glibc 2.38 (C23's __isoc23_strtol and friends), which would leave Ubuntu
# 22.04, Debian 12 and AppImageHub's test behind. Built on bookworm, a
# reference platform of Qt 6.12, Qt needs no newer glibc than the rest of
# the AppImage, and it links bookworm's ICU, the one the engine uses, so
# the bundle carries one ICU instead of two.
#
# What LibreMerge uses: qtbase (Core, Gui, Widgets, Concurrent, DBus, the
# xcb and Wayland platforms) and qtimageformats. No GTK theme: the
# AppImage has never carried one, and with it GTK itself would come in.
# Qt's own strings (dialog buttons, text-field menus) are compiled from
# qttranslations with bookworm's lrelease: the .ts format has not changed.
set -euo pipefail

OUT="${1:?usage: build_qt.sh <out-dir>}"
QT_VER="6.12.0"
PREFIX="/opt/qt"
ARCH="$(uname -m)"
BASE="https://download.qt.io/official_releases/qt/${QT_VER%.*}/$QT_VER/submodules"
declare -A SHA256=(
  [qtbase]="a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307"
  [qtimageformats]="0943132b5db8de6b6a7f1d162682173e8763c7ed94e80fd39113292845f67762"
  [qttranslations]="85929c0c30d6f273f23bd879bb69803ea17b010ab73cab2f8abf95357f6f6bbb"
)

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq > /dev/null
apt-get install -y -qq --no-install-recommends \
  build-essential cmake ninja-build pkg-config perl python3 \
  curl ca-certificates xz-utils zstd file \
  libgl-dev libegl-dev libgles-dev libdrm-dev libgbm-dev \
  libfontconfig-dev libfreetype-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev \
  libxrender-dev libsm-dev libice-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
  libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
  libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev \
  libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
  libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev \
  libxkbcommon-x11-dev \
  libwayland-dev libwayland-bin wayland-protocols \
  libdbus-1-dev libglib2.0-dev \
  libicu-dev zlib1g-dev libpng-dev libjpeg-dev libzstd-dev \
  libpcre2-dev libdouble-conversion-dev libssl-dev \
  qt6-l10n-tools > /dev/null

# a compiler process of Qt's can take a gigabyte (the precompiled headers):
# one per gigabyte of the container's memory, so that none is killed
MEM_GB="$(awk '/MemTotal/ {print int($2 / 1048576)}' /proc/meminfo)"
JOBS="${QT_JOBS:-$(( MEM_GB < $(nproc) ? MEM_GB : $(nproc) ))}"
[ "$JOBS" -ge 1 ] || JOBS=1

WORK="$(mktemp -d)"
cd "$WORK"
for module in qtbase qtimageformats qttranslations; do
  tarball="$module-everywhere-src-$QT_VER.tar.xz"
  curl -fsSL -o "$tarball" "$BASE/$tarball"
  echo "${SHA256[$module]}  $tarball" | sha256sum -c --quiet
  tar xf "$tarball"
done

cmake -S "qtbase-everywhere-src-$QT_VER" -B qtbase-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_BENCHMARKS=OFF \
  -DFEATURE_sql=OFF \
  -DFEATURE_xcb=ON -DFEATURE_wayland=ON -DFEATURE_gtk3=OFF \
  -DFEATURE_dbus=ON -DFEATURE_icu=ON -DFEATURE_fontconfig=ON \
  > qtbase-configure.log
# the platforms and themes the AppImage needs: fail here, not at runtime
for feature in xcb wayland dbus icu fontconfig xkbcommon; do
  if ! grep -q "^FEATURE_$feature:BOOL=ON" qtbase-build/CMakeCache.txt; then
    echo "ERROR: qtbase configured without $feature" >&2
    tail -40 qtbase-configure.log >&2
    exit 1
  fi
done
cmake --build qtbase-build --parallel "$JOBS"
cmake --install qtbase-build > /dev/null

"$PREFIX/bin/qt-cmake" -S "qtimageformats-everywhere-src-$QT_VER" \
  -B qtimageformats-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF > /dev/null
cmake --build qtimageformats-build --parallel "$JOBS"
cmake --install qtimageformats-build > /dev/null

mkdir -p "$PREFIX/translations"
for ts in "qttranslations-everywhere-src-$QT_VER"/translations/qtbase_*.ts; do
  /usr/lib/qt6/bin/lrelease -silent "$ts" \
    -qm "$PREFIX/translations/$(basename "$ts" .ts).qm"
done

tar -C "$(dirname "$PREFIX")" -cf - "$(basename "$PREFIX")" \
  | zstd -q -19 -T0 -o "$OUT/qt-$QT_VER-$ARCH.tar.zst"
ls -la "$OUT/qt-$QT_VER-$ARCH.tar.zst"
