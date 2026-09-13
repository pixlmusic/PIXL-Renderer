[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SourceLibrary,
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceLibrary).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Output must be a new directory; existing caches are never overwritten.' }
$metadata = Get-Content -LiteralPath (Join-Path $source 'Library.ini') -Raw
if ($metadata -notmatch 'ShaderABI\s*=\s*PIXL.SharedBuffers.20260902.1') { throw 'Unexpected shader ABI.' }
$updates = @{ WindowLife=@('0-7-3','0-7-4'); MaterialLayers=@('1-3-2','1-3-3') }
foreach ($id in $updates.Keys) {
    $pattern = '(?ms)(^\[' + $id + '\][^\[]*?^Version\s*=\s*)' + [regex]::Escape($updates[$id][0]) + '(?=\s|$)'
    if ($metadata -notmatch $pattern) { throw "This scoped exporter requires the previous $id version $($updates[$id][0]); use a fresh validated snapshot for another update." }
    $metadata = [regex]::Replace($metadata, $pattern, '${1}' + $updates[$id][1])
}
New-Item -ItemType Directory -Path $output | Out-Null
$kept = 0
$excluded = 0
foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File -Filter '*.pixlbin') {
    $relative = $file.FullName.Substring($source.TrimEnd('\').Length + 1)
    if ($relative -notmatch '^(Vertex|Pixel|Compute)[\\/]') { throw "Unknown cache path: $relative" }
    $affected = $false
    if ($relative -match '^Pixel[\\/].*[\\/]Lighting[\\/]') {
        if ($file.BaseName -notmatch '^[0-9A-Fa-f]{8}') { throw "Invalid descriptor: $relative" }
        $descriptor = [Convert]::ToUInt32($file.BaseName.Substring(0,8),16)
        # Union of MaterialLayers/WindowLife AffectsCachedShader contracts.
        # No vertex, compute or unrelated pixel family is touched.
        $affected = (($descriptor -shr 24) -band 63) -in @(0,1,2,3,7,8,10,11,16,19)
    }
    if ($affected) { ++$excluded; continue }
    $target = Join-Path $output $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Cache copy mismatch: $relative" }
    ++$kept
}
[IO.File]::WriteAllText((Join-Path $output 'Library.ini'), $metadata, [Text.UTF8Encoding]::new($false))
Write-Output "PASS: exported $kept reusable stages; excluded $excluded affected stages. Source cache untouched."
