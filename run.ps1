# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build if needed, then run OpennComm from the source tree. The counterpart of
# run.sh.
#
# Run from a Visual Studio developer prompt, with Qt and OpenCV findable:
#
#   $env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.1\msvc2022_64;C:\vcpkg\installed\x64-windows"
#   powershell -ExecutionPolicy Bypass -File run.ps1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$Build = Join-Path $Root 'build-windows'
$WinBin = Join-Path $Root 'third_party\win\bin'

$needed = @(
    (Join-Path $WinBin 'libmediapipe.dll'),
    (Join-Path $WinBin 'llama.dll'),
    (Join-Path $WinBin 'whisper.dll'),
    (Join-Path $Root 'models\face_landmarker.task'),
    (Join-Path $Root 'third_party\piper\piper.exe')
)
if ($needed | Where-Object { -not (Test-Path $_) }) {
    Write-Host "==> fetching dependencies"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\fetch-deps.ps1')
    if ($LASTEXITCODE -ne 0) { throw "fetch-deps.ps1 failed" }
}

if (-not (Test-Path (Join-Path $Build 'CMakeCache.txt'))) {
    & cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}
& cmake --build $Build --target openncomm
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# The vendored DLLs are not installed anywhere Windows would look, so point the
# loader at the copies in the source tree. An installed build has them beside
# the executable and needs none of this. Qt's own DLLs come from wherever
# CMAKE_PREFIX_PATH found them, which is normally already on PATH in a Qt
# developer shell; added here as well so a bare VS prompt also works.
$qtBin = @()
if ($env:CMAKE_PREFIX_PATH) {
    $qtBin = ($env:CMAKE_PREFIX_PATH -split ';') |
             ForEach-Object { Join-Path $_ 'bin' } |
             Where-Object { Test-Path $_ }
}
$env:PATH = (@($WinBin) + $qtBin + @($env:PATH)) -join ';'

# The models are found relative to the executable, and in a build tree the
# executable is three directories down from models\. paths.cc already looks
# there, but saying it explicitly means a build directory somewhere else still
# works.
$env:OPENNCOMM_MODELS = Join-Path $Root 'models'
$env:OPENNCOMM_PIPER = Join-Path $Root 'third_party\piper\piper.exe'

& (Join-Path $Build 'src\openncomm.exe') @args
exit $LASTEXITCODE
