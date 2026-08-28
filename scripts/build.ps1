[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$qpm = 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe'
$ndk = 'C:\Users\Owner\AppData\Roaming\QPM-RS\ndk\android-ndk-r27d'
$cmake = 'C:\Users\Owner\AppData\Local\SaberStage\tools\cmake\cmake\data\bin\cmake.exe'
$ninja = 'C:\Users\Owner\AppData\Local\Programs\QPM\ninja.exe'

Push-Location $repo
try {
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
}
finally {
    Pop-Location
}
