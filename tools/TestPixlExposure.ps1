[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ShaderRoot,
    [Parameter(Mandatory = $true)][string]$Fxc
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ShaderRoot).Path
$compiler = (Resolve-Path -LiteralPath $Fxc).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repo ('build\exposure-validation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null

# CPU reference of the exposure shader's stop-domain response. This checks the
# recurrence, not GPU rendering or Skyrim's upstream image-space adaptation.
function StepExposure([double]$previousEV, [double]$targetEV, [double]$dt, [double]$tau) {
    $errorEV = $targetEV - $previousEV
    $errorEV = [Math]::Sign($errorEV) * [Math]::Max([Math]::Abs($errorEV) - 0.06, 0.0)
    $step = $errorEV * (1.0 - [Math]::Exp(-$dt / $tau))
    $step = [Math]::Max(-4.0 * $dt, [Math]::Min(6.0 * $dt, $step))
    return $previousEV + $step
}
foreach ($target in @(-3.0, 3.0)) {
    $finalValues = @()
    foreach ($fps in @(30, 60, 144)) {
        $value = 0.0
        for ($frame = 0; $frame -lt $fps; ++$frame) {
            $previous = $value
            $value = StepExposure $value $target (1.0 / $fps) 0.18
            if (($target -lt 0 -and ($value -gt $previous -or $value -lt $target)) -or
                ($target -gt 0 -and ($value -lt $previous -or $value -gt $target))) {
                throw 'Exposure response is not monotonic / overshoots.'
            }
            $limit = $(if ($target -lt 0) { 4.0 } else { 6.0 }) / $fps
            if ([Math]::Abs($value - $previous) -gt ($limit + 1e-9)) { throw 'Exposure slew limit exceeded.' }
        }
        $finalValues += $value
        Write-Host ('Response {0:+0.0;-0.0} EV, {1} FPS after 1 s: {2:F4} EV' -f $target, $fps, $value)
    }
    $spread = ($finalValues | Measure-Object -Maximum).Maximum - ($finalValues | Measure-Object -Minimum).Minimum
    if ($spread -gt 0.03) { throw "Frame-rate response spread is too high: $spread EV" }
}
if ((StepExposure 0 0.055 (1.0 / 60) 0.18) -ne 0) { throw 'Deadband failed.' }
if ((StepExposure 0 3 0 0.18) -ne 0) { throw 'Zero delta advanced exposure.' }
if ((StepExposure 1 1 (1.0 / 60) 0.18) -ne 1) { throw 'Stable exposure moved.' }

$jobs = @(
    @{ file='CameraSuite/PhysicalCameraExposureCS.hlsl'; profile='cs_5_0'; defines=@('COMPUTESHADER') },
    @{ file='CameraSuite/PhysicalCameraHistogramCS.hlsl'; profile='cs_5_0'; defines=@('COMPUTESHADER') },
    @{ file='CameraSuite/PhysicalCameraLocalExposureCS.hlsl'; profile='cs_5_0'; defines=@('COMPUTESHADER') }
)
# Both native light-adaptation permutations, plus their scene presentation
# consumers. These use the deployed include tree, not a stripped shader stub.
foreach ($samples in @(4,16)) {
    $jobs += @{ file='ISHDR.hlsl'; profile='ps_5_0'; defines=@('PSHADER', "DOWNSAMPLE=$samples", 'DOWNADAPT') }
}
foreach ($fade in @('FADE', '')) {
    $defines = @('PSHADER', 'BLEND')
    if ($fade) { $defines += $fade }
    # Linear Light is a runtime shared-buffer switch, not a permutation define.
    $jobs += @{ file='ISHDR.hlsl'; profile='ps_5_0'; defines=$defines }
}
for ($index = 0; $index -lt $jobs.Count; ++$index) {
    $job = $jobs[$index]
    $arguments = @('/nologo', '/Ges', '/O3', '/T', $job.profile, '/E', 'main', '/I', $root)
    # FXC's pre-existing BLEND optimizer warning also occurs on the untouched
    # live shader. Match runtime flags there and retain its diagnostics; all
    # changed exposure / DOWNADAPT paths must pass warnings-as-errors.
    if ('BLEND' -notin $job.defines) { $arguments += '/WX' }
    foreach ($define in (@('WINPC','DX11') + $job.defines)) { $arguments += @('/D', $define) }
    $arguments += @('/Fo', (Join-Path $output "$index.cso"), (Join-Path $root $job.file))
    try {
        $ErrorActionPreference = 'Continue'
        $diagnostics = & $compiler @arguments 2>&1
        $compileExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = 'Stop' }
    $diagnostics | Write-Output
    if ($compileExit -ne 0) { throw "FXC failed: $($job.file) $($job.defines -join ', ')" }
}
Write-Host "PASS: exposure reference checks and $($jobs.Count) strict shader compilations. Outputs: $output"
