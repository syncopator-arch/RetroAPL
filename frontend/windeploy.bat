@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) Keith Doyle
setlocal enabledelayedexpansion

rem windeploy.bat
rem
rem Gathers everything retroapl.exe needs to run on a machine without
rem a Qt development install: the Qt DLLs and plugins (windeployqt), and
rem the MinGW runtime DLLs. Run after winbuild.bat (build.bat in the
rem repository root runs both).
rem
rem The result goes in frontend\deploy\, a folder holding only the
rem program and its DLLs; setup-aplhome.bat copies it to %%APLHOME%%\bin.
rem frontend\build\ is left as CMake's build folder.
rem
rem Last, every .exe and .dll in deploy\ is checked with MinGW's objdump
rem for DLLs it imports that are neither in deploy\ nor part of Windows,
rem and any such DLL is reported by name.
rem
rem Resolved relative to this script's own location, so it works
rem regardless of the current directory.

set SCRIPT_DIR=%~dp0
set EXE=%SCRIPT_DIR%build\retroapl.exe
set DEPLOY=%SCRIPT_DIR%deploy

call "%SCRIPT_DIR%winenv.bat"
if errorlevel 1 exit /b 1
set WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe
set OBJDUMP=%MINGW_BIN%\objdump.exe

if not exist "%EXE%" (
    echo Error: %EXE% not found. Run winbuild.bat first.
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo Error: windeployqt.exe not found at %WINDEPLOYQT%
    echo Check your Qt install path/version.
    exit /b 1
)

rem --- A fresh deploy folder ---
if exist "%DEPLOY%" rmdir /s /q "%DEPLOY%"
mkdir "%DEPLOY%"
copy /y "%EXE%" "%DEPLOY%\" >nul
if errorlevel 1 (
    echo Error: could not copy retroapl.exe to %DEPLOY%
    exit /b 1
)

rem --- Qt DLLs and plugins ---
echo ==^> Running windeployqt...
"%WINDEPLOYQT%" "%DEPLOY%\retroapl.exe"
if errorlevel 1 (
    echo Error: windeployqt failed.
    exit /b 1
)

rem --- MinGW runtime DLLs ---
rem windeployqt normally copies these itself; this makes sure of it.
rem libwinpthread-1.dll is the sequencer's threads.
for %%F in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if not exist "%DEPLOY%\%%F" if exist "%MINGW_BIN%\%%F" (
        copy /y "%MINGW_BIN%\%%F" "%DEPLOY%\" >nul
        echo ==^> Copied %%F from %MINGW_BIN%
    )
)

rem --- Check for missing DLLs ---
set MISSING=0
if not exist "%OBJDUMP%" (
    echo Warning: %OBJDUMP% not found; skipping the missing-DLL check.
    goto :done
)
echo ==^> Checking for missing DLLs...
rem objdump is run by name: winenv.bat put MinGW's bin first on PATH.
rem (Naming it by its full, quoted path here would need cmd's
rem double-quoting trick, which is easy to get wrong.)
rem findstr keeps only the "DLL Name:" lines: objdump -p also prints each
rem DLL's export table, tens of thousands of lines for the Qt DLLs, and
rem for /f is very slow reading that much output itself.
for /r "%DEPLOY%" %%B in (*.exe *.dll) do (
    for /f "tokens=1-3" %%a in ('objdump -p "%%B" 2^>nul ^| findstr /c:"DLL Name:"') do (
        if "%%a %%b"=="DLL Name:" call :check_dll "%%c" "%%~nxB"
    )
)
if !MISSING!==0 (
    echo ==^> No missing DLLs found.
) else (
    echo.
    echo Warning: the DLLs listed above aren't in %DEPLOY% or in Windows.
    echo Copy each one into %DEPLOY% -- most likely from %MINGW_BIN%
    echo or %QT_PREFIX%\bin -- and please report it, so this script can
    echo be fixed.
)

:done
echo.
echo ==^> The program and its DLLs are in %DEPLOY%
echo Next: setup-aplhome.bat in the repository root, to install it in
echo %%APLHOME%%\bin along with the font and help files.
endlocal
exit /b 0

rem check_dll NAME IMPORTER
rem Reports NAME if it isn't in deploy\ or the Windows system folder.
rem api-ms-win-* and ext-ms-win-* are Windows API sets, resolved by
rem Windows itself rather than found as files.
:check_dll
set "DLL=%~1"
if /I "%DLL:~0,11%"=="api-ms-win-" exit /b 0
if /I "%DLL:~0,11%"=="ext-ms-win-" exit /b 0
if exist "%DEPLOY%\%DLL%" exit /b 0
if exist "%SystemRoot%\System32\%DLL%" exit /b 0
echo   missing: %DLL%   ^(needed by %~2^)
set MISSING=1
exit /b 0
