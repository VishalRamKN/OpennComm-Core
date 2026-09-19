<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party components

OpennComm is licensed GPL-3.0-or-later. Everything below is compatible with
that, but the terms differ and several require attribution in distributed
builds. Verified 2026-09-18; packaging section added 2026-09-19.

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

| Shipped in the package | License | Obligation |
|---|---|---|
| `libmediapipe.so` | Apache-2.0 | attribution, NOTICE |
| `face_landmarker.task` | Apache-2.0 | attribution |
| `libllama.so`, `libwhisper.so`, `libggml*.so` | MIT | copyright notice |
| Piper binary, `libonnxruntime.so` | MIT | copyright notice |
| `libespeak-ng.so` and its data | **GPL-3.0** | source offer |

Qt, OpenCV, SQLite and ffmpeg are **not** redistributed — the packages depend
on the distribution's copies. That is what keeps the LGPL obligation on Qt
trivial to meet, and it is worth preserving for that reason as well as the
technical ones.

The model weights are **not** in the package either. They are downloaded on
first use, which is why the CC-BY-SA-4.0 voice below imposes nothing on the
package itself.

The packages carry the verbatim licence of everything in that table, in
`/usr/share/doc/openncomm/licenses/`, alongside this file and the project
`LICENSE`. The four that arrive as prebuilt binaries are fetched and
checksummed by `scripts/fetch-deps.sh`; llama.cpp's and whisper.cpp's come from
their own checked-out trees.

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
restrictions. Any model shipped or auto-downloaded by OpennComm must be
verified before it is added here.

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

Auditing it was not a formality. The voice's own model card records its licence
only as "See URL"; the terms had to be traced to the upstream dataset.
