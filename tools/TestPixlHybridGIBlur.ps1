[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$includeRoot = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$source = Join-Path $repo 'pipeline/Hybrid GI/Kernels/HybridGI/blur.cs.hlsl'
$text = [string](Get-Content -LiteralPath $source -Raw)
if (-not $text.Contains('N.z >= 0.0f ? 1.0f : -1.0f') -or
    -not $text.Contains('saturate((accumFrames * 255.0f) / max((float)MaxAccumFrames, 1.0f))')) {
    throw 'Blur numeric tests need updating to match the production expressions.'
}
# Reference-math checks, not GPU image-quality or timing measurements.
function Dot($a, $b) { return $a[0]*$b[0] + $a[1]*$b[1] + $a[2]*$b[2] }
$normals = [Collections.Generic.List[object]]::new()
foreach ($n in @(@(1,0,0), @(-1,0,0), @(0,1,0), @(0,-1,0), @(0,0,1), @(0,0,-1))) { $normals.Add($n) }
$random = [Random]::new(90610)
for ($i=0; $i -lt 1000; $i++) {
    $n = @(($random.NextDouble()*2-1), ($random.NextDouble()*2-1), ($random.NextDouble()*2-1))
    if ($i -lt 100) { $n[2] = 0.0 }
    $length = [Math]::Sqrt((Dot $n $n))
    if ($length -gt 1e-8) { $normals.Add(@($n | ForEach-Object { $_ / $length })) }
}
foreach ($n in $normals) {
    $sz = if ($n[2] -ge 0) { 1.0 } else { -1.0 }
    $a = 1.0 / ($sz + $n[2]); $ya = $n[1]*$a; $b = $n[0]*$ya; $c = $n[0]*$sz
    $t = @(($c*$n[0]*$a-1), ($sz*$b), $c)
    $v = @($b, ($n[1]*$ya-$sz), $n[1])
    foreach ($value in ($t + $v)) { if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw 'Nonfinite basis' } }
    foreach ($errorValue in @((Dot $t $n), (Dot $v $n), (Dot $t $v), ((Dot $t $t)-1), ((Dot $v $v)-1))) {
        if ([Math]::Abs($errorValue) -gt 1e-8) { throw 'Non-orthonormal blur basis' }
    }
}
foreach ($budget in 1..64) {
    $previousAngle = 91.0
    foreach ($frames in 0..($budget+1)) {
        $encoded = $frames / 255.0
        $fraction = [Math]::Min(1.0, [Math]::Max(0.0, ($encoded*255.0)/[Math]::Max($budget,1)))
        $angle = 90.0*(1-0.8*[Math]::Sqrt($fraction))
        if ($angle -lt 17.99999 -or $angle -gt $previousAngle+1e-8) { throw 'Invalid history rejection curve' }
        if ($frames -ge $budget -and [Math]::Abs($angle-18.0) -gt 1e-6) { throw 'Converged threshold is not 18 degrees' }
        $previousAngle = $angle
    }
}
Write-Output "PASS: $($normals.Count) basis cases and all 64 history budgets (reference math)."
$output = Join-Path $repo ('build/hybridgi-blur-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$count = 0
foreach ($resolution in 0..2) {
    foreach ($temporal in 0..1) {
        foreach ($atrous in 0..1) {
            $argsFxc = @('/nologo','/WX','/Ges','/O3','/T','cs_5_0','/E','main','/I',$includeRoot,
                '/D','COMPUTESHADER','/D','WINPC','/D','DX11')
            if ($resolution -eq 1) { $argsFxc += @('/D','HALF_RES') }
            if ($resolution -eq 2) { $argsFxc += @('/D','QUARTER_RES') }
            if ($temporal) { $argsFxc += @('/D','TEMPORAL_DENOISER') }
            if ($atrous) { $argsFxc += @('/D','ATROUS_STEP_2') }
            $argsFxc += @('/Fo', (Join-Path $output "$count.cso"), $source)
            & $compiler @argsFxc
            if ($LASTEXITCODE -ne 0) { throw "FXC failed: resolution=$resolution temporal=$temporal atrous=$atrous" }
            $count++
        }
    }
}
Write-Output "PASS: $count blur variants, strict FXC SM5 with warnings as errors. Output: $output"
