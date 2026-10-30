[CmdletBinding()]
param()
$ErrorActionPreference='Stop';$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($p,$rx,$why){$f=Join-Path $repo $p;if(!(Test-Path $f)){throw "Missing $p"};if((Get-Content $f -Raw)-notmatch $rx){throw "Missing reflection contract ($why): $p"}}
Require 'engine\Renderer\ReflectionContext.h' 'radianceConfidence[\s\S]*maxRoughness[\s\S]*worldFallback[\s\S]*temporal[\s\S]*spatial' 'published reflection metadata'
Require 'engine\Modules\HybridGI.cpp' 'ReflectionContext::Get\(\)\.Publish' 'HybridGI producer'
Require 'engine\Deferred.cpp' 'ReflectionContext::Get\(\)\.Acquire' 'deferred consumer'
$shader='pipeline\Hybrid GI\Kernels\HybridGI\hybridReflection.cs.hlsl'
foreach($rx in @('TraceScreenReflection','srcReflectionHiZ.Load','ReflectionRayDepth','exitS','--mip','facing','TraceWorldFallback','srcHistory','outReflection.*confidence')){Require $shader $rx $rx}
foreach($rx in @('Common/MotionBlur\.hlsli','cameraMotionPixels','response\s*=\s*max\(response,\s*0\.72f\s*\*\s*saturate\(cameraMotionPixels\s*/\s*3\.0f\)\)','lerp\(response\s*\*\s*0\.55f,\s*response,\s*confidence\)','lerp\(0\.28f,\s*0\.48f,\s*roughness\)\s*\*\s*exp2\(-0\.55f\s*\*\s*cameraMotionPixels\)')){Require $shader $rx $rx}
foreach($rx in @('float incidence\s*=\s*smoothstep\(0\.05f,\s*0\.30f,\s*facing\)','float hitTolerance\s*=\s*thickness\s*\*\s*lerp\(0\.20f,\s*1\.0f,\s*incidence\)','finalDelta\s*<=\s*hitTolerance','uncertainMotion\s*=\s*saturate\(cameraMotionPixels\s*/\s*2\.0f\)')){Require $shader $rx $rx}
foreach($rx in @('roughTraceScale\s*=\s*lerp\(1\.0f,\s*0\.70f,','ceil\(\(float\)configuredTests\s*\*\s*roughTraceScale\)','uint tests\s*=\s*max\(8u,')){Require $shader $rx 'roughness-adaptive traversal budget'}
foreach($rx in @('srcGIHistoryLength\s*:\s*register\(t12\)','response\s*=\s*max\(response,\s*0\.52f\s*\*\s*directLightChange\)','relativeRadianceChange','samplerPointClamp')){Require $shader $rx $rx}
Require 'pipeline\Hybrid GI\Kernels\HybridGI\radianceDisocc.cs.hlsl' 'srcPreviousRadiance\s*:\s*register\(t11\)[\s\S]*?lightingReactivity[\s\S]*?prevAccum\s*=\s*min' 'reprojected direct-light history response'
Require 'pipeline\Hybrid GI\Kernels\HybridGI\worldCache.hlsli' 'radialDistance\s*=\s*length\(diff\)' 'isotropic voxel cascade handoff'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(11\) = texRadiance->srv.get\(\); // previous frame' 'previous radiance binding before pyramid refresh'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(12\) = settings.EnableTemporalDenoiser' 'SSR reactive GI history binding'
Require 'engine\Modules\RadiantGrid.cpp' 'if \(maskIndex < 32u\)\s*strictLightDataTemp\.ShadowBitMask \|= \(1u << maskIndex\)' 'safe modded shadow-mask index'
Require 'engine\Modules\HybridGI.cpp' 'reflectionDepthCompute, "prefilterDepths.cs.hlsl",.*MIN_FILTER' 'conservative depth producer'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(11\) = texReflectionDepth->srv.get\(\)' 'dedicated CS Hi-Z binding'
Require 'engine\Modules\WaterOptics.cpp' 'EnableEnhancedSSR' 'specialized water path retained'
Write-Host 'PASS: shared reflection publication wraps the existing Hi-Z hybrid pipeline and retains WaterOptics.'
