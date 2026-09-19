#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the .deb and the .rpm, each inside a container of the distribution it
# targets.
#
# The container is not a convenience. A distribution package is linked against
# that distribution's Qt and OpenCV and records their sonames as dependencies:
# an rpm built on Fedora 43 requires Qt_6.10 and libopencv_core.so.411, and
# will not install on Fedora 42. Build on the OLDEST release you intend to
# support, and once per release after that.
#
#   ./scripts/build-packages.sh            both
#   ./scripts/build-packages.sh deb        just the .deb
#   ./scripts/build-packages.sh rpm        just the .rpm
#
# Override the base images to target a different release:
#   DEB_IMAGE=ubuntu:24.04 RPM_IMAGE=fedora:41 ./scripts/build-packages.sh
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

# Debian 13 (trixie) is the floor: it ships Qt 6.8, and this needs 6.4 or newer.
# Debian 12 and Ubuntu 22.04 ship Qt 6.2 and cannot build this at all.
DEB_IMAGE="${DEB_IMAGE:-debian:13}"
RPM_IMAGE="${RPM_IMAGE:-fedora:42}"

ENGINE="${ENGINE:-podman}"
command -v "$ENGINE" >/dev/null || { echo "$ENGINE not found" >&2; exit 1; }

mkdir -p "$PROJECT/build"

SNAPSHOT=$(mktemp "${TMPDIR:-/var/tmp}/openncomm-src.XXXXXXXX.tar")
trap 'rm -f "$SNAPSHOT"' EXIT

# The model files are large, pinned and distribution-independent, so they are
# shared with the container read-only rather than downloaded again per build.
# scripts/fetch-deps.sh verifies each against its checksum and, finding them
# already correct, writes nothing.
[ -f "$PROJECT/models/face_landmarker.task" ] || {
  echo "models/ is empty -- run ./scripts/fetch-deps.sh on the host first," >&2
  echo "so the containers can share them instead of downloading 1.2 GB each." >&2
  exit 1
}

# What must NOT cross into the container: anything already compiled. A
# llama.cpp built against the host's glibc would be silently reused by
# fetch-deps.sh, and the resulting package would depend on a glibc the target
# distribution does not have. Source and prebuilt-portable blobs only.
# Written to a file before any container starts, rather than piped straight in.
# `tar -cf - | podman run` looks equivalent and is not: the pipe holds only a
# few dozen KB, so tar blocks and keeps reading the working tree as the
# container drains it -- which, behind several minutes of `dnf install`, can be
# long after the build was launched. Editing a file meanwhile silently produces
# a package built from two different versions of the tree. Observed, not
# theorised: a package built during this session picked up documentation edits
# made sixteen minutes after it started.
copy_source() {
  tar -C "$PROJECT" -cf - \
      --exclude=./build \
      --exclude=./.git \
      --exclude=./third_party/llama.cpp/build \
      --exclude=./third_party/whisper.cpp/build \
      --exclude=./third_party/prefix \
      --exclude=./models \
      .
}

snapshot_source() {
  # An `if`, not `[ -s ... ] && return 0`: under `set -e` that list exits the
  # script when the test fails, which is the first call, because mktemp has
  # just created the file empty.
  if [ -s "$SNAPSHOT" ]; then
    return 0
  fi
  echo "==> snapshotting the source tree"
  copy_source > "$SNAPSHOT"
}

build_deb() {
  snapshot_source
  echo "==> .deb in $DEB_IMAGE"
  # libegl/libgles are not for us -- nothing here draws with either -- but
  # libmediapipe.so links both, and the staged-tree check in stage-install.sh
  # fails without them. They must be present at build time for dpkg-shlibdeps
  # to turn them into a dependency rather than a missing library.
  "$ENGINE" run --rm -i < "$SNAPSHOT" \
    -v "$PROJECT/models:/models:ro,z" \
    -v "$PROJECT/build:/out:z" \
    -e DEB_MAINTAINER="${DEB_MAINTAINER:-}" \
    "$DEB_IMAGE" bash -euo pipefail -c '
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq
      apt-get install -y -qq --no-install-recommends \
        build-essential cmake ninja-build pkg-config git curl ca-certificates \
        unzip python3-pip patchelf dpkg-dev file \
        qt6-base-dev libgl-dev libegl-dev libgles-dev \
        libopencv-dev libsqlite3-dev >/dev/null
      # --no-same-owner: the tar carries the host user'"'"'s uid, and a root
      # container restoring it leaves a tree git refuses to touch ("detected
      # dubious ownership"), which breaks the pinned-commit check in
      # scripts/fetch-deps.sh. safe.directory covers the same ground for any
      # checkout that still does not match.
      mkdir -p /work && tar -C /work --no-same-owner -xf -
      git config --global --add safe.directory '"'"'*'"'"'
      ln -s /models /work/models
      cd /work
      ./scripts/build-deb.sh
      cp /work/build/*.deb /out/
    '
}

build_rpm() {
  snapshot_source
  echo "==> .rpm in $RPM_IMAGE"
  "$ENGINE" run --rm -i < "$SNAPSHOT" \
    -v "$PROJECT/models:/models:ro,z" \
    -v "$PROJECT/build:/out:z" \
    "$RPM_IMAGE" bash -euo pipefail -c '
      # install_weak_deps=False is not a micro-optimisation. Fedora'"'"'s
      # opencv-devel recommends a tree that reaches gdal, hdf5, arrow and
      # samba; pulling it takes over twenty minutes on a normal connection and
      # none of it is needed to compile against OpenCV'"'"'s core headers.
      # --nodocs drops manpages and licences for the build-only packages.
      dnf install -y -q --setopt=install_weak_deps=False --nodocs \
        gcc gcc-c++ cmake ninja-build pkgconf-pkg-config git curl unzip \
        python3-pip patchelf rpm-build file \
        qt6-qtbase-devel opencv-devel sqlite-devel libglvnd-devel >/dev/null
      # See the note in build_deb about ownership.
      mkdir -p /work && tar -C /work --no-same-owner -xf -
      git config --global --add safe.directory '"'"'*'"'"'
      ln -s /models /work/models
      cd /work
      ./scripts/build-rpm.sh
      cp /work/build/*.rpm /out/
    '
}

case "${1:-all}" in
  deb) build_deb ;;
  rpm) build_rpm ;;
  all) build_deb; build_rpm ;;
  *)   echo "usage: $0 [deb|rpm|all]" >&2; exit 1 ;;
esac

echo
echo "==> packages in build/"
ls -lh "$PROJECT"/build/*.deb "$PROJECT"/build/*.rpm 2>/dev/null || true
