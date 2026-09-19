<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Contributing to OpennComm

There is no formal process yet — no CLA, no sign-off requirement. Open an issue
or a pull request. By contributing you agree your work is licensed
GPL-3.0-or-later, the same as the rest of the project.

There is **no commercial version and no plan for one**. Nobody is collecting
rights to relicense your work proprietarily later.

## Before you change anything, read this section

The list below is the whole reason this file exists. Every item looks like an
oversight and is not. Several are the difference between a patient being
understood and a patient having words put in their mouth. The most likely way
this project degrades is a well-meaning contributor tidying one of them away.

Each rule is also stated, with its reasoning, at the top of the header that owns
it in `core/include/openncomm/`.

1. **The ambiguous blink dead band stays.** Between `short_max_ms` and `long_ms`
   nothing happens. Closing that gap lets a cycling blink held slightly too long
   be read as a selection, and the machine speaks an answer the patient never
   chose. Do not raise `short_max_ms` to meet `long_ms`.

2. **Undecodable morse is dropped, never guessed.** Spelling the wrong word on a
   patient's behalf is worse than making them repeat a letter.

3. **A long blink commits once, on the closing edge**, while the eye is still
   shut — not on reopening. The patient gets the spoken answer as confirmation
   rather than having to hold and hope. `long_fired` guards both a second commit
   during the same closure and the release being read as a short blink.

4. **Calibration bounds come from column and row averages, never `min`/`max`.**
   min/max discard which side is which and can invert left/right outright on a
   mirrored feed. A negative `range_x` is normal and correct.

5. **`mirror_x` defaults to true**, and applies only before a calibration
   exists. If left/right is swapped but up/down is fine, look here.

6. **Selection is gated on `options_loaded`, never on button text.** Text
   sniffing once refused legitimate answers beginning "Look".

7. **Morse is never metered or rate-limited.** It is the only way a locked-in
   patient can say something that is not one of the offered options.

8. **Nothing a tour or setup screen provokes may count as an answer.**

9. **Never guess on the patient's behalf.** An ambiguous input is discarded and
   the patient repeats it. Speaking a sentence the patient did not choose is the
   worst outcome in the system.

10. **Patient effort is the scarce resource.** A design costing the caregiver a
    button press and the patient nothing is correct. The reverse is not.

11. **The patient must always be able to say something.** Every degraded path —
    no model, model failure, no disk space — still leaves four selectable
    options or morse spelling available.

## Building and testing

    ./scripts/fetch-deps.sh          # one-time: MediaPipe library and model
    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

`core/` is pure C with no I/O and no dependencies beyond libm, so its tests run
without a camera, a microphone, a model or a window. **Keep it that way.** If a
change to `core/` needs hardware to test, the change is in the wrong place.

The test suites carry the rules above. A patch that turns one red has not found
a flaky test.

## Patient data

OpennComm stores a patient's conversation history locally, and that history is
medical information about a person who often cannot consent to sharing it.

**Never attach a real database, export, or log to an issue or pull request, and
never derive test fixtures from real conversations**, however convenient it
would be for debugging. Reproduce bugs with synthetic input. If you cannot,
describe the input rather than pasting it.

## Licensing of dependencies

Before adding a dependency, check its licence against `THIRD_PARTY.md` and add
it there. Two standing constraints:

- **Qt must stay dynamically linked** — it is LGPL-3.0.
- **Model weights are licensed separately from code.** "Open weights" is not
  "open source". Models with usage restrictions (Llama, Gemma) are not
  acceptable, however good they are.
