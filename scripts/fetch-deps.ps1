# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetch and build the binary dependencies, on Windows. The counterpart of
# scripts/fetch-deps.sh, and it keeps that script's rules:
#
# Everything is pinned and checksummed. That is not ceremony: the landmark
# model decides how every blink threshold in core/ behaves, so a silently
# updated file would move the thresholds a patient was tuned against without
# anything appearing to change. A version, never a "latest".
#
# The model, voice and licence checksums below are the same values as in
# fetch-deps.sh, deliberately -- they are the same files. If one is ever
# changed, it has to be changed in both places or the two platforms ship
# different weights under one version number.
#
# Run from a Visual Studio developer prompt ("x64 Native Tools Command Prompt"),
# which is where cl, lib and dumpbin live. The check at the top says so if not.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Invoke-WebRequest renders a progress bar per block, which on a 1.1 GB file
# costs more time than the download does.
$ProgressPreference = 'SilentlyContinue'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("openncomm-deps-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null
# `break` so the error still propagates: a trap without it runs the handler and
# then carries on at the next statement, which here would mean continuing the
# build after a checksum mismatch.
trap { Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue; break }

# Where cmake/deps.cmake expects to find the finished artifacts. One flat pair
# of directories, because MSVC scatters the .dll and the .lib of one library
# into different build subdirectories and which ones depends on the generator.
$WinBin = Join-Path $Root 'third_party\win\bin'
$WinLib = Join-Path $Root 'third_party\win\lib'
New-Item -ItemType Directory -Path $WinBin, $WinLib -Force | Out-Null

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

function Require-Tool($name, $advice) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "$name was not found on PATH.`n  $advice"
    }
}

function Get-Sha256($path) {
    (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLower()
}

# True if the file on disk is already the pinned artifact.
function Test-Pinned($path, $sha) {
    (Test-Path $path) -and ((Get-Sha256 $path) -eq $sha.ToLower())
}

function Assert-Sha($path, $sha) {
    $got = Get-Sha256 $path
    if ($got -ne $sha.ToLower()) {
        Remove-Item -Force $path -ErrorAction SilentlyContinue
        throw "checksum mismatch: $path`n  expected $sha`n  got      $got"
    }
}

# Checks what is already on disk rather than trusting its presence. Verifying
# only at download time would leave a truncated or altered model in place
# forever, which for the landmark model means blink thresholds that quietly no
# longer mean what core/ says.
function Get-Pinned($path, $url, $sha, $what) {
    if (Test-Pinned $path $sha) { return }
    if (Test-Path $path) {
        Write-Host "==> $path does not match its pinned checksum, refetching"
        Remove-Item -Force $path
    }
    Write-Host "==> fetching $what"
    $dir = Split-Path -Parent $path
    if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    # curl.exe, shipped with Windows since 1803: it resumes, it reports
    # progress usefully on a 1.1 GB file, and it fails loudly on an HTTP error
    # instead of writing the error page to disk.
    & curl.exe -fL --progress-bar -o $path $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
    Assert-Sha $path $sha
}

# A tag can be moved; a commit cannot. Cloning shallow by tag is fast, so the
# tag buys the speed and the commit buys the guarantee.
function Assert-Pin($dir, $sha, $tag) {
    $got = (& git -C $dir rev-parse HEAD).Trim()
    if ($got -ne $sha) {
        throw "$dir`: tag $tag no longer points at the pinned commit`n  expected $sha`n  got      $got"
    }
}

function Copy-Artifact($searchRoot, $filename, $dest) {
    $hit = Get-ChildItem -Path $searchRoot -Filter $filename -Recurse -File -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if (-not $hit) { throw "$filename was not produced under $searchRoot" }
    Copy-Item -Force $hit.FullName (Join-Path $dest $filename)
}

# ---------------------------------------------------------------------------
# toolchain
# ---------------------------------------------------------------------------

Require-Tool git    'Install Git for Windows: https://git-scm.com/download/win'
Require-Tool cmake  'Install CMake and tick "Add to PATH": https://cmake.org/download/'
Require-Tool ninja  'Install Ninja: winget install Ninja-build.Ninja'
Require-Tool cl     'Open the "x64 Native Tools Command Prompt for VS" and run this from there.'
Require-Tool lib    'Open the "x64 Native Tools Command Prompt for VS" and run this from there.'
Require-Tool dumpbin 'Open the "x64 Native Tools Command Prompt for VS" and run this from there.'

# ---------------------------------------------------------------------------
# MediaPipe
# ---------------------------------------------------------------------------
#
# The C API arrives as a prebuilt library inside the mediapipe Python wheel --
# there is no source build of it to do, on any platform. Fetched by its exact
# file URL rather than through pip, because that URL is content-addressed and
# can be checksummed, and because it means this script does not need a Python
# installation to build a C++ application.

$MpVersion = '1.0.1'
$MpWheelUrl = 'https://files.pythonhosted.org/packages/22/71/42365b0aec2a96dfbeb3441220fe8dccd9a833f36adfecf3aa9f211c449b/mediapipe-1.0.1-py3-none-win_amd64.whl'
$MpWheelSha = '96dc9de6bd04a6315ef424fda5c48e0929f2d78317295e75bc32c0bceeab517b'
$MpDllSha   = '31335db8bb8cd4bb294fd689b6b06086eb33782ee9c7f4667e12a6014a68436c'

$MpDll = Join-Path $WinBin 'libmediapipe.dll'
$MpImportLib = Join-Path $WinLib 'libmediapipe.lib'

if (-not ((Test-Pinned $MpDll $MpDllSha) -and (Test-Path $MpImportLib))) {
    Write-Host "==> fetching libmediapipe.dll (mediapipe $MpVersion)"
    # Named .zip, not .whl: a wheel IS a zip, but Expand-Archive checks the
    # extension and refuses anything else.
    $wheel = Join-Path $Tmp 'mediapipe-wheel.zip'
    Get-Pinned $wheel $MpWheelUrl $MpWheelSha "the mediapipe wheel (~20 MB)"

    $unpack = Join-Path $Tmp 'wheel'
    Expand-Archive -Path $wheel -DestinationPath $unpack -Force
    Copy-Item -Force (Join-Path $unpack 'mediapipe\tasks\c\libmediapipe.dll') $MpDll
    Assert-Sha $MpDll $MpDllSha

    # The wheel carries the DLL and no import library, because Python loads it
    # at runtime and never links against it. MSVC cannot link a DLL directly,
    # so one is generated from the DLL's own export table: dumpbin lists what
    # it exports, and lib turns that list into the .lib the linker wants.
    #
    # This is exact rather than approximate -- the names come out of the binary
    # that will actually be loaded, so the import library cannot describe a
    # different version of the library than the one beside it.
    Write-Host "==> generating an import library for libmediapipe.dll"
    $def = Join-Path $Tmp 'libmediapipe.def'
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($line in (& dumpbin /NOLOGO /EXPORTS $MpDll)) {
        # "  ordinal hint RVA name" -- and nothing else in the output has that
        # shape. Forwarded exports carry a trailing "= target" which is dropped
        # with the rest of the line.
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') {
            $names.Add($Matches[1])
        }
    }
    if ($names.Count -lt 100) {
        throw "dumpbin found only $($names.Count) exports in libmediapipe.dll; expected several hundred"
    }
    # LIBRARY, explicitly: it is what the import library records as the name to
    # load at runtime. Left to default it follows the .lib filename, and a
    # rename would then silently produce a binary looking for the wrong DLL.
    $lines = @('LIBRARY libmediapipe.dll', 'EXPORTS') + $names
    Set-Content -Path $def -Value $lines -Encoding ASCII

    & lib /NOLOGO "/DEF:$def" /MACHINE:X64 "/OUT:$MpImportLib" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "lib failed to build the mediapipe import library" }
    Write-Host "    $($names.Count) exports"
}

# Version 1, not "latest" -- see the note at the top of this file.
Get-Pinned 'models\face_landmarker.task' `
  'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task' `
  '64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff' `
  'face_landmarker.task'

# ---------------------------------------------------------------------------
# llama.cpp, and the one ggml both runtimes share
# ---------------------------------------------------------------------------
#
# One ggml, not two. llama.cpp and whisper.cpp each vendor their own copy and
# each builds it as ggml.dll -- at 0.24 and 0.23 respectively. Windows resolves
# a DLL once per name per process, so whichever directory is searched first
# would serve both, and the loser would be calling a library it was not built
# against. It happens to work, which is worse than failing, because it is luck
# rather than correctness.
#
# So llama.cpp's ggml is built and installed here, and whisper.cpp is then built
# against it with WHISPER_USE_SYSTEM_GGML. This ordering is load-bearing: the
# prefix must exist before whisper.cpp is configured.
$GgmlPrefix = Join-Path $Root 'third_party\prefix'

$LlamaTag = 'v0.4.1'
$LlamaSha = 'b29c606e28a01b1bc8c1351026a0fa6e616bf6c4'
$LlamaDir = Join-Path $Root 'third_party\llama.cpp'

if (-not ((Test-Path (Join-Path $WinBin 'llama.dll')) -and
          (Test-Path (Join-Path $GgmlPrefix 'lib\cmake\ggml\ggml-config.cmake')))) {
    Write-Host "==> building llama.cpp $LlamaTag (a few minutes)"
    if (-not (Test-Path $LlamaDir)) {
        & git clone -q --depth 1 --branch $LlamaTag https://github.com/ggml-org/llama.cpp.git $LlamaDir
        if ($LASTEXITCODE -ne 0) { throw "git clone of llama.cpp failed" }
    }
    Assert-Pin $LlamaDir $LlamaSha $LlamaTag

    # Ninja rather than the Visual Studio generator, to match the Unix side:
    # single-config, so the artifacts land in one predictable place instead of
    # a Release\ subdirectory that only exists for multi-config builds.
    #
    # CMAKE_INSTALL_LIBDIR is pinned to `lib` so the staging prefix has the same
    # shape on every platform. Do NOT pass -DGGML_LIB_INSTALL_DIR: it is
    # declared `CACHE PATH`, and a relative PATH given on the command line is
    # resolved against the working directory, silently becoming an absolute one.
    & cmake -S $LlamaDir -B (Join-Path $LlamaDir 'build') -G Ninja `
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON `
        "-DCMAKE_INSTALL_PREFIX=$GgmlPrefix" -DCMAKE_INSTALL_LIBDIR=lib `
        -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF `
        -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_CURL=OFF | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "configuring llama.cpp failed" }

    # Only the library: llama.cpp's own CLI target fails to configure
    # build-info.h in this layout, and we do not need it.
    & cmake --build (Join-Path $LlamaDir 'build') --target llama
    if ($LASTEXITCODE -ne 0) { throw "building llama.cpp failed" }

    # The ggml subdirectory only. A full `cmake --install` of llama.cpp fails
    # looking for the CLI binary we deliberately did not build, and installing
    # only --component ggml omits the CMake package files whisper.cpp needs.
    & cmake --install (Join-Path $LlamaDir 'build\ggml') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "installing ggml failed" }
}

# ggml is several libraries -- ggml, ggml-base, ggml-cpu -- and only `ggml` is
# linked against; the rest are loaded through it at runtime and so need the DLL
# but not the import library. Copied by glob rather than by name because which
# backends get built depends on what the machine has.
Get-ChildItem -Path (Join-Path $GgmlPrefix 'bin') -Filter 'ggml*.dll' -File |
    ForEach-Object { Copy-Item -Force $_.FullName $WinBin }
Get-ChildItem -Path (Join-Path $GgmlPrefix 'lib') -Filter 'ggml*.lib' -File |
    ForEach-Object { Copy-Item -Force $_.FullName $WinLib }
Copy-Artifact (Join-Path $LlamaDir 'build') 'llama.dll' $WinBin
Copy-Artifact (Join-Path $LlamaDir 'build') 'llama.lib' $WinLib

# Apache-2.0 weights. See THIRD_PARTY.md for why that rules out Llama and Gemma.
Get-Pinned 'models\qwen2.5-1.5b-instruct-q4_k_m.gguf' `
  'https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf' `
  '6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e' `
  'the language model (~1.1 GB)'

# ---------------------------------------------------------------------------
# whisper.cpp
# ---------------------------------------------------------------------------

$WhisperTag = 'v1.9.4'
$WhisperSha = '927cfce34f31707e17f2bff35c349632fb9e2c3a'
$WhisperDir = Join-Path $Root 'third_party\whisper.cpp'

if (-not (Test-Path (Join-Path $WinBin 'whisper.dll'))) {
    Write-Host "==> building whisper.cpp $WhisperTag"
    if (-not (Test-Path $WhisperDir)) {
        & git clone -q --depth 1 --branch $WhisperTag https://github.com/ggml-org/whisper.cpp.git $WhisperDir
        if ($LASTEXITCODE -ne 0) { throw "git clone of whisper.cpp failed" }
    }
    Assert-Pin $WhisperDir $WhisperSha $WhisperTag

    # Against llama.cpp's ggml, not its own vendored copy. See the note above.
    & cmake -S $WhisperDir -B (Join-Path $WhisperDir 'build') -G Ninja `
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON `
        -DWHISPER_USE_SYSTEM_GGML=ON "-Dggml_DIR=$GgmlPrefix\lib\cmake\ggml" `
        -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=OFF `
        -DWHISPER_BUILD_SERVER=OFF | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "configuring whisper.cpp failed" }

    & cmake --build (Join-Path $WhisperDir 'build') --target whisper
    if ($LASTEXITCODE -ne 0) { throw "building whisper.cpp failed" }
}
Copy-Artifact (Join-Path $WhisperDir 'build') 'whisper.dll' $WinBin
Copy-Artifact (Join-Path $WhisperDir 'build') 'whisper.lib' $WinLib

Get-Pinned 'models\ggml-base.en-q5_1.bin' `
  'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin' `
  '4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f' `
  'the speech model (~57 MB)'

# ---------------------------------------------------------------------------
# Piper
# ---------------------------------------------------------------------------
#
# A neural voice, so the patient does not have to sound like a machine. Shipped
# as a self-contained binary with its own onnxruntime and espeak-ng beside it,
# which is why there is nothing to build here -- and why, on Windows, nothing
# has to be told where those DLLs are: the loader searches the directory the
# .exe was loaded from. See src/speech.cc.
#
# Downloaded to a file and checksummed before extraction rather than streamed
# into the unpacker: an archive that is never written down is an archive whose
# contents cannot be checked before they are unpacked.
$PiperUrl = 'https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip'
$PiperZipSha = 'f3c58906402b24f3a96d92145f58acba6d86c9b5db896d207f78dc80811efcea'
$PiperExeSha = '96f3da3811151580073e40bb4dd20eb0fb8115f5f5f76e2fb54282b3edfa5c1f'
$PiperExe = Join-Path $Root 'third_party\piper\piper.exe'

if (-not (Test-Pinned $PiperExe $PiperExeSha)) {
    Write-Host "==> fetching Piper"
    $zip = Join-Path $Tmp 'piper.zip'
    Get-Pinned $zip $PiperUrl $PiperZipSha 'Piper (~40 MB)'
    Remove-Item -Recurse -Force (Join-Path $Root 'third_party\piper') -ErrorAction SilentlyContinue
    Expand-Archive -Path $zip -DestinationPath (Join-Path $Root 'third_party') -Force
    Assert-Sha $PiperExe $PiperExeSha
}

# ---------------------------------------------------------------------------
# Voices
# ---------------------------------------------------------------------------
#
# Two voices, one of each. Which one a patient is given is not a cosmetic
# choice: this is the voice other people will hear as theirs, and being handed
# the wrong one every day is its own small indignity. Either can be picked in
# Settings. Licences are in THIRD_PARTY.md.
$VoiceRoot = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US'
$Voices = @(
  @{ Path = 'amy/medium';  Name = 'en_US-amy-medium.onnx';      Sha = 'b3a6e47b57b8c7fbe6a0ce2518161a50f59a9cdd8a50835c02cb02bdd6206c18' },
  @{ Path = 'amy/medium';  Name = 'en_US-amy-medium.onnx.json'; Sha = '95a23eb4d42909d38df73bb9ac7f45f597dbfcde2d1bf9526fdeaf5466977d77' },
  @{ Path = 'joe/medium';  Name = 'en_US-joe-medium.onnx';      Sha = '58afce0321b8d9c46d7cdf9c16500cc55a793b4220212dba6b70fb788b3baf06' },
  @{ Path = 'joe/medium';  Name = 'en_US-joe-medium.onnx.json'; Sha = '3d6d5410b3795cb1950595247ef8f06190719e6fdbfa3a2356d8ec368e1aad33' }
)
foreach ($v in $Voices) {
    Get-Pinned "models\voices\$($v.Name)" "$VoiceRoot/$($v.Path)/$($v.Name)" $v.Sha "voice $($v.Name)"
}

# ---------------------------------------------------------------------------
# Licences
# ---------------------------------------------------------------------------
#
# The licence texts of everything the packages redistribute.
#
# This is not tidiness. The installer ships espeak-ng.dll, which is GPL-3.0, and
# Apache-2.0 section 4 and the MIT notice clause both require the licence to
# travel with the binary. A source tree distributes nothing and so needed none
# of this; packages do.
#
# llama.cpp and whisper.cpp carry theirs in their own checked-out trees, so only
# the ones that arrive as prebuilt binaries are fetched. Pinned and checksummed
# like everything else here: a licence that silently changed under us would be
# the one file where nobody would think to look.
$Licences = @(
  @{ Path = 'third_party\licenses\mediapipe.LICENSE'
     Url  = 'https://raw.githubusercontent.com/google-ai-edge/mediapipe/v0.10.21/LICENSE'
     Sha  = '8707eef0533987efc5b155d64761eeb6e20793f50b9bd1a68dad1cf4719d0ed8'
     What = 'the MediaPipe licence (Apache-2.0)' },
  @{ Path = 'third_party\licenses\piper.LICENSE'
     Url  = 'https://raw.githubusercontent.com/rhasspy/piper/2023.11.14-2/LICENSE.md'
     Sha  = '4cd71dece7037f1d6d93cce7570c57ab75ea9ac566fd4990be2f3ab08d15b47f'
     What = 'the Piper licence (MIT)' },
  @{ Path = 'third_party\licenses\espeak-ng.COPYING'
     Url  = 'https://raw.githubusercontent.com/espeak-ng/espeak-ng/1.52.0/COPYING'
     Sha  = '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903'
     What = 'the espeak-ng licence (GPL-3.0)' },
  @{ Path = 'third_party\licenses\onnxruntime.LICENSE'
     Url  = 'https://raw.githubusercontent.com/microsoft/onnxruntime/v1.14.1/LICENSE'
     Sha  = '2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c'
     What = 'the onnxruntime licence (MIT)' },
  # Taken from SPDX's tagged license-list-data rather than creativecommons.org,
  # which serves the legal code as HTML that changes shape without notice; a tag
  # can be pinned and checksummed like everything else here.
  @{ Path = 'third_party\licenses\CC-BY-SA-4.0.txt'
     Url  = 'https://raw.githubusercontent.com/spdx/license-list-data/v3.26.0/text/CC-BY-SA-4.0.txt'
     Sha  = 'cde7883b9050a1104f4ac19a1572aafd6e5d7323b68351aaf51fbf4beba54966'
     What = 'the CC-BY-SA-4.0 licence (the en_US-amy-medium voice)' },
  @{ Path = 'third_party\licenses\CC0-1.0.txt'
     Url  = 'https://raw.githubusercontent.com/spdx/license-list-data/v3.26.0/text/CC0-1.0.txt'
     Sha  = 'a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499'
     What = 'the CC0-1.0 dedication (the en_US-joe-medium voice)' },
  @{ Path = 'third_party\licenses\qwen2.5.LICENSE'
     Url  = 'https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/LICENSE'
     Sha  = '832dd9e00a68dd83b3c3fb9f5588dad7dcf337a0db50f7d9483f310cd292e92e'
     What = 'the Qwen2.5 licence (Apache-2.0)' },
  # The weights, not whisper.cpp -- the code is Georgi Gerganov's MIT licence,
  # already in that checkout, and the model is OpenAI's, under a different one.
  @{ Path = 'third_party\licenses\whisper-model.LICENSE'
     Url  = 'https://raw.githubusercontent.com/openai/whisper/v20231117/LICENSE'
     Sha  = 'b5d65a59060e68c4ff940e1eddfa6f94b2d68fdf58ed7f4dd57721c997e35e9d'
     What = 'the Whisper model licence (MIT)' },

  # These last two are needed on Windows and on no other platform, and the
  # reason is worth stating plainly.
  #
  # The .deb and the .rpm do not redistribute Qt, OpenCV or SQLite: they depend
  # on the distribution's copies, which is what keeps the LGPL obligation on Qt
  # trivial to meet. Windows has no distribution to depend on, so the installer
  # and the portable archive carry Qt and OpenCV themselves -- and the terms
  # that bound a build on Linux now bind a shipped artifact.
  #
  # Qt stays dynamically linked, in separate DLLs beside the executable that
  # anybody can replace with their own build, which is what LGPL-3.0 section 4
  # asks for. What it also asks for is the licence text itself, which is why it
  # is fetched here. See THIRD_PARTY.md.
  @{ Path = 'third_party\licenses\LGPL-3.0.txt'
     Url  = 'https://raw.githubusercontent.com/spdx/license-list-data/v3.26.0/text/LGPL-3.0-only.txt'
     Sha  = '996af0513df21f7496288951c41428a03c174e9e4a9d63665c57d670f845ccb1'
     What = 'the LGPL-3.0 licence (Qt, redistributed on Windows)' },
  @{ Path = 'third_party\licenses\opencv.LICENSE'
     Url  = 'https://raw.githubusercontent.com/opencv/opencv/4.11.0/LICENSE'
     Sha  = 'cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30'
     What = 'the OpenCV licence (Apache-2.0, redistributed on Windows)' }
)
foreach ($l in $Licences) { Get-Pinned $l.Path $l.Url $l.Sha $l.What }

# ---------------------------------------------------------------------------
# The invariant, guarded rather than assumed
# ---------------------------------------------------------------------------
#
# A whisper.cpp bump that quietly stopped honouring WHISPER_USE_SYSTEM_GGML
# would put a second ggml.dll back beside the first, and the symptom would not
# be a build failure -- it would be one of the two runtimes calling the wrong
# ABI at runtime. Fail here instead.
$stray = Get-ChildItem -Path (Join-Path $WhisperDir 'build') -Filter 'ggml*.dll' -Recurse -File -ErrorAction SilentlyContinue
if ($stray) {
    Write-Host "whisper.cpp built its own ggml -- that is the name collision returning:"
    $stray | ForEach-Object { Write-Host "  $($_.FullName)" }
    throw "refusing to continue with two copies of ggml"
}

Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "==> deps ready, checksums verified"
Get-ChildItem $WinBin | Select-Object Name, Length | Format-Table -AutoSize
Get-ChildItem 'models' -File | Select-Object Name, Length | Format-Table -AutoSize
