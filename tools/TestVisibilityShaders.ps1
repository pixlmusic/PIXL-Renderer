[CmdletBinding()]
param([string]$Fxc = '')

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Fxc)) {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $Fxc = Get-ChildItem -Path (Join-Path $sdkRoot '*\x64\fxc.exe') -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Fxc -or -not (Test-Path -LiteralPath $Fxc -PathType Leaf)) {
    throw 'Windows SDK FXC is required.'
}

$output = Join-Path $repo ('build\active-hiz-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$kernelRoot = Join-Path $repo 'pipeline\Hybrid GI\Kernels'
$sharedRoot = Join-Path $repo 'distribution\PIXL-Shaders'
$cases = @(
    @{ Name = 'ReflectionHiZ.Interval'; File = 'HybridGI\prefilterDepths.cs.hlsl'; Defines = @('MIN_FILTER') },
    @{ Name = 'HybridGI.LinearDepth'; File = 'HybridGI\prefilterDepths.cs.hlsl'; Defines = @('LINEAR_FILTER') },
    @{ Name = 'HybridGI.ReflectionTrace'; File = 'HybridGI\hybridReflection.cs.hlsl'; Defines = @() }
)
foreach ($case in $cases) {
    $source = Join-Path $kernelRoot $case.File
    $arguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', 'cs_5_0', '/E', 'main',
        '/I', $kernelRoot, '/I', $sharedRoot, '/Fo', (Join-Path $output ($case.Name + '.cso')))
    foreach ($define in $case.Defines) { $arguments += @('/D', ($define + '=1')) }
    $arguments += $source
    $compilerOutput = & $Fxc @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Hi-Z shader failed ($($case.Name)): $compilerOutput" }
    Write-Host "PASS $($case.Name)"
}

# The retired grass hierarchy must not return to the release registry or package.
$registry = Get-Content -LiteralPath (Join-Path $repo 'engine\RenderModule.cpp') -Raw
$descriptor = Get-Content -LiteralPath (Join-Path $repo 'pipeline\Foliage Optimizer\Module.ini') -Raw
 $health = Get-Content -LiteralPath (Join-Path $repo 'engine\PipelineHealth.cpp') -Raw
if ($registry -match 'foliageOptimizer' -or
    $descriptor -notmatch '(?im)^Pipeline\s*=\s*Retired\s*$' -or
    $health -notmatch '(?s)"FoliageOptimizer"\s*,\s*\{.*?\.modifiedShaderDirectory\s*=\s*false') {
    throw 'Retired FoliageOptimizer is still exposed to release builds.'
}
Write-Host 'PASS retired FoliageOptimizer build boundary and obsolete-feature migration'
