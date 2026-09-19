#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build if needed, then run OpennComm from the source tree.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -f third_party/mediapipe/lib/libmediapipe.so ] || [ ! -f models/face_landmarker.task ] \
   || [ ! -f third_party/llama.cpp/build/bin/libllama.so ] \
   || [ ! -f third_party/whisper.cpp/build/bin/libwhisper.so ] \
   || [ ! -x third_party/piper/piper ]; then
  echo "==> fetching dependencies"
  ./scripts/fetch-deps.sh
fi

[ -d build ] || cmake -B build -G Ninja
cmake --build build

# libmediapipe.so is not installed system-wide; point the loader at the copy in
# the source tree. An installed build resolves it through RPATH instead.
export LD_LIBRARY_PATH="$PWD/third_party/mediapipe/lib:$PWD/third_party/llama.cpp/build/bin:$PWD/third_party/whisper.cpp/build/bin:${LD_LIBRARY_PATH:-}"

# MediaPipe logs a wall of TensorFlow noise on startup that hides real errors.
exec ./build/src/openncomm "$@" 2> >(grep -vE "^(W|I)[0-9]{4} |^INFO: |Logging before InitGoogle" >&2)
