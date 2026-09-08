[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc,
    [string]$LightingSource
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$lighting = if ($LightingSource) { (Resolve-Path -LiteralPath $LightingSource).Path } else { Join-Path $root 'Lighting.hlsl' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\landscape-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$cases = [Collections.Generic.List[object]]::new()
foreach ($technique in @('LANDSCAPE', 'LODLANDSCAPE', 'LODOBJECTS', 'LODOBJECTSHD')) {
    foreach ($mask in 0..3) {
        $defines = @($technique)
        if ($mask -band 1) { $defines += 'TERRAIN_DETAIL' }
        if ($mask -band 2) { $defines += 'DISTANCE_BLEND' }
        $cases.Add([pscustomobject]@{ File = 'Lighting.hlsl'; Defines = $defines })
    }
}
foreach ($technique in @('SIMPLE', 'STENCIL', 'LOD', 'SPECULAR')) {
    foreach ($mask in 0..3) {
        $defines = @($technique, 'NUM_SPECULAR_LIGHTS=0')
        if ($mask -band 1) { $defines += 'HORIZON_BLEND' }
        if ($mask -band 2) { $defines += 'WATER_OPTICS' }
        $cases.Add([pscustomobject]@{ File = 'Water.hlsl'; Defines = $defines })
    }
}
foreach ($mask in 0..3) {
    $defines = @('LANDSCAPE', 'MATERIAL_LAYERS')
    if ($mask -band 1) { $defines += 'TERRAIN_DETAIL' }
    if ($mask -band 2) { $defines += 'DISTANCE_BLEND' }
    $cases.Add([pscustomobject]@{ File = 'Lighting.hlsl'; Defines = $defines })
}
$results = [Collections.Generic.List[object]]::new()
$caseIndex = 0
foreach ($case in $cases) {
    foreach ($stage in @('vs', 'ps')) {
        $defines = @('WINPC', 'DX11', $(if ($stage -eq 'vs') { 'VSHADER' } else { 'PSHADER' })) + $case.Defines
        $prefix = Join-Path $output ($caseIndex.ToString('D2') + '-' + $stage)
        $arguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', ($stage + '_5_0'), '/E', 'main', '/I', $root)
        foreach ($define in $defines) { $arguments += @('/D', $define) }
        $source = if ($case.File -eq 'Lighting.hlsl') { $lighting } else { Join-Path $root $case.File }
        $arguments += @('/Fo', ($prefix + '.cso'), $source)
        try {
            $ErrorActionPreference = 'Continue'
            $diagnostics = & $compiler @arguments 2>&1
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = 'Stop' }
        $diagnostics | Set-Content -LiteralPath ($prefix + '.log') -Encoding UTF8
        $results.Add([pscustomobject]@{ file = $case.File; stage = $stage; defines = ($defines -join ';'); exitCode = $code; log = ($prefix + '.log') })
        if ($code -ne 0) { Write-Host "FAIL $caseIndex/$stage : $diagnostics" }
    }
    ++$caseIndex
}
$results | Export-Csv -LiteralPath (Join-Path $output 'results.csv') -NoTypeInformation
foreach ($path in @('Lighting.hlsl', 'Water.hlsl', 'TerrainDetail/TerrainDetail.hlsli', 'Common/SharedData.hlsli')) {
    $source = if ($path -eq 'Lighting.hlsl') { $lighting } else { Join-Path $root $path }
    Write-Host "$path SHA256: $((Get-FileHash -LiteralPath $source).Hash) ($source)"
}
Write-Host "Landscape/water shader evidence: $output"
$failed = @($results | Where-Object { $_.exitCode -ne 0 })
if ($failed.Count) { throw "$($failed.Count) of $($results.Count) selected shader cases failed." }
Write-Host "PASS all $($results.Count) selected landscape/water VS/PS cases; not exhaustive engine permutations."
