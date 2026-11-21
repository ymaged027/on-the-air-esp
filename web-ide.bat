@echo off
title MTR-OTA - Web Code Studio (http://127.0.0.1:8080)
echo ============================================================
echo   MTR-OTA Web Code Studio
echo   Paste Arduino code in your browser, then Compile ^& Flash!
echo ============================================================
echo Connect Wi-Fi to MTR-OTA (pass: mtr-ota007), then keep this
echo window open while using the Code Window in your browser.
echo.
python "%~dp0tools\ota_studio.py" %*
echo.
pause
