[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $RelativePath" }
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing renderer metadata contract ($Description): $RelativePath"
    }
}

foreach ($contract in @(
    @('engine\MaterialForge.h', 'Metadata::ABI::MaterialForge', 'MaterialForge ABI size'),
    @('engine\Modules\CameraSuite.h', 'Metadata::ABI::CameraDOF', 'Camera DOF ABI size'),
    @('engine\Modules\HybridGI.h', 'Metadata::ABI::HybridGI', 'HybridGI ABI size'),
    @('engine\Modules\GroundResponse.h', 'Metadata::ABI::GroundResponse', 'Ground Response ABI size'),
    @('engine\Modules\RainResponse.h', 'Metadata::ABI::RainResponse', 'Rain Response ABI size'),
    @('engine\Modules\WaterOptics.h', 'Metadata::ABI::WaterOptics', 'Water Optics ABI size'),
    @('pipeline\Camera Suite\Kernels\CameraSuite\DofControl.hlsli', 'cbuffer\s+DofControl\s*:\s*register\(b1\)', 'Camera DOF b1'),
    @('pipeline\Hybrid GI\Kernels\HybridGI\common.hlsli', 'cbuffer\s+HybridGICB\s*:\s*register\(b1\)', 'HybridGI b1'),
    @('pipeline\Ground Response\Kernels\GroundResponse\Runtime.hlsli', 'cbuffer\s+GroundResponseRuntimeCB\s*:\s*register\(b13\)', 'Ground Response b13'),
    @('distribution\Shaders\Common\SharedData.hlsli', 'cbuffer\s+FeatureData\s*:\s*register\(b6\)', 'shared FeatureData b6'))) {
    Require-Text $contract[0] $contract[1] $contract[2]
}

Require-Text 'engine\Modules\CameraSuite.cpp' 'CameraDofFStop\.defaultValue' 'restore-default schema linkage'
Require-Text 'engine\Menu\WorkshopToolsRenderer.cpp' 'ShaderABIHash' 'developer metadata diagnostics'
Write-Host 'PASS: shared settings defaults/ranges and critical CPU/HLSL ABI ownership contracts validated.'
