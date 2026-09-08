[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc,
    [string]$EntrySource = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$entry = if ($EntrySource) { (Resolve-Path -LiteralPath $EntrySource).Path } else { Join-Path $root 'RunGrass.hlsl' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\grass-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
# GetGrassShaderDefines supplies depth/alpha flags and loaded module defines.
# These are representative minimal/integrated configurations, not the full
# power set of module toggles, VR, custom macros or other engine shader families.
$integrated = @('GROUND_RESPONSE', 'CONTACT_SHADOWS', 'RADIANT_GRID', 'NATURAL_LIGHTING',
    'SKY_BOUNCE', 'WORLD_PROBES', 'AMBIENT_PROBE', 'ATMOSPHERE_PIPELINE')
foreach ($mask in 0..15) {
    foreach ($stage in @('vs', 'ps')) {
        $defines = @('WINPC', 'DX11', $(if ($stage -eq 'vs') { 'VSHADER' } else { 'PSHADER' }))
        if ($mask -band 1) { $defines += 'FOLIAGE_DYNAMICS' }
        if ($mask -band 2) { $defines += 'RENDER_DEPTH' }
        if ($mask -band 4) { $defines += 'DO_ALPHA_TEST' }
        if ($mask -band 8) { $defines += $integrated }
        $prefix = Join-Path $output ($mask.ToString('D2') + '-' + $stage)
        $compilerArguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', ($stage + '_5_0'), '/E', 'main', '/I', $root)
        foreach ($define in $defines) { $compilerArguments += @('/D', $define) }
        $compilerArguments += @('/Fo', ($prefix + '.cso'), $entry)
        $compileOutput = & $compiler @compilerArguments 2>&1
        $code = $LASTEXITCODE
        $compileOutput | Set-Content -LiteralPath ($prefix + '.log') -Encoding UTF8
        $results.Add([pscustomobject]@{ stage = $stage; defines = ($defines -join ';'); exitCode = $code; log = ($prefix + '.log') })
        if ($code -ne 0) { Write-Host "FAIL $mask/$stage : $compileOutput" }
    }
}
$results | Export-Csv -LiteralPath (Join-Path $output 'results.csv') -NoTypeInformation
Write-Host "RunGrass source: $entry"
Write-Host "RunGrass SHA256: $((Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash)"
Write-Host "Grass shader evidence: $output"
$failed = @($results | Where-Object { $_.exitCode -ne 0 })
if ($failed.Count) { throw "$($failed.Count) of $($results.Count) grass shader cases failed." }
Write-Host "PASS all $($results.Count) grass vertex/pixel cases with strictness, validation and warnings-as-errors."
