[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $RelativePath" }
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing light-transport contract ($Description): $RelativePath"
    }
}

$header = 'engine\Renderer\LightTransportWorld.h'
Require-Text $header 'std::array<ProbeView' 'bounded probe registry'
Require-Text $header 'ValidFor\(std::uint64_t expectedFrame\)' 'frame validity check'
Require-Text $header 'winrt::com_ptr<ID3D11ShaderResourceView>' 'safe frame-scoped COM ownership'
Require-Text 'engine\State.cpp' 'LightTransportWorld::Get\(\)\.BeginFrame' 'per-frame expiry'
Require-Text 'engine\Renderer\RenderPassScheduler.cpp' 'LightTransportWorld::Get\(\)\.Invalidate' 'history/resource invalidation'
Require-Text 'engine\Modules\RadiantGrid.cpp' 'PublishLocalLights' 'local-light producer'
Require-Text 'engine\State.cpp' 'data\.DirLightColor\.z,[\s\S]*?data\.SunDirection\.x,[\s\S]*?data\.SunDirection\.z' 'world-space sun direction participates in environment invalidation'
Require-Text 'engine\Renderer\LightTransportWorld.h' 'PublishEnvironmentEnergy\(const std::array<float,9>& energy\)' 'environment sample carries ambient, direct energy and direction'
Require-Text 'engine\Renderer\EmitterChanges.h' '0\.9961947f' 'direction changes use an angular threshold'
Require-Text 'engine\Modules\RadiantGrid.h' 'AddParticleLightsToBuffer\([\s\S]*?a_emitterIdentities' 'particle-emitter identity output'
Require-Text 'engine\Modules\RadiantGrid.cpp' 'frameLights\.push_back\(\{\s*it->first,\s*resolved\s*\}\)' 'particle owner survives persisted-light staging'
Require-Text 'engine\Modules\RadiantGrid.cpp' 'a_emitterIdentities\[a_lightsData\.size\(\)\]\s*=\s*reinterpret_cast<std::uintptr_t>\(frameLight\.owner\)' 'stable particle owner identity published with GPU light'
Require-Text 'engine\Modules\AmbientProbe.cpp' 'AmbientEnvironmentSH' 'ambient SH producer'
Require-Text 'engine\Modules\SkyBounce.cpp' 'SkyVisibility' 'sky-visibility producer'
Require-Text 'engine\Modules\HybridGI.cpp' 'AcquireLocalLights' 'shared local-light consumer'
Require-Text 'engine\Modules\HybridGI.cpp' 'radiantGrid\.lights->srv\.get\(\)' 'legacy direct fallback'
Require-Text 'engine\Menu\WorkshopToolsRenderer.cpp' 'Published Light-Transport Resources' 'developer diagnostics'

Write-Host 'PASS: Phase 9 bounded publication, invalidation, producer, consumer and fallback contracts validated.'
