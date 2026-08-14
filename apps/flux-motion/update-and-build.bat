@echo off
setlocal

rem Normal use:
rem   update-and-build.bat
rem Clean configure and rebuild:
rem   update-and-build.bat clean
rem   update-and-build.bat /clean
rem   update-and-build.bat -clean

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-and-build.ps1" %*
set "RESULT=%ERRORLEVEL%"

if not "%RESULT%"=="0" (
    echo.
    echo Update/build failed with exit code %RESULT%.
    pause
)

exit /b %RESULT%
