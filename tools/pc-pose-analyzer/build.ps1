[CmdletBinding()]
param(
    [string]$BeatSaberDir = "C:\Users\Owner\BSManager\BSInstances\1.37.1",
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSCommandPath
$projectFile = Join-Path $projectDir "SaberStage.PcPoseAnalyzer.csproj"
$loader = Join-Path $BeatSaberDir "Beat Saber_Data\Managed\IPA.Loader.dll"

if (-not (Test-Path -LiteralPath $loader)) {
    throw "BeatSaberDir does not point to a modded PC Beat Saber installation: $BeatSaberDir"
}

Write-Host "Building SaberStage PC Pose Analyzer against: $BeatSaberDir"
dotnet build $projectFile -c Release -p:BeatSaberDir="$BeatSaberDir"
if ($LASTEXITCODE -ne 0) {
    throw "PC Pose Analyzer build failed with exit code $LASTEXITCODE."
}

$dll = Join-Path $projectDir "bin\Release\net472\SaberStage.PcPoseAnalyzer.dll"
if (-not (Test-Path -LiteralPath $dll)) {
    throw "The build completed without producing the expected plugin: $dll"
}

Write-Host "Built: $dll"

if ($Install) {
    $running = Get-Process -Name "Beat Saber" -ErrorAction SilentlyContinue
    if ($running) {
        throw "Beat Saber is running. Close it before replacing a plugin DLL."
    }

    $destination = Join-Path $BeatSaberDir "Plugins\SaberStage.PcPoseAnalyzer.dll"
    Copy-Item -LiteralPath $dll -Destination $destination -Force
    Write-Host "Installed: $destination"
}
