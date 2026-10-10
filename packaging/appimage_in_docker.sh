#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build and test the Linux AppImage for one architecture, in Docker:
#
#   packaging/appimage_in_docker.sh <arm64|amd64> [out-dir]
#
# Needs the Qt of that architecture in deps/linux, made once per Qt
# version by packaging/linux/build_qt.sh (its header says how). amd64 on an
# Apple Silicon Mac runs emulated, and takes several times longer.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${1:?usage: appimage_in_docker.sh <arm64|amd64> [out-dir]}"
OUT="${2:-$ROOT/build-appimage}"
case "$PLATFORM" in
  arm64) ARCH=aarch64 ;;
  amd64) ARCH=x86_64 ;;
  *) echo "unknown platform: $PLATFORM" >&2; exit 2 ;;
esac
if ! ls "$ROOT"/deps/linux/qt-*-"$ARCH".tar.zst > /dev/null 2>&1; then
  echo "ERROR: no Qt for $ARCH in deps/linux (see packaging/linux/build_qt.sh)" >&2
  exit 1
fi
mkdir -p "$OUT"
docker run --rm --platform "linux/$PLATFORM" \
  -v "$ROOT":/src:ro -v "$ROOT/deps/linux":/qt:ro -v "$OUT":/out \
  debian:12 bash /src/packaging/linux/appimage_container.sh
