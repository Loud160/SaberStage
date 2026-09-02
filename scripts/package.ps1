# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Stages and packages a validated SaberStage qmod.
# - Only declared runtime files enter the archive; local diagnostics and build caches are excluded.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$qpm = 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe'

Push-Location $repo
try {
    & $qpm qmod manifest
    if ($LASTEXITCODE -ne 0) { throw "QMOD manifest generation failed with exit code $LASTEXITCODE" }
    & $qpm qmod zip
    if ($LASTEXITCODE -ne 0) { throw "QMOD packaging failed with exit code $LASTEXITCODE" }
    & python (Join-Path $repo 'tests\VerifyPackage.py')
    if ($LASTEXITCODE -ne 0) { throw "QMOD verification failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}
