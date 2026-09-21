# OpennComm

Assistive communication for people who cannot speak or move.
A caregiver asks a question aloud; the patient answers with head movement,
blinks, or morse code spelled with their eyelids. The answer is spoken back.

**Everything runs on the machine in front of you.** No account, no network, no
cloud service, no telemetry. Speech recognition, speech synthesis and answer
generation are local models; the camera feed never leaves the process.

Linux, x86_64. Qt6 + C.

## Not a medical device

OpennComm has no regulatory clearance and no clinical validation, and a
caregiver must be present whenever it is used. Read [DISCLAIMER](DISCLAIMER)
before deploying it to anyone.

## Status

Early. Version 0.2.0 — see [CHANGELOG.md](CHANGELOG.md) for what that release
contains and what it does not, and [docs/PLAN.md](docs/PLAN.md) for the longer
account of what is built, what is not, and why.

## What you need

A webcam, and either Linux or Windows on x86_64.

**On Windows there is nothing to install but the application** — see
[Windows](#windows) below. Everything on this page up to that point is about
Linux, or about building from source.

### Linux

| | |
|---|---|
| Build | CMake 3.20+, Ninja, a C++17 compiler, pkg-config, git, curl, unzip, python3-pip |
| To install or package | `patchelf` (not needed for `./run.sh`) |
| Libraries | **Qt 6.4 or newer**, OpenCV 4, SQLite 3, OpenGL/GLX headers |
| At runtime | `ffmpeg` for the microphone, `paplay` (PulseAudio or PipeWire) for sound |
| Optional | `espeak-ng`, used only if the Piper voice is missing |

Fedora:

    sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config git curl unzip \
        python3-pip patchelf qt6-qtbase-devel opencv-devel sqlite-devel libglvnd-devel \
        ffmpeg pulseaudio-utils espeak-ng

Debian or Ubuntu (24.04 or newer — 22.04 and Debian 12 ship Qt 6.2, which is
too old):

    sudo apt install build-essential cmake ninja-build pkg-config git curl unzip \
        python3-pip patchelf qt6-base-dev libopencv-dev libsqlite3-dev libgl1-mesa-dev \
        ffmpeg pulseaudio-utils espeak-ng

Arch:

    sudo pacman -S cmake ninja base-devel pkgconf git curl unzip python-pip \
        qt6-base opencv sqlite mesa patchelf ffmpeg libpulse espeak-ng

The OpenGL headers are not optional and are easy to miss: nothing here draws
with OpenGL, but Qt's CMake package resolves `OpenGL::GLX` while finalizing any
executable, and without them configuration fails with an error that never
mentions OpenGL being absent.

Then about 1.3 GB of models is downloaded once, by `scripts/fetch-deps.sh`.
Nothing is sent anywhere; the downloads are one way. If you installed a `.deb`
or an `.rpm` instead, they are already inside it and there is nothing to
fetch.

### Windows

Windows 10 (1809 or newer) or Windows 11, on x86_64, and a webcam. Nothing
else: Qt, OpenCV, the models and the voice all travel inside the download.

Two ways to get it, from the [releases
page](https://github.com/VishalRamKN/OpennComm-Core/releases):

| | |
|---|---|
| `OpennComm-0.2.0-windows-x64.exe` | The installer. Adds a Start Menu entry and an uninstaller. Needs an administrator once, to write to Program Files. |
| `OpennComm-0.2.0-windows-x64.zip` | The portable copy. Unpack it anywhere — including a USB stick — and run `openncomm.exe`. Needs no administrator and no installation. |

Both are about 1.4 GB, because both carry every model and both voices. That is
the point: a package that installs and then has no voice is a bad thing to
hand to somebody who cannot tell you it is broken.

The portable `.zip` is the one to use on a ward or office computer where
nobody has an administrator password.

Windows Defender SmartScreen will warn that the publisher is unknown, because
these builds are not code-signed — a certificate costs money this project does
not have. **More info → Run anyway.** The checksums published beside each
release are how to verify you have what was built.

Building from source on Windows is in
[CONTRIBUTING.md](CONTRIBUTING.md#building-on-windows); it is not needed to
use the application.

## Running

    git clone https://github.com/VishalRamKN/OpennComm-Core.git
    cd OpennComm-Core
    ./run.sh

That fetches the models on first use, builds, and opens the window. It starts
full screen, because the patient reads the four answers from a bed; **F11**
toggles, **Escape** returns to a window, and `./run.sh --windowed` starts in
one.

If you installed a `.deb` or `.rpm` instead, there is nothing to clone or
build: run `openncomm`, or pick OpennComm out of the desktop menu. Every
command below that spells out `./build/src/openncomm` is just `openncomm` on an
installed system.

On Windows, start it from the Start Menu, or run `openncomm.exe` from the
folder you unpacked. The same commands work from a terminal — `openncomm.exe
--check`, `openncomm.exe --version` — and print to the console they were
started from.

On the first run it asks who it is speaking for. After that a caregiver types
or speaks a question, and the patient answers it with their eyes: a short blink
moves the highlight and a long one speaks the answer, or the nose points at one
of the four options, or morse spelled with two blinks says anything at all.

**[docs/USING.md](docs/USING.md) is the guide to using it** — each input mode,
the two per-patient setup screens, the history and what every setting does,
with the reasoning behind each.


## Checking it works

    openncomm --version
    ./build/src/openncomm --check

`--version` prints the release and exits, needing no display, and `--check`
puts the same version in its first line — that output is what belongs in a bug
report.

`--check` exercises every subsystem and says which are up — including speaking
a sentence aloud, because audio has more ways to be silently broken than
missing. The failure this exists for is quiet: a language model that never
loaded is indistinguishable, from the four buttons alone, from one that simply
wrote plain answers. The running app shows
the same thing as a strip of indicators under the toolbar.

Everything degrades independently. With no model the patient still gets four
answers from the built-in phrasebook; with no microphone the caregiver types the
question; with neither, morse spelling still works.

## Installing the models separately

**If you installed the `.deb` or the `.rpm`, skip this.** They carry every
model and both voices; there is nothing to download and this command will tell
you so.

    ./build/src/openncomm --fetch-models

Downloads the speech, answer and voice models into
`~/.local/share/openncomm/models`, skipping anything the application can
already find. It is for a source tree where `scripts/fetch-deps.sh` has not
been run, for the AppImage, which bundles only the face model, and for adding
a Piper voice of your own to an installed system without root.

**The application works with whatever is present.** Morse spelling and the
built-in phrasebook need nothing but the face model. Without the rest you lose
spoken questions, written answers and the neural voice; you do not lose the
ability to say something.

## Packaging

**`.deb`, `.rpm` and the Windows installer are the supported packages, and all
of them are self-contained.**

    ./scripts/build-packages.sh          # both, each in a container
    ./scripts/build-packages.sh deb
    ./scripts/build-packages.sh rpm

They land in `build/`. Installing one gives you `openncomm` on `PATH` and an
entry in the desktop menu, and **nothing further to download** — the packages
carry the face, speech and language models and both voices. That is why they
are about 1.4 GB, and install to roughly 1.5 GB. Each build takes a few
minutes longer than the code alone would.

It is a lot to download for a program of 200 MB, and it is deliberate. This
gets installed for somebody who cannot speak. A package that installs, opens,
tracks the face and then silently has no voice is a package whose one user
cannot report what is wrong with it; the caregiver has to find out from
documentation. Paying the size once, at the download, where there is a
progress bar and somebody watching it, is the better place for the cost.

**A warning about the rpm on a metered connection.** Fedora's `opencv-videoio`
is linked against the full GDAL stack, so installing the package also pulls
PDAL, arrow, hdf5 and several hundred megabytes of `proj-data` cartographic
grids — none of which OpennComm touches. On a machine that does
not already have them, expect the install to fetch close to a gigabyte on top
of the package's own 1.4 GB. The `.deb` does not have this to anything like
the same degree. See `docs/PLAN.md`.

These packages use the system Qt, OpenCV and SQLite and bundle only what no
distribution ships — MediaPipe, llama.cpp, whisper.cpp, ggml and Piper — in a
private directory beside them (`/usr/lib64/openncomm` on Fedora,
`/usr/lib/x86_64-linux-gnu/openncomm` on Debian). That is exactly the
configuration the AppImage could not achieve, and the reason it works.

**Build one package per distribution release.** A package records the sonames
it was linked against: the Fedora 43 rpm requires `Qt_6.10` and
`libopencv_core.so.411`, and will not install on Fedora 42. Build on the oldest
release you intend to support. `build-packages.sh` defaults to `debian:13` and
`fedora:42`; override with `DEB_IMAGE` and `RPM_IMAGE`.

`scripts/build-deb.sh` and `scripts/build-rpm.sh` build a package for the
machine you are on, without a container, if you would rather do that.

### Windows

From a Visual Studio developer prompt, with Qt and OpenCV findable:

    $env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.1\msvc2022_64;C:\vcpkg\installed\x64-windows"
    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1

That fetches the dependencies, builds, and produces both the `.exe` installer
and the portable `.zip` in `build-windows\`. Both are made from the same
`cmake --install` tree, so they cannot disagree about what is in them, and
`scripts\verify-windows-tree.ps1` checks that tree for every DLL, model and
licence before either is written.

There is no container step and no per-release rebuild the way there is on
Linux: a Windows binary does not record the versions it was linked against, so
one build runs on Windows 10 1809 and everything after it.

NSIS is needed for the installer — `winget install NSIS.NSIS`. Without it
`cpack` still produces the `.zip`.

### Installing from source instead

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    sudo cmake --install build --prefix /usr

This needs `patchelf`, which rewrites the vendored libraries' RUNPATHs — they
are recorded with the build machine's absolute paths and are wrong everywhere
else. `./run.sh` does not need it. Windows has no equivalent of any of this: a
DLL records no search path, and the loader looks beside the executable.

### AppImage — still broken

`./scripts/build-appimage.sh` produces an AppImage that **crashes on a current
Fedora**, during Qt platform-plugin initialisation. The same binary runs
correctly against the system Qt, so the fault is in the bundled copies, not the
application. See `docs/PLAN.md`. Use a `.deb`, an `.rpm` or `./run.sh`.

## Building and testing

    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

`core/` builds and tests standalone, with no Qt, no OpenCV and none of the
vendored runtimes — 7 of the 8 suites, in a couple of seconds, on a machine
with none of this installed:

    cmake -S core -B build-core -G Ninja
    cmake --build build-core && ctest --test-dir build-core

(The eighth is the history suite, which needs Qt and SQLite and so lives in
`src/`.) This is the configuration a CI job should use; there is no CI
configured in this repository yet.

To check a package rather than the build tree, install it into a clean
container of its own distribution and run the application's self-check there:

    ./scripts/test-package.sh deb
    ./scripts/test-package.sh rpm

That is the only check that exercises what a person actually downloads: the
dependency resolver supplies Qt, OpenCV and the GL stack, the models come out
of the package, and nothing at all is mounted in from this tree.

## Layout

    core/      pure C, no I/O, no dependencies -- the patient-facing logic
               (blink timing, option scanning, morse, gaze mapping, EAR)
               plus its tests. Runs without a camera, model or window.
    src/       Qt6 application shell
    spike/     throwaway risk-retirement programs
    cmake/     the vendored-dependency targets, shared by src/ and spike/,
               and the install-time fixups each platform needs
    scripts/   dependency fetching and packaging: *.sh for Linux, *.ps1 for
               Windows, with the same checksums pinned in both
    packaging/ desktop entry, AppStream metadata, icons and licence notices
    docs/      usage guide, plan and design notes

Only two files know which platform they are on: `src/audio.cc`, because sound
is the one thing Linux and Windows do not do alike, and `src/paths.cc`, because
Windows has no filesystem hierarchy to be relative to. Everything that decides
what a patient is understood to have said is in `core/` and is the same code
everywhere.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

Copyleft is a deliberate choice rather than a default. Assistive communication
is a market of expensive proprietary devices, and the point of this project is
that nobody should have to buy their own voice. The GPL means improvements to it
stay available to the people who need them.

It is also honest about what ships: speech synthesis depends on espeak-ng, which
is GPL-3.0, so a permissive licence on this repository would not describe the
binary you actually run. See [THIRD_PARTY.md](THIRD_PARTY.md) for the full
dependency and model-licence inventory.

There is no commercial edition and no plan for one.

"OpennComm" is the project's name, not part of the licence grant. Forks are very
welcome; please ship them under a different name.

## Authors

Initial development by Vishal Ram K N and Drisha P.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Read the invariants section first —
it is short, and every item in it is load-bearing.
