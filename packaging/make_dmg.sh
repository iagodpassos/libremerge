#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a distributable LibreMerge.dmg (macOS).
#
# - Release build of the app
# - macdeployqt bundles Qt frameworks and the Poco/ICU dylibs into the app
# - code signing: Developer ID identity when available, ad-hoc otherwise
#   (ad-hoc runs fine locally; distributing to others without Gatekeeper
#   warnings additionally needs an Apple Developer account + notarization)
# - hdiutil builds a compressed dmg with an /Applications shortcut
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="0.9.4"
DMG="$ROOT/LibreMerge-$VERSION.dmg"

# Universal (arm64 + x86_64, macOS 12+) toolchain: official Qt + ICU/Poco
# built by packaging/build_deps.sh. Falls back to the Homebrew toolchain
# (arm64-only, host-OS floor) when deps/ is absent.
QT_DEPS="$ROOT/deps/qt/6.8.3/macos"
if [ -d "$QT_DEPS" ] && [ -d "$ROOT/deps/icu" ] && [ -d "$ROOT/deps/poco" ] \
    && [ -d "$ROOT/deps/libarchive" ]; then
  echo "==> Release build (universal, macOS 12+)"
  BUILD="$ROOT/build-universal"
  QT_BIN="$QT_DEPS/bin"
  QT_TRANSLATIONS="$QT_DEPS/translations"
  cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DCMAKE_PREFIX_PATH="$QT_DEPS;$ROOT/deps/icu;$ROOT/deps/poco;$ROOT/deps/libarchive" \
    -DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON > /dev/null
else
  echo "==> Release build (Homebrew toolchain, arm64-only)"
  BUILD="$ROOT/build-release"
  QT_BIN="$(brew --prefix qt)/bin"
  QT_TRANSLATIONS="$(brew --prefix)/share/qt/translations"
  cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
fi
cmake --build "$BUILD" --target LibreMerge

APP="$BUILD/app/LibreMerge.app"

echo "==> Bundling frameworks (macdeployqt)"
"$QT_BIN/macdeployqt" "$APP" -always-overwrite

echo "==> Slimming the bundle (unused Qt modules)"
FRAMEWORKS="$APP/Contents/Frameworks"
PLUGINS="$APP/Contents/PlugIns"
# modules LibreMerge does not use, pulled in transitively via plugins
for fw in QtVirtualKeyboard QtVirtualKeyboardQml QtVirtualKeyboardSettings \
          QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQuick \
          QtPdf QtSvg QtOpenGL QtNetwork; do
  rm -rf "$FRAMEWORKS/$fw.framework"
done
rm -rf "$PLUGINS/virtualkeyboard" "$PLUGINS/qmltooling" \
  "$PLUGINS/platforminputcontexts" "$PLUGINS/networkinformation" \
  "$PLUGINS/tls" "$PLUGINS/imageformats/libqpdf.dylib" \
  "$PLUGINS/imageformats/libqsvg.dylib" "$PLUGINS/iconengines/libqsvgicon.dylib"

# fail loudly if a remaining binary still wants a removed framework
while read -r binary; do
  for dep in $(otool -L "$binary" | awk '/@rpath\/Qt.*\.framework/ {print $1}'); do
    fw="$(echo "$dep" | sed -E 's|@rpath/(Qt[^.]*)\.framework.*|\1|')"
    if [ ! -d "$FRAMEWORKS/$fw.framework" ]; then
      echo "    ERROR: $(basename "$binary") still needs removed $fw" >&2
      exit 1
    fi
  done
done < <(find "$PLUGINS" -name '*.dylib'; echo "$APP/Contents/MacOS/LibreMerge")

echo "==> Bundling Qt's own translations"
# LibreMerge's strings are compiled into the binary, but Qt's own (dialog
# buttons, text-field context menus, the application menu) come from
# qtbase_<lang>.qm, which macdeployqt does not copy: without it the 0.9.4
# dmg showed "Cancel" and "Paste" in English to pt-BR users. Ship the
# catalog for every language the app is translated to, and point qt.conf
# at it (the default would be Contents/translations).
mkdir -p "$APP/Contents/Resources/translations"
for ts in "$ROOT"/app/i18n/libremerge_*.ts; do
  lang="$(basename "$ts" .ts)"
  lang="${lang#libremerge_}"
  qm="$QT_TRANSLATIONS/qtbase_$lang.qm"
  if [ ! -f "$qm" ]; then
    echo "    ERROR: $qm not found" >&2
    exit 1
  fi
  cp "$qm" "$APP/Contents/Resources/translations/"
  echo "    qtbase_$lang.qm"
done
grep -q '^Translations' "$APP/Contents/Resources/qt.conf" \
  || echo "Translations = Resources/translations" >> "$APP/Contents/Resources/qt.conf"

echo "==> Signing"
IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null \
  | awk -F '"' '/Developer ID Application/ {print $2; exit}')"
if [ -n "$IDENTITY" ]; then
  echo "    using identity: $IDENTITY"
  codesign --force --deep --options runtime -s "$IDENTITY" "$APP"
else
  echo "    no Developer ID found - ad-hoc signature"
  codesign --force --deep -s - "$APP"
fi
codesign --verify --deep "$APP"

echo "==> Building dmg"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
rm -f "$DMG"
hdiutil create -volname "LibreMerge $VERSION" -srcfolder "$STAGING" \
  -ov -format UDZO "$DMG" > /dev/null

echo "==> Done: $DMG"
