@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem DeployPIXL.bat
rem Deploys the current Release DLL and the runtime rain/particle shader sources.
rem Override any installation with an environment variable before running:
rem   set PIXL_SKYRIM_ROOT_1=H:\path\to\Skyrim
rem   set PIXL_SKYRIM_ROOT_2=H:\path\to\Skyrim
rem   set PIXL_SKYRIM_ROOT_3=H:\path\to\Skyrim

set "ROOT=%~dp0"
set "DLL=%ROOT%build\PIXL-12C\Release\PIXLRenderer.dll"
set "PARTICLE=%ROOT%distribution\Shaders\Particle.hlsl"
set "RAIN=%ROOT%pipeline\Rain Response\Kernels\RainResponse"

if not defined PIXL_SKYRIM_ROOT_1 set "PIXL_SKYRIM_ROOT_1=H:\The Elder Scrolls - Skyrim - Special Edition"
if not defined PIXL_SKYRIM_ROOT_2 set "PIXL_SKYRIM_ROOT_2=H:\SKYRIM-SE-GOG\Skyrim Anniversary Edition"
if not defined PIXL_SKYRIM_ROOT_3 set "PIXL_SKYRIM_ROOT_3=H:\SteamLibrary\steamapps\common\Skyrim Special Edition"

if not exist "%DLL%" (
    echo ERROR: Release DLL not found: %DLL%
    echo Run BuildRelease.bat first.
    exit /b 1
)

set "FAILED=0"
for %%G in (1 2 3) do call :deploy "%%G" "!PIXL_SKYRIM_ROOT_%%G!"
if "%FAILED%" == "1" (
    echo.
    echo One or more installations could not be deployed.
    exit /b 1
)

echo.
echo PIXL deployed successfully to all detected installations.
exit /b 0

:deploy
set "INDEX=%~1"
set "GAME=%~2"
if not exist "%GAME%\SkyrimSE.exe" (
    echo [%INDEX%] Skipping missing Skyrim installation: %GAME%
    exit /b 0
)
if not exist "%GAME%\Data\SKSE\Plugins" mkdir "%GAME%\Data\SKSE\Plugins"
if not exist "%GAME%\Data\Shaders\RainResponse" mkdir "%GAME%\Data\Shaders\RainResponse"

copy /Y "%DLL%" "%GAME%\Data\SKSE\Plugins\PIXLRenderer.dll" >nul
if errorlevel 1 set "FAILED=1"
if exist "%PARTICLE%" copy /Y "%PARTICLE%" "%GAME%\Data\Shaders\Particle.hlsl" >nul
if errorlevel 1 set "FAILED=1"

for %%S in (Precipitation.hlsli RainResponse.hlsli RoofRunoffCompositeCS.hlsl RoofRunoffDetectCS.hlsl RoofRunoffGenerateCS.hlsl RoofRunoffResolveCS.hlsl WorldPrecipitation.hlsli optimized-ggx.hlsli) do (
    if exist "%RAIN%\%%S" copy /Y "%RAIN%\%%S" "%GAME%\Data\Shaders\RainResponse\%%S" >nul
    if errorlevel 1 set "FAILED=1"
)
echo [%INDEX%] Deployed: %GAME%
exit /b 0
