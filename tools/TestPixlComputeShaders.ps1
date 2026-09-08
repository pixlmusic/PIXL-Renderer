[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)

$ErrorActionPreference = 'Stop'
$shaderRootPath = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\compute-validation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$jobs = [Collections.Generic.List[object]]::new()

# Matches HybridGI::CompileComputeShaders: 3 resolution modes and every
# combination of temporal, GI, experimental reflection, and adaptive switches.
$kernels = @(
    'prefilterDepths.cs.hlsl', 'prefilterRadiance.cs.hlsl', 'prefilterNormal.cs.hlsl',
    'radianceDisocc.cs.hlsl', 'gi.cs.hlsl', 'blur.cs.hlsl', 'blur.cs.hlsl',
    'upsample.cs.hlsl', 'worldCacheInject.cs.hlsl', 'worldCacheDecay.cs.hlsl',
    'hybridReflection.cs.hlsl', 'hybridReflectionDenoise.cs.hlsl'
)
foreach ($resolution in 0..2) {
    foreach ($mask in 0..15) {
        $defines = @()
        if ($resolution -eq 1) { $defines += 'HALF_RES' }
        if ($resolution -eq 2) { $defines += 'QUARTER_RES' }
        if ($mask -band 1) { $defines += 'TEMPORAL_DENOISER' }
        if ($mask -band 2) { $defines += 'GI' }
        if ($mask -band 4) { $defines += @('GI_SPECULAR', 'HYBRID_REFLECTIONS') }
        if ($mask -band 8) { $defines += 'ADAPTIVE_RAY_ALLOCATION' }
        for ($index = 0; $index -lt $kernels.Count; ++$index) {
            $kernelDefines = @($defines)
            if ($index -eq 0) { $kernelDefines += 'LINEAR_FILTER' }
            if ($index -eq 6) { $kernelDefines += 'ATROUS_STEP_2' }
            $jobs.Add([pscustomobject]@{ path = 'HybridGI/' + $kernels[$index]; defines = $kernelDefines })
        }
    }
}
# Matches Atmosphere's four shader getters and their loaded-module switches.
foreach ($mask in 0..7) {
    $defines = @()
    if ($mask -band 1) { $defines += 'RADIANT_GRID' }
    if ($mask -band 2) { $defines += 'TERRAIN_OCCLUSION' }
    if ($mask -band 4) { $defines += 'SKY_VEIL' }
    $jobs.Add([pscustomobject]@{ path = 'Atmosphere/VolumetricFogLightScatteringCS.hlsl'; defines = $defines })
}
$jobs.Add([pscustomobject]@{ path = 'Atmosphere/VolumetricFogMaterialCS.hlsl'; defines = @() })
$jobs.Add([pscustomobject]@{ path = 'Atmosphere/VolumetricFogMaterialCS.hlsl'; defines = @('RAIN_RESPONSE') })
foreach ($kernel in @('VolumetricFogConservativeDepthCS.hlsl', 'VolumetricFogIntegrationCS.hlsl')) {
    $jobs.Add([pscustomobject]@{ path = 'Atmosphere/' + $kernel; defines = @() })
}

$results = [Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $jobs.Count; ++$index) {
    $job = $jobs[$index]
    $prefix = Join-Path $output $index.ToString('D4')
    $arguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', 'cs_5_0', '/E', 'main', '/I', $shaderRootPath)
    foreach ($define in (@('COMPUTESHADER', 'WINPC', 'DX11') + $job.defines)) { $arguments += @('/D', $define) }
    $arguments += @('/Fo', ($prefix + '.cso'), (Join-Path $shaderRootPath $job.path))
    # These macros use #ifdef/defined, not numeric replacement values. FXC's
    # /D switch is equivalent for these branches to runtime's empty definition.
    $ErrorActionPreference = 'Continue'
    $compilerOutput = & $compiler @arguments 2>&1
    $compileExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $compilerOutput | Out-String | Set-Content -LiteralPath ($prefix + '.log') -Encoding UTF8
    $results.Add([pscustomobject]@{
        index = $index; path = $job.path; defines = ($job.defines -join ';'); exitCode = $compileExit
        sourceHash = (Get-FileHash -LiteralPath (Join-Path $shaderRootPath $job.path)).Hash
    })
    if ($compileExit -ne 0) { Write-Host "FAIL $index $($job.path): $prefix.log" }
    if (($index + 1) % 48 -eq 0) { Write-Host "Compiled $($index + 1)/$($jobs.Count) cases" }
}
$results | Export-Csv -LiteralPath (Join-Path $output 'results.csv') -NoTypeInformation
$failures = @($results | Where-Object { $_.exitCode -ne 0 })
Write-Host "Compute validation: $($results.Count) cases, $($failures.Count) failed; $output"
if ($failures.Count) { throw 'FXC validation failed; inspect case logs.' }
