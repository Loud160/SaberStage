# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Installs the development Camera2 preset used for repeatable pose screenshots.
# - Existing user presets are preserved unless the explicit target name is selected.

[CmdletBinding()]
param(
    [string]$BeatSaberDir = "C:\Users\Owner\BSManager\BSInstances\1.37.1"
)

$ErrorActionPreference = "Stop"
$source = Join-Path (Split-Path -Parent $PSCommandPath) "camera2\SaberStage IK Diagnostic.json"
$cameraDirectory = Join-Path $BeatSaberDir "UserData\Camera2\Cameras"

if (-not (Test-Path -LiteralPath (Join-Path $BeatSaberDir "Plugins\Camera2.dll"))) {
    throw "Camera2.dll was not found in the selected PC Beat Saber installation: $BeatSaberDir"
}

New-Item -ItemType Directory -Path $cameraDirectory -Force | Out-Null
$destination = Join-Path $cameraDirectory "SaberStage IK Diagnostic.json"
Copy-Item -LiteralPath $source -Destination $destination -Force
Write-Host "Installed Camera2 diagnostic preset: $destination"
Write-Host "Select this preset in Camera2 when you want analyzer screenshots from that view. Existing scene bindings were not changed."
