@echo off
setlocal EnableExtensions
chcp 65001 >nul
title CraftPacker — static release build

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
for %%I in ("%ROOT%\..\vcpkg") do set "VCPKG_ROOT=%%~fI"
set "CMAKE_EXE=C:\projects\craftpacker\cmake_portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"

echo ============================================
echo  CraftPacker v3 — STATIC build (standalone exe)
echo ============================================
echo.

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg toolchain not found:
    echo   %VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    echo Set VCPKG_ROOT in this script if your clone is elsewhere.
    goto :fail
)

if not exist "%VCPKG_ROOT%\installed\x64-windows-static\lib\Qt6Core.lib" (
    echo ERROR: Static Qt6 not found. Install once from vcpkg root:
    echo   vcpkg install qtbase[core,gui,widgets,network]:x64-windows-static
    goto :fail
)

echo [1/3] MSVC x64 environment...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed — install VS 2022 Build Tools with C++ workload.
    goto :fail
)

echo [2/3] CMake configure — vcpkg toolchain, x64-windows-static, /MT...
pushd "%ROOT%"
"%CMAKE_EXE%" -B build_static -S . ^
    "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    popd
    echo ERROR: CMake configure failed.
    goto :fail
)

echo [3/3] Building Release...
"%CMAKE_EXE%" --build build_static --config Release
if errorlevel 1 (
    popd
    echo ERROR: Build failed.
    goto :fail
)
popd

set "BUILT_EXE="
if exist "%ROOT%\build_static\Release\CraftPacker.exe" set "BUILT_EXE=%ROOT%\build_static\Release\CraftPacker.exe"
if not defined BUILT_EXE if exist "%ROOT%\build_static\CraftPacker.exe" set "BUILT_EXE=%ROOT%\build_static\CraftPacker.exe"
if not defined BUILT_EXE (
    echo ERROR: CraftPacker.exe not found — expected build_static\CraftPacker.exe or build_static\Release\CraftPacker.exe.
    goto :fail
)

echo.
echo ============================================
echo Build output: %BUILT_EXE%
echo ============================================
echo Verifying dependencies (expect only system DLLs)...
dumpbin /dependents "%BUILT_EXE%" 2>nul | findstr /i ".dll"

echo.
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"
copy /Y "%BUILT_EXE%" "%ROOT%\dist\CraftPacker_v3.exe" >nul
if errorlevel 1 (
    echo ERROR: Copy to dist failed.
    goto :fail
)
echo Shipped: %ROOT%\dist\CraftPacker_v3.exe
for %%I in ("%ROOT%\dist\CraftPacker_v3.exe") do echo Size: %%~zI bytes

echo.
echo Done.
exit /b 0

:fail
echo.
pause
exit /b 1
