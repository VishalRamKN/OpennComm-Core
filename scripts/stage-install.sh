#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build OpennComm and install it into a staging directory, ready to be turned
# into a .deb or a .rpm. Shared by scripts/build-deb.sh and scripts/build-rpm.sh
# so that both formats package byte-identical trees.
#
# Prints the staging directory on stdout. All progress goes to stderr, so the
# callers can capture the path.
set -euo pipefail
cd "$(dirname "$0")/.."

STAGE="${STAGE:-$PWD/build/stage}"
PREFIX="${PREFIX:-/usr}"

# patchelf is needed by cmake/patch-rpath.cmake during install, not during the
# build. Checked here so the failure arrives before the compile rather than
# after it.
if ! command -v patchelf >/dev/null; then
  echo "patchelf is required to build packages." >&2
  echo "  Fedora:  sudo dnf install patchelf" >&2
  echo "  Debian:  sudo apt install patchelf" >&2
  exit 1
fi

echo "==> fetching dependencies" >&2
./scripts/fetch-deps.sh >&2

echo "==> building" >&2
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >&2
cmake --build build --target openncomm >&2

echo "==> staging into $STAGE" >&2
rm -rf "$STAGE"
DESTDIR="$STAGE" cmake --install build --prefix "$PREFIX" >&2

# The staged tree is what actually ships, so it is what gets checked. An
# unresolved library here becomes a package that installs cleanly and then
# fails to start, which is the worst place to find out.
echo "==> verifying staged tree" >&2
missing=$(LD_LIBRARY_PATH= ldd "$STAGE$PREFIX/bin/openncomm" 2>/dev/null | grep "not found" || true)
if [ -n "$missing" ]; then
  echo "staged binary has unresolved libraries:" >&2
  echo "$missing" >&2
  exit 1
fi

# Every vendored library must resolve through $ORIGIN, not through a path that
# happens to exist on the build machine. See cmake/patch-rpath.cmake.
for so in "$STAGE$PREFIX"/lib*/openncomm/*.so*; do
  [ -f "$so" ] && [ ! -L "$so" ] || continue
  rpath=$(objdump -p "$so" | awk '/RPATH|RUNPATH/{print $2}')
  if [ -n "$rpath" ] && [ "$rpath" != '$ORIGIN' ]; then
    echo "$so: RUNPATH is '$rpath', expected \$ORIGIN" >&2
    exit 1
  fi
done

echo "$STAGE"
