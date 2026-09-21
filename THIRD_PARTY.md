<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party components

OpennComm is licensed GPL-3.0-or-later. Everything below is compatible with
that, but the terms differ and several require attribution in distributed
builds. Verified 2026-09-18; packaging section added 2026-09-19; model weights
moved into the packages, with the obligations that follow from it, 2026-09-20.

## Libraries

| Component | License | Notes |
|---|---|---|
| MediaPipe (`libmediapipe.so`) | Apache-2.0 | Face landmarks. Attribution required. |
| OpenCV | BSD-3-Clause / Apache-2.0 | Camera capture and colour conversion. |
| Qt 6 | **LGPL-3.0-only** | Must be **dynamically linked**. Never link Qt statically, and never bundle a modified Qt without also shipping the means to relink it. |
| espeak-ng | **GPL-3.0** | Phonemization for speech synthesis. This is the component that makes the combined work GPL-3.0. |
| Piper | MIT, bundling **GPL-3.0** espeak-ng | The prebuilt `rhasspy/piper` binary, invoked as a subprocess and never linked. It carries its own onnxruntime and libespeak-ng. Upstream's maintained successor (`OHF-Voice/piper1-gpl`) relicensed to GPL-3.0 for exactly this reason. |
| llama.cpp | MIT | Answer generation. |
| whisper.cpp | MIT | Speech recognition. |
| ggml | MIT | The tensor library under both of the above. Built once, from llama.cpp's copy, and shared — whisper.cpp is compiled against it rather than vendoring a second one. See CONTRIBUTING.md, rule 12. |
| onnxruntime | MIT | Inside the prebuilt Piper binary; never linked by this project. |
| ffmpeg | LGPL-2.1+ / GPL-2+ | Microphone capture, as a subprocess. Invoked, never linked. |
| SQLite | blessing (public domain) | Local history. |
| PipeWire / PulseAudio | MIT / LGPL-2.1+ | Audio capture and playback. |

## What the packages actually redistribute

Until there were packages this section did not need to exist: a source tree
distributes nothing. The `.deb` and `.rpm` do, so these are the components
whose terms now apply to a shipped artifact rather than to a build:

Filenames below are the Linux ones. The Windows packages carry the same
components under Windows names — `libmediapipe.dll`, `llama.dll`,
`whisper.dll`, `ggml*.dll`, `piper.exe`, `onnxruntime.dll`, `espeak-ng.dll` —
and the obligations are identical.

| Shipped in the package | License | Obligation |
|---|---|---|
| `libmediapipe.so` | Apache-2.0 | attribution, NOTICE |
| `libllama.so`, `libwhisper.so`, `libggml*.so` | MIT | copyright notice |
| Piper binary, `libonnxruntime.so` | MIT | copyright notice |
| `libespeak-ng.so` and its data | **GPL-3.0** | source offer |
| `face_landmarker.task` | Apache-2.0 | attribution |
| `ggml-base.en-q5_1.bin` | MIT (OpenAI) | copyright notice |
| `qwen2.5-1.5b-instruct-q4_k_m.gguf` | Apache-2.0 | attribution |
| `voices/en_US-amy-medium.onnx` | **CC-BY-SA-4.0** | **credit, and share-alike on the voice** |
| `voices/en_US-joe-medium.onnx` | CC0-1.0 | none |

On Linux, Qt, OpenCV, SQLite and ffmpeg are **not** redistributed — the `.deb`
and the `.rpm` depend on the distribution's copies. That is what keeps the LGPL
obligation on Qt trivial to meet, and it is worth preserving for that reason as
well as the technical ones.

### What the Windows packages additionally redistribute

Windows has no distribution to depend on. The installer and the portable
archive therefore carry Qt and OpenCV themselves, and terms that bound only a
build on Linux bind a shipped artifact here:

| Additionally shipped on Windows | License | Obligation |
|---|---|---|
| `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6Multimedia.dll` and the Qt plugins | **LGPL-3.0** | licence text, and the user must be able to relink against their own Qt |
| `opencv_world*.dll` | Apache-2.0 | attribution |
| `sqlite3.dll` | public domain | none |

**The LGPL condition is met by construction, not by promise.** Qt is
dynamically linked and its DLLs are separate files sitting beside
`openncomm.exe`; replacing them with a user's own build of the same Qt version
requires no cooperation from us and nothing more than copying files over. That
is what LGPL-3.0 section 4 asks for, and it is why Qt must never be statically
linked into this application — see the note in `src/CMakeLists.txt`.

The verbatim texts ship in `doc\licenses\` beside the binaries:
`LGPL-3.0.txt` and `opencv.LICENSE`. LGPL-3.0 is written as a set of additional
permissions on top of GPL-3.0 and is not readable without it; the GPL-3.0 text
is already there as `espeak-ng.COPYING`, and `LICENSE` at the top of the
install directory is the same licence again, for OpennComm itself.

Qt Multimedia is used on Windows only. On Linux the same two jobs — reaching
the microphone and the speaker — are done by `ffmpeg` and `paplay` as
subprocesses, which is why the Linux packages have no Qt Multimedia dependency
to redistribute or to depend on. See `src/audio.h`.

**The model weights are in the package, as of 0.2.0.** They used to be
downloaded by the user on first run, and this section used to say that their
terms therefore bound the user and not the artifact. That is no longer true.
Every licence in the last five rows above is now an obligation on the `.deb`
and the `.rpm` themselves.

One of them has teeth. `en_US-amy-medium` is CC-BY-SA-4.0: the credit has to
travel with the voice, and a *modified* voice would have to be redistributed
under the same licence. `packaging/MODEL-NOTICE`, installed at
`/usr/share/doc/openncomm/MODEL-NOTICE`, is that credit — it names every
model, where it came from, and what it is under. The share-alike condition
attaches to the voice, not to OpennComm: CC-BY-SA-4.0 is one-way compatible
with GPL-3.0, so the combination is fine, and the voice ships unmodified.

The packages carry the verbatim licence of everything in that table, in
`/usr/share/doc/openncomm/licenses/`, alongside this file, `MODEL-NOTICE` and
the project `LICENSE`. All of them but two are fetched and checksummed by
`scripts/fetch-deps.sh`; llama.cpp's and whisper.cpp's come from their own
checked-out trees.

espeak-ng is GPL-3.0, so the packages also carry `WRITTEN-OFFER`, the source
offer that section 6 requires. It names the upstream repository and tag, and
points at `scripts/fetch-deps.sh` for the pinned revision and checksum of every
vendored component, so a build can be reproduced from the repository alone.

## Why the project is GPL-3.0

Every dependency above is permissive or LGPL **except espeak-ng**, which is
GPL-3.0 and which the speech synthesis path cannot currently avoid. Rather than
keep espeak-ng permanently behind a subprocess boundary in order to claim a
permissive licence the shipped binary would not honour, the project is GPL-3.0
so that the stated licence and the distributed artifact are the same thing.

## Model weights are licensed separately

**Open weights is not open source.** A model's licence is independent of this
project's, and some widely-used models are not OSI-licensed and carry usage
restrictions. Any model shipped by OpennComm must be verified before it is
added here — and since 0.2.0 every one of these is inside the packages, so
"shipped" is meant literally. Adding a model now means adding its licence text
to `scripts/fetch-deps.sh`, its install rule in `src/CMakeLists.txt`, and its
attribution to `packaging/MODEL-NOTICE`.

| Model | Licence | Status |
|---|---|---|
| `face_landmarker.task` (MediaPipe) | Apache-2.0 | shipped |
| `ggml-base.en-q5_1.bin` (Whisper) | MIT | shipped — speech recognition |
| `en_US-amy-medium` (Piper voice) | **CC-BY-SA-4.0** | shipped — attribution required, see below |
| `en_US-joe-medium` (Piper voice) | CC0 (public domain) | shipped — no conditions |
| Qwen2.5-1.5B-Instruct Q4_K_M | Apache-2.0 | shipped — answer generation |

Deliberately excluded from consideration, because their licences are not open
source and restrict use and redistribution:

  - `en_US-ryan-medium` and `en_US-hfc_male-medium` (Piper voices) —
    CC-BY-**NC**-SA-4.0. Both are better male voices than the one shipped, and
    neither can be used: the non-commercial clause is not an open source
    licence and is incompatible with GPL-3.0. A hospital could not redistribute
    a build containing them, which is the whole point of the project. That is
    why the male voice here is `en_US-joe-medium` (CC0) instead.
  - Llama 3.x — Meta Llama Community License
  - Gemma — Google Gemma Terms of Use

Qwen2.5-1.5B-Instruct was selected and its Apache-2.0 licence confirmed against
the specific checkpoint on Hugging Face.

### Voice attribution

The `en_US-amy-medium` voice is **CC-BY-SA-4.0**, from
[MycroftAI/mimic3-voices](https://github.com/MycroftAI/mimic3-voices). That is
share-alike, and it is the one component here whose licence is not simply
inherited by the project's own: any distributed build must credit it, and a
modified voice must be shared under the same terms. CC-BY-SA-4.0 is one-way
compatible with GPL-3.0, so combining it with this project is fine.

Since the packages ship the voice itself, that credit is a shipped file:
`packaging/MODEL-NOTICE`, installed beside the licence texts. If the voice is
ever swapped or retrained, that file and `third_party/licenses/` move with it.

Auditing it was not a formality. The voice's own model card records its licence
only as "See URL"; the terms had to be traced to the upstream dataset.
