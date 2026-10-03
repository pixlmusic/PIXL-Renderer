$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.h')
$source = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.cpp')
$atmosphere = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.cpp')
$atmosphereHeader = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.h')
$lighting = Get-Content -Raw (Join-Path $root 'distribution/Shaders/Lighting.hlsl')
$scattering = Get-Content -Raw (Join-Path $root 'pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl')

foreach ($token in @('materialExtinction', 'integratedScattering', 'conservativeDepth', 'memoryBytes', 'temporalValid')) {
    if ($header -notmatch [regex]::Escape($token)) { throw "VolumetricContext missing $token" }
}
if ($source -notmatch 'std::scoped_lock') { throw 'VolumetricContext publication must be synchronized' }
if ($atmosphere -notmatch 'VolumetricContext::Get\(\)\.Publish') { throw 'Atmosphere does not publish the shared froxel volume' }
if ($atmosphere -notmatch 'VolumetricContext::Get\(\)\.Acquire') { throw 'Atmosphere composite does not consume the shared froxel volume' }
if ($atmosphere -notmatch 'TemporalContext::Get\(\)\.IsHistoryValid') { throw 'Atmosphere does not consume unified temporal validity' }
if ($atmosphereHeader -match 'lastVerticalFov|lastExteriorWorldspaceIdentity|hasProjectionHistory|hasWorldspaceHistory') {
    throw 'Legacy broad temporal state remains in Atmosphere'
}
if ($scattering -notmatch 'HistoryDepthRejection|ConservativeDepth') { throw 'Froxel shader lost depth-aware history rejection' }
if ($lighting -notmatch 'Atmosphere/Atmosphere\.hlsli' -or $lighting -notmatch 'Atmosphere::GetAtmosphere\(') {
    throw 'Lighting no longer composites the Atmosphere volume'
}
Write-Host 'VolumetricContext contract passed.'
