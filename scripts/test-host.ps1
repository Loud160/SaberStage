# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Builds and runs platform-neutral C++ tests plus Python tooling tests.
# - This is the fast validation gate before an ARM64 Quest build.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$qpm = 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe'

Push-Location $repo
try {
    & $qpm restore
    if ($LASTEXITCODE -ne 0) { throw "qpm restore failed with exit code $LASTEXITCODE" }

    $wslRepo = (& wsl.exe wslpath -a ($repo -replace '\\', '/')).Trim()
    & wsl.exe bash -lc "cmake -S '$wslRepo' -B '$wslRepo/build-host' -G Ninja -DSABERSTAGE_HOST_TESTS=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build '$wslRepo/build-host' --parallel 1 && ctest --test-dir '$wslRepo/build-host' --output-on-failure"
    if ($LASTEXITCODE -ne 0) { throw "host tests failed with exit code $LASTEXITCODE" }

    & python (Join-Path $repo 'tests\ToolingTests.py')
    if ($LASTEXITCODE -ne 0) { throw "tooling and repository tests failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}
