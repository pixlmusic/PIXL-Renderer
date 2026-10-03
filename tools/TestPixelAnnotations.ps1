[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require-Text([string]$p,[string]$rx,[string]$why) {
    $full=Join-Path $repo $p
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Missing file: $p" }
    if ((Get-Content -LiteralPath $full -Raw) -notmatch $rx) { throw "Missing annotation contract ($why): $p" }
}
$h='engine\Renderer\PixelAnnotations.h'
foreach($name in @('Terrain','Rock','Wood','Metal','Grass','Foliage','Skin','Hair','Cloth','Glass','ContainedLiquid','Water','Snow','Mud','Emissive','Particle','Sky','WindowInterior','PublishBase','PublishReconstruction')) { Require-Text $h $name $name }
Require-Text 'engine\Renderer\PixelAnnotations.cpp' 'containedLiquid[\s\S]*windowInterior[\s\S]*water' 'specific classification priority'
Require-Text 'engine\Modules\ImageReconstruction.cpp' 'PixelAnnotations::Get\(\)\.PublishBase' 'authoritative mask publication'
Require-Text 'engine\Modules\ImageReconstruction.cpp' 'annotations\.reactiveMask\.get\(\)' 'RCAS shared annotation consumption'
Require-Text 'distribution\Shaders\Common\PIXLPixelAnnotations.hlsli' 'PIXL_MATERIAL_WINDOW_INTERIOR' 'HLSL category contract'
Write-Host 'PASS: compact pixel-annotation taxonomy and shared existing-mask publication validated.'
