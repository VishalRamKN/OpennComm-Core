<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

Notable changes to OpennComm. Dates are ISO 8601, versions follow
[Semantic Versioning](https://semver.org/).

Anything that changes what a patient's input is understood to mean is called
out explicitly, whatever its size. A one-line threshold change can alter which
answer gets spoken; a thousand-line packaging change cannot.

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
  nothing more than the face model.
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

[0.1.0]: https://github.com/VishalRamKN/OpennComm-Core/releases/tag/v0.1.0
