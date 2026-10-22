@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) Keith Doyle
setlocal

rem winbuild.bat
rem
rem Builds RetroAPL on Windows: the MIDI sequencer library (libseq.a),
rem the interpreter (libapl.a and apl_cli.exe), then the Qt front end
rem (APLTerminal.exe). Run windeploy.bat next to gather the DLLs it
rem needs -- or run build.bat in the repository root, which does both.
rem
rem Usage: winbuild.bat [clean|check]
rem   (none)  Build all three parts.
rem   clean   Remove all build output (all three parts) and stop.
rem   check   Build the libraries, then run the sequencer's and the
rem           interpreter's regression tests.
rem
rem Run from anywhere -- paths are resolved relative to this script's
rem own location (%~dp0), not the current directory.
rem
rem Expected layout:
rem   ...\backend\    <- makefile, produces libapl.a and apl_cli.exe
rem   ...\sequencer\  <- makefile, produces libseq.a
rem   ...\frontend\   <- CMakeLists.txt, this script

set SCRIPT_DIR=%~dp0
set FRONTEND_DIR=%SCRIPT_DIR%
set BACKEND_DIR=%SCRIPT_DIR%..\backend
set SEQ_DIR=%SCRIPT_DIR%..\sequencer

if /I "%~1"=="clean" goto :clean

call "%SCRIPT_DIR%winenv.bat"
if errorlevel 1 exit /b 1

rem --- Sequencer library ---
rem (The backend's own make would build it too, but only for its
rem default target.)
echo ==^> Building the MIDI sequencer library (libseq.a)...
pushd "%SEQ_DIR%"
mingw32-make CC=gcc AR=ar libseq.a
if errorlevel 1 (
    echo Error: sequencer build failed.
    popd
    exit /b 1
)
popd

rem --- Backend ---
rem The default target: libapl.a for the front end, and apl_cli.exe,
rem the command-line interpreter the tests use.
echo ==^> Building the interpreter (libapl.a, apl_cli.exe)...
pushd "%BACKEND_DIR%"
mingw32-make CC=gcc AR=ar
if errorlevel 1 (
    echo Error: backend build failed.
    popd
    exit /b 1
)
popd

if not exist "%SEQ_DIR%\libseq.a" (
    echo Error: the build did not produce %SEQ_DIR%\libseq.a
    exit /b 1
)
if not exist "%BACKEND_DIR%\libapl.a" (
    echo Error: the build did not produce %BACKEND_DIR%\libapl.a
    exit /b 1
)

if /I "%~1"=="check" goto :check

rem --- Frontend ---
echo ==^> Building the Qt front end...
cd /d "%FRONTEND_DIR%"
if exist build rmdir /s /q build
mkdir build
cd build

cmake .. -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH="%QT_PREFIX%" ^
  -DCMAKE_CXX_COMPILER="%MINGW_BIN%\g++.exe" ^
  -DCMAKE_C_COMPILER="%MINGW_BIN%\gcc.exe"
if errorlevel 1 (
    echo Error: cmake configure failed.
    exit /b 1
)

mingw32-make
if errorlevel 1 (
    echo Error: frontend build failed.
    exit /b 1
)

if not exist APLTerminal.exe (
    echo Error: build did not produce APLTerminal.exe
    exit /b 1
)

echo.
echo ==^> Build complete!
echo Executable: %FRONTEND_DIR%build\APLTerminal.exe
echo Run windeploy.bat next to gather the DLLs it needs.
endlocal
exit /b 0

rem --- Tests ---
:check
echo ==^> Sequencer tests...
pushd "%SEQ_DIR%"
mingw32-make CC=gcc AR=ar check
if errorlevel 1 (
    echo Error: sequencer tests failed.
    popd
    exit /b 1
)
popd
echo ==^> Interpreter tests...
pushd "%BACKEND_DIR%"
mingw32-make CC=gcc AR=ar check
if errorlevel 1 (
    echo Error: interpreter tests failed.
    popd
    exit /b 1
)
popd
echo.
echo ==^> All tests passed.
endlocal
exit /b 0

rem --- Clean ---
rem Done here rather than with "make clean", whose recipes need rm (a
rem POSIX shell), which a plain Qt + MinGW install doesn't have.
:clean
echo ==^> Cleaning the front end...
if exist "%FRONTEND_DIR%build" rmdir /s /q "%FRONTEND_DIR%build"
if exist "%FRONTEND_DIR%deploy" rmdir /s /q "%FRONTEND_DIR%deploy"

echo ==^> Cleaning the interpreter...
pushd "%BACKEND_DIR%"
for %%D in (lib_objs cli_objs san_objs tests\.scratch) do if exist %%D rmdir /s /q %%D
for %%F in (libapl.a apl_cli.exe apl_cli_san.exe kapl_unicode_test.exe run_tests.exe) do if exist %%F del /q %%F
if exist tests\*.out del /q tests\*.out
popd

echo ==^> Cleaning the sequencer...
pushd "%SEQ_DIR%"
if exist *.o del /q *.o
for %%F in (libseq.a seqtest.exe seqdemo.exe smftest.exe) do if exist %%F del /q %%F
for %%D in (tests tests\device tests\smf) do if exist %%D\*.out del /q %%D\*.out
popd

echo ==^> Clean complete.
endlocal
exit /b 0
