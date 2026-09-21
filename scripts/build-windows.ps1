# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build OpennComm on Windows and produce both shipping artifacts: the .exe
# installer and the portable .zip. The counterpart of scripts/build-deb.sh and
# scripts/build-rpm.sh.
#
# Run from a Visual Studio developer prompt ("x64 Native Tools Command Prompt
# for VS 2022"), with Qt and OpenCV findable. The usual way to say where they
# are is CMAKE_PREFIX_PATH:
#
#   $env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.1\msvc2022_64;C:\vcpkg\installed\x64-windows"
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
#
# Options:
#   -SkipDeps    do not re-run fetch-deps.ps1 (it is idempotent, just slow)
#   -NoPackage   build only, do not run cpack

[CmdletBinding()]
param(
    [switch]$SkipDeps,
    [switch]$NoPackage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProgressPreference = 'SilentlyContinue'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$Build = Join-Path $Root 'build-windows'

if (-not $SkipDeps) {
    Write-Host "==> fetching dependencies"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\fetch-deps.ps1')
    if ($LASTEXITCODE -ne 0) { throw "fetch-deps.ps1 failed" }
}

# The AppStream <releases> block carries a version of its own, and it is what
# the Linux software centres display. It is checked here as well as in
# stage-install.sh so that a Windows-only release cannot be the one that lets
# the two drift apart -- they are built from the same VERSION file, and an
# 0.2.0 installer beside a 0.1.0 metainfo is a support problem nobody can see
# from the outside.
$Version = (Get-Content (Join-Path $Root 'VERSION') -Raw).Trim()
$Meta = Get-ChildItem (Join-Path $Root 'packaging') -Filter '*.metainfo.xml' | Select-Object -First 1
if ($Meta) {
    $MetaVersion = ([xml](Get-Content $Meta.FullName)).component.releases.release[0].version
    if (-not $MetaVersion) {
        $MetaVersion = ([xml](Get-Content $Meta.FullName)).component.releases.release.version
    }
    if ($MetaVersion -ne $Version) {
        throw "version mismatch: VERSION says $Version, $($Meta.Name) says $MetaVersion`nUpdate the <releases> block, with the release date, before building."
    }
}
Write-Host "==> building OpennComm $Version"

# Ninja and an explicit Release build. The default with no CMAKE_BUILD_TYPE is
# a debug-ish build on MSVC, which for a program doing face landmarking at
# 30fps is the difference between working and not -- see kMinUsableFps in
# src/camera.cc.
& cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

& cmake --build $Build --target openncomm
if ($LASTEXITCODE -ne 0) { throw "build failed" }

if ($NoPackage) {
    Write-Host "==> built, not packaged (-NoPackage)"
    exit 0
}

Write-Host "==> packaging (this takes a while: ~1.4 GB of models)"
Push-Location $Build
try {
    & cpack
    if ($LASTEXITCODE -ne 0) { throw "cpack failed" }
} finally {
    Pop-Location
}

# The artifacts are what ship, so they are what get checked. CPack's staging
# directory is the tree that went into both the installer and the archive.
$Staged = (Get-ChildItem (Join-Path $Build '_CPack_Packages') -Recurse -Directory `
             -Filter "OpennComm-$Version-windows-x64" -ErrorAction SilentlyContinue |
           Select-Object -First 1)
if (-not $Staged) { throw "could not find the staged tree under $Build\_CPack_Packages" }

& powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\verify-windows-tree.ps1') `
    -Tree $Staged.FullName
if ($LASTEXITCODE -ne 0) { throw "the packaged tree is incomplete" }

Write-Host ""
Write-Host "==> done"
Get-ChildItem $Build -Filter 'OpennComm-*' -File |
    Select-Object Name, @{ N = 'MB'; E = { [math]::Round($_.Length / 1MB) } } |
    Format-Table -AutoSize
