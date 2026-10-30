#include "Deferred.h"
#include "Modules/ContainedLiquids.h"

#include <DDSTextureLoader.h>

#include "ShaderCache.h"
#include "State.h"
#include "Renderer/RenderOrigin.h"
#include "Renderer/RenderPassScheduler.h"
#include "Renderer/ReflectionContext.h"
#include "Renderer/PixelAnnotations.h"
#include "Utils/D3D.h"

#include "Modules/WorldProbes.h"
#include "Modules/AmbientProbe.h"
#include "Modules/HybridGI.h"
#include "Modules/SkyBounce.h"
#include "Modules/TissueDiffusion.h"
#include "Modules/TerrainSeam.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/GroundResponse.h"
#include "Modules/ReactiveFX.h"
#include "Modules/RainResponse.h"
#include "Modules/MaterialLayers.h"
#include "Modules/DistantLife.h"
#include "Modules/VolumetricClouds.h"
#include "Modules/WaterOptics.h"

#include "Hooks.h"

#include <array>
#include <cmath>
#include <memory>
#include <iterator>

struct DepthStates
{
	ID3D11DepthStencilState* a[6][40];
};

struct BlendStates
{
	ID3D11BlendState* a[7][2][13][2];

	static BlendStates* GetSingleton()
	{
		static auto blendStates = reinterpret_cast<BlendStates*>(REL::RelocationID(524749, 411364).address());
		return blendStates;
	}
};

// Rebuild off to the side. The original target is not released until all views
// have been created, so a D3D allocation error cannot leave a half-valid GBuffer.
void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags)
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	texDesc.BindFlags = bindFlags;
	texDesc.Format = format;
	srvDesc.Format = format;
	rtvDesc.Format = format;
	uavDesc.Format = format;

	winrt::com_ptr<ID3D11Texture2D> newTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> newSRV;
	winrt::com_ptr<ID3D11RenderTargetView> newRTV;
	winrt::com_ptr<ID3D11UnorderedAccessView> newUAV;
	DX::ThrowIfFailed(device->CreateTexture2D(&texDesc, nullptr, newTexture.put()));
	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
		DX::ThrowIfFailed(device->CreateShaderResourceView(newTexture.get(), &srvDesc, newSRV.put()));
	if (bindFlags & D3D11_BIND_RENDER_TARGET)
		DX::ThrowIfFailed(device->CreateRenderTargetView(newTexture.get(), &rtvDesc, newRTV.put()));
	if (bindFlags & D3D11_BIND_UNORDERED_ACCESS)
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(newTexture.get(), &uavDesc, newUAV.put()));

	auto& data = renderer->GetRuntimeData().renderTargets[target];
	// The engine stores these pointers using its wrapper type. Retain the native
	// COM reference and transfer ownership exactly once, as the original did.
	auto release = [](auto& pointer) {
		if (pointer) {
			pointer->Release();
			pointer = nullptr;
		}
	};
	release(data.UAV);
	release(data.RTV);
	release(data.SRVCopy);
	release(data.SRV);
	release(data.textureCopy);
	release(data.texture);
	data.texture = REX::W32::AsW32(newTexture.detach());
	if (newSRV) data.SRV = REX::W32::AsW32(newSRV.detach());
	if (newRTV) data.RTV = REX::W32::AsW32(newRTV.detach());
	if (newUAV) data.UAV = REX::W32::AsW32(newUAV.detach());
}

void Deferred::SetupResources()
{
	auto renderer = globals::game::renderer;
	pixlGIDebugCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PixlGIDebugData>(), "Deferred::PixlGIDebugCB");

	{
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		if (!main.texture) {
			logger::error("[Deferred] Main render target is unavailable during SetupResources");
			return;
		}
		main.texture->GetDesc(REX::W32::AsW32(&texDesc));
		if (main.SRV) main.SRV->GetDesc(REX::W32::AsW32(&srvDesc));
		else { srvDesc.Format = texDesc.Format; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = texDesc.MipLevels; }
		if (main.RTV) main.RTV->GetDesc(REX::W32::AsW32(&rtvDesc));
		else { rtvDesc.Format = texDesc.Format; rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; }
		if (main.UAV) main.UAV->GetDesc(REX::W32::AsW32(&uavDesc));
		else { uavDesc.Format = texDesc.Format; uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; }

		// Available targets:
		// MAIN ONLY ALPHA
		// WATER REFLECTIONS
		// BLURFULL_BUFFER
		// LENSFLAREVIS
		// SAO DOWNSCALED
		// SAO CAMERAZ+MIP_LEVEL_0_ESRAM
		// SAO_RAWAO_DOWNSCALED
		// SAO_RAWAO_PREVIOUS_DOWNSCALDE
		// SAO_TEMP_BLUR_DOWNSCALED
		// INDIRECT
		// INDIRECT_DOWNSCALED
		// RAWINDIRECT
		// RAWINDIRECT_DOWNSCALED
		// RAWINDIRECT_PREVIOUS
		// RAWINDIRECT_PREVIOUS_DOWNSCALED
		// RAWINDIRECT_SWAP
		// LIGHT_VOLUMES_HALF_RES
		// LIGHT_VOLUMES_BLUR_HALF_RES
		// LIGHT_VOLUMES_QUARTER_RES
		// LIGHT_VOLUMES_BLUR_QUARTER_RES
		// TEMPORAL_AA_WATER_1
		// TEMPORAL_AA_WATER_2

		// Albedo
		SetupRenderTarget(ALBEDO, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Specular
		SetupRenderTarget(SPECULAR, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Reflectance
		SetupRenderTarget(REFLECTANCE, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Normal + Roughness
		SetupRenderTarget(NORMALROUGHNESS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks
		SetupRenderTarget(MASKS, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		// Masks2: R = 1-vertexAO, G = signed effects-only view-depth relief.
		SetupRenderTarget(MASKS2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);

		// TAA water history buffers need RGBA16: alpha stores premultiplied coverage for ISWaterBlend
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_1, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
		SetupRenderTarget(RE::RENDER_TARGETS::kWATER_2, texDesc, srvDesc, rtvDesc, uavDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
	}

	{
		auto device = globals::d3d::device;

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		winrt::com_ptr<ID3D11SamplerState> newLinearSampler;
		winrt::com_ptr<ID3D11SamplerState> newPointSampler;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, newLinearSampler.put()));
		Util::SetResourceName(newLinearSampler.get(), "Deferred::LinearSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, newPointSampler.put()));
		Util::SetResourceName(newPointSampler.get(), "Deferred::PointSampler");
		if (linearSampler) linearSampler->Release();
		if (pointSampler) pointSampler->Release();
		linearSampler = newLinearSampler.detach();
		pointSampler = newPointSampler.detach();
	}

	// Directional shadow structured buffer (t98): CPU-written each frame, read-only on GPU.
	// One element holds the sun cascade data uploaded from BSShadowDirectionalLight.
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(DirectionalShadowLightData);
		sbDesc.ByteWidth = sizeof(DirectionalShadowLightData);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;

		auto newShadowBuffer = std::make_unique<Buffer>(sbDesc, nullptr, "Deferred::DirectionalShadowLights");
		newShadowBuffer->CreateSRV(srvDesc);
		delete directionalShadowLights;
		directionalShadowLights = newShadowBuffer.release();
	}
}

void Deferred::ReflectionsPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Reflections Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto state = globals::state;

	state->activeReflections = true;
	state->UpdateSharedData(false, false);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	if (!PIXL::Renderer::RenderPassScheduler::Get().Execute(PIXL::Renderer::PassExecutionPoint::ReflectionsPrepass))
		RenderModule::ForEachLoadedModule("ReflectionsPrepass", [](RenderModule* feature) { feature->ReflectionsPrepass(); }, true);
}

void Deferred::EarlyPrepasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Early Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	globals::state->UpdateSharedData(false, true);

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	// Shadow maps have just been rendered — upload BSShadowDirectionalLight data to t98.
	CopyShadowLightData();

	auto& scheduler = PIXL::Renderer::RenderPassScheduler::Get();
	const auto* graphicsState = globals::game::graphicsState;
	scheduler.BeginFrame(
		globals::state->frameCount,
		graphicsState ? graphicsState->screenWidth : 0u,
		graphicsState ? graphicsState->screenHeight : 0u,
		PIXL::RenderOrigin::Get().HistoryValid());
	if (!scheduler.Execute(PIXL::Renderer::PassExecutionPoint::EarlyPrepass))
		RenderModule::ForEachLoadedModule("EarlyPrepass", [](RenderModule* feature) { feature->EarlyPrepass(); }, true);
}

void Deferred::PrepassPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Prepass");

	auto shaderCache = globals::shaderCache;

	if (!shaderCache->IsEnabled())
		return;

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	if (!PIXL::Renderer::RenderPassScheduler::Get().Execute(PIXL::Renderer::PassExecutionPoint::Prepass))
		RenderModule::ForEachLoadedModule("Prepass", [](RenderModule* feature) { feature->Prepass(); }, true);
}

void Deferred::StartDeferred()
{
	if (!globals::state->inWorld || deferredPass)
		return;
	globals::state->UpdateSharedData(true, false);
	// Freeze Image Reconstruction camera history while the main gameplay view is
	// active. Later reflection, UI, and post-display camera updates may overwrite
	// Skyrim's cached per-frame camera buffer before post-processing evaluates DLSS.
	if (globals::pipeline::imageReconstruction.loaded)
		globals::pipeline::imageReconstruction.CaptureCameraFrame();

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& setRenderTargetMode = shadowState->GetRuntimeData().setRenderTargetMode;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

	// Backup original render targets
	for (uint i = 0; i < 4; i++) {
		forwardRenderTargets[i] = renderTargets[i];
	}

	RE::RENDER_TARGET targets[8]{
		RE::RENDER_TARGET::kMAIN,
		RE::RENDER_TARGET::kMOTION_VECTOR,
		NORMALROUGHNESS,
		ALBEDO,
		SPECULAR,
		REFLECTANCE,
		MASKS,
		MASKS2
	};

	for (uint i = 2; i < 8; i++) {
		renderTargets[i] = targets[i];                                             // We must use unused targets to be indexable
		setRenderTargetMode[i] = RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR;  // Dirty from last frame, this calls ClearRenderTargetView once
	}

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = true;

	{
		auto context = globals::d3d::context;

		ID3D11Buffer* buffers[1] = { *globals::game::perFrame.get() };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	PrepassPasses();

	OverrideBlendStates();
}

void Deferred::DeferredPasses()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Deferred");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		context->CSSetConstantBuffers(12, 1, buffers);
	}

	auto specular = renderer->GetRuntimeData().renderTargets[SPECULAR];
	auto albedo = renderer->GetRuntimeData().renderTargets[ALBEDO];
	auto normalRoughness = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	auto masks = renderer->GetRuntimeData().renderTargets[MASKS];
	auto masks2 = renderer->GetRuntimeData().renderTargets[MASKS2];

	auto main = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[0]];
	auto normals = renderer->GetRuntimeData().renderTargets[forwardRenderTargets[2]];
	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto reflectance = renderer->GetRuntimeData().renderTargets[REFLECTANCE];

	auto motionVectors = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	bool interior = Util::IsInterior();
	// Geometry/decal visibility is already complete. Effects-only relief must
	// never replace the DSV used by Skyrim's prepass or transparent geometry.
	globals::pipeline::materialLayers.ResolveEffectsDepth(REX::W32::AsReal(depth.depthSRV), REX::W32::AsReal(masks2.SRV));

	auto& skyBounce = globals::pipeline::skyBounce;

	auto& ssgi = globals::pipeline::hybridGI;
    if (ssgi.settings.EnableCacheIntelligence) {
        const auto annotationSize = Util::ConvertToDynamic(float2{
            (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
        // The opaque MRT is complete here; late reconstruction masks are not.
        PIXL::Renderer::PixelAnnotations::Get().PublishOpaque(REX::W32::AsReal(masks.SRV),
            static_cast<std::uint32_t>(annotationSize.x), static_cast<std::uint32_t>(annotationSize.y));
    }
	if (ssgi.loaded)
		ssgi.DrawHybridGI();
	else
		PIXL::Renderer::ReflectionContext::Get().Invalidate();
	auto [ssgi_ao, ssgi_y, ssgi_cocg, ssgi_gi_spec, ssgi_bent_visibility] = ssgi.GetOutputTextures();
	if (!ssgi_gi_spec)
		PIXL::Renderer::ReflectionContext::Get().Invalidate();
	const auto sharedReflections = PIXL::Renderer::ReflectionContext::Get().Acquire();
	if (sharedReflections.valid)
		ssgi_gi_spec = sharedReflections.radianceConfidence.get();
	bool ssgi_hq_spec = ssgi.settings.EnableExperimentalSpecularGI;
	pixlGIDebugCB->Update(PixlGIDebugData{
		globals::pipeline::materialLayers.showEffectsDepthDebug ? 100u : ssgi.settings.DebugView,
		ssgi.settings.DebugGain,
		ssgi.settings.EnableSpecularOcclusion ? 1u : 0u,
		std::clamp(ssgi.settings.SpecularOcclusionStrength, 0.0f, 1.0f) });

	auto dispatchCount = Util::GetScreenDispatchCount(true);

	auto& sss = globals::pipeline::tissueDiffusion;
	if (sss.loaded)
		sss.DrawSSS();

	auto& worldProbes = globals::pipeline::worldProbes;
	if (worldProbes.loaded)
		worldProbes.UpdateCubemap();

	auto& ambientProbe = globals::pipeline::ambientProbe;
	auto* environmentProbeSRV = ambientProbe.loaded && ambientProbe.envIBLTexture && ambientProbe.envIBLTexture->srv ?
		ambientProbe.envIBLTexture->srv.get() : nullptr;
	auto* skyProbeSRV = ambientProbe.loaded && ambientProbe.skyIBLTexture && ambientProbe.skyIBLTexture->srv ?
		ambientProbe.skyIBLTexture->srv.get() : nullptr;
	// Deferred Composite
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite");

		ID3D11ShaderResourceView* srvs[18]{
			REX::W32::AsReal(specular.SRV),                                                                 // t0  SpecularTexture
			REX::W32::AsReal(albedo.SRV),                                                                   // t1  AlbedoTexture
			REX::W32::AsReal(normalRoughness.SRV),                                                          // t2  NormalRoughnessTexture
			REX::W32::AsReal(masks.SRV),                                                                    // t3  MasksTexture
			globals::pipeline::materialLayers.GetEffectsDepth(Util::GetCurrentSceneDepthSRV(false)), // t4 effects depth, raster fallback
			worldProbes.loaded ? REX::W32::AsReal(reflectance.SRV) : nullptr,                            // t5  ReflectanceTexture
			worldProbes.loaded && worldProbes.envTexture ? worldProbes.envTexture->srv.get() : nullptr,          // t6  EnvTexture
			worldProbes.loaded && worldProbes.envReflectionsTexture ? worldProbes.envReflectionsTexture->srv.get() : nullptr, // t7 EnvReflectionsTexture
			worldProbes.loaded && skyBounce.loaded && skyBounce.texProbeArray ? skyBounce.texProbeArray->srv.get() : nullptr, // t8 SkyBounceProbeArray
			REX::W32::AsReal(masks2.SRV),                                                                 // t9  Masks2Texture (vertexAO in .x)
			ssgi_ao,                                                                                        // t10 SsgiAoTexture
			ssgi_y,                                                                                         // t11 SsgiYTexture (diffuse GI is independent of HQ specular GI)
			ssgi_cocg,                                                                                      // t12 SsgiCoCgTexture
			ssgi_hq_spec ? ssgi_gi_spec : nullptr,                                                          // t13 SsgiSpecularTexture
			environmentProbeSRV,                                                                            // t14 EnvironmentProbeTexture
			skyProbeSRV,                                                                                    // t15 SkyProbeTexture
			ssgi.GetInputRadianceSRV(),                                                                      // t16 PIXL lighting input diagnostic
			ssgi_bent_visibility,                                                                             // t17 PIXL bent normal + directional visibility
		};

		if (worldProbes.loaded)
			context->CSSetSamplers(0, 1, &linearSampler);

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
		auto* pixlDebugBuffer = pixlGIDebugCB->CB();
		context->CSSetConstantBuffers(13, 1, &pixlDebugBuffer);

		ID3D11UnorderedAccessView* uavs[3]{ REX::W32::AsReal(main.UAV), REX::W32::AsReal(normals.UAV), REX::W32::AsReal(motionVectors.UAV) };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		auto shader = interior ? GetComputeMainCompositeInterior() : GetComputeMainComposite();
		context->CSSetShader(shader, nullptr, 0);
		if (shader && dispatchCount.x > 0 && dispatchCount.y > 0) {
			TracyD3D11Zone(globals::state->tracyCtx, "Deferred Composite - Dispatch");
			globals::profiler->BeginPass("DeferredComposite");
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
			globals::profiler->EndPass();
		}
	}

	// Clear
	{
		ID3D11ShaderResourceView* views[18]{};
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3]{ nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(12, 1, buffers);
		context->CSSetConstantBuffers(13, 1, buffers);

		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Dedicated rain runoff is composited after the opaque/deferred scene is
	// resolved but before later forward precipitation/effects. It therefore has
	// real scene depth available and no longer depends on Skyrim rain-card placement.
	auto& volumetricClouds = globals::pipeline::volumetricClouds;
	if (volumetricClouds.loaded)
		volumetricClouds.DrawClouds();
	// Water renders after EndDeferred returns. Refresh the cloud pair here so its
	// pixel shader sees this frame's resolved radiance and matching depth.
	if (globals::pipeline::waterOptics.loaded)
		globals::pipeline::waterOptics.RefreshCloudResources();

	auto& distantLife = globals::pipeline::distantLife;
	if (distantLife.loaded)
		distantLife.DrawDistantLife();

	// Reactive FX resolves after opaque lighting so its depth/normal collision
	// sees the final displaced scene, but before rain/forward presentation layers.
	auto& reactiveFX = globals::pipeline::reactiveFX;
	if (reactiveFX.loaded)
		reactiveFX.DrawReactiveFX();

	auto& rainResponse = globals::pipeline::rainResponse;
	if (rainResponse.loaded)
		rainResponse.DrawRoofRunoff();

	if (worldProbes.loaded)
		worldProbes.PostDeferred();

}

void Deferred::EndDeferred()
{
	// A module/cache toggle between StartDeferred and EndDeferred must never
	// strand overridden MRTs or engine blend states.
	if (!deferredPass)
		return;

	auto shadowState = globals::game::shadowState;
	auto& renderTargets = shadowState->GetRuntimeData().renderTargets;
	auto& stateUpdateFlags = shadowState->GetRuntimeData().stateUpdateFlags;

	// Do not render to our targets past this point
	for (uint i = 0; i < 4; i++) {
		renderTargets[i] = forwardRenderTargets[i];
	}

	for (uint i = 4; i < 8; i++) {
		renderTargets[i] = RE::RENDER_TARGET::kNONE;
	}

	auto context = globals::d3d::context;
	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	if (globals::shaderCache && globals::shaderCache->IsEnabled())
		DeferredPasses();  // Preserve restoration even if cache was disabled mid-frame.

	stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again

	deferredPass = false;

	ResetBlendStates();
	if (globals::pipeline::containedLiquids.loaded)
		globals::pipeline::containedLiquids.ReplayAfterDeferred();
}

void Deferred::OverrideBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	static std::once_flag setup;
	std::call_once(setup, [&]() {
		auto device = globals::d3d::device;

		for (int a = 0; a < 7; a++) {
			for (int b = 0; b < 2; b++) {
				for (int c = 0; c < 13; c++) {
					for (int d = 0; d < 2; d++) {
						forwardBlendStates[a][b][c][d] = blendStates->a[a][b][c][d];

						if (auto blendState = forwardBlendStates[a][b][c][d]) {
							D3D11_BLEND_DESC blendDesc;
							forwardBlendStates[a][b][c][d]->GetDesc(&blendDesc);

							blendDesc.IndependentBlendEnable = true;

							// Default to original blending method
							for (int i = 1; i < 8; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = blendDesc.RenderTarget[0].SrcBlend;
								blendDesc.RenderTarget[i].DestBlend = blendDesc.RenderTarget[0].DestBlend;
								blendDesc.RenderTarget[i].BlendOp = blendDesc.RenderTarget[0].BlendOp;
								blendDesc.RenderTarget[i].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlendAlpha;
								blendDesc.RenderTarget[i].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlendAlpha;
								blendDesc.RenderTarget[i].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOpAlpha;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = blendDesc.RenderTarget[0].RenderTargetWriteMask;
							}

							// Normals and motion vectors must use alpha blending
							for (int i = 1; i < 3; i++) {
								blendDesc.RenderTarget[i].BlendEnable = blendDesc.RenderTarget[0].BlendEnable;
								blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
								blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
								blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
								blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
							}

							DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, &deferredBlendStates[a][b][c][d]));
						} else {
							deferredBlendStates[a][b][c][d] = nullptr;
						}
					}
				}
			}
		}
	});

	// Set modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = deferredBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void Deferred::ResetBlendStates()
{
	auto blendStates = BlendStates::GetSingleton();

	// Restore modified blend states
	for (int a = 0; a < 7; a++) {
		for (int b = 0; b < 2; b++) {
			for (int c = 0; c < 13; c++) {
				for (int d = 0; d < 2; d++) {
					blendStates->a[a][b][c][d] = forwardBlendStates[a][b][c][d];
				}
			}
		}
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

template <typename T>
void Deferred::SetShadowCascadeParameters(T& lightData, DirectionalShadowLightData& dd)
{
	const auto count = (std::min)(static_cast<std::size_t>(lightData.shadowmapDescriptors.size()), std::size(dd.ShadowProj));
	for (uint32_t i = 0; i < count; i++) {
		auto proj = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&lightData.shadowmapDescriptors[i].lightTransform));
		DirectX::XMStoreFloat4x4(&dd.ShadowProj[i], proj);

		const auto determinant = DirectX::XMMatrixDeterminant(proj);
		const float det = DirectX::XMVectorGetX(determinant);
		const auto invProj = std::isfinite(det) && std::abs(det) > 1.0e-12f
			? DirectX::XMMatrixInverse(nullptr, proj)
			: DirectX::XMMatrixIdentity();
		DirectX::XMStoreFloat4x4(&dd.InvShadowProj[i], invProj);
	}
}

void Deferred::CopyShadowLightData()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "CopyShadowLightData");

	auto context = globals::d3d::context;
	if (!context)
		return;
	auto* shadowSceneNode = globals::game::smState ? globals::game::smState->shadowSceneNode[0] : nullptr;
	auto* sunShadowLight = shadowSceneNode ? shadowSceneNode->GetRuntimeData().sunShadowDirLight : nullptr;
	if (!sunShadowLight || !directionalShadowLights || !directionalShadowLights->resource || !directionalShadowLights->srv) {
		ID3D11ShaderResourceView* empty = nullptr;
		context->PSSetShaderResources(98, 1, &empty);
		return;
	}

	DirectionalShadowLightData dd{};

	auto& dirData = sunShadowLight->GetShadowDirectionalLightRuntimeData();
	dd.EndSplitDistances = { dirData.endSplitDistances[0], dirData.endSplitDistances[1] };
	dd.StartSplitDistances = { dirData.startSplitDistances[0], dirData.startSplitDistances[1] };

	SetShadowCascadeParameters(sunShadowLight->GetRuntimeData(), dd);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	DX::ThrowIfFailed(context->Map(directionalShadowLights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy(mapped.pData, &dd, sizeof(DirectionalShadowLightData));
	context->Unmap(directionalShadowLights->resource.get(), 0);

	ID3D11ShaderResourceView* srv = directionalShadowLights->srv.get();
	context->PSSetShaderResources(98, 1, &srv);
}

void Deferred::ClearShaderCache()
{
	if (mainCompositeCS) {
		mainCompositeCS->Release();
		mainCompositeCS = nullptr;
	}
	if (mainCompositeInteriorCS) {
		mainCompositeInteriorCS->Release();
		mainCompositeInteriorCS = nullptr;
	}
}

ID3D11ComputeShader* Deferred::GetComputeMainComposite()
{
	if (!mainCompositeCS) {
		logger::debug("Compiling DeferredCompositeCS");

		std::vector<std::pair<const char*, const char*>> defines;

		if (globals::pipeline::worldProbes.loaded)
			defines.push_back({ "WORLD_PROBES", nullptr });

		if (globals::pipeline::skyBounce.loaded)
			defines.push_back({ "SKY_BOUNCE", nullptr });

		if (globals::pipeline::hybridGI.loaded)
			defines.push_back({ "HYBRID_GI", nullptr });

		if (globals::pipeline::ambientProbe.loaded)
			defines.push_back({ "AMBIENT_PROBE", nullptr });

		// TERRAIN_SEAM flips DepthTexture's HLSL type from `Texture2D<unorm float>`
		// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
		if (globals::pipeline::terrainSeam.loaded)
			defines.push_back({ "TERRAIN_SEAM", nullptr });

		mainCompositeCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\PIXL-Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeCS;
}

ID3D11ComputeShader* Deferred::GetComputeMainCompositeInterior()
{
	if (!mainCompositeInteriorCS) {
		logger::debug("Compiling DeferredCompositeCS INTERIOR");

		std::vector<std::pair<const char*, const char*>> defines;
		defines.push_back({ "INTERIOR", nullptr });

		if (globals::pipeline::worldProbes.loaded)
			defines.push_back({ "WORLD_PROBES", nullptr });

		if (globals::pipeline::hybridGI.loaded)
			defines.push_back({ "HYBRID_GI", nullptr });

		if (globals::pipeline::ambientProbe.loaded)
			defines.push_back({ "AMBIENT_PROBE", nullptr });

		// TERRAIN_SEAM flips DepthTexture's HLSL type from `Texture2D<unorm float>`
		// (R24_UNORM_X8_TYPELESS game depth) to `Texture2D<float>` (R32_FLOAT blendedDepth).
		if (globals::pipeline::terrainSeam.loaded)
			defines.push_back({ "TERRAIN_SEAM", nullptr });

		mainCompositeInteriorCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\PIXL-Shaders\\DeferredCompositeCS.hlsl", defines, "cs_5_0"));
	}
	return mainCompositeInteriorCS;
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	func();
	globals::deferred->EarlyPrepasses();
};

void Deferred::Hooks::Main_RenderPlayerView::thunk(void* a1, bool a2, bool a3)
{
	auto* state = globals::state;
	if (state && state->frameAnnotations)
		state->BeginPerfEvent("Player View");

	if (state && globals::pipeline::imageReconstruction.loaded && globals::game::graphicsState) {
		static std::uint32_t lastConfiguredFrame = std::numeric_limits<std::uint32_t>::max();
		if (lastConfiguredFrame != state->frameCount) {
			lastConfiguredFrame = state->frameCount;
			auto& reconstruction = globals::pipeline::imageReconstruction;
			reconstruction.ConfigureTAA();
			reconstruction.ConfigureUpscaling(globals::game::graphicsState);
			static bool firstConfigurationLogged = false;
			if (!firstConfigurationLogged) {
				firstConfigurationLogged = true;
				logger::info("[ImageReconstruction] 1.7.104 player-view setup: method={}, output={}x{}, render scale={:.3f}x{:.3f}.",
					static_cast<unsigned>(reconstruction.GetUpscaleMethod()),
					globals::game::graphicsState->screenWidth, globals::game::graphicsState->screenHeight,
					reconstruction.resolutionScale.x, reconstruction.resolutionScale.y);
			}
		}
	}

	func(a1, a2, a3);
	if (state && state->frameAnnotations)
		state->EndPerfEvent();
}

void Deferred::Hooks::Main_RenderWorld::thunk(bool a1)
{
	auto* const state = globals::state;
	const bool runtime117104 = REL::Module::get().version() == SKSE::RUNTIME_SSE_1_7_104;
	const bool frameAnnotations = state->frameAnnotations;
	if (frameAnnotations)
		state->BeginPerfEvent("World");

	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	state->inWorld = true;
	// The verified RenderPlayerView call site is unavailable on 1.7.104.
	if (runtime117104) {
		globals::deferred->EarlyPrepasses();
	}

	func(a1);

	state->inWorld = false;
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	if (frameAnnotations)
		state->EndPerfEvent();
};

void Deferred::Hooks::Main_RenderWorld_Start::thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
{
	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		// Here is where the first opaque objects start rendering
		globals::deferred->StartDeferred();
	}

	func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches
};

// PIXL_GR_FINAL_DEPTH_SYNC_V1
//
// TerrainSeam builds its R32/R16 scene-depth textures during the early depth
// phase. GroundResponse displaces the real landscape later, during TerrainSeam's
// deferred landscape replay. Once a geometric GroundResponse pass has rendered,
// copy the FINAL hardware depth (which now contains the raised HS/DS surface)
// back into TerrainSeam's scene-depth textures before decals/deferred composite.
//
// This deliberately does NOT modify TerrainSeam.cpp, State.cpp, the HS/DS bridge,
// b13, or the GroundResponse geometry shaders.
static void SyncGroundResponseFinalDepthToTerrainSeam()
{
	auto& groundResponse = globals::pipeline::groundResponse;
	if (!groundResponse.loaded ||
		!groundResponse.terrainGeometryRenderedThisFrame) {
		return;
	}

	auto& terrainSeam = globals::pipeline::terrainSeam;
	if (!terrainSeam.loaded ||
		!terrainSeam.settings.Enabled ||
		!terrainSeam.blendedDepthTexture ||
		!terrainSeam.blendedDepthTexture16) {
		return;
	}

	auto* renderer = globals::game::renderer;
	auto* context = globals::d3d::context;
	if (!renderer || !context)
		return;

	auto& mainDepth =
		renderer->GetDepthStencilData().depthStencils[
			RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	if (!mainDepth.depthSRV ||
		!terrainSeam.blendedDepthTexture->uav ||
		!terrainSeam.blendedDepthTexture16->uav) {
		return;
	}

	// Preserve compute-stage ownership as well as OM targets: this conversion
	// runs between engine draw calls and cannot assume CS slots 0..1 are empty.
	winrt::com_ptr<ID3D11ComputeShader> previousComputeShader;
	std::array<ID3D11ClassInstance*, 256> previousComputeClasses{};
	UINT previousComputeClassCount = static_cast<UINT>(previousComputeClasses.size());
	context->CSGetShader(previousComputeShader.put(), previousComputeClasses.data(), &previousComputeClassCount);
	ID3D11ShaderResourceView* previousComputeViews[2]{};
	ID3D11UnorderedAccessView* previousComputeUAVs[2]{};
	context->CSGetShaderResources(0, 2, previousComputeViews);
	context->CSGetUnorderedAccessViews(0, 2, previousComputeUAVs);

	// The main depth texture is currently the active DSV. D3D11 cannot expose the
	// same subresource for depth output and shader input simultaneously, so retain
	// the exact MRT/DSV set, unbind it for the tiny compute resolve, then restore it.
	ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	ID3D11DepthStencilView* savedDSV = nullptr;
	context->OMGetRenderTargets(
		D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
		savedRTVs,
		&savedDSV);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	// State::UpdateSharedData keeps TerrainSeam's R16 depth bound on PS t17.
	// It is one of our UAV destinations, so explicitly unbind it during resolve.
	ID3D11ShaderResourceView* nullPSDepth = nullptr;
	context->PSSetShaderResources(17, 1, &nullPSDepth);

	// Reuse TerrainSeam's existing, proven depth conversion compute shader.
	// Feeding the final main depth into BOTH inputs makes its min(a,b) operation
	// an exact format-converting copy into the R32 + R16 scene-depth textures.
	ID3D11ShaderResourceView* depthViews[2] = {
		REX::W32::AsReal(mainDepth.depthSRV),
		REX::W32::AsReal(mainDepth.depthSRV)
	};
	context->CSSetShaderResources(0, 2, depthViews);

	ID3D11UnorderedAccessView* depthUAVs[2] = {
		REX::W32::AsReal(terrainSeam.blendedDepthTexture->uav.get()),
		REX::W32::AsReal(terrainSeam.blendedDepthTexture16->uav.get())
	};
	context->CSSetUnorderedAccessViews(0, 2, depthUAVs, nullptr);

	auto* depthBlendCS = terrainSeam.GetDepthBlendShader();
	if (depthBlendCS) {
		context->CSSetShader(depthBlendCS, nullptr, 0);

		const auto dispatchCount = Util::GetScreenDispatchCount();
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	ID3D11ShaderResourceView* nullDepthViews[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, 2, nullDepthViews);

	ID3D11UnorderedAccessView* nullDepthUAVs[2] = { nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, 2, nullDepthUAVs, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);

	context->OMSetRenderTargets(
		D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
		savedRTVs,
		savedDSV);

	context->CSSetShaderResources(0, 2, previousComputeViews);
	context->CSSetUnorderedAccessViews(0, 2, previousComputeUAVs, nullptr);
	context->CSSetShader(previousComputeShader.get(),
		previousComputeClassCount ? previousComputeClasses.data() : nullptr,
		previousComputeClassCount);
	for (auto*& view : previousComputeViews) {
		if (view) view->Release();
	}
	for (auto*& uav : previousComputeUAVs) {
		if (uav) uav->Release();
	}
	for (UINT i = 0; i < (std::min)(previousComputeClassCount, static_cast<UINT>(previousComputeClasses.size())); ++i) {
		if (previousComputeClasses[i]) previousComputeClasses[i]->Release();
	}

	for (auto*& rtv : savedRTVs) {
		if (rtv) {
			rtv->Release();
			rtv = nullptr;
		}
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}

	// Restore the standard scene-depth binding expected by Lighting/decal passes.
	ID3D11ShaderResourceView* finalDepth16 =
		Util::GetCurrentSceneDepthSRV(true);
	context->PSSetShaderResources(17, 1, &finalDepth16);

}
void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
{
	auto deferred = globals::deferred;

	if (globals::shaderCache->IsEnabled() && globals::state->inWorld) {
		auto& terrainSeam = globals::pipeline::terrainSeam;
		// Defer terrain rendering until after everything else
		if (terrainSeam.loaded && terrainSeam.settings.Enabled) {
			terrainSeam.RenderTerrainSeamPasses();

			// GroundResponse tessellates/displaces terrain after TerrainSeam's early
			// blended-depth texture was produced. Synchronize the now-final hardware
			// depth before anything consumes scene depth again.
			SyncGroundResponseFinalDepthToTerrainSeam();
		}
	}

	// Deferred blended decals

	func(This, RenderFlags);

	deferred->EndDeferred();

	// Copy depth from before water
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	context->CopyResource(REX::W32::AsReal(depthCopy.texture), REX::W32::AsReal(depth.texture));

	// After this point, water starts rendering
};

void Deferred::Hooks::BSCubeMapCamera_RenderCubemap::thunk(RE::NiAVObject* camera, int a2, bool a3, bool a4, bool a5)
{
	auto deferred = globals::deferred;
	auto state = globals::state;

	const bool previousActiveReflections = state->activeReflections;
	const uint32_t reflectionFlag = static_cast<uint32_t>(State::ExtraShaderDescriptors::IsReflections);
	const bool previousReflectionFlag = (state->permutationData.ExtraShaderDescriptor & reflectionFlag) != 0;
	deferred->ReflectionsPrepasses();
	state->permutationData.ExtraShaderDescriptor |= reflectionFlag;
	func(camera, a2, a3, a4, a5);
	state->activeReflections = previousActiveReflections;
	if (!previousReflectionFlag)
		state->permutationData.ExtraShaderDescriptor &= ~reflectionFlag;
}

void Deferred::Hooks::Main_RenderFirstPersonView::thunk(bool a1, bool a2)
{
	auto* const state = globals::state;
	state->permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
	func(a1, a2);
	state->permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::InWorld);
}

void Deferred::Hooks::Renderer_ResetState::thunk(void* This)
{
	func(This);

	auto* const state = globals::state;
	auto* const context = globals::d3d::context;

	ID3D11Buffer* buffers[3] = {
		state->permutationCB->CB(),
		state->sharedDataCB->CB(),
		state->featureDataCB->CB()
	};

	// PIXL b4/b5/b6 are consumed by both raster stages.  The previous code
	// restored them only for PS/CS, leaving vertex features such as enhanced
	// vegetation wind without FeatureData (b6).
	context->VSSetConstantBuffers(4, 3, buffers);
	context->PSSetConstantBuffers(4, 3, buffers);
	context->CSSetConstantBuffers(5, 2, buffers + 1);

	// t100 is intentionally stage-specific: grass deformation is VS-only while
	// snow/mud relief is PS-only.  A renderer reset can discard SRV state, so
	// restore the interaction field here as well as from GroundResponse::Update.
	auto& groundResponse = globals::pipeline::groundResponse;
	ID3D11ShaderResourceView* interactionSRV =
		groundResponse.collisionTexture ? groundResponse.collisionTexture->srv.get() : nullptr;
	ID3D11ShaderResourceView* grassInteractionSRV =
		groundResponse.settings.EnableGroundResponse ? interactionSRV : nullptr;
	ID3D11ShaderResourceView* terrainInteractionSRV =
		groundResponse.settings.EnableDeformableGround ? interactionSRV : nullptr;
	context->VSSetShaderResources(100, 1, &grassInteractionSRV);
	context->PSSetShaderResources(100, 1, &terrainInteractionSRV);
}
