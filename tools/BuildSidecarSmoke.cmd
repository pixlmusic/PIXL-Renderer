@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PIXL_VS=%%i"
if not defined PIXL_VS exit /b 1
call "%PIXL_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /I extern\Streamline-DX12\include tools\SidecarSmoke.cpp /Fe:build\SidecarSmoke.exe /Fo:build\SidecarSmoke.obj /link d3d11.lib d3d12.lib dxgi.lib user32.lib
exit /b %errorlevel%
