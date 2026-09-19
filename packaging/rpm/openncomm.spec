# SPDX-License-Identifier: GPL-3.0-or-later
#
# This spec packages a tree that has ALREADY been built and installed into a
# staging directory by scripts/build-rpm.sh. It does not build anything itself.
#
# That is deliberate. A from-source %build would have to run scripts/fetch-deps.sh
# inside the buildroot, which clones llama.cpp and whisper.cpp and downloads
# about 1.2 GB of model weights over the network. Network access mid-build makes
# the result depend on the day it was built, which is the opposite of what the
# pinned checksums in that script exist to guarantee. Build once, verify once,
# package the bytes that were verified.

%global debug_package %{nil}
%global _build_id_links none

# The vendored runtimes are private to this application. Without these two,
# rpm would advertise libggml.so.0 to the whole system as though it were a
# system library, and then require it back from itself.
%global __provides_exclude_from ^%{_libdir}/openncomm/.*$
%global __requires_exclude ^(libggml.*|libllama\\.so.*|libwhisper\\.so.*|libmediapipe\\.so.*|libpiper_phonemize\\.so.*|libonnxruntime\\.so.*|libespeak-ng\\.so.*)$

# These libraries arrive prebuilt, and their RUNPATHs were already rewritten to
# $ORIGIN at install time by cmake/patch-rpath.cmake. Let them alone: strip has
# nothing useful to do on a vendored binary, and check-rpaths objects to the
# $ORIGIN that is the entire mechanism holding this layout together.
%global __brp_strip %{nil}
%global __brp_strip_static_archive %{nil}
%global __brp_strip_comment_note %{nil}
%global __brp_check_rpaths %{nil}

Name:           openncomm
Version:        %{oc_version}
Release:        1%{?dist}
Summary:        Assistive communication that runs entirely on your own machine
License:        GPL-3.0-or-later
BuildArch:      x86_64

# ffmpeg is how the microphone is read. Asked for by path rather than by
# package name, because the binary comes from ffmpeg-free in Fedora's own
# repositories and from ffmpeg in RPM Fusion, and either will do.
Requires:       /usr/bin/ffmpeg

# Speech output needs one of these to reach the sound server. src/speech.cc
# tries paplay first, then pw-play; aplay is deliberately not used, because it
# routes through an ALSA plugin absent on a stock PipeWire desktop.
Requires:       (pipewire-utils or pulseaudio-utils)

%description
OpennComm is assistive communication for people who cannot speak or use their
hands. A webcam tracks head movement and blinks; those become letters, words
and spoken sentences.

Everything runs on this machine. Speech recognition, sentence suggestion and
the synthesised voice are all local, so nothing a patient says is sent
anywhere.

The package is usable before anything has been downloaded: morse spelling and
the built-in phrasebook need only the face model included here. The speech and
language models, about 1.2 GB, are fetched into the user's own data directory
on first use.

OpennComm is NOT a medical device. It must not be relied on for clinical
decisions or for emergency communication. See the DISCLAIMER in
%{_docdir}/openncomm.

%prep
# Nothing to prepare: see the note at the top of this file.

%build
# Nothing to build: see the note at the top of this file.

%install
# Quoted: this project is routinely checked out into a path containing a
# space, and an unquoted expansion here fails deep inside rpmbuild.
cp -a "%{oc_stage}/." "%{buildroot}/"

%files
%license %{_datadir}/licenses/openncomm/LICENSE
# The whole directory rather than a list: it also holds WRITTEN-OFFER and the
# verbatim upstream licences, and a file installed there but not listed here
# fails the build rather than being quietly dropped. That is the right failure,
# but a tedious one to keep rediscovering.
# (Written without the doc-dir macro on purpose: rpm expands macros inside
# comments too, and warns when it does.)
%doc %{_docdir}/openncomm/
%{_bindir}/openncomm
%{_libdir}/openncomm/
%{_datadir}/openncomm/
# Named for the AppStream component id rather than the binary; see the
# install() rules in src/CMakeLists.txt.
%{_datadir}/applications/io.github.vishalramkn.OpennComm-Core.desktop
%{_datadir}/icons/hicolor/256x256/apps/io.github.vishalramkn.OpennComm-Core.png
%{_datadir}/icons/hicolor/scalable/apps/io.github.vishalramkn.OpennComm-Core.svg
%{_datadir}/metainfo/io.github.vishalramkn.OpennComm-Core.metainfo.xml

%changelog
* Sat Sep 19 2026 OpennComm contributors - 0.1.0-1
- First packaged release.
