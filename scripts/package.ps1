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
