@echo off
REM Launches the release build straight into a saved game, waits for it to
REM finish loading, grabs the window to a PNG, then closes it. One argument:
REM the save name (defaults to "poo"). Output: D:\RWE\visual-capture.png
setlocal
set SAVE=%1
if "%SAVE%"=="" set SAVE=poo

start "" /D "D:\RWE\build-release" "D:\RWE\build-release\rwe.exe" --load=%SAVE%
timeout /t 30 /nobreak >nul
ffmpeg -y -loglevel error -f gdigrab -framerate 1 -i title=RWE -frames:v 1 "D:\RWE\visual-capture.png"
taskkill /IM rwe.exe /F >nul 2>&1
echo captured to D:\RWE\visual-capture.png
