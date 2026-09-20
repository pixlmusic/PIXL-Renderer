[CmdletBinding()]
param(
    [ValidateSet('BuildDeploy', 'Deploy', 'PackageCommitPush')]
    [string]$Action = 'Deploy',
    [string]$Message = 'PIXL Renderer update'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $Root 'BuildRelease.bat'
$ReleaseDll = Join-Path $Root 'build\PIXL-12C\Release\PIXLRenderer.dll'
$ParticleShader = Join-Path $Root 'distribution\Shaders\Particle.hlsl'
$RainShaderRoot = Join-Path $Root 'pipeline\Rain Response\Kernels\RainResponse'
$WindowLifeShaderRoot = Join-Path $Root 'pipeline\WindowLife\Kernels\WindowLife'
$BackupRoot = Join-Path $Root 'build\deployment-backups'
$DeployStamp = Get-Date -Format 'yyyyMMdd-HHmmss'

$RainShaders = @(
    'Precipitation.hlsli', 'RainResponse.hlsli', 'RoofRunoffCompositeCS.hlsl',
    'RoofRunoffDetectCS.hlsl', 'RoofRunoffGenerateCS.hlsl', 'RoofRunoffResolveCS.hlsl',
    'WorldPrecipitation.hlsli', 'optimized-ggx.hlsli'
)
$DeployFiles = @(
    @{ Source = $ReleaseDll; Relative = 'Data\SKSE\Plugins\PIXLRenderer.dll' },
    @{ Source = $ParticleShader; Relative = 'Data\Shaders\Particle.hlsl' },
    # Lighting.hlsl owns the WindowLife call site. It must travel with the
    # matching WindowLife include or shader permutations fail at compile time.
    @{ Source = (Join-Path $Root 'distribution\Shaders\Lighting.hlsl'); Relative = 'Data\Shaders\Lighting.hlsl' }
)
foreach ($shader in $RainShaders) {
    $DeployFiles += @{ Source = Join-Path $RainShaderRoot $shader; Relative = "Data\Shaders\RainResponse\$shader" }
}

# WindowLife is compiled from this pipeline tree, but its source and authored
# atlases are also loaded directly by the runtime. Keep the live tree aligned
# with the same source used by the build instead of relying on an old package
# copy. The pipeline cache is synchronized separately below.
$WindowLifeFiles = @(Get-ChildItem -LiteralPath $WindowLifeShaderRoot -File | ForEach-Object {
    @{ Source = $_.FullName; Relative = "Data\Shaders\WindowLife\$($_.Name)" }
})
$DeployFiles += $WindowLifeFiles
$DeployFiles += @{ Source = (Join-Path $Root 'pipeline\WindowLife\Module.ini'); Relative = 'Data\Shaders\PIXL\Modules\WindowLife.ini' }

function Invoke-Native([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $File $($Arguments -join ' ')" }
}

function Get-GameRoots {
    $roots = @(
        $env:PIXL_SKYRIM_ROOT_1,
        $env:PIXL_SKYRIM_ROOT_2,
        $env:PIXL_SKYRIM_ROOT_3
    ) | Where-Object { $_ -and $_.Trim() }
    if ($roots.Count -eq 0) {
        throw 'No Skyrim installations were configured. Set PIXL_SKYRIM_ROOT_1/2/3 before running this script.'
    }
    $valid = @()
    foreach ($rootPath in $roots) {
        if (-not (Test-Path (Join-Path $rootPath 'SkyrimSE.exe'))) {
            if ($env:PIXL_SKIP_MISSING -eq '1') { Write-Warning "Skipping missing installation: $rootPath"; continue }
            throw "SkyrimSE.exe was not found: $rootPath. Set PIXL_SKIP_MISSING=1 only when intentional."
        }
        $valid += (Resolve-Path $rootPath).Path
    }
    if ($valid.Count -eq 0) { throw 'No Skyrim installations were found.' }
    return $valid
}

function Assert-Sources {
    foreach ($entry in $DeployFiles) {
        if (-not (Test-Path $entry.Source)) { throw "Required deployment source is missing: $($entry.Source)" }
    }
}

function Get-CacheSource {
    $candidates = @(Get-GameRoots | ForEach-Object {
        $candidate = Join-Path $_ 'Data\PIXL\PipelineLibrary'
        if (Test-Path $candidate) {
            $files = @(Get-ChildItem -LiteralPath $candidate -Recurse -File)
            if ($files.Count -gt 0) {
                $bytes = ($files | Measure-Object Length -Sum).Sum
                [pscustomobject]@{ Path = $candidate; Bytes = $bytes; Files = $files.Count }
            }
        }
    } | Sort-Object Bytes -Descending)
    if ($candidates.Count -eq 0) { throw 'No PipelineLibrary cache was found in any Skyrim installation.' }
    $selected = $candidates[0]
    if ($selected.Bytes -lt 100MB) {
        throw "The largest PipelineLibrary is only $($selected.Bytes) bytes; refusing to propagate an incomplete shader cache."
    }
    Write-Host "Using canonical shader cache: $($selected.Path) ($($selected.Files) files, $($selected.Bytes) bytes)" -ForegroundColor Cyan
    return $selected.Path
}

function Restore-Target([string]$GameRoot, [string]$BackupPath, [array]$Touched) {
    foreach ($entry in $Touched) {
        $destination = Join-Path $GameRoot $entry.Relative
        $saved = Join-Path $BackupPath $entry.Relative
        if ($entry.Kind -eq 'Tree') {
            if (Test-Path $destination) { Remove-Item $destination -Recurse -Force }
            if ($entry.Existed -and (Test-Path $saved)) {
                New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
                Copy-Item $saved $destination -Recurse -Force
            }
            continue
        }
        if ($entry.Existed -and (Test-Path $saved)) {
            New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
            Copy-Item $saved $destination -Force
        } elseif (Test-Path $destination) {
            Remove-Item $destination -Force
        }
    }
}

function Deploy-All {
    Assert-Sources
    $targets = @(Get-GameRoots)
    $backup = Join-Path $BackupRoot "Release-$DeployStamp"
    New-Item -ItemType Directory -Force $backup | Out-Null
    $cacheSource = Get-CacheSource
    # Stage the canonical cache before touching any target. This matters when
    # the largest cache is itself one of the destinations: replacing that
    # destination must never delete the source needed for the next install.
    $cacheDeploySource = Join-Path $backup 'PipelineLibrary-Source'
    Copy-Item $cacheSource $cacheDeploySource -Recurse -Force
    $deployed = @()
    $completedTargets = @()
    try {
        foreach ($game in $targets) {
            $safeName = ($game -replace '[:\\/ ]', '_').Trim('_')
            $targetBackup = Join-Path $backup $safeName
            New-Item -ItemType Directory -Force $targetBackup | Out-Null
            $touched = @()
            try {
                foreach ($entry in $DeployFiles) {
                    $destination = Join-Path $game $entry.Relative
                    $existed = Test-Path $destination
                    if ($existed) {
                        $saved = Join-Path $targetBackup $entry.Relative
                        New-Item -ItemType Directory -Force (Split-Path $saved) | Out-Null
                        Copy-Item $destination $saved -Force
                    }
                    $touched += @{ Relative = $entry.Relative; Existed = $existed }
                    New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
                    Copy-Item $entry.Source $destination -Force
                    $sourceHash = (Get-FileHash $entry.Source -Algorithm SHA256).Hash
                    $destHash = (Get-FileHash $destination -Algorithm SHA256).Hash
                    if ($sourceHash -ne $destHash) { throw "Hash verification failed: $destination" }
                }
                # Replace the entire cache tree so stale permutations cannot
                # survive on one installation. The full tree is backed up and
                # restored if any target fails, preserving rollback safety.
                $cacheRelative = 'Data\PIXL\PipelineLibrary'
                $cacheDestination = Join-Path $game $cacheRelative
                $cacheExisted = Test-Path $cacheDestination
                $cacheSaved = Join-Path $targetBackup $cacheRelative
                if ($cacheExisted) {
                    New-Item -ItemType Directory -Force (Split-Path $cacheSaved) | Out-Null
                    Copy-Item $cacheDestination $cacheSaved -Recurse -Force
                }
                $touched += @{ Relative = $cacheRelative; Existed = $cacheExisted; Kind = 'Tree' }
                if (Test-Path $cacheDestination) { Remove-Item $cacheDestination -Recurse -Force }
                New-Item -ItemType Directory -Force (Split-Path $cacheDestination) | Out-Null
                Copy-Item $cacheDeploySource $cacheDestination -Recurse -Force
                $sourceCacheFiles = @(Get-ChildItem -LiteralPath $cacheDeploySource -Recurse -File)
                $destCacheFiles = @(Get-ChildItem -LiteralPath $cacheDestination -Recurse -File)
                if ($sourceCacheFiles.Count -ne $destCacheFiles.Count) { throw "Pipeline cache file count mismatch: $game" }
                $deployed += $game
                $completedTargets += @{ Game = $game; Backup = $targetBackup; Touched = $touched }
                Write-Host "[OK] Deployed and verified: $game" -ForegroundColor Green
            } catch {
                Write-Warning "Deployment failed for $game. Restoring its previous files."
                Restore-Target $game $targetBackup $touched
                throw
            }
        }
        $marker = Join-Path $Root 'build\PIXL-LAST-DEPLOY.json'
        $windowLifeHash = (Get-FileHash (Join-Path $WindowLifeShaderRoot 'WindowLife.hlsli') -Algorithm SHA256).Hash
        [ordered]@{ Timestamp = (Get-Date).ToString('o'); DllHash = (Get-FileHash $ReleaseDll -Algorithm SHA256).Hash; WindowLifeShaderHash = $windowLifeHash; PipelineCacheSource = $cacheSource; Targets = $deployed } |
            ConvertTo-Json | Set-Content $marker -Encoding UTF8
        Write-Host "Deployment completed. Backups: $backup" -ForegroundColor Green
    } catch {
        foreach ($record in $completedTargets) {
            Write-Warning "Restoring previously deployed installation: $($record.Game)"
            Restore-Target $record.Game $record.Backup $record.Touched
        }
        Write-Error $_
        throw
    }
}

function Build-Release {
    if (-not (Test-Path $BuildScript)) { throw "Build script missing: $BuildScript" }
    Invoke-Native $env:ComSpec @('/d', '/c', "`"$BuildScript`" PIXL-12C")
    if (-not (Test-Path $ReleaseDll)) { throw "Build reported success but DLL is missing: $ReleaseDll" }
}

function Copy-Tree([string]$Source, [string]$Destination) {
    if (Test-Path $Source) {
        New-Item -ItemType Directory -Force $Destination | Out-Null
        Copy-Item (Join-Path $Source '*') $Destination -Recurse -Force
    }
}

function Package-Release {
    $marker = Join-Path $Root 'build\PIXL-LAST-DEPLOY.json'
    if (-not (Test-Path $marker)) { throw 'No successful deployment marker exists. Run BuildDeployAll.bat and test live first.' }
    $template = $env:PIXL_PACKAGE_TEMPLATE
    if (-not $template) {
        $template = Get-ChildItem (Join-Path $Root 'dist') -Directory -Filter 'PIXL-Renderer-1.0.3-RELEASE-*' |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    $package = Join-Path $Root "dist\PIXL-Renderer-1.0.3-LIVE-$DeployStamp"
    if ($template -and (Test-Path $template)) { Copy-Item $template $package -Recurse -Force }
    else { Copy-Item (Join-Path $Root 'distribution\*') $package -Recurse -Force }

    New-Item -ItemType Directory -Force (Join-Path $package 'SKSE\Plugins') | Out-Null
    Copy-Item $ReleaseDll (Join-Path $package 'SKSE\Plugins\PIXLRenderer.dll') -Force
    Copy-Tree (Join-Path $Root 'distribution\Shaders') (Join-Path $package 'Shaders')
    # Keep release archives on the same authoritative WindowLife source as
    # live deployment; distribution\Shaders does not own this feature tree.
    Copy-Tree $WindowLifeShaderRoot (Join-Path $package 'Shaders\WindowLife')
    Copy-Tree (Join-Path $Root 'distribution\SKSE') (Join-Path $package 'SKSE')

    $cacheSource = Get-GameRoots | ForEach-Object {
        $candidate = Join-Path $_ 'Data\PIXL\PipelineLibrary'
        if (Test-Path $candidate) { $m = Get-ChildItem $candidate -Recurse -File | Measure-Object Length -Sum; [pscustomobject]@{ Path = $candidate; Bytes = $m.Sum } }
    } | Sort-Object Bytes -Descending | Select-Object -First 1
    if ($cacheSource) { Copy-Tree $cacheSource.Path (Join-Path $package 'Data\PIXL\PipelineLibrary'); Write-Host "Included shader cache: $($cacheSource.Path)" }
    else { Write-Warning 'No PipelineLibrary cache found; package contains no preloaded cache.' }

    $zip = "$package.zip"
    Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -Force
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash
    Write-Host "Package: $zip" -ForegroundColor Green
    Write-Host "SHA256: $hash"
    Start-Process explorer.exe -ArgumentList "/select,`"$zip`""
    return $package
}

function Commit-Push {
    $status = @(git status --short)
    $status | Write-Host
    git diff --check
    if ($LASTEXITCODE -ne 0) { throw 'Whitespace errors found.' }
    Write-Host 'Review the status above. Only tracked modifications will be staged.' -ForegroundColor Yellow
    $confirm = Read-Host 'Type COMMIT to stage and continue'
    if ($confirm -cne 'COMMIT') { Write-Host 'Cancelled.'; return $false }
    git add -u
    if ($LASTEXITCODE -ne 0) { throw 'git add failed.' }
    git diff --cached --check
    git diff --cached --name-status
    $final = Read-Host 'Type PUSH to commit and push origin/main'
    if ($final -cne 'PUSH') { Write-Host 'Cancelled; staged changes remain.'; return $false }
    git commit -m $Message
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed.' }
    git push origin HEAD:main
    if ($LASTEXITCODE -ne 0) { throw 'git push failed.' }
    Write-Host 'Commit pushed successfully.' -ForegroundColor Green
    return $true
}

try {
    if ($Action -eq 'BuildDeploy') { Build-Release; Deploy-All }
    elseif ($Action -eq 'Deploy') { Deploy-All }
    else { if (Commit-Push) { Package-Release } }
    exit 0
} catch {
    Write-Error $_
    exit 1
}
