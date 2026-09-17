[CmdletBinding()]
param(
    [string]$ArchivePath = "",
    [string]$AllowedOutputRoot = "",
    [switch]$IncludeWorkingTree
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$allowedRoot = [IO.Path]::GetFullPath($(if ($AllowedOutputRoot) { $AllowedOutputRoot } else { Join-Path $sourceRoot "dist" }))
if (-not $ArchivePath) { $ArchivePath = Join-Path $allowedRoot "PIXL-Renderer-1.0.2-Source.zip" }
$archive = [IO.Path]::GetFullPath($ArchivePath)

if (-not $archive.StartsWith($allowedRoot.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing source archive outside allowed output root '$allowedRoot': $archive"
}
if ([string]::Equals($archive.TrimEnd('\'), $allowedRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use the allowed output root itself as an archive."
}

$commit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commit) { throw "Unable to resolve the source commit" }
$trackedChanges = @(& git -C $sourceRoot status --porcelain=v1 --untracked-files=no)
if ($LASTEXITCODE -ne 0) { throw "Unable to inspect Git status" }
$allowedPatchedSubmodule = @($trackedChanges | Where-Object { $_ -eq " m extern/FidelityFX-SDK" })
$unexpectedTrackedChanges = @($trackedChanges | Where-Object { $_ -ne " m extern/FidelityFX-SDK" })
if (!$IncludeWorkingTree -and $unexpectedTrackedChanges.Count -ne 0) {
    throw "Public source exports must come from a committed revision; tracked changes remain: $($unexpectedTrackedChanges -join ', ')"
}
if ($allowedPatchedSubmodule.Count -ne 0) {
    $fidelityRoot = Join-Path $sourceRoot "extern\FidelityFX-SDK"
    $fidelityPatch = Join-Path $sourceRoot "cmake\patches\FidelityFX-DX11-Short-Output.patch"
    $fidelityChanges = @(& git -C $fidelityRoot diff --name-only)
    if ($LASTEXITCODE -ne 0 -or
        $fidelityChanges.Count -ne 1 -or
        $fidelityChanges[0] -ne "sdk/src/backends/dx11/CMakeLists.txt") {
        throw "FidelityFX submodule contains changes beyond the reproducible PIXL DX11 path patch."
    }
    & git -C $fidelityRoot apply --reverse --check $fidelityPatch
    if ($LASTEXITCODE -ne 0) {
        throw "FidelityFX submodule change does not exactly match the committed reproducible patch."
    }
}

New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force | Out-Null
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
& git -C $sourceRoot archive --format=zip --prefix="PIXL-Renderer-1.0.2-Source/" --output=$archive $commit
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archive)) {
    throw "git archive failed"
}

# Explicit snapshot mode preserves uncommitted release work without altering Git.
# Retain git-archive's export-ignore policy, then overlay current file contents.
if ($IncludeWorkingTree) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $snapshot = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $snapshotPrefix = 'PIXL-Renderer-1.0.2-Source/'
        foreach ($entry in @($snapshot.Entries)) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $relative = $entry.FullName.Substring($snapshotPrefix.Length)
            $local = Join-Path $sourceRoot $relative
            $name = $entry.FullName
            $entry.Delete()
            if (Test-Path -LiteralPath $local -PathType Leaf) {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($snapshot, $local, $name) | Out-Null
            }
        }
        foreach ($relative in @(& git -C $sourceRoot ls-files --others --exclude-standard)) {
            $attributes = & git -C $sourceRoot check-attr export-ignore -- $relative
            if ($attributes -match ': set$') { continue }
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($snapshot, (Join-Path $sourceRoot $relative), ($snapshotPrefix + $relative)) | Out-Null
        }
    } finally { $snapshot.Dispose() }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entries = @($zip.Entries | ForEach-Object { $_.FullName })
} finally {
    $zip.Dispose()
}

$prefix = "PIXL-Renderer-1.0.2-Source/"
$required = @(
    "CMakeLists.txt",
    "README.md",
    "COPYING",
    "EXCEPTIONS.md",
    "ATTRIBUTION.md",
    "THIRD_PARTY_NOTICES.md",
    "SOURCE_DEPENDENCIES.md",
    "engine/XSEPlugin.cpp",
    "distribution/Shaders/Lighting.hlsl",
    "pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli",
    "cmake/patches/FidelityFX-DX11-Short-Output.patch"
)
foreach ($path in $required) {
    if ($entries -notcontains ($prefix + $path)) { throw "Public source archive is missing required source: $path" }
}

$forbiddenPatterns = @(
    '^PIXL-Renderer-v1\.0-Source/PIXL_Preset_Experiments/',
    '^PIXL-Renderer-v1\.0-Source/tools/GeneratePixlPresetExperiments\.ps1$',
    '^PIXL-Renderer-v1\.0-Source/docs/(release_polish|reports|PIXL_GUI_Audit|Crysis3Pipeline|DynamicFire|HairReconstruction)/',
    '^PIXL-Renderer-v1\.0-Source/tools/PixDiTEnhance/',
    '^PIXL-Renderer-v1\.0-Source/(build|bin|dist|Data)/',
    '^PIXL-Renderer-v1\.0-Source/\.playwright-cli/',
    '^PIXL-Renderer-v1\.0-Source/(AGENTS|AI-INSTRUCTIONS|PIXL_ACTIVE_STATE|PIXL_DEVELOPMENT_WORKFLOW|PIXL_ENGINEERING_CONTEXT|PIXL_RELEASE_PREP_REPORT|PIXL_RELEASE_STATE|START_CODEX_ACTIVE_DEVELOPMENT|PRIVATE_COMPONENTS)\.md$',
    '\.(onnx|pt|pth|ckpt|safetensors|pixlbin)$'
)
foreach ($entry in $entries) {
    foreach ($pattern in $forbiddenPatterns) {
        $normalizedEntry = $entry.Replace($prefix, 'PIXL-Renderer-v1.0-Source/')
        if ($normalizedEntry -match $pattern) { throw "Private/generated file escaped into public source archive: $entry" }
    }
}

$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$submodules = @(& git -C $sourceRoot submodule status | ForEach-Object { $_.Trim() })
[ordered]@{
    product = "PIXL Renderer"
    sourceCommit = $commit
    sourceWorkingTreeSnapshot = [bool]$IncludeWorkingTree
    sourceWorkingTreeDirty = [bool]$unexpectedTrackedChanges.Count
    archive = [IO.Path]::GetFileName($archive)
    sha256 = $hash
    entries = $entries.Count
    privatePixDiTExcluded = $true
    generatedBuildExcluded = $true
    shaderCacheExcluded = $true
    submodules = $submodules
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($archive + ".metadata.json") -Encoding utf8
Set-Content -LiteralPath ($archive + ".sha256") -Value "$hash  $([IO.Path]::GetFileName($archive))" -Encoding ascii

Write-Host "Public source archive: $archive"
Write-Host "Source commit: $commit"
Write-Host "Entries: $($entries.Count)"
Write-Host "SHA256: $hash"
