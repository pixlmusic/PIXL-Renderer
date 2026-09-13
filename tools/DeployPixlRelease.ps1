[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$GameDirectory
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
$backup = Join-Path $repo ('build\deployment-backups\Release-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
$backup = [IO.Path]::GetFullPath($backup)
if (!$backup.StartsWith($repo.TrimEnd('\') + '\build\deployment-backups\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup target.' }
function Assert-Target([string]$path) {
    if (!$path.StartsWith($data + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside game Data: $path" }
    $cursor = $path
    while ($cursor -and $cursor.Length -ge $game.Length) {
        if ((Test-Path -LiteralPath $cursor) -and (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) { throw "Reparse point in deployment path: $cursor" }
        $cursor = Split-Path -Parent $cursor
    }
}
# Never replace live caches, user preferences or mod-manager plugin activation.
$entries = @(foreach ($entry in $manifest.files) {
    if ($entry.path -like 'PIXL/PipelineLibrary/*' -or $entry.path -like '*/UserGraphics.json') { continue }
    $target = [IO.Path]::GetFullPath((Join-Path $data $entry.path))
    Assert-Target $target
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -ieq $entry.sha256) { continue }
    [pscustomobject]@{ Relative=$entry.path; Target=$target; Source=(Join-Path $package $entry.path); Backup=(Join-Path $backup $entry.path); Hash=$entry.sha256; Existed=(Test-Path -LiteralPath $target) }
})
New-Item -ItemType Directory -Path $backup | Out-Null
$entries | Export-Csv -LiteralPath (Join-Path $backup 'deployment.csv') -NoTypeInformation
$userConfig = Join-Path $data 'SKSE\Plugins\PIXL\Config\UserGraphics.json'
$userHash = if (Test-Path -LiteralPath $userConfig) { (Get-FileHash -LiteralPath $userConfig).Hash } else { $null }
foreach ($entry in $entries) {
    Assert-Target $entry.Target
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Target),(Split-Path -Parent $entry.Backup) | Out-Null
    $temporary = $entry.Target + '.pixl-new-' + [Guid]::NewGuid().ToString('N')
    Copy-Item -LiteralPath $entry.Source -Destination $temporary
    if ((Get-FileHash -LiteralPath $temporary).Hash -ine $entry.Hash) { throw "Copy verification failed: $temporary" }
    # Move old Vortex hardlinks aside instead of writing through them.
    if ($entry.Existed) { Move-Item -LiteralPath $entry.Target -Destination $entry.Backup }
    Move-Item -LiteralPath $temporary -Destination $entry.Target
    if ((Get-FileHash -LiteralPath $entry.Target).Hash -ine $entry.Hash) { throw "Live verification failed: $($entry.Target)" }
}
if ($userHash -and (Get-FileHash -LiteralPath $userConfig).Hash -ine $userHash) { throw 'UserGraphics unexpectedly changed.' }
Write-Host "PASS: $($entries.Count) files deployed. User settings/cache preserved. Backup: $backup"
