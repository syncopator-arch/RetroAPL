@echo off
rem setup-aplhome.bat
rem
rem Create or update an APLHOME folder from this repository.
rem
rem Usage: setup-aplhome.bat [target-folder]
rem   target-folder  Defaults to %APLHOME% if set, else C:\aplhome.
rem
rem Two kinds of files are handled differently:
rem   Program files -- fonts\kapl.ttf, help.dat, resources\*.md.
rem       Copied if missing, replaced if this repository's copy differs,
rem       so re-running setup after an update brings them up to date.
rem   User files -- ws\, fns\, images\.
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
for %%D in (bin fonts resources ws fns images) do if not exist "%TARGET%\%%D\" mkdir "%TARGET%\%%D"

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
for %%D in (ws fns images) do if exist "%SRC%\%%D\" robocopy "%SRC%\%%D" "%TARGET%\%%D" *.* %RC% %KEEP%
if errorlevel 8 goto :copy_failed

echo.
echo Done. Existing workspaces, functions and images were left as they were.

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
