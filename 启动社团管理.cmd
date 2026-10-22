@echo off
setlocal
chcp 65001 >nul
title ClubManager
wsl.exe -d Ubuntu-24.04 --cd "%~dp0." --exec python3 scripts/launch.py --background %*
if errorlevel 1 (
    echo.
    echo ClubManager could not start. Please read the message above.
    pause
    exit /b 1
)
