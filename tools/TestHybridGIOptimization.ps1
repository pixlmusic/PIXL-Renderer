param(
    [Parameter(Mandatory=$true)][string]$ShaderRoot,
    [Parameter(Mandatory=$true)][string]$Fxc
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo 'pipeline\Hybrid GI\Kernels\HybridGI\gi.cs.hlsl'
$output = Join-Path $repo 'build\gi-optimization\permutations'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$count = 0
foreach ($resolution in 0..2) {
    foreach ($temporal in 0..1) {
        foreach ($mode in 0..3) {
            foreach ($adaptive in 0..1) {
                $argsFxc = @('/nologo','/WX','/Ges','/O3','/T','cs_5_0','/E','main','/I',$ShaderRoot)
                $defines = @('COMPUTESHADER','WINPC','DX11')
                if ($resolution -eq 1) { $defines += 'HALF_RES' }
                if ($resolution -eq 2) { $defines += 'QUARTER_RES' }
                if ($temporal) { $defines += 'TEMPORAL_DENOISER' }
                if ($mode -ge 1) { $defines += 'GI' }
                if ($mode -ge 2) { $defines += 'GI_SPECULAR' }
                if ($mode -eq 3) { $defines += 'HYBRID_REFLECTIONS' }
                if ($adaptive) { $defines += 'ADAPTIVE_RAY_ALLOCATION' }
                foreach ($define in $defines) { $argsFxc += @('/D',$define) }
                $argsFxc += @('/Fo',(Join-Path $output "$count.cso"),$source)
                & $Fxc @argsFxc | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "GI compile failed: $($defines -join ', ')" }
                ++$count
            }
        }
    }
    Write-Host "GI permutations compiled: $count / 48"
}
Write-Host 'PASS: all 48 GI permutations, including legacy specular, with warnings treated as errors.'
