[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BasePackageDirectory,
    [Parameter(Mandatory=$true)][string]$SurfaceTidesSource,
    [string]$SurfaceTidesSourceArchive = "",
    [string]$OutputDirectory = "",
    [string]$ArchivePath = ""
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$base = (Resolve-Path -LiteralPath $BasePackageDirectory).Path
$surface = (Resolve-Path -LiteralPath $SurfaceTidesSource).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repo 'dist'))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $allowedRoot "PIXL-Renderer-1.0.2-FOMOD-$stamp" }
if (!$ArchivePath) { $ArchivePath = "$OutputDirectory.zip" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$archive = [IO.Path]::GetFullPath($ArchivePath)

function Assert-Output([string]$path) {
    if (!$path.StartsWith($allowedRoot.TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Output must remain under $allowedRoot"
    }
    if ([string]::Equals($path.TrimEnd('\'),$allowedRoot.TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to use the dist root itself.'
    }
}
function Copy-Tree([string]$source,[string]$destination) {
    if (!(Test-Path -LiteralPath $source)) { throw "Missing source: $source" }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
}
function Assert-SurfaceTidesUniversalDll([string]$dll,[string]$sourceRoot) {
    $binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll))
    foreach ($export in @('SKSEPlugin_Load','SKSEPlugin_Query','SKSEPlugin_Version')) {
        if ($binaryText.IndexOf($export,[StringComparison]::Ordinal) -lt 0) {
            throw "SurfaceTides DLL is missing required SKSE export: $export"
        }
    }
    $main = Get-Content -LiteralPath (Join-Path $sourceRoot 'src\plugin\Main.cpp') -Raw
    foreach ($runtime in @('1,5,97,0','1,6,1170,0','1,6,1179,0','1,7,104,0')) {
        if ($main -notmatch [regex]::Escape($runtime)) { throw "SurfaceTides source does not allow required runtime: $runtime" }
    }
}

Assert-Output $output
Assert-Output $archive
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }

foreach ($required in @(
    'PIXL-RENDERER.manifest.json',
    'SKSE\Plugins\PIXLRenderer.dll',
    'Shaders\Water.hlsl',
    'Shaders\Common\Color.hlsli',
    'Shaders\CameraSuite\HDROutputCS.hlsl',
    'SKSE\Plugins\PIXL\Documentation\COPYING',
    'SKSE\Plugins\PIXL\Documentation\EXCEPTIONS.md',
    'SKSE\Plugins\PIXL\Documentation\ATTRIBUTION.md',
    'SKSE\Plugins\PIXL\Documentation\THIRD_PARTY_NOTICES.md',
    'SKSE\Plugins\PIXL\Documentation\SOURCE-AND-CREDITS.md'
)) {
    if (!(Test-Path -LiteralPath (Join-Path $base $required))) { throw "Incomplete PIXL package: $required" }
}
$surfaceDllCandidates = @(
    (Join-Path $surface 'build\windows-universal-v8\Release\SurfaceTides.dll'),
    (Join-Path $surface 'build\windows-vendored-v8d\Release\SurfaceTides.dll'),
    (Join-Path $surface 'build\windows\Release\SurfaceTides.dll')
)
$surfaceDll = $surfaceDllCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$surfaceDll = if ($surfaceDll) { (Resolve-Path -LiteralPath $surfaceDll).Path } else { $surfaceDllCandidates[0] }
$surfaceShader = Join-Path $surface 'Data\Shaders\SurfaceTides\Water.hlsl'
$surfacePixlIni = Join-Path $repo 'installer\PIXLRenderer\SurfaceTides-PIXL-1.0.2.ini'
if (!(Test-Path -LiteralPath $surfaceDll)) { throw "Missing SurfaceTides bridge DLL: $surfaceDll" }
if (!(Test-Path -LiteralPath $surfaceShader)) { throw "Missing SurfaceTides bridge shader: $surfaceShader" }
if (!(Test-Path -LiteralPath $surfacePixlIni)) { throw "Missing PIXL SurfaceTides preset: $surfacePixlIni" }
Assert-SurfaceTidesUniversalDll $surfaceDll $surface

$surfaceNoticeFiles = @(
    (Join-Path $surface 'LICENSE'),
    (Join-Path $surface 'LICENSE.md'),
    (Join-Path $surface 'THIRD_PARTY.md'),
    (Join-Path $surface 'THIRD_PARTY_NOTICES.md')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
$surfaceLicenseDirectory = Join-Path $surface 'licenses'
if ($surfaceNoticeFiles.Count -eq 0 -and -not (Test-Path -LiteralPath $surfaceLicenseDirectory -PathType Container)) {
    throw 'SurfaceTides source has no detectable licence/third-party notice set; refusing to build the bridge package.'
}

$core = Join-Path $output 'PIXL-Core'
$bridge = Join-Path $output 'PIXL-Optional\SurfaceTides-1.0.2'
$bridgeDocs = Join-Path $bridge 'SKSE\Plugins\PIXL\Documentation\SurfaceTidesBridge'
Copy-Tree $base $core
Copy-Tree (Join-Path $repo 'installer\PIXLRenderer\fomod') (Join-Path $output 'fomod')
Copy-Tree (Join-Path $repo 'installer\PIXLRenderer\images') (Join-Path $output 'fomod\images')
Copy-Item -LiteralPath (Join-Path $repo 'installer\PIXLRenderer\PIXL-INSTALLER-NOTICE.md') -Destination (Join-Path $core 'PIXL-INSTALLER-NOTICE.md')

New-Item -ItemType Directory -Path (Join-Path $bridge 'SKSE\Plugins'),(Join-Path $bridge 'Shaders\SurfaceTides'),$bridgeDocs -Force | Out-Null
Copy-Item -LiteralPath $surfaceDll -Destination (Join-Path $bridge 'SKSE\Plugins\SurfaceTides.dll')
Copy-Item -LiteralPath $surfaceShader -Destination (Join-Path $bridge 'Shaders\SurfaceTides\Water.hlsl')
Copy-Item -LiteralPath $surfacePixlIni -Destination (Join-Path $bridge 'SKSE\Plugins\SurfaceTides.ini')
Copy-Item -LiteralPath (Join-Path $repo 'docs\SURFACETIDES-UPSTREAM.md') -Destination (Join-Path $bridgeDocs 'INSTALL-AND-SOURCE.md')
foreach ($notice in @('LICENSE','THIRD_PARTY.md')) {
    $sourceNotice = Join-Path $surface $notice
    if (Test-Path -LiteralPath $sourceNotice) { Copy-Item -LiteralPath $sourceNotice -Destination $bridgeDocs }
}
if (Test-Path -LiteralPath (Join-Path $surface 'licenses')) {
    Copy-Item -LiteralPath (Join-Path $surface 'licenses') -Destination $bridgeDocs -Recurse
}
if ($SurfaceTidesSourceArchive) {
    $sourceArchive = (Resolve-Path -LiteralPath $SurfaceTidesSourceArchive).Path
    Write-Host "SurfaceTides source companion (upload separately; never nest it): $sourceArchive"
}

& (Join-Path $repo 'tools\TestPixlFomod.ps1') -PackageDirectory $output -SchemaPath (Join-Path $surface 'tools\fomod\ModConfig5.0.xsd')

$sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
if (!(Test-Path -LiteralPath $sevenZip)) { throw '7-Zip is required to create the release archive.' }
Push-Location $output
try { & $sevenZip a -tzip -mx=7 $archive '.' | Out-Host }
finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw '7-Zip archive creation failed.' }
& $sevenZip t $archive | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'FOMOD archive verification failed.' }
$hash = (Get-FileHash -LiteralPath $archive).Hash
Write-Host "PIXL FOMOD: $archive"
Write-Host "SHA256: $hash"
