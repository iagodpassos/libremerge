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
XZ_VER="5.8.1"
ZSTD_VER="1.5.7"
LIBARCHIVE_VER="3.8.1"
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

# --- xz / liblzma (universal; libarchive needs it for 7z and tar.xz) ---------
if [ ! -f "$DEPS/xz/lib/liblzma.dylib" ]; then
  echo "==> xz $XZ_VER"
  cd "$WORK"
  if [ ! -d "xz-$XZ_VER" ]; then
    curl -fsSL -o xz.tgz \
      "https://github.com/tukaani-project/xz/releases/download/v$XZ_VER/xz-$XZ_VER.tar.gz"
    tar xzf xz.tgz
  fi
  cmake -S "xz-$XZ_VER" -B xz-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" \
    -DCMAKE_INSTALL_PREFIX="$DEPS/xz" \
    -DCMAKE_INSTALL_NAME_DIR="$DEPS/xz/lib" \
    -DBUILD_SHARED_LIBS=ON -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF \
    -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF \
    -DXZ_DOC=OFF -DXZ_NLS=OFF > /dev/null
  cmake --build xz-build > /dev/null
  cmake --install xz-build > /dev/null
  echo "    xz ok: $(lipo -info "$DEPS"/xz/lib/liblzma.*.dylib | tail -1)"
fi

# --- zstd (universal; libarchive needs it for tar.zst) -----------------------
if [ ! -f "$DEPS/zstd/lib/libzstd.dylib" ]; then
  echo "==> zstd $ZSTD_VER"
  cd "$WORK"
  if [ ! -d "zstd-$ZSTD_VER" ]; then
    curl -fsSL -o zstd.tgz \
      "https://github.com/facebook/zstd/releases/download/v$ZSTD_VER/zstd-$ZSTD_VER.tar.gz"
    tar xzf zstd.tgz
  fi
  cmake -S "zstd-$ZSTD_VER/build/cmake" -B zstd-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" \
    -DCMAKE_INSTALL_PREFIX="$DEPS/zstd" \
    -DCMAKE_INSTALL_NAME_DIR="$DEPS/zstd/lib" \
    -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_STATIC=OFF \
    -DZSTD_BUILD_SHARED=ON -DZSTD_BUILD_TESTS=OFF > /dev/null
  cmake --build zstd-build > /dev/null
  cmake --install zstd-build > /dev/null
  echo "    zstd ok: $(lipo -info "$DEPS"/zstd/lib/libzstd.*.*.dylib | tail -1)"
fi

# --- libarchive (universal; the archive-comparison backend) ------------------
# zlib, bz2 and iconv come from the macOS SDK, which is universal already;
# crypto/xml-based formats are disabled (not needed for zip/7z/tar reading)
if [ ! -f "$DEPS/libarchive/lib/libarchive.dylib" ]; then
  echo "==> libarchive $LIBARCHIVE_VER"
  cd "$WORK"
  if [ ! -d "libarchive-$LIBARCHIVE_VER" ]; then
    curl -fsSL -o libarchive.tgz \
      "https://github.com/libarchive/libarchive/releases/download/v$LIBARCHIVE_VER/libarchive-$LIBARCHIVE_VER.tar.gz"
    tar xzf libarchive.tgz
  fi
  cmake -S "libarchive-$LIBARCHIVE_VER" -B libarchive-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" \
    -DCMAKE_INSTALL_PREFIX="$DEPS/libarchive" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_NAME_DIR="$DEPS/libarchive/lib" \
    -DCMAKE_PREFIX_PATH="$DEPS/xz;$DEPS/zstd" \
    -DENABLE_OPENSSL=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF \
    -DENABLE_LZ4=OFF -DENABLE_LIBB2=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_NETTLE=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF \
    -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_TEST=OFF \
    -DENABLE_ACL=OFF > /dev/null
  cmake --build libarchive-build > /dev/null
  cmake --install libarchive-build > /dev/null
  echo "    libarchive ok: $(lipo -info "$DEPS"/libarchive/lib/libarchive.*.dylib | tail -1)"
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
