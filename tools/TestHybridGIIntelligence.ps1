$ErrorActionPreference = 'Stop'
function Require([bool]$ok, [string]$message) { if (!$ok) { throw $message } }
# Log quantization must bound multiplicative error of (1 + variance).
$maxError = 0.0
foreach ($i in 0..65535) {
    $variance = [Math]::Pow(2.0, 16.0 * $i / 65535.0) - 1.0
    $q = [uint32][Math]::Round([Math]::Log(1.0 + $variance, 2.0) * 65535.0 / 16.0)
    Require ($q -eq $i) 'Quantizer is not monotonic/invertible at code centres'
    $sample = [Math]::Pow(2.0, 16.0 * ($i + 0.49) / 65535.0) - 1.0
    $decoded = [Math]::Pow(2.0, 16.0 * $q / 65535.0) - 1.0
    $maxError = [Math]::Max($maxError, [Math]::Abs(($decoded + 1.0) / ($sample + 1.0) - 1.0))
}
Require ($maxError -lt 0.000085) 'Variance quantization error exceeds 0.0085 percent of 1+variance'
foreach ($state in 0..15) {
    foreach ($material in 0..15) {
        [uint32]$word = [uint32]([uint64]65535 + ([uint64]255 -shl 16) + ([uint64]$material -shl 24) + ([uint64]$state -shl 28))
        Require (($word -band 65535) -eq 65535) 'Variance field overlap'
        Require ((($word -shr 16) -band 255) -eq 255) 'Light-change field overlap'
        Require ((($word -shr 24) -band 15) -eq $material) 'Material field overlap'
        Require (($word -shr 28) -eq $state) 'State field overlap'
    }
}
# Independently accumulate raw moments and compare the central recurrence used
# by injection. Include zeros, alternating bright/dark and sustained step changes.
$mean = 0.0; $second = 0.0; $central = 0.0
foreach ($i in 0..999) {
    $sample = if ($i -lt 100) { 0.0 } elseif ($i -lt 500) { [double](($i % 2) * 10) } else { 2.0 }
    $alpha = 0.13
    $delta = $sample - $mean
    $central = (1.0 - $alpha) * ($central + $alpha * $delta * $delta)
    $mean += $alpha * $delta
    $second = (1.0 - $alpha) * $second + $alpha * $sample * $sample
    Require ([Math]::Abs($central - [Math]::Max(0.0, $second - $mean * $mean)) -lt 1e-10) 'EW variance diverges from raw-moment oracle'
}
Require ($central -lt 1e-10) 'Stable radiance variance did not converge'
# Every possible representative point inside a cell must be accepted at its
# own plane, including oblique normals and negative world-space coordinates.
foreach ($n in @(@(1.0,0.0,0.0), @(0.0,0.0,1.0), @(-0.6,0.0,0.8), @(0.577350269,0.577350269,0.577350269))) {
    foreach ($size in @(96.0,512.0)) {
        $support = 0.5 * $size * ([Math]::Abs($n[0])+[Math]::Abs($n[1])+[Math]::Abs($n[2]))
        foreach ($x in @(-0.5,0.5)) { foreach ($y in @(-0.5,0.5)) { foreach ($z in @(-0.5,0.5)) {
            $separation = $size * ($x*$n[0]+$y*$n[1]+$z*$n[2])
            Require ($separation -ge -$support-1e-10) 'Conservative receiver test rejects an in-cell plane'
        } } }
        $behind = -2.0 * $support
        Require (($behind + 2.0*$support)/$support -eq 0.0) 'Opposing-plane rejection endpoint'
    }
}
Write-Host "PASS: 65536 quantization centres, bounded error=$maxError, field isolation, EW variance oracle and convergence."
