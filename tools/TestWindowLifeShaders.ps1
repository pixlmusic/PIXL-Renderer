[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ShaderRoot)
$ErrorActionPreference = 'Stop'
$fxc = (Get-Command fxc.exe -ErrorAction SilentlyContinue).Source
if (-not $fxc) {
    $sdkRoots = @(
        $(if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin' }),
        $(if ($env:ProgramFiles) { Join-Path $env:ProgramFiles 'Windows Kits\10\bin' })
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    if ($sdkRoots.Count -gt 0) {
        $fxc = Get-ChildItem -LiteralPath $sdkRoots -Filter fxc.exe -File -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $fxc) { throw 'fxc.exe was not found. Install the Windows SDK or add its bin directory to PATH.' }
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
