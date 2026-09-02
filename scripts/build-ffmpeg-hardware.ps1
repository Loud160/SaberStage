# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Builds the pinned Quest FFmpeg hardware-enabled runtime from Windows/WSL.
# - Artifacts stay versioned and isolated from any system FFmpeg installation.

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
