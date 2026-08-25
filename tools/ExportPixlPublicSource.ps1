[CmdletBinding()]
param(
    [string]$ArchivePath = "",
    [string]$AllowedOutputRoot = ""
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$allowedRoot = [IO.Path]::GetFullPath($(if ($AllowedOutputRoot) { $AllowedOutputRoot } else { Join-Path $sourceRoot "dist" }))
if (-not $ArchivePath) { $ArchivePath = Join-Path $allowedRoot "PIXL-Renderer-v1.0-Source.zip" }
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
if ($unexpectedTrackedChanges.Count -ne 0) {
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
& git -C $sourceRoot archive --format=zip --prefix="PIXL-Renderer-v1.0-Source/" --output=$archive $commit
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archive)) {
    throw "git archive failed"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entries = @($zip.Entries | ForEach-Object { $_.FullName })
} finally {
    $zip.Dispose()
}

$prefix = "PIXL-Renderer-v1.0-Source/"
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
    '^PIXL-Renderer-v1\.0-Source/tools/PixDiTEnhance/',
    '^PIXL-Renderer-v1\.0-Source/(build|bin|dist|Data)/',
    '^PIXL-Renderer-v1\.0-Source/\.playwright-cli/',
    '^PIXL-Renderer-v1\.0-Source/(AGENTS|AI-INSTRUCTIONS|PIXL_ACTIVE_STATE|PIXL_DEVELOPMENT_WORKFLOW|PIXL_ENGINEERING_CONTEXT|PIXL_RELEASE_PREP_REPORT|PIXL_RELEASE_STATE|START_CODEX_ACTIVE_DEVELOPMENT|PRIVATE_COMPONENTS)\.md$',
    '\.(onnx|pt|pth|ckpt|safetensors|pixlbin)$'
)
foreach ($entry in $entries) {
    foreach ($pattern in $forbiddenPatterns) {
        if ($entry -match $pattern) { throw "Private/generated file escaped into public source archive: $entry" }
    }
}

$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$submodules = @(& git -C $sourceRoot submodule status | ForEach-Object { $_.Trim() })
[ordered]@{
    product = "PIXL Renderer"
    sourceCommit = $commit
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
