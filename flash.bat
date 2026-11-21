@echo off
title MTR-OTA - 1-Click Wireless Flasher
echo ====================================================
echo   On The Air ESP (MTR-OTA) - Compile ^& Flash Wi-Fi
echo ====================================================
echo Make sure you are connected to Wi-Fi: MTR-OTA (pass: mtr-ota007)
echo.
python "%~dp0tools\ota_flash.py" push %*
echo.
pause
