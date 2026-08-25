[CmdletBinding()]
param(
    [string]$PackageDirectory = "",
    [string]$BuildDirectory = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo "build\PIXL-12C\Release"
}
$errors = [Collections.Generic.List[string]]::new()

if ((Split-Path -Leaf $repo) -match '(?i)^community[-_ ]?shaders$') {
    $errors.Add("Physical source root still uses a retired renderer identity: $repo")
}

function Add-Error([string]$Message) { $errors.Add($Message) }
function Require-Source([string]$RelativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $repo $RelativePath))) { Add-Error "Missing source contract: $RelativePath" }
}
function Require-PackageFile([string]$Root, [string]$RelativePath) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root $RelativePath) -PathType Leaf)) { Add-Error "Missing package file: $RelativePath" }
}

foreach ($path in @(
    "ATTRIBUTION.md",
    "THIRD_PARTY_NOTICES.md",
    "EXCEPTIONS.md",
    "engine\RenderModule.cpp",
    "engine\Renderer\QualityProfiles.cpp",
    "engine\Renderer\WorldBenchmark.cpp",
    "engine\Menu\PIXLRendererPage.cpp",
	"pipeline\AmbientProbe\Kernels\AmbientProbe\DiffuseAmbientProbeCS.hlsl",
	"pipeline\AmbientProbe\Kernels\AmbientProbe\DiffuseAmbientProbe.dds",
	"pipeline\AmbientProbe\Kernels\AmbientProbe\SpecAmbientProbe.dds",
    "pipeline\Hybrid GI\Kernels\HybridGI\gi.cs.hlsl",
    "pipeline\Camera Suite\Kernels\CameraSuite\BloomUpsampleCS.hlsl",
	"pipeline\Terrain Field\Assets\Plugin\PIXL-TerrainField.esp",
    "distribution\Shaders\Lighting.hlsl",
    "distribution\Shaders\Water.hlsl",
    "distribution\Interface\PIXLRenderer\Visuals\Brand\PIXL-Mark.png",
    "distribution\SKSE\Plugins\PIXLRenderer\QualityPreviews\README.txt"
)) { Require-Source $path }

# Terrain Field's runtime fallback intentionally resolves local form 0x800
# from the bundled ESL when Skyrim's editor-ID index omits texture sets. Keep
# that native-plugin contract machine-verifiable rather than relying on a file
# existing under the expected name.
$terrainPlugin = Join-Path $repo "pipeline\Terrain Field\Assets\Plugin\PIXL-TerrainField.esp"
if (Test-Path -LiteralPath $terrainPlugin -PathType Leaf) {
    $terrainBytes = [IO.File]::ReadAllBytes($terrainPlugin)
    $terrainText = [Text.Encoding]::ASCII.GetString($terrainBytes)
    $hasTes4Header = $terrainBytes.Length -ge 16 -and $terrainText.StartsWith("TES4")
    $hasEslFlag = $terrainBytes.Length -ge 12 -and (([BitConverter]::ToUInt32($terrainBytes, 8) -band 0x200) -ne 0)
    $hasLandscapeRecord = $false
    for ($offset = 0; $offset -le $terrainBytes.Length - 16; ++$offset) {
        if ($terrainBytes[$offset] -eq 0x54 -and $terrainBytes[$offset + 1] -eq 0x58 -and
            $terrainBytes[$offset + 2] -eq 0x53 -and $terrainBytes[$offset + 3] -eq 0x54 -and
            [BitConverter]::ToUInt32($terrainBytes, $offset + 12) -eq 0x800) {
            $hasLandscapeRecord = $true
            break
        }
    }
    if (-not $hasTes4Header -or -not $hasEslFlag -or -not $hasLandscapeRecord -or
        $terrainText.IndexOf("LandscapeDefault", [StringComparison]::Ordinal) -lt 0) {
        Add-Error "Terrain Field plugin does not provide the required ESL TXST LandscapeDefault form 0x800"
    }
}

foreach ($retiredRoot in @("src", "features", "package")) {
    if (Test-Path -LiteralPath (Join-Path $repo $retiredRoot)) { Add-Error "Retired source root still exists: $retiredRoot" }
}

$descriptorIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath (Join-Path $repo "pipeline") -Directory | ForEach-Object {
    $descriptor = Join-Path $_.FullName "Module.ini"
    if (-not (Test-Path -LiteralPath $descriptor)) {
        Add-Error "Pipeline directory has no Module.ini: $($_.Name)"
        return
    }
    $text = Get-Content -LiteralPath $descriptor -Raw
    if ($text -notmatch '(?m)^\[PIXL Module\]\s*$') { Add-Error "Invalid PIXL descriptor section: $descriptor" }
    $match = [regex]::Match($text, '(?m)^\s*Id\s*=\s*([^\r\n]+?)\s*$')
    if (-not $match.Success) {
        Add-Error "Descriptor has no Id: $descriptor"
        return
    }
    $id = $match.Groups[1].Value.Trim()
    if (-not $descriptorIds.Add($id)) { Add-Error "Duplicate PIXL module Id: $id" }
}

$runtimeIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$headers = @(Get-ChildItem -LiteralPath (Join-Path $repo "engine\Modules") -Recurse -File -Filter "*.h") + @(Get-Item (Join-Path $repo "engine\MaterialForge.h"))
foreach ($header in $headers) {
    $text = Get-Content -LiteralPath $header.FullName -Raw
    foreach ($match in [regex]::Matches($text, 'GetShortName\s*\([^)]*\)[^{]*\{\s*return\s+"([A-Za-z0-9_]+)"')) {
        $id = $match.Groups[1].Value
        if ($id -ne "Streamline") { $null = $runtimeIds.Add($id) }
    }
}
foreach ($id in $descriptorIds) {
    if (-not $runtimeIds.Contains($id)) { Add-Error "Descriptor Id has no runtime module: $id" }
}
foreach ($id in $runtimeIds) {
    if (-not $descriptorIds.Contains($id)) { Add-Error "Runtime module has no descriptor: $id" }
}

# Keep every literal runtime Data/Shaders reference backed by a staged source
# asset. This catches incomplete renames before they can become scene-specific
# null resources (for example, an exterior-only shader or probe texture).
$availableRuntimeAssets = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$distributionShaderRoot = Join-Path $repo "distribution\Shaders"
if (Test-Path -LiteralPath $distributionShaderRoot -PathType Container) {
    $distributionShaderPrefix = $distributionShaderRoot.TrimEnd('\') + '\'
    Get-ChildItem -LiteralPath $distributionShaderRoot -Recurse -File | ForEach-Object {
        $null = $availableRuntimeAssets.Add($_.FullName.Substring($distributionShaderPrefix.Length))
    }
}
Get-ChildItem -LiteralPath (Join-Path $repo "pipeline") -Directory | ForEach-Object {
    $kernelRoot = Join-Path $_.FullName "Kernels"
    if (Test-Path -LiteralPath $kernelRoot -PathType Container) {
        $kernelPrefix = $kernelRoot.TrimEnd('\') + '\'
        Get-ChildItem -LiteralPath $kernelRoot -Recurse -File | ForEach-Object {
            $null = $availableRuntimeAssets.Add($_.FullName.Substring($kernelPrefix.Length))
        }
    }
}

$runtimeAssetRefs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$runtimeAssetPattern = 'Data(?:\\\\|/)Shaders(?:\\\\|/)([^"\r\n]+\.(?:hlsl|hlsli|dds|png|cube|lut|tga))'
Get-ChildItem -LiteralPath (Join-Path $repo "engine") -Recurse -File | Where-Object {
    $_.Extension -in @('.cpp','.h')
} | ForEach-Object {
    $sourceText = Get-Content -LiteralPath $_.FullName -Raw
    foreach ($match in [regex]::Matches($sourceText, $runtimeAssetPattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
        $assetRef = $match.Groups[1].Value -replace '\\\\','\' -replace '/','\'
        # Formatted paths such as "{}.hlsl" are validated by their owning
        # module and cannot be resolved statically here.
        if ($assetRef -notmatch '[{}]') { $null = $runtimeAssetRefs.Add($assetRef) }
    }
}
foreach ($assetRef in $runtimeAssetRefs) {
    if (-not $availableRuntimeAssets.Contains($assetRef)) {
        Add-Error "Runtime shader asset has no staging source: Shaders\$assetRef"
    }
}

# Validate the complete literal HLSL include graph against the same assembled
# shader namespace the staging tool creates. C++ compilation cannot detect a
# missing include that is reached only by a runtime shader permutation.
$shaderSources = @(
    Get-ChildItem -LiteralPath $distributionShaderRoot -Recurse -File | Where-Object { $_.Extension -in @('.hlsl', '.hlsli') }
)
Get-ChildItem -LiteralPath (Join-Path $repo "pipeline") -Directory | ForEach-Object {
    $kernelRoot = Join-Path $_.FullName "Kernels"
    if (Test-Path -LiteralPath $kernelRoot -PathType Container) {
        $shaderSources += @(
            Get-ChildItem -LiteralPath $kernelRoot -Recurse -File | Where-Object { $_.Extension -in @('.hlsl', '.hlsli') }
        )
    }
}
$includePattern = '(?m)^\s*#\s*include\s+"([^"]+)"'
foreach ($shaderSource in $shaderSources) {
    $shaderText = Get-Content -LiteralPath $shaderSource.FullName -Raw
    foreach ($match in [regex]::Matches($shaderText, $includePattern)) {
        $include = $match.Groups[1].Value.Replace('/', '\')
        while ($include.Contains('\\')) { $include = $include.Replace('\\', '\') }
        if (-not $availableRuntimeAssets.Contains($include)) {
            Add-Error "Shader include has no staging source: $($shaderSource.FullName) -> $include"
        }
    }
    if ($shaderText -match '\bEFFECTS11\b') {
        Add-Error "Deleted Effects11 permutation remains in active shader source: $($shaderSource.FullName)"
    }
}

foreach ($jsonPath in @(
    "distribution\SKSE\Plugins\PIXLRenderer\SettingsDefault.json",
    "distribution\SKSE\Plugins\PIXLRenderer\Presets\PIXL-Renderer-Live-Tested.json",
    "distribution\SKSE\Plugins\PIXLRenderer\Themes\PIXL.json",
    "distribution\SKSE\Plugins\PIXLRenderer\Translations\en.json"
)) {
    try { Get-Content -LiteralPath (Join-Path $repo $jsonPath) -Raw | ConvertFrom-Json | Out-Null }
    catch { Add-Error "Invalid JSON: $jsonPath ($($_.Exception.Message))" }
}

$scanRoots = @(
    (Join-Path $repo "engine"),
    (Join-Path $repo "pipeline"),
    (Join-Path $repo "distribution"),
    (Join-Path $repo "tools"),
    (Join-Path $repo "docs"),
    (Join-Path $repo "CMakeLists.txt"),
    (Join-Path $repo "AI-INSTRUCTIONS.md")
)
$identityAllowPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in @(
    "engine\PipelineHealth.cpp",
    "engine\PipelineHealth.h",
    "README.md"
)) {
    $null = $identityAllowPaths.Add((Join-Path $repo $relativePath))
}
$textFiles = foreach ($root in $scanRoots) {
    if (Test-Path -LiteralPath $root -PathType Leaf) { Get-Item -LiteralPath $root; continue }
    Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
        $_.Extension -in @('.cpp','.h','.hlsl','.hlsli','.ini','.json','.ps1','.txt','.md') -and
        -not $identityAllowPaths.Contains($_.FullName) -and
        $_.Name -ne 'AuditPixlRenderer.ps1' -and
        $_.Name -notin @('SOURCE-AND-CREDITS.md','COPYING','EXCEPTIONS.md','ATTRIBUTION.md','THIRD_PARTY_NOTICES.md','LICENSE','license.md','license.txt','reflex.license.txt','nvngx_dlss.license.txt')
    }
}
$identityHit = $textFiles | Select-String -Pattern 'Community Shaders|CommunityShaders|community-shaders|TruePBR|True PBR|CS Editor' | Select-Object -First 1
if ($identityHit) { Add-Error "Retired renderer identity remains in active source: $($identityHit.Path):$($identityHit.LineNumber)" }

$builtDll = Join-Path $BuildDirectory "PIXLRenderer.dll"
if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) { Add-Error "Missing build DLL: $builtDll" }

if ($PackageDirectory) {
    if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
        Add-Error "Package directory does not exist: $PackageDirectory"
    } else {
        $package = (Resolve-Path -LiteralPath $PackageDirectory).Path
        foreach ($file in @(
            "COPYING",
            "EXCEPTIONS.md",
            "ATTRIBUTION.md",
            "THIRD_PARTY_NOTICES.md",
            "SOURCE-AND-CREDITS.md",
			"PIXL-TerrainField.esp",
            "SKSE\Plugins\PIXLRenderer.dll",
            "SKSE\Plugins\PIXL\Config\RendererDefaults.json",
            "SKSE\Plugins\PIXL\Profiles\PIXL-Golden-Baseline.json",
            "SKSE\Plugins\PIXL\Interface\Visuals\Brand\PIXL-Mark.png",
            "SKSE\Plugins\PIXL\Interface\QualityPreviews\README.txt",
            "Shaders\PIXL\Modules\HybridGI.ini",
			"Shaders\AmbientProbe\DiffuseAmbientProbeCS.hlsl",
			"Shaders\AmbientProbe\DiffuseAmbientProbe.dds",
			"Shaders\AmbientProbe\SpecAmbientProbe.dds",
            "Shaders\CameraSuite\BloomUpsampleCS.hlsl",
            "Shaders\HybridGI\gi.cs.hlsl",
            "Shaders\Lighting.hlsl",
            "Shaders\Water.hlsl",
            "PIXL-RENDERER.manifest.json"
        )) { Require-PackageFile $package $file }

        foreach ($assetRef in $runtimeAssetRefs) {
            Require-PackageFile $package ("Shaders\" + $assetRef)
        }

        $packageText = Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object {
            $_.Extension -in @('.json','.ini','.txt','.hlsl','.hlsli') -and
            # UserGraphics may legitimately retain false-valued legacy module
            # keys so older profiles can round-trip. It is user data, not product
            # identity or active source, and is included only when the staging
            # caller explicitly supplies it.
            $_.Name -notin @('PIXL-RENDERER.manifest.json','UserGraphics.json','SOURCE-AND-CREDITS.md','ATTRIBUTION.md','THIRD_PARTY_NOTICES.md')
        }
        $packageHit = $packageText | Select-String -Pattern 'Community Shaders|CommunityShaders|community-shaders|TruePBR|True PBR|CS Editor' | Select-Object -First 1
        if ($packageHit) { Add-Error "Retired identity remains in live package: $($packageHit.Path):$($packageHit.LineNumber)" }

        if (Test-Path -LiteralPath $builtDll) {
            $buildHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
            $packageHash = (Get-FileHash -LiteralPath (Join-Path $package "SKSE\Plugins\PIXLRenderer.dll") -Algorithm SHA256).Hash
            if ($buildHash -ne $packageHash) { Add-Error "Staged DLL does not match the verified build" }
        }

        $packageTerrainPlugin = Join-Path $package "PIXL-TerrainField.esp"
        if ((Test-Path -LiteralPath $terrainPlugin -PathType Leaf) -and
            (Test-Path -LiteralPath $packageTerrainPlugin -PathType Leaf)) {
            $sourceTerrainHash = (Get-FileHash -LiteralPath $terrainPlugin -Algorithm SHA256).Hash
            $packageTerrainHash = (Get-FileHash -LiteralPath $packageTerrainPlugin -Algorithm SHA256).Hash
            if ($sourceTerrainHash -ne $packageTerrainHash) { Add-Error "Staged Terrain Field plugin does not match source" }
        }
    }
}

if ($errors.Count) {
    $errors | ForEach-Object { Write-Host "ERROR: $_" -ForegroundColor Red }
    throw "PIXL Renderer audit failed with $($errors.Count) issue(s)."
}

Write-Host "PIXL Renderer audit passed"
Write-Host "  Integrated modules: $($descriptorIds.Count)"
if (Test-Path -LiteralPath $builtDll) { Write-Host "  DLL: $builtDll" }
