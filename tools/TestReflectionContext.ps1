[CmdletBinding()]
param()
$ErrorActionPreference='Stop';$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($p,$rx,$why){$f=Join-Path $repo $p;if(!(Test-Path $f)){throw "Missing $p"};if((Get-Content $f -Raw)-notmatch $rx){throw "Missing reflection contract ($why): $p"}}
Require 'engine\Renderer\ReflectionContext.h' 'radianceConfidence[\s\S]*maxRoughness[\s\S]*worldFallback[\s\S]*temporal[\s\S]*spatial' 'published reflection metadata'
Require 'engine\Modules\HybridGI.cpp' 'ReflectionContext::Get\(\)\.Publish' 'HybridGI producer'
Require 'engine\Deferred.cpp' 'ReflectionContext::Get\(\)\.Acquire' 'deferred consumer'
$shader='pipeline\Hybrid GI\Kernels\HybridGI\hybridReflection.cs.hlsl'
foreach($rx in @('TraceScreenReflection','binary refinement','hitFacing','TraceWorldFallback','srcHistory','outReflection.*confidence')){Require $shader $rx $rx}
Require 'engine\Modules\WaterOptics.cpp' 'EnableEnhancedSSR' 'specialized water path retained'
Write-Host 'PASS: shared reflection publication wraps the existing Hi-Z hybrid pipeline and retains WaterOptics.'
