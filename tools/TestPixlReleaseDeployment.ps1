$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$root = Join-Path $repo ('build\deployment-test-' + [Guid]::NewGuid().ToString('N'))
$package = Join-Path $root 'package'
$game = Join-Path $root 'game'
$dllRelative = 'SKSE/Plugins/PIXLRenderer.dll'
$cacheRelative = 'PIXL/PipelineLibrary/Vertex/fixture.pixlbin'
$configRelative = 'SKSE/Plugins/PIXL/Config/UserGraphics.json'
foreach ($relative in @($dllRelative,$cacheRelative,$configRelative)) {
    foreach ($base in @($package,(Join-Path $game 'Data'))) {
        $path = Join-Path $base $relative
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        [IO.File]::WriteAllText($path, $(if ($base -eq $package) { 'new' } else { 'old' }))
    }
}
[IO.File]::WriteAllText((Join-Path $game 'SkyrimSE.exe'), 'test fixture, not executable')
$liveDll = Join-Path $game ('Data/' + $dllRelative)
$linkedOriginal = Join-Path $root 'mod-manager-original.dll'
New-Item -ItemType HardLink -Path $linkedOriginal -Target $liveDll | Out-Null
$files = @(Get-ChildItem $package -Recurse -File | ForEach-Object {
    @{ path=$_.FullName.Substring($package.Length+1).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash $_.FullName).Hash }
})
@{ product='PIXL Renderer'; executable=$dllRelative; channel='RELEASE-CANDIDATE'; cacheMode='preloaded'; files=$files } |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $package 'PIXL-RENDERER.manifest.json')
& (Join-Path $PSScriptRoot 'DeployPixlRelease.ps1') -PackageDirectory $package -GameDirectory $game
if ([IO.File]::ReadAllText($liveDll) -ne 'new') { throw 'DLL not updated.' }
if ([IO.File]::ReadAllText($linkedOriginal) -ne 'old') { throw 'Hardlink source was changed.' }
foreach ($relative in @($cacheRelative,$configRelative)) {
    if ([IO.File]::ReadAllText((Join-Path $game ('Data/' + $relative))) -ne 'old') { throw "Protected file changed: $relative" }
}
& (Join-Path $PSScriptRoot 'DeployPixlRelease.ps1') -PackageDirectory $package -GameDirectory $game
Write-Host 'PASS: DLL update, hardlink isolation, cache/config preservation and repeat deployment.'
