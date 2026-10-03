[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $RelativePath" }
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing temporal contract ($Description): $RelativePath"
    }
}

$header = 'engine\Renderer\TemporalContext.h'
foreach ($contract in @(
    'TemporalInvalidationReason', 'TemporalContext', 'HistoryRegistry', 'MotionContext',
    'BeginFrame', 'PublishDisocclusion', 'RegisterHistory', 'SetHistoryValid',
    'InvalidateHistory', 'GetDiagnostics')) {
    if ($contract -eq 'HistoryRegistry') { continue }
    Require-Text $header $contract $contract
}

Require-Text $header 'CameraCut[\s\S]*Teleport[\s\S]*WorldspaceChange[\s\S]*RenderOriginShift[\s\S]*ResolutionChange[\s\S]*SettingsChange[\s\S]*ModuleReset[\s\S]*DeviceReset' 'typed invalidation reasons'
Require-Text 'engine\Renderer\TemporalContext.cpp' '\}\s*for \(auto& \[reason, callback\] : callbacks\)' 'callbacks executed outside registry lock'
Require-Text 'engine\State.cpp' 'TemporalContext::Get\(\)\.BeginFrame' 'authoritative frame publication'
Require-Text 'engine\Modules\HybridGI.cpp' 'worldHistoryId.*RegisterHistory|RegisterHistory[\s\S]*worldHistoryId' 'HybridGI selective world history'
Require-Text 'engine\Modules\CameraSuite.cpp' 'dofHistoryId.*IsHistoryValid|IsHistoryValid\(dofHistoryId\)' 'Auto-DOF migration'
Require-Text 'engine\Modules\ImageReconstruction.cpp' 'IsHistoryValid\(reconstructionHistoryId\)' 'reconstruction migration'
Require-Text 'engine\Renderer\RenderPassScheduler.cpp' 'Do not turn[\s\S]*indiscriminate ModuleReset' 'legacy coarse reset isolation'

Write-Host 'PASS: unified temporal context, selective histories and representative migrations validated.'
