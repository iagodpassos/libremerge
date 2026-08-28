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
export VERSION="0.8.1"

mkdir -p "$WORK"
cd "$WORK"

cmake -S "$SRC" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target LibreMerge
rm -rf AppDir
DESTDIR="$PWD/AppDir" cmake --install build

# linuxdeploy's qt plugin does not know the client buffer integration
# category, so the wayland platform came up bufferless on GNOME
# ("Failed to load client buffer integration: wayland-egl"). Pre-place
# the plugins in the AppDir; linuxdeploy then bundles their deps and
# patches their rpaths like any other ELF it finds. vulkan-server is
# skipped to keep the Vulkan loader out of the dependency set.
QT_PLUGIN_DIR="$(/usr/bin/qmake6 -query QT_INSTALL_PLUGINS)"
mkdir -p AppDir/usr/plugins/wayland-graphics-integration-client
for plugin in libqt-plugin-wayland-egl.so libshm-emulation-server.so \
              libdrm-egl-server.so libdmabuf-server.so; do
  cp "$QT_PLUGIN_DIR/wayland-graphics-integration-client/$plugin" \
     AppDir/usr/plugins/wayland-graphics-integration-client/
done

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
# the wayland platform plugin dlopens its own helpers: client buffer
# integrations (wayland-egl), the xdg-shell integration and the window
# decorations - without them the window comes up bufferless and
# borderless on a pure Wayland session (GNOME)
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
# deploy first (generates an AppRun that sources apprun-hooks/*.sh at
# runtime), then add our hook, then pack
./linuxdeploy-"$ARCH".AppImage --appdir AppDir --plugin qt

# the bundled Qt (Debian 12's 6.4) cannot position windows on a native
# Wayland session, so dialogs land in a screen corner instead of being
# centered on the application window; prefer XWayland until the bundle
# moves to a newer Qt. QT_QPA_PLATFORM set by the user still wins.
mkdir -p AppDir/apprun-hooks
cat > AppDir/apprun-hooks/00-libremerge-platform.sh <<'EOF'
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
EOF

./linuxdeploy-"$ARCH".AppImage --appdir AppDir --output appimage

ls -la LibreMerge*.AppImage
