$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.h')
$source = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.cpp')
$atmosphere = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.cpp')
$atmosphereHeader = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.h')
$lighting = Get-Content -Raw (Join-Path $root 'distribution/Shaders/Lighting.hlsl')
$scattering = Get-Content -Raw (Join-Path $root 'pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl')
$lightVolumes = Get-Content -Raw (Join-Path $root 'engine/Modules/LightVolumes.cpp')

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
foreach ($field in @('Width', 'Height', 'Depth')) {
    $minimum = if ($field -eq 'Depth') { 10 } else { 32 }
    if ($lightVolumes -notmatch "size\.$field\s*=\s*std::clamp\(size\.$field,\s*$minimum,\s*640\)") {
        throw "LightVolumes custom $field must remain within the UI allocation limits"
    }
}
foreach ($function in @('LoadSettings', 'RestoreDefaultSettings')) {
    $refresh = "(?s)void LightVolumes::$function\([^)]*\).*?if \(loaded && initialised && gVolumetricLightingSizeHigh\)\s*SetupVL\(\);.*?\n\}"
    if ($lightVolumes -notmatch $refresh) { throw "LightVolumes::$function must refresh the initialized native volume" }
}
foreach ($axis in @('H', 'V')) {
    $blur = Get-Content -Raw (Join-Path $root "distribution/Shaders/ISVolumetricLightingBlur${axis}CS.hlsl")
    if ($blur -notmatch 'int2 pix = clamp\(int2\(x, y\), 0, screenSizeMin1\.xy\)') {
        throw "LightVolumes $axis blur must clamp both halo sampling boundaries"
    }
    if ($blur -notmatch '(?s)GroupMemoryBarrierWithGroupSync\(\);.*?if \(base >= 0 && base < TG_DIM - WINDOW \* 2 && all\(int2\(x, y\) <= screenSizeMin1\.xy\)\)') {
        throw "LightVolumes $axis blur must bound writes after the group-wide barrier"
    }
}
Write-Host 'VolumetricContext, native LightVolumes settings and blur boundary contracts passed.'
