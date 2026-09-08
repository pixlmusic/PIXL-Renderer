[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$PackageDirectory)

$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path.TrimEnd('\')
$manifestName = 'PIXL-RENDERER.manifest.json'
$manifestPath = Join-Path $packageRoot $manifestName
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.product -cne 'PIXL Renderer' -or
    $manifest.executable -cne 'SKSE/Plugins/PIXLRenderer.dll' -or
    $manifest.channel -notin @('LIVE-TEST', 'RELEASE-CANDIDATE', 'RELEASE') -or
    $manifest.cacheMode -notin @('compile-on-device', 'preloaded')) {
    throw 'Invalid PIXL package manifest identity or mode.'
}
if (@($manifest.files).Count -eq 0) { throw 'Package manifest contains no payloads.' }

# Reject links before hashing. A release archive must be self-contained, and a
# manifest path must never cause the validator to read outside the package.
$rootItem = Get-Item -LiteralPath $packageRoot -Force
if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Package root must not be a reparse point.'
}
$items = @(Get-ChildItem -LiteralPath $packageRoot -Force -Recurse)
if ($items | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }) {
    throw 'Package contains a reparse point.'
}
$payloads = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest.files) {
    $relative = [string]$entry.path
    # Canonical slash-separated names only: excludes rooted/drive/ADS paths,
    # traversal, ambiguous trailing dots/spaces, and Windows wildcard syntax.
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match '[\\:<>"|?*\x00-\x1f]' -or
        $relative -match '(^|/)(\.{1,2}|)(/|$)' -or $relative -match '[. ](/|$)' -or
        $relative -ieq $manifestName) {
        throw "Invalid manifest payload path: $relative"
    }
    if (-not $payloads.Add($relative)) { throw "Duplicate manifest payload: $relative" }
    $path = [IO.Path]::GetFullPath((Join-Path $packageRoot $relative))
    if (-not $path.StartsWith($packageRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest payload escapes package: $relative"
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing payload: $relative" }
    $expectedBytes = 0L
    if (-not [long]::TryParse([string]$entry.bytes, [ref]$expectedBytes) -or $expectedBytes -lt 0 -or
        [string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "Invalid manifest size/hash: $relative"
    }
    if ((Get-Item -LiteralPath $path).Length -ne $expectedBytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        throw "Payload integrity mismatch: $relative"
    }
}
foreach ($item in $items | Where-Object { -not $_.PSIsContainer }) {
    $relative = $item.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
    if ($relative -ine $manifestName -and -not $payloads.Contains($relative)) {
        throw "Unmanifested package file: $relative"
    }
}
if (-not $payloads.Contains($manifest.executable)) { throw 'Executable is not covered by the manifest.' }
Write-Host "PIXL manifest verified: $($payloads.Count) payloads ($($manifest.channel))"
