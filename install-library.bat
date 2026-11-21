@echo off
title MTR-OTA - Install Arduino IDE Library
echo ====================================================
echo   Installing OnTheAirESP into Arduino IDE Libraries
echo ====================================================
echo.
python "%~dp0tools\ota_flash.py" install-lib %*
echo.
pause
