[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\materialforge-shader-tests-' + [Guid]::NewGuid().ToString('N'))
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
$weather = @('RAIN_RESPONSE', 'WORLD_PROBES', 'SKY_BOUNCE', 'GROUND_RESPONSE')
$variants = @(
    @{name='Legacy'; flags=@('SPECULAR')},
    @{name='LegacyEnvironment'; flags=@('SPECULAR','ENVMAP')},
    @{name='ComplexMaterial'; flags=@('SPECULAR','ENVMAP','MATERIAL_LAYERS')},
    @{name='LegacyParallax'; flags=@('SPECULAR','PARALLAX','MATERIAL_LAYERS')},
    @{name='Multilayer'; flags=@('MULTI_LAYER_PARALLAX','SPECULAR','MATERIAL_LAYERS')},
    @{name='PBR'; flags=@('MATERIAL_FORGE')},
    @{name='PBRProjected'; flags=@('MATERIAL_FORGE','PROJECTED_UV','GLINT')},
    @{name='Terrain'; flags=@('LANDSCAPE','MATERIAL_LAYERS')},
    @{name='PBRTerrain'; flags=@('LANDSCAPE','MATERIAL_FORGE','MATERIAL_LAYERS','GLINT')},
    @{name='TerrainSnow'; flags=@('LANDSCAPE','MATERIAL_FORGE','SNOW')},
    @{name='Tree'; flags=@('TREE_ANIM','DO_ALPHA_TEST','MATERIAL_FORGE')},
    @{name='Skin'; flags=@('SKIN','SKINNED','PIXL_SKIN','ACTOR_SURFACE_EFFECTS')},
    @{name='Hair'; flags=@('HAIR','SKINNED','STRAND_SHADING')},
    @{name='Eye'; flags=@('EYE')},
    @{name='Window'; flags=@('SPECULAR','ENVMAP','MATERIAL_LAYERS','PIXL_WINDOW_LIFE')},
    @{name='PBRWindow'; flags=@('MATERIAL_FORGE','PIXL_WINDOW_LIFE')},
    @{name='SkinnedPBR'; flags=@('SKINNED','MATERIAL_FORGE','ACTOR_SURFACE_EFFECTS')},
    @{name='LOD'; flags=@('LODLANDSCAPE','MATERIAL_FORGE')}
)
foreach ($variant in $variants) {
    Add-Case ($variant.name + '.Dry') $lighting 'ps_5_0' ($pixel + $variant.flags)
    Add-Case ($variant.name + '.WetDeferred') $lighting 'ps_5_0' ($pixel + $variant.flags + $weather + @('DEFERRED'))
}
Add-Case 'PBR.WetForward' $lighting 'ps_5_0' ($pixel + @('MATERIAL_FORGE') + $weather)
Add-Case 'Window.WetForward' $lighting 'ps_5_0' ($pixel + @('SPECULAR','ENVMAP','PIXL_WINDOW_LIFE','MATERIAL_LAYERS') + $weather)
Add-Case 'PBR.Vertex' $lighting 'vs_5_0' @('WINPC','DX11','VSHADER','MATERIAL_FORGE')
Add-Case 'Terrain.Vertex' $lighting 'vs_5_0' @('WINPC','DX11','VSHADER','MATERIAL_FORGE','LANDSCAPE')
Add-Case 'Terrain.Stochastic' $lighting 'ps_5_0' ($pixel + $weather + @('DEFERRED','LANDSCAPE','MATERIAL_FORGE','MATERIAL_LAYERS','TERRAIN_DETAIL','TERRAIN_SEAM'))
Add-Case 'TerrainLOD.Stochastic' $lighting 'ps_5_0' ($pixel + @('LODLANDSCAPE','DISTANCE_BLEND','TERRAIN_DETAIL'))
Add-Case 'Object.TerrainSeam' $lighting 'ps_5_0' ($pixel + $weather + @('DEFERRED','MATERIAL_FORGE','TERRAIN_SEAM'))
Add-Case 'Fur' $lighting 'ps_5_0' ($pixel + $weather + @('DEFERRED','SKINNED','AUTO_FUR','MATERIAL_FORGE'))
Add-Case 'Water.Rain' 'Water.hlsl' 'ps_5_0' ($pixel + @('RAIN_RESPONSE','WATER_OPTICS','SPECULAR','REFLECTIONS','REFRACTIONS'))

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
Write-Host "PASS all $($results.Count) targeted Material Forge integration shader cases with /WX /Ges."
