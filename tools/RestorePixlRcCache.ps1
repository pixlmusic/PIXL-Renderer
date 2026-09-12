[CmdletBinding()]
param([switch]$Install)
$ErrorActionPreference = 'Stop'
if (Get-Process SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue) { throw 'Close Skyrim before cache restoration.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$old = Join-Path $repo 'build\deployment-backups\RC-DayNight-20260912-122615-9db15f481ba94e1f8f9ee399e34f60ef\PipelineLibrary'
$live = 'H:\The Elder Scrolls - Skyrim - Special Edition\Data\PIXL\PipelineLibrary'
$shaders = 'H:\The Elder Scrolls - Skyrim - Special Edition\Data\Shaders'
$old = (Resolve-Path -LiteralPath $old).Path
$live = (Resolve-Path -LiteralPath $live).Path
foreach ($root in @($old,$live)) {
    $cursor = $root
    while ($cursor) {
        if (((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse root requires review: $cursor" }
        $cursor = Split-Path -Parent $cursor
    }
    if (Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }) { throw "Linked cache contents require review: $root" }
}
# The current run's metadata records WindowLife 0-7-2. All other identities must match.
$oldIni = (Get-Content -LiteralPath (Join-Path $old 'Library.ini') -Raw).Trim()
$newIni = (Get-Content -LiteralPath (Join-Path $live 'Library.ini') -Raw).Trim()
if ($oldIni.Replace('Version = 0-7-1','Version = 0-7-2') -cne $newIni) { throw 'Cache metadata differs beyond the reviewed WindowLife version change.' }
$affected = @('Lighting','Sky','ISSAOCompositeFog','ISSAOCompositeSAOFog')
$retained = 0; $excluded = 0; $newFiles = 0
$work = Join-Path $repo ('build\cache-restores\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
$prepared = Join-Path $work 'PipelineLibrary'
New-Item -ItemType Directory -Path $prepared | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $old -File -Recurse) {
    $relative = $file.FullName.Substring($old.Length + 1)
    $parts = $relative.Split('\')
    if ($parts.Length -ge 4 -and $parts[0] -eq 'Pixel' -and $parts[2] -in $affected) { ++$excluded; continue }
    $dest = Join-Path $prepared $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $dest
    # Only PS logic changed in these entry points. Preserve valid VS bytecode
    # without the coarse top-level file timestamp forcing a VS rebuild.
    if ($parts.Length -ge 4 -and $parts[0] -eq 'Vertex' -and $parts[2] -in $affected) {
        (Get-Item -LiteralPath $dest).LastWriteTimeUtc = [DateTime]::UtcNow
    }
    if ((Get-FileHash -LiteralPath $dest).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Restore hash mismatch: $relative" }
    ++$retained
}
foreach ($file in Get-ChildItem -LiteralPath $live -File -Recurse) {
    $relative = $file.FullName.Substring($live.Length + 1)
    $parts = $relative.Split('\')
    if ($parts.Length -ge 4 -and $parts[0] -eq 'Pixel' -and $parts[2] -in $affected) {
        $entryPoint = if ($parts[2] -like 'ISSAOComposite*') { 'ISSAOComposite.hlsl' } else { $parts[2] + '.hlsl' }
        if ($file.LastWriteTimeUtc -lt (Get-Item -LiteralPath (Join-Path $shaders $entryPoint)).LastWriteTimeUtc) { throw "New cache entry predates changed source: $relative" }
    }
    $dest = Join-Path $prepared $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
    if ((Get-FileHash -LiteralPath $dest).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "New cache hash mismatch: $relative" }
    ++$newFiles
}
Write-Host "Prepared and hash-verified: $retained old files retained; $excluded stale affected pixel files excluded; $newFiles current files merged."
if ($Install) {
    if (Get-Process SkyrimSE,SkyrimVR -ErrorAction SilentlyContinue) { throw 'Skyrim started; restoration not installed.' }
    # Both directory move targets are fully resolved and narrowly validated.
    $expectedLive = 'H:\The Elder Scrolls - Skyrim - Special Edition\Data\PIXL\PipelineLibrary'
    if ($live -cne $expectedLive -or !$prepared.StartsWith($repo + '\build\cache-restores\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected move target.' }
    Move-Item -LiteralPath $live -Destination (Join-Path $work 'NewlyCompiledCacheBackup')
    Move-Item -LiteralPath $prepared -Destination $live
    Write-Host "RESTORED: $live"
    Write-Host "Current-run backup: $work\NewlyCompiledCacheBackup"
} else { Write-Host "Prepared only: $prepared" }
