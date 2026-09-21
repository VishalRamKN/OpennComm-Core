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

# The AppStream <releases> block carries a version of its own, and software
# centres display that one rather than the package's. If it falls behind the
# VERSION file, the package says 0.2.0 and GNOME Software says 0.1.0, and
# nothing anywhere fails. Check before building rather than after shipping.
VERSION=$(tr -d '[:space:]' < VERSION)
META=$(ls packaging/*.metainfo.xml 2>/dev/null | head -1)
if [ -n "$META" ]; then
  META_VERSION=$(sed -n 's/.*<release version="\([^"]*\)".*/\1/p' "$META" | head -1)
  if [ -z "$META_VERSION" ]; then
    echo "$META has no <release version=...> entry; add one for $VERSION." >&2
    exit 1
  fi
  if [ "$META_VERSION" != "$VERSION" ]; then
    echo "version mismatch: VERSION says $VERSION, $META says $META_VERSION" >&2
    echo "Update the <releases> block, with the release date, before building." >&2
    exit 1
  fi
fi

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
# LD_LIBRARY_PATH is emptied for this one command on purpose: the point is to
# see what the binary resolves through its own RUNPATH, not what happens to be
# on the build machine's library path. shellcheck reads the bare `VAR= cmd` as
# a probable typo, and here it is the whole intent.
# shellcheck disable=SC1007
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

# The models and the voices are the whole point of the package being 1.3 GB
# rather than 55 MB. A staging step that silently dropped one of them produces
# a package that installs cleanly, opens, and then has no voice -- which the
# patient cannot report. Sizes are compared, not just names, because a
# truncated copy passes an existence test and fails at load.
echo "==> verifying staged models" >&2
MODELDIR="$STAGE$PREFIX/share/openncomm/models"
for m in face_landmarker.task ggml-base.en-q5_1.bin \
         qwen2.5-1.5b-instruct-q4_k_m.gguf \
         voices/en_US-amy-medium.onnx voices/en_US-amy-medium.onnx.json \
         voices/en_US-joe-medium.onnx voices/en_US-joe-medium.onnx.json; do
  if [ ! -f "$MODELDIR/$m" ]; then
    echo "staged tree is missing models/$m" >&2
    exit 1
  fi
  want=$(stat -c %s "models/$m")
  got=$(stat -c %s "$MODELDIR/$m")
  if [ "$want" != "$got" ]; then
    echo "staged models/$m is $got bytes, source is $want" >&2
    exit 1
  fi
done

echo "$STAGE"
