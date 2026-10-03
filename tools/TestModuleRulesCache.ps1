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
