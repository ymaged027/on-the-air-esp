@echo off
title MTR-OTA - Wireless OTA Monitor
echo ====================================================
echo   MTR-OTA Wireless OTA Monitor (192.168.4.1:23)
echo ====================================================
echo Make sure you are connected to Wi-Fi: MTR-OTA (pass: mtr-ota007)
echo Type 'help' once connected. Press Ctrl+C to exit.
echo.
python "%~dp0tools\ota_monitor.py" --connect 192.168.4.1 %*
echo.
pause
