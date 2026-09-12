[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ShaderRoot)
$ErrorActionPreference = 'Stop'
$fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
$output = Join-Path $PSScriptRoot '..\build\windowlife-shader-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$cases = @(@(), @('DO_ALPHA_TEST'), @('ENVMAP'), @('GLOWMAP'), @('PBR'), @('SKINNED'))
for ($i = 0; $i -lt $cases.Count; ++$i) {
    $args = @('/nologo','/Ges','/O3','/T','ps_5_0','/E','main','/I',$ShaderRoot)
    foreach ($define in (@('WINPC','DX11','PSHADER','PIXL_WINDOW_LIFE') + $cases[$i])) { $args += @('/D',$define) }
    $args += @('/Fo',(Join-Path $output "$i.cso"),(Join-Path $ShaderRoot 'Lighting.hlsl'))
    & $fxc @args
    if ($LASTEXITCODE -ne 0) { throw "WindowLife permutation $i failed" }
}
Write-Host 'PASS: six WindowLife pixel shader permutations.'
