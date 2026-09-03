[CmdletBinding()]
param(
    [string]$OutputDirectory = "",
    [string]$CompatibilityMirror = "",
    [string]$ArchivePath = "",
    [string]$BuildDirectory = "",
    [string]$PipelineLibrary = "",
    [string]$UserConfigPath = "",
    [string]$AllowedOutputRoot = "",
    [switch]$SkipPipelineLibrary,
    [switch]$SkipArchive
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$allowedRoot = [IO.Path]::GetFullPath($(if ($AllowedOutputRoot) { $AllowedOutputRoot } else { Join-Path $sourceRoot "dist" }))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $allowedRoot "PIXL-Renderer-v1.0-Clean-Cache" }
if (-not $ArchivePath) { $ArchivePath = Join-Path $allowedRoot "PIXL-Renderer-v1.0-Clean-Cache.zip" }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $sourceRoot "build\PIXL-12C\Release" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$archive = if ($ArchivePath) { [IO.Path]::GetFullPath($ArchivePath) } else { "" }
$mirror = if ($CompatibilityMirror) { [IO.Path]::GetFullPath($CompatibilityMirror) } else { "" }

function Assert-PackageTarget([string]$Path) {
    if (-not $Path.StartsWith($allowedRoot.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing target outside the allowed output root '$allowedRoot': $Path"
    }
    if ([string]::Equals($Path.TrimEnd('\'), $allowedRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate on the allowed output root itself."
    }
}

function Remove-PackageItem([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    Assert-PackageTarget $Path
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        Remove-Item -LiteralPath $Path -Force
    } else {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Copy-Tree([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source)) { throw "Missing package source: $Source" }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

Assert-PackageTarget $output
if ($mirror) { Assert-PackageTarget $mirror }
if (-not $SkipArchive -and $archive) { Assert-PackageTarget $archive }

$dll = Join-Path $BuildDirectory "PIXLRenderer.dll"
$required = @(
    $dll,
    (Join-Path $sourceRoot "distribution\Shaders"),
    (Join-Path $sourceRoot "distribution\SKSE\Plugins\PIXLRenderer\SettingsDefault.json"),
    (Join-Path $sourceRoot "COPYING")
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing verified artifact: $path" }
}

Remove-PackageItem $output
New-Item -ItemType Directory -Path $output -Force | Out-Null

$shaderRoot = Join-Path $output "Shaders"
$moduleCatalog = Join-Path $shaderRoot "PIXL\Modules"
$pluginRoot = Join-Path $output "SKSE\Plugins\PIXL"
$configRoot = Join-Path $pluginRoot "Config"
$profileRoot = Join-Path $pluginRoot "Profiles"
$interfaceRoot = Join-Path $pluginRoot "Interface"
New-Item -ItemType Directory -Path $shaderRoot,$moduleCatalog,$configRoot,$profileRoot,$interfaceRoot -Force | Out-Null

# One closed shader pipeline: Skyrim entry points plus every integrated PIXL kernel.
Copy-Tree (Join-Path $sourceRoot "distribution\Shaders") $shaderRoot
Get-ChildItem -LiteralPath (Join-Path $sourceRoot "pipeline") -Directory | Sort-Object Name | ForEach-Object {
    $descriptor = Join-Path $_.FullName "Module.ini"
    $kernels = Join-Path $_.FullName "Kernels"
    if (-not (Test-Path -LiteralPath $descriptor)) { throw "Missing PIXL module descriptor: $descriptor" }
    $descriptorText = Get-Content -LiteralPath $descriptor -Raw
    # Retired ABI stubs remain in source only so old shared-buffer layouts and
    # cached shader includes can be inspected safely. They are not runtime modules
    # and must not be copied into a public/game package.
    if ($descriptorText -match '(?im)^\s*Pipeline\s*=\s*Retired\s*$') { return }
    $idMatch = [regex]::Match($descriptorText, '(?m)^\s*Id\s*=\s*([^\r\n]+?)\s*$')
    if (-not $idMatch.Success) { throw "Module descriptor has no Id: $descriptor" }
    $moduleId = $idMatch.Groups[1].Value.Trim()
    Copy-Item -LiteralPath $descriptor -Destination (Join-Path $moduleCatalog ($moduleId + ".ini")) -Force
    if (Test-Path -LiteralPath $kernels) { Copy-Tree $kernels $shaderRoot }
}

# Native data paths that Skyrim or the corresponding PIXL hook consumes directly.
Copy-Tree (Join-Path $sourceRoot "pipeline\Radiant Grid\Assets\LightProfiles") (Join-Path $output "ParticleLights")
Copy-Tree (Join-Path $sourceRoot "pipeline\Terrain Occlusion\Assets\HeightMaps") (Join-Path $output "textures\heightmaps")
Copy-Tree (Join-Path $sourceRoot "pipeline\Waterbody\Assets\Meshes\Water") (Join-Path $output "meshes\water")
Copy-Tree (Join-Path $sourceRoot "pipeline\Waterbody\Assets\WorldWater") (Join-Path $pluginRoot "World\Water")
Copy-Item -LiteralPath (Join-Path $sourceRoot "pipeline\Terrain Field\Assets\Plugin\PIXL-TerrainField.esp") -Destination $output -Force

# PIXL runtime identity and compact presentation assets.
Copy-Item -LiteralPath $dll -Destination (Join-Path $output "SKSE\Plugins\PIXLRenderer.dll") -Force
$runtimeSource = Join-Path $sourceRoot "distribution\SKSE\Plugins\PIXLRenderer"
Copy-Item -LiteralPath (Join-Path $runtimeSource "SettingsDefault.json") -Destination (Join-Path $configRoot "RendererDefaults.json") -Force
if (-not [string]::IsNullOrWhiteSpace($UserConfigPath)) {
    $resolvedUserConfig = (Resolve-Path -LiteralPath $UserConfigPath).Path
    try {
        Get-Content -LiteralPath $resolvedUserConfig -Raw | ConvertFrom-Json | Out-Null
    } catch {
        throw "Invalid user graphics configuration '$resolvedUserConfig': $($_.Exception.Message)"
    }
    Copy-Item -LiteralPath $resolvedUserConfig -Destination (Join-Path $configRoot "UserGraphics.json") -Force
}
Copy-Item -LiteralPath (Join-Path $runtimeSource "Presets\PIXL-Renderer-Live-Tested.json") -Destination (Join-Path $profileRoot "PIXL-Golden-Baseline.json") -Force
New-Item -ItemType Directory -Path (Join-Path $interfaceRoot "Themes"),(Join-Path $interfaceRoot "Locale") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $runtimeSource "Themes\PIXL.json") -Destination (Join-Path $interfaceRoot "Themes\PIXL.json") -Force
Copy-Item -LiteralPath (Join-Path $runtimeSource "Translations\en.json") -Destination (Join-Path $interfaceRoot "Locale\en.json") -Force
Copy-Tree (Join-Path $runtimeSource "QualityPreviews") (Join-Path $interfaceRoot "QualityPreviews")

$presentationSource = Join-Path $sourceRoot "distribution\Interface\PIXLRenderer"
Copy-Tree (Join-Path $presentationSource "Fonts\Jost") (Join-Path $interfaceRoot "Fonts\Jost")
Copy-Tree (Join-Path $presentationSource "Fonts\Sanguis") (Join-Path $interfaceRoot "Fonts\Sanguis")
Copy-Tree (Join-Path $presentationSource "Visuals\Brand") (Join-Path $interfaceRoot "Visuals\Brand")

$pipelineCount = 0
$includePipelineLibrary = -not $SkipPipelineLibrary -and -not [string]::IsNullOrWhiteSpace($PipelineLibrary)
if ($includePipelineLibrary) {
    $pipelineRoot = [IO.Path]::GetFullPath($PipelineLibrary)
    $libraryIni = Join-Path $pipelineRoot "Library.ini"
    if (-not (Test-Path -LiteralPath $libraryIni)) { throw "Missing validated PIXL pipeline metadata: $libraryIni" }
    $metadata = Get-Content -LiteralPath $libraryIni -Raw
    if ($metadata -notmatch 'Layout\s*=\s*PIXL\.StageShard\.v1') { throw "Pipeline library is not PIXL.StageShard.v1" }
    if ($metadata -notmatch 'ShaderABI\s*=\s*PIXL\.SharedBuffers\.20260902\.1') { throw "Pipeline library was built for an incompatible PIXL shared-shader ABI" }
    $pipelineCount = (Get-ChildItem -LiteralPath $pipelineRoot -File -Recurse -Filter "*.pixlbin").Count
    if ($pipelineCount -lt 3000) { throw "Pipeline library is incomplete ($pipelineCount stages; expected at least 3000)" }
    Copy-Tree $pipelineRoot (Join-Path $output "PIXL\PipelineLibrary")
}

foreach ($document in @("PIXL-RENDERER-README.md", "SOURCE-AND-CREDITS.md")) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot "distribution\$document") -Destination $output -Force
}
foreach ($document in @("COPYING", "EXCEPTIONS.md", "ATTRIBUTION.md", "THIRD_PARTY_NOTICES.md")) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $document) -Destination $output -Force
}

$manifestFiles = Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($output.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
[ordered]@{
    product = "PIXL Renderer"
    title = "PBR Rendering Engine v1.0"
    version = "1.0.0"
    channel = "LIVE-TEST"
    executable = "SKSE/Plugins/PIXLRenderer.dll"
    dataRoot = "SKSE/Plugins/PIXL"
    exclusiveRenderer = $true
    cacheMode = if ($includePipelineLibrary) { "preloaded" } else { "compile-on-device" }
    preloadedPipelineStages = $pipelineCount
    shaderCompilePattern = "PIXL.StageShard.v1"
    shaderABI = "PIXL.SharedBuffers.20260902.1"
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    files = $manifestFiles
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output "PIXL-RENDERER.manifest.json") -Encoding utf8

if ($mirror) {
    if (-not [string]::Equals($mirror, $output, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-PackageItem $mirror
        New-Item -ItemType Directory -Path (Split-Path -Parent $mirror) -Force | Out-Null
        New-Item -ItemType Junction -Path $mirror -Target $output -Force | Out-Null
    }
}

if (-not $SkipArchive -and $archive) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force | Out-Null
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    $sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
    $sevenZip = @(
        $(if ($sevenZipCommand) { $sevenZipCommand.Source }),
        $(if (${env:ProgramFiles}) { Join-Path ${env:ProgramFiles} "7-Zip\7z.exe" }),
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe" })
    ) |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($sevenZip) {
        Push-Location $output
        try {
            & $sevenZip a -tzip -mx=7 -mmt=on $archive "*"
            if ($LASTEXITCODE -ne 0) { throw "7-Zip failed with exit code $LASTEXITCODE" }
        } finally {
            Pop-Location
        }
    } else {
        Compress-Archive -Path (Join-Path $output "*") -DestinationPath $archive -CompressionLevel Optimal
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    Set-Content -LiteralPath ($archive + ".sha256") -Value "$hash  $([IO.Path]::GetFileName($archive))" -Encoding ascii
    Write-Host "Archive: $archive"
    Write-Host "SHA256: $hash"
}

Write-Host "PIXL live-test stage: $output"
if ($mirror) { Write-Host "Current test path: $mirror" }
