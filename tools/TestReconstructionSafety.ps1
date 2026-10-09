[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Require($path, $pattern) {
    if ((Get-Content -LiteralPath (Join-Path $repo $path) -Raw) -notmatch $pattern) {
        throw "Safety contract missing: $path / $pattern"
    }
}
Require 'engine/Modules/ReactiveFX.cpp' 'CompactSpawnBatch<kMaximumParticles>'
Require 'engine/Modules/ReactiveFX.cpp' 'FAILED\(result\)[\s\S]*spawnCommands.clear\(\)'
Require 'engine/Modules/ReactiveFX.cpp' 'FAILED\(result\)[\s\S]*simulationTimeRemaining = 0.0f'
Require 'engine/Modules/ReactiveFX.cpp' 'scoped_lock lock\(eventMutex\)[\s\S]*sanitized.seed = Hash\(\+\+lastEventSeed'
Require 'engine/Modules/ReactiveFX.h' 'atomic<std::uint32_t> droppedEvents'
Require 'engine/Modules/CameraSuite.cpp' 'CSGetConstantBuffers\(5'
Require 'engine/Modules/CameraSuite.cpp' 'CSGetConstantBuffers\(12'
Require 'engine/Modules/CameraSuite.cpp' 'previousShared.get\(\)'
Require 'engine/Modules/CameraSuite.cpp' 'motionBlurEnabled = settings\.enableModernMotionBlur && !isMainOrLoadingMenu'
Require 'engine/Modules/ImageReconstruction/Streamline.cpp' 'return evalResult == sl::Result::eOk'
Require 'engine/Modules/ImageReconstruction.cpp' 'SetHistoryValid\(reconstructionHistoryId, historyEvaluated\)'
Require 'engine/Modules/ImageReconstruction.cpp' 'if \(settings\.qualityMode > 4u\)[\s\S]*settings\.qualityMode = 4u;'
Require 'engine/Modules/ImageReconstruction.cpp' 'if \(settings\.streamlineLogLevel > 2u\)[\s\S]*settings\.streamlineLogLevel = 2u;'
Require 'engine/Modules/NaturalLighting.cpp' 'if \(refr && niLight\)\s*globals::pipeline::distantLife\.ObserveStaticLight'
Require 'engine/Modules/TissueDiffusion.cpp' 'if \(!GetComputeShaderPrepass\(\)\)[\s\S]*if \(!GetComputeShaderHorizontalBlur\(\) \|\| !GetComputeShaderVerticalBlur\(\)\)[\s\S]*if \(!GetComputeShaderBurley\(\)\)[\s\S]*validMaterials = false;'
Require 'engine/Modules/TissueDiffusion.cpp' 'isBeastRaceKeyword = form \? form->As<RE::BGSKeyword>\(\) : nullptr;'
Require 'engine/Modules/TissueDiffusion.cpp' 'if \(isBeastRaceKeyword\)[\s\S]*race->HasKeyword\(isBeastRaceKeyword\)'
Require 'engine/Modules/TerrainOcclusion.cpp' 'void TerrainOcclusion::ClearShaderCache\(\)\s*\{\s*shadowUpdateProgram = nullptr;'
if ((Get-Content -LiteralPath (Join-Path $repo 'engine/Modules/TerrainOcclusion.cpp') -Raw) -match 'tex(?:HeightMap|ShadowHeight)\.release\(\)') {
    throw 'Terrain texture wrappers must retain RAII ownership during replacement.'
}
Require 'pipeline/Terrain Occlusion/Kernels/TerrainOcclusion/ShadowUpdate.cs.hlsl' 'sampleHeights = g_shadowHeight\[gtid - offset\][\s\S]*GroupMemoryBarrierWithGroupSync\(\);\s*if \(combineHeights\)[\s\S]*g_shadowHeight\[gtid\] = [^;]+;\s*GroupMemoryBarrierWithGroupSync\(\);'
Require 'engine/ShaderCache.cpp' 'if \(SUCCEEDED\(stripResult\) && strippedShaderBlob\)\s*\{\s*std::swap\(shaderBlob, strippedShaderBlob\);'
Require 'engine/ShaderCache.cpp' 'if \(strippedShaderBlob\)\s*strippedShaderBlob->Release\(\);'
Require 'engine/Modules/Waterbody/WaterCache.cpp' 'header\.dataCount < 0[\s\S]*payloadEnd - payloadStart\) / sizeof\(T\)[\s\S]*return false;[\s\S]*vec\.resize\(header\.dataCount\);'
Require 'engine/Modules/ImageReconstruction/FidelityFX.cpp' 'evaluated = ffxFsr3ContextDispatchUpscale\([^;]+== FFX_OK;'
Require 'engine/Modules/ImageReconstruction/FidelityFX.cpp' 'return evaluated;'
Require 'engine/Modules/ImageReconstruction/FidelityFX.cpp' 'FfxInterface fsrInterface\{\};'
Require 'engine/Modules/ImageReconstruction/FidelityFX.cpp' 'FfxFsr3ContextDescription contextDescription\{\};'
Require 'engine/Modules/ImageReconstruction.cpp' 'historyEvaluated = fidelityFX\.Upscale\([^;]+;\s*if \(!historyEvaluated\)\s*\{\s*pendingDLSSReset\.store\(true'
Require 'engine/Modules/ImageReconstruction/DX12SwapChain.cpp' 'if \(imageReconstruction\.ShouldUseNeuralRenderingThisFrame\(\) &&[\s\S]*neuralDepthBufferShared12 && neuralDepthBufferShared12->resource &&[\s\S]*neuralMotionVectorBufferShared12 && neuralMotionVectorBufferShared12->resource'
if ((Get-Content -LiteralPath (Join-Path $repo 'engine/Modules/ImageReconstruction/DX12SwapChain.cpp') -Raw) -match 'ShouldUseNeuralRenderingThisFrame\(\) &&\s*imageReconstruction\.HasCurrentDLSSFrame\(\)') {
    throw 'Neural Rendering must not be gated by a strict DLSS frame-token match; this suppressed the previously working neural handoff.'
}
Require 'pipeline/ImageReconstruction/Kernels/ImageReconstruction/EncodeTexturesCS.hlsl' 'dilationWeight > 0.5f \? dilatedMotionVector : motionVector'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl' 'CameraViewProjUnjittered'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl' 'uint lowerWeight = 32u - upperWeight'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl' 'InterlockedAdd\(LocalHistogram\[bin\], lowerWeight\)'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraHistogramCS.hlsl' 'InterlockedAdd\(LocalHistogram\[min\(bin \+ 1u, 255u\)\], upperWeight\)'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraExposureCS.hlsl' 'total \* 0.95f'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraExposureCS.hlsl' 'lerp\(avgLogLum, medianLogLum, 0\.42f\)'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/PhysicalCameraLocalExposureCS.hlsl' 'PreviousLocalExposure'
Require 'engine/Modules/CameraSuite.cpp' 'std::swap\(cameraLocalExposureTexture, cameraLocalExposureHistoryTexture\)'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/Stormglass.hlsli' 'transmittance = exp\(-pathMetres'
Require 'pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl' 'PixlApplySubmergedGrade\(cameraScene, stormglass.uv\)'
Require 'distribution/Shaders/Lighting.hlsl' 'lightsDiffuseColor \+= pixlLocalWaterBounce'
Require 'pipeline/Water Optics/Kernels/WaterOptics/WaterCaustics.hlsli' 'waterPoint.xy \+ FrameBuffer::CameraPosAdjust.xy'
Require 'distribution/Shaders/Water.hlsl' 'float2 edgeDistance = min\(ssrReflectionUv,'
# Equal-weight histogram reference: trimmed log mean and broad highlight
# percentile. A 1% candle must not change the meter; a broad bright region must.
function Meter([int]$brightSamples) {
    $samples = @()
    for ($i = 0; $i -lt 1000; ++$i) {
        $samples += $(if ($i -ge (1000 - $brightSamples)) { 6.0 } else { -4.0 })
    }
    $accepted = $samples[20..979]
    return @((($accepted | Measure-Object -Average).Average), $samples[949])
}
$baseline = Meter 0
$candle = Meter 10
$broad = Meter 300
if ($baseline[0] -ne $candle[0] -or $baseline[1] -ne $candle[1]) { throw 'Small candle dominated metering' }
if ($broad[0] -le $baseline[0] -or $broad[1] -le $baseline[1]) { throw 'Broad bright region ignored' }
Write-Host 'PASS reconstruction, motion-blur bindings and ReactiveFX failure contracts.'
