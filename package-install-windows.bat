@echo off
setlocal enabledelayedexpansion

rem package-install-windows.bat
rem
rem Assembles a Windows install package: APLTerminal.exe plus everything
rem windeployqt placed alongside it in frontend\build\, plus a ready-to-use
rem %APLHOME% skeleton (font, help data, curated fns, a fixed list of
rem workspaces) pulled from a reference %APLHOME%.
rem
rem What ships from frontend\build\ is determined by EXCLUDING known
rem CMake/build byproducts (CMakeFiles, APLTerminal_autogen, .qt,
rem CMakeCache.txt, cmake_install.cmake, Makefile) rather than
rem whitelisting DLL/plugin names -- windeployqt's plugin set varies by
rem Qt version and its output is meant to be trusted as-is; only the
rem build tooling that happens to live in the same directory is filtered.
rem
rem Usage: package-install-windows.bat [reference-aplhome] [output-basename]
rem   reference-aplhome  Defaults to %APLHOME% if set.
rem   output-basename    Defaults to apl-windows-install (no date -- %DATE%
rem                       format is locale-dependent, so pass a name
rem                       explicitly if you want one baked in).
rem
rem Run from the repo root (sibling of backend\ and frontend\), after
rem winbuild.bat and windeploy.bat have both completed successfully.

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%frontend\build

set REF_APLHOME=%~1
if "%REF_APLHOME%"=="" set REF_APLHOME=%APLHOME%
if "%REF_APLHOME%"=="" (
    echo Error: no reference APLHOME given and %%APLHOME%% is not set.
    echo Usage: %~nx0 ^<reference-aplhome-dir^> [output-basename]
    exit /b 1
)
if not exist "%REF_APLHOME%" (
    echo Error: reference APLHOME "%REF_APLHOME%" does not exist.
    exit /b 1
)

set OUTF=%~2
if "%OUTF%"=="" set OUTF=apl-windows-install

if not exist "%BUILD_DIR%\APLTerminal.exe" (
    echo Error: %BUILD_DIR%\APLTerminal.exe not found.
    echo Run winbuild.bat and windeploy.bat first.
    exit /b 1
)

set STAGE=%TEMP%\%OUTF%_stage
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\%OUTF%\bin"
mkdir "%STAGE%\%OUTF%\aplhome\fonts"
mkdir "%STAGE%\%OUTF%\aplhome\fns"
mkdir "%STAGE%\%OUTF%\aplhome\ws"

rem --- binary + Qt runtime: everything windeployqt produced -------------
robocopy "%BUILD_DIR%" "%STAGE%\%OUTF%\bin" /E ^
    /XD CMakeFiles APLTerminal_autogen .qt ^
    /XF CMakeCache.txt cmake_install.cmake Makefile >nul
if %ERRORLEVEL% GEQ 8 (
    echo Error: robocopy failed copying build output ^(code %ERRORLEVEL%^).
    exit /b 1
)

rem robocopy has no "copy everything except these" wildcard for a single
rem file set, so trim translations after the fact instead: keep only
rem qt_en.qm, drop the ~30 other language files windeployqt bundled.
if exist "%STAGE%\%OUTF%\bin\translations" (
    for %%F in ("%STAGE%\%OUTF%\bin\translations\*.qm") do (
        if /i not "%%~nxF"=="qt_en.qm" del "%%F"
    )
)

rem --- font ---------------------------------------------------------------
if exist "%REF_APLHOME%\fonts\kapl.ttf" (
    copy /y "%REF_APLHOME%\fonts\kapl.ttf" "%STAGE%\%OUTF%\aplhome\fonts\" >nul
) else (
    echo Warning: kapl.ttf not found under %REF_APLHOME%\fonts\
)

rem --- help.dat -------------------------------------------------------------
if exist "%REF_APLHOME%\help.dat" (
    copy /y "%REF_APLHOME%\help.dat" "%STAGE%\%OUTF%\aplhome\" >nul
) else (
    echo Warning: help.dat not found under %REF_APLHOME%
)

rem --- fns: curated by extension (*.fn only) -------------------------------
set FN_COUNT=0
if exist "%REF_APLHOME%\fns\*.fn" (
    for %%F in ("%REF_APLHOME%\fns\*.fn") do (
        copy /y "%%F" "%STAGE%\%OUTF%\aplhome\fns\" >nul
        set /a FN_COUNT+=1
    )
)
echo Copied !FN_COUNT! .fn file(s)

rem --- ws: explicit list only, matching the Linux package ------------------
for %%W in (continue.ws medium.ws mpl.ws small.ws music.ws) do (
    if exist "%REF_APLHOME%\ws\%%W" (
        copy /y "%REF_APLHOME%\ws\%%W" "%STAGE%\%OUTF%\aplhome\ws\" >nul
    ) else (
        echo Warning: workspace %%W not found at %REF_APLHOME%\ws\%%W
    )
)

rem NOTE: settings.ini deliberately NOT included -- same reasoning as the
rem Linux package: QSettings creates a fresh one with defaults on first
rem write, so a new install shouldn't inherit someone else's UI state.

rem --- generate the target-machine installer -------------------------------
> "%STAGE%\%OUTF%\install.bat" (
    echo @echo off
    echo rem install.bat -- places this package's binary and %%APLHOME%% contents
    echo rem on THIS machine. aplhome contents are no-clobber: existing files
    echo rem are left alone. bin contents are always refreshed with this
    echo rem package's version.
    echo rem Usage: install.bat [target-aplhome] [bin-dir]
    echo setlocal
    echo set SCRIPT_DIR=%%~dp0
    echo set TARGET_APLHOME=%%~1
    echo if "%%TARGET_APLHOME%%"=="" set TARGET_APLHOME=%%APLHOME%%
    echo if "%%TARGET_APLHOME%%"=="" set TARGET_APLHOME=%%USERPROFILE%%\apl
    echo set BIN_DIR=%%~2
    echo if "%%BIN_DIR%%"=="" set BIN_DIR=%%LOCALAPPDATA%%\APLTerminal
    echo if not exist "%%BIN_DIR%%" mkdir "%%BIN_DIR%%"
    echo robocopy "%%SCRIPT_DIR%%bin" "%%BIN_DIR%%" /E ^>nul
    echo if not exist "%%TARGET_APLHOME%%" mkdir "%%TARGET_APLHOME%%"
    echo robocopy "%%SCRIPT_DIR%%aplhome" "%%TARGET_APLHOME%%" /E /XC /XN /XO ^>nul
    echo rem APLHOME is a shared, machine-wide location -- grant the local
    echo rem Users group read/write ^(and inherit to files created later,
    echo rem e.g. new ^)SAVEs, debug.log^) so normal, non-admin accounts can
    echo rem use it. This only succeeds if install.bat itself is running
    echo rem elevated or as the folder's owner -- see the note below.
    echo icacls "%%TARGET_APLHOME%%" /grant Users:^(OI^)^(CI^)M /T ^>nul 2^>nul
    echo echo Installed APLTerminal to %%BIN_DIR%%
    echo echo Populated %%TARGET_APLHOME%% ^(existing files, if any, were left untouched^)
    echo echo.
    echo echo Note: if this is the FIRST time setting up this shared APLHOME,
    echo echo run this installer as Administrator once so other accounts on
    echo echo this machine can read/write it. Later reinstalls/updates by a
    echo echo normal user are fine once that one-time grant has been made.
    echo echo.
    echo echo Set these if not already present in your environment:
    echo echo   setx APLHOME "%%TARGET_APLHOME%%"
    echo echo   setx PATH "%%PATH%%;%%BIN_DIR%%"
    echo endlocal
)

rem --- zip it: cmd has no native zip, shell out to PowerShell --------------
if exist "%SCRIPT_DIR%%OUTF%.zip" del "%SCRIPT_DIR%%OUTF%.zip"
powershell -NoProfile -Command ^
  "Compress-Archive -Path '%STAGE%\%OUTF%' -DestinationPath '%SCRIPT_DIR%%OUTF%.zip' -Force"
if errorlevel 1 (
    echo Error: PowerShell Compress-Archive failed.
    exit /b 1
)

rmdir /s /q "%STAGE%"

echo.
echo Created %OUTF%.zip
echo.
echo On the target machine:
echo   Expand-Archive %OUTF%.zip
echo   cd %OUTF%
echo   install.bat

endlocal
