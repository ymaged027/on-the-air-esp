@echo off
title MTR-OTA - Uninstall Silent Background Compiler
echo ============================================================
echo   Stopping ^& Removing MTR-OTA Silent Background Compiler
echo ============================================================
echo.
python "%~dp0tools\ota_studio.py" --uninstall-startup
echo.
pause
