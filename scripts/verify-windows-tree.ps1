# SPDX-License-Identifier: GPL-3.0-or-later
#
# Check that an installed or packaged Windows tree is actually complete.
#
# Shared by scripts/build-windows.ps1 and by CI, for the same reason
# scripts/stage-install.sh does this work on Linux: what ships is what gets
# checked. A tree missing a DLL produces an application that installs cleanly
# and will not start; a tree missing a model produces one that starts, tracks
# the face, and then has no voice -- which is the one failure the patient using
# it cannot report.
#
#   powershell -File scripts\verify-windows-tree.ps1 -Tree <directory>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Tree
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not (Test-Path $Tree)) { throw "no such tree: $Tree" }
Write-Host "==> verifying $Tree"

# Named individually rather than counted. A count tells you something is
# missing; a name tells you which part of the application has quietly stopped
# existing.
$Required = @(
  @{ Path = 'openncomm.exe';        Why = 'the application' },

  @{ Path = 'libmediapipe.dll';     Why = 'face tracking -- without it there is no input at all' },
  @{ Path = 'llama.dll';            Why = 'written answers' },
  @{ Path = 'whisper.dll';          Why = 'speech recognition' },
  @{ Path = 'ggml.dll';             Why = 'the tensor library both runtimes call' },

  @{ Path = 'Qt6Core.dll';          Why = 'Qt' },
  @{ Path = 'Qt6Gui.dll';           Why = 'Qt' },
  @{ Path = 'Qt6Widgets.dll';       Why = 'Qt' },
  @{ Path = 'Qt6Multimedia.dll';    Why = 'the microphone and the speaker -- see src/audio.h' },
  # Without this the application exits at startup naming a plugin that was
  # never copied, rather than anything the person did.
  @{ Path = 'platforms\qwindows.dll'; Why = 'the Qt platform plugin' },

  @{ Path = 'piper\piper.exe';      Why = 'the neural voice' },
  @{ Path = 'piper\onnxruntime.dll'; Why = 'Piper' },
  @{ Path = 'piper\espeak-ng.dll';  Why = 'Piper' },
  @{ Path = 'piper\espeak-ng-data\phontab'; Why = 'Piper phoneme data' },

  @{ Path = 'models\face_landmarker.task';              Why = 'face tracking' },
  @{ Path = 'models\ggml-base.en-q5_1.bin';             Why = 'speech recognition' },
  @{ Path = 'models\qwen2.5-1.5b-instruct-q4_k_m.gguf'; Why = 'written answers' },
  @{ Path = 'models\voices\en_US-amy-medium.onnx';      Why = 'a voice' },
  @{ Path = 'models\voices\en_US-amy-medium.onnx.json'; Why = 'a voice cannot load without its config' },
  @{ Path = 'models\voices\en_US-joe-medium.onnx';      Why = 'a voice' },
  @{ Path = 'models\voices\en_US-joe-medium.onnx.json'; Why = 'a voice cannot load without its config' },

  # Not paperwork: espeak-ng is GPL-3.0 and the amy voice is CC-BY-SA-4.0, and
  # both require their terms to travel with the binary. See THIRD_PARTY.md.
  @{ Path = 'LICENSE';                    Why = 'the licence' },
  @{ Path = 'doc\DISCLAIMER';             Why = 'this is not a medical device, and says so' },
  @{ Path = 'doc\MODEL-NOTICE';           Why = 'where each model came from and what it is under' },
  @{ Path = 'doc\WRITTEN-OFFER';          Why = 'the GPL-3.0 source offer for espeak-ng' },
  @{ Path = 'doc\licenses\CC-BY-SA-4.0.txt'; Why = 'the en_US-amy-medium voice requires it' },
  @{ Path = 'doc\licenses\espeak-ng.COPYING'; Why = 'GPL-3.0, and what LGPL-3.0 below is written on top of' },
  # Qt and OpenCV are redistributed on Windows and on no other platform, so
  # these two files exist in this tree and in no Linux package.
  @{ Path = 'doc\licenses\LGPL-3.0.txt';    Why = 'Qt ships in this package' },
  @{ Path = 'doc\licenses\opencv.LICENSE';  Why = 'OpenCV ships in this package' }
)

$missing = @()
foreach ($r in $Required) {
    if (-not (Test-Path (Join-Path $Tree $r.Path))) {
        $missing += "  $($r.Path)  -- $($r.Why)"
    }
}

# Two libraries whose filename depends on how they were built, so they are
# matched rather than named: OpenCV is opencv_world4110.dll in the official
# prebuilt but opencv_core4110.dll and friends from a source or vcpkg build,
# and sqlite3 is sqlite3.dll from vcpkg and something else from anywhere else.
# Both are copied by CMake reading the imported targets that were actually
# linked, so the thing that can go wrong is that mechanism silently finding
# nothing -- which an exact filename would not catch either.
# sqlite3 is here because it is the one CMake does NOT collect on its own --
# see the note in src/CMakeLists.txt. A statically linked sqlite would
# legitimately have no DLL, but that is not the configuration this is built and
# tested in, so a missing one means the install rule stopped working.
$Patterns = @(
  @{ Glob = 'opencv_*.dll'; Why = 'OpenCV -- the camera' },
  @{ Glob = 'sqlite3*.dll'; Why = 'SQLite -- the conversation history' }
)
foreach ($p in $Patterns) {
    if (-not (Get-ChildItem $Tree -Filter $p.Glob -File -ErrorAction SilentlyContinue)) {
        $missing += "  $($p.Glob)  -- $($p.Why)"
    }
}

if ($missing.Count -gt 0) {
    Write-Host "this tree is incomplete:"
    $missing | ForEach-Object { Write-Host $_ }
    throw "refusing to ship it"
}

# Sizes, not just names: a truncated copy passes an existence test and then
# fails at load, which is much later and much harder to read.
foreach ($m in @('face_landmarker.task',
                 'ggml-base.en-q5_1.bin',
                 'qwen2.5-1.5b-instruct-q4_k_m.gguf',
                 'voices\en_US-amy-medium.onnx',
                 'voices\en_US-joe-medium.onnx')) {
    $want = (Get-Item (Join-Path $Root "models\$m")).Length
    $got  = (Get-Item (Join-Path $Tree  "models\$m")).Length
    if ($want -ne $got) {
        throw "models\$m is $got bytes in the tree, $want in the source"
    }
}

$bytes = (Get-ChildItem $Tree -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host "==> complete: $([math]::Round($bytes / 1GB, 2)) GB"
