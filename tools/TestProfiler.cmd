@echo off
if not defined PIXL_TEST_VCVARS (
    echo Set PIXL_TEST_VCVARS to the vcvars64.bat path before running.
    exit /b 2
)
call "%PIXL_TEST_VCVARS%" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /MD tools\TestProfiler.cpp /Fe:build\TestProfiler.exe /Fo:build\TestProfiler.obj /link d3d11.lib
if errorlevel 1 (popd & exit /b 1)
build\TestProfiler.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /MD tools\TestHybridGIMath.cpp /Fe:build\TestHybridGIMath.exe /Fo:build\TestHybridGIMath.obj
if errorlevel 1 (popd & exit /b 1)
build\TestHybridGIMath.exe
set testResult=%errorlevel%
popd
exit /b %testResult%
