@echo off
setlocal EnableExtensions
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\PIXLRelease.ps1" -Action Deploy
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
