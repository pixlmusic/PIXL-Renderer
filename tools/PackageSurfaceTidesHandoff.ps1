[CmdletBinding()]
param([Parameter(Mandatory)][string]$SurfaceTidesSource)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = (Resolve-Path -LiteralPath $SurfaceTidesSource).Path
$output = Join-Path $repo 'dist'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path $repo "build\release-staging\surfacetides-$stamp"
$patch = Join-Path $work 'patch'
$handoff = Join-Path $work 'handoff'
$sourceStage = Join-Path $work 'SurfaceTides-1.0.2-PIXL-Source'
New-Item -ItemType Directory -Path "$patch\SKSE\Plugins","$patch\Shaders\SurfaceTides",$handoff,$sourceStage -Force | Out-Null
Copy-Item -LiteralPath "$source\build\windows\Release\SurfaceTides.dll" -Destination "$patch\SKSE\Plugins\SurfaceTides.dll"
Copy-Item -LiteralPath "$repo\installer\PIXLRenderer\SurfaceTides-PIXL-1.0.2.ini" -Destination "$patch\SKSE\Plugins\SurfaceTides.ini"
Copy-Item -LiteralPath "$source\Data\Shaders\SurfaceTides\Water.hlsl" -Destination "$patch\Shaders\SurfaceTides\Water.hlsl"
foreach($notice in @('LICENSE','THIRD_PARTY.md')) { Copy-Item -LiteralPath "$source\$notice" -Destination $patch }
Copy-Item -LiteralPath "$source\licenses" -Destination $patch -Recurse
Copy-Item -LiteralPath "$repo\docs\SURFACETIDES-UPSTREAM.md" -Destination "$patch\INSTALL-AND-SOURCE.md"
Copy-Item -LiteralPath "$repo\docs\SURFACETIDES-UPSTREAM.md" -Destination "$handoff\START-HERE.md"
Copy-Item -LiteralPath "$repo\installer\PIXLRenderer\SurfaceTides-PIXL-1.0.2.ini" -Destination "$handoff\PIXL-PRESET-SAMPLE.ini"

# Include maintained build/runtime sources and bundled dependency source. Omit
# local build products and old experiment/recovery packages.
foreach($directory in @('src','include','Data','tests','cmake','vendor','licenses','tools','installer','presets','optional','docs','.github')) {
    $root = Join-Path $source $directory
    if (!(Test-Path -LiteralPath $root)) { continue }
    foreach($file in Get-ChildItem -LiteralPath $root -File -Recurse -Force) {
        $relative = $file.FullName.Substring($source.Length + 1)
        if($relative -match '(^|[\\/])(\.git|build|out|\.vs|__pycache__)([\\/]|$)' -or
           $file.Name -eq '__folder_managed_by_vortex' -or
           $file.Extension -match '^\.(dll|exe|pdb|obj|lib|zip|7z|log|tmp|bak|pyc)$') { continue }
        $target = Join-Path $sourceStage $relative
        New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
foreach($file in Get-ChildItem -LiteralPath $source -File -Force) {
    if($file.Name -match '^(CMakeLists\.txt|CMakePresets\.json|LICENSE|README\.md|THIRD_PARTY\.md|\.gitignore)$') {
        Copy-Item -LiteralPath $file.FullName -Destination $sourceStage
    }
}
New-Item -ItemType Directory -Path "$handoff\PIXL-reference" -Force | Out-Null
foreach($relative in @('engine\Hooks.cpp','distribution\Shaders\Water.hlsl','COPYING','THIRD_PARTY_NOTICES.md')) {
    $target=Join-Path "$handoff\PIXL-reference" $relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo $relative) -Destination $target
}
$sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
function Archive([string]$directory,[string]$zip) {
    if(Test-Path -LiteralPath $zip) { throw "Archive already exists; preserve it or choose a new output: $zip" }
    Push-Location $directory
    try { & $sevenZip a -tzip -mx=7 $zip '.' | Out-Host; if($LASTEXITCODE) { throw '7-Zip failed' } }
    finally { Pop-Location }
    & $sevenZip t $zip | Out-Host
    if($LASTEXITCODE) { throw 'Archive verification failed' }
    $hash=(Get-FileHash -LiteralPath $zip).Hash
    "$hash  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath "$zip.sha256" -Encoding ascii
}
$sourceZip=Join-Path $output "SurfaceTides-1.0.2-PIXL-Source-$stamp.zip"
$patchZip=Join-Path $output "SurfaceTides-1.0.2-PIXL-Compatibility-$stamp.zip"
Archive $sourceStage $sourceZip
Archive $patch $patchZip
Copy-Item -LiteralPath $sourceZip,$patchZip,"$sourceZip.sha256","$patchZip.sha256" -Destination $handoff
$handoffZip=Join-Path $output "SurfaceTides-PIXL-Upstream-Handoff-$stamp.zip"
Archive $handoff $handoffZip
Write-Host "Owner handoff: $handoffZip"
Write-Host 'Publish the compatibility patch alongside its matching SurfaceTides source archive.'
