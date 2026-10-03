[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing file: $RelativePath" }
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing hook compatibility contract ($Description): $RelativePath"
    }
}

$header = 'engine\Renderer\HookRegistry.h'
foreach ($status in @(
    'Validated', 'Installed', 'Disabled', 'Unsupported',
    'SignatureMismatch', 'RelocationMissing')) {
    Require-Text $header ("\b" + $status + "\b") "status $status"
}

foreach ($field in @(
    'std::string name', 'std::string owner', 'std::string relocation',
    'std::string featureImpact', 'std::uint32_t patchSize', 'bool required')) {
    Require-Text $header $field "metadata $field"
}

Require-Text 'engine\Renderer\HookRegistry.cpp' 'VirtualQuery' 'guarded signature memory access'
Require-Text 'engine\Renderer\HookRegistry.cpp' 'mask\.size\(\) != expected\.size\(\)' 'signature mask validation'
Require-Text 'engine\Hooks.cpp' 'Core\.RendererPipelineHooks' 'required core hook group'
Require-Text 'engine\Hooks.cpp' 'Early\.D3D11DeviceIAT' 'optional D3D11 IAT hook'
Require-Text 'engine\Hooks.cpp' 'Image Reconstruction owns device creation' 'intentional hook ownership handoff'
Require-Text 'engine\Modules\FoliageOptimizer.cpp' 'HookStatus::Unsupported' 'unsupported optional hook isolation'
Require-Text 'engine\Modules\FoliageOptimizer.cpp' 'vanilla grass retained' 'optional feature fallback'
Require-Text 'engine\Menu\WorkshopToolsRenderer.cpp' 'Hook Compatibility' 'developer compatibility inspector'

Write-Host 'PASS: Phase 5 hook metadata, runtime reporting, guarded signatures and optional fallback contracts validated.'
