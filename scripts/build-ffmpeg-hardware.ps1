[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$linuxRepo = (wsl.exe -d Ubuntu -e wslpath -a $repo).Trim()
if (-not $linuxRepo) { throw 'Could not map the SaberStage repository into WSL.' }

$command = "cd '$linuxRepo' && bash scripts/build-ffmpeg-hardware.sh"
if ($Force) { $command += ' --force' }
wsl.exe -d Ubuntu -e bash -lc $command
if ($LASTEXITCODE -ne 0) { throw "FFmpeg hardware runtime build failed with exit code $LASTEXITCODE" }
