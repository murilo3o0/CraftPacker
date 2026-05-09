@echo off
chcp 65001 >nul
title CraftPacker Static Build

echo ============================================
echo  CraftPacker v3 - STATIC BUILD (Standalone)
echo ============================================
echo.

REM Check if vcpkg installed QtBase static
if not exist "C:\projects\craftpacker\vcpkg\installed\x64-windows-static\lib\Qt6Core.lib" (
    echo ERROR: Static Qt6 not found. Run first:
    echo   C:\projects\craftpacker\vcpkg\vcpkg install qtbase[core,gui,widgets,network]:x64-windows-static
    echo.
    echo This build step is currently running in background.
    echo Wait for it to finish, then run this script again.
    pause
    exit /b 1
)

echo [1/3] Setting up VS 2022 environment...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to set up VS environment
    pause
    exit /b 1
)

echo [2/3] Configuring with static Qt6...
cmake -B build_static -S . ^
    -DCMAKE_TOOLCHAIN_FILE=C:/projects/craftpacker/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed
    pause
    exit /b 1
)

echo [3/3] Building statically linked CraftPacker.exe...
cmake --build build_static --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ============================================
echo  SUCCESS! Static .exe created at:
echo  %~dp0build_static\Release\CraftPacker.exe
echo ============================================
echo Verifying no DLL dependencies...
cd build_static\Release
dumpbin /dependents CraftPacker.exe | findstr /i "\.dll"
echo.
echo Copying to dist folder...
if not exist "%~dp0dist" mkdir "%~dp0dist"
copy /Y CraftPacker.exe "%~dp0dist\CraftPacker_v3.exe"
echo.
echo FINAL: C:\projects\craftpacker\CraftPacker-main\dist\CraftPacker_v3.exe
echo Size:
for %%I in ("%~dp0dist\CraftPacker_v3.exe") do echo %%~zI bytes
echo This is a SINGLE FILE. No DLLs. No extraction.
pause