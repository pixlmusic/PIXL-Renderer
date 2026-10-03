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

$output = Join-Path $repo ('build\visibility-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$includeRoot = Join-Path $repo 'distribution\Shaders'
$sources = @(
    (Join-Path $repo 'pipeline\Foliage Optimizer\Kernels\FoliageOptimizer\GrassHiZCS.hlsl'),
    (Join-Path $repo 'pipeline\Foliage Optimizer\Kernels\FoliageOptimizer\GrassCullingCS.hlsl')
)
foreach ($source in $sources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($source)
    $arguments = @('/nologo', '/WX', '/Ges', '/O3', '/T', 'cs_5_0', '/E', 'main',
        '/I', $includeRoot, '/Fo', (Join-Path $output ($name + '.cso')), $source)
    $compilerOutput = & $Fxc @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Visibility shader failed ($name): $compilerOutput" }
    Write-Host "PASS $name"
}

$foliage = Get-Content -LiteralPath (Join-Path $repo 'engine\Modules\FoliageOptimizer\FoliageOptimizer.cpp') -Raw
if ($foliage -notmatch 'VisibilityContext::Get\(\)' -or $foliage -match '\bhiZ\.') {
    throw 'Foliage Optimizer has not fully migrated to the shared VisibilityContext.'
}
Write-Host 'PASS: shared visibility shader and consumer contracts validated.'
