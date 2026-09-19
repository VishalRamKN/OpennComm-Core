#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetch the binary blobs that are too large to keep in git.
#
# Everything here is pinned and checksummed. That is not ceremony: the landmark
# model decides how every blink threshold in core/ behaves, so a silently
# updated file would move the thresholds a patient was tuned against without
# anything appearing to change. A version, never a "latest".
set -euo pipefail
cd "$(dirname "$0")/.."

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# verify <file> <sha256>. The hashes are of the artifacts this project was
# developed and tested against. A mismatch means the bytes changed underneath
# us, which is a reason to stop rather than a reason to carry on.
verify() {
  local got
  got=$(sha256sum "$1" | cut -d' ' -f1)
  if [ "$got" != "$2" ]; then
    rm -f "$1"
    echo "checksum mismatch: $1" >&2
    echo "  expected $2" >&2
    echo "  got      $got" >&2
    exit 1
  fi
}

# matches <file> <sha256>. True if the file is already the pinned artifact.
matches() {
  [ -f "$1" ] && [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$2" ]
}

# fetch <file> <url> <sha256> <description>. Checks what is already on disk
# rather than trusting its presence. Verifying only at download time would
# leave a truncated or altered model in place forever, which for the landmark
# model means blink thresholds that quietly no longer mean what core/ says.
fetch() {
  if matches "$1" "$3"; then return 0; fi
  if [ -f "$1" ]; then
    echo "==> $1 does not match its pinned checksum, refetching" >&2
    rm -f "$1"
  fi
  echo "==> fetching $4"
  curl -fL --progress-bar -o "$1" "$2"
  verify "$1" "$3"
}

# pin_check <dir> <sha> <tag>. A tag can be moved; a commit cannot. Cloning
# shallow by tag is fast, so the tag buys the speed and the commit buys the
# guarantee.
pin_check() {
  local got
  got=$(git -C "$1" rev-parse HEAD)
  if [ "$got" != "$2" ]; then
    echo "$1: tag $3 no longer points at the pinned commit" >&2
    echo "  expected $2" >&2
    echo "  got      $got" >&2
    exit 1
  fi
}

MP_WHEEL_VERSION=1.0.1
MP_LIB_SHA=b72e6d61a79d1080d29a96ba95e3cfa3e43f6c433c0acc3bc9b3eb7ac0ba103a
# Version 1, not "latest" -- see the note at the top of this file.
MODEL_URL="https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task"
MODEL_SHA=64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff

if ! matches third_party/mediapipe/lib/libmediapipe.so "$MP_LIB_SHA"; then
  echo "==> fetching libmediapipe.so (mediapipe ${MP_WHEEL_VERSION})"
  pip download --no-deps -q -d "$tmp" "mediapipe==${MP_WHEEL_VERSION}"
  ( cd "$tmp" && unzip -qo mediapipe-*.whl 'mediapipe/tasks/c/libmediapipe.so' )
  mkdir -p third_party/mediapipe/lib
  cp "$tmp/mediapipe/tasks/c/libmediapipe.so" third_party/mediapipe/lib/
  verify third_party/mediapipe/lib/libmediapipe.so "$MP_LIB_SHA"
fi

mkdir -p models
fetch models/face_landmarker.task "$MODEL_URL" "$MODEL_SHA" "face_landmarker.task"

# One ggml, not two. llama.cpp and whisper.cpp each vendor their own copy and
# each installs it as libggml.so.0 -- at 0.24 and 0.23 respectively. One SONAME,
# two ABIs: the dynamic linker resolves a library once per SONAME per process,
# so whichever loads first would serve both, and the loser would be calling a
# library it was not built against. It happened to work, which is worse than
# failing, because it was luck rather than correctness.
#
# So llama.cpp's ggml is built and installed here, and whisper.cpp is then built
# against it with WHISPER_USE_SYSTEM_GGML. This ordering is load-bearing: the
# prefix must exist before whisper.cpp is configured.
GGML_PREFIX="$PWD/third_party/prefix"

LLAMA_TAG=v0.4.1
LLAMA_SHA=b29c606e28a01b1bc8c1351026a0fa6e616bf6c4
if [ ! -f third_party/llama.cpp/build/bin/libllama.so ] \
   || [ ! -f "$GGML_PREFIX/lib/cmake/ggml/ggml-config.cmake" ]; then
  echo "==> building llama.cpp ${LLAMA_TAG} (a few minutes)"
  [ -d third_party/llama.cpp ] || git clone -q --depth 1 --branch "$LLAMA_TAG" \
      https://github.com/ggml-org/llama.cpp.git third_party/llama.cpp
  pin_check third_party/llama.cpp "$LLAMA_SHA" "$LLAMA_TAG"
  # CMAKE_INSTALL_LIBDIR is pinned to `lib` so the staging prefix is identical
  # on Fedora (which would default to lib64) and Debian (lib/x86_64-linux-gnu);
  # the packaging containers build both. Do NOT pass -DGGML_LIB_INSTALL_DIR:
  # it is declared `CACHE PATH`, and a relative PATH given on the command line
  # is resolved against the working directory, silently becoming $PWD/lib.
  # Letting ggml derive it from CMAKE_INSTALL_LIBDIR keeps it relative.
  cmake -S third_party/llama.cpp -B third_party/llama.cpp/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="$GGML_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
        -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_CURL=OFF >/dev/null
  # Only the library: llama.cpp's own CLI target fails to configure build-info.h
  # in this layout, and we do not need it.
  cmake --build third_party/llama.cpp/build --target llama
  # The ggml subdirectory only. A full `cmake --install` of llama.cpp fails
  # looking for the CLI binary we deliberately did not build, and installing
  # only --component ggml omits the CMake package files whisper.cpp needs to
  # find it.
  cmake --install third_party/llama.cpp/build/ggml >/dev/null
fi

# Apache-2.0 weights. See THIRD_PARTY.md for why that rules out Llama and Gemma.
LLM=models/qwen2.5-1.5b-instruct-q4_k_m.gguf
LLM_SHA=6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e
fetch "$LLM" \
  "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf" \
  "$LLM_SHA" "the language model (~1.1 GB)"

WHISPER_TAG=v1.9.4
WHISPER_SHA=927cfce34f31707e17f2bff35c349632fb9e2c3a
if [ ! -f third_party/whisper.cpp/build/bin/libwhisper.so ]; then
  echo "==> building whisper.cpp ${WHISPER_TAG}"
  [ -d third_party/whisper.cpp ] || git clone -q --depth 1 --branch "$WHISPER_TAG" \
      https://github.com/ggml-org/whisper.cpp.git third_party/whisper.cpp
  pin_check third_party/whisper.cpp "$WHISPER_SHA" "$WHISPER_TAG"
  # Against llama.cpp's ggml, not its own vendored copy. See the note above.
  cmake -S third_party/whisper.cpp -B third_party/whisper.cpp/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
        -DWHISPER_USE_SYSTEM_GGML=ON -Dggml_DIR="$GGML_PREFIX/lib/cmake/ggml" \
        -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=OFF \
        -DWHISPER_BUILD_SERVER=OFF >/dev/null
  cmake --build third_party/whisper.cpp/build --target whisper
fi

STT=models/ggml-base.en-q5_1.bin
STT_SHA=4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f
fetch "$STT" \
  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin" \
  "$STT_SHA" "the speech model (~57 MB)"

# Piper: a neural voice, so the patient does not have to sound like a machine.
# Shipped as a self-contained binary (its own onnxruntime and espeak-ng), which
# is why there is nothing to build here. Downloaded to a file and checksummed
# before extraction rather than piped into tar: an archive that is never written
# down is an archive whose contents cannot be checked before they are unpacked.
PIPER_URL="https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz"
PIPER_SHA=a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992
PIPER_BIN_SHA=12672a94ca6716e5a8f335cfa68bf43bd9a33284960e3f9d16b85090bf7aab6b
if ! matches third_party/piper/piper "$PIPER_BIN_SHA"; then
  echo "==> fetching Piper"
  mkdir -p third_party
  rm -rf third_party/piper
  curl -fsSL -o "$tmp/piper.tar.gz" "$PIPER_URL"
  verify "$tmp/piper.tar.gz" "$PIPER_SHA"
  tar xzf "$tmp/piper.tar.gz" -C third_party/
  verify third_party/piper/piper "$PIPER_BIN_SHA"
fi

# Two voices, one of each. Which one a patient is given is not a cosmetic
# choice: this is the voice other people will hear as theirs, and being handed
# the wrong one every day is its own small indignity. Either can be picked in
# Settings. Licences are in THIRD_PARTY.md.
VOICE_DIR=models/voices
VOICE_ROOT="https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US"
mkdir -p "$VOICE_DIR"
voice_sha() {
  case "$1" in
    en_US-amy-medium.onnx)      echo b3a6e47b57b8c7fbe6a0ce2518161a50f59a9cdd8a50835c02cb02bdd6206c18 ;;
    en_US-amy-medium.onnx.json) echo 95a23eb4d42909d38df73bb9ac7f45f597dbfcde2d1bf9526fdeaf5466977d77 ;;
    en_US-joe-medium.onnx)      echo 58afce0321b8d9c46d7cdf9c16500cc55a793b4220212dba6b70fb788b3baf06 ;;
    en_US-joe-medium.onnx.json) echo 3d6d5410b3795cb1950595247ef8f06190719e6fdbfa3a2356d8ec368e1aad33 ;;
    *) echo "no pinned checksum for $1" >&2; exit 1 ;;
  esac
}
for spec in "amy/medium en_US-amy-medium" "joe/medium en_US-joe-medium"; do
  set -- $spec
  for f in "$2.onnx" "$2.onnx.json"; do
    fetch "$VOICE_DIR/$f" "$VOICE_ROOT/$1/$f" "$(voice_sha "$f")" "voice $f"
  done
done

# The licence texts of everything the packages redistribute.
#
# This is not tidiness. The .deb and .rpm ship libespeak-ng.so, which is
# GPL-3.0, and Apache-2.0 section 4 and the MIT notice clause both require the
# licence to travel with the binary. A source tree distributes nothing and so
# needed none of this; packages do.
#
# llama.cpp and whisper.cpp carry theirs in their own checked-out trees, so only
# the four that arrive as prebuilt binaries are fetched. Pinned and checksummed
# like everything else here: a licence that silently changed under us would be
# the one file where nobody would think to look.
mkdir -p third_party/licenses
fetch third_party/licenses/mediapipe.LICENSE \
  "https://raw.githubusercontent.com/google-ai-edge/mediapipe/v0.10.21/LICENSE" \
  8707eef0533987efc5b155d64761eeb6e20793f50b9bd1a68dad1cf4719d0ed8 \
  "the MediaPipe licence (Apache-2.0)"
fetch third_party/licenses/piper.LICENSE \
  "https://raw.githubusercontent.com/rhasspy/piper/2023.11.14-2/LICENSE.md" \
  4cd71dece7037f1d6d93cce7570c57ab75ea9ac566fd4990be2f3ab08d15b47f \
  "the Piper licence (MIT)"
fetch third_party/licenses/espeak-ng.COPYING \
  "https://raw.githubusercontent.com/espeak-ng/espeak-ng/1.52.0/COPYING" \
  8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903 \
  "the espeak-ng licence (GPL-3.0)"
fetch third_party/licenses/onnxruntime.LICENSE \
  "https://raw.githubusercontent.com/microsoft/onnxruntime/v1.14.1/LICENSE" \
  2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c \
  "the onnxruntime licence (MIT)"

# Guard the invariant rather than trusting that it held. A whisper.cpp bump that
# quietly stopped honouring WHISPER_USE_SYSTEM_GGML would put a second
# libggml.so.0 back on the library path, and the symptom would not be a build
# failure -- it would be one of the two runtimes calling the wrong ABI at
# runtime. Fail here instead.
stray=$(find third_party/whisper.cpp/build -name 'libggml*.so*' 2>/dev/null | head -5)
if [ -n "$stray" ]; then
  echo "whisper.cpp built its own ggml -- that is the SONAME collision returning:" >&2
  echo "$stray" >&2
  exit 1
fi

echo "==> deps ready, checksums verified"
ls -la third_party/mediapipe/lib/libmediapipe.so models/*.task models/*.gguf models/*.bin
