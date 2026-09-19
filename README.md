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

Early. See [docs/PLAN.md](docs/PLAN.md) for what is built and what is not.

## What you need

Linux on x86_64, a webcam, and:

| | |
|---|---|
| Build | CMake 3.20+, Ninja, a C++17 compiler, pkg-config, git, curl, unzip, python3-pip |
| Libraries | **Qt 6.4 or newer**, OpenCV 4, SQLite 3, OpenGL/GLX headers |
| At runtime | `ffmpeg` for the microphone, `paplay` (PulseAudio or PipeWire) for sound |
| Optional | `espeak-ng`, used only if the Piper voice is missing |

Fedora:

    sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config git curl unzip \
        python3-pip qt6-qtbase-devel opencv-devel sqlite-devel libglvnd-devel \
        ffmpeg pulseaudio-utils espeak-ng

Debian or Ubuntu (24.04 or newer — 22.04 and Debian 12 ship Qt 6.2, which is
too old):

    sudo apt install build-essential cmake ninja-build pkg-config git curl unzip \
        python3-pip qt6-base-dev libopencv-dev libsqlite3-dev libgl1-mesa-dev \
        ffmpeg pulseaudio-utils espeak-ng

Arch:

    sudo pacman -S cmake ninja base-devel pkgconf git curl unzip python-pip \
        qt6-base opencv sqlite mesa ffmpeg libpulse espeak-ng

The OpenGL headers are not optional and are easy to miss: nothing here draws
with OpenGL, but Qt's CMake package resolves `OpenGL::GLX` while finalizing any
executable, and without them configuration fails with an error that never
mentions OpenGL being absent.

Then about 1.2 GB of models is downloaded on first run. Nothing is sent
anywhere; the downloads are one way.

## Running

    git clone https://github.com/VishalRamKN/OpennComm.git
    cd OpennComm
    ./run.sh

That fetches the models on first use, builds, and opens the window. It starts
full screen, because the patient reads the four answers from a bed; **F11**
toggles, **Escape** returns to a window, and `./run.sh --windowed` starts in
one.

On the first run it asks who it is speaking for. After that a caregiver types
or speaks a question, and the patient answers it with their eyes: a short blink
moves the highlight and a long one speaks the answer, or the nose points at one
of the four options, or morse spelled with two blinks says anything at all.

**[docs/USING.md](docs/USING.md) is the guide to using it** — each input mode,
the two per-patient setup screens, the history and what every setting does,
with the reasoning behind each.


## Checking it works

    ./build/src/openncomm --check

Exercises every subsystem and says which are up — including speaking a sentence
aloud, because audio has more ways to be silently broken than missing. The failure this exists for is
quiet: a language model that never loaded is indistinguishable, from the four
buttons alone, from one that simply wrote plain answers. The running app shows
the same thing as a strip of indicators under the toolbar.

Everything degrades independently. With no model the patient still gets four
answers from the built-in phrasebook; with no microphone the caregiver types the
question; with neither, morse spelling still works.

## Installing the models separately

    ./build/src/openncomm --fetch-models

Downloads the speech, answer and voice models into
`~/.local/share/openncomm/models`. A packaged build does not carry them — they
are about 1.2 GB against roughly 200 MB of code — so it fetches them on first
use and finds them there afterwards.

**The application works before any of that finishes.** Morse spelling and the
built-in phrasebook need nothing but the face model, which ships inside the
bundle. Without the downloads you lose spoken questions, written answers and
the neural voice; you do not lose the ability to say something.

## Packaging — not working yet

`./scripts/build-appimage.sh` produces an AppImage that **crashes on a current
Fedora**, during Qt platform-plugin initialisation. The same binary runs
correctly against the system Qt, so the fault is in the bundled copies, not the
application. See `docs/PLAN.md`.

Use `./run.sh` until this is fixed.

## Building and testing

    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

## Layout

    core/     pure C, no I/O, no dependencies -- the patient-facing logic
              (blink timing, option scanning, morse, gaze mapping, EAR)
              plus its tests. Runs without a camera, model or window.
    src/      Qt6 application shell
    spike/    throwaway risk-retirement programs
    docs/     usage guide, plan and design notes

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
