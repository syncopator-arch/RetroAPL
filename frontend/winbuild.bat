@echo off
setlocal

rem winbuild.bat
rem
rem Builds the backend (libapl.a) then the Qt frontend (APLTerminal.exe).
rem Run from anywhere -- paths are resolved relative to this script's
rem own location (%~dp0), not the current directory.
rem
rem Expected layout (per CMakeLists.txt):
rem   ...\backend\    <- makefile, produces libapl.a
rem   ...\frontend\   <- CMakeLists.txt, this script

set SCRIPT_DIR=%~dp0
set FRONTEND_DIR=%SCRIPT_DIR%
set BACKEND_DIR=%SCRIPT_DIR%..\backend

call "%SCRIPT_DIR%winenv.bat"
if errorlevel 1 exit /b 1

rem --- Backend ---
echo ==^> Building backend (libapl.a)...
pushd "%BACKEND_DIR%"
mingw32-make CC=%MINGW_BIN%\gcc.exe AR=%MINGW_BIN%\ar.exe libapl.a
if errorlevel 1 (
    echo Error: backend build failed.
    popd
    exit /b 1
)
popd

if not exist "%BACKEND_DIR%\libapl.a" (
    echo Error: backend build did not produce libapl.a
    exit /b 1
)
echo ==^> Backend build complete: %BACKEND_DIR%\libapl.a

rem --- Frontend ---
echo ==^> Building frontend...
cd /d "%FRONTEND_DIR%"
if exist build rmdir /s /q build
mkdir build
cd build

cmake .. -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=%QT_PREFIX% ^
  -DCMAKE_CXX_COMPILER=%MINGW_BIN%\g++.exe ^
  -DCMAKE_C_COMPILER=%MINGW_BIN%\gcc.exe
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

rem --- Font deploy ---
rem APLTerminalWidget::loadAPLFont() only ever checks %APLHOME%\fonts\kapl.ttf
rem (see get_aplhome_dir() in windows_platform.c for the %APLHOME%/
rem %USERPROFILE%\apl fallback). Deploy the repo's canonical copy there so
rem a freshly built exe actually has a font to load when run.
set TARGET_APLHOME=%APLHOME%
if "%TARGET_APLHOME%"=="" set TARGET_APLHOME=%USERPROFILE%\apl

if not exist "%TARGET_APLHOME%\fonts\kapl.ttf" (
    if exist "%SCRIPT_DIR%..\fonts\kapl.ttf" (
        if not exist "%TARGET_APLHOME%\fonts" mkdir "%TARGET_APLHOME%\fonts"
        copy /y "%SCRIPT_DIR%..\fonts\kapl.ttf" "%TARGET_APLHOME%\fonts\kapl.ttf" >nul
        echo ==^> Deployed kapl.ttf to %TARGET_APLHOME%\fonts\kapl.ttf
    ) else (
        echo Warning: kapl.ttf not found at %SCRIPT_DIR%..\fonts\kapl.ttf
        echo Please ensure the font file is in %%APLHOME%%\fonts\ before running.
    )
)

echo.
echo ==^> Build complete!
echo Executable: %FRONTEND_DIR%build\APLTerminal.exe
echo Run windeploy.bat next to bundle the required Qt DLLs.

endlocal
