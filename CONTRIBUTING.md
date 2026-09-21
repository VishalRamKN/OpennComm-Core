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

    ./scripts/fetch-deps.sh          # one-time: libraries, models and voices
    cmake -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

`core/` is pure C with no I/O and no dependencies beyond libm, so its tests run
without a camera, a microphone, a model or a window. **Keep it that way.** If a
change to `core/` needs hardware to test, the change is in the wrong place.

The test suites carry the rules above. A patch that turns one red has not found
a flaky test.

### Building on Windows

Needed once: [Visual Studio 2022 with the C++
workload](https://visualstudio.microsoft.com/downloads/) (the Build Tools are
enough), [CMake](https://cmake.org/download/), Ninja, Git,
[Qt 6.4+ with the Qt Multimedia module](https://www.qt.io/download-qt-installer),
and OpenCV 4 and SQLite 3. The quickest route to the last two is the official
[prebuilt OpenCV](https://opencv.org/releases/) and `vcpkg install
sqlite3:x64-windows`.

Then, **from the "x64 Native Tools Command Prompt for VS 2022"** — the fetch
script needs `cl`, `lib` and `dumpbin`, which only exist in that shell:

    $env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.1\msvc2022_64;C:\vcpkg\installed\x64-windows"
    powershell -ExecutionPolicy Bypass -File run.ps1

That fetches the dependencies, builds, and opens the window, the same way
`./run.sh` does on Linux. `scripts\build-windows.ps1` produces the installer
and the portable archive.

Four things differ from the Linux build, and all four are in the code rather
than in your head:

- **Qt Multimedia is a Windows-only dependency.** `src/audio.cc` uses
  `QAudioSource` and `QAudioSink` there; on Linux the same two jobs are done by
  `ffmpeg` and `paplay`, which are already installed and already know which
  sound server is running. Asking for Multimedia on Linux too would put a
  dependency into every distribution package to serve code that platform never
  compiles.
- **MediaPipe ships no import library.** The wheel has `libmediapipe.dll` and
  nothing to link against, because Python loads it at runtime.
  `fetch-deps.ps1` generates one from the DLL's own export table with `dumpbin`
  and `lib`. That is why a developer prompt is required and a plain PowerShell
  window is not.
- **The install is one flat directory,** not a prefix: `openncomm.exe` with
  `models\`, `piper\` and the DLLs beside it. `src/paths.cc` looks for that
  layout as well as the Unix one, and it is what lets the portable `.zip` run
  from a USB stick.
- **No `patchelf` and no RPATH.** A DLL records no search path; Windows looks
  in the executable's own directory. `cmake/patch-rpath.cmake` is not run.

Before sending a Windows change, run `scripts\verify-windows-tree.ps1` against
an installed tree. It checks for every DLL, model and licence by name, which is
what stops a package that installs cleanly and then cannot start — or worse,
starts and has no voice.

## If you touch packaging

Packages are built with `./scripts/build-packages.sh`, and checked by
installing one into a clean container and running the application there:

    ./scripts/test-package.sh deb

Five rules here, numbered on from the list above because they exist for the
same reason — each looks like an oversight and is not:

12. **There is exactly one ggml in the process.** llama.cpp and whisper.cpp
    each vendor their own and each calls it `libggml.so.0`, at different
    versions. `scripts/fetch-deps.sh` builds llama.cpp's, installs it to
    `third_party/prefix`, and builds whisper.cpp against it with
    `WHISPER_USE_SYSTEM_GGML=ON`; the ordering in that script is load-bearing.

    Putting them in separate directories does **not** fix this. The dynamic
    linker resolves a library once per SONAME per process and keys its cache on
    the SONAME, not the path, so the first `libggml.so.0` loaded serves both
    and the other runtime silently calls an ABI it was not built against. That
    is not a crash; it is wrong answers. A guard at the end of `fetch-deps.sh`
    fails the build if a `libggml*.so` reappears under `third_party/whisper.cpp`.

13. **Vendored libraries get their RUNPATH rewritten to `$ORIGIN` on install.**
    llama.cpp and whisper.cpp bake in the absolute path of the tree that built
    them, and MediaPipe arrives pointing into Google's internal Bazel layout.
    `cmake/patch-rpath.cmake` does this and fails loudly if it patches nothing.
    Do not "simplify" that failure away — it fired once already on a real bug,
    and the silent version of it ships libraries with someone else's paths.

14. **The AppStream id is one name across five files.** It is
    `io.github.vishalramkn.OpennComm-Core` — reverse-DNS, matching the
    repository — and it is simultaneously the `.desktop` filename, both icon
    filenames, the `.metainfo.xml` filename, the `<id>`, the `<launchable>` and
    the `Icon=` line. A desktop environment associates metadata, icon and
    launcher *by that name matching*, so a half-finished rename does not fail
    any build; it produces an application that installs fine and shows up in
    GNOME Software with no icon and no description. `src/CMakeLists.txt` spells
    it once, in `OPENNCOMM_APPID`. Do not rename the executable or the package
    to match — those are plain `openncomm`, deliberately.

15. **Both package formats come from one staged tree.** `stage-install.sh`
    builds it; `build-deb.sh` and `build-rpm.sh` only wrap it. If the `.deb`
    and the `.rpm` ever disagree about what is inside them, nobody outside can
    see it. Note that `dpkg-shlibdeps` and rpm's generator do **not** agree
    about what a dependency is: shlibdeps omits libraries whose symbols go
    unused, so the GL stack is listed by hand in `build-deb.sh` and explained
    there.

16. **The packages carry every model.** All seven files — face, speech,
    language and both voices — ship inside the `.deb` and the `.rpm`, which is
    why they are 1.4 GB. Do not trade that back for a smaller download. The
    person this is installed for cannot tell you that the voice never
    arrived; a package that installs, opens, tracks the face and then silently
    cannot speak puts the cost of the missing gigabyte on them instead of on
    whoever downloaded it. `stage-install.sh` and `build-packages.sh` both
    fail on a missing model, and `test-package.sh` mounts nothing from this
    tree so that it can actually tell.

    Adding or swapping a model is four edits, not one: the download and its
    checksum in `fetch-deps.sh`, the install rule in `src/CMakeLists.txt`, the
    attribution in `packaging/MODEL-NOTICE`, and the row in `THIRD_PARTY.md`.
    The notice is not paperwork — `en_US-amy-medium` is CC-BY-SA-4.0, and that
    credit is a condition of shipping the voice at all.

## Repository settings

One-time, and one of them is load-bearing: `SECURITY.md` tells people to report
privately through GitHub's **Report a vulnerability** button, and that button
does not exist until the setting is on.

- **Private vulnerability reporting** (Settings → Security).
- **Secret scanning with push protection.**
- **Branch protection** on `main`.
- Disable the wiki unless you intend to use it.

## Releasing

`VERSION` is the single source of truth. Three things move with it, and
`scripts/stage-install.sh` and CI both fail if the first two disagree:

1. `VERSION`
2. the `<release version=... date=...>` block in `packaging/*.metainfo.xml`,
   which is what software centres display
3. `CHANGELOG.md`

Then tag, and build packages per distribution release with
`scripts/build-packages.sh` — one per release you support, oldest first, since
a package only installs on the release it was linked against. Check each one
before attaching it to anything:

    ./scripts/test-package.sh deb
    ./scripts/test-package.sh rpm

That installs the package into a clean container and runs the application's
self-check there, so the dependency resolver supplies Qt, OpenCV and the GL
stack rather than the build tree. It is the only step that exercises what
somebody actually downloads.

The Windows artifacts are built once, on a Windows machine, and are not per
release: a Windows binary records no library versions the way an rpm records
sonames, so one build serves Windows 10 1809 and everything after it.

    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1

That produces `OpennComm-<version>-windows-x64.exe` and
`...-windows-x64.zip` in `build-windows\`, from one `cmake --install` tree so
the two cannot disagree, and refuses to finish if
`scripts\verify-windows-tree.ps1` finds anything missing. Install the `.exe`
on a clean machine and run `openncomm.exe --check` before attaching either to
a release — that is the Windows equivalent of `test-package.sh`, and there is
no container shortcut for it.

Publish the SHA-256 of both Windows artifacts in the release notes. They are
not code-signed, so SmartScreen will call the publisher unknown, and a checksum
is the only way somebody can tell what they downloaded is what was built.

Two things belong in the release notes, because neither is visible from the
download: that this is **not a medical device** (link `DISCLAIMER`), and that
every package is about 1.4 GB because every model and both voices are inside
it, with nothing to download after installing.

Add a `CHANGELOG.md` entry for anything a user would notice. Anything that
changes what a patient's input is understood to mean goes in regardless of how
small the diff is — a one-line threshold change can alter which answer gets
spoken.

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
