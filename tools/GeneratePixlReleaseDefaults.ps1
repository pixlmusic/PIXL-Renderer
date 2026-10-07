[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$BaselinePath,
    [switch]$PresetsOnly
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

function Apply-QualityContract([object]$Config, [int]$Tier) {
    # Windows PowerShell 5.1 targets a framework without Math.Clamp.
    $Tier = [Math]::Min(3, [Math]::Max(0, $Tier))
    # These arrays mirror engine/Renderer/QualityProfiles.cpp. Keep artistic
    # appearance controls out of this function: profiles own workload only.
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
    $gi.NumSlices = @(2, 3, 5, 8)[$Tier]
    $gi.NumSteps = @(4, 7, 10, 16)[$Tier]
    $gi.WorldCacheSampleCount = @(2, 3, 6, 8)[$Tier]
    $gi.WorldCacheTraceSteps = @(2, 3, 3, 5)[$Tier]
    $gi.WorldCacheInjectionStride = @(8, 5, 3, 1)[$Tier]
    $gi.EnableWorldCacheSecondBounce = $Tier -ge 1
    $gi.ReflectionSteps = @(12, 20, 36, 56)[$Tier]
    $gi.MaxAccumFrames = @(16, 24, 24, 36)[$Tier]
    $gi.BlurRadius = @(3.0, 2.5, 2.0, 1.6)[$Tier]
    $gi.RadianceFireflyClamp = @(4.0, 5.0, 8.0, 12.0)[$Tier]
    $gi.ReflectionFireflyClamp = @(4.0, 6.0, 10.0, 16.0)[$Tier]
    # The cache should catch up as the player moves without depending on tier.
    $gi.WorldCacheCellSizeFar = 384.0
    # Higher tiers inject more cache samples, so they can use a slower replacement
    # rate for a steadier world-space anchor without sacrificing convergence.
    $gi.WorldCacheTemporalResponse = @(0.08, 0.075, 0.07, 0.06)[$Tier]
    $Config.'Contact Shadows'.SampleCount = @(1, 1, 3, 6)[$Tier]
    $Config.'Contact Shadows'.FalloffStart = @(3072, 3584, 4608, 6144)[$Tier]
    $Config.'Contact Shadows'.FalloffEnd = @(6144, 7168, 8192, 10240)[$Tier]
    $Config.'Material Forge'.LocalContactShadowLightCount = @(1, 1, 2, 2)[$Tier]
    $Config.'Light Volumes'.ExteriorQuality = [Math]::Min($Tier, 2)
    $Config.'Light Volumes'.InteriorQuality = [Math]::Min($Tier, 2)

    $forge = $Config.'Material Forge'
    $forge.EnableSpecularAA = 1
    $forge.SpecularAAStrength = @(0.50, 0.75, 1.34, 1.50)[$Tier]
    $forge.EnableGGXMultiScatter = 1
    $forge.GGXMultiScatterStrength = @(0.55, 0.75, 1.00, 1.00)[$Tier]

    $layers = $Config.'Material Layers'
    $layers.EnableComplexMaterial = 1
    $layers.EnableParallax = [int]($Tier -ge 1)
    $layers.EnableHeightBlending = [int]($Tier -ge 2)
    $layers.EnableShadows = [int]($Tier -ge 1)
    $tuning = $layers.'PIXL Tuning'
    $tuning.ObjectNearSteps = @(4, 6, 12, 24)[$Tier]
    $tuning.ObjectMaxSteps = @(8, 12, 24, 32)[$Tier]
    $tuning.ObjectRefinementSteps = @(4, 4, 8, 12)[$Tier]
    $tuning.TerrainNearSteps = @(4, 6, 10, 30)[$Tier]
    $tuning.TerrainMaxSteps = @(8, 14, 30, 64)[$Tier]
    $tuning.TerrainRefinementSteps = @(4, 4, 8, 16)[$Tier]
    $tuning.EnableDetailReconstruction = [int]($Tier -ge 1)
    $tuning.DetailQuality = @(0, 1, 2, 2)[$Tier]

    $Config.Atmosphere.volumetricGridPixelSize = @(64, 40, 24, 16)[$Tier]
    $Config.Atmosphere.volumetricGridSizeZ = @(24, 36, 64, 80)[$Tier]
    $Config.Atmosphere.volumetricHistoryMissSampleCount = @(1, 2, 4, 8)[$Tier]

    $water = $Config.'Water Optics'
    $water.EnableEnhancedSSR = 1
    $water.EnableEnhancedCaustics = 1
    $water.SSRDistanceScale = @(0.65, 0.90, 1.20, 1.50)[$Tier]
    $water.SSREdgeFade = @(1.35, 1.00, 0.60, 0.25)[$Tier]
    if ($water.PSObject.Properties.Name -contains 'SSRTraceQuality') {
        $water.SSRTraceQuality = [double]$Tier
    } else {
        $water | Add-Member -NotePropertyName SSRTraceQuality -NotePropertyValue ([double]$Tier)
    }

    # Ground Response's authored coverage/depth/distance/material behavior is
    # immutable across release tiers. Only geometric subdivision is scalable.
    $ground = $Config.'Ground Response'
    $ground.GeometryTessellationNear = @(4.0, 7.0, 10.0, 16.0)[$Tier]
    $ground.GeometryTessellationFar = @(1.25, 2.0, 2.5, 6.0)[$Tier]
    $ground.SessionSurfaceHistoryTileBudget = @(48, 96, 192, 512)[$Tier]
    $Config.'Terrain Detail'.enableLODTerrainTilingFix = 1

    $foliage = $Config.'Foliage Optimizer'
    $foliage.EnableOcclusionCulling = $true
    $foliage.EnableMidLOD = $true
    $foliage.EnableFarLOD = $true
    $foliage.MinPixelSize = @(6.0, 3.5, 2.0, 1.0)[$Tier]
    $foliage.FullDetailPixelSize = @(48.0, 28.0, 16.0, 8.0)[$Tier]
    $foliage.MinDensity = @(0.01, 0.02, 0.03, 0.08)[$Tier]
    $foliage.SimpleShadingPixelSize = @(16.0, 9.0, 0.0, 0.0)[$Tier]
    $foliage.MeshCostBias = @(0.85, 0.60, 0.40, 0.0)[$Tier]
    $foliage.CostBiasStartDistance = @(3000.0, 5000.0, 6000.0, 20000.0)[$Tier]
    $foliage.CollisionDistance = @(1024.0, 1536.0, 2048.0, 4096.0)[$Tier]
    $foliage.EnableMeshLOD = $Tier -le 1

    $skin = $Config.'Skin Optics'
    $skin.EnableSkin = $true
    $skin.EnableSkinDetail = $Tier -ge 1
    $skin.UseSSS = $true
    $Config.'Tissue Diffusion'.BurleySamples = @(6, 12, 24, 64)[$Tier]
    $Config.'Strand Shading'.Enabled = 1
    $Config.'Strand Shading'.HairMode = [int]($Tier -ge 2)
    $Config.'Strand Shading'.EnableSelfShadow = [int]($Tier -ge 1)
    $actor = $Config.'Actor Surface Effects'
    $actor.EffectQuality = $Tier
    $actor.MaximumAffectedNPCs = @(8, 16, 48, 64)[$Tier]
    $actor.EffectDistance = @(2000.0, 3200.0, 4800.0, 8000.0)[$Tier]
    if ($actor.PSObject.Properties.Name -contains 'QualityContractVersion') {
        $actor.QualityContractVersion = 2
    } else {
        $actor | Add-Member -NotePropertyName QualityContractVersion -NotePropertyValue 2
    }

    Set-MenuTier $Config $Tier
    # A live/user baseline has already completed onboarding. Public defaults must
    # never inherit that session state or a fresh install will skip Quick Start.
    $Config.Menu.FirstTimeSetupCompleted = $false
    if ($Config.Menu.PSObject.Properties.Name -contains 'QualityContractVersion') {
        $Config.Menu.QualityContractVersion = 2
    } else {
        $Config.Menu | Add-Member -NotePropertyName QualityContractVersion -NotePropertyValue 2
    }

    # Hardware-neutral release default: native PIXL TAA, no sidecar features.
    $reconstruction = $Config.ImageReconstruction
    $reconstruction.upscaleMethod = 1
    $reconstruction.upscaleMethodNoDLSS = 1
    $reconstruction.qualityMode = 0
    $reconstruction.frameGenerationMode = 0
    $reconstruction.frameGenerationForceEnable = 0
    $reconstruction.neuralRenderingEnabled = $false
    $reconstruction.sharpnessFSR = 0.0
    if ($Config.PSObject.Properties.Name -contains 'Pixel Capture') {
        $Config.'Pixel Capture'.PhotoFinishNeuralEnabled = $false
    }
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

$high = Read-Baseline
Remove-RetiredCompatibilityKeys $high
Apply-QualityContract $high 2
$defaultPath = Join-Path $distribution 'SettingsDefault.json'
$enhancedPath = Join-Path $presetDirectory 'PIXL-Renderer-Enhanced.json'
$ultraPath = Join-Path $presetDirectory 'PIXL-Renderer-Ultra.json'
$liveTestedPath = Join-Path $presetDirectory 'PIXL-Renderer-Live-Tested.json'
if (-not $PresetsOnly) {
    Write-Config $high $defaultPath
}
Write-Config $high $enhancedPath
Write-Config $high $ultraPath
Write-Config $high $liveTestedPath

$low = Read-Baseline
Remove-RetiredCompatibilityKeys $low
Apply-QualityContract $low 0
Write-Config $low (Join-Path $presetDirectory 'PIXL-Renderer-Low.json')

$medium = Read-Baseline
Remove-RetiredCompatibilityKeys $medium
Apply-QualityContract $medium 1
Write-Config $medium (Join-Path $presetDirectory 'PIXL-Renderer-Medium.json')

$cinematic = Read-Baseline
Remove-RetiredCompatibilityKeys $cinematic
Apply-QualityContract $cinematic 3
Write-Config $cinematic (Join-Path $presetDirectory 'PIXL-Renderer-Ultimate.json')

# Ensure no Ground Response visual/behavioral control was accidentally changed.
$baseline = Read-Baseline
$groundBefore = @(Flatten $baseline.'Ground Response' 'Ground Response')
$groundAfter = @(Flatten $high.'Ground Response' 'Ground Response')
$groundBeforeMap = @{}; $groundBefore | ForEach-Object { $groundBeforeMap[$_.Path] = $_.Value }
$groundChanges = @($groundAfter | Where-Object { $groundBeforeMap[$_.Path] -ne $_.Value } | ForEach-Object Path)
$allowedGroundChanges = @(
    'Ground Response.GeometryTessellationNear',
    'Ground Response.GeometryTessellationFar',
    'Ground Response.SessionSurfaceHistoryTileBudget',
    'Ground Response.SnowCoverageFeather'
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
    High = $ultraPath
    HighDefault = $defaultPath
    EnhancedPreset = $enhancedPath
    GroundChanges = ($groundChanges -join ', ')
} | Format-List
