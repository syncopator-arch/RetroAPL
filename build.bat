@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) Keith Doyle
setlocal

rem build.bat -- build RetroAPL on Windows.
rem
rem Usage: build.bat [clean|check]
rem   (none)  Build all three parts (sequencer, interpreter, Qt front
rem           end) with frontend\winbuild.bat, then gather the program
rem           and its DLLs in frontend\deploy\ with frontend\windeploy.bat.
rem   clean   Remove all build output.
rem   check   Build the libraries and run their regression tests.
rem
rem Then, the first time and after each rebuild: setup-aplhome.bat
rem
rem Needs Qt 6 for MinGW, the MinGW compiler and CMake, all from the Qt
rem online installer (see README.md). Run from anywhere.

set ROOT=%~dp0

if /I "%~1"=="clean" goto :pass
if /I "%~1"=="check" goto :pass
if not "%~1"=="" (
    echo Unknown command: %1
    echo Usage: build.bat [clean^|check]
    exit /b 1
)

call "%ROOT%frontend\winbuild.bat"
if errorlevel 1 exit /b 1
call "%ROOT%frontend\windeploy.bat"
if errorlevel 1 exit /b 1
endlocal
exit /b 0

:pass
call "%ROOT%frontend\winbuild.bat" %1
if errorlevel 1 exit /b 1
endlocal
exit /b 0
