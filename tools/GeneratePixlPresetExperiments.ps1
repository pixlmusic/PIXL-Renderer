[CmdletBinding()]
param(
    [string]$BaselinePath = 'H:\The Elder Scrolls - Skyrim - Special Edition\Data\SKSE\Plugins\PIXL\Config\UserGraphics.json',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'PIXL_Preset_Experiments'
}
$baselineResolved = (Resolve-Path -LiteralPath $BaselinePath).Path
$baselineHashBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $baselineResolved).Hash
$baselineBytes = [IO.File]::ReadAllBytes($baselineResolved)
$utf8NoBom = New-Object Text.UTF8Encoding($false)

function Read-Config {
    Get-Content -Raw -LiteralPath $baselineResolved | ConvertFrom-Json
}

function Write-Config([object]$Config, [string]$Path) {
    $json = $Config | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8NoBom)
}

function Set-MenuTier([object]$Config, [int]$Tier) {
    $Config.Menu.RendererQuality = $Tier
    $Config.Menu.LightingQuality = $Tier
    $Config.Menu.MaterialsQuality = $Tier
    $Config.Menu.AtmosphereQuality = $Tier
    $Config.Menu.WaterQuality = $Tier
    $Config.Menu.TerrainVegetationQuality = $Tier
    $Config.Menu.CharactersQuality = $Tier
    $Config.Menu.CameraQuality = $Tier
}

function Apply-QualityContract([object]$Config, [int]$Tier) {
    $lighting = @(
        @{ Slices=3; Steps=6; CacheSamples=3; CacheTrace=2; Stride=6; Second=$false; Reflections=16; ShadowSamples=1; LocalShadows=1; History=20; Blur=2.8; RadianceClamp=4.0; ReflectionClamp=4.0; Response=0.08 },
        @{ Slices=4; Steps=8; CacheSamples=4; CacheTrace=3; Stride=4; Second=$true;  Reflections=24; ShadowSamples=1; LocalShadows=1; History=20; Blur=2.5; RadianceClamp=5.0; ReflectionClamp=6.0; Response=0.10 },
        @{ Slices=5; Steps=10;CacheSamples=6; CacheTrace=4; Stride=3; Second=$true;  Reflections=32; ShadowSamples=2; LocalShadows=1; History=20; Blur=2.2; RadianceClamp=6.0; ReflectionClamp=8.0; Response=0.12 },
        @{ Slices=6; Steps=12;CacheSamples=8; CacheTrace=4; Stride=2; Second=$true;  Reflections=48; ShadowSamples=2; LocalShadows=2; History=18; Blur=2.0; RadianceClamp=8.0; ReflectionClamp=10.0;Response=0.14 }
    )[$Tier]
    $gi = $Config.'Hybrid GI'
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
    $gi.NumSlices = $lighting.Slices
    $gi.NumSteps = $lighting.Steps
    $gi.WorldCacheSampleCount = $lighting.CacheSamples
    $gi.WorldCacheTraceSteps = $lighting.CacheTrace
    $gi.WorldCacheInjectionStride = $lighting.Stride
    $gi.EnableWorldCacheSecondBounce = $lighting.Second
    $gi.ReflectionSteps = $lighting.Reflections
    $gi.MaxAccumFrames = $lighting.History
    $gi.BlurRadius = $lighting.Blur
    $gi.RadianceFireflyClamp = $lighting.RadianceClamp
    $gi.ReflectionFireflyClamp = $lighting.ReflectionClamp
    $gi.WorldCacheTemporalResponse = $lighting.Response
    $Config.'Contact Shadows'.SampleCount = $lighting.ShadowSamples
    $Config.'Material Forge'.LocalContactShadowLightCount = $lighting.LocalShadows
    $nativeVolumeTier = [Math]::Min($Tier, 2)
    $Config.'Light Volumes'.ExteriorQuality = $nativeVolumeTier
    $Config.'Light Volumes'.InteriorQuality = $nativeVolumeTier

    $specularAA = @(0.50, 0.75, 1.00, 1.34)[$Tier]
    $multiscatter = @(0.55, 0.75, 1.00, 1.00)[$Tier]
    $objectNear = @(4, 6, 9, 12)[$Tier]
    $objectMax = @(8, 12, 18, 24)[$Tier]
    $objectRefine = @(2, 4, 6, 8)[$Tier]
    $terrainNear = @(4, 6, 8, 10)[$Tier]
    $terrainMax = @(8, 14, 22, 30)[$Tier]
    $terrainRefine = @(2, 4, 6, 8)[$Tier]
    $detailQuality = @(0, 1, 2, 2)[$Tier]
    $forge = $Config.'Material Forge'
    $forge.EnableSpecularAA = 1
    $forge.SpecularAAStrength = $specularAA
    $forge.EnableGGXMultiScatter = 1
    $forge.GGXMultiScatterStrength = $multiscatter
    $layers = $Config.'Material Layers'
    $layers.EnableComplexMaterial = 1
    $layers.EnableParallax = [int]($Tier -ge 1)
    $layers.EnableHeightBlending = [int]($Tier -ge 2)
    $layers.EnableShadows = [int]($Tier -ge 1)
    $tuning = $layers.'PIXL Tuning'
    $tuning.ObjectNearSteps = $objectNear
    $tuning.ObjectMaxSteps = $objectMax
    $tuning.ObjectRefinementSteps = $objectRefine
    $tuning.TerrainNearSteps = $terrainNear
    $tuning.TerrainMaxSteps = $terrainMax
    $tuning.TerrainRefinementSteps = $terrainRefine
    $tuning.EnableDetailReconstruction = [int]($Tier -ge 1)
    $tuning.DetailQuality = $detailQuality

    $Config.Atmosphere.volumetricGridPixelSize = @(32, 24, 20, 16)[$Tier]
    $Config.Atmosphere.volumetricGridSizeZ = @(32, 40, 52, 64)[$Tier]
    $Config.Atmosphere.volumetricHistoryMissSampleCount = @(1, 2, 3, 4)[$Tier]

    $water = $Config.'Water Optics'
    $water.EnableEnhancedSSR = 1
    $water.EnableEnhancedCaustics = 1
    $water.SSRDistanceScale = @(0.65, 0.86, 1.06, 1.29)[$Tier]
    $water.SSREdgeFade = @(1.35, 1.15, 0.95, 0.80)[$Tier]
    $water.CausticsDispersion = @(0.25, 0.45, 0.65, 0.88)[$Tier]

    $ground = $Config.'Ground Response'
    $ground.EnableDeformableGround = $true
    $ground.EnableSnowDeformation = $true
    $ground.EnableMudDeformation = $true
    $ground.GeometryRenderDistance = @(1800.0, 2600.0, 3400.0, 4000.0)[$Tier]
    $ground.GeometryFadeStart = @(1450.0, 2150.0, 2850.0, 3400.0)[$Tier]
    $ground.GeometryTessellationNear = @(4.0, 7.0, 10.0, 14.0)[$Tier]
    $ground.GeometryTessellationFar = @(1.5, 2.0, 2.5, 3.0)[$Tier]
    $ground.GeometryTessellationNearDistance = @(400.0, 500.0, 600.0, 699.0)[$Tier]
    $ground.GeometryTessellationFarDistance = @(900.0, 1250.0, 1600.0, 1895.0)[$Tier]
    $Config.'Terrain Detail'.enableLODTerrainTilingFix = 1

    $skin = $Config.'Skin Optics'
    $skin.EnableSkin = $true
    $skin.EnableSkinDetail = ($Tier -ge 1)
    $skin.UseSSS = $true
    $Config.'Tissue Diffusion'.BurleySamples = @(8, 12, 16, 21)[$Tier]
    $Config.'Strand Shading'.Enabled = 1
    $Config.'Strand Shading'.HairMode = [int]($Tier -ge 2)
    $Config.'Strand Shading'.EnableSelfShadow = [int]($Tier -ge 1)
    $Config.'Actor Surface Effects'.EffectQuality = $Tier
    Set-MenuTier $Config $Tier
}

function Set-DLAA([object]$Config) {
    $ir = $Config.ImageReconstruction
    $ir.upscaleMethod = 3
    $ir.upscaleMethodNoDLSS = 2
    $ir.qualityMode = 0
    $ir.presetDLSS = 5
    $ir.forceLatestDLSSModelOnLegacyRTX = $true
    $ir.sharpnessEnabledDLSS = $false
    $ir.sharpnessDLSS = 0.0
    $ir.sharpnessFSR = 0.0
    $ir.frameGenerationMode = 0
    $ir.frameGenerationForceEnable = 0
}

function Flatten-Config([object]$Node, [string]$Path = '') {
    if ($null -eq $Node) {
        [pscustomobject]@{ Path=$Path; Value='null'; Raw=$null }
        return
    }
    if ($Node -is [pscustomobject]) {
        foreach ($property in $Node.PSObject.Properties) {
            $child = if ($Path) { "$Path.$($property.Name)" } else { $property.Name }
            Flatten-Config $property.Value $child
        }
        return
    }
    if ($Node -is [System.Collections.IList] -and $Node -isnot [string]) {
        $compact = $Node | ConvertTo-Json -Compress -Depth 20
        [pscustomobject]@{ Path=$Path; Value=$compact; Raw=$Node }
        return
    }
    [pscustomobject]@{ Path=$Path; Value=($Node | ConvertTo-Json -Compress); Raw=$Node }
}

function Test-ConfigValueEqual([object]$Left, [object]$Right) {
    if ($null -eq $Left -or $null -eq $Right) { return $null -eq $Left -and $null -eq $Right }
    $leftNumeric = $Left -is [ValueType] -and $Left -isnot [bool] -and $Left -isnot [char]
    $rightNumeric = $Right -is [ValueType] -and $Right -isnot [bool] -and $Right -isnot [char]
    if ($leftNumeric -and $rightNumeric) { return [double]$Left -eq [double]$Right }
    if ($Left -is [System.Collections.IList] -and $Left -isnot [string]) {
        return ($Left | ConvertTo-Json -Compress -Depth 20) -eq ($Right | ConvertTo-Json -Compress -Depth 20)
    }
    return $Left -eq $Right
}

function Get-ModuleInfo([string]$TopLevel) {
    $map = @{
        'Actor Surface Effects'=@('Actor Surface Effects','engine/Modules/ActorSurfaceEffects.cpp','Lighting.hlsl + ActorSurfaceEffects.hlsli','Characters > Surface Effects','Fidelity/performance; actor-local temporal state')
        'Advanced'=@('Shader infrastructure','engine/State.cpp + ShaderCache','Compiler/runtime service','Developer','CPU/build behavior; no direct image effect')
        'Ambient Probe'=@('Ambient Probe','engine/Modules/AmbientProbe.cpp','Lighting/ambient-probe consumers','Lighting > Ambient','Artistic lighting; indirect/exposure interaction')
        'Atmosphere'=@('Atmosphere','engine/Modules/Atmosphere.cpp','VolumetricFog and atmosphere shaders','Atmosphere','Fidelity/performance; temporal froxel history')
        'Camera Suite'=@('Camera Suite','engine/Modules/CameraSuite.cpp','HDROutputCS/post composite','Camera','Artistic/post; exposure and temporal presentation')
        'Contact Shadows'=@('Contact Shadows','engine/Modules/ContactShadows.cpp','ContactShadowsCS + Lighting.hlsl t45','Lighting > Shadows','Fidelity/performance; screen-space stability')
        'Disable at Boot'=@('Module lifecycle','engine/State.cpp','Feature define/resource availability','Modules','Infrastructure; may change shader permutations')
        'Distance Blend'=@('Distance Blend','engine/Modules/DistanceBlend.cpp','Lighting/LOD shaders','Terrain','Fidelity; non-temporal')
        'Foliage Dynamics'=@('Foliage Dynamics','engine/Modules/FoliageDynamics.cpp','RunGrass/Lighting foliage paths','Vegetation','Artistic/fidelity; temporal alpha and motion stability')
        'General'=@('Renderer core','engine/State.cpp','Shader cache/runtime','General','Infrastructure; no monotonic fidelity meaning')
        'Ground Response'=@('Ground Response','engine/Modules/GroundResponse.cpp','GroundResponse compute + Lighting tessellation','Terrain > Ground Response','Fidelity/performance; persistent deformation history')
        'Horizon Blend'=@('Horizon Blend','engine/Modules/HorizonBlend.cpp','Terrain/LOD correction','Automatic','Correctness service')
        'Hybrid GI'=@('Hybrid GI / Radiance Weave','engine/Modules/HybridGI.cpp','HybridGI compute + Lighting composite','Lighting > Radiance Weave','Fidelity/performance; extensive temporal history')
        'ImageReconstruction'=@('Image Reconstruction','engine/Modules/ImageReconstruction.cpp','DLSS/FSR/TAA + masks/RCAS','Reconstruction','Resolution/performance; central temporal consumer')
        'Interior Daylight'=@('Interior Daylight','engine/Modules/InteriorDaylight.cpp','Interior geometry/light hooks','Lighting > Interiors','Correctness/fidelity')
        'Light Volumes'=@('Light Volumes','engine/Modules/LightVolumes.cpp','Volumetric lighting grid','Lighting > Volumes','Fidelity/performance; volumetric history interaction')
        'Linear Light Core'=@('Linear Light Core','engine/Modules/LinearLightCore.cpp','Color.hlsli + Lighting/Water/Grass/Effect','Lighting > Linear Light Core','Global color-space/energy calibration; not monotonic')
        'Material Forge'=@('Material Forge','engine/MaterialForge.cpp','Lighting PBR/BRDF paths','Materials > PBR','Fidelity/performance; specular temporal stability')
        'Material Layers'=@('Material Layers','engine/Modules/MaterialLayers.cpp','Lighting POM/material-layer paths','Materials > Surface Depth','Fidelity/performance; parallax shimmer risk')
        'Menu'=@('Quality/UI','engine/Menu.cpp + Renderer/QualityProfiles.cpp','Seven coordinated quality contracts','Quality','Control metadata; tiers 0..3')
        'Natural Lighting'=@('Natural Lighting','engine/Modules/NaturalLighting.cpp','CPU light preparation','Automatic','Correctness service')
        'Pixel Capture'=@('Pixel Capture','engine/Modules/PixelCapture.cpp','Capture/photo-finish kernels','Photo Mode','Offline fidelity/memory; temporal sampling')
        'Pulse Profiler'=@('Pulse Profiler','engine/Modules/PulseProfiler.cpp','CPU/GPU telemetry overlay','Performance','Diagnostics only')
        'Radiant Grid'=@('Radiant Grid','engine/Modules/RadiantGrid.cpp','Lighting clustered local-light buffers','Lighting > Local Lights','Fidelity/performance; deterministic grid stability')
        'Rain Response'=@('Rain Response','engine/Modules/RainResponse.cpp','Rain/snow particles, wetness, runoff','Weather','Fidelity/performance; temporal/transparency sensitivity')
        'Replace Original Shaders'=@('Shader replacement','engine/State.cpp','Skyrim shader-class hooks','Advanced','Required integration; not a quality dial')
        'Skin Optics'=@('Skin Optics','engine/Modules/SkinOptics.cpp','Lighting character BRDF','Characters > Skin','Fidelity; material-specific')
        'Sky Continuity'=@('Sky Continuity','engine/Modules/SkyContinuity.cpp','Sun/moon/shadow state','Atmosphere > Sky','Artistic/correctness; time-of-day state')
        'Sky Veil'=@('Sky Veil','engine/Modules/SkyVeil.cpp','Volumetric cloud shaders','Atmosphere > Clouds','Fidelity/performance; temporal volumetrics')
        'SkyBounce'=@('Sky Bounce','engine/Modules/SkyBounce.cpp','SkyBounce probe volume + Lighting','Lighting > Sky Bounce','Fidelity/performance; spatial cache')
        'Strand Shading'=@('Strand Shading','engine/Modules/StrandShading.cpp','Lighting hair path','Characters > Hair','Fidelity/performance; specular stability')
        'Terrain Detail'=@('Terrain Detail','engine/Modules/TerrainDetail.cpp','Terrain/LOD shader','Terrain','Correctness/fidelity')
        'Terrain Field'=@('Terrain Field','engine/Modules/TerrainField.cpp','Shared terrain data','Automatic','Correctness/data service')
        'Terrain Occlusion'=@('Terrain Occlusion','engine/Modules/TerrainOcclusion.cpp','TerrainOcclusion compute','Terrain > Shadows','Fidelity/performance; amortized updates')
        'Terrain Seam'=@('Terrain Seam','engine/Modules/TerrainSeam.cpp','Terrain seam/depth hooks','Automatic','Correctness service')
        'Thin Surface'=@('Thin Surface','engine/Modules/ThinSurface.cpp','Lighting alpha/thin-surface path','Materials > Thin Surfaces','Fidelity; alpha stability')
        'Tissue Diffusion'=@('Tissue Diffusion','engine/Modules/TissueDiffusion.cpp','SSS kernels + Lighting','Characters > Tissue Diffusion','Fidelity/performance; screen-space diffusion')
        'Volume Occlusion'=@('Volume Occlusion','engine/Modules/VolumeOcclusion.cpp','Volumetric shadow compute','Lighting > Volumes','Fidelity/performance')
        'Water Optics'=@('Water Optics','engine/Modules/WaterOptics.cpp','Water.hlsl + enhanced SSR/caustics','Water','Fidelity/performance; screen-space reflection stability')
        'Waterbody'=@('Waterbody','engine/Modules/Waterbody.cpp','Water mesh/cache path','Water','CPU/geometry optimization')
        'Window Life'=@('WindowLife','engine/Modules/WindowLife.cpp','Lighting.hlsl + WindowLife.hlsli','Windows','Fidelity/performance; view-dependent parallax/transparency')
        'World Probes'=@('World Probes','engine/Modules/WorldProbes.cpp','Cubemap/SSR environment resources','Lighting > World Probes','Fidelity/performance; cached environment history')
        'Version'=@('Configuration schema','engine/State.cpp','Loader migration','Automatic','Schema only')
    }
    if ($map.ContainsKey($TopLevel)) { return $map[$TopLevel] }
    return @($TopLevel,'active module source','module shader/runtime consumer','Advanced','Requires source-specific interpretation')
}

function Get-Range([string]$Path, [object]$Raw) {
    if ($Raw -is [bool]) { return 'false/true' }
    if ($Path -match '^Menu\..*Quality$') { return '0..3 (Low..Ultra)' }
    if ($Path -match 'EffectQuality$') { return '0..3' }
    if ($Path -match 'upscaleMethod(NoDLSS)?$') { return '0..3 (None/TAA/FSR/DLSS)' }
    if ($Path -match 'qualityMode$') { return '0..4 (Native AA..Ultra Performance)' }
    if ($Path -match 'presetDLSS$') { return '0..5 (Default/J/K/L/M/F)' }
    if ($Path -match 'MaximumAffectedNPCs$') { return '1..64' }
    if ($Path -match 'PhotoFinishQualityPreset$') { return '0..3' }
    if ($Path -match 'PhotoFinishScale$') { return '1/2/4' }
    if ($Path -match 'PhotoFinishTemporalSamples$') { return '1/4/8/16/24' }
    if ($Path -match 'Linear Light Core\..*Gamma$') { return '0.1..3.0' }
    if ($Path -match 'Linear Light Core\..*Mult$') { return '0..10' }
    if ($Path -match 'Ground Response\.GeometryRenderDistance$') { return '384..4096' }
    if ($Path -match 'Ground Response\.GeometryFadeStart$') { return '256..4000' }
    if ($Path -match 'Ground Response\.GeometryTessellationNear$') { return '1..16' }
    if ($Path -match 'Ground Response\.GeometryTessellationFar$') { return '1..10' }
    if ($Path -match 'Contact Shadows\.SampleCount$') { return '1..4 (quality contract uses 1..2)' }
    if ($Path -match 'Atmosphere\.volumetricGridPixelSize$') { return '8..64 (lower is higher fidelity/cost)' }
    if ($Path -match 'Atmosphere\.volumetricGridSizeZ$') { return '16..128' }
    if ($Path -match 'Water Optics\.SSRDistanceScale$') { return '0.25..1.5' }
    if ($Path -match 'Water Optics\.SSREdgeFade$') { return '0.25..2.0' }
    if ($Path -match 'Water Optics\.CausticsDispersion$') { return '0..1.5' }
    if ($Path -match 'Material Forge\.SpecularAAStrength$') { return '0..2 (quality contract 0.50..1.34)' }
    if ($Path -match 'Material Forge\.GGXMultiScatterStrength$') { return '0..2 (quality contract 0.55..1.0)' }
    if ($Raw -is [System.Collections.IList] -and $Raw -isnot [string]) { return 'fixed-shape serialized array' }
    if ($Path -match '\.(pad\d|Magic|Version)$') { return 'internal ABI/schema; do not edit' }
    return 'source/UI constrained; preserved unless listed in preset diff'
}

function Get-DiffReason([string]$Path, [string]$Preset) {
    if ($Path -match '^Menu\..*Quality$|^Menu\.RendererQuality$') { return 'Keeps the visible quality contract synchronized with the module values.' }
    if ($Path -match '^ImageReconstruction\.') { return 'Selects the profile reconstruction strategy without enabling frame generation.' }
    if ($Path -match '^Linear Light Core\.') { return 'Calibrates the global gamma/energy conversion as one coherent linear-lighting stack.' }
    if ($Path -match '^Hybrid GI\.|^Contact Shadows\.|^Light Volumes\.') { return 'Applies the renderer-owned Lighting quality/stability contract.' }
    if ($Path -match '^Material Forge\.|^Material Layers\.') { return 'Applies the renderer-owned material/POM/BRDF quality contract.' }
    if ($Path -match '^Atmosphere\.') { return 'Scales volumetric froxel quality or disables optional volumetrics for Minimum.' }
    if ($Path -match '^Water Optics\.') { return 'Scales the real SSR/caustic workload and stability controls.' }
    if ($Path -match '^Ground Response\.|^Terrain Detail\.') { return 'Scales or safely gates geometric ground simulation and tessellation.' }
    if ($Path -match '^Skin Optics\.|^Tissue Diffusion\.|^Strand Shading\.|^Actor Surface Effects\.') { return 'Scales character material/SSS/surface-effect workload.' }
    if ($Path -match '^Camera Suite\.') { return 'Keeps post-processing coherent with the preset goal rather than maximizing intensity.' }
    if ($Path -match '^Pixel Capture\.') { return 'Raises offline screenshot reconstruction only; normal gameplay cost is unchanged.' }
    if ($Path -match '^Rain Response\.|^Sky Veil\.|^Window Life\.|^Foliage Dynamics\.') { return 'Enables or safely gates an optional high-cost visual module for this profile.' }
    return "Intentional $Preset profile adjustment."
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$referencePath = Join-Path $OutputDirectory 'PIXL_Baseline_REFERENCE.json'
[IO.File]::WriteAllBytes($referencePath, $baselineBytes)

$complete = Read-Config
Apply-QualityContract $complete 2
Set-DLAA $complete
$complete.'Actor Surface Effects'.MaximumAffectedNPCs = 32
$complete.'Actor Surface Effects'.EffectDistance = 4000.0
$complete.'Camera Suite'.bloomStrength = 0.30
$complete.'Camera Suite'.bloomThreshold = 0.62
$complete.'Camera Suite'.bloomRadius = 0.90
Write-Config $complete (Join-Path $OutputDirectory 'PIXL_Complete.json')

$extreme = Read-Config
Apply-QualityContract $extreme 3
Set-DLAA $extreme
$extreme.'Actor Surface Effects'.MaximumAffectedNPCs = 48
$extreme.'Actor Surface Effects'.EffectDistance = 4800.0
$extreme.'Pixel Capture'.PhotoFinishQualityPreset = 3
$extreme.'Pixel Capture'.PhotoFinishScale = 4
$extreme.'Pixel Capture'.PhotoFinishTemporalSamples = 24
$extreme.'Pixel Capture'.PhotoFinishDetailStrength = 0.72
$extreme.'Camera Suite'.bloomStrength = 0.28
$extreme.'Camera Suite'.bloomThreshold = 0.68
$extreme.'Camera Suite'.bloomRadius = 0.85
Write-Config $extreme (Join-Path $OutputDirectory 'PIXL_Extreme_Fidelity.json')

$minimum = Read-Config
Apply-QualityContract $minimum 0
$minimum.ImageReconstruction.upscaleMethod = 2
$minimum.ImageReconstruction.upscaleMethodNoDLSS = 2
$minimum.ImageReconstruction.qualityMode = 4
$minimum.ImageReconstruction.presetDLSS = 0
$minimum.ImageReconstruction.sharpnessEnabledDLSS = $false
$minimum.ImageReconstruction.sharpnessDLSS = 0.0
$minimum.ImageReconstruction.sharpnessFSR = 0.20
$minimum.ImageReconstruction.frameGenerationMode = 0
$minimum.ImageReconstruction.frameGenerationForceEnable = 0
$minimum.'Hybrid GI'.Enabled = $false
$minimum.'Hybrid GI'.EnableGI = $false
$minimum.'Hybrid GI'.EnableExperimentalSpecularGI = $false
$minimum.'Hybrid GI'.EnableWorldCache = $false
$minimum.'Hybrid GI'.EnableWorldCacheSecondBounce = $false
$minimum.'Hybrid GI'.EnableVoxelReflections = $false
$minimum.'Contact Shadows'.Enable = 0
$minimum.'Light Volumes'.ExteriorEnabled = $false
$minimum.'Light Volumes'.InteriorEnabled = $false
$minimum.'Material Forge'.EnableGGXMultiScatter = 0
$minimum.'Material Forge'.EnableLocalContactShadows = 0
$minimum.'Material Layers'.EnableParallax = 0
$minimum.'Material Layers'.EnableHeightBlending = 0
$minimum.'Material Layers'.EnableShadows = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableDetailReconstruction = 0
$minimum.'Material Layers'.'PIXL Tuning'.DetailQuality = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableObjectAutoPOM = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableTerrainAutoPOM = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableAutoPOMSelfShadows = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableTerrainSelfShadows = 0
$minimum.'Material Layers'.'PIXL Tuning'.EnableTerrainVirtualDepth = 0
$minimum.Atmosphere.volumetricFogEnabled = 0
$minimum.Atmosphere.useWorldProbes = 0
$minimum.'Sky Veil'.EnableVolumetricClouds = 0
$minimum.'Water Optics'.EnableEnhancedSSR = 0
$minimum.'Water Optics'.EnableEnhancedCaustics = 0
$minimum.'Ground Response'.EnableGroundResponse = $false
$minimum.'Ground Response'.EnableDeformableGround = $false
$minimum.'Ground Response'.EnableGeometricSnow = $false
$minimum.'Ground Response'.EnableSnowDeformation = $false
$minimum.'Ground Response'.EnableMudDeformation = $false
$minimum.'Ground Response'.EnableWeatherSnowAccumulation = $false
$minimum.'Actor Surface Effects'.Enable = $false
$minimum.'Rain Response'.EnableRainResponse = 0
$minimum.'Rain Response'.EnableRainParticleEnhancement = 0
$minimum.'Rain Response'.EnableRaindropFx = 0
$minimum.'Rain Response'.EnableRipples = 0
$minimum.'Rain Response'.EnableSplashes = 0
$minimum.'Rain Response'.SnowPrecipitation.Enable = $false
$minimum.'Foliage Dynamics'.EnableEnhancedVegetation = 0
$minimum.'Foliage Dynamics'.EnableEnhancedWind = 0
$minimum.'Foliage Dynamics'.OverrideComplexGrassSettings = 0
$minimum.'Skin Optics'.EnableSkin = $false
$minimum.'Skin Optics'.EnableSkinDetail = $false
$minimum.'Skin Optics'.UseSSS = $false
$minimum.'Tissue Diffusion'.EnableCharacterLighting = 0
$minimum.'Tissue Diffusion'.BurleySamples = 8
$minimum.'Strand Shading'.Enabled = 0
$minimum.'Window Life'.EnableWindowLife = $false
$minimum.'World Probes'.EnabledSSR = 0
$minimum.'Camera Suite'.enablePhysicalCamera = $false
$minimum.'Camera Suite'.enableBloom = $false
$minimum.'Camera Suite'.enableModernMotionBlur = $false
$minimum.'Camera Suite'.enableStormglass = $false
$minimum.'Camera Suite'.enableSubmergedOptics = $false
$minimum.'Camera Suite'.enableColdLens = $false
$minimum.'Camera Suite'.enableElementalDamageLens = $false
Write-Config $minimum (Join-Path $OutputDirectory 'PIXL_Minimum.json')

$linear = Read-Config
Apply-QualityContract $linear 2
$linear.ImageReconstruction.upscaleMethod = 1
$linear.ImageReconstruction.upscaleMethodNoDLSS = 2
$linear.ImageReconstruction.qualityMode = 1
$linear.ImageReconstruction.frameGenerationMode = 0
$linear.ImageReconstruction.frameGenerationForceEnable = 0
$ll = $linear.'Linear Light Core'
$ll.enableLinearLightCore = 1
$ll.lightGamma = 1.15
$ll.colorGamma = 1.35
$ll.emitColorGamma = 1.10
$ll.glowmapGamma = 1.10
$ll.ambientGamma = 1.10
$ll.fogGamma = 1.10
$ll.fogAlphaGamma = 1.20
$ll.effectGamma = 1.10
$ll.effectAlphaGamma = 1.10
$ll.skyGamma = 1.10
$ll.waterGamma = 1.10
$ll.vlGamma = 1.10
$ll.vanillaDiffuseColorMult = 1.05
$ll.directionalLightMult = 1.10
$ll.pointLightMult = 1.10
$ll.ambientMult = 1.35
$ll.emitColorMult = 1.00
$ll.glowmapMult = 0.85
$ll.effectLightingMult = 1.00
$ll.membraneEffectMult = 1.00
$ll.bloodEffectMult = 1.00
$ll.projectedEffectMult = 1.00
$ll.deferredEffectMult = 1.00
$ll.otherEffectMult = 1.00
$linear.'Hybrid GI'.GIStrength = 1.05
$linear.'Hybrid GI'.GISaturation = 0.75
$linear.'Hybrid GI'.AOPower = 1.10
$linear.'Ambient Probe'.EnvironmentProbeScale = 1.05
$linear.'Ambient Probe'.SkyProbeScale = 1.15
$linear.'Contact Shadows'.ShadowContrast = 2.50
$linear.'Camera Suite'.cameraExposureCompensationEV = -2.60
$linear.'Camera Suite'.cameraMinExposureEV = -6.0
$linear.'Camera Suite'.cameraMaxExposureEV = 3.0
$linear.'Camera Suite'.cameraHighlightProtection = 0.90
$linear.'Camera Suite'.cameraShadowDetail = 0.25
$linear.'Camera Suite'.cameraLocalExposure = 0.15
$linear.'Camera Suite'.bloomStrength = 0.25
$linear.'Camera Suite'.bloomThreshold = 0.70
$linear.'Camera Suite'.bloomRadius = 0.85
Write-Config $linear (Join-Path $OutputDirectory 'PIXL_Linear_Diffuse.json')

$profiles = [ordered]@{
    'PIXL_Complete' = $complete
    'PIXL_Extreme_Fidelity' = $extreme
    'PIXL_Minimum' = $minimum
    'PIXL_Linear_Diffuse' = $linear
}

$baseline = Read-Config
$baselineFlat = @{}
foreach ($row in @(Flatten-Config $baseline)) { $baselineFlat[$row.Path] = $row }

$diffLines = New-Object Collections.Generic.List[string]
$diffLines.Add('# PIXL preset differences')
$diffLines.Add('')
$diffLines.Add('Only settings that differ from the untouched live baseline are listed.')
foreach ($entry in $profiles.GetEnumerator()) {
    $diffLines.Add('')
    $diffLines.Add("## $($entry.Key)")
    $diffLines.Add('')
    $diffLines.Add('| Setting | Baseline | New Value | Module | Reason |')
    $diffLines.Add('| --- | --- | --- | --- | --- |')
    foreach ($row in @(Flatten-Config $entry.Value) | Sort-Object Path) {
        $old = $baselineFlat[$row.Path]
        if ($null -eq $old -or -not (Test-ConfigValueEqual $old.Raw $row.Raw)) {
            $top = $row.Path.Split('.')[0]
            $module = (Get-ModuleInfo $top)[0]
            $oldValue = if ($null -eq $old) { '(missing)' } else { $old.Value }
            $diffLines.Add("| ``$($row.Path)`` | ``$oldValue`` | ``$($row.Value)`` | $module | $(Get-DiffReason $row.Path $entry.Key) |")
        }
    }
}
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'PRESET_DIFFS.md'), $diffLines, $utf8NoBom)

$mapLines = New-Object Collections.Generic.List[string]
$mapLines.Add('# PIXL configuration map')
$mapLines.Add('')
$mapLines.Add("Baseline: ``$baselineResolved``  ")
$mapLines.Add("SHA-256: ``$baselineHashBefore``  ")
$mapLines.Add('Generated from every serialized leaf/array in the active live configuration. Exact artistic fields are intentionally described as non-monotonic; only source-verified workload controls are changed by the presets.')
$mapLines.Add('')
$mapLines.Add('| Config key | Baseline | GUI / module | C++ owner | GPU / runtime consumer | Valid range / schema | Quality direction and interactions |')
$mapLines.Add('| --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in @(Flatten-Config $baseline) | Sort-Object Path) {
    $top = $row.Path.Split('.')[0]
    $info = Get-ModuleInfo $top
    $mapLines.Add("| ``$($row.Path)`` | ``$($row.Value)`` | $($info[3]) / $($info[0]) | ``$($info[1])`` | $($info[2]) | $(Get-Range $row.Path $row.Raw) | $($info[4]) |")
}
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'CONFIGURATION_MAP.md'), $mapLines, $utf8NoBom)

$artifactLines = New-Object Collections.Generic.List[string]
$artifactLines.Add('| File | Changed settings | SHA-256 |')
$artifactLines.Add('| --- | ---: | --- |')
$artifactLines.Add("| PIXL_Baseline_REFERENCE.json | 0 | ``$((Get-FileHash -Algorithm SHA256 -LiteralPath $referencePath).Hash)`` |")
foreach ($entry in $profiles.GetEnumerator()) {
    $changedCount = 0
    foreach ($row in @(Flatten-Config $entry.Value)) {
        $old = $baselineFlat[$row.Path]
        if ($null -eq $old -or -not (Test-ConfigValueEqual $old.Raw $row.Raw)) { $changedCount++ }
    }
    $profilePath = Join-Path $OutputDirectory ($entry.Key + '.json')
    $artifactLines.Add("| $($entry.Key).json | $changedCount | ``$((Get-FileHash -Algorithm SHA256 -LiteralPath $profilePath).Hash)`` |")
}
$artifactTable = $artifactLines -join [Environment]::NewLine

$report = @"
# PIXL Renderer experimental preset report

## Baseline

- Source config: ``$baselineResolved``
- Identification: ``Util::PathHelpers::GetSettingsUserPath()`` resolves this exact file; ``State::Load`` merges it over ``RendererDefaults.json`` and every active ``RenderModule::LoadSettings`` consumes its named object. The current beta staging config is byte-identical.
- Format/schema: UTF-8 JSON, root object keyed by core settings and each module's ``GetName()``.
- Baseline SHA-256: ``$baselineHashBefore``
- Reference copy SHA-256: ``$((Get-FileHash -Algorithm SHA256 -LiteralPath $referencePath).Hash)``
- Baseline modified: **NO**
- Baseline identity: current owner-tested Medium quality contracts, TAA, frame generation Off.

The shipped ``SettingsDefault.json`` and ``PIXL-Renderer-Live-Tested.json`` are byte-identical to each other but not to the current live baseline. They are defaults/package inputs, not the active user state.

### Generated artifacts

$artifactTable

To test a profile, close Skyrim, retain the untouched reference in this directory, and copy the chosen profile to the live ``UserGraphics.json`` path. Each profile is a complete independent configuration, not a partial override. Returning the byte-identical reference restores this captured baseline.

## PIXL Extreme Fidelity

This is the reference/screenshot profile. It applies the source-defined Ultra contracts to Lighting, Materials, Atmosphere, Water, Terrain/Vegetation, Characters and Camera; uses DLSS Native AA (DLAA) with model preset F; retains frame generation Off; expands Actor Surface Effects' bounded nearby-NPC budget; and selects the maximum validated Photo Finish plan (4x, 24 real jittered samples). Experimental voxel reflections remain Off because their instability is not made higher quality merely by enabling them.

Bloom is deliberately reduced and its threshold raised rather than maximized. Strong broad bloom would erase local contrast and contaminate reconstruction. Artistic GI, fog, wetness, snow, vegetation and skin values otherwise remain the owner's accepted baseline.

Estimated relative GPU impact: **extreme** during Photo Finish; **high** during gameplay.

## PIXL Complete

This is the recommended coherent profile. It uses High quality contracts--the point before the last expensive ray/probe/tessellation increments--while keeping every production-ready visual module enabled. DLAA preset F provides a clean native-resolution temporal reference without frame-generation ambiguity. Actor Surface Effects receives a moderate 32-NPC/4000-unit budget. Bloom is tightened to protect image clarity.

Deliberately below maximum: GI rays/cache samples, POM steps, volumetric grid depth, water trace range, geometric-ground tessellation and SSS samples. Each is one tier below Ultra because its final increment has disproportionate cost. Reflection/voxel experiments, bodycam and frame generation are not force-enabled.

Estimated relative GPU impact: **high** versus Minimum, **moderate-to-high** versus the Medium baseline.

## PIXL Minimum

This profile applies the Low workload contract, then safely gates optional expensive shading/simulation branches without disabling renderer infrastructure at boot. Hybrid GI, contact shadows, light volumes, volumetric fog/clouds, enhanced water SSR/caustics, geometric ground response, actor accumulation, enhanced precipitation, WindowLife, enhanced foliage/wind, skin SSS/detail, strand shading and camera finishing effects are disabled through their normal runtime settings. Material Forge and shader replacement infrastructure remain loaded. FSR Ultra Performance supplies the largest practical resolution delta; frame generation remains Off.

Expected differences: flatter/less indirect lighting, no fake interiors or dynamic ground/actor accumulation, simpler water/vegetation/characters/weather, lower volumetric depth and substantially softer reconstruction. This is a scalability floor, not the intended PIXL look.

Estimated relative GPU impact: **large reduction expected**; exact timing requires live profiling.

## PIXL Linear Diffuse

Linear Light Core is a global color/energy conversion system, not a single diffuse checkbox. ``Color.hlsli`` changes diffuse decode, light color conversion, Lambert normalization, emissive/glow, ambient, fog, sky, water, effects and irradiance conventions across Lighting, Grass, Water and Effect shaders. Material Forge's linear branch also removes the legacy sRGB/Pi compensation used when the module is Off.

That explains why enabling it over an ordinary preset can look much too dark or over-contrasted: the module changes several energy conventions at once while the baseline exposure, ambient probe, GI, AO, bloom and per-domain gamma/multipliers were tuned for the Off path. Material Forge textures are already hardware-decoded/handled separately, so forcing every remaining domain to a textbook 2.2 exponent would double-darken parts of PIXL's mixed Skyrim inputs. The profile therefore uses conservative near-linear exponents, restrained 1.10 direct/local multipliers, 1.35 ambient support, reduced GI/AO, higher highlight protection, a less-negative exposure compensation and tighter bloom. TAA is retained to isolate lighting behavior from DLSS model differences.

No disconnected configuration binding was found. One implementation limitation remains: ``isDirLightLinear`` is a fixed false member in the current code, so all directional light input is treated as non-linear; this matches the current Skyrim input assumption but is not runtime-detected. Configuration can calibrate it, but cannot prove every modded light/weather source uses the same encoding. This profile is therefore a serious visual reference experiment, not a newly approved default.

Estimated relative GPU impact: **moderate-to-high**, close to the High contract; Linear Light Core itself is low overhead.

## Validation performed

- All five JSON files parse through PowerShell's standards-compliant JSON parser.
- Every generated profile has the same root schema and setting-path set as the live baseline.
- No duplicate JSON member names were emitted.
- Profile-controlled values are within the actual loader/UI ranges documented in source.
- DLSS/FSR/TAA enums, DLSS preset F, Photo Finish limits and actor budgets match loader clamps.
- Frame generation is Off in every profile to keep A/B captures deterministic.
- The baseline and reference hashes are identical; the source baseline hash is rechecked after generation.
- Internal ``Magic``, ``Version`` and padding fields are untouched.
- Key ordering and array shapes match the baseline recursively; all numeric values are finite.

## Disconnected / compatibility-only settings

- Legacy Camera Suite enhanced-DOF values remain serialized but ``LoadSettings`` forces ``enableEnhancedDepthOfField=false``; they are retained for schema compatibility and not used as preset controls.
- Pixel Capture's legacy Photo Lens DOF fields are likewise read/clamped but forced disabled.
- ``Horizon Blend``, ``Natural Lighting``, ``Terrain Field`` and ``Volume Occlusion`` serialize ``null`` because they are automatic/no-public-setting services.
- Several legacy ``Disable at Boot`` aliases are round-tripped for migration compatibility. PIXL-managed correctness services are forced enabled by ``State::Load``.
- ``Menu.*Quality`` fields select/describe the coordinated contracts; module JSON contains the actual runtime settings. Both are synchronized in these profiles.

## Compact A/B test plan

Use the same save, resolution, weather/time commands, camera path and reconstruction warm-up. Test in this order: **Baseline -> PIXL Complete -> Extreme Fidelity -> Linear Diffuse -> Minimum**.

1. Sunny exterior: stone, foliage, shadow edges and camera rotation.
2. Overcast exterior: ambient/GI balance, fog, wetness and no radiance pumping.
3. Sunrise/sunset: sky continuity, WindowLife glass, reflections and exposure adaptation.
4. Snow: bright and shaded snow while strafing/rotating; ground deformation and precipitation.
5. Forest: alpha-tested foliage, wind motion, grass specular and temporal shimmer.
6. Water: grazing reflections, shore/caustics and camera motion.
7. Interior: candles/point lights, contact shadows, WindowLife and readable dark materials.
8. Dialogue close-up: skin, eyes, hair, SSS and actor accumulation.
9. Wet weather: rain, runoff, puddles, wet PBR and reconstruction trails.
10. High-motion benchmark fly-through: ghosting, disocclusion, volumetrics and frame pacing.

Allow temporal histories to settle before screenshots. For Linear Diffuse, compare noon exterior, candle interior, snow and skin first; those reveal color-space/energy mistakes fastest. Use Pulse Profiler only for measured comparisons and do not compare Photo Finish cost with normal gameplay.
"@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'PRESET_REPORT.md'), $report, $utf8NoBom)

$baselineHashAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $baselineResolved).Hash
if ($baselineHashAfter -ne $baselineHashBefore) { throw 'Baseline config changed during generation.' }
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $referencePath).Hash -ne $baselineHashBefore) { throw 'Reference copy is not byte-identical.' }

$expectedPaths = @($baselineFlat.Keys | Sort-Object)
foreach ($entry in $profiles.GetEnumerator()) {
    $path = Join-Path $OutputDirectory ($entry.Key + '.json')
    $parsed = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
    $actualPaths = @((Flatten-Config $parsed).Path | Sort-Object)
    if (Compare-Object $expectedPaths $actualPaths) {
        throw "Schema mismatch in $path"
    }
}

Write-Host "Generated PIXL presets in $OutputDirectory"
Write-Host "Baseline preserved: $baselineHashAfter"
