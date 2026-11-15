[CmdletBinding()]
param()
$ErrorActionPreference='Stop';$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($p,$rx,$why){$f=Join-Path $repo $p;if(!(Test-Path $f)){throw "Missing $p"};if((Get-Content $f -Raw)-notmatch $rx){throw "Missing reflection contract ($why): $p"}}
Require 'engine\Renderer\ReflectionContext.h' 'radianceConfidence[\s\S]*maxRoughness[\s\S]*worldFallback[\s\S]*temporal[\s\S]*spatial' 'published reflection metadata'
Require 'engine\Modules\HybridGI.cpp' 'ReflectionContext::Get\(\)\.Publish' 'HybridGI producer'
Require 'engine\Modules\HybridGI.cpp' 'void HybridGI::DrawHybridGI\(\)[\s\S]*?ReflectionContext::Get\(\)\.Invalidate\(\);[\s\S]*?if \(!context\)' 'early-exit invalidates the previous reflection frame'
Require 'engine\Deferred.cpp' 'ReflectionContext::Get\(\)\.Acquire' 'deferred consumer'
Require 'engine\Modules\HybridGI.cpp' 'srvs\.at\(0\) = texWorkingDepth->srv\.get\(\);[\s\S]*?srvs\.at\(11\) = texReflectionDepth->srv\.get\(\);[\s\S]*?uavs\.at\(0\) = texGiSpecular\[!inputSpecTexIdx\]->uav\.get\(\);[\s\S]*?HybridGI::HybridReflections' 'reflection trace input, Hi-Z and output bindings'
Require 'engine\Modules\HybridGI.cpp' 'reflectionActive && texGiSpecular\[outputSpecIdx\][\s\S]*?radianceConfidence = texGiSpecular\[outputSpecIdx\]->srv;[\s\S]*?reflectionFrame\.valid = true' 'only complete SSR output is published'
Require 'engine\Deferred.cpp' 'if \(sharedReflections\.valid\)[\s\S]*?ssgi_gi_spec = sharedReflections\.radianceConfidence\.get\(\);[\s\S]*?ssgi_hq_spec = ssgi\.settings\.EnableExperimentalSpecularGI && sharedReflections\.valid' 'reflection publication gates its deferred binding'
Require 'engine\Deferred.cpp' 'ssgi_hq_spec \? ssgi_gi_spec : nullptr,\s*// t13 SsgiSpecularTexture' 'SSR resource bound only when the published frame is valid'
$composite='distribution\PIXL-Shaders\DeferredCompositeCS.hlsl'
Require $composite 'ao = 1 - SsgiAoTexture\[pixCoord\]\.x;[\s\S]*?MultiBounceAO\(linAlbedo, ssgiAo\)[\s\S]*?IrradianceToLinear\(directionalAmbientColor\) \* multiBounceHybridGIAo' 'SSAO visibility reaches reconstructed indirect ambient'
Require $composite 'float vertexAO = 1\.0 - Masks2Texture\[dispatchID\.xy\]\.x;[\s\S]*?ssgiAo = saturate\(ssgiAo \* vertexAO\);' 'independent SSGI and vertex visibility combine without cancelling occlusion'
Require $composite 'diffuseColor = max\(0\.0, diffuseColor - directionalAmbientColor\);[\s\S]*?linDiffuseColor = Color::IrradianceToLinear\(diffuseColor\);[\s\S]*?diffuseColor = Color::IrradianceToGamma\(linDiffuseColor\);[\s\S]*?IrradianceToLinear\(directionalAmbientColor\) \* multiBounceHybridGIAo' 'SSAO only modulates the reconstructed indirect ambient term'
if ((Get-Content (Join-Path $repo $composite) -Raw) -match 'linDiffuseColor\s*\*=\s*sqrt\(multiBounceHybridGIAo\)') { throw 'SSAO must not darken the direct diffuse/emissive residual' }
Require $composite 'finalIrradiance = lerp\(fallbackIrradiance, hybridReflection, hybridConfidence\);' 'SSR confidence replaces rather than stacks with the probe fallback'
Require $composite '#if defined\(HYBRID_GI\) && !defined\(WORLD_PROBES\)[\s\S]*?ReflectanceTexture\[dispatchID\.xy\].*?screenReflection\.rgb.*?screenReflection\.a' 'SSR remains connected without WorldProbes'
$shader='pipeline\Hybrid GI\Kernels\HybridGI\hybridReflection.cs.hlsl'
foreach($rx in @('TraceScreenReflection','srcReflectionHiZ.Load','ReflectionRayDepth','exitS','--mip','facing','TraceWorldFallback','srcHistory','outReflection.*confidence')){Require $shader $rx $rx}
foreach($rx in @('float response\s*=\s*saturate\(ReflectionTemporalResponse\)','directLightChange\s*=\s*saturate\(1\.0f\s*-\s*\(giFrames\s*-\s*1\.0f\)','relativeRadianceChange','lerp\(response\s*\*\s*0\.30f,\s*response,\s*confidence\)','roughTraceScale\s*=\s*lerp\(1\.0f,\s*0\.70f,\s*smoothstep\(0\.30f,\s*max\(ReflectionMaxRoughness,\s*0\.31f\),\s*roughness\)\)')){Require $shader $rx $rx}
if((Get-Content (Join-Path $repo $shader) -Raw) -match 'cameraMotionPixels|MotionBlur::GetSSMotionVector'){throw 'Reflection response must use validated reprojection and radiance evidence, not physical-camera motion'}
foreach($rx in @('float incidence\s*=\s*smoothstep\(0\.05f,\s*0\.30f,\s*facing\)','float hitTolerance\s*=\s*thickness\s*\*\s*lerp\(0\.20f,\s*1\.0f,\s*incidence\)','finalDelta\s*<=\s*hitTolerance','screenNeighborhoodMin','screenNeighborhoodMax')){Require $shader $rx $rx}
foreach($rx in @('roughTraceScale\s*=\s*lerp\(1\.0f,\s*0\.70f,','ceil\(\(float\)configuredTests\s*\*\s*roughTraceScale\)','uint tests\s*=\s*max\(8u,')){Require $shader $rx 'roughness-adaptive traversal budget'}
foreach($rx in @('srcGIHistoryLength\s*:\s*register\(t12\)','response\s*=\s*max\(response,\s*0\.52f\s*\*\s*directLightChange\)','relativeRadianceChange','samplerPointClamp')){Require $shader $rx $rx}
Require 'pipeline\Hybrid GI\Kernels\HybridGI\radianceDisocc.cs.hlsl' 'srcPreviousRadiance\s*:\s*register\(t11\)[\s\S]*?lightingReactivity[\s\S]*?prevAccum\s*=\s*min' 'reprojected direct-light history response'
Require 'pipeline\Hybrid GI\Kernels\HybridGI\worldCache.hlsli' 'radialDistance\s*=\s*length\(diff\)' 'isotropic voxel cascade handoff'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(11\) = texRadiance->srv.get\(\); // previous frame' 'previous radiance binding before pyramid refresh'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(12\) = settings.EnableTemporalDenoiser' 'SSR reactive GI history binding'
Require 'engine\Modules\RadiantGrid.cpp' 'kShadowMaskChannelCount\s*=\s*4[\s\S]*?if \(maskIndex < kShadowMaskChannelCount && IsShadowMapRendered\([\s\S]*?strictLightDataTemp\.ShadowBitMask \|= \(1u << maskIndex\)' 'safe modded shadow-mask index'
Require 'engine\Modules\HybridGI.cpp' 'reflectionDepthCompute, "prefilterDepths.cs.hlsl",.*MIN_FILTER' 'conservative depth producer'
Require 'engine\Modules\HybridGI.cpp' 'srvs.at\(11\) = texReflectionDepth->srv.get\(\)' 'dedicated CS Hi-Z binding'
Require 'engine\Modules\WaterOptics.cpp' 'EnableEnhancedSSR' 'specialized water path retained'
Write-Host 'PASS: shared reflection publication wraps the existing Hi-Z hybrid pipeline and retains WaterOptics.'
