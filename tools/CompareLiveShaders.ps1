[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LiveShaderRoot,
    [string]$SourceRoot = "",
    [string]$ReportDirectory = ""
)

$ErrorActionPreference = "Stop"
if (-not $SourceRoot) {
    $SourceRoot = Join-Path $PSScriptRoot ".."
}
$sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$liveRootPath = (Resolve-Path -LiteralPath $LiveShaderRoot).Path.TrimEnd('\')

function Get-RelativePath([string]$Root, [string]$Path) {
    return $Path.Substring($Root.Length).TrimStart('\').Replace('\', '/')
}

function Get-FileDigest([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Test-BackupLike([string]$RelativePath) {
    return $RelativePath -match '(?i)(\.bak($|\.)|\.vortex_backup$|\.pre13|\.13[A-Z0-9_]*backup|(^|/)(backup|backups|_pixl_backups)(/|$))'
}

$sourceMap = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
$collisions = [System.Collections.Generic.List[object]]::new()

function Add-SourceFile([string]$RelativePath, [string]$FilePath, [string]$Origin) {
    $relative = $RelativePath.Replace('\', '/')
    $entry = [pscustomobject]@{
        RelativePath = $relative
        FilePath = $FilePath
        Origin = $Origin
    }
    if ($sourceMap.ContainsKey($relative)) {
        $previous = $sourceMap[$relative]
        $same = (Get-FileDigest $previous.FilePath) -eq (Get-FileDigest $FilePath)
        $collisions.Add([pscustomobject]@{
            RelativePath = $relative
            EarlierOrigin = $previous.Origin
            LaterOrigin = $Origin
            Identical = $same
        })
    }
    # StagePixlRendererStandalone.ps1 overlays later module trees on earlier files.
    $sourceMap[$relative] = $entry
}

$distributionRoot = Join-Path $sourceRootPath 'distribution\Shaders'
Get-ChildItem -LiteralPath $distributionRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    Add-SourceFile (Get-RelativePath $distributionRoot $_.FullName) $_.FullName 'distribution/Shaders'
}

Get-ChildItem -LiteralPath (Join-Path $sourceRootPath 'pipeline') -Directory | Sort-Object Name | ForEach-Object {
    $moduleDirectory = $_
    $descriptor = Join-Path $moduleDirectory.FullName 'Module.ini'
    if (-not (Test-Path -LiteralPath $descriptor)) {
        throw "Missing module descriptor: $descriptor"
    }
    $descriptorText = Get-Content -LiteralPath $descriptor -Raw
    $idMatch = [regex]::Match($descriptorText, '(?m)^\s*Id\s*=\s*([^\r\n]+?)\s*$')
    if (-not $idMatch.Success) {
        throw "Module descriptor has no Id: $descriptor"
    }
    $moduleId = $idMatch.Groups[1].Value.Trim()
    Add-SourceFile "PIXL/Modules/$moduleId.ini" $descriptor "pipeline/$($moduleDirectory.Name)/Module.ini"

    $kernelRoot = Join-Path $moduleDirectory.FullName 'Kernels'
    if (Test-Path -LiteralPath $kernelRoot) {
        Get-ChildItem -LiteralPath $kernelRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
            Add-SourceFile (Get-RelativePath $kernelRoot $_.FullName) $_.FullName "pipeline/$($moduleDirectory.Name)/Kernels"
        }
    }
}

$liveMap = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $liveRootPath -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative = Get-RelativePath $liveRootPath $_.FullName
    if ($liveMap.ContainsKey($relative)) {
        throw "Case-insensitive duplicate live shader path: $relative"
    }
    $liveMap[$relative] = $_
}

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($relative in @($sourceMap.Keys | Sort-Object)) {
    $source = $sourceMap[$relative]
    $sourceFile = Get-Item -LiteralPath $source.FilePath
    $sourceHash = Get-FileDigest $source.FilePath
    if ($liveMap.ContainsKey($relative)) {
        $liveFile = $liveMap[$relative]
        $liveHash = Get-FileDigest $liveFile.FullName
        $status = if ($sourceHash -eq $liveHash) { 'Match' } else { 'Different' }
        $rows.Add([pscustomobject]@{
            Status = $status
            RelativePath = $relative
            Extension = $sourceFile.Extension.ToLowerInvariant()
            BackupLike = Test-BackupLike $relative
            SourceOrigin = $source.Origin
            SourceBytes = $sourceFile.Length
            LiveBytes = $liveFile.Length
            SourceSha256 = $sourceHash
            LiveSha256 = $liveHash
            SourcePath = $sourceFile.FullName
            LivePath = $liveFile.FullName
        })
        $liveMap.Remove($relative) | Out-Null
    } else {
        $rows.Add([pscustomobject]@{
            Status = 'SourceOnly'
            RelativePath = $relative
            Extension = $sourceFile.Extension.ToLowerInvariant()
            BackupLike = Test-BackupLike $relative
            SourceOrigin = $source.Origin
            SourceBytes = $sourceFile.Length
            LiveBytes = $null
            SourceSha256 = $sourceHash
            LiveSha256 = ''
            SourcePath = $sourceFile.FullName
            LivePath = ''
        })
    }
}

foreach ($relative in @($liveMap.Keys | Sort-Object)) {
    $liveFile = $liveMap[$relative]
    $rows.Add([pscustomobject]@{
        Status = 'LiveOnly'
        RelativePath = $relative
        Extension = $liveFile.Extension.ToLowerInvariant()
        BackupLike = Test-BackupLike $relative
        SourceOrigin = ''
        SourceBytes = $null
        LiveBytes = $liveFile.Length
        SourceSha256 = ''
        LiveSha256 = Get-FileDigest $liveFile.FullName
        SourcePath = ''
        LivePath = $liveFile.FullName
    })
}

$summary = [ordered]@{
    sourceRoot = $sourceRootPath
    liveShaderRoot = $liveRootPath
    canonicalFiles = $sourceMap.Count
    liveFiles = (Get-ChildItem -LiteralPath $liveRootPath -File -Recurse).Count
    matches = @($rows | Where-Object Status -eq 'Match').Count
    different = @($rows | Where-Object Status -eq 'Different').Count
    sourceOnly = @($rows | Where-Object Status -eq 'SourceOnly').Count
    liveOnly = @($rows | Where-Object Status -eq 'LiveOnly').Count
    liveOnlyBackupLike = @($rows | Where-Object { $_.Status -eq 'LiveOnly' -and $_.BackupLike }).Count
    overlayCollisions = $collisions.Count
    nonidenticalOverlayCollisions = @($collisions | Where-Object { -not $_.Identical }).Count
}

if ($ReportDirectory) {
    $reportPath = [IO.Path]::GetFullPath($ReportDirectory)
    New-Item -ItemType Directory -Path $reportPath -Force | Out-Null
    $rows | Sort-Object Status, RelativePath | Export-Csv -LiteralPath (Join-Path $reportPath 'shader-reconciliation.csv') -NoTypeInformation -Encoding utf8
    $collisions | Sort-Object RelativePath | Export-Csv -LiteralPath (Join-Path $reportPath 'shader-overlay-collisions.csv') -NoTypeInformation -Encoding utf8
    $summary | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $reportPath 'shader-reconciliation-summary.json') -Encoding utf8
}

$summary | ConvertTo-Json -Depth 3

