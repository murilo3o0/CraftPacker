@echo off
SETLOCAL ENABLEDELAYEDEXPANSION

echo === Setting up VS 2022 Build Environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

echo === CMake Configure ===
set "CMAKE=C:\Users\PC1\cmake-portable\cmake-3.30.0-windows-x86_64\bin\cmake.exe"
set "QT_DIR=C:\Qt\6.11.0\msvc2022_64"
set "SOURCE_DIR=C:\projects\craftpacker\CraftPacker-main"
set "BUILD_DIR=%SOURCE_DIR%\build"

cd /d "%SOURCE_DIR%"

echo Running CMake configure...
"%CMAKE%" -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_PREFIX_PATH=%QT_DIR%" ^
    -DCMAKE_CXX_STANDARD=20 ^
    -DPython3_FOUND=OFF ^
    -DUSE_SYSTEM_SQLITE=0

if %ERRORLEVEL% neq 0 (
    echo CMake configuration FAILED with error %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo === CMake Build ===
"%CMAKE%" --build "%BUILD_DIR%" --config Release

if %ERRORLEVEL% neq 0 (
    echo Build FAILED with error %ERRORLEVEL%
) else (
    echo.
    echo === Build SUCCESS ===
    echo Executable: %BUILD_DIR%\CraftPacker.exe
)

exit /b %ERRORLEVEL%