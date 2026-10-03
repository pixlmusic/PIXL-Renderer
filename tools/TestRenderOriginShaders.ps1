[CmdletBinding()]
param([string]$ShaderRoot = '', [string]$Fxc = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ShaderRoot)) {
    $ShaderRoot = Join-Path $repo 'distribution\Shaders'
}
if ([string]::IsNullOrWhiteSpace($Fxc)) {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $Fxc = Get-ChildItem -Path (Join-Path $sdkRoot '*\x64\fxc.exe') -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Fxc -or -not (Test-Path -LiteralPath $Fxc -PathType Leaf)) {
    throw 'Windows SDK FXC is required.'
}
$outDir = Join-Path $PSScriptRoot '..\build\render-origin-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$binary = Join-Path $outDir 'RenderOriginValidation.cso'
& $Fxc /nologo /WX /Ges /O3 /T cs_5_0 /E main /I $ShaderRoot /Fo $binary (Join-Path $PSScriptRoot 'RenderOriginValidation.hlsl')
if ($LASTEXITCODE -ne 0) { throw 'RenderOrigin GPU API compile failed' }
$reflection = (& $Fxc /dumpbin $binary) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Shader reflection failed' }
$reflection | Set-Content -LiteralPath (Join-Path $outDir 'RenderOriginReflection.txt')
foreach ($field in @(@('RenderOriginHigh',704), @('RenderOriginDelta',768), @('EngineToRenderOffset',784), @('PreviousEngineToRenderOffset',800), @('EngineOriginDelta',816), @('RenderOriginFlags',832))) {
    if ($reflection -notmatch ($field[0] + '\s*;[^\r\n]*Offset:\s*' + $field[1] + '\b')) { throw "HLSL packing mismatch: $($field[0])" }
}
Write-Host 'PASS: GPU coordinate API and reflected SharedData offsets 704..832 (848 bytes).'
