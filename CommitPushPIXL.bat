@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "MESSAGE=%~1"
if not defined MESSAGE set "MESSAGE=PIXL Renderer update"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\PIXLRelease.ps1" -Action PackageCommitPush -Message "%MESSAGE%"
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
