[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $RelativePath" }
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing GPU resource contract ($Description): $RelativePath"
    }
}

$header = 'engine\Renderer\GPUResourceServices.h'
foreach ($contract in @(
    'AcquireTexture', 'ReleaseTexture', 'AcquireBuffer', 'ReleaseBuffer',
    'UploadDiscard', 'RegisterHistory', 'InvalidateHistories',
    'OnResolutionChanged', 'OnResourcesRecreated', 'ResourceServiceDiagnostics')) {
    Require-Text $header $contract $contract
}

Require-Text 'engine\Renderer\GPUResourceServices.cpp' 'kMaxPooledTextures\s*=\s*64' 'bounded texture pool'
Require-Text 'engine\Renderer\GPUResourceServices.cpp' 'kMaxPooledBuffers\s*=\s*64' 'bounded buffer pool'
Require-Text 'engine\Renderer\GPUResourceServices.cpp' 'D3D11_MAP_WRITE_DISCARD' 'DX11 upload fallback'
Require-Text 'engine\Renderer\GPUResourceServices.cpp' 'entry\.generation == a_handle\.generation' 'stale handle guard'
Require-Text 'engine\Modules\DistantLife.cpp' 'GPUResourceServices::Get\(\)\.UploadDiscard' 'representative upload migration'
Require-Text 'engine\Renderer\RenderPassScheduler.cpp' 'OnResolutionChanged\(\)' 'resolution lifecycle bridge'
Require-Text 'engine\Renderer\GPUResourceServices.cpp' 'TemporalContext::Get\(\)\.Invalidate' 'legacy history compatibility bridge'
Require-Text 'engine\State.cpp' 'GPUResourceServices::Get\(\)\.BeginFrame' 'frame lifecycle bridge'

Write-Host 'PASS: Phase 3 shared DX11 resource, upload and history-service contracts validated.'
