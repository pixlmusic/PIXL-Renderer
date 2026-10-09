[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Require-Text([string]$RelativePath, [string]$Pattern, [string]$Description) {
    $path = Join-Path $repo $RelativePath
    if ((Get-Content -LiteralPath $path -Raw) -notmatch $Pattern) {
        throw "Missing ModuleRules cache contract ($Description): $RelativePath"
    }
}

Require-Text 'engine\ModuleRules.h' 'InvalidateConstraintCache' 'public invalidation'
Require-Text 'engine\ModuleRules.cpp' 'constraintCacheIndex' 'indexed lookup'
Require-Text 'engine\ModuleRules.cpp' 'constraintCacheValid' 'invalidatable snapshot'
Require-Text 'engine\RenderModule.cpp' 'ModuleRules::InvalidateConstraintCache' 'module-load invalidation'
Require-Text 'engine\Renderer\QualityProfiles.cpp' 'ModuleRules::InvalidateConstraintCache' 'quality-profile invalidation'
Require-Text 'engine\Menu\TuningWorkspaceRenderer.cpp' 'ModuleRules::InvalidateConstraintCache' 'reactive setting scan invalidation'

Write-Host 'PASS: ModuleRules uses an invalidatable indexed constraint snapshot with lifecycle invalidation.'

Require-Text 'engine\RenderModule.h' 'HasNoPipelinePermutationDependencies\(\) const \{ return false;' 'unknown modules remain conservative'
Require-Text 'engine\Modules\Microclimates.h' 'HasNoPipelinePermutationDependencies\(\) const override \{ return true;' 'runtime-only weather field may preserve cache'
Require-Text 'engine\ShaderCache.cpp' '!module->loaded && !module->HasNoPipelinePermutationDependencies\(\)' 'removed runtime-only modules do not wipe the library'
Require-Text 'engine\RenderModule.cpp' 'GetShortName\(\) == "ClothDynamics" \|\| GetShortName\(\) == "Microclimates"' 'retired experiments cannot reload'
Require-Text 'engine\Modules\ClothDynamics.cpp' 'a_stage != CachedShaderStage::Pixel' 'wear removal preserves vertex stages'
Require-Text 'engine\Modules\ClothDynamics.cpp' 'const bool skinned = \(a_descriptor & \(1u << 1u\)\) != 0u' 'wear removal retains descriptor-scoped invalidation'
Write-Host 'PASS: retired-module cache migration contracts; unknown dependencies still fail closed.'
