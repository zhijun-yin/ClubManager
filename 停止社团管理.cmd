@echo off
setlocal
chcp 65001 >nul
title ClubManager - Stop
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." --exec python3 scripts/launch.py --stop %*
if errorlevel 1 (
    echo.
    echo Could not stop ClubManager. Please read the message above.
    pause
    exit /b 1
)
timeout /t 2 /nobreak >nul 2>nul
exit /b 0
