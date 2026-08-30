#include "SkyBounce.h"

#include "Deferred.h"
#include "I18n/I18n.h"
#include "ShaderCache.h"
#include "State.h"
#include "Utils/D3D.h"

#define I18N_KEY_PREFIX "feature.sky_bounce."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SkyBounce::Settings,
	MaxZenith,
	MinDiffuseVisibility,
	MinSpecularVisibility)

void SkyBounce::LoadSettings(json& o_json)
{
	settings = o_json;
	queuedResetSkyBounce = true;
}

void SkyBounce::SaveSettings(json& o_json)
{
	o_json = settings;
}

void SkyBounce::RestoreDefaultSettings()
{
	settings = {};
	queuedResetSkyBounce = true;
}

void SkyBounce::ResetSkyBounce()
{
	auto context = globals::d3d::context;
	if (!context || !texAccumFramesArray || !texShadowBitmask || !texShadowVisibility)
		return;

	UINT clr[1] = { 0 };
	context->ClearUnorderedAccessViewUint(texAccumFramesArray->uav.get(), clr);
	context->ClearUnorderedAccessViewUint(texShadowBitmask->uav.get(), clr);

	float clrf[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	context->ClearUnorderedAccessViewFloat(texShadowVisibility->uav.get(), clrf);

	queuedResetSkyBounce = false;
}

void SkyBounce::DrawSettings()
{
	ImGui::Text("%s", T(TKEY("min_visibility_desc"), "Minimum visibility values. Diffuse darkens objects. Specular removes the sky from reflections."));
	ImGui::SliderFloat(T(TKEY("diffuse_min_visibility"), "Minimum Diffuse Sky Light"), &settings.MinDiffuseVisibility, 0.01f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Minimum sky visibility retained in diffuse ambient lighting. Updates in real time; lower values allow deeper outdoor occlusion.");
	ImGui::SliderFloat(T(TKEY("specular_min_visibility"), "Minimum Sky Reflection"), &settings.MinSpecularVisibility, 0.01f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Minimum sky visibility retained in environment reflections. Updates in real time; lower values darken occluded rough reflections more strongly.");

	ImGui::Separator();

	if (ImGui::Button(T(TKEY("rebuild"), "Rebuild SkyBounce")))
		ResetSkyBounce();

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("rebuild_tooltip"), "Changes below require rebuilding, a loading screen, or moving away from the current location to apply."));

	if (ImGui::SliderAngle(T(TKEY("max_zenith"), "Sky Occlusion Zenith Limit"), &settings.MaxZenith, 0, 90))
		queuedResetSkyBounce = true;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("max_zenith_tooltip"), "Smaller angles creates more focused top-down shadow."));
}

void SkyBounce::SetupResources()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	{
		auto& precipitationOcclusion = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};

		precipitationOcclusion.texture->GetDesc(&texDesc);
		precipitationOcclusion.depthSRV->GetDesc(&srvDesc);
		precipitationOcclusion.views[0]->GetDesc(&dsvDesc);

		texOcclusion = new Texture2D(texDesc, "SkyBounce::Occlusion");
		texOcclusion->CreateSRV(srvDesc);
		texOcclusion->CreateDSV(dsvDesc);
	}

	{
		D3D11_TEXTURE3D_DESC texDesc{
			.Width = probeArrayDims[0],
			.Height = probeArrayDims[1],
			.Depth = probeArrayDims[2],
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = {
				.MipSlice = 0,
				.FirstWSlice = 0,
				.WSize = texDesc.Depth }
		};

		texProbeArray = new Texture3D(texDesc, "SkyBounce::ProbeArray");
		texProbeArray->CreateSRV(srvDesc);
		texProbeArray->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8_UINT;

		texAccumFramesArray = new Texture3D(texDesc, "SkyBounce::AccumFramesArray");
		texAccumFramesArray->CreateSRV(srvDesc);
		texAccumFramesArray->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32_UINT;

		texShadowBitmask = new Texture3D(texDesc, "SkyBounce::ShadowBitmask");
		texShadowBitmask->CreateSRV(srvDesc);
		texShadowBitmask->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8_UNORM;

		texShadowVisibility = new Texture3D(texDesc, "SkyBounce::ShadowVisibility");
		texShadowVisibility->CreateSRV(srvDesc);
		texShadowVisibility->CreateUAV(uavDesc);
	}

	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;  // Use comparison filtering
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;               // Address mode (Clamp for shadow maps)
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // Comparison function
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, comparisonSampler.put()));
		Util::SetResourceName(comparisonSampler.get(), "SkyBounce::ComparisonSampler");
	}

	CompileComputeShaders();
}

void SkyBounce::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&probeUpdateCompute
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void SkyBounce::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &probeUpdateCompute, "UpdateProbesCS.hlsl", {} },
		};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\SkyBounce") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
	}
}

SkyBounce::SkyBounceCB SkyBounce::GetCommonBufferData([[maybe_unused]] bool a_inWorld)
{
	// Use the actual Skyrim sky presentation rather than globals/state inWorld.
	// CameraSuite diagnostics proved the generic inWorld flag can be false/stale
	// during normal exterior gameplay. A zero SkyBounce CB here also breaks any
	// consumer that tries to locate the live 3D probe field (including Stormglass).
	const auto* sky = RE::Sky::GetSingleton();
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull)
		return SkyBounce::SkyBounceCB{};

	if (globals::state && globals::state->isMapMenuOpen)
		return SkyBounce::SkyBounceCB{};

	static float3 prevCellID = { 0, 0, 0 };

	// shadowState::posAdjust can retain the world-camera origin while Skyrim is in
	// first person. Grass uses this probe field for soft SSS visibility, so that
	// stale origin shifts the field away from nearby vegetation and can collapse
	// transmission to black. The captured FrameBuffer position is the origin used
	// by the current raster frame in both first and third person.
	const auto& cameraPos = globals::game::frameBufferCached.GetCameraPosAdjust();
	auto eyePos = float3{ cameraPos.x, cameraPos.y, cameraPos.z };

	float3 cellSize = {
		occlusionDistance / probeArrayDims[0],
		occlusionDistance / probeArrayDims[1],
		occlusionDistance * .5f / probeArrayDims[2]
	};
	auto cellID = eyePos / cellSize;
	cellID = { round(cellID.x), round(cellID.y), round(cellID.z) };
	auto cellOrigin = cellID * cellSize;
	float3 cellIDDiff = prevCellID - cellID;
	prevCellID = cellID;

	return {
		.OcclusionViewProj = OcclusionTransform,
		.OcclusionDir = OcclusionDir,
		.PosOffset = cellOrigin - eyePos,
		.ArrayOrigin = {
			((int)cellID.x - probeArrayDims[0] / 2) % probeArrayDims[0],
			((int)cellID.y - probeArrayDims[1] / 2) % probeArrayDims[1],
			((int)cellID.z - probeArrayDims[2] / 2) % probeArrayDims[2] },
		.ValidMargin = { (int)cellIDDiff.x, (int)cellIDDiff.y, (int)cellIDDiff.z },
		.MinDiffuseVisibility = settings.MinDiffuseVisibility,
		.MinSpecularVisibility = settings.MinSpecularVisibility
	};
}

void SkyBounce::Prepass()
{
	if (!globals::state || globals::state->isMapMenuOpen)
		return;

	bool interior = true;

	if (auto sky = globals::game::sky)
		interior = sky->mode.get() != RE::Sky::Mode::kFull;

	if (interior)
		return;

	TracyD3D11Zone(globals::state->tracyCtx, "SkyBounce - Update Probes");

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	if (!context || !renderer || !probeUpdateCompute || !comparisonSampler ||
		!texOcclusion || !texProbeArray || !texAccumFramesArray || !texShadowBitmask || !texShadowVisibility)
		return;

	{
		auto& esramDepthStencil = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM];
		auto* directionalShadowSRV = shadowCascadeSRV && globals::deferred && globals::deferred->directionalShadowLights ?
			globals::deferred->directionalShadowLights->srv.get() : nullptr;

		std::array<ID3D11ShaderResourceView*, 4> srvs = {
			texOcclusion->srv.get(),
			shadowCascadeSRV ? shadowCascadeSRV : nullptr,
			directionalShadowSRV,
			shadowCascadeSRV ? esramDepthStencil.depthSRV : nullptr
		};
		std::array<ID3D11UnorderedAccessView*, 4> uavs = {
			texProbeArray->uav.get(),
			texAccumFramesArray->uav.get(),
			texShadowBitmask->uav.get(),
			texShadowVisibility->uav.get()
		};
		std::array<ID3D11SamplerState*, 1> samplers = {
			comparisonSampler.get()
		};

		// Update probe array
		{
			context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(probeUpdateCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("SkyBounce::ProbeUpdate");
			context->Dispatch((probeArrayDims[0] + 7u) >> 3, (probeArrayDims[1] + 7u) >> 3, probeArrayDims[2]);
			globals::profiler->EndPass();
		}

		// Reset
		{
			srvs.fill(nullptr);
			uavs.fill(nullptr);
			samplers.fill(nullptr);

			context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(nullptr, nullptr, 0);
		}
	}

	// Set PS shader resources
	{
		ID3D11ShaderResourceView* srv = texProbeArray->srv.get();
		context->PSSetShaderResources(50, 1, &srv);

		srv = texShadowVisibility->srv.get();
		context->PSSetShaderResources(53, 1, &srv);
	}
}

void SkyBounce::PostPostLoad()
{
	logger::info("[SKY_BOUNCE] Hooking BSLightingShaderProperty::GetPrecipitationOcclusionMapRenderPassesImp");
	stl::write_vfunc<0x2D, BSLightingShaderProperty_GetPrecipitationOcclusionMapRenderPassesImpl>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::write_thunk_call<Main_Precipitation_RenderOcclusion>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));

	stl::write_thunk_call<SetViewFrustum>(REL::RelocationID(25643, 26185).address() + REL::Relocate(0x5D9, 0x59D));

	MenuOpenCloseEventHandler::Register();
}

//////////////////////////////////////////////////////////////

struct BSParticleShaderRainEmitter
{
	void* vftable_BSParticleShaderRainEmitter_0;
	char _pad_8[4056];
};

enum class ShaderTechnique
{
	// Sky
	SkySunOcclude = 0x2,

	// Grass
	GrassNoAlphaDirOnlyFlatLit = 0x3,
	GrassNoAlphaDirOnlyFlatLitSlope = 0x5,
	GrassNoAlphaDirOnlyVertLitSlope = 0x6,
	GrassNoAlphaDirOnlyFlatLitBillboard = 0x13,
	GrassNoAlphaDirOnlyFlatLitSlopeBillboard = 0x14,

	// Utility
	UtilityGeneralStart = 0x2B,

	// Effect
	EffectGeneralStart = 0x4000002C,

	// Lighting
	LightingGeneralStart = 0x4800002D,

	// DistantTree
	DistantTreeDistantTreeBlock = 0x5C00002E,
	DistantTreeDepth = 0x5C00002F,

	// Grass
	GrassDirOnlyFlatLit = 0x5C000030,
	GrassDirOnlyFlatLitSlope = 0x5C000032,
	GrassDirOnlyVertLitSlope = 0x5C000033,
	GrassDirOnlyFlatLitBillboard = 0x5C000040,
	GrassDirOnlyFlatLitSlopeBillboard = 0x5C000041,
	GrassRenderDepth = 0x5C00005C,

	// Sky
	SkySky = 0x5C00005E,
	SkyMoonAndStarsMask = 0x5C00005F,
	SkyStars = 0x5C000060,
	SkyTexture = 0x5C000061,
	SkyClouds = 0x5C000062,
	SkyCloudsLerp = 0x5C000063,
	SkyCloudsFade = 0x5C000064,

	// Particle
	ParticleParticles = 0x5C000065,
	ParticleParticlesGryColorAlpha = 0x5C000066,
	ParticleParticlesGryColor = 0x5C000067,
	ParticleParticlesGryAlpha = 0x5C000068,
	ParticleEnvCubeSnow = 0x5C000069,
	ParticleEnvCubeRain = 0x5C00006A,

	// Water
	WaterSimple = 0x5C00006B,
	WaterSimpleVc = 0x5C00006C,
	WaterStencil = 0x5C00006D,
	WaterStencilVc = 0x5C00006E,
	WaterDisplacementStencil = 0x5C00006F,
	WaterDisplacementStencilVc = 0x5C000070,
	WaterGeneralStart = 0x5C000071,

	// Sky
	SkySunGlare = 0x5C006072,

	// BloodSplater
	BloodSplaterFlare = 0x5C006073,
	BloodSplaterSplatter = 0x5C006074,
};

//////////////////////////////////////////////////////////////

RE::BSShaderProperty::RenderPassArray* SkyBounce::BSLightingShaderProperty_GetPrecipitationOcclusionMapRenderPassesImpl::thunk(
	RE::BSLightingShaderProperty* property,
	RE::BSGeometry* geometry,
	[[maybe_unused]] uint32_t renderMode,
	[[maybe_unused]] RE::BSGraphics::BSShaderAccumulator* accumulator)
{
	auto& skyBounce = globals::pipeline::skyBounce;
	if (!property)
		return nullptr;

	auto* precipitationOcclusionMapRenderPassList = &property->occlusionPasses;
	precipitationOcclusionMapRenderPassList->Clear();
	if (!geometry || !accumulator)
		return precipitationOcclusionMapRenderPassList;

	auto batch = accumulator->GetRuntimeData().batchRenderer;
	if (batch && batch->geometryGroups[14])
		batch->geometryGroups[14]->flags &= ~1;

	using enum RE::BSShaderProperty::EShaderPropertyFlag;
	using enum RE::BSUtilityShader::Flags;

	if (skyBounce.inOcclusion) {
		if (property->flags.any(kSkinned) && property->flags.none(kTreeAnim))
			return precipitationOcclusionMapRenderPassList;
	} else {
		if (property->flags.any(kSkinned))
			return precipitationOcclusionMapRenderPassList;
	}

	if (skyBounce.inOcclusion) {
		if (auto userData = geometry->GetUserData()) {
			RE::BSFadeNode* fadeNode = nullptr;

			RE::NiNode* parent = geometry->parent;
			while (parent && !fadeNode) {
				fadeNode = parent->AsFadeNode();
				parent = parent->parent;
			}

			if (fadeNode) {
				if (auto extraData = fadeNode->GetExtraData("BSX")) {
					auto bsxFlags = (RE::BSXFlags*)extraData;
					auto value = static_cast<int32_t>(bsxFlags->value);

					if (value & (static_cast<int32_t>(RE::BSXFlags::Flag::kRagdoll) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kEditorMarker) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kDynamic) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kAddon) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kNeedsTransformUpdate) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kMagicShaderParticles) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kLights) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kBreakable) |
									static_cast<int32_t>(RE::BSXFlags::Flag::kSearchedBreakable))) {
						return precipitationOcclusionMapRenderPassList;
					}
				}
			}
		}
	}

	bool valid = false;

	if (skyBounce.inOcclusion) {
		valid = property->flags.any(kZBufferWrite) && property->flags.none(kRefraction, kTempRefraction, kLODLandscape, kEyeReflect, kDecal, kDynamicDecal);
	} else {
		valid = property->flags.any(kZBufferWrite) && property->flags.none(kRefraction, kTempRefraction, kMultiTextureLandscape, kNoLODLandBlend, kLODLandscape, kEyeReflect, kDecal, kDynamicDecal);
	}

	if (valid) {
		if (geometry->worldBound.radius > 32) {
			stl::enumeration<RE::BSUtilityShader::Flags> technique;
			technique.set(RenderDepth);

			if (property->flags.any(kVertexColors)) {
				technique.set(Vc);
			}

			const auto alphaProperty = static_cast<RE::NiAlphaProperty*>(geometry->GetGeometryRuntimeData().alphaProperty.get());
			if (alphaProperty && alphaProperty->GetAlphaTesting()) {
				technique.set(Texture);
				technique.set(AlphaTest);
			}

			if (property->flags.any(kLODObjects, kHDLODObjects)) {
				technique.set(LodObject);
			}

			if (property->flags.any(kTreeAnim)) {
				technique.set(TreeAnim);
			}

			precipitationOcclusionMapRenderPassList->EmplacePass(
				globals::game::utilityShader,
				property,
				geometry,
				technique.underlying() + static_cast<uint32_t>(ShaderTechnique::UtilityGeneralStart));
		}
	}
	return precipitationOcclusionMapRenderPassList;
}

void SkyBounce::SetViewFrustum::thunk(RE::NiCamera* a_camera, RE::NiFrustum* a_frustum)
{
	auto& skyBounce = globals::pipeline::skyBounce;

	if (skyBounce.inOcclusion && a_frustum) {
		uint corner = skyBounce.frameCount % 4;

		float frustumSize = a_frustum->fTop;

		a_frustum->fBottom = (corner == 0 || corner == 1) ? -frustumSize : 0.0f;
		a_frustum->fLeft = (corner == 0 || corner == 2) ? -frustumSize : 0.0f;
		a_frustum->fRight = (corner == 1 || corner == 3) ? frustumSize : 0.0f;
		a_frustum->fTop = (corner == 2 || corner == 3) ? frustumSize : 0.0f;
	}

	func(a_camera, a_frustum);
}

void SkyBounce::RenderOcclusion()
{
	ZoneScopedS(8);
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto sky = globals::game::sky;
	if (!shaderCache || !state || !renderer) {
		logger::error("[SkyBounce] Required renderer state is unavailable; using the game precipitation path");
		Main_Precipitation_RenderOcclusion::func();
		return;
	}

	if (!shaderCache->IsEnabled()) {
		TracyD3D11Zone(globals::state->tracyCtx, "Precipitation Mask");
		state->BeginPerfEvent("Precipitation Mask");
		Main_Precipitation_RenderOcclusion::func();
		state->EndPerfEvent();
		return;
	}

	if (sky) {
		if (!Util::IsInterior()) {
			static bool doPrecip = false;

			auto precip = sky->precip;
			if (!precip) {
				Main_Precipitation_RenderOcclusion::func();
				return;
			}

			{
				TracyD3D11Zone(globals::state->tracyCtx, "Precipitation Mask");
				state->BeginPerfEvent("Precipitation Mask");

				doPrecip = false;

				auto precipObject = precip->currentPrecip;
				if (!precipObject) {
					precipObject = precip->lastPrecip;
				}
				if (precipObject) {
					precip->SetupMask();
					auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
					auto shaderProp = effect.get();
					auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);
					if (particleShaderProperty && particleShaderProperty->particleEmitter) {
						auto rain = static_cast<RE::BSParticleShaderRainEmitter*>(particleShaderProperty->particleEmitter);

						globals::profiler->BeginPass("SkyBounce::PrecipMask");
						precip->RenderMask(rain);
						globals::profiler->EndPass();
					}
				}

				state->EndPerfEvent();
			}

			{
				TracyD3D11Zone(globals::state->tracyCtx, "SkyBounce Mask");
				state->BeginPerfEvent("SkyBounce Mask");

				if (queuedResetSkyBounce)
					ResetSkyBounce();

				frameCount++;

				auto& precipitation = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
				RE::BSGraphics::DepthStencilData precipitationCopy = precipitation;

				precipitation.depthSRV = texOcclusion->srv.get();
				precipitation.texture = texOcclusion->resource.get();
				precipitation.views[0] = texOcclusion->dsv.get();

				static float& PrecipitationShaderCubeSize = (*(float*)REL::RelocationID(515451, 401590).address());
				float originalPrecipitationShaderCubeSize = PrecipitationShaderCubeSize;

				static RE::NiPoint3& PrecipitationShaderDirection = (*(RE::NiPoint3*)REL::RelocationID(515509, 401648).address());
				RE::NiPoint3 originalParticleShaderDirection = PrecipitationShaderDirection;

				inOcclusion = true;
				PrecipitationShaderCubeSize = occlusionDistance;

				float originaLastCubeSize = precip->lastCubeSize;
				precip->lastCubeSize = PrecipitationShaderCubeSize;

				float2 vPoint;
				{
					constexpr float rcpRandMax = 1.f / RAND_MAX;
					static int randSeed = std::rand();
					static uint randFrameCount = 0;

					// r2 sequence
					vPoint = float2(randSeed * rcpRandMax) + (float)randFrameCount * float2(0.245122333753f, 0.430159709002f);
					vPoint.x -= static_cast<unsigned long long>(vPoint.x);
					vPoint.y -= static_cast<unsigned long long>(vPoint.y);

					randFrameCount++;
					if (randFrameCount == 1000) {
						randFrameCount = 0;
						randSeed = std::rand();
					}

					// disc transformation
					vPoint.x = sqrt(vPoint.x * sin(settings.MaxZenith));
					vPoint.y *= 6.28318530718f;

					vPoint = { vPoint.x * cos(vPoint.y), vPoint.x * sin(vPoint.y) };
				}

				float3 PrecipitationShaderDirectionF = -float3{ vPoint.x, vPoint.y, sqrt(std::max(1.0f - vPoint.LengthSquared(), 0.0f)) };
				PrecipitationShaderDirectionF.Normalize();

				PrecipitationShaderDirection = { PrecipitationShaderDirectionF.x, PrecipitationShaderDirectionF.y, PrecipitationShaderDirectionF.z };

				static REL::Relocation<void(RE::Precipitation*, RE::NiPointer<RE::NiCamera>)> _computeProjection{ REL::RelocationID(25643, 26185) };
				{
					ZoneScopedN("SkyBounce - Setup Projection");
					_computeProjection(precip, precip->occlusionData.camera);
					precip->SetupMask();
				}

				BSParticleShaderRainEmitter* rain = new BSParticleShaderRainEmitter;
				{
					TracyD3D11Zone(state->tracyCtx, "SkyBounce - Render Height Map");
					globals::profiler->BeginPass("SkyBounce::OcclusionMask");
					precip->RenderMask((RE::BSParticleShaderRainEmitter*)rain);
					globals::profiler->EndPass();
				}
				inOcclusion = false;

				OcclusionDir = -float4{ PrecipitationShaderDirectionF.x, PrecipitationShaderDirectionF.y, PrecipitationShaderDirectionF.z, 0 };
				OcclusionTransform = ((RE::BSParticleShaderRainEmitter*)rain)->occlusionProjection;

				delete rain;

				PrecipitationShaderCubeSize = originalPrecipitationShaderCubeSize;
				precip->lastCubeSize = originaLastCubeSize;

				PrecipitationShaderDirection = originalParticleShaderDirection;

				precipitation = precipitationCopy;

				{
					ZoneScopedN("SkyBounce - Restore Projection");
					_computeProjection(precip, precip->occlusionData.camera);
				}

				state->EndPerfEvent();
			}
		}
	}
}

void SkyBounce::CaptureShadowCascadeSRV()
{
	auto context = globals::d3d::context;
	if (!context)
		return;

	ID3D11ShaderResourceView* srv = nullptr;
	context->PSGetShaderResources(4, 1, &srv);
	if (shadowCascadeSRV)
		shadowCascadeSRV->Release();
	shadowCascadeSRV = srv;
}

void SkyBounce::Main_Precipitation_RenderOcclusion::thunk()
{
	globals::pipeline::skyBounce.RenderOcclusion();
}

RE::BSEventNotifyControl SkyBounce::MenuOpenCloseEventHandler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	// When entering a new cell through a loadscreen, update every frame until completion
	if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
		if (!a_event->opening)
			globals::pipeline::skyBounce.queuedResetSkyBounce = true;
	}

	return RE::BSEventNotifyControl::kContinue;
}
#undef I18N_KEY_PREFIX
