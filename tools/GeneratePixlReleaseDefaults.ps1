[CmdletBinding()]
param(
    [string]$BaselinePath = 'H:\The Elder Scrolls - Skyrim - Special Edition\Data\SKSE\Plugins\PIXL\Config\UserGraphics.json'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$distribution = Join-Path $repoRoot 'distribution\SKSE\Plugins\PIXLRenderer'
$presetDirectory = Join-Path $distribution 'Presets'
$baselineResolved = (Resolve-Path -LiteralPath $BaselinePath).Path
$baselineBytes = [IO.File]::ReadAllBytes($baselineResolved)
$baselineHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $baselineResolved).Hash
$utf8NoBom = New-Object Text.UTF8Encoding($false)

New-Item -ItemType Directory -Force -Path $presetDirectory | Out-Null

function Read-Baseline {
    Get-Content -Raw -LiteralPath $baselineResolved | ConvertFrom-Json
}

function Write-Config([object]$Config, [string]$Path) {
    $json = $Config | ConvertTo-Json -Depth 100
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8NoBom)
    $null = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Remove-RetiredCompatibilityKeys([object]$Config) {
    # UserGraphics may retain disabled legacy keys so older profiles can round-trip,
    # but public release presets must contain only current PIXL module identities.
    $retiredPbrKey = 'True' + 'PBR'
    if ($Config.PSObject.Properties.Name -contains 'Modules') {
        $Config.Modules.PSObject.Properties.Remove($retiredPbrKey)
    }
    if ($Config.PSObject.Properties.Name -contains 'Disable at Boot') {
        $Config.'Disable at Boot'.PSObject.Properties.Remove($retiredPbrKey)
    }
}

function Set-MenuTier([object]$Config, [int]$Tier) {
    foreach ($name in @(
        'RendererQuality', 'LightingQuality', 'MaterialsQuality',
        'AtmosphereQuality', 'WaterQuality', 'TerrainVegetationQuality',
        'CharactersQuality', 'CameraQuality')) {
        $Config.Menu.$name = $Tier
    }
}

function Apply-MediumContract([object]$Config) {
    # Keep the approved live image and reduce only real workload controls. These
    # values mirror engine/Renderer/QualityProfiles.cpp exactly.
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
    $gi.NumSlices = 4
    $gi.NumSteps = 8
    $gi.WorldCacheSampleCount = 4
    $gi.WorldCacheTraceSteps = 3
    $gi.WorldCacheInjectionStride = 4
    $gi.EnableWorldCacheSecondBounce = $true
    $gi.ReflectionSteps = 24
    $gi.MaxAccumFrames = 20
    $gi.BlurRadius = 2.5
    $gi.RadianceFireflyClamp = 5.0
    $gi.ReflectionFireflyClamp = 6.0
    $gi.WorldCacheTemporalResponse = 0.10
    $Config.'Contact Shadows'.SampleCount = 2
    $Config.'Material Forge'.LocalContactShadowLightCount = 1
    $Config.'Light Volumes'.ExteriorQuality = 1
    $Config.'Light Volumes'.InteriorQuality = 1

    $forge = $Config.'Material Forge'
    $forge.EnableSpecularAA = 1
    $forge.SpecularAAStrength = 0.75
    $forge.EnableGGXMultiScatter = 1
    $forge.GGXMultiScatterStrength = 0.75

    $layers = $Config.'Material Layers'
    $layers.EnableComplexMaterial = 1
    $layers.EnableParallax = 1
    $layers.EnableHeightBlending = 0
    $layers.EnableShadows = 1
    $tuning = $layers.'PIXL Tuning'
    $tuning.ObjectNearSteps = 6
    $tuning.ObjectMaxSteps = 12
    $tuning.ObjectRefinementSteps = 4
    $tuning.TerrainNearSteps = 6
    $tuning.TerrainMaxSteps = 14
    $tuning.TerrainRefinementSteps = 4
    $tuning.EnableDetailReconstruction = 1
    $tuning.DetailQuality = 1

    $Config.Atmosphere.volumetricGridPixelSize = 36
    $Config.Atmosphere.volumetricGridSizeZ = 40
    $Config.Atmosphere.volumetricHistoryMissSampleCount = 2

    $water = $Config.'Water Optics'
    $water.EnableEnhancedSSR = 1
    $water.EnableEnhancedCaustics = 1
    $water.SSRDistanceScale = 0.90
    $water.SSREdgeFade = 1.00
    $water.CausticsDispersion = 0.45

    # Ground Response's authored coverage/depth/distance/material behavior is
    # immutable across release tiers. Only geometric subdivision is scalable.
    $ground = $Config.'Ground Response'
    $ground.GeometryTessellationNear = 7.0
    $ground.GeometryTessellationFar = 2.0
    $Config.'Terrain Detail'.enableLODTerrainTilingFix = 1

    $skin = $Config.'Skin Optics'
    $skin.EnableSkin = $true
    $skin.EnableSkinDetail = $true
    $skin.UseSSS = $true
    $Config.'Tissue Diffusion'.BurleySamples = 12
    $Config.'Strand Shading'.Enabled = 1
    $Config.'Strand Shading'.HairMode = 0
    $Config.'Strand Shading'.EnableSelfShadow = 1
    $Config.'Actor Surface Effects'.EffectQuality = 1

    Set-MenuTier $Config 1

    # Hardware-neutral release default: FSR 3.1 Balanced, no frame generation.
    $reconstruction = $Config.ImageReconstruction
    $reconstruction.upscaleMethod = 2
    $reconstruction.upscaleMethodNoDLSS = 2
    $reconstruction.qualityMode = 2
    $reconstruction.frameGenerationMode = 0
    $reconstruction.frameGenerationForceEnable = 0
    $reconstruction.sharpnessFSR = 0.0
}

function Flatten([object]$Node, [string]$Path = '') {
    if ($Node -is [pscustomobject]) {
        foreach ($property in $Node.PSObject.Properties) {
            $child = if ($Path) { "$Path.$($property.Name)" } else { $property.Name }
            Flatten $property.Value $child
        }
        return
    }
    if ($Node -is [System.Collections.IList] -and $Node -isnot [string]) {
        [pscustomobject]@{ Path=$Path; Value=($Node | ConvertTo-Json -Compress -Depth 20) }
        return
    }
    [pscustomobject]@{ Path=$Path; Value=($Node | ConvertTo-Json -Compress) }
}

$ultraPath = Join-Path $presetDirectory 'PIXL-Renderer-Ultra.json'
$ultra = Read-Baseline
Remove-RetiredCompatibilityKeys $ultra
Write-Config $ultra $ultraPath

$medium = Read-Baseline
Remove-RetiredCompatibilityKeys $medium
Apply-MediumContract $medium
$defaultPath = Join-Path $distribution 'SettingsDefault.json'
$mediumPath = Join-Path $presetDirectory 'PIXL-Renderer-Medium.json'
$liveTestedPath = Join-Path $presetDirectory 'PIXL-Renderer-Live-Tested.json'
Write-Config $medium $defaultPath
Write-Config $medium $mediumPath
# The live-tested/golden profile remains the user's approved Ultra image. Only
# a fresh installation's default workload is Medium.
Write-Config $ultra $liveTestedPath

# Ensure no Ground Response visual/behavioral control was accidentally changed.
$baseline = Read-Baseline
$groundBefore = @(Flatten $baseline.'Ground Response' 'Ground Response')
$groundAfter = @(Flatten $medium.'Ground Response' 'Ground Response')
$groundBeforeMap = @{}; $groundBefore | ForEach-Object { $groundBeforeMap[$_.Path] = $_.Value }
$groundChanges = @($groundAfter | Where-Object { $groundBeforeMap[$_.Path] -ne $_.Value } | ForEach-Object Path)
$allowedGroundChanges = @(
    'Ground Response.GeometryTessellationNear',
    'Ground Response.GeometryTessellationFar'
)
$unexpectedGround = @($groundChanges | Where-Object { $_ -notin $allowedGroundChanges })
if ($unexpectedGround.Count -ne 0) {
    throw "Unexpected Ground Response changes: $($unexpectedGround -join ', ')"
}

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $baselineResolved).Hash -ne $baselineHash) {
    throw 'The live baseline was modified while generating release defaults.'
}

[pscustomobject]@{
    Baseline = $baselineResolved
    BaselineSHA256 = $baselineHash
    Ultra = $ultraPath
    MediumDefault = $defaultPath
    MediumPreset = $mediumPath
    GroundChanges = ($groundChanges -join ', ')
} | Format-List
