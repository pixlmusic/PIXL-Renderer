[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$RuntimeDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$names = @('sl.interposer.dll', 'sl.common.dll', 'sl.dlss_g.dll', 'sl.reflex.dll', 'sl.pcl.dll')
$expected = $null
foreach ($name in $names) {
    $path = Join-Path $root $name
    $file = Get-Item -LiteralPath $path
    $info = $file.VersionInfo
    $version = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
    if ($version -eq '0.0.0.0') { throw "Missing version metadata: $path" }
    if ($null -eq $expected) { $expected = $version }
    if ($version -ne $expected) { throw "Mixed Streamline runtime: $name is $version, expected $expected" }
    [pscustomobject]@{ File=$name; Version=$version; SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
}
# NGX has its own version scheme; it must not be compared with Streamline.
$ngx = Join-Path $root 'nvngx_dlssg.dll'
if (-not (Test-Path -LiteralPath $ngx -PathType Leaf)) { throw "Missing NGX DLSS-G runtime: $ngx" }
Write-Host "PASS: matching Streamline $expected package. GPU execution/SM86 compatibility require SidecarSmoke and game testing."
