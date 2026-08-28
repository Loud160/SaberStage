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
