[CmdletBinding()]
param(
    [string]$Fxc = ''
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Fxc)) {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $Fxc = Get-ChildItem -Path (Join-Path $sdkRoot '*\x64\fxc.exe') -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Fxc -or -not (Test-Path -LiteralPath $Fxc -PathType Leaf)) {
    throw 'Windows SDK FXC is required.'
}

$output = Join-Path $repo ('build\reactive-fx-shader-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$distributionShaders = Join-Path $repo 'distribution\Shaders'
$computeSource = Join-Path $repo 'pipeline\Reactive FX\Kernels\ReactiveFX\ReactiveFXCS.hlsl'

foreach ($entry in @('SpawnCS', 'SimulateCS', 'BuildMaskCS', 'CompositeCS', 'WriteReactiveMaskCS')) {
    $arguments = @(
        '/nologo', '/WX', '/Ges', '/O3', '/T', 'cs_5_0', '/E', $entry,
        '/D', 'COMPUTESHADER=1', '/D', 'WINPC=1', '/D', 'DX11=1',
        '/I', $distributionShaders,
        '/Fo', (Join-Path $output ($entry + '.cso')),
        $computeSource
    )
    $compilerOutput = & $Fxc @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Reactive FX shader failed ($entry): $compilerOutput"
    }
    Write-Host "PASS $entry"
}

Write-Host "Reactive FX shader evidence: $output"
Write-Host 'PASS all Reactive FX compute SM5 shader cases with strictness and warnings-as-errors.'
