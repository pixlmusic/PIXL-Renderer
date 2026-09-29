@echo off
setlocal EnableExtensions
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\PIXLDeveloperTools.ps1" -Action Git
set "exit_code=%ERRORLEVEL%"
if not "%exit_code%"=="0" pause
endlocal & exit /b %exit_code%
