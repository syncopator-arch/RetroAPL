@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) Keith Doyle
rem setup-aplhome.bat
rem
rem Create or update an APLHOME folder from this repository, and -- if
rem the program has been built and deployed (build.bat) -- install it in
rem APLHOME\bin and add RetroAPL to the Start menu.
rem
rem Usage: setup-aplhome.bat [target-folder]
rem   target-folder  Defaults to %APLHOME% if set, else C:\aplhome.
rem
rem Two kinds of files are handled differently:
rem   Program files -- bin\ (retroapl.exe and its DLLs, from
rem       frontend\deploy\), fonts\kapl.ttf, help.dat, resources\*.md.
rem       Copied if missing, replaced if this repository's copy differs,
rem       so re-running setup after an update (and rebuild) brings them
rem       up to date.
rem   User files -- ws\, fns\, images\, midi\.
rem       Starting examples only. Copied if missing, never replaced.
rem
rem If APLHOME isn't set yet, it is set for your account (setx).

setlocal

set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
set "SRC=%REPO%\aplhome"

if not "%~1"=="" goto :arg_target
if defined APLHOME goto :env_target
set "TARGET=C:\aplhome"
goto :have_target
:arg_target
set "TARGET=%~f1"
goto :have_target
:env_target
set "TARGET=%APLHOME%"
:have_target

if not exist "%SRC%\" goto :no_src
if not exist "%REPO%\fonts\kapl.ttf" goto :no_font

echo Setting up APLHOME in: %TARGET%
for %%D in (bin fonts resources ws fns images midi) do if not exist "%TARGET%\%%D\" mkdir "%TARGET%\%%D"

rem robocopy options: quiet output, list only the files it copies.
set "RC=/NJH /NJS /NDL /NP /NS /NC"
rem For user files, also skip any file that already exists (changed,
rem newer or older), so only missing files are copied.
set "KEEP=/XC /XN /XO"

rem --- program files ---------------------------------------------------------
robocopy "%REPO%\fonts" "%TARGET%\fonts" kapl.ttf %RC%
if errorlevel 8 goto :copy_failed
if exist "%SRC%\help.dat" robocopy "%SRC%" "%TARGET%" help.dat %RC%
if errorlevel 8 goto :copy_failed
if exist "%SRC%\resources\" robocopy "%SRC%\resources" "%TARGET%\resources" *.* %RC%
if errorlevel 8 goto :copy_failed

rem --- user files ------------------------------------------------------------
for %%D in (ws fns images midi) do if exist "%SRC%\%%D\" robocopy "%SRC%\%%D" "%TARGET%\%%D" *.* %RC% %KEEP%
if errorlevel 8 goto :copy_failed

echo.
echo Done. Existing workspaces, functions and images were left as they were.

rem --- the program and the Start menu --------------------------------------
rem frontend\deploy\ is made by windeploy.bat (build.bat runs it): the
rem program, the Qt and MinGW DLLs and the Qt plugin folders.
set "DEPLOY=%REPO%\frontend\deploy"
if not exist "%DEPLOY%\retroapl.exe" goto :no_program
robocopy "%DEPLOY%" "%TARGET%\bin" *.* /E %RC%
if errorlevel 8 goto :copy_failed

rem The program was called APLTerminal.exe before V106. Its old copy is a
rem program file, so it goes, now that the new one is in place. (The DLLs
rem it used are the new one's too.) It can't be deleted while it runs.
if exist "%TARGET%\bin\APLTerminal.exe" del /q "%TARGET%\bin\APLTerminal.exe" 2>nul
if exist "%TARGET%\bin\APLTerminal.exe" echo Note: couldn't remove the old %TARGET%\bin\APLTerminal.exe -- is it running? Close it and delete it, or run this script again.

echo.
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Programs')+'\RetroAPL.lnk'); $s.TargetPath='%TARGET%\bin\retroapl.exe'; $s.WorkingDirectory='%TARGET%'; $s.Description='RetroAPL'; $s.Save()"
if errorlevel 1 (
    echo RetroAPL is installed in %TARGET%\bin. ^(The Start menu entry
    echo couldn't be made; run %TARGET%\bin\retroapl.exe directly.^)
) else (
    echo RetroAPL is installed in %TARGET%\bin and in your Start menu.
)
goto :aplhome

:no_program
echo.
echo The program hasn't been built yet (no %DEPLOY%\retroapl.exe).
echo Run build.bat, then run this script again to install it in
echo %TARGET%\bin and add it to your Start menu.

:aplhome

rem --- APLHOME ---------------------------------------------------------------
if not defined APLHOME goto :set_aplhome
if /I "%APLHOME%"=="%TARGET%" goto :end
echo.
echo Note: APLHOME is currently set to %APLHOME%
echo To use the new folder instead, run:  setx APLHOME "%TARGET%"
goto :end

:set_aplhome
setx APLHOME "%TARGET%" >nul
echo.
echo APLHOME has been set to %TARGET% for your account.
echo Open a new window (or sign out and back in) for programs to see it.
goto :end

:no_src
echo Error: %SRC% not found -- run this from a RetroAPL checkout.
exit /b 1

:no_font
echo Error: %REPO%\fonts\kapl.ttf not found.
exit /b 1

:copy_failed
echo Error: copying files into %TARGET% failed.
exit /b 1

:end
endlocal
