#!/usr/bin/env pwsh
<#
.SYNOPSIS
    CraftPacker v3 - Automated Build Script
.DESCRIPTION
    Downloads dependencies (VS Build Tools, CMake, Qt) if needed,
    configures CMake with secure key injection, builds, and packages.
.PARAMETER BuildConfig
    Release or Debug (default: Release)
.PARAMETER InstallDeps
    Install missing dependencies automatically
.PARAMETER UseStaticQt
    Build with static Qt for standalone exe (requires static Qt build)
.PARAMETER CfApiKey
    CurseForge API key to inject at build time (XOR-obfuscated)
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildConfig = "Release",
    [switch]$InstallDeps,
    [switch]$UseStaticQt,
    [string]$CfApiKey = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $PSCommandPath
$RootDir = Resolve-Path "$ScriptDir"
$BuildDir = "$RootDir\build"
$OutputDir = "$RootDir\dist"

Write-Host "=== CraftPacker v3 Build Script ===" -ForegroundColor Cyan
Write-Host "Root: $RootDir"
Write-Host "Config: $BuildConfig"
Write-Host "Static Qt: $UseStaticQt"
Write-Host ""

# ============================================================
# Step 1: Check for required tools
# ============================================================
function Find-OrInstallTool {
    param([string]$Name, [string]$CheckCmd, [string]$InstallUrl, [string]$InstallerArgs)

    try {
        $result = Invoke-Expression $CheckCmd 2>$null
        if ($LASTEXITCODE -eq 0 -and $result) {
            Write-Host "  ✓ $Name found" -ForegroundColor Green
            return $true
        }
    } catch {}

    if (-not $InstallDeps) {
        Write-Host "  ✗ $Name NOT found. Use -InstallDeps to auto-install." -ForegroundColor Yellow
        return $false
    }

    Write-Host "  ⬇ Installing $Name..." -ForegroundColor Yellow
    $installer = "$env:TEMP\$Name-installer.exe"
    try {
        Invoke-WebRequest -Uri $InstallUrl -OutFile $installer -UseBasicParsing
        Start-Process -Wait -FilePath $installer -ArgumentList $InstallerArgs
        Write-Host "  ✓ $Name installed" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "  ✗ Failed to install $Name : $_" -ForegroundColor Red
        return $false
    }
}

Write-Host "Checking dependencies..." -ForegroundColor Cyan

# Check CMake
$cmakeOk = Find-OrInstallTool "CMake" "cmake --version" `
    "https://github.com/Kitware/CMake/releases/download/v3.30.0/cmake-3.30.0-windows-x86_64.msi" `
    "/quiet /norestart"

# Check MSVC
$msvcOk = $false
try {
    $cl = & "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" 2>$null
    if ($LASTEXITCODE -eq 0) { $msvcOk = $true }
} catch {}
try {
    $cl = & "cl.exe" 2>$null
    if ($LASTEXITCODE -eq 0) { $msvcOk = $true }
} catch {}

if (-not $msvcOk) {
    Write-Host "  ✗ MSVC Build Tools NOT found" -ForegroundColor Yellow
    if ($InstallDeps) {
        Write-Host "  ⬇ Please install Visual Studio Build Tools 2022 from:" -ForegroundColor Yellow
        Write-Host "     https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
        Write-Host "     Make sure to select 'Desktop development with C++' workload"
    }
}

# Check Qt
$qtOk = $false
try {
    $qmake = & "qmake.exe" --version 2>$null
    if ($LASTEXITCODE -eq 0) { $qtOk = $true }
} catch {}
try {
    $qtDir = Get-ChildItem "C:\Qt\6.*" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($qtDir) { $qtOk = $true }
} catch {}

if (-not $qtOk) {
    Write-Host "  ✗ Qt 6 NOT found" -ForegroundColor Yellow
    if ($InstallDeps) {
        Write-Host "  ⬇ Please install Qt 6.5+ from:" -ForegroundColor Yellow
        Write-Host "     https://www.qt.io/download-qt-installer"
        Write-Host "     Components: Qt 6.x > MSVC 2022 64-bit, Qt WebEngine"
    }
}

if (-not ($cmakeOk -and $msvcOk -and $qtOk)) {
    Write-Host "`nMissing dependencies. Run with -InstallDeps or install manually." -ForegroundColor Red
    if (-not $InstallDeps) {
        Write-Host "Re-run: .\build.ps1 -InstallDeps" -ForegroundColor Cyan
    }
    exit 1
}

# ============================================================
# Step 2: Configure with CMake
# ============================================================
Write-Host "`nConfiguring with CMake..." -ForegroundColor Cyan

# Generate API key if provided
$env:CF_API_KEY = $CfApiKey

# Find Qt6 path
$qtPath = ""
try {
    $qtPath = Get-ChildItem "C:\Qt\6.*\msvc2022_64" -Directory -ErrorAction SilentlyContinue |
              Select-Object -First 1 -ExpandProperty FullName
} catch {}

$cmakeArgs = @(
    "-S", $RootDir,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildConfig",
    "-DCMAKE_CXX_STANDARD=20"
)

if ($qtPath) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$qtPath"
}

if ($UseStaticQt) {
    # For static Qt, you'd need a statically built Qt, then:
    $cmakeArgs += "-DCMAKE_CXX_FLAGS=/MT"
}

Write-Host "Running: cmake $($cmakeArgs -join ' ')" -ForegroundColor Gray
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    Write-Host "  ✓ CMake configured successfully" -ForegroundColor Green
} catch {
    Write-Host "  ✗ CMake configuration failed: $_" -ForegroundColor Red
    exit 1
}

# ============================================================
# Step 3: Build
# ============================================================
Write-Host "`nBuilding..." -ForegroundColor Cyan
try {
    & cmake --build $BuildDir --config $BuildConfig
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    Write-Host "  ✓ Build succeeded" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Build failed: $_" -ForegroundColor Red
    exit 1
}

# ============================================================
# Step 4: Package (standalone)
# ============================================================
Write-Host "`nPackaging..." -ForegroundColor Cyan

# Create output directory
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Find the built executable
$exePath = Get-ChildItem -Recurse -Filter "CraftPacker.exe" -Path $BuildDir | Select-Object -First 1
if (-not $exePath) {
    Write-Host "  ✗ Executable not found in build output" -ForegroundColor Red
    exit 1
}

Write-Host "  ✓ Found: $($exePath.FullName)" -ForegroundColor Green

# Copy executable to output
Copy-Item $exePath.FullName "$OutputDir\CraftPacker.exe" -Force

# If not using static Qt, deploy Qt DLLs
if (-not $UseStaticQt) {
    $windeployqt = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if ($windeployqt) {
        Write-Host "  Running windeployqt..." -ForegroundColor Gray
        Push-Location $OutputDir
        & $windeployqt.Path "CraftPacker.exe" --no-compiler-runtime --no-quick --no-system-d3d-compiler 2>&1 | Out-Null
        Pop-Location
        Write-Host "  ✓ Qt DLLs deployed" -ForegroundColor Green
    } else {
        Write-Host "  ⚠ windeployqt not found. Qt DLLs not deployed." -ForegroundColor Yellow
    }
}

# Create version info
$version = "3.0.1"
"Build: $BuildConfig`nDate: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`nVersion: $version" | Out-File "$OutputDir\build_info.txt"

# Zip the output
$zipFile = "$RootDir\CraftPacker-v$version-$BuildConfig.zip"
if (Get-Command "Compress-Archive" -ErrorAction SilentlyContinue) {
    Compress-Archive -Path "$OutputDir\*" -DestinationPath $zipFile -Force
    Write-Host "  ✓ Packaged: $zipFile" -ForegroundColor Green
}

Write-Host "`n=== Build Complete ===" -ForegroundColor Cyan
Write-Host "Executable: $OutputDir\CraftPacker.exe"
Write-Host "Archive: $zipFile"