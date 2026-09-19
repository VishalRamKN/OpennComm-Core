#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build an .rpm. Run this on the oldest Fedora you intend to support, or in a
# container of one -- see scripts/build-packages.sh, which does that for you.
#
# The package depends on the system Qt, OpenCV and sqlite3 rather than bundling
# them. That is the whole reason this path exists: bundling Qt is what makes the
# AppImage crash (docs/PLAN.md, finding 13), and a distribution package is the
# configuration that was verified to work.
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

VERSION=$(tr -d '[:space:]' < VERSION)

# rpmbuild cannot handle a path containing a space anywhere in _topdir or
# _buildrootdir -- its own %mkbuilddir scriptlet word-splits it and aborts with
# "does not start with RPM_BUILD_ROOT". A source tree can easily sit under such
# a path on a desktop, so the whole rpm workspace is built somewhere without
# one and only the finished package is copied back.
WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/openncomm-rpm.XXXXXXXX")
trap 'rm -rf "$WORK"' EXIT

STAGE="$WORK/stage" ./scripts/stage-install.sh >/dev/null

echo "==> building rpm $VERSION"
mkdir -p "$WORK/rpmbuild"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

rpmbuild -bb "$PROJECT/packaging/rpm/openncomm.spec" \
  --define "_topdir $WORK/rpmbuild" \
  --define "oc_version $VERSION" \
  --define "oc_stage $WORK/stage" \
  --quiet

mkdir -p "$PROJECT/build"
find "$WORK/rpmbuild/RPMS" -name '*.rpm' -exec cp {} "$PROJECT/build/" \;
find "$PROJECT/build" -maxdepth 1 -name 'openncomm-*.rpm' -printf '%p  %s bytes\n'
