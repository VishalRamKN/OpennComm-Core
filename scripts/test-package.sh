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
#   ./scripts/test-package.sh deb
#   ./scripts/test-package.sh rpm
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

DEB_IMAGE="${DEB_IMAGE:-debian:13}"
RPM_IMAGE="${RPM_IMAGE:-fedora:42}"
ENGINE="${ENGINE:-podman}"

# --check needs the large models, which the package deliberately does not carry.
# They are shared in read-only and pointed at with OPENNCOMM_MODELS, which is
# what a user would have after the first-run download.
[ -f "$PROJECT/models/qwen2.5-1.5b-instruct-q4_k_m.gguf" ] || {
  echo "models/ is incomplete -- run ./scripts/fetch-deps.sh first" >&2
  exit 1
}

case "${1:-}" in
  deb)
    PKG=$(ls -t "$PROJECT"/build/openncomm_*.deb 2>/dev/null | head -1)
    [ -n "$PKG" ] || { echo "no .deb in build/ -- build one first" >&2; exit 1; }
    echo "==> installing $(basename "$PKG") into $DEB_IMAGE"
    "$ENGINE" run --rm \
      -v "$PKG:/tmp/pkg.deb:ro,z" \
      -v "$PROJECT/models:/models:ro,z" \
      "$DEB_IMAGE" bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        # apt resolves the package'"'"'s own dependencies; nothing is named here
        # by hand, so a missing Depends shows up as a failure rather than being
        # papered over.
        apt-get install -y -qq /tmp/pkg.deb >/dev/null
        command -v openncomm
        OPENNCOMM_MODELS=/models QT_QPA_PLATFORM=offscreen \
          openncomm --check 2>&1 |
          grep -vE "^(W|I)[0-9]{4} |^INFO: |Logging before InitGoogle"
      '
    ;;
  rpm)
    PKG=$(ls -t "$PROJECT"/build/openncomm-*.rpm 2>/dev/null | head -1)
    [ -n "$PKG" ] || { echo "no .rpm in build/ -- build one first" >&2; exit 1; }
    echo "==> installing $(basename "$PKG") into $RPM_IMAGE"
    "$ENGINE" run --rm \
      -v "$PKG:/tmp/pkg.rpm:ro,z" \
      -v "$PROJECT/models:/models:ro,z" \
      "$RPM_IMAGE" bash -euo pipefail -c '
        dnf install -y -q /tmp/pkg.rpm >/dev/null
        command -v openncomm
        OPENNCOMM_MODELS=/models QT_QPA_PLATFORM=offscreen \
          openncomm --check 2>&1 |
          grep -vE "^(W|I)[0-9]{4} |^INFO: |Logging before InitGoogle"
      '
    ;;
  *)
    echo "usage: $0 [deb|rpm]" >&2
    exit 1
    ;;
esac
