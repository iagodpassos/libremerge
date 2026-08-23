#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build universal (arm64 + x86_64) ICU and Poco with a macOS 12 floor,
# for the distributable universal bundle. Homebrew's bottles are
# arm64-only and target the host OS, which is why the release toolchain
# builds these two itself. One-time; results land in deps/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
WORK="$DEPS/work"
TARGET="12.0"
ARCHFLAGS="-arch arm64 -arch x86_64"
ICU_VER="76_1"
ICU_TAG="release-76-1"
POCO_VER="1.13.3"
JOBS="$(sysctl -n hw.ncpu)"

mkdir -p "$WORK"

# --- ICU (universal, multi-arch single build) --------------------------------
if [ ! -f "$DEPS/icu/lib/libicuuc.dylib" ]; then
  echo "==> ICU $ICU_VER"
  cd "$WORK"
  if [ ! -d "icu" ]; then
    curl -fsSL -o icu.tgz \
      "https://github.com/unicode-org/icu/releases/download/$ICU_TAG/icu4c-$ICU_VER-src.tgz"
    tar xzf icu.tgz
  fi
  cd icu/source
  export MACOSX_DEPLOYMENT_TARGET="$TARGET"
  CFLAGS="$ARCHFLAGS -mmacosx-version-min=$TARGET" \
  CXXFLAGS="$ARCHFLAGS -mmacosx-version-min=$TARGET" \
  LDFLAGS="$ARCHFLAGS -mmacosx-version-min=$TARGET -Wl,-headerpad_max_install_names" \
    ./runConfigureICU MacOSX --prefix="$DEPS/icu" \
      --disable-static --enable-shared --disable-samples --disable-tests \
      > /dev/null
  make -j"$JOBS" > /dev/null
  make install > /dev/null
  # ICU's autoconf build writes bare install names (libicuuc.76.dylib): dyld
  # can't resolve them and macdeployqt can't locate the files. Rewrite them
  # to absolute paths (Homebrew-style) and re-sign.
  for lib in "$DEPS"/icu/lib/libicu*."${ICU_VER/_/.}".dylib; do
    install_name_tool -id "$lib" "$lib"
    otool -L "$lib" | awk 'NR>1 && $1 ~ /^libicu/ {print $1}' | while read -r dep; do
      install_name_tool -change "$dep" "$DEPS/icu/lib/$dep" "$lib"
    done
    codesign -f -s - "$lib"
  done
  echo "    ICU ok: $(lipo -info "$DEPS"/icu/lib/libicuuc.*.*.dylib | tail -1)"
fi

# --- Poco Foundation (universal) ---------------------------------------------
if [ ! -f "$DEPS/poco/lib/libPocoFoundation.dylib" ]; then
  echo "==> Poco $POCO_VER"
  cd "$WORK"
  if [ ! -d "poco-poco-$POCO_VER-release" ]; then
    curl -fsSL -o poco.tgz \
      "https://github.com/pocoproject/poco/archive/refs/tags/poco-$POCO_VER-release.tar.gz"
    tar xzf poco.tgz
    # Poco's bundled zlib predates the upstream zlib 1.2.12 fix: modern SDKs
    # define TARGET_OS_MAC, which turns fdopen into a macro and breaks
    # <stdio.h>. Same one-line fix as upstream zlib.
    sd -F 'defined(MACOS) || defined(TARGET_OS_MAC)' 'defined(MACOS)' \
      "poco-poco-$POCO_VER-release/Foundation/src/zutil.h"
  fi
  cmake -S "poco-poco-$POCO_VER-release" -B poco-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" \
    -DCMAKE_INSTALL_PREFIX="$DEPS/poco" \
    -DENABLE_XML=OFF -DENABLE_JSON=OFF -DENABLE_NET=OFF \
    -DENABLE_NETSSL=OFF -DENABLE_CRYPTO=OFF -DENABLE_JWT=OFF \
    -DENABLE_DATA=OFF -DENABLE_DATA_SQLITE=OFF -DENABLE_ZIP=OFF \
    -DENABLE_PAGECOMPILER=OFF -DENABLE_PAGECOMPILER_FILE2PAGE=OFF \
    -DENABLE_MONGODB=OFF -DENABLE_REDIS=OFF -DENABLE_PROMETHEUS=OFF \
    -DENABLE_UTIL=OFF -DENABLE_ACTIVERECORD=OFF \
    -DENABLE_ACTIVERECORD_COMPILER=OFF -DENABLE_ENCODINGS=OFF \
    -DENABLE_TESTS=OFF > /dev/null
  cmake --build poco-build > /dev/null
  cmake --install poco-build > /dev/null
  echo "    Poco ok: $(lipo -info "$DEPS"/poco/lib/libPocoFoundation.*.dylib | tail -1)"
fi

# --- Qt patch (QTBUG-136184) -------------------------------------------------
# Qt 6.8's FindWrapOpenGL links AGL, but recent SDKs removed it (only an
# empty shell remains under /System/Library/Frameworks, which fools
# find_library but fails at link time). Qt 6.9 dropped AGL entirely; do the
# same in our vendored copy.
WRAP="$DEPS/qt/6.8.3/macos/lib/cmake/Qt6/FindWrapOpenGL.cmake"
if [ -f "$WRAP" ] && rg -q 'WrapOpenGL_AGL' "$WRAP"; then
  echo "==> Patch QTBUG-136184 (AGL) no Qt"
  perl -0pi -e 's/[ \t]*find_library\(WrapOpenGL_AGL.*?INTERFACE \$\{__opengl_fw_path\}\)\n[ \t]*target_link_libraries\(WrapOpenGL::WrapOpenGL INTERFACE \$\{__opengl_agl_fw_path\}\)/        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE \${__opengl_fw_path})/s' "$WRAP"
fi

echo "==> deps prontos em $DEPS"
