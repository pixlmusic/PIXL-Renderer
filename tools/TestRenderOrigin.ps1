[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo 'build\render-origin-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Join-Path $PSScriptRoot 'TestRenderOrigin.cpp'
$exe = Join-Path $output 'RenderOriginTests.exe'
$obj = Join-Path $output 'RenderOriginTests.obj'
$batch = Join-Path $output 'compile-tests.bat'
@"
@echo off
call "$VcVars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"$obj" /Fe"$exe" "$source"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ASCII
& $batch
if ($LASTEXITCODE -ne 0) { throw 'RenderOrigin test compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'RenderOrigin tests failed.' }
