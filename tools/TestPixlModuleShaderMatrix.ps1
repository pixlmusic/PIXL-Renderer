[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\module-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null

# Each define set mirrors a PIXL module branch in Lighting.hlsl or the exact
# compile call in the owning C++ module. This is a targeted integration matrix,
# not every game-engine technique permutation.
$cases = [Collections.Generic.List[object]]::new()
function Add-Case([string]$name, [string]$file, [string]$profile, [string[]]$defines, [string]$entry = 'main') {
    $cases.Add([pscustomobject]@{ name = $name; file = $file; profile = $profile; defines = $defines; entry = $entry })
}

$lighting = 'Lighting.hlsl'
$pixel = @('WINPC', 'DX11', 'PSHADER')
Add-Case 'SkinOptics.Actor' $lighting 'ps_5_0' ($pixel + @('SKINNED', 'SKIN', 'PIXL_SKIN'))
Add-Case 'StrandShading.Hair' $lighting 'ps_5_0' ($pixel + @('HAIR', 'STRAND_SHADING'))
Add-Case 'TissueDiffusion.Actor' $lighting 'ps_5_0' ($pixel + @('SKINNED', 'SKIN', 'PIXL_SKIN', 'TISSUE_DIFFUSION', 'DEFERRED'))
Add-Case 'ContactShadows.Deferred' $lighting 'ps_5_0' ($pixel + @('DEFERRED', 'CONTACT_SHADOWS'))
Add-Case 'NaturalLighting.RadiantGrid' $lighting 'ps_5_0' ($pixel + @('NATURAL_LIGHTING', 'RADIANT_GRID'))
Add-Case 'VolumeOcclusion.Lighting' $lighting 'ps_5_0' ($pixel + @('VOLUME_OCCLUSION'))
Add-Case 'TerrainSeam.Landscape' $lighting 'ps_5_0' ($pixel + @('LANDSCAPE', 'DEFERRED', 'TERRAIN_SEAM'))

$compute = @('COMPUTESHADER', 'WINPC', 'DX11')
$contact = 'ContactShadows/RaymarchCS.hlsl'
Add-Case 'ContactShadows.Raymarch.Standard' $contact 'cs_5_0' ($compute + @('SAMPLE_COUNT=64'))
Add-Case 'ContactShadows.Raymarch.TerrainSeam' $contact 'cs_5_0' ($compute + @('SAMPLE_COUNT=64', 'TERRAIN_SEAM'))
Add-Case 'TissueDiffusion.Extract' 'TissueDiffusion/DiffuseExtractionCS.hlsl' 'cs_5_0' $compute
Add-Case 'TissueDiffusion.Horizontal' 'TissueDiffusion/SeparableSSSCS.hlsl' 'cs_5_0' ($compute + @('HORIZONTAL'))
Add-Case 'TissueDiffusion.Vertical' 'TissueDiffusion/SeparableSSSCS.hlsl' 'cs_5_0' $compute
Add-Case 'TissueDiffusion.Burley' 'TissueDiffusion/SeparableSSSCS.hlsl' 'cs_5_0' ($compute + @('BURLEY'))
Add-Case 'RadiantGrid.Build' 'RadiantGrid/ClusterBuildingCS.hlsl' 'cs_5_0' $compute
Add-Case 'RadiantGrid.Cull' 'RadiantGrid/ClusterCullingCS.hlsl' 'cs_5_0' $compute
Add-Case 'VolumeOcclusion.DownsampleMip0' 'VolumeOcclusion/DownsampleShadowCS.hlsl' 'cs_5_0' ($compute + @('DOWNSAMPLE_SHADOW_MIP0'))
Add-Case 'VolumeOcclusion.DownsampleMip1' 'VolumeOcclusion/DownsampleShadowCS.hlsl' 'cs_5_0' ($compute + @('DOWNSAMPLE_SHADOW_MIP1'))
Add-Case 'VolumeOcclusion.BlurHorizontal' 'VolumeOcclusion/BlurShadowCS.hlsl' 'cs_5_0' ($compute + @('BLUR_HORIZONTAL'))
Add-Case 'VolumeOcclusion.BlurVertical' 'VolumeOcclusion/BlurShadowCS.hlsl' 'cs_5_0' ($compute + @('BLUR_VERTICAL'))
Add-Case 'TerrainSeam.DepthBlend' 'TerrainSeam/DepthBlend.hlsl' 'cs_5_0' $compute
Add-Case 'TerrainOcclusion.ShadowUpdate' 'TerrainOcclusion/ShadowUpdate.cs.hlsl' 'cs_5_0' $compute
Add-Case 'DistantLife.BuildMask' 'DistantLife/DistantLifeCS.hlsl' 'cs_5_0' $compute 'BuildMaskCS'
Add-Case 'DistantLife.Composite' 'DistantLife/DistantLifeCS.hlsl' 'cs_5_0' $compute 'CompositeCS'

$results = [Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $cases.Count; ++$index) {
    $case = $cases[$index]
    $source = Join-Path $root $case.file
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing staged shader payload for $($case.name): $source" }
    $arguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', $case.profile, '/E', $case.entry, '/I', $root)
    foreach ($define in $case.defines) { $arguments += @('/D', $define) }
    $log = Join-Path $output (('{0:D2}-{1}.log' -f $index, $case.name))
    $arguments += @('/Fo', (Join-Path $output (('{0:D2}.cso' -f $index))), $source)
    $previousAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $diagnostics = & $compiler @arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousAction }
    $diagnostics | Set-Content -LiteralPath $log -Encoding UTF8
    $results.Add([pscustomobject]@{
        name = $case.name; file = $case.file; profile = $case.profile
        defines = ($case.defines -join ';'); exitCode = $exitCode; log = $log
    })
    if ($exitCode -ne 0) { Write-Host "FAIL $($case.name): $log" }
}

$results | Export-Csv -LiteralPath (Join-Path $output 'results.csv') -NoTypeInformation
$failed = @($results | Where-Object { $_.exitCode -ne 0 })
Write-Host "Module shader matrix: $($results.Count) cases, $($failed.Count) failed; $output"
if ($failed.Count) { throw 'Strict module shader matrix failed; inspect the case logs.' }
Write-Host "PASS all $($results.Count) targeted runtime module shader cases with /WX /Ges."
