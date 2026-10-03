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
Require-Text 'engine\Modules\AmbientProbe.cpp' 'AmbientEnvironmentSH' 'ambient SH producer'
Require-Text 'engine\Modules\SkyBounce.cpp' 'SkyVisibility' 'sky-visibility producer'
Require-Text 'engine\Modules\HybridGI.cpp' 'AcquireLocalLights' 'shared local-light consumer'
Require-Text 'engine\Modules\HybridGI.cpp' 'radiantGrid\.lights->srv\.get\(\)' 'legacy direct fallback'
Require-Text 'engine\Menu\WorkshopToolsRenderer.cpp' 'Published Light-Transport Resources' 'developer diagnostics'

Write-Host 'PASS: Phase 9 bounded publication, invalidation, producer, consumer and fallback contracts validated.'
