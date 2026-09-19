# OpennComm — plan and findings

An assistive communication tool for patients who can blink or move their head
but cannot speak. Everything runs locally on one machine: no network, no HTTP,
no account, no telemetry. English only for now.

## Where it stands

| | |
|---|---|
| `core/` — blink timing, option scanning, morse, gaze, EAR | **done**, 111 assertions green |
| Landmark pipeline (risk spike) | **retired**, see below |
| Qt6 shell — blink + head mode, spoken answers | **runnable** |
| Answer generation (llama.cpp, Qwen2.5-1.5B) | **working**, 21/24 answers from the model |
| Audio out (Piper neural voice, espeak-ng fallback) | **working** |
| Audio in (whisper.cpp, push-to-talk) | **working** |
| Morse mode (spell any answer with two blinks) | **working** |
| Per-patient tuning: blink speed, morse letter gap | **working** |
| Five-point gaze calibration, saved between runs | **working** |
| Model downloads, CI, clean-clone build | **working** |
| `.deb` and `.rpm` packaging | **working**, built per distribution in a container |
| AppImage packaging | **produces a binary that crashes — see below** |
| Question and answer history, searchable and deletable | **working** |
| Builds on a distro other than the developer's | **verified on Ubuntu 24.04** |
| Auto-scan, morse autosend, head pointer | **working** |
| Blink calibration, measured per patient | **working** |

## Findings from the landmark spike

Measured on a 12th-gen i5-12450H, HP Wide Vision HD webcam, Fedora 43.

**1. MediaPipe ships an official C API, so there is nothing to convert.**
`mediapipe/tasks/c/libmediapipe.so` in the PyPI wheel exports
`MpFaceLandmarkerCreate` / `DetectForVideo` / `Close`. That gives us the
478-point refined mesh directly from upstream, so the landmark indices in
`core/ear.h` mean what the model says they mean, with no ONNX conversion and no
re-tuning of blink detection. This was the single largest risk in the project
and it cost 3.7 MB of model and 110 MB of library.

**2. Inference is not the bottleneck.** 15–18 ms mean on CPU, 33 ms worst, with
XNNPACK. The 30 fps budget is 33 ms, so there is roughly a 2x margin before the
GPU delegate is even worth considering.

**3. Camera exposure silently halves the frame rate, and that is a patient
problem.** Webcams default to aperture-priority auto-exposure with
`exposure_dynamic_framerate` on. In this room's light the camera lengthens
exposure and delivers **16 fps instead of 29** — silently.

Frame rate is the sampling resolution of every threshold in `core/blink.h`. At
16 fps a frame is 62 ms, so the 110 ms ignore floor is under two frames and the
180 ms ambiguous band is barely three. Blink classification gets coarse exactly
when the room is dim — which is to say, at a bedside, at night.

Forcing manual exposure restores 29 fps but, at this light level, darkens the
image until the face is not detected at all (0%, versus 99% on auto). So the fix
is not a constant. **The camera module needs a startup auto-tune**: walk
exposure down while gain compensates, and keep the shortest exposure that still
detects a face reliably. Report the achieved frame rate, and warn the caregiver
when it is too low for the configured blink speed.

None of this is reachable from a sandboxed camera API; it needs the V4L2
controls directly. Running natively is what makes the auto-tune possible at
all.

## Findings from the generation work

**1. The prompt mattered far more than the decoder.** The obvious phrasing --
"write four answers the patient might give" -- fails in an instructive way on a
1.5B model: it casts *itself* as the patient and writes one long conversational
reply ("I'm not in pain, but my muscles feel stiff..."). That yielded **2 usable
answers out of 24**. Rewriting it to say the model is writing *options for*
someone else, plus two worked examples, took it to **21 out of 24**. The worked
examples do most of the work; instructions alone are not enough at this size.

**2. GBNF grammars are only partly usable in llama.cpp v0.4.1.** The intent was
a structured grammar forcing exactly four lines. Any grammar built from
sub-rules and sequences aborts the process:

    Unexpected empty grammar stack after accepting piece: \n (198)

Reproduced with `root ::= line "\n" line "\n" line "\n" line "\n"`, with an
always-continuable tail added, and under greedy sampling with nothing else in
the chain -- so it is neither the tail nor the sampler order. A single flat
repetition is stable.

The reduced grammar that ships, `[a-zA-Z' \n]+`, still earns its place: it bans
digits and punctuation, which a speech synthesiser stumbles over. Line count was
never a safety property -- `oc_options_from_model()` drops unusable lines and
tops up from the phrasebook, so four good buttons are guaranteed regardless.
Worth revisiting on a later llama.cpp.

**3. Latency is 5.4 s mean on CPU, and it does not matter.** The phrasebook
fills the buttons the instant the question is asked, so nothing is blocked on
generation; answers swap in when they arrive. This is the design decision that
makes local inference viable on ordinary hardware. The grammar costs roughly
2 s of that, checking a 151k-token vocabulary each step.

**4. Threads must be set explicitly.** `llama_context_default_params()` left
generation at 13.5 s per question. Setting `n_threads` to cores-2 (leaving room
for the camera pipeline, which must keep hitting 30 fps) more than halved it.

## Findings from the speech work

**1. `parecord` ignores `--rate`.** It hands back the device's native rate --
48kHz on this machine -- whatever you ask for. Whisper accepts 16kHz and
nothing else, so that would have been transcribed as gibberish at three times
speed, and silently: there is no error, just bad text. ffmpeg resamples
correctly and is used instead, as a subprocess.

**2. Push-to-talk, not voice activity detection.** The caregiver holds a button
while speaking. This follows the rule that settles most arguments here: patient
effort is scarce, caregiver effort is not. An always-listening microphone
eventually transcribes a passing conversation as a question and puts words in
front of the patient that nobody asked for.

**3. Transcription costs about half a second** with `base.en-q5_1` (57MB) on
CPU. Measured against espeak-synthesised speech, three of four test questions
came back verbatim with punctuation. The fourth -- "Are you in pain right now"
-> "You've been paying right now" -- is a phonetic confusion on robotic
synthesis rather than a whisper limitation, but it is worth re-testing with a
human voice, because *pain* is the single most important word in this
application. If real speech shows the same weakness, move to `small.en`.

**4. Whisper hallucinates on silence**, producing confident, plausible
sentences from nothing. The listener drops clips under a second, clips below an
RMS floor, and anything whisper brackets as a non-speech event
(`[BLANK_AUDIO]`, `(wind blowing)`).

**5. A 1.5B model does not reliably tell an instruction about answers from an
answer.** The prompt asked for four answers covering "yes, no, a specific need,
and an alternative". The model duly offered **"A specific need"** as something
for the patient to say aloud. The spread is now taught by the worked examples
instead of named in a list. Caught by `--check`, which is exactly the kind of
thing that survives unnoticed when nobody looks at the output.

**6. `aplay` does not work on a stock PipeWire desktop.** It routes through an
ALSA plugin that is not installed and fails with "No such device". `paplay`
talks to the sound server directly and honours the raw format flags. Worth
knowing because the failure names the wrong thing.

**7. The voice's licence had to be traced by hand.** `en_US-amy-medium` records
its own licence as "See URL"; following it reaches CC-BY-SA-4.0. That is
share-alike and needs attribution, which is now in THIRD_PARTY.md. This is
exactly the check that gets skipped when a model "just works".

**8. `linuxdeploy` aborts on an unreadable `PATH` entry.** It walks every
directory in `PATH` and dies with `filesystem error: status: Permission denied`
on the first one it cannot read — a stale entry pointing into another user's
home is enough. `scripts/build-appimage.sh` filters `PATH` to readable
directories rather than making that the operator's problem. It also finds its
plugins by exact filename on `PATH`: the Qt plugin must be called
`linuxdeploy-plugin-qt`, not anything else.

**9. llama.cpp and whisper.cpp bake an absolute build-tree rpath into their
libraries.** Harmless on the build machine, wrong everywhere else: on another
machine that path either does not exist or holds different libraries. The
packaging script rewrites it to `$ORIGIN` when `patchelf` is available, and the
AppRun sets `LD_LIBRARY_PATH` regardless.

**10. `appimagetool` downloads the AppImage runtime mid-build**, and hangs
indefinitely in `SYN-SENT` if that connection stalls — silently, with no output
at all. `scripts/build-appimage.sh` fetches the runtime up front and passes
`--runtime-file`, so a network problem fails fast and visibly instead.

**11. `QStandardPaths::AppDataLocation` doubles the name.** With the
organisation and application both called OpennComm it returns
`~/.local/share/OpennComm/OpennComm`. The XDG path is now built directly, so it
is the one the documentation claims and the one a person would guess.

**12. There is no single models directory.** In a packaged build the face model
comes from inside the bundle while the large ones are downloaded to XDG data.
`--check` originally resolved everything from one directory and reported a
perfectly good installation as broken. Resolve each model on its own.

**13. The AppImage crashes, and the application is not at fault.** Established
by elimination, with the same binary each time:

| libraries on the path | result |
|---|---|
| ours + system Qt | runs |
| ours + bundled Qt | runs `--check`, crashes the GUI |
| linuxdeploy's bundled `usr/lib` | segfault in platform-plugin init |

The crash is inside `dlopen` of the Qt platform plugin, during its static
initialisation, and happens with both the xcb and wayland plugins. The likely
cause is linuxdeploy's ELF tooling being older than the host toolchain: its
bundled `strip` already fails on the `.relr.dyn` sections current binutils emit,
and it rewrites the rpath of all ~400 libraries it copies.

The fix is what AppImage practice says to do anyway — build inside a container
running the **oldest** distribution to be supported, not the newest. Podman is
available. Not done; `./run.sh` is the supported path meanwhile.

**14. llama.cpp and whisper.cpp both installed `libggml.so.0`, at different
versions.** 0.24 and 0.23, one SONAME, two ABIs. The dynamic linker resolves a
library once per SONAME per process, so whichever loaded first served both, and
the loser was calling a library it was not built against. It worked, which is
worse than failing: it was luck, not correctness.

**Fixed.** `scripts/fetch-deps.sh` now builds llama.cpp's ggml, installs it into
`third_party/prefix`, and builds whisper.cpp against it with
`WHISPER_USE_SYSTEM_GGML=ON`. whisper.cpp 1.9.4 compiles clean against ggml 0.24.
There is now one ggml on disk and one in the process; `--check` exercises both
runtimes and passes.

Two things worth keeping in mind. Separating the libraries into different
directories would NOT have fixed this -- the loader keys its cache on SONAME,
not on path, so an `$ORIGIN` rpath per runtime would have changed nothing. And
the ordering in `fetch-deps.sh` is load-bearing: the prefix must exist before
whisper.cpp is configured. A guard at the end of that script fails the build if
a `libggml*.so` ever reappears under `third_party/whisper.cpp/build`, because
the symptom of a regression here is not a build error, it is the wrong ABI at
runtime.

## Invariants

`core/` carries the rules that must not be quietly undone; each is stated at the
top of the header that owns it, with the reason. Briefly:

1. The ambiguous blink dead band between `short_max_ms` and `long_ms` stays.
2. Undecodable morse is dropped, never guessed.
3. A long blink commits once, on the closing edge, while the eye is still shut.
4. Calibration bounds come from column/row averages, never `min`/`max`.
5. `mirror_x` defaults to true and applies only before calibration exists.
6. Selection is gated on `options_loaded`, never on button text.
7. Morse is never metered — it is the only way a locked-in patient can say
   something that is not one of the offered options.
8. Blink calibration refuses rather than guesses when the patient's short and
   long blinks overlap, and its bounds come from the second most extreme
   sample in each group, never the most extreme — the same reasoning as 4.

## Findings from the interaction work

**15. End-of-speech detection rather than hold-to-talk.** Holding a button
cuts off the last word the moment a finger lifts early, so the microphone stops
itself instead. Three tries were needed to get the detector right, each one
measured by playing a question through the speakers into the microphone:

  - A single RMS threshold stopped the recording **2.3 s into a 3 s question**.
    The quiet consonants between words fall back under the onset threshold for
    longer than the trailing-silence window.
  - Making the continuation threshold a fraction of the onset threshold put it
    at 1.6x room tone, which room tone crosses as it fluctuates. The recording
    **never ended**, and whisper hallucinated a paragraph out of nine seconds of
    nothing.
  - Both thresholds now come from the measured noise floor independently:
    onset at 3.5x, continuation at 2.0x, with a minimum on each. Plus a
    1.4 s floor on how soon a question can end, so the first gap between words
    cannot be read as the end of a sentence.

**16. Whisper writes sentences out of silence, so the silence is cut.** Even
with the detector right, the ~1 s tail before the stop fires was enough:
"Are you in pain right now?" came back with "I am in pain right now but I can't
do it." appended, invented entirely from room tone. Trailing silence is now
trimmed from the buffer before transcription, measured from the audio rather
than the clock — capture starts a little after the timer and the two are not
aligned closely enough to cut on. Two runs afterwards, both exact.

**17. The phrasebook does not pre-empt the model.** Filling the buttons
instantly and replacing them when the model answers would let the four options
change under a patient part-way through choosing one, which is worse than
waiting. It is the fallback only: model failed, no model, or past twelve
seconds.

**18. The male voice had to be chosen on licence, not on quality.**
`en_US-ryan-medium` and `en_US-hfc_male-medium` are both better voices and both
CC-BY-**NC**-SA-4.0. Non-commercial is not an open source licence and is
incompatible with GPL-3.0 — a hospital could not redistribute a build
containing them. Shipping `en_US-joe-medium` (CC0) instead.

## Findings from the history work

**19. `VACUUM` then checkpoint, not the other way round.** Clearing the
history has to leave nothing readable in the file, not merely unlink the rows
— sqlite keeps deleted text in free pages. The obvious order, checkpoint the
write-ahead log and then vacuum, leaves the vacuum's own output sitting in a
fresh WAL and the main file never shrinks. A test asserting the file halved in
size caught it; the first version of that test used forty rows, which fitted
inside sqlite's minimum file size, so it was asserting nothing and could not
have failed for the right reason.

**20. Punctuation is not a diagram.** The morse screen wrote dots and dashes
as text, using U+00B7 and U+2013 at 42px. A middle dot set at 42px is still a
two-pixel speck and an en dash is a hairline, so the one element that changes
with every blink was the smallest and faintest thing on screen, while an empty
message box took a third of the window. The symbols are painted now, sized from
the widget, and the same drawing code fills the reference chart so the two
cannot drift apart. The chart went from ten columns to eight, because at ten a
five-symbol code collapsed into a smear.

**21. It did not build on any machine but mine, and neither failure was
obvious.** Verified in an Ubuntu 24.04 container -- glibc 2.39, gcc 13.3,
Qt 6.4.2, OpenCV 4.6 -- against Fedora 43 with Qt 6.10 and gcc 15.

  - `spike/` asked for Qt Core only, while `ui_shot` in it links
    `openncomm_lib` and therefore Qt Widgets. Qt's executable finalizer then
    resolves Gui's dependencies, including `OpenGL::GLX`, and those targets
    come from Qt6Gui's own `find_dependency(OpenGL)`, which had only ever run
    in `src/` -- imported targets are not visible across directories. On Qt
    6.10 the finalizer no longer asks, so this configured cleanly on the
    machine it was written on and died on Qt 6.4 with an error naming OpenGL
    and nothing else. Fixed by asking for Widgets where Widgets is used.
  - No minimum Qt version was declared at all, so Qt 6.2 -- Ubuntu 22.04,
    Debian 12 -- configured happily and failed minutes later inside a compile.
  - The microphone button used the U+1F3A4 emoji, which needs a colour emoji
    font. On a minimal install it drew as a replacement box.

Afterwards, on Ubuntu: configures, builds, 8/8 suites pass, the model writes
4 of 4 answers in 5.5 s, and the window renders. The container also built
`libggml.so.0.23.0`, independently reproducing finding 14.

**22. Three capabilities that a first pass would leave out, and should not.**

  - **Auto-scan** in blink mode. Genuinely important, and the reason is not
    convenience: for a patient who can blink but for whom each blink costs
    something, scanning turns "up to four short blinks plus one long one" into
    one long one. It lives in `oc_selector` with the rule that matters tested
    -- it must never move the highlight out from under a blink in progress, or
    it hands the patient an answer they did not choose. The interval restarts
    on a manual advance too, so one gesture cannot become two moves.
  - **Morse autosend.** `core/morse.c` implements and tests it, and the setting
    exposes it, with the countdown on screen for its whole duration -- a
    message that speaks itself with no warning is exactly the failure this
    project is arranged to prevent.
  - **A pointer for head mode.** It draws from `oc_gaze_position()`, which is
    the arithmetic `oc_gaze_quadrant()` reduces to a quadrant, and a test walks
    the whole field asserting the two never disagree. A patient trusts the
    pointer, so one that contradicted the highlighted button would be worse
    than none.

Deliberately out of scope: plan and quota machinery, which has no meaning in a
tool that runs on one machine; a guided tour and mode-chooser overlay, whose
job Settings does; and mirror auto-detection, which calibration folds in
already. Tamil is deferred.

## Next

1. Camera exposure auto-tune (finding 6).
2. AppImage, rebuilt inside an older-distro container (finding 13). Lower
   priority now that `.deb` and `.rpm` exist: those take the same "build in a
   container of the oldest release you support" discipline and do not have to
   bundle Qt to do it, which is what the AppImage cannot currently survive.

## Packaging

`.deb` and `.rpm` are built by `scripts/build-packages.sh`, each inside a
container of the distribution it targets. Both come from one staged tree
(`scripts/stage-install.sh`), so the two formats cannot drift apart.

The shape of it: system Qt, OpenCV and SQLite; everything no distribution
packages -- MediaPipe, llama.cpp, whisper.cpp, ggml, Piper -- bundled in a
private `/usr/lib64/openncomm`. This is the arrangement finding 13's table
already showed to work ("ours + system Qt: runs"), which is why it needed no
new debugging.

Three things that are easy to get wrong here, all of them now enforced rather
than remembered:

- **The vendored libraries carry absolute RUNPATHs** from the machine that
  built them, and MediaPipe carries one pointing into Google's Bazel layout.
  `cmake/patch-rpath.cmake` rewrites them all to `$ORIGIN` at install time and
  fails loudly if it finds nothing to patch -- which it did once, silently,
  when the prefix was resolved at configure time and `--prefix` then moved it.
- **A package is pinned to the sonames it was linked against.** The Fedora 43
  rpm requires `Qt_6.10` and `libopencv_core.so.411` and will not install on
  Fedora 42. One build per release, oldest first.
- **rpmbuild cannot handle a space in `_topdir`.** This checkout lives in
  "Website Projects", so `scripts/build-rpm.sh` does its work under `/var/tmp`
  and copies the finished package back. The same space is why the vendored
  libraries' baked-in RUNPATHs were not merely wrong but unparseable: a RUNPATH
  is colon-separated with no quoting.
- **dpkg and rpm disagree about what a dependency is.** rpm generates a
  requirement for every `DT_NEEDED` entry, so the rpm correctly asked for
  `libGLX.so.0`, `libOpenGL.so.0`, `libEGL.so.1` and `libGLESv2.so.2`.
  `dpkg-shlibdeps` generates one only where a symbol is actually *used*, and
  nothing here calls a GL function — Qt's CMake package pulls GL into the link
  while finalizing any target, as the README warns. So the first `.deb` named
  none of the four. Found by diffing the two packages' metadata against each
  other, which is worth doing whenever a project ships both.

  How bad it was is worth stating precisely, because the obvious version of
  this story is wrong. `libqt6gui6` on Debian already depends on `libegl1`,
  `libglx0` and `libopengl0`, so three of the four arrived anyway — latently,
  by way of somebody else's packaging decision rather than ours. The fourth,
  `libgles2`, is pulled in by nothing: it is needed by the private
  `libmediapipe.so`, which `dpkg-shlibdeps` does not examine at all. That one
  was a real missing dependency, and the same absence had already broken the
  build container earlier ("libGLESv2.so.2 => not found"). All four are now
  declared by hand in `scripts/build-deb.sh`, including the three that would
  have worked by luck.

- **Installing the rpm pulls about 1 GiB of things OpennComm never uses.**
  Measured in a bare `fedora:42` container: the 53 MB package schedules 483
  packages and 1 GiB of downloads. `dnf install gdal-libs` on its own accounts
  for 171 packages and 862 MiB of that — Fedora's `opencv-videoio` links the
  full GDAL stack, which drags in PDAL, arrow, hdf5 and the `proj-data-*`
  cartographic grids (`proj-data-us` alone unpacks to 319 MiB). None of it is
  reachable from this application, which uses OpenCV for camera capture and
  colour conversion and nothing else.

  The `.deb` is better but not clean: 366 packages in the same conditions, of
  which 5 are gdal/proj. Both figures are inflated by the bare container, which
  starts with no Qt, no mesa and no X11 — a real desktop already has that part.
  The geospatial stack is the part it would *not* already have, which is why it
  is the number worth quoting.

  Not fixed, and not trivially fixable: the dependency is real as far as the
  linker is concerned, because `libopencv_videoio.so` is what `src/camera.cc`
  calls. The options are to capture video without OpenCV, or to vendor a
  minimal OpenCV built without GDAL — and the second gives up exactly the
  system-library principle that makes these packages work at all. Worth
  revisiting before recommending the rpm to anyone on a metered connection.

`scripts/test-package.sh` exists for this class of bug: it installs a built
package into a clean container of its own distribution and runs `--check`
there, so apt or dnf resolves the package's declared dependencies with nothing
from the build tree present. It confirmed the fixed `.deb`. It did not find the
bug — a metadata diff did — which is the argument for doing both.

## Settled

- **Licence.** GPL-3.0-or-later. Piper phonemizes through espeak-ng, which is
  GPL-3.0, so a permissive licence on this repository would not have described
  the binary that actually runs. The reasoning is in the README; the full
  dependency inventory is in `THIRD_PARTY.md`.
- **Copyright.** Initial development by Vishal Ram K N and Drisha P. Source
  headers read `Copyright (C) 2026 OpennComm contributors`, so contributors
  are credited without the headers needing to change per patch.
