@echo off
title On The Air ESP - 1-Click Wireless Flasher
echo ====================================================
echo   On The Air ESP - Compile ^& Flash Over Wi-Fi
echo ====================================================
echo Make sure you are connected to your Sumobot Wi-Fi (e.g. SUMO-01-OTA).
echo.
python "%~dp0tools\sumo_flash.py" push %*
echo.
pause
