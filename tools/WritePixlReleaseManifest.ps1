[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [string]$Version = '1.0.5'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repo 'dist')).TrimEnd('\')
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path.TrimEnd('\')
if (!$release.StartsWith($allowedRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Release output must remain under $allowedRoot"
}
$core = Join-Path $release 'PIXL-Core'
$packageManifest = Get-Content -LiteralPath (Join-Path $core 'PIXL-RENDERER.manifest.json') -Raw | ConvertFrom-Json
if ($packageManifest.version -cne $Version) { throw "Core manifest version mismatch: $($packageManifest.version)" }
$dll = Join-Path $core 'SKSE\Plugins\PIXLRenderer.dll'
if ((Get-Item -LiteralPath $dll).VersionInfo.ProductVersion -cne "$Version.0") { throw 'Staged DLL version mismatch.' }

$names = @(
    "PIXL-Renderer-v$Version-Core.zip",
    "PIXL-Renderer-v$Version-Source.zip",
    "PIXL-Renderer-v$Version-FOMOD.zip",
    'SurfaceTides-1.0.2-PIXL-Source.zip'
)
$artifacts = [ordered]@{}
$checksums = [Collections.Generic.List[string]]::new()
foreach ($name in $names) {
    $path = Join-Path $release $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing release archive: $path" }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $artifacts[$name] = [ordered]@{ bytes = (Get-Item -LiteralPath $path).Length; sha256 = $hash }
    $checksums.Add("$hash  $name")
}
$dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
$dirty = [bool]@(& git -C $repo status --porcelain --untracked-files=normal)
$shaderCount = @(Get-ChildItem -LiteralPath (Join-Path $core 'Shaders') -File -Recurse | Where-Object { $_.Extension -in @('.hlsl','.hlsli','.fx','.fxh') }).Count

[ordered]@{
    product = 'PIXL Renderer'
    version = $Version
    releaseReadiness = 'live-test-candidate'
    buildConfiguration = 'Release'
    commit = $commit
    sourceWorkingTreeDirty = $dirty
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    dll = [ordered]@{ path = 'PIXL-Core/SKSE/Plugins/PIXLRenderer.dll'; bytes = (Get-Item -LiteralPath $dll).Length; sha256 = $dllHash }
    cacheMode = $packageManifest.cacheMode
    shaderABI = $packageManifest.shaderABI
    shaderRevision = $packageManifest.shaderRevision
    shippedShaderFiles = $shaderCount
    coreManifestPayloads = @($packageManifest.files).Count
    artifacts = $artifacts
    exhaustiveSourceReviewRequired = $true
    manualTestsRequired = $true
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $release 'RELEASE_MANIFEST.json') -Encoding UTF8
$checksums | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $repo 'docs\CHANGELOG.md') -Destination (Join-Path $release 'CHANGELOG.md') -Force
Copy-Item -LiteralPath (Join-Path $repo 'docs\RELEASE_CHECKLIST_1.0.5.md') -Destination (Join-Path $release 'RELEASE_CHECKLIST_1.0.5.md') -Force
Write-Host "PASS: release manifest, four archive hashes, $shaderCount shader files, and checklist at $release"
