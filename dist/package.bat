@echo off
chcp 65001 >nul
title CraftPacker v3 - Standalone Package Builder
echo ============================================
echo   CraftPacker v3 - Standalone .exe Builder
echo ============================================
echo.

REM Delete any previous package
if exist "%~dp0CraftPacker_v3_Standalone.exe" del "%~dp0CraftPacker_v3_Standalone.exe"
if exist "%~dp0CraftPacker_v3_temp.sed" del "%~dp0CraftPacker_v3_temp.sed"

REM List all files to include
set FILES=CraftPacker.exe
set DLLS=Qt6Core.dll Qt6Gui.dll Qt6Network.dll Qt6Svg.dll Qt6Widgets.dll icuuc.dll opengl32sw.dll D3Dcompiler_47.dll

REM Verify all files exist
echo [1/2] Verifying files...
for %%f in (%FILES%) do (
    if not exist "%~dp0..\build\%%f" (
        echo ERROR: Missing %%f
        pause
        exit /b 1
    )
)
for %%f in (%DLLS%) do (
    if not exist "%~dp0..\build\%%f" (
        echo ERROR: Missing %%f
        pause
        exit /b 1
    )
)
echo All files found.

REM Create IExpress SED file programmatically
REM IExpress requires specific formatting
echo [2/2] Creating IExpress SED...

(
echo [Version]
echo Class=IEXPRESS
echo SEDVersion=3
echo [Options]
echo PackagePurpose=ExtractAndRun
echo ShowInstallProgramWindow=0
echo HideExtractAnimation=1
echo UseLongFileName=1
echo InsideCompressed=1
echo CAB_FixedSize=0
echo CAB_MaxSize=0
echo CAB_ReserveCodePage=1252
echo LongFileName=1
echo SelfExtractor=C:\WINDOWS\System32\wextract.exe
echo GUIMode=1
echo InstallPrompt=%InstallPrompt%
echo UninstallPrompt=%UninstallPrompt%
echo DisplayLicense=0
echo FinishPrompt=0
echo InstallProgress=0
echo SourceDir=C:\projects\craftpacker\CraftPacker-main\build
echo AppLaunched=CraftPacker.exe
echo PostInstallCmd=<None>
echo AdminInstall=0
echo.
echo [Strings]
echo AppName=CraftPacker v3 - Modpack Studio
echo.
echo [SourceFiles]
echo SourceFiles=%%FILEGROUP%%
echo [SourceFiles^:%%FILEGROUP%%]
echo File0=CraftPacker.exe
echo File1=Qt6Core.dll
echo File2=Qt6Gui.dll
echo File3=Qt6Network.dll
echo File4=Qt6Svg.dll
echo File5=Qt6Widgets.dll
echo File6=icuuc.dll
echo File7=opengl32sw.dll
echo File8=D3Dcompiler_47.dll
) > "%~dp0CraftPacker_v3_temp.sed"

echo.
echo IExpress SED file created.
echo.
echo Now launching IExpress to build the package...
echo Please follow these steps:
echo   1. IExpress will show a wizard. Press Next.
echo   2. Select "Extract files and run an installation command" -> Next
echo   3. Enter "CraftPacker v3" as the package title -> Next
echo   4. Select "No prompt" -> Next
echo   5. Select "Do not display a license" -> Next
echo   6. Click "Add" to add ALL files from the list -> Next
echo   7. Enter "CraftPacker.exe" as the Install Program -> Next
echo   8. Select "Hidden" -> Next
echo   9. Select "No message" -> Next
echo   10. Browse to save as "CraftPacker_v3_Standalone.exe" -> Next
echo   11. Check "Don't save" -> Next -> Next -> Finish
echo.
echo OR just press any key to run the SED directly...
pause >nul

REM Try to run IExpress with the SED
start /wait "" "C:\WINDOWS\System32\iexpress.exe" /N "%~dp0CraftPacker_v3_temp.sed"

if exist "%~dp0CraftPacker_v3_Standalone.exe" (
    echo.
    echo ============================================
    echo   SUCCESS! Standalone .exe created at:
    echo   %~dp0CraftPacker_v3_Standalone.exe
    echo   File size:
    for %%I in ("%~dp0CraftPacker_v3_Standalone.exe") do echo   %%~zI bytes
    echo ============================================
) else (
    echo.
    echo WARNING: The standalone exe was not found.
    echo This usually means you need to use the IExpress GUI wizard.
    echo Opening IExpress now - please follow the instructions above.
    start /wait "" "C:\WINDOWS\System32\iexpress.exe"
)

if exist "%~dp0CraftPacker_v3_temp.sed" del "%~dp0CraftPacker_v3_temp.sed"
echo.
pause