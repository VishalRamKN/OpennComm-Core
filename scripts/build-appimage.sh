#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build an AppImage.
#
# The models are NOT bundled. They are about 1.2 GB against roughly 200 MB of
# code, and bundling them would make every update a gigabyte download. The face
# model is the exception: it is 3.6 MB, and without it there is no input at all,
# so it ships inside. Everything else is fetched into the user's XDG data
# directory on first use.
#
# That is only acceptable because the application is usable before any download
# finishes -- morse spelling and the built-in phrasebook need nothing more than
# the face model. Keep it that way.
#
# KNOWN BROKEN on a current Fedora, and no longer the recommended path --
# scripts/build-packages.sh builds a .deb and a .rpm that work. See the
# Packaging section of the README. The AppImage this produces segfaults during
# Qt platform-plugin initialisation. Verified by elimination: the same binary
# runs correctly against our own libraries plus the system Qt, and crashes only
# when linuxdeploy's bundled copies are on the library path.
#
# The likely cause is linuxdeploy's ELF tooling being older than the host
# toolchain -- its bundled `strip` already fails on the .relr.dyn sections that
# current binutils emit, and it rewrites the rpath of every one of the ~400
# libraries it copies.
#
# The fix is the thing AppImages are supposed to do anyway: build inside a
# container running the OLDEST distribution you intend to support, rather than
# the newest. Not done yet, and no longer urgent: scripts/build-packages.sh
# builds a .deb and a .rpm that install and run, and ./run.sh still works from
# a source tree.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "WARNING: the AppImage produced here is known to crash on a current"
echo "         Fedora. See the comment at the top of this script."
echo

TOOLS="${TOOLS_DIR:-$PWD/build/tools}"
APPDIR="$PWD/build/AppDir"
LD_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
AT_URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
RT_URL=https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64

mkdir -p "$TOOLS"
[ -x "$TOOLS/linuxdeploy" ]    || { curl -fsSL -o "$TOOLS/linuxdeploy" "$LD_URL"; chmod +x "$TOOLS/linuxdeploy"; }
# linuxdeploy locates plugins by this exact filename, on PATH.
[ -x "$TOOLS/linuxdeploy-plugin-qt" ] || { curl -fsSL -o "$TOOLS/linuxdeploy-plugin-qt" "$QT_URL"; chmod +x "$TOOLS/linuxdeploy-plugin-qt"; }
# Fetched here rather than left to linuxdeploy's appimage plugin, which pulls
# the runtime from the network mid-build and hangs indefinitely in SYN-SENT if
# that connection stalls. Downloading up front fails fast and visibly instead.
[ -x "$TOOLS/appimagetool" ]       || { curl -fsSL -o "$TOOLS/appimagetool" "$AT_URL"; chmod +x "$TOOLS/appimagetool"; }
[ -f "$TOOLS/runtime-x86_64" ]     || curl -fsSL -o "$TOOLS/runtime-x86_64" "$RT_URL"

./scripts/fetch-deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --target openncomm

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/openncomm/models" \
         "$APPDIR/usr/share/openncomm/piper"

cp build/src/openncomm "$APPDIR/usr/bin/"

# Our own libraries are NOT copied by hand. linuxdeploy already finds them
# through the executable's RUNPATH and puts them in usr/lib; copying them again
# into usr/lib/openncomm put two differently-patched copies of each on the
# library path, which is a hazard of its own.
#
# The libggml.so.0 SONAME conflict that used to be noted here is fixed:
# scripts/fetch-deps.sh builds one ggml and builds whisper.cpp against it. See
# finding 14 in docs/PLAN.md.

# Piper is self-contained: its own onnxruntime, libespeak-ng and phoneme data.
cp -a third_party/piper/.                               "$APPDIR/usr/share/openncomm/piper/"

# The only model small enough to bundle, and the only one without which nothing
# works at all.
cp models/face_landmarker.task                          "$APPDIR/usr/share/openncomm/models/"

cp packaging/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

# linuxdeploy walks every directory in PATH and aborts outright on one it
# cannot read ("filesystem error: status: Permission denied"). Stale entries
# pointing into another user's home are common on a developer machine, so keep
# only the readable ones rather than making that the operator's problem.
CLEAN_PATH=""
while IFS= read -r dir; do
  [ -n "$dir" ] && [ -r "$dir" ] || continue
  case ":$CLEAN_PATH:" in *":$dir:"*) continue ;; esac
  CLEAN_PATH="${CLEAN_PATH:+$CLEAN_PATH:}$dir"
done <<< "$(echo "$PATH" | tr ':' '\n')"
export PATH="$TOOLS:$CLEAN_PATH"

# linuxdeploy bundles its own binutils, and that strip is older than the
# toolchain on a current Fedora: it does not recognise the .relr.dyn sections
# modern binutils emit and fails with "unknown type [0x13]". Skipping the strip
# costs a larger AppImage and nothing else.
export NO_STRIP=1

export QMAKE="${QMAKE:-/usr/lib64/qt6/bin/qmake}"
export LD_LIBRARY_PATH="$PWD/third_party/mediapipe/lib:$PWD/third_party/llama.cpp/build/bin:$PWD/third_party/whisper.cpp/build/bin:${LD_LIBRARY_PATH:-}"
# Qt's Wayland platform plugins are named differently per distro -- Fedora ships
# a single libqwayland.so where Debian splits it into -egl and -generic -- and
# linuxdeploy fails outright on a plugin it cannot find. Ask for whatever is
# actually installed. Without this the AppImage runs under XWayland rather than
# natively, which is a worse experience but not a broken one.
QT_PLATFORMS_DIR="$(dirname "$(dirname "$QMAKE")")/plugins/platforms"
EXTRA_PLATFORM_PLUGINS=""
if [ -d "$QT_PLATFORMS_DIR" ]; then
  for so in "$QT_PLATFORMS_DIR"/libqwayland*.so; do
    [ -f "$so" ] || continue
    EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:+$EXTRA_PLATFORM_PLUGINS;}$(basename "$so")"
  done
fi
export EXTRA_PLATFORM_PLUGINS
echo "==> extra platform plugins: ${EXTRA_PLATFORM_PLUGINS:-none}"

# llama.cpp and whisper.cpp bake an absolute build-tree rpath into their
# libraries. Harmless here, wrong everywhere else: on another machine that path
# either does not exist or, worse, holds different libraries. Point them at
# their own directory instead.
if command -v patchelf >/dev/null; then
  for so in "$APPDIR"/usr/lib/openncomm/*.so*; do
    [ -f "$so" ] && [ ! -L "$so" ] && patchelf --set-rpath '$ORIGIN' "$so" 2>/dev/null || true
  done
fi

"$TOOLS/linuxdeploy" \
  --appdir "$APPDIR" \
  --executable build/src/openncomm \
  --desktop-file packaging/io.github.vishalramkn.OpennComm-Core.desktop \
  --icon-file packaging/io.github.vishalramkn.OpennComm-Core.png \
  --library third_party/mediapipe/lib/libmediapipe.so \
  --plugin qt

# OpenCV links FlexiBLAS, which dlopens its actual BLAS backend at runtime from
# a compiled-in path. ldd cannot see that, so the bundler misses it and the
# application aborts with "Failed to load the BLAS fallback library". AppRun
# points FlexiBLAS at these.
if [ -d /usr/lib64/flexiblas ]; then
  mkdir -p "$APPDIR/usr/lib/flexiblas"
  cp -P /usr/lib64/flexiblas/libflexiblas_openblas-serial.so \
        /usr/lib64/flexiblas/libflexiblas_netlib.so \
        /usr/lib64/flexiblas/libflexiblas_fallback_lapack.so \
        "$APPDIR/usr/lib/flexiblas/" 2>/dev/null || true
fi

# linuxdeploy bundles only the platform plugins a GUI needs. --check runs
# headless, but anything else asking for QT_QPA_PLATFORM=offscreen should find it.
OFFSCREEN="$QT_PLATFORMS_DIR/libqoffscreen.so"
[ -f "$OFFSCREEN" ] && cp -n "$OFFSCREEN" "$APPDIR/usr/plugins/platforms/" 2>/dev/null || true

# AppRun is regenerated by linuxdeploy; ours replaces it.
cp packaging/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

rm -f build/OpennComm-x86_64.AppImage
ARCH=x86_64 "$TOOLS/appimagetool" \
  --runtime-file "$TOOLS/runtime-x86_64" \
  "$APPDIR" build/OpennComm-x86_64.AppImage

chmod +x build/OpennComm-x86_64.AppImage
ls -lh build/OpennComm-x86_64.AppImage
echo
echo "Verify with:  ./build/OpennComm-x86_64.AppImage --check"
