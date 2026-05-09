#!/usr/bin/env pwsh
#Requires -Version 7.0
<#
.SYNOPSIS
  MSVC + CMake + shared Qt Release build with windeployqt (portable folder).

.NOTES
  Use when static vcpkg Qt is unavailable or vcpkg is locked.
  Output: dist/CraftPacker_Windows/

.PARAMETER QtPath
  Path to Qt MSVC kit (must contain bin\windeployqt.exe and lib\cmake\Qt6).
.PARAMETER CMakePath
  Path to cmake.exe. If omitted, probes known portable installs + PATH.
#>
param(
    [string]$QtPath = "C:\Qt\6.11.0\msvc2022_64",
    [string]$CMakePath,
    [string]$VsVcVars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    [string]$SourceRoot = "",
    [string]$BuildDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $SourceRoot) { $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path }
if (-not $BuildDir) { $BuildDir = Join-Path $SourceRoot "build" }

$possibleCMake = @(
    "C:\projects\craftpacker\cmake_portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
    "$env:USERPROFILE\cmake-portable\cmake-3.30.0-windows-x86_64\bin\cmake.exe"
    "cmake.exe"
)
if (-not $CMakePath) {
    foreach ($c in $possibleCMake) {
        if ($c -eq "cmake.exe") {
            try { $CMakePath = (Get-Command cmake -ErrorAction Stop).Source } catch {}
        } elseif (Test-Path -LiteralPath $c) {
            $CMakePath = $c
            break
        }
    }
}
if (-not $CMakePath -or -not (Test-Path -LiteralPath $CMakePath)) {
    throw "CMake not found. Install CMake or set -CMakePath."
}
if (-not (Test-Path -LiteralPath $VsVcVars)) {
    throw "VCVARS not found: $VsVcVars"
}
$qtConfig = Join-Path $QtPath "lib\cmake\Qt6\Qt6Config.cmake"
if (-not (Test-Path -LiteralPath $qtConfig)) {
    throw "Qt 6 kit not found at QtPath: $qtConfig"
}
$windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt not found: $windeployqt"
}

Write-Host "Source:   $SourceRoot"
Write-Host "Build:    $BuildDir"
Write-Host "CMake:    $CMakePath"
Write-Host "Qt:       $QtPath"
Write-Host ""

$vcvarsQuoted = "`"$VsVcVars`""
$cmakeQuoted = "`"$CMakePath`""
$srcQuoted = "`"$SourceRoot`""
$bldQuoted = "`"$BuildDir`""
$qtPrefix = "`"$QtPath`""

$batLines = @(
    "@echo off"
    "call $vcvarsQuoted || exit /b 1"
    "cd /d $srcQuoted || exit /b 1"
    "$cmakeQuoted -S $srcQuoted -B $bldQuoted -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$qtPrefix -DCMAKE_CXX_STANDARD=20 -DPython3_FOUND=OFF -DUSE_SYSTEM_SQLITE=0 || exit /b 1"
    "$cmakeQuoted --build $bldQuoted --config Release || exit /b 1"
)
$tmpBat = Join-Path ([System.IO.Path]::GetTempPath()) "craftpacker-build-dynamic-$(Get-Random).cmd"
$encNoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($tmpBat, ($batLines -join "`r`n"), $encNoBom)

try {
    $p = Start-Process -FilePath "cmd.exe" -ArgumentList @("/s", "/c", "`"$tmpBat`"") -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) {
        throw "Build failed with exit $($p.ExitCode)"
    }
} finally {
    Remove-Item -LiteralPath $tmpBat -Force -ErrorAction SilentlyContinue
}

$exeBuilt = Join-Path $BuildDir "CraftPacker.exe"
if (-not (Test-Path -LiteralPath $exeBuilt)) {
    throw "Expected output missing: $exeBuilt"
}

$deploy = Join-Path $SourceRoot "dist\CraftPacker_Windows"
if (Test-Path -LiteralPath $deploy) {
    Remove-Item -LiteralPath $deploy -Recurse -Force
}
New-Item -ItemType Directory -Path $deploy | Out-Null

Copy-Item -LiteralPath $exeBuilt -Destination (Join-Path $deploy "CraftPacker.exe")

$resourcesBuilt = Join-Path $BuildDir "resources"
if (Test-Path -LiteralPath $resourcesBuilt) {
    Copy-Item -LiteralPath $resourcesBuilt -Destination (Join-Path $deploy "resources") -Recurse
}

Write-Host "Running windeployqt..."
& $windeployqt "--release" "--compiler-runtime" (Join-Path $deploy "CraftPacker.exe")

$zipPath = Join-Path $SourceRoot "dist\CraftPacker_Windows_portable.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -LiteralPath $deploy -DestinationPath $zipPath -Force

$releaseZip = Join-Path $SourceRoot "dist\CraftPacker_v3_Portable.zip"
Copy-Item -LiteralPath $zipPath -Destination $releaseZip -Force

Write-Host ""
Write-Host "Done. Run: $deploy\CraftPacker.exe"
Write-Host "Zip:      $zipPath"
Write-Host "Release:  $releaseZip (same contents; use for GitHub assets)"
Get-Item (Join-Path $deploy "CraftPacker.exe") | Format-List FullName, Length, LastWriteTime
