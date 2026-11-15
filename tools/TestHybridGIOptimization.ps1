param(
    [Parameter(Mandatory=$true)][string]$ShaderRoot,
    [Parameter(Mandatory=$true)][string]$Fxc,
    [switch]$CacheIntelligence
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo 'pipeline\Hybrid GI\Kernels\HybridGI\gi.cs.hlsl'
$sourceText = Get-Content -LiteralPath $source -Raw
if ($sourceText -notmatch 'smoothstep\(0\.006f,\s*0\.045f,\s*absoluteRadianceChange\)' -or
	$sourceText -notmatch 'historyReactivity\s*=\s*smoothstep\(0\.10f,\s*0\.45f,\s*relativeRadianceChange\)\s*\*') {
	throw 'HybridGI temporal reactivity must suppress sub-threshold radiance noise while retaining relative-change response.'
}
if ($sourceText -notmatch 'cacheConfidence\s*=\s*saturate\(cacheHitRatio\s*\*\s*2\.2f\)' -or
	$sourceText -notmatch 'cacheBlend\s*=\s*WorldCacheStrength\s*\*\s*0\.60f\s*\*\s*cacheConfidence' -or
	$sourceText -match 'cacheBlend\s*=.*(giCoverage|screenCoverage|screenMiss|cacheRole)') {
	throw 'HybridGI world-cache contribution must use cache confidence and remain independent of the stochastic AO horizon mask.'
}
if ($sourceText -notmatch 'o_cacheContributionY\s*=\s*max\(cacheY\.x\s*\*\s*cacheBlend' -or
	$sourceText -notmatch 'reactiveResponse\s*=\s*lerp\(0\.55f,\s*0\.16f,\s*cacheSignalShare\)') {
	throw 'World-cache-dominant GI history must use the damped temporal response.'
}
$injection = Get-Content -LiteralPath (Join-Path $repo 'pipeline\Hybrid GI\Kernels\HybridGI\worldCacheInject.cs.hlsl') -Raw
if ($injection -match 'if\s*\(staleLighting\)\s*historyValid\s*=\s*false' -or
	$injection -notmatch 'if\s*\(staleLighting\)\s*response\s*=\s*max\(response,\s*0\.12f\)' -or
	$injection -notmatch 'if\s*\(CacheScheduleEnabled\s*!=\s*0u\)\s*response\s*=\s*min\(response,\s*0\.18f\)') {
	throw 'HybridGI cache refresh must blend invalidated light history and bound sparse scheduled radiance steps.'
}
$cacheHelpers = Get-Content -LiteralPath (Join-Path $repo 'pipeline\Hybrid GI\Kernels\HybridGI\worldCache.hlsli') -Raw
if ($injection -notmatch 'WorldCacheVarianceDampedResponse\(response,\s*oldRelativeVariance\)' -or
	$cacheHelpers -notmatch 'return\s+saturate\(response\)\s*\*\s*rcp\(1\.0f\s*\+\s*max\(relativeVariance,\s*0\.0f\)\)') {
	throw 'Intelligent cache refresh must damp uncertain-cell update steps according to measured variance.'
}
if ($injection -notmatch 'WorldCacheDecayLightChange\(oldLightChangeEvidence\)' -or
	$cacheHelpers -notmatch 'return\s+evidence\s*>\s*64u\s*\?\s*evidence\s*-\s*64u\s*:\s*0u') {
	throw 'Emitter-change reliability must fade over successive cache publications instead of clearing in one step.'
}
$output = Join-Path $repo 'build\gi-optimization\permutations'
New-Item -ItemType Directory -Path $output -Force | Out-Null
# The fixed overlap must rise monotonically with valid world-cache confidence,
# remain zero without confidence, and never exceed its 60% base contribution.
$previousBlend = 0.0
foreach ($confidence in @(0.0, 0.10, 0.25, 0.50, 0.75, 1.0)) {
	$blend = 0.60 * [Math]::Min(1.0, $confidence * 2.2)
	if ($blend -lt $previousBlend - 1e-10) { throw 'World-cache confidence blend is not monotonic' }
	if ($blend -gt 0.60 + 1e-10) { throw 'World-cache contribution exceeds its bounded overlap' }
	$previousBlend = $blend
}
if ($previousBlend -lt 0.60 - 1e-10) { throw 'World-cache confidence does not reach the configured overlap bound' }
$count = 0
foreach ($resolution in 0..2) {
    foreach ($temporal in 0..1) {
        foreach ($mode in 0..3) {
            foreach ($adaptive in 0..1) {
                $argsFxc = @('/nologo','/WX','/Ges','/O3','/T','cs_5_0','/E','main','/I',$ShaderRoot)
                $defines = @('COMPUTESHADER','WINPC','DX11')
                if ($CacheIntelligence) { $defines += 'CACHE_INTELLIGENCE' }
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
