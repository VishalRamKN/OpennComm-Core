<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

Notable changes to OpennComm. Dates are ISO 8601, versions follow
[Semantic Versioning](https://semver.org/).

Anything that changes what a patient's input is understood to mean is called
out explicitly, whatever its size. A one-line threshold change can alter which
answer gets spoken; a thousand-line packaging change cannot.

## [0.2.0] — 2026-09-21

### OpennComm runs on Windows

Windows 10 (1809 or newer) and Windows 11, on x86_64. Two downloads from the
releases page: an `.exe` installer that adds a Start Menu entry, and a portable
`.zip` that unpacks anywhere — including a USB stick — and needs no
administrator. That second one is not a convenience: a ward or office computer
where nobody has an administrator password is a normal place for this to be
needed, and it was previously a place it could not be used at all.

Both carry every model and both voices, for the same reason the `.deb` and the
`.rpm` do. Both are about 1.4 GB.

What this cost, and what it did not:

- **Nothing in `core/` changed.** Every rule that decides what a patient is
  understood to have said — blink timing, option scanning, morse, gaze
  mapping — is the same code on both platforms, and the same tests run against
  it. A port that forked that logic per operating system would mean a patient
  tuned on one and moved to the other, which is not a thing anyone should have
  to think about.
- **Sound is the one real difference,** and it is now behind two classes in
  the new `src/audio.h`. Linux still shells out to `ffmpeg` and `paplay`,
  unchanged and still the tested path, because those programs already know
  whether the machine is running PulseAudio or PipeWire. Windows has neither
  program and one audio API that is always present, so it uses Qt Multimedia
  directly and starts no subprocess at all. `listener.cc` and `speech.cc` —
  which hold the judgements about when a question has ended and when an answer
  has finished being spoken — contain no `#ifdef` and were not rewritten.
- Windows capture is coalesced into 64 ms chunks before the end-of-speech
  detection sees it. Qt hands over a few milliseconds at a time, and the RMS of
  five milliseconds of audio is noise about noise — the thresholds in
  `listener.cc` were measured against real recordings and now mean the same
  thing on both platforms instead of needing a second set of numbers.
- The camera opens through DirectShow rather than Media Foundation. Two things
  this depends on are only honoured by DirectShow: the one-frame buffer, without
  which the blink being classified is a third of a second old, and the MJPG
  format, without which most webcams cannot sustain 30fps over USB 2.0.
- `openncomm.exe --version` and `--check` print to the console they were
  started from. The application has to be a GUI-subsystem binary or every
  launch would flash a console behind a full-screen program the patient cannot
  dismiss; the cost is that its output goes nowhere unless it asks for the
  parent's console, which it now does for those two commands only.
- `--check` asks the capture backend what is missing instead of looking for
  `ffmpeg` by name, so on Windows it no longer reports a program that was never
  going to be there.

Getting there:

- `scripts/fetch-deps.ps1` is the counterpart of `fetch-deps.sh`, pinned and
  checksummed against the same artifacts — the same face model, the same
  weights, the same voices. If a checksum changes it has to change in both, or
  the two platforms ship different weights under one version number.
- The MediaPipe wheel carries `libmediapipe.dll` and no import library, because
  Python loads it and never links against it. One is generated from the DLL's
  own export table, so it cannot describe a different version of the library
  than the one beside it.
- `cmake/deps.cmake` declares the four vendored runtimes once, for every
  platform, instead of `src/` and `spike/` each having their own copy — which
  is how a path gets fixed in one and left wrong in the other.
- CI now builds `core/` under MSVC on every push, which catches a change that
  only compiles with GCC. A second job builds, installs and starts the whole
  application on Windows, on `main` and on release tags.

Not done, and worth knowing: the builds are not code-signed, so SmartScreen
warns that the publisher is unknown. A certificate costs money this project
does not have. The checksums published beside each release are the way to
verify a download.

### The packages now carry every model and both voices

Installing the `.deb` or the `.rpm` gives a working system with nothing left
to download. Before this, the package held the 3.6 MB face model and nothing
else: it installed cleanly, opened, tracked the face — and then had no speech
recognition, no written answers and no neural voice, with nothing on screen
saying which of those were missing rather than broken. A caregiver setting it
up at a bedside had to learn from the README that a further 1.2 GB was needed
and run `openncomm --fetch-models` to get it.

The size is now paid once, at download, by somebody watching a progress bar
and knowing what they are waiting for — instead of later, by a patient who
cannot say that something is wrong.

- The `.deb` and the `.rpm` are about 1.4 GB, up from 55 MB. They install to
  roughly 1.5 GB.
- `openncomm --fetch-models` still exists, for a source tree and the AppImage,
  and now skips any model the application can already find rather than
  downloading a second copy into the home directory.
- `scripts/test-package.sh` no longer shares this tree's `models/` into the
  container or sets `OPENNCOMM_MODELS`. It used to, which meant the one thing
  it could not tell you was whether the package carried the models. It now
  installs the package and nothing else, lists the models it found inside, and
  loads them.
- `scripts/stage-install.sh` fails the build if a model is missing from the
  staged tree or differs in size from the source, and
  `scripts/build-packages.sh` checks all seven files on the host before
  starting a container.
- The `.deb` is compressed with zstd rather than xz, and the `.rpm` with
  threaded zstd. On quantised weights xz -3 took 68 s per 100 MB to reach
  95.5%, where zstd -19 takes 15 s and reaches 95.0% — a quarter of an hour of
  build time for a slightly worse result.

### Licensing follows the weights into the package

Shipping the models moves their licences onto the artifact, where before they
bound only the user who downloaded them. The packages now carry:

- `MODEL-NOTICE`, in `/usr/share/doc/openncomm/`, naming every model, where it
  came from and what it is under.
- The verbatim text of CC-BY-SA-4.0, CC0-1.0, Qwen2.5's Apache-2.0 and
  OpenAI Whisper's MIT, beside the licences already there.

The one with teeth is `en_US-amy-medium`, which is CC-BY-SA-4.0: the credit
has to travel with the voice. It ships unmodified, and the share-alike
condition attaches to the voice rather than to OpennComm — CC-BY-SA-4.0 is
one-way compatible with GPL-3.0. See [THIRD_PARTY.md](THIRD_PARTY.md).

Nothing about the application's behaviour changed. What a patient's input is
understood to mean is untouched.

## [0.1.0] — 2026-09-19

First packaged release.

**OpennComm is not a medical device.** It has no regulatory clearance and no
clinical validation, a caregiver must be present whenever it is used, and it
must not be relied on for emergency communication. See
[DISCLAIMER](DISCLAIMER).

### Input and communication

- Blink mode: a short blink moves the highlight, a long blink speaks the
  highlighted answer. An ambiguous blink, between the two thresholds, is
  deliberately discarded rather than guessed.
- Head mode: point your nose at one of four options and blink, with a ring
  showing where the nose is aiming.
- Morse mode: spell anything at all with two blinks. Undecodable morse is
  dropped rather than guessed, and morse is never rate-limited — it is the only
  way a locked-in patient can say something that is not on offer.
- Auto-scan, morse autosend, and a five-point head calibration and a blink
  calibration, both saved per patient.
- Searchable question-and-answer history, with deletion that rewrites the file
  rather than dropping rows.

### Runs entirely on one machine

Speech recognition (whisper.cpp), answer generation (Qwen2.5-1.5B via
llama.cpp) and the synthesised voice (Piper) are all local. No account, no
telemetry, no network listener. The camera feed never leaves the process.

### Packaging

- `.deb` and `.rpm`, built per distribution release by
  `scripts/build-packages.sh` and verified by installing into a clean container
  and running the self-check there.
- Qt, OpenCV and SQLite come from the distribution. MediaPipe, llama.cpp,
  whisper.cpp, ggml and Piper are bundled, because no distribution ships them.
- Only the 3.6 MB face model is included. The speech and language models —
  about 1.2 GB — are downloaded on first use, and **the application is usable
  before that finishes**: morse spelling and the built-in phrasebook need
  nothing more than the face model. (Changed in 0.2.0: the packages now carry
  everything.)
- `openncomm --version` and `openncomm --check`, both of which run without a
  display, for reporting problems from a machine with no screen attached.

### Known limitations

- **The AppImage is broken.** `scripts/build-appimage.sh` produces a binary
  that crashes during Qt platform-plugin initialisation. Use a `.deb`, an
  `.rpm`, or `./run.sh`. See `docs/PLAN.md`, finding 13.
- **Installing the `.rpm` pulls roughly 1 GiB of unrelated packages**, 862 MiB
  of it the GDAL stack behind Fedora's `opencv-videoio`. None of it is used.
  The `.deb` does not have this to the same degree.
- A package only installs on the distribution release it was built for: it
  records the sonames it was linked against.
- English only. Tamil is deferred.
- x86_64 Linux only.

### Licensing

GPL-3.0-or-later, because Piper phonemizes through espeak-ng, which is
GPL-3.0 — a permissive licence here would not have described the binary that
actually runs. The packages carry the verbatim licence of every bundled
component and a written offer of source. See [THIRD_PARTY.md](THIRD_PARTY.md).

[0.2.0]: https://github.com/VishalRamKN/OpennComm-Core/releases/tag/v0.2.0
[0.1.0]: https://github.com/VishalRamKN/OpennComm-Core/releases/tag/v0.1.0
