@echo off
title MTR-OTA - 1-Click First-Time Setup (USB)
echo ============================================================
echo   On The Air ESP (MTR-OTA) - 1-Click First-Time Setup
echo ============================================================
echo This will:
echo   1. Install OnTheAirESP into your Arduino IDE Libraries
echo   2. Verify/install the ESP32 NodeMCU-32S board core
echo   3. Auto-detect your USB NodeMCU-32S and flash it once
echo.
python "%~dp0tools\ota_flash.py" setup %*
echo.
pause
