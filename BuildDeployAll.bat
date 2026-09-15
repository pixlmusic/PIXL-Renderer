@echo off
setlocal EnableExtensions

rem Build the Release configuration, then deploy the resulting DLL and
rem runtime shader sources to the three local Skyrim installations.
call "%~dp0BuildRelease.bat" PIXL-12C
if errorlevel 1 (
    echo.
    echo BUILD FAILED - deployment was not started.
    exit /b 1
)

call "%~dp0DeployPIXL.bat"
set "exit_code=%ERRORLEVEL%"
endlocal & exit /b %exit_code%
