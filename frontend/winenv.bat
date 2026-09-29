@echo off
rem winenv.bat
rem
rem Shared toolchain detection for winbuild.bat / windeploy.bat. Called
rem via "call winenv.bat" -- sets MINGW_BIN and QT_PREFIX for the caller.
rem
rem The Qt online installer bundles MinGW under a version-numbered folder
rem (e.g. mingw1120_64, mingw1310_64) that changes across Qt releases --
rem hardcoding it broke the first time this was tried on a machine with a
rem different Qt install. This auto-detects it instead.
rem
rem Override either by setting MINGW_ROOT / QT_PREFIX before calling this
rem script, e.g.:
rem   set MINGW_ROOT=C:\Qt\Tools\mingw1310_64
rem   set QT_PREFIX=C:\Qt\6.11.1\mingw_64
rem   call winenv.bat

if defined MINGW_ROOT (
    set MINGW_BIN=%MINGW_ROOT%\bin
) else (
    set MINGW_BIN=
    for /d %%i in ("C:\Qt\Tools\mingw*_64") do set MINGW_BIN=%%i\bin
)

if not defined MINGW_BIN (
    echo Error: no MinGW toolchain found under C:\Qt\Tools\mingw*_64
    echo Set MINGW_ROOT explicitly, e.g.:
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
    echo Set QT_PREFIX explicitly, e.g.:
    echo   set QT_PREFIX=C:\Qt\6.11.1\mingw_64
    exit /b 1
)

echo Using MINGW_BIN=%MINGW_BIN%
echo Using QT_PREFIX=%QT_PREFIX%
