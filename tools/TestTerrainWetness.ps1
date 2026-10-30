[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$lighting = Get-Content -Raw (Join-Path $repo 'distribution/PIXL-Shaders/Lighting.hlsl')
$defaults = Get-Content -Raw (Join-Path $repo 'distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json') | ConvertFrom-Json

$wetnessLodFade = '(?s)waterFilmCoverage\s*=\s*MaterialSurface::FilmCoverage\(wetnessGlossinessSpecular,\s*porosity\);.*?waterFilmCoverage\s*\*=\s*1\.0f\s*-\s*pixlWeatherSnowMask;\s*#\s*if\s+defined\(LOD_LAND_BLEND\).*?waterFilmCoverage\s*\*=\s*1\.0f\s*-\s*saturate\(lodLandBlendFactor\);\s*#\s*endif.*?waterRoughnessSpecular\s*= '
if ($lighting -notmatch $wetnessLodFade) {
	throw 'Rain wetness film must fade with full-detail-to-LOD landscape blending before direct and indirect specular evaluation.'
}

$completeWetnessLodFade = '(?s)terrainDetailWetnessWeight\s*=\s*1\.0f\s*-\s*saturate\(lodLandBlendFactor\);\s*wetness\s*\*=\s*terrainDetailWetnessWeight;\s*puddle\s*\*=\s*terrainDetailWetnessWeight;\s*puddleWetness\s*\*=\s*terrainDetailWetnessWeight;\s*shoreWetness\s*\*=\s*terrainDetailWetnessWeight;'
if ($lighting -notmatch $completeWetnessLodFade) {
	throw 'Wet albedo, puddle normals, and shoreline wetness must share the terrain LOD fade, not only the final reflection film.'
}

if ($lighting -notmatch 'nearFactor\s*=\s*1\.0f\s*-\s*smoothstep\(0\.0f,\s*4096\.0f\s*\*\s*2\.5f,\s*max\(viewPosition\.z,\s*0\.0f\)\)') {
	throw 'The shared near fade must use ordered smoothstep edges.'
}
if ($lighting -notmatch 'puddle\s*\*=\s*saturate\(wetnessOcclusion\s*\*\s*2\.0\);' -or
	$lighting -notmatch 'float puddleNormalWeight\s*=\s*saturate\(puddle\)\s*\*\s*nearFactor;' -or
	$lighting -match 'puddle\s*\*=\s*[^;]*nearFactor') {
	throw 'View depth may fade puddle normal detail, but must not fade rain film coverage into a camera-following band.'
}
if ($lighting -notmatch 'worldRainWetnessScale\s*=\s*0\.84f' -or $lighting -notmatch 'wetnessMinPuddleRoughness\s*=\s*0\.14f') {
	throw 'World terrain rain response must keep the reduced wetness and non-mirror roughness floor.'
}

$ground = $defaults.'Ground Response'
if ($ground.GeometryRenderDistance - $ground.GeometryFadeStart -lt 512) {
	throw 'Ground Response terrain geometry fade must span at least 512 world units to avoid a camera-following distance ring.'
}

Write-Host 'PASS: terrain rain film follows LOD blend and Ground Response geometry has a broad distance fade.'
