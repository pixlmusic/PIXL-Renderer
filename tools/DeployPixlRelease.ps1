[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string[]]$IncludePattern = @('*'),
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$game = (Resolve-Path -LiteralPath $GameDirectory).Path

if (!(Test-Path -LiteralPath (Join-Path $game 'SkyrimSE.exe') -PathType Leaf)) {
    throw "Not a Skyrim SE installation: $game"
}
if (!(Test-Path -LiteralPath (Join-Path $game 'Data') -PathType Container)) {
    throw "Skyrim Data directory is missing: $game"
}
if (!$DryRun -and (Get-Process SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue)) {
    throw 'Close Skyrim before deployment. PIXL will not replace loaded DLLs or a cache being compiled.'
}

$data = (Resolve-Path -LiteralPath (Join-Path $game 'Data')).Path.TrimEnd('\')
& (Join-Path $PSScriptRoot 'VerifyPixlPackageManifest.ps1') -PackageDirectory $package
$manifest = Get-Content -LiteralPath (Join-Path $package 'PIXL-RENDERER.manifest.json') -Raw | ConvertFrom-Json

function Assert-Target([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if (!$full.StartsWith($data + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Deployment target escapes the game Data directory: $full"
    }
    $cursor = Split-Path -Parent $full
    while ($cursor -and $cursor.Length -ge $game.Length) {
        if ((Test-Path -LiteralPath $cursor) -and
            (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw "Deployment path contains a junction or symbolic link: $cursor"
        }
        if ([string]::Equals($cursor.TrimEnd('\'), $game.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor = Split-Path -Parent $cursor
    }
}

function Test-Included([string]$relative) {
    foreach ($pattern in $IncludePattern) {
        if ($relative -like $pattern) { return $true }
    }
    return $false
}

# User settings and the shader cache are runtime-owned during iterative deploys.
$candidates = @(foreach ($entry in $manifest.files) {
    if ($entry.path -like 'PIXL/PipelineLibrary/*' -or $entry.path -like '*/UserGraphics.json') { continue }
    if (!(Test-Included $entry.path)) { continue }
    $source = Join-Path $package $entry.path
    $target = [IO.Path]::GetFullPath((Join-Path $data $entry.path))
    Assert-Target $target
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Manifest source is missing: $source" }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ine $entry.sha256) {
        throw "Package source hash does not match its manifest: $($entry.path)"
    }
    $unchanged = (Test-Path -LiteralPath $target -PathType Leaf) -and
        ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ieq $entry.sha256)
    [pscustomobject]@{
        Relative = $entry.path
        Source = $source
        Target = $target
        Hash = $entry.sha256
        Existed = Test-Path -LiteralPath $target -PathType Leaf
        Unchanged = $unchanged
    }
})

if ($candidates.Count -eq 0) {
    throw "No manifest files matched the requested deployment patterns: $($IncludePattern -join ', ')"
}
$entries = @($candidates | Where-Object { !$_.Unchanged })
Write-Host ("Deployment plan: {0} selected, {1} changed, {2} already current." -f
    $candidates.Count, $entries.Count, ($candidates.Count - $entries.Count)) -ForegroundColor Cyan

if ($DryRun) {
    foreach ($entry in $entries) { Write-Host "  WOULD DEPLOY  $($entry.Relative)" }
    Write-Host 'DRY RUN PASS: package and target validated; no files changed.' -ForegroundColor Green
    return
}
if ($entries.Count -eq 0) {
    Write-Host "PASS: target is already current: $game" -ForegroundColor Green
    return
}

$backup = [IO.Path]::GetFullPath((Join-Path $repo (
    'build\deployment-backups\Release-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))))
$backupRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build\deployment-backups')).TrimEnd('\') + '\'
if (!$backup.StartsWith($backupRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup target.' }
New-Item -ItemType Directory -Path $backup -Force | Out-Null

$records = @($entries | ForEach-Object {
    [pscustomobject]@{
        Relative = $_.Relative
        Source = $_.Source
        Target = $_.Target
        Hash = $_.Hash
        Existed = $_.Existed
        Backup = Join-Path $backup $_.Relative
    }
})
$records | Export-Csv -LiteralPath (Join-Path $backup 'deployment.csv') -NoTypeInformation
$userConfig = Join-Path $data 'SKSE\Plugins\PIXL\Config\UserGraphics.json'
$userHash = if (Test-Path -LiteralPath $userConfig -PathType Leaf) {
    (Get-FileHash -LiteralPath $userConfig -Algorithm SHA256).Hash
} else { $null }
$attempted = [Collections.Generic.List[object]]::new()
$temporaryFiles = [Collections.Generic.List[string]]::new()

try {
    for ($index = 0; $index -lt $records.Count; ++$index) {
        $entry = $records[$index]
        Write-Progress -Activity 'Deploying PIXL Renderer' -Status $entry.Relative -PercentComplete (($index * 100) / $records.Count)
        Assert-Target $entry.Target
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Target) | Out-Null
        if ($entry.Existed) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Backup) | Out-Null
            Copy-Item -LiteralPath $entry.Target -Destination $entry.Backup -Force
        }
        $attempted.Add($entry)
        $temporary = $entry.Target + '.pixl-new-' + [Guid]::NewGuid().ToString('N')
        $temporaryFiles.Add($temporary)
        Copy-Item -LiteralPath $entry.Source -Destination $temporary -Force
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ine $entry.Hash) {
            throw "Temporary copy verification failed: $temporary"
        }
        if (Test-Path -LiteralPath $entry.Target) { Remove-Item -LiteralPath $entry.Target -Force }
        Move-Item -LiteralPath $temporary -Destination $entry.Target
        if ((Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash -ine $entry.Hash) {
            throw "Live verification failed: $($entry.Target)"
        }
    }
    Write-Progress -Activity 'Deploying PIXL Renderer' -Completed
    if ($userHash -and (Get-FileHash -LiteralPath $userConfig -Algorithm SHA256).Hash -ine $userHash) {
        throw 'UserGraphics unexpectedly changed.'
    }
} catch {
    Write-Progress -Activity 'Deploying PIXL Renderer' -Completed
    Write-Warning 'Deployment failed. Restoring every attempted file from the rollback set.'
    $rollback = @($attempted)
    [array]::Reverse($rollback)
    foreach ($entry in $rollback) {
        try {
            if (Test-Path -LiteralPath $entry.Target) { Remove-Item -LiteralPath $entry.Target -Force }
            if ($entry.Existed -and (Test-Path -LiteralPath $entry.Backup)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Target) | Out-Null
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Target -Force
            }
        } catch {
            Write-Warning "Rollback failed for $($entry.Relative): $($_.Exception.Message)"
        }
    }
    foreach ($temporary in $temporaryFiles) {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue }
    }
    throw
}

$dllEntry = $manifest.files | Where-Object { $_.path -eq $manifest.executable } | Select-Object -First 1
$marker = Join-Path $repo 'build\PIXL-LAST-DEPLOY.json'
[ordered]@{
    Timestamp = (Get-Date).ToString('o')
    Package = $package
    Game = $game
    Channel = $manifest.channel
    DllHash = if ($dllEntry) { $dllEntry.sha256 } else { $null }
    IncludePattern = $IncludePattern
    FilesChanged = $entries.Count
    Backup = $backup
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $marker -Encoding UTF8

Write-Host "PASS: $($entries.Count) changed file(s) deployed and verified." -ForegroundColor Green
Write-Host "Target : $game"
Write-Host "Backup : $backup"
Write-Host 'User settings and the live shader cache were preserved.'
