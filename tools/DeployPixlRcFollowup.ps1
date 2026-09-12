[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$UserConfigPath = '',
    [string]$ExpectedUserConfigHash = ''
)
$ErrorActionPreference = 'Stop'
if (Get-Process SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue) { throw 'Close Skyrim before deployment.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
if (!(Test-Path -LiteralPath (Join-Path $game 'SkyrimSE.exe'))) { throw 'Not a Skyrim SE installation.' }
$data = (Resolve-Path -LiteralPath (Join-Path $game 'Data')).Path.TrimEnd('\')
& (Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1') -PackageDirectory $package
$manifest = Get-Content -LiteralPath (Join-Path $package 'PIXL-RENDERER.manifest.json') -Raw | ConvertFrom-Json
# Deliberately excludes UserGraphics, profiles, unrelated mods and sidecar DLLs.
$relativePaths = @(
    'SKSE/Plugins/PIXLRenderer.dll',
    'SKSE/Plugins/PIXL/Config/RendererDefaults.json',
    'Shaders/Common/SharedData.hlsli',
    'Shaders/ISSAOComposite.hlsl',
    'Shaders/Lighting.hlsl',
    'Shaders/Sky.hlsl',
    'Shaders/PIXL/Modules/WindowLife.ini',
    'Shaders/WindowLife/WindowLife.hlsli',
    'Shaders/WindowLife/OutdoorAtlas.png',
    'Shaders/WindowLife/OutdoorAtlas_2k.dds',
    'Shaders/WindowLife/OutdoorAtlasNight.png',
    'Shaders/WindowLife/OutdoorAtlasNight_2k.dds'
)
function Assert-LivePath([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if (!$full.StartsWith($data + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside game Data: $path" }
    $cursor = $full
    while ($cursor.Length -ge $data.Length) {
        if (Test-Path -LiteralPath $cursor) {
            if (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point requires manual review: $cursor" }
        }
        $cursor = Split-Path -Parent $cursor
    }
}
$backup = Join-Path $repo ('build\deployment-backups\RC-DayNight-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
$entries = foreach ($relative in $relativePaths) {
    $target = [IO.Path]::GetFullPath((Join-Path $data $relative))
    Assert-LivePath $target
    $entry = @($manifest.files | Where-Object { $_.path -ceq $relative })
    if ($entry.Count -ne 1) { throw "Missing manifest entry: $relative" }
    [pscustomobject]@{ Relative=$relative; Target=$target; Source=(Join-Path $package $relative); Backup=(Join-Path $backup $relative); Hash=$entry[0].sha256; Existed=(Test-Path -LiteralPath $target) }
}
New-Item -ItemType Directory -Path $backup | Out-Null
$userConfig = Join-Path $data 'SKSE\Plugins\PIXL\Config\UserGraphics.json'
$userHash = if (Test-Path -LiteralPath $userConfig) { (Get-FileHash -LiteralPath $userConfig).Hash } else { $null }
if ($UserConfigPath) {
    if (!$ExpectedUserConfigHash -or $userHash -ine $ExpectedUserConfigHash) { throw 'Live user config changed since snapshot; do not overwrite.' }
    $tunedConfig = (Resolve-Path -LiteralPath $UserConfigPath).Path
    Get-Content -LiteralPath $tunedConfig -Raw | ConvertFrom-Json | Out-Null
    Assert-LivePath $userConfig
    $entries += [pscustomobject]@{ Relative='SKSE/Plugins/PIXL/Config/UserGraphics.json'; Target=$userConfig; Source=$tunedConfig; Backup=(Join-Path $backup 'SKSE/Plugins/PIXL/Config/UserGraphics.json'); Hash=(Get-FileHash -LiteralPath $tunedConfig).Hash; Existed=$true }
}
$entries | Export-Csv -LiteralPath (Join-Path $backup 'deployment.csv') -NoTypeInformation
$changedCount = 0
foreach ($entry in $entries) {
    if ($entry.Existed -and (Get-FileHash -LiteralPath $entry.Target).Hash -ieq $entry.Hash) { continue }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Target),(Split-Path -Parent $entry.Backup) | Out-Null
    # Copy first, then move the old link aside. Never overwrite a Vortex hardlink in place.
    $temporary = $entry.Target + '.pixl-new-' + [Guid]::NewGuid().ToString('N')
    Copy-Item -LiteralPath $entry.Source -Destination $temporary
    if ((Get-FileHash -LiteralPath $temporary).Hash -ine $entry.Hash) { throw "Copy verification failed: $temporary" }
    if ($entry.Existed) { Move-Item -LiteralPath $entry.Target -Destination $entry.Backup }
    Move-Item -LiteralPath $temporary -Destination $entry.Target
    if ((Get-FileHash -LiteralPath $entry.Target).Hash -ine $entry.Hash) { throw "Live verification failed: $($entry.Target)" }
    Write-Host "Deployed: $($entry.Relative)"
    ++$changedCount
}
# Cache is intentionally untouched. Module metadata performs targeted invalidation.
$expectedUserHash = if ($UserConfigPath) { (Get-FileHash -LiteralPath $tunedConfig).Hash } else { $userHash }
if ($expectedUserHash -and (Get-FileHash -LiteralPath $userConfig).Hash -ne $expectedUserHash) { throw 'UserGraphics unexpectedly changed.' }
foreach ($entry in $entries) {
    if ((Get-FileHash -LiteralPath $entry.Target).Hash -ine $entry.Hash) { throw "Final verification failed: $($entry.Relative)" }
}
Write-Host "PASS: $changedCount files updated and all selected files verified. Cache untouched. Backup: $backup"
