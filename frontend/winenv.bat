@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) Keith Doyle
rem winenv.bat
rem
rem Shared toolchain detection for winbuild.bat / windeploy.bat. Called
rem via "call winenv.bat" -- sets MINGW_BIN and QT_PREFIX for the caller,
rem and puts the MinGW, Qt and CMake tools on PATH (for the caller's
rem setlocal scope only; nothing is changed permanently).
rem
rem The Qt online installer bundles MinGW under a version-numbered folder
rem (e.g. mingw1120_64, mingw1310_64) that changes across Qt releases --
rem hardcoding it broke the first time this was tried on a machine with a
rem different Qt install. This auto-detects it instead.
rem
rem Override any of these by setting them before calling this script:
rem   set MINGW_ROOT=C:\Qt\Tools\mingw1310_64
rem   set QT_PREFIX=C:\Qt\6.11.1\mingw_64
rem   set CMAKE_BIN=C:\Qt\Tools\CMake_64\bin
rem   call winenv.bat
rem
rem With more than one Qt 6 kit installed, the last folder in name order
rem is used -- so 6.9.x wins over 6.10.x. Set QT_PREFIX to choose.

if defined MINGW_ROOT (
    set MINGW_BIN=%MINGW_ROOT%\bin
) else (
    set MINGW_BIN=
    for /d %%i in ("C:\Qt\Tools\mingw*_64") do set MINGW_BIN=%%i\bin
)

if not defined MINGW_BIN (
    echo Error: no MinGW toolchain found under C:\Qt\Tools\mingw*_64
    echo Install it with the Qt installer, or set MINGW_ROOT explicitly, e.g.:
    echo   set MINGW_ROOT=C:\Qt\Tools\mingw1310_64
    exit /b 1
)
if not exist "%MINGW_BIN%\gcc.exe" (
    echo Error: gcc.exe not found at %MINGW_BIN%
    echo Detected MINGW_BIN may be wrong -- set MINGW_ROOT explicitly if so.
    exit /b 1
)

if not defined QT_PREFIX (
    set QT_PREFIX=
    for /d %%i in ("C:\Qt\6.*") do (
        if exist "%%i\mingw_64\bin\qmake.exe" set QT_PREFIX=%%i\mingw_64
    )
)
if not defined QT_PREFIX (
    echo Error: no Qt mingw_64 kit found under C:\Qt\6.*
    echo Install one with the Qt installer, or set QT_PREFIX explicitly, e.g.:
    echo   set QT_PREFIX=C:\Qt\6.11.1\mingw_64
    exit /b 1
)

rem CMake: the Qt installer's copy if there is one, else whatever is on PATH.
if not defined CMAKE_BIN (
    if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set CMAKE_BIN=C:\Qt\Tools\CMake_64\bin
)

rem Put the tools first on PATH: mingw32-make and gcc from MinGW, so the
rem build doesn't depend on what else is installed; Qt's bin for
rem windeployqt; CMake. windeployqt also finds the MinGW runtime DLLs
rem through gcc's folder on PATH.
if defined CMAKE_BIN (
    set "PATH=%MINGW_BIN%;%QT_PREFIX%\bin;%CMAKE_BIN%;%PATH%"
) else (
    set "PATH=%MINGW_BIN%;%QT_PREFIX%\bin;%PATH%"
)

where mingw32-make >nul 2>nul
if errorlevel 1 (
    echo Error: mingw32-make.exe not found in %MINGW_BIN%
    exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo Error: cmake not found.
    echo Install CMake with the Qt installer ^(it goes in C:\Qt\Tools\CMake_64^),
    echo or from cmake.org, or set CMAKE_BIN to the folder holding cmake.exe.
    exit /b 1
)

echo Using MINGW_BIN=%MINGW_BIN%
echo Using QT_PREFIX=%QT_PREFIX%
if defined CMAKE_BIN echo Using CMAKE_BIN=%CMAKE_BIN%
exit /b 0
