#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a self-contained Linux AppImage. Meant to run INSIDE a Debian 12
# (bookworm) container/system. Its glibc is 2.36, but nothing bundled may
# use a symbol newer than 2.35, so the AppImage runs on Ubuntu 22.04 (the
# oldest LTS AppImageHub tests on); the build checks that at the end.
#
#   packaging/make_appimage.sh <source-dir> <work-dir>
#
# Produces <work-dir>/LibreMerge-<version>-<arch>.AppImage with Qt, ICU,
# Poco, libarchive and the wayland+xcb platform plugins bundled (the .deb
# lesson: platform plugins are dlopen'd and easy to miss), plus Qt's own
# translations. Besides the app's build dependencies the container needs
# qt6-translations-l10n, and zlib1g-dev, libbz2-dev, liblzma-dev and
# libzstd-dev for libarchive (built here, see below).
set -euo pipefail

SRC="${1:?usage: make_appimage.sh <source-dir> <work-dir>}"
WORK="${2:?usage: make_appimage.sh <source-dir> <work-dir>}"
ARCH="$(uname -m)"
export VERSION="0.9.9"

mkdir -p "$WORK"
cd "$WORK"

# libarchive from source rather than Debian's: bookworm's libarchive13
# calls arc4random_buf, new in glibc 2.36 and the only symbol in the whole
# bundle above 2.35, which kept the AppImage off Ubuntu 22.04 and failed
# AppImageHub's test. Without it libarchive uses its own generator
# (archive_random.c). Same version and options as the dmg (build_deps.sh).
LIBARCHIVE_VER="3.8.1"
LIBARCHIVE_SHA256="bde832a5e3344dc723cfe9cc37f8e54bde04565bfe6f136bc1bd31ab352e9fab"
LIBARCHIVE="$PWD/libarchive"
if [ ! -f "$LIBARCHIVE/lib/libarchive.so" ]; then
  curl -fsSL -o libarchive.tar.gz \
    "https://github.com/libarchive/libarchive/releases/download/v$LIBARCHIVE_VER/libarchive-$LIBARCHIVE_VER.tar.gz"
  echo "$LIBARCHIVE_SHA256  libarchive.tar.gz" | sha256sum -c --quiet
  tar xzf libarchive.tar.gz
  cmake -S "libarchive-$LIBARCHIVE_VER" -B libarchive-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$LIBARCHIVE" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DHAVE_ARC4RANDOM_BUF=OFF \
    -DENABLE_OPENSSL=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF \
    -DENABLE_LZ4=OFF -DENABLE_LIBB2=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_NETTLE=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF \
    -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF \
    -DENABLE_ACL=OFF > /dev/null
  cmake --build libarchive-build > /dev/null
  cmake --install libarchive-build > /dev/null
fi
# a missing -dev package silently drops a codec: zip, 7z, tar.xz and
# tar.zst need all four
needed="$(objdump -p "$LIBARCHIVE/lib/libarchive.so" | awk '/NEEDED/ {print $2}')"
for codec in libz.so libbz2.so liblzma.so libzstd.so; do
  if ! grep -q "^$codec" <<< "$needed"; then
    echo "ERROR: libarchive built without ${codec%.so} (install its -dev package)" >&2
    exit 1
  fi
done
# linuxdeploy resolves the app's libarchive here, not in the system
export LD_LIBRARY_PATH="$LIBARCHIVE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S "$SRC" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DLibArchive_INCLUDE_DIR="$LIBARCHIVE/include" \
  -DLibArchive_LIBRARY="$LIBARCHIVE/lib/libarchive.so"
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

# Qt's own strings (dialog buttons, text-field context menus) come from
# qtbase_<lang>.qm. The qt plugin deploys them into usr/translations
# (where its qt.conf, Prefix=../, points Qt) only when Debian's
# qt6-translations-l10n is installed; the 0.9.4 containers lacked it and
# the AppImages showed "Cancel" and "Paste" in English to pt-BR users.
# Guard the app's languages (fail loudly if the package goes missing) and
# drop the rest, like the dmg: a UI language LibreMerge is not
# translated to stays English throughout instead of half-translated.
QT_TRANSLATIONS="$(/usr/bin/qmake6 -query QT_INSTALL_TRANSLATIONS)"
mkdir -p AppDir/usr/translations
keep=""
for ts in "$SRC"/app/i18n/libremerge_*.ts; do
  lang="$(basename "$ts" .ts)"
  lang="${lang#libremerge_}"
  qm="$QT_TRANSLATIONS/qtbase_$lang.qm"
  if [ ! -f "$qm" ]; then
    echo "ERROR: $qm not found (install qt6-translations-l10n)" >&2
    exit 1
  fi
  cp "$qm" AppDir/usr/translations/
  keep="$keep qtbase_$lang.qm"
done
for qm in AppDir/usr/translations/*.qm; do
  case " $keep " in
    *" $(basename "$qm") "*) ;;
    *) rm -f "$qm" ;;
  esac
done

# the bundled Qt (Debian 12's 6.4) cannot position windows on a native
# Wayland session, so dialogs land in a screen corner instead of being
# centered on the application window; prefer XWayland until the bundle
# moves to a newer Qt. QT_QPA_PLATFORM set by the user still wins.
mkdir -p AppDir/apprun-hooks
cat > AppDir/apprun-hooks/00-libremerge-platform.sh <<'EOF'
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
EOF

# the glibc floor: nothing bundled may need more than 2.35 (Ubuntu 22.04)
too_new=""
while IFS= read -r -d '' elf; do
  syms="$(objdump -T "$elf" 2>/dev/null || true)"
  if grep -qE 'GLIBC_2\.(3[6-9]|[4-9][0-9])' <<< "$syms"; then
    too_new="$too_new ${elf#AppDir/}"
  fi
done < <(find AppDir -type f \( -name '*.so*' -o -perm -u+x \) -print0)
if [ -n "$too_new" ]; then
  echo "ERROR: glibc newer than 2.35 needed by:$too_new" >&2
  exit 1
fi

# appimagetool warns that "usr/share/metainfo/libremerge.appdata.xml" is
# missing: it expects the desktop file's name, while validate-tree wants
# the component ID's (io.github.iagodpassos.libremerge.appdata.xml, which
# CI validates and AppImageHub reads). The warning is expected.
./linuxdeploy-"$ARCH".AppImage --appdir AppDir --output appimage

ls -la LibreMerge*.AppImage
