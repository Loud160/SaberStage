# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Builds SaberStage Android shader AssetBundles with the pinned Unity project.
# - The output is verified before replacing the bundle embedded by the native build.

[CmdletBinding()]
param([string] $UnityEditor)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$project = Join-Path $repositoryRoot "tools/avatar-shader"
$output = Join-Path $project "Build/Android"
$assetDirectory = Join-Path $repositoryRoot "assets"
$asset = Join-Path $assetDirectory "saberstage_avatar_shaders"
if (-not $UnityEditor) {
    $UnityEditor = Join-Path $env:ProgramFiles "Unity/Hub/Editor/2022.3.33f1/Editor/Unity.exe"
}
if (-not (Test-Path -LiteralPath $UnityEditor -PathType Leaf)) {
    throw "Unity 2022.3.33f1 is required to match Beat Saber 1.40.8. Pass -UnityEditor with its Unity.exe path."
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
New-Item -ItemType Directory -Path $assetDirectory -Force | Out-Null
$env:SABERSTAGE_AVATAR_SHADER_OUTPUT = $output
$log = Join-Path $project "Build/unity-avatar-shader.log"
Write-Output "Building SaberStage's Quest MToon shaders. Unity may spend several minutes importing the shader project."
$arguments = @("-batchmode", "-nographics", "-quit", "-projectPath", ('"' + $project + '"'),
    "-executeMethod", "BuildSaberStageAvatarShaders.BuildAndroid", "-logFile", ('"' + $log + '"'))
$process = Start-Process -FilePath $UnityEditor -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 100 }
    throw "Unity failed to build the SaberStage avatar shaders (exit $($process.ExitCode))."
}
$built = Join-Path $output "saberstage_avatar_shaders"
if (-not (Test-Path -LiteralPath $built -PathType Leaf)) { throw "Unity completed without producing $built" }
Copy-Item -LiteralPath $built -Destination $asset -Force
Write-Output "Built $asset"
Write-Output "SHA-256: $((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant())"
