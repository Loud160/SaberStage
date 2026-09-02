# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Restores dependencies, generates embedded assets, and builds SaberStage for Quest.
# - All one-click and manual paths converge here so they produce the same native binary.

[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$qpm = 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe'
$ndk = 'C:\Users\Owner\AppData\Roaming\QPM-RS\ndk\android-ndk-r27d'
$cmake = 'C:\Users\Owner\AppData\Local\SaberStage\tools\cmake\cmake\data\bin\cmake.exe'
$ninja = 'C:\Users\Owner\AppData\Local\Programs\QPM\ninja.exe'
$ffmpegReady = Join-Path $repo '.cache\dependencies\ffmpeg-hardware\saberstage-ffmpeg-9.0.1.ready'

Push-Location $repo
try {
    & python (Join-Path $PSScriptRoot 'prepare-native-logger.py')
    if ($LASTEXITCODE -ne 0) { throw "Native Logger Quest preparation failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $ffmpegReady)) {
        & (Join-Path $PSScriptRoot 'build-ffmpeg-hardware.ps1')
        if ($LASTEXITCODE -ne 0) { throw "private FFmpeg hardware runtime build failed with exit code $LASTEXITCODE" }
    }
    & $qpm restore
    if ($LASTEXITCODE -ne 0) { throw "qpm restore failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $ndk)) { throw "Android NDK r27d not found at $ndk" }
    if (-not (Test-Path -LiteralPath $cmake)) { throw "CMake not found at $cmake; see docs/BUILD_AND_DEPLOY.md" }
    if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja not found at $ninja" }

    if ($Clean -and (Test-Path -LiteralPath (Join-Path $repo 'build'))) {
        Remove-Item -LiteralPath (Join-Path $repo 'build') -Recurse -Force
    }

    & $cmake -S $repo -B (Join-Path $repo 'build') -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_ANDROID_NDK=$ndk" `
        -DANDROID_ABI=arm64-v8a `
        -DANDROID_PLATFORM=android-29 `
        "-DCMAKE_MAKE_PROGRAM=$ninja"
    if ($LASTEXITCODE -ne 0) { throw "Quest configure failed with exit code $LASTEXITCODE" }
    & $cmake --build (Join-Path $repo 'build') --parallel 1
    if ($LASTEXITCODE -ne 0) { throw "Quest build failed with exit code $LASTEXITCODE" }
    & python (Join-Path $PSScriptRoot 'verify-native-library.py') (Join-Path $repo 'build\libsaberstage.so')
    if ($LASTEXITCODE -ne 0) { throw "native dependency validation failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}
