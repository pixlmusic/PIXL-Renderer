[CmdletBinding()]
param([string]$ShaderRoot='')
$ErrorActionPreference='Stop'
$repository=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stage=Join-Path $repository 'build\contained-liquids-shader-tests\Shaders'
if (-not $ShaderRoot) {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item -Path (Join-Path $repository 'distribution\Shaders\*') -Destination $stage -Recurse -Force
    Get-ChildItem -LiteralPath (Join-Path $repository 'pipeline') -Directory | ForEach-Object {
        $descriptor=Join-Path $_.FullName 'Module.ini'
        $kernels=Join-Path $_.FullName 'Kernels'
        if ((Test-Path -LiteralPath $descriptor) -and (Test-Path -LiteralPath $kernels) -and
            ((Get-Content -LiteralPath $descriptor -Raw) -notmatch '(?im)^\s*Pipeline\s*=\s*Retired\s*$')) {
            Copy-Item -Path (Join-Path $kernels '*') -Destination $stage -Recurse -Force
        }
    }
    $ShaderRoot=$stage
} else {
    $ShaderRoot=(Resolve-Path -LiteralPath $ShaderRoot).Path
}
$fxc=Get-ChildItem -Path 'C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$fxc) { throw 'Windows SDK FXC required' }
$out=Join-Path $repository 'build\contained-liquids-shader-tests\Compiled'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$cases=@(@(),@('ENVMAP','DO_ALPHA_TEST'),@('ENVMAP','DO_ALPHA_TEST','PIXL_WINDOW_LIFE'),@('DEFERRED','ENVMAP','DO_ALPHA_TEST'),@('MATERIAL_FORGE'),@('SKINNED'),@('MODELSPACENORMALS'),@('MATERIAL_LAYERS','ENVMAP'),@('VOLUME_OCCLUSION','ENVMAP'))
for($i=0;$i -lt $cases.Count;++$i) {
    $arguments=@('/nologo','/WX','/Ges','/O3','/T','ps_5_0','/E','main','/I',$ShaderRoot)
    foreach($define in (@('WINPC','DX11','PSHADER','PIXL_CONTAINED_LIQUIDS')+$cases[$i])) {$arguments+=@('/D',$define)}
    $arguments+=@('/Fo',(Join-Path $out "$i.cso"),(Join-Path $ShaderRoot 'Lighting.hlsl'))
    & $fxc @arguments
    if($LASTEXITCODE -ne 0){throw "Liquid shader case $i failed"}
}
Write-Host "PASS: $($cases.Count) Contained Liquids shader cases; warnings treated as errors."
