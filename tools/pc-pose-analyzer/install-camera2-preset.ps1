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
