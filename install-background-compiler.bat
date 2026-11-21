@echo off
title MTR-OTA - Install Silent Background Compiler
echo ============================================================
echo   Installing MTR-OTA Silent Background Compiler
echo ============================================================
echo After this, you NEVER need to open web-ide.bat again!
echo It runs invisibly in the background (no console window)
echo and auto-starts whenever you log into Windows.
echo.
python "%~dp0tools\ota_studio.py" --install-startup
echo.
pause
