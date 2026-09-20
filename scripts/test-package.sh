#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Install a built package into a clean container of its target distribution and
# run the application's own self-check inside it.
#
# This is the test that matters. Everything before it proves the package was
# assembled the way it was meant to be; this proves a person who downloads it
# and installs it gets a working program -- with the dependency resolver, not
# the build tree, supplying Qt, OpenCV and the GL stack.
#
# Nothing from this source tree is mounted into the container and
# OPENNCOMM_MODELS is deliberately not set. The models used to be shared in
# from the host here, which quietly meant the one thing this script could not
# tell you was whether the package carried them. It does now, so the container
# gets the package and nothing else.
#
#   ./scripts/test-package.sh deb
#   ./scripts/test-package.sh rpm
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

DEB_IMAGE="${DEB_IMAGE:-debian:13}"
RPM_IMAGE="${RPM_IMAGE:-fedora:42}"
ENGINE="${ENGINE:-podman}"

# Run inside the container once the package is installed.
#
# --check exercises the models by loading them, which is the real test, and the
# files are listed by name as well. Both, not either. A file of the right name
# that whisper or llama.cpp cannot load would pass the listing alone; and
# whether --check can speak depends on what the base image happens to drag in
# behind pipewire-utils, so "speech out" is not something to rely on here even
# though a Fedora container does manage it.
VERIFY=$(cat <<'INNER'
command -v openncomm
echo "==> models carried by the package"
for m in face_landmarker.task ggml-base.en-q5_1.bin \
         qwen2.5-1.5b-instruct-q4_k_m.gguf \
         voices/en_US-amy-medium.onnx voices/en_US-amy-medium.onnx.json \
         voices/en_US-joe-medium.onnx voices/en_US-joe-medium.onnx.json; do
  f="/usr/share/openncomm/models/$m"
  [ -s "$f" ] || { echo "MISSING from the installed package: $m" >&2; exit 1; }
  echo "  ok  $(du -h "$f" | cut -f1)  $m"
done
echo
QT_QPA_PLATFORM=offscreen openncomm --check 2>&1 |
  grep -vE "^(W|I)[0-9]{4} |^INFO: |Logging before InitGoogle"
INNER
)

case "${1:-}" in
  deb)
    PKG=$(ls -t "$PROJECT"/build/openncomm_*.deb 2>/dev/null | head -1)
    [ -n "$PKG" ] || { echo "no .deb in build/ -- build one first" >&2; exit 1; }
    echo "==> installing $(basename "$PKG") into $DEB_IMAGE"
    "$ENGINE" run --rm \
      -v "$PKG:/tmp/pkg.deb:ro,z" \
      "$DEB_IMAGE" bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        # apt resolves the package'"'"'s own dependencies; nothing is named here
        # by hand, so a missing Depends shows up as a failure rather than being
        # papered over.
        apt-get install -y -qq /tmp/pkg.deb >/dev/null
        '"$VERIFY"'
      '
    ;;
  rpm)
    PKG=$(ls -t "$PROJECT"/build/openncomm-*.rpm 2>/dev/null | head -1)
    [ -n "$PKG" ] || { echo "no .rpm in build/ -- build one first" >&2; exit 1; }
    echo "==> installing $(basename "$PKG") into $RPM_IMAGE"
    "$ENGINE" run --rm \
      -v "$PKG:/tmp/pkg.rpm:ro,z" \
      "$RPM_IMAGE" bash -euo pipefail -c '
        dnf install -y -q /tmp/pkg.rpm >/dev/null
        '"$VERIFY"'
      '
    ;;
  *)
    echo "usage: $0 [deb|rpm]" >&2
    exit 1
    ;;
esac
