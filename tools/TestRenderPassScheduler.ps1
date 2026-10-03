[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing scheduler contract file: $RelativePath"
    }
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -notmatch $Pattern) {
        throw "Missing scheduler contract ($Description): $RelativePath"
    }
}

$header = 'engine\Renderer\RenderPassScheduler.h'
foreach ($phase in @(
    'FrameBegin', 'SceneAcquire', 'DepthPreparation', 'TerrainPreparation',
    'LightingPreparation', 'IndirectLighting', 'Atmosphere', 'Water',
    'Transparency', 'Reconstruction', 'Presentation', 'FrameEnd')) {
    Require-Text $header ("\b" + $phase + "\b") "render phase $phase"
}

foreach ($field in @(
    'PassId id', 'RenderPhase phase', 'RenderModule\* owner', 'ResourceUsage reads',
    'ResourceUsage writes', 'bool temporal', 'bool optional', 'bool enabled',
    'bool profilingEnabled', 'QualityGroup qualityGroup')) {
    Require-Text $header $field "PassDesc field $field"
}

$deferred = 'engine\Deferred.cpp'
foreach ($point in @('ReflectionsPrepass', 'EarlyPrepass', 'Prepass')) {
    $schedulerCall = if ($point -eq 'EarlyPrepass') { 'scheduler\.Execute' } else { 'RenderPassScheduler::Get\(\)\.Execute' }
    Require-Text $deferred ($schedulerCall + "\(PIXL::Renderer::PassExecutionPoint::" + $point + "\)") "scheduler execution point $point"
    Require-Text $deferred ('ForEachLoadedModule\("' + $point) "legacy fallback $point"
}

Require-Text 'engine\State.cpp' 'RegisterLegacyModulePasses\(RenderModule::GetModuleList\(\)\)' 'deterministic legacy registration'
Require-Text 'engine\State.cpp' 'NotifyResourcesRecreated\(\)' 'resource recreation notification'
Require-Text 'engine\Profiler.h' 'activeTimerStack' 'nested profiler storage'
Require-Text 'engine\Profiler.cpp' 'activeTimerStack\.push_back' 'nested profiler begin tracking'
Require-Text 'engine\Profiler.cpp' 'activeTimerStack\.pop_back' 'nested profiler end tracking'

Write-Host 'PASS: Phase 1 scheduler contracts, legacy fallbacks, lifecycle notifications and nested profiler support validated.'
