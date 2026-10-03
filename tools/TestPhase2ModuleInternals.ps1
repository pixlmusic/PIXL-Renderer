[CmdletBinding()]
param([string]$VcVars)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $VcVars) {
    $VcVars = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
}
if (-not (Test-Path -LiteralPath $VcVars -PathType Leaf)) {
    throw "Visual Studio environment script not found: $VcVars"
}

$output = Join-Path $repo 'build\phase2-module-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Join-Path $PSScriptRoot 'TestPhase2ModuleInternals.cpp'
$camera = Join-Path $repo 'engine\Modules\CameraSuite\CameraPolicy.cpp'
$surface = Join-Path $repo 'engine\Modules\GroundResponse\SurfaceClassifier.cpp'
$temporal = Join-Path $repo 'engine\Modules\HybridGI\TemporalPolicy.cpp'
$exe = Join-Path $output 'Phase2ModuleInternals.exe'
$batch = Join-Path $output 'compile-tests.bat'

@"
@echo off
call "$VcVars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /Fe"$exe" "$source" "$camera" "$surface" "$temporal"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ASCII

& $batch
if ($LASTEXITCODE -ne 0) { throw 'Phase 2 module-internal test compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Phase 2 module-internal tests failed.' }

$shaderCache = Get-Content -LiteralPath (Join-Path $repo 'engine\ShaderCache.cpp') -Raw
if ($shaderCache -notmatch 'ShaderCacheInternal::GetShaderProfile' -or
    $shaderCache -notmatch 'ShaderCacheInternal::GetShaderPath' -or
    $shaderCache -notmatch 'ShaderCacheInternal::GetTechnique') {
    throw 'Shader Cache is not delegating descriptor policy to its internal component.'
}

Write-Host 'PASS: Phase 2 internal policies preserve surface, camera, temporal and shader-descriptor contracts.'
