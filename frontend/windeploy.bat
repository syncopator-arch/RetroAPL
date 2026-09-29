@echo off
setlocal

rem windeploy.bat
rem
rem Bundles the Qt runtime DLLs alongside the built exe so it will run
rem on a machine without a Qt dev install. Run after winbuild.bat.
rem Resolved relative to this script's own location, so it works
rem regardless of the current directory.

set SCRIPT_DIR=%~dp0
set EXE=%SCRIPT_DIR%build\APLTerminal.exe

call "%SCRIPT_DIR%winenv.bat"
if errorlevel 1 exit /b 1
set WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe

if not exist "%EXE%" (
    echo Error: %EXE% not found. Run winbuild.bat first.
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo Error: windeployqt.exe not found at %WINDEPLOYQT%
    echo Check your Qt install path/version.
    exit /b 1
)

"%WINDEPLOYQT%" "%EXE%"
if errorlevel 1 (
    echo Error: windeployqt failed.
    exit /b 1
)

echo.
echo ==^> Qt runtime DLLs deployed alongside %EXE%
echo Reminder: the app still reads its own font from %%APLHOME%%\fonts\kapl.ttf
echo (windeployqt only bundles Qt itself, not kapl.ttf) -- winbuild.bat
echo deploys it there automatically if %%APLHOME%% is set.

endlocal
