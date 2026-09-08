[CmdletBinding()]
param(
    [string]$BaselinePath = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $repoRoot 'distribution\SKSE\Plugins\PIXLRenderer\SettingsDefault.json'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot 'distribution\SKSE\Plugins\PIXLRenderer\Presets\PIXL-Renderer-Ultimate.json'
}

$config = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $BaselinePath) | ConvertFrom-Json

# Keep the source-defined Ultra workload contract in lockstep with
# Renderer/QualityProfiles.cpp. These are the highest validated production
# values, not unbounded experimental maxima.
$config.Menu.RendererQuality = 3
$config.Menu.LightingQuality = 3
$config.Menu.MaterialsQuality = 3
$config.Menu.AtmosphereQuality = 3
$config.Menu.WaterQuality = 3
$config.Menu.TerrainVegetationQuality = 3
$config.Menu.CharactersQuality = 3
$config.Menu.CameraQuality = 3

$gi = $config.'Hybrid GI'
$gi.Enabled = $true
$gi.EnableGI = $true
$gi.EnableBlur = $true
$gi.EnableTemporalDenoiser = $true
$gi.EnableWorldCache = $true
$gi.EnableDirectionalOcclusion = $true
$gi.EnableBentNormalLighting = $true
$gi.EnableSpecularOcclusion = $true
$gi.EnableAdaptiveDenoiser = $true
$gi.ResolutionMode = 0
$gi.NumSlices = 6
$gi.NumSteps = 12
$gi.WorldCacheSampleCount = 8
$gi.WorldCacheTraceSteps = 4
$gi.WorldCacheInjectionStride = 2
$gi.EnableWorldCacheSecondBounce = $true
$gi.ReflectionSteps = 48
$gi.MaxAccumFrames = 18
$gi.BlurRadius = 2.0
$gi.RadianceFireflyClamp = 8.0
$gi.ReflectionFireflyClamp = 10.0
$gi.WorldCacheTemporalResponse = 0.14
$config.'Contact Shadows'.SampleCount = 2
$config.'Material Forge'.LocalContactShadowLightCount = 2
$config.'Light Volumes'.ExteriorQuality = 2
$config.'Light Volumes'.InteriorQuality = 2

$forge = $config.'Material Forge'
$forge.EnableSpecularAA = 1
$forge.SpecularAAStrength = 1.34
$forge.EnableGGXMultiScatter = 1
$forge.GGXMultiScatterStrength = 1.0
$layers = $config.'Material Layers'
$layers.EnableComplexMaterial = 1
$layers.EnableParallax = 1
$layers.EnableHeightBlending = 1
$layers.EnableShadows = 1
$tuning = $layers.'PIXL Tuning'
$tuning.ObjectNearSteps = 12
$tuning.ObjectMaxSteps = 24
$tuning.ObjectRefinementSteps = 8
$tuning.TerrainNearSteps = 10
$tuning.TerrainMaxSteps = 30
$tuning.TerrainRefinementSteps = 8
$tuning.EnableDetailReconstruction = 1
$tuning.DetailQuality = 2

$config.Atmosphere.volumetricGridPixelSize = 24
$config.Atmosphere.volumetricGridSizeZ = 64
$config.Atmosphere.volumetricHistoryMissSampleCount = 4

$water = $config.'Water Optics'
$water.EnableEnhancedSSR = 1
$water.EnableEnhancedCaustics = 1
$water.SSRDistanceScale = 1.5
$water.SSREdgeFade = 0.25
$water.CausticsDispersion = 0.88

$ground = $config.'Ground Response'
$ground.EnableDeformableGround = $true
$ground.EnableSnowDeformation = $true
$ground.EnableMudDeformation = $true
$ground.GeometryRenderDistance = 4000.0
$ground.GeometryFadeStart = 3400.0
$ground.GeometryTessellationNear = 14.0
$ground.GeometryTessellationFar = 3.0
$ground.GeometryTessellationNearDistance = 699.0
$ground.GeometryTessellationFarDistance = 1895.0
# Preserve the requested softer snow-to-ground transition.
$ground.SnowCoverageFeather = 0.35

$config.'Skin Optics'.EnableSkin = $true
$config.'Skin Optics'.EnableSkinDetail = $true
$config.'Skin Optics'.UseSSS = $true
$config.'Tissue Diffusion'.BurleySamples = 24
$config.'Strand Shading'.Enabled = 1
$config.'Strand Shading'.HairMode = 1
$config.'Strand Shading'.EnableSelfShadow = 1
$config.'Actor Surface Effects'.EffectQuality = 3
$config.'Actor Surface Effects'.MaximumAffectedNPCs = 48
$config.'Actor Surface Effects'.EffectDistance = 4800.0

# Native DLAA is the highest-quality real-time reconstruction path when the
# DLSS sidecar is supported. TAA remains the non-DLSS fallback. FG and live NR
# stay off so this preset is deterministic and avoids the current sidecar
# hardware warnings observed in runtime logs.
$ir = $config.ImageReconstruction
$ir.upscaleMethod = 3
$ir.upscaleMethodNoDLSS = 1
$ir.qualityMode = 0
$ir.presetDLSS = 5
$ir.forceLatestDLSSModelOnLegacyRTX = $true
$ir.sharpnessEnabledDLSS = $false
$ir.sharpnessDLSS = 0.0
$ir.sharpnessFSR = 0.0
$ir.frameGenerationMode = 0
$ir.frameGenerationForceEnable = 0
$ir.neuralRenderingEnabled = $false

# Photo Finish may use its separate multi-sample path; this does not affect
# normal gameplay rendering cost.
$config.'Pixel Capture'.PhotoFinishQualityPreset = 3
$config.'Pixel Capture'.PhotoFinishScale = 4
$config.'Pixel Capture'.PhotoFinishTemporalSamples = 24
$config.'Pixel Capture'.PhotoFinishNeuralEnabled = $true

$utf8NoBom = New-Object Text.UTF8Encoding($false)
$json = $config | ConvertTo-Json -Depth 100
$resolvedOutput = if ([IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $repoRoot $OutputPath }
[IO.File]::WriteAllText($resolvedOutput, $json + [Environment]::NewLine, $utf8NoBom)
Write-Output "Wrote $resolvedOutput"
