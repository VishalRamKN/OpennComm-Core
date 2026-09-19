#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a .deb. Run this on the oldest Debian or Ubuntu you intend to support,
# or in a container of one -- see scripts/build-packages.sh, which does that.
#
# The floor is Qt 6.4, which means Debian 13 or Ubuntu 24.04 and newer. Debian
# 12 and Ubuntu 22.04 ship Qt 6.2 and cannot build this at all; the README says
# the same thing about building from source.
#
# Like the rpm, this depends on the system Qt, OpenCV and sqlite3 and bundles
# only what no distribution packages. See scripts/build-rpm.sh for why.
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

VERSION=$(tr -d '[:space:]' < VERSION)
REVISION="${DEB_REVISION:-1}"
ARCH=$(dpkg --print-architecture)

# Debian requires a maintainer in RFC 822 form, and a published package whose
# maintainer cannot receive mail is worse than one that was never published:
# somebody with a patient in front of them has nowhere to report a wrong answer.
#
# No address is baked into this repository. It falls back to the git identity of
# whoever is building, because that is the person who produced this particular
# binary and it is an identity they already publish on every commit. Inside a
# packaging container there is no .git and no git config -- the snapshot excludes
# it -- so scripts/build-packages.sh resolves this on the host and passes it in.
if [ -n "${DEB_MAINTAINER:-}" ]; then
  MAINTAINER="$DEB_MAINTAINER"
else
  _mname=$(git config user.name 2>/dev/null || true)
  _mmail=$(git config user.email 2>/dev/null || true)
  if [ -z "$_mname" ] || [ -z "$_mmail" ]; then
    echo "No maintainer for this package, and none could be worked out." >&2
    echo "  Set one:  DEB_MAINTAINER='Your Name <you@example.org>' $0" >&2
    echo "  or configure git user.name and user.email." >&2
    exit 1
  fi
  MAINTAINER="$_mname <$_mmail>"
  echo "==> maintainer taken from git config: $MAINTAINER" >&2
fi

STAGE=$(./scripts/stage-install.sh)
PKGDIR="$PROJECT/build/deb/openncomm_${VERSION}-${REVISION}_${ARCH}"

rm -rf "$PROJECT/build/deb"
mkdir -p "$PKGDIR/DEBIAN"
cp -a "$STAGE"/. "$PKGDIR"/

# Debian puts the licence here, not in /usr/share/licenses.
mkdir -p "$PKGDIR/usr/share/doc/openncomm"
mv "$PKGDIR/usr/share/licenses/openncomm/LICENSE" \
   "$PKGDIR/usr/share/doc/openncomm/copyright"
rm -rf "$PKGDIR/usr/share/licenses"

# Found rather than assumed: Debian's libdir is lib/x86_64-linux-gnu, two
# levels deep, where Fedora's is a single lib64. A `lib*/openncomm` glob
# matches the second and silently misses the first.
LIBDIR=$(cd "$PKGDIR" && find usr -type d -name openncomm -path 'usr/lib*' | head -1)
if [ -z "$LIBDIR" ]; then
  echo "could not find the private library directory under $PKGDIR/usr" >&2
  exit 1
fi

# Ask dpkg-shlibdeps what the binary actually needs, rather than writing a
# dependency list by hand that drifts from the truth on the next Qt transition.
# -l puts the private directory on its search path so the vendored libraries
# resolve; --ignore-missing-info stops it failing over them, since they belong
# to no package by design.
echo "==> computing dependencies"
# dpkg-shlibdeps insists on reading debian/control even with -O, which prints
# to stdout instead of writing a substvars file. This package is not built with
# debhelper and has no debian/ directory, so it gets a throwaway one.
SHLIBWORK=$(mktemp -d)
trap 'rm -rf "$SHLIBWORK"' EXIT
mkdir -p "$SHLIBWORK/debian"
cat > "$SHLIBWORK/debian/control" <<SHLIBCONTROL
Source: openncomm
Package: openncomm
Architecture: ${ARCH}
SHLIBCONTROL

SHLIBDEPS=$(cd "$SHLIBWORK" && dpkg-shlibdeps -O --ignore-missing-info \
  -l"$PKGDIR/$LIBDIR" \
  "$PKGDIR/usr/bin/openncomm" | sed 's/^shlibs:Depends=//')
if [ -z "$SHLIBDEPS" ]; then
  echo "dpkg-shlibdeps produced no dependencies -- refusing to build a package" >&2
  echo "that claims to need nothing. Check that the Qt, OpenCV and sqlite3" >&2
  echo "development packages are installed in this build environment." >&2
  exit 1
fi

# The GL stack, by hand, because dpkg-shlibdeps will not produce it and rpm's
# generator does. Two separate reasons:
#
#   libGLX.so.0 and libOpenGL.so.0 are in the executable's DT_NEEDED -- Qt's
#   CMake package pulls them in while finalizing any target, as the README
#   notes -- but nothing here calls a GL function. dpkg-shlibdeps emits a
#   dependency only where a symbol is actually used, so it drops both. The
#   dynamic linker does not care: an unused DT_NEEDED entry still has to
#   resolve, or the program does not start.
#
#   libEGL.so.1 and libGLESv2.so.2 are needed by libmediapipe.so, which is
#   private to this package and therefore skipped by shlibdeps altogether.
#
# Unversioned: these come from libglvnd, and every release that carries Qt 6.4
# or newer carries a new enough libglvnd for this.
GL_DEPENDS="libglx0, libopengl0, libegl1, libgles2"

INSTALLED_SIZE=$(du -sk "$PKGDIR" --exclude=DEBIAN | cut -f1)

cat > "$PKGDIR/DEBIAN/control" <<CONTROL
Package: openncomm
Version: ${VERSION}-${REVISION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_SIZE}
Depends: ${SHLIBDEPS}, ${GL_DEPENDS}, ffmpeg, pipewire-bin | pulseaudio-utils
Description: assistive communication that runs entirely on your own machine
 OpennComm is assistive communication for people who cannot speak or use their
 hands. A webcam tracks head movement and blinks; those become letters, words
 and spoken sentences.
 .
 Everything runs on this machine. Speech recognition, sentence suggestion and
 the synthesised voice are all local, so nothing a patient says is sent
 anywhere.
 .
 The package is usable before anything has been downloaded: morse spelling and
 the built-in phrasebook need only the face model included here. The speech and
 language models, about 1.2 GB, are fetched into the user's own data directory
 on first use.
 .
 OpennComm is NOT a medical device. It must not be relied on for clinical
 decisions or for emergency communication. See the DISCLAIMER in
 /usr/share/doc/openncomm.
CONTROL

# Fedora's desktop-file-utils and hicolor-icon-theme carry file triggers that do
# this automatically; Debian does not, so the package asks for it itself.
cat > "$PKGDIR/DEBIAN/triggers" <<'TRIGGERS'
activate-noawait update-desktop-database
interest-noawait /usr/share/icons/hicolor
TRIGGERS

echo "==> building deb $VERSION-$REVISION"
# xz at the default level on a 200 MB tree is slow for no benefit here; most of
# the weight is libmediapipe.so, which is already dense.
dpkg-deb --build --root-owner-group -Zxz -z3 "$PKGDIR" \
         "$PROJECT/build/openncomm_${VERSION}-${REVISION}_${ARCH}.deb"

ls -lh "$PROJECT/build/openncomm_${VERSION}-${REVISION}_${ARCH}.deb"
