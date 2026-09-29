[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$sourceShaders = Join-Path $repository 'distribution\Shaders'
$stage = Join-Path $repository 'build\cspom-shader-tests\Shaders'
$output = Join-Path $repository 'build\cspom-shader-tests\Compiled'

$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$fxc = Get-ChildItem -Path (Join-Path $sdkBin '*\x64\fxc.exe') -File -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $fxc) { throw 'Windows SDK FXC required.' }

Remove-Item -LiteralPath (Split-Path -Parent $stage) -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stage,$output -Force | Out-Null
Copy-Item -Path (Join-Path $sourceShaders '*') -Destination $stage -Recurse -Force

Get-ChildItem -LiteralPath (Join-Path $repository 'pipeline') -Directory | ForEach-Object {
    $descriptor = Join-Path $_.FullName 'Module.ini'
    $kernels = Join-Path $_.FullName 'Kernels'
    if ((Test-Path -LiteralPath $descriptor) -and (Test-Path -LiteralPath $kernels)) {
        $text = Get-Content -LiteralPath $descriptor -Raw
        if ($text -match '(?im)^\s*Pipeline\s*=\s*Retired\s*$') { return }
        Copy-Item -Path (Join-Path $kernels '*') -Destination $stage -Recurse -Force
    }
}

$cases = @(
    @('SPECULAR','VC'),
    @('SPECULAR','DO_ALPHA_TEST','VC'),
    @('PARALLAX'),
    @('ENVMAP'),
    @('MATERIAL_FORGE'),
    @('LANDSCAPE'),
    @('LANDSCAPE','MATERIAL_FORGE'),
    @('PARALLAX','DEFERRED'),
    @('MATERIAL_FORGE','DEFERRED'),
    @('PARALLAX','TREE_ANIM'),
    @('PARALLAX','DO_ALPHA_TEST'),
    @('PARALLAX','MODELSPACENORMALS'),
    @('PARALLAX','SKINNED')
)

for ($index = 0; $index -lt $cases.Count; ++$index) {
    $arguments = @('/nologo','/WX','/Ges','/O3','/T','ps_5_0','/E','main','/I',$stage)
    foreach ($define in (@('WINPC','DX11','PSHADER','MATERIAL_LAYERS','PIXL_CSPOM') + $cases[$index])) {
        $arguments += @('/D',$define)
    }
    $arguments += @('/Fo',(Join-Path $output "$index.cso"),(Join-Path $stage 'Lighting.hlsl'))
    & $fxc @arguments
    if ($LASTEXITCODE -ne 0) { throw "CSPOM shader case $index failed: $($cases[$index] -join ', ')" }
}

Write-Host "PASS: $($cases.Count) CSPOM Lighting pixel permutations; warnings treated as errors."
