$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$header = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.h')
$source = Get-Content -Raw (Join-Path $root 'engine/Renderer/VolumetricContext.cpp')
$atmosphere = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.cpp')
$atmosphereHeader = Get-Content -Raw (Join-Path $root 'engine/Modules/Atmosphere.h')
$lighting = Get-Content -Raw (Join-Path $root 'distribution/PIXL-Shaders/Lighting.hlsl')
$scattering = Get-Content -Raw (Join-Path $root 'pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl')
$cloudTemporal = Get-Content -Raw (Join-Path $root 'pipeline/VolumetricClouds/Kernels/VolumetricClouds/VolumetricCloudsCS.hlsl')
$cloudDistribution = Get-Content -Raw (Join-Path $root 'distribution/PIXL-Shaders/VolumetricClouds/VolumetricCloudsCS.hlsl')
$hdrSun = Get-Content -Raw (Join-Path $root 'pipeline/Camera Suite/Kernels/CameraSuite/HDRSun.hlsli')
$lightVolumes = Get-Content -Raw (Join-Path $root 'engine/Modules/LightVolumes.cpp')
$settingsDefaults = Get-Content -Raw (Join-Path $root 'distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json') | ConvertFrom-Json

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
$fogDefaults = $settingsDefaults.Atmosphere
if ([math]::Abs($fogDefaults.automaticWeatherStrength - 0.50) -gt 1e-6 -or
	[math]::Abs($fogDefaults.fogDensity - 0.049) -gt 1e-6 -or
	$fogDefaults.startDistance -ne 0 -or $fogDefaults.volumetricFogStartDistance -ne 2224 -or
	$fogDefaults.volumetricFogDistance -ne 48535 -or
	[math]::Abs($fogDefaults.volumetricHistoryWeight - 0.65) -gt 1e-6 -or
	$fogDefaults.volumetricHistoryMissSampleCount -ne 4) {
	throw 'Shipped fog defaults no longer match the promoted runtime profile and motion-stability settings'
}
$cloudDefaults = $settingsDefaults.'PIXL Volumetric Clouds'
if ($settingsDefaults.Version -ne '1-0-9-0' -or $cloudDefaults.Quality -ne 2 -or
	$cloudDefaults.CloudThickness -ne 65000 -or $cloudDefaults.EnableHighCloudLayer -ne $true -or
	[math]::Abs($cloudDefaults.TemporalResponse - 0.68) -gt 1e-6) {
	throw 'Shipped cloud defaults must retain the promoted high-cloud layer at balanced quality with reduced temporal history'
}
if ($cloudTemporal -notmatch 'cameraMotionRejection\s*=\s*smoothstep\(0\.05f,\s*0\.9f,\s*screenMotionPixels\)' -or
	$hdrSun -notmatch 'min\(0\.15f\s*\*\s*log2\(maxBoost\s*/\s*kMinHdrSunBoost\),\s*0\.6f\)') {
	throw 'Sun and cloud temporal response must retain the bounded motion-stability shaping'
}
if ($cloudTemporal -notmatch 'SampleCompositeCloud\(float2 uv, out float cloudDepth\)' -or
	$cloudTemporal -notmatch 'depthWeight\s*=\s*exp2\(-abs\(depths\s*-\s*anchorDepth\)' -or
	$cloudTemporal -notmatch 'maximumOpacity\s*-\s*minimumOpacity\s*>\s*0\.12f') {
	throw 'Cloud composite must use bounded depth/coverage guided upsampling at cloud edges'
}
if ($cloudTemporal -notmatch 'PreviousRayDistance\(float2 uv, float previousViewDepth, out bool valid\)' -or
	$cloudTemporal -notmatch 'historyDepth\s*-\s*previousRayDepth' -or
	$cloudTemporal -notmatch 'min\(Temporal\.w,\s*0\.045f\)') {
	throw 'Cloud temporal depth must validate the reprojected point in previous-camera ray space'
}
if ($cloudTemporal -notmatch 'currentMinOpacity\s*=\s*1\.0f\s*-\s*localMax\.a' -or
	$cloudTemporal -notmatch 'historyOpacity\s*=\s*1\.0f\s*-\s*history\.a' -or
	$cloudTemporal -notmatch 'historyOpacity\s*>\s*currentMaxOpacity\s*\+\s*coverageTolerance' -or
	$cloudTemporal -notmatch 'historyWeight\s*=\s*0\.0f') {
	throw 'Cloud temporal history must reject old coverage outside the current neighborhood to prevent clear-sky alpha stencils'
}
if ($cloudTemporal -cne $cloudDistribution) {
	throw 'Cloud distribution payload must match the authoritative pipeline shader'
}
if ($scattering -notmatch 'historyAlpha\s*\*=\s*exp2\(-8\.0f\s*\*\s*motionInFroxels\)' -or
	$scattering -notmatch 'all\(isfinite\(previousClip\)\)') {
	throw 'Volumetric fog history must reject camera motion and invalid reprojections'
}
if ($scattering -notmatch 'HistoryDepthRejection|ConservativeDepth') { throw 'Froxel shader lost depth-aware history rejection' }
if ($scattering -notmatch 'ComputeSkyLightScattering\(float3 positionWS\)' -or
	$scattering -notmatch 'GetAmbientColorOccluded\(kAmbientProbeSkyDirection, skyVisibility\)' -or
	$scattering -notmatch 'EvaluateDiffuse\(skyBounceSH, kSkyBounceUp,') {
	throw 'Volumetric ambient sky must use stable world-space hemisphere directions, independent of camera rotation'
}
if ($scattering -notmatch '(?s)float directionalShadow = 1\.0f;.*?SampleDirectionalShadow\(centerPositionWS\).*?SampleDirectionalWorldShadow\(centerPositionWS\).*?ComputeLightScattering\(dispatchID, cellOffset, directionalShadow\)') {
	throw 'Volumetric history-miss samples must share one center-sampled directional visibility result'
}
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
    $blur = Get-Content -Raw (Join-Path $root "distribution/PIXL-Shaders/ISVolumetricLightingBlur${axis}CS.hlsl")
    if ($blur -notmatch 'int2 pix = clamp\(int2\(x, y\), 0, screenSizeMin1\.xy\)') {
        throw "LightVolumes $axis blur must clamp both halo sampling boundaries"
    }
    if ($blur -notmatch '(?s)GroupMemoryBarrierWithGroupSync\(\);.*?if \(base >= 0 && base < TG_DIM - WINDOW \* 2 && all\(int2\(x, y\) <= screenSizeMin1\.xy\)\)') {
        throw "LightVolumes $axis blur must bound writes after the group-wide barrier"
    }
}
Write-Host 'VolumetricContext, native LightVolumes settings and blur boundary contracts passed.'
