@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Drag a PipelineLibrary folder onto this file, or place/run it inside that folder.
if "%~1"=="" (
    set "ROOT=%CD%"
) else (
    set "ROOT=%~1"
)

if not exist "%ROOT%\Pixel" goto :invalid
if not exist "%ROOT%\Vertex" goto :invalid
if not exist "%ROOT%\Compute" goto :invalid

set /a REMOVED=0
for /r "%ROOT%" %%F in (__folder_managed_by_vortex) do (
    if exist "%%~fF" (
        del /f /q "%%~fF" >nul 2>&1
        if not exist "%%~fF" set /a REMOVED+=1
    )
)

echo Removed !REMOVED! Vortex marker file(s) from:
echo %ROOT%
pause
exit /b 0

:invalid
echo Refusing to run: this does not look like a PIXL PipelineLibrary folder.
echo Expected Pixel, Vertex, and Compute subdirectories under:
echo %ROOT%
pause
exit /b 1
