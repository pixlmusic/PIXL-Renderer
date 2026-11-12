#include "RadiantGrid.h"
#include "HybridGI.h"
#include "MaterialForge.h"
#include "NaturalLighting.h"
#include "LinearLightCore.h"

#include "I18n/I18n.h"
#include "Menu/ThemeManager.h"
#include "Renderer/LightTransportWorld.h"
#include "Shadercache.h"
#include "State.h"
#include "Utils/ExternalEmittance.h"

#include <RE/B/BSLightingShaderMaterialGlowmap.h>
#include <RE/N/NiPointLight.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <numbers>

namespace
{
	constexpr std::uint32_t kShadowMaskChannelCount = 4;
	constexpr std::uint32_t kNoShadowMaskIndex = 255;

	bool IsPromotableLocalLight(RE::NiLight* a_light)
	{
		auto* pointLight = a_light ? skyrim_cast<RE::NiPointLight*>(a_light) : nullptr;
		if (!pointLight)
			return false;
		const float radius = pointLight->GetLightRuntimeData().radius.x;
		return std::isfinite(radius) && radius > 0.0f;
	}

	std::uint32_t GetShadowMaskIndex(RE::BSShadowLight* a_light)
	{
		return a_light ? a_light->GetRuntimeData().maskIndex : kNoShadowMaskIndex;
	}

	bool IsShadowMapRendered(RE::ShadowSceneNode* a_sceneNode, RE::BSShadowLight* a_light)
	{
		if (!a_sceneNode || !a_light || GetShadowMaskIndex(a_light) >= kShadowMaskChannelCount)
			return false;

		// This membership is stable for a render frame, but shadow setup is reached
		// once per geometry. Memoize the active-list lookup so large scenes do not
		// rescan Skyrim's shadow-light vector for every object/light pair.
		struct MembershipCacheEntry
		{
			const RE::ShadowSceneNode* sceneNode = nullptr;
			const RE::BSShadowLight* light = nullptr;
			uint32_t frame = UINT32_MAX;
			bool active = false;
		};
		thread_local std::array<MembershipCacheEntry, 256> membershipCache{};
		const uint32_t frame = globals::state ? globals::state->frameCount : UINT32_MAX - 1u;
		const auto sceneKey = reinterpret_cast<std::uintptr_t>(a_sceneNode) >> 4u;
		const auto lightKey = reinterpret_cast<std::uintptr_t>(a_light) >> 4u;
		auto& cached = membershipCache[(sceneKey ^ lightKey) % membershipCache.size()];
		if (cached.sceneNode == a_sceneNode && cached.light == a_light && cached.frame == frame)
			return cached.active;

		// The accumulator is a transient raw-pointer work list. In practice it can
		// retain entries that are no longer selected, while activeShadowLights owns
		// the references Skyrim actually selected for this render. Use that strong
		// list for both mask validity and cache capture to avoid stale channels.
		const auto& active = a_sceneNode->GetRuntimeData().activeShadowLights;
		const bool isActive = std::any_of(active.begin(), active.end(), [a_light](const auto& entry) {
			return entry.get() == a_light;
		});
		cached = { a_sceneNode, a_light, frame, isActive };
		return isActive;
	}

	RE::ShadowSceneNode* GetActiveShadowSceneNode(RE::ShadowSceneNode* a_fallback = nullptr)
	{
		auto* shaderManager = globals::game::smState;
		if (!shaderManager)
			return nullptr;

		const auto isKnownSceneNode = [&](RE::ShadowSceneNode* a_candidate) {
			if (!a_candidate)
				return false;
			for (auto* sceneNode : shaderManager->shadowSceneNode)
				if (sceneNode == a_candidate)
					return true;
			return false;
		};

		if (isKnownSceneNode(a_fallback))
			return a_fallback;

		// Skyrim 1.7.104 can render through a nonzero shader-manager scene node.
		// Read the node selected by the active accumulator and validate membership
		// before using it for point-light enumeration or shadow-atlas descriptors.
		if (auto accumulatorAddress = globals::game::currentAccumulator.get();
			accumulatorAddress && *accumulatorAddress) {
			auto* active = (*accumulatorAddress)->GetRuntimeData().activeShadowSceneNode;
			if (isKnownSceneNode(active))
				return active;
		}

		if (isKnownSceneNode(shaderManager->shadowSceneNode[0]))
			return shaderManager->shadowSceneNode[0];
		return nullptr;
	}

	void TryAssignShadowMask(RadiantGrid::LightData& a_light, RE::BSShadowLight* a_shadowLight,
		RE::ShadowSceneNode* a_sceneNode)
	{
		const auto maskIndex = GetShadowMaskIndex(a_shadowLight);
		a_light.shadowMaskIndex = maskIndex;
		if (maskIndex < kShadowMaskChannelCount && IsShadowMapRendered(a_sceneNode, a_shadowLight)) {
			a_light.lightFlags.set(RadiantGrid::LightFlags::Shadow);
		} else {
			a_light.lightFlags.reset(RadiantGrid::LightFlags::Shadow);
		}
	}

	struct LocalShadowRenderInfo
	{
		uint32_t engineSlice = UINT32_MAX;
		uint32_t srvSlice = UINT32_MAX;
		uint32_t maskIndex = RadiantGrid::NO_SHADOW_MASK_INDEX;
		int32_t renderTarget = -1;
		float biasScale = 0.0f;
		DirectX::XMFLOAT4X4 lightTransform{};
	};

	bool CanDirectCopyLocalShadow(const D3D11_TEXTURE2D_DESC& a_source, uint32_t a_cacheResolution, DXGI_FORMAT a_cacheFormat)
	{
		const DXGI_FORMAT typelessFormat = a_cacheFormat == DXGI_FORMAT_R16_UNORM ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
		return a_source.Width == a_cacheResolution && a_source.Height == a_cacheResolution &&
			(a_source.Format == a_cacheFormat || a_source.Format == typelessFormat);
	}

	// D3D11 views return ID3D11Resource interfaces while the renderer stores an
	// ID3D11Texture2D interface. Compare canonical COM identity instead of raw
	// interface addresses; interface pointer equality is not the resource
	// association contract and can reject a valid runtime target.
	bool SameD3DResourceIdentity(REX::W32::ID3D11Resource* a_lhs, REX::W32::ID3D11Resource* a_rhs)
	{
		if (!a_lhs || !a_rhs)
			return false;
		REX::W32::IUnknown* lhsIdentity = nullptr;
		REX::W32::IUnknown* rhsIdentity = nullptr;
		const auto lhsResult = a_lhs->QueryInterface(REX::W32::IID_IUnknown, reinterpret_cast<void**>(&lhsIdentity));
		const auto rhsResult = a_rhs->QueryInterface(REX::W32::IID_IUnknown, reinterpret_cast<void**>(&rhsIdentity));
		const bool same = SUCCEEDED(lhsResult) && SUCCEEDED(rhsResult) && lhsIdentity == rhsIdentity;
		if (lhsIdentity)
			lhsIdentity->Release();
		if (rhsIdentity)
			rhsIdentity->Release();
		return same;
	}

	bool ReadLocalShadowRenderInfo(RE::BSShadowLight* a_light, uint32_t a_descriptorIndex, LocalShadowRenderInfo& a_info)
	{
		if (!a_light)
			return false;
		auto& runtimeData = a_light->GetRuntimeData();
		if (a_descriptorIndex >= runtimeData.shadowmapDescriptors.size())
			return false;
		const auto& descriptor = runtimeData.shadowmapDescriptors[a_descriptorIndex];
		if (!descriptor.isEnabled)
			return false;
		a_info.engineSlice = descriptor.shadowmapIndex;
		a_info.renderTarget = static_cast<int32_t>(descriptor.renderTarget);
		a_info.maskIndex = runtimeData.maskIndex;
		a_info.biasScale = runtimeData.shadowBiasScale;
		static_assert(sizeof(a_info.lightTransform) == sizeof(descriptor.lightTransform));
		std::memcpy(&a_info.lightTransform, &descriptor.lightTransform, sizeof(a_info.lightTransform));
		for (const auto& row : a_info.lightTransform.m)
			for (const float component : row)
				if (!std::isfinite(component))
					return false;
		return true;
	}

	bool IsSphereInCameraFrustum(const RE::NiCamera* a_camera, const RE::NiPoint3& a_position, float a_radius)
	{
		if (!a_camera)
			return true;
		const auto& frustum = a_camera->GetRuntimeData2().viewFrustum;
		const auto& world = a_camera->world;
		const RE::NiPoint3 forward{ world.rotate.entry[0][0], world.rotate.entry[1][0], world.rotate.entry[2][0] };
		const RE::NiPoint3 right{ world.rotate.entry[0][2], world.rotate.entry[1][2], world.rotate.entry[2][2] };
		const RE::NiPoint3 up{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
		const auto signedDistance = [&](RE::NiPoint3 normal, RE::NiPoint3 point) {
			const auto offset = a_position - point;
			return normal.x * offset.x + normal.y * offset.y + normal.z * offset.z;
		};
		const auto& origin = world.translate;
		if (signedDistance(forward, origin + forward * frustum.fNear) < -a_radius ||
			signedDistance(forward * -1.0f, origin + forward * frustum.fFar) < -a_radius)
			return false;
		if (frustum.bOrtho) {
			if (signedDistance(right, origin + right * frustum.fLeft) < -a_radius ||
				signedDistance(right * -1.0f, origin + right * frustum.fRight) < -a_radius ||
				signedDistance(up, origin + up * frustum.fTop) < -a_radius ||
				signedDistance(up * -1.0f, origin + up * frustum.fBottom) < -a_radius)
				return false;
		} else {
			const auto sidePlane = [&](RE::NiPoint3 axis, float slope, float forwardSign, float axisSign) {
				const float invLength = 1.0f / std::sqrt(slope * slope + 1.0f);
				const auto normal = forward * (forwardSign * slope * invLength) + axis * (axisSign * invLength);
				return signedDistance(normal, origin);
			};
			if (sidePlane(right, frustum.fLeft, -1.0f, +1.0f) < -a_radius ||
				sidePlane(right, frustum.fRight, +1.0f, -1.0f) < -a_radius ||
				sidePlane(up, frustum.fTop, +1.0f, -1.0f) < -a_radius ||
				sidePlane(up, frustum.fBottom, -1.0f, +1.0f) < -a_radius)
				return false;
		}
		return true;
	}
}

#define I18N_KEY_PREFIX "feature.radiant_grid."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RadiantGrid::Settings,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableLightsVisualisation,
	LightsVisualisationMode,
	EnableScreenSpaceLocalShadows,
	EnableLocalShadows,
	PromoteLocalLightsToShadowCasters,
	LocalShadowSlots,
	LocalShadowResolution,
	LocalShadowSamples,
	LocalShadowFilterScale)

// Keep synchronized with RadiantGrid/Common.hlsli::MAX_CLUSTER_LIGHTS. Dense
// interiors and particle-heavy scenes can legitimately exceed 128 overlapping
// lights; truncating the list there creates visible 64-pixel cluster boundaries.
// Reserve the original 256 entries per cluster on the CPU as well so the GPU
// never relies on the pre-audit undersized allocation.
static constexpr uint CLUSTER_MAX_LIGHTS = 256;

void RadiantGrid::DrawSettings()
{
	auto shaderCache = globals::shaderCache;
	auto& materialSettings = globals::pipeline::materialForge.settings;

	ImGui::Checkbox(T(TKEY("enable_particle_lights"), "Enable Particle Lights"), &settings.EnableParticleLights);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds supported effect/particle emitters to the clustered local-light list. Updates in real time.");
	if (settings.EnableParticleLights) {
		ImGui::Checkbox("Cull Configured Particle Lights", &settings.EnableParticleLightsCulling);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Honours per-effect particle-light culling rules from configuration files. Updates in real time and can reduce unnecessary clustered lights.");
	}

	ImGui::Spacing();
	ImGui::SeparatorText("Local Light Shading");
	ImGui::Checkbox("Cache Native Local Light Shadows", &settings.EnableLocalShadows);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Reuses Skyrim's rendered point and spot light shadow maps for clustered lighting. This does not make non-shadow-casting lights cast shadows. Uses additional GPU memory and shadow-map copy work.");
	if (settings.EnableLocalShadows) {
		ImGui::Checkbox("Promote Local Lights to Shadow Casters", &settings.PromoteLocalLightsToShadowCasters);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Asks Skyrim to allocate shadow maps for eligible local lights that do not already cast shadows. This can be expensive and is experimental; leave off while validating native shadow caching.");
		ImGui::SliderFloat("Local Shadow Edge Softness", &settings.LocalShadowFilterScale,
			LOCAL_SHADOW_FILTER_SCALE_MIN, LOCAL_SHADOW_FILTER_SCALE_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Widens the cached local shadow filter. A small increase lets nearby light reach a little farther past edges such as a fireplace lip. Higher values soften all cached point and spot light shadows and add no extra samples.");
	}
	ImGui::Checkbox("Screen-Space Point Light Shadows", &settings.EnableScreenSpaceLocalShadows);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Checks camera depth along the receiver-to-light path for up to four influential local lights. It supplements native shadows for visible blockers; blockers outside the screen cannot be detected.");
	bool physicalFalloff = materialSettings.EnablePhysicalLocalLightFalloff != 0;
	if (ImGui::Checkbox("Inverse-Square Local Lights", &physicalFalloff))
		materialSettings.EnablePhysicalLocalLightFalloff = physicalFalloff ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Blends the conventional radius falloff toward PIXL's regularized inverse-square (1/d²) model. Disable to use the configured vanilla-style falloff.");
	if (physicalFalloff) {
		ImGui::SliderFloat("Physical Falloff Strength", &materialSettings.PhysicalLocalLightFalloffStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) ImGui::TextWrapped("0 matches conventional radius falloff; 1 uses the full inverse-square model.");
		ImGui::SliderFloat("Local Emitter Radius", &materialSettings.LocalLightMinimumDistance, 7.0f, 70.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) ImGui::TextWrapped("Bounds attenuation close to a light source. Larger values broaden the near-light response.");
	}
	bool localContactShadows = materialSettings.EnableLocalContactShadows != 0;
	if (ImGui::Checkbox("Local Contact Shadows", &localContactShadows))
		materialSettings.EnableLocalContactShadows = localContactShadows ? 1u : 0u;
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds a short screen-space contact ray to influential clustered local lights. Existing native shadow maps remain authoritative; off-screen blockers cannot be detected.");
	if (localContactShadows) {
		int contactLightCount = static_cast<int>(materialSettings.LocalContactShadowLightCount);
		if (ImGui::SliderInt("Contact-Shadow Lights / Pixel", &contactLightCount, 1, 4, "%d", ImGuiSliderFlags_AlwaysClamp))
			materialSettings.LocalContactShadowLightCount = static_cast<uint>(std::clamp(contactLightCount, 1, 4));
		ImGui::SliderFloat("Contact-Shadow Length", &materialSettings.LocalContactShadowLength, 16.0f, 48.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Sets the short near-surface ray distance in world units. Longer rays can find more nearby blockers but cost the same six depth taps and are more sensitive to camera motion.");
		ImGui::SliderFloat("Contact-Shadow Strength", &materialSettings.LocalContactShadowStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
	ImGui::Spacing();

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("statistics"), "Statistics"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Clustered Light Count: %u", lightCount);
		ImGui::Text("Particle Emitters: %u", particleEmitterLightCount);
		ImGui::Text("Glow-mapped Emitters: %u", glowMappedEmitterLightCount);
		ImGui::Text("Local Shadows: %u cached / %u tracked / %u rendered this frame",
			localShadowStatCached, localShadowStatTracked, localShadowStatRendered);
		ImGui::Text("Emitter shadow matches: %u particle / %u glow-mapped",
			particleShadowMatchCount, glowShadowMatchCount);

		ImGui::TreePop();
	}

	if (globals::state->IsDeveloperMode())
		ImGui::SeparatorText(T(TKEY("debug"), "Debug"));

	if (globals::state->IsDeveloperMode() && ImGui::TreeNode(T(TKEY("light_limit_vis"), "Light Limit Visualization"))) {
		ImGui::Checkbox(T(TKEY("enable_lights_vis"), "Enable Lights Visualisation"), &settings.EnableLightsVisualisation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_lights_vis_tooltip"), "Enables a clustered-light diagnostic overlay. Changing this clears and automatically recompiles affected lighting shaders."));
		}

		{
			static const char* comboOptions[] = { "Light Limit", "Strict Lights Count", "Clustered Lights Count", "Shadow Mask" };
			int visualizationMode = static_cast<int>(settings.LightsVisualisationMode);
			if (ImGui::Combo(T(TKEY("lights_vis_mode"), "Lights Visualisation Mode"), &visualizationMode, comboOptions, 4))
				settings.LightsVisualisationMode = static_cast<uint>(std::clamp(visualizationMode, 0, 3));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("lights_vis_mode_tooltip"),
									  " - Visualise the light limit. Red when the \"strict\" light limit is reached (portal-strict lights).\n"
									  " - Visualise the number of strict lights.\n"
									  " - Visualise the number of clustered lights.\n"
									  " - Visualize the Shadow Mask.\n"));
			}
		}
		currentEnableLightsVisualisation = settings.EnableLightsVisualisation;
		if (previousEnableLightsVisualisation != currentEnableLightsVisualisation) {
			globals::state->SetDefines(settings.EnableLightsVisualisation ? "LLFDEBUG" : "");
			shaderCache->Clear(RE::BSShader::Type::Lighting);
			previousEnableLightsVisualisation = currentEnableLightsVisualisation;
		}

		ImGui::TreePop();
	}
}

void RadiantGrid::DrawOverlay()
{
	if (!settings.EnableLightsVisualisation)
		return;
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale();
	ImGui::SetNextWindowPos(ImVec2(pos, pos), ImGuiCond_Always);
	ImGui::Begin("##LLFDebug", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	Util::Text::Error("%s", T(TKEY("debug_feature_enabled"), "DEBUG FEATURE - LIGHT LIMIT VISUALISATION ENABLED"));
	ImGui::End();
}

RadiantGrid::PerFrame RadiantGrid::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.EnableScreenSpaceLocalShadows = settings.EnableScreenSpaceLocalShadows ? 1u : 0u;
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
	if (!IsLocalShadowsEnabled())
		return perFrame;
	perFrame.EnableLocalShadows = true;
	const auto sanitize = [](float value, float minValue, float maxValue) {
		return std::isfinite(value) ? std::clamp(value, minValue, maxValue) : minValue;
	};
	const float texelSize = 1.0f / static_cast<float>(std::max(localShadowCacheResolution, 1u));
	static RE::Setting* poissonRadiusScale = nullptr;
	static bool poissonRadiusScaleLookedUp = false;
	if (!poissonRadiusScaleLookedUp && globals::game::iniSettingCollection) {
		poissonRadiusScaleLookedUp = true;
		poissonRadiusScale = globals::game::iniSettingCollection->GetSetting("fPoissonRadiusScale:Display");
		if (!poissonRadiusScale && globals::game::iniPrefSettingCollection)
			poissonRadiusScale = globals::game::iniPrefSettingCollection->GetSetting("fPoissonRadiusScale:Display");
	}
	const float poisson = poissonRadiusScale ? sanitize(poissonRadiusScale->data.f, 0.0f, LOCAL_SHADOW_MAX_POISSON_RADIUS) : LOCAL_SHADOW_DEFAULT_POISSON_RADIUS;
	const float filterRadius = localShadowEngineResolution ? poisson / static_cast<float>(localShadowEngineResolution) : poisson * texelSize;
	perFrame.LocalShadowSamples = settings.LocalShadowSamples <= 1 ? 1 : settings.LocalShadowSamples <= 4 ? 4 : 8;
	perFrame.LocalShadowFilterRadius = filterRadius * sanitize(settings.LocalShadowFilterScale, LOCAL_SHADOW_FILTER_SCALE_MIN, LOCAL_SHADOW_FILTER_SCALE_MAX);
	perFrame.LocalShadowTexelSize = texelSize;
	return perFrame;
}

void RadiantGrid::SetupResources()
{
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	clusterSize[0] = ((uint)screenSize.x + 63) / 64;
	clusterSize[1] = ((uint)screenSize.y + 63) / 64;
	clusterSize[2] = 32;
	uint clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

	{
		std::vector<std::pair<const char*, const char*>> clusterDefines;
		clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\PIXL-Shaders\\RadiantGrid\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
		clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\PIXL-Shaders\\RadiantGrid\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");

		lightBuildingCB = new ConstantBuffer(ConstantBufferDesc<LightBuildingCB>());
		lightCullingCB = new ConstantBuffer(ConstantBufferDesc<LightCullingCB>());
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DEFAULT;
		sbDesc.CPUAccessFlags = 0;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;

		std::uint32_t numElements = clusterCount;

		sbDesc.StructureByteStride = sizeof(ClusterAABB);
		sbDesc.ByteWidth = sizeof(ClusterAABB) * numElements;
		clusters = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Clusters");
		srvDesc.Buffer.NumElements = numElements;
		clusters->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		clusters->CreateUAV(uavDesc);

		numElements = 1;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexCounter = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexCounter");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexCounter->CreateUAV(uavDesc);

		numElements = clusterCount * CLUSTER_MAX_LIGHTS;
		sbDesc.StructureByteStride = sizeof(uint32_t);
		sbDesc.ByteWidth = sizeof(uint32_t) * numElements;
		lightIndexList = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightIndexList");
		srvDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightIndexList->CreateUAV(uavDesc);

		numElements = clusterCount;
		sbDesc.StructureByteStride = sizeof(LightGrid);
		sbDesc.ByteWidth = sizeof(LightGrid) * numElements;
		lightGrid = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::LightGrid");
		srvDesc.Buffer.NumElements = numElements;
		lightGrid->CreateSRV(srvDesc);
		uavDesc.Buffer.NumElements = numElements;
		lightGrid->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(LightData);
		sbDesc.ByteWidth = sizeof(LightData) * MAX_LIGHTS;
		lights = eastl::make_unique<Buffer>(sbDesc, nullptr, "LLF::Lights");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_LIGHTS;
		lights->CreateSRV(srvDesc);
	}

	{
		strictLightDataCB = new ConstantBuffer(ConstantBufferDesc<StrictLightDataCB>());
	}
}

void RadiantGrid::Reset()
{
	// Reset is called at every Present. Geometry queues feed the NEXT prepass;
	// clearing them here starves practical lighting and destroys its history.
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	const auto scene = reinterpret_cast<std::uintptr_t>(cell && cell->IsInteriorCell() ?
		static_cast<void*>(cell) : static_cast<void*>(player ? player->GetWorldspace() : nullptr));
	std::scoped_lock lock{ particleLightQueueMutex, particleLightStateMutex };
	if (scene != 0 && scene == particleLightScene)
		return;
	particleLightScene = scene;
	particleLightSceneGeneration.fetch_add(1, std::memory_order_release);
	queuedParticleLights.clear();
	queuedParticleLightOwners.clear();
	persistedParticleLights.clear();
	particleLightTime = 0.0;
	particleEmitterLightCount = 0;
	glowMappedEmitterLightCount = 0;
	particleLightBufferStart = 0;
	particleLightBufferCount = 0;
}

void RadiantGrid::SaveSettings(json& o_json)
{
	o_json = settings;
}

void RadiantGrid::LoadSettings(json& o_json)
{
	settings = o_json;
}

void RadiantGrid::RestoreDefaultSettings()
{
	settings = {};
}

RE::NiNode* GetParentRoomNode(RE::NiAVObject* object)
{
	if (object == nullptr) {
		return nullptr;
	}

	static const auto* roomRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSMultiBoundRoom }.get();
	static const auto* portalRtti = REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSPortalSharedNode }.get();

	const auto* rtti = object->GetRTTI();
	if (rtti == roomRtti || rtti == portalRtti) {
		return static_cast<RE::NiNode*>(object);
	}

	return GetParentRoomNode(object->parent);
}

void RadiantGrid::BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass)
{
	auto shaderCache = globals::shaderCache;

	if (!a_pass || !shaderCache || !shaderCache->IsEnabled())
		return;

	// Skyrim and many mesh replacers author candles, braziers and hearth embers as
	// ordinary glow-mapped lighting geometry. Their emissive pixels reach bloom,
	// but they never pass through the soft-particle light extractor and therefore
	// do not illuminate nearby opaque surfaces. Observe those deliberately named
	// incandescent materials here and queue one bounded local emitter for the next
	// Radiant Grid update. Classification is cached per geometry and remains under
	// the existing Enable Particle Lights master switch.
	QueueIncandescentGeometryLight(a_pass);

	strictLightDataTemp.NumStrictLights = 0;
	strictLightDataTemp.ShadowBitMask = 0;
	strictLightDataTemp.FirstPerson = localShadowFirstPersonPass ? 1u : 0u;
	const auto cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	eyePositionCached = { cameraPosition.x, cameraPosition.y, cameraPosition.z };
	strictLightDataTemp.WorldEyePosition = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f };

	// 1.7.104 changed BSLightingShader::SetupGeometry's internal point-light
	// call site, so the binary call hook is intentionally not installed there.
	// RADIANT_GRID lighting replaces Skyrim's local-light loop and consumes this
	// buffer for portal-strict interior lights; collect the same render-pass data
	// from the stable SetupGeometry vfunc wrapper instead.
	if (REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_104 && !pointLightSetupCallsiteHooked) {
		BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(a_pass);
	}

	strictLightDataTemp.RoomIndex = -1;
	if (!roomNodes.empty()) {
		if (RE::NiNode* roomNode = GetParentRoomNode(a_pass->geometry)) {
			if (auto it = roomNodes.find(roomNode); it != roomNodes.cend()) {
				strictLightDataTemp.RoomIndex = it->second;
			}
		}
	}
}

void RadiantGrid::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass)
{
	const auto accumulatorAddress = globals::game::currentAccumulator.get();
	if (!a_pass || !a_pass->sceneLights || !accumulatorAddress ||
		!*accumulatorAddress || !globals::game::smState) {
		strictLightDataTemp.NumStrictLights = 0;
		strictLightDataTemp.ShadowBitMask = 0;
		strictLightDataTemp.WorldEyePosition.w = 0.0f;
		worldFallbackLightSignature = 0;
		return;
	}

	auto& isl = globals::pipeline::naturalLighting;

	auto accumulator = *accumulatorAddress;
	auto* activeShadowSceneNode = GetActiveShadowSceneNode(accumulator->GetRuntimeData().activeShadowSceneNode);
	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == globals::game::smState->shadowSceneNode[0];

	const uint32_t availableLights = a_pass->numLights > 0 ? a_pass->numLights - 1 : 0;
	const uint32_t strictLightCapacity = static_cast<uint32_t>(std::size(strictLightDataTemp.StrictLights));
	const uint32_t passLightCount = std::min(availableLights, strictLightCapacity);
	strictLightDataTemp.NumStrictLights = inWorld ? 0 : passLightCount;

	uint32_t writeIdx = 0;
	for (uint32_t i = 0; i < passLightCount; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		if (!bsLight)
			continue;
		auto niLight = bsLight->light.get();
		if (!niLight)
			continue;

		auto& runtimeData = niLight->GetLightRuntimeData();

		LightData light{};
		light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
		light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

		if (isl.loaded) {
			isl.ProcessLight(light, bsLight, niLight);
		} else {
			light.radius = runtimeData.radius.x;
			// light.color *= runtimeData.fade;
			light.fade = runtimeData.fade;
		}

		light.fade *= bsLight->lodDimmer;

		SetLightPosition(light, niLight->world.translate, inWorld);

		if (i < a_pass->numShadowLights && bsLight->IsShadowLight()) {
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			TryAssignShadowMask(light, shadowLight, activeShadowSceneNode);
			light.lightFlags.set(LightFlags::ShadowCaster);
			if (localShadowCache && localShadowBuffer) {
				if (auto* caster = FindLocalShadowCaster(shadowLight); caster && caster->slice >= 0 &&
					(caster->shadowParams.x != LOCAL_SHADOW_TYPE_OMNI || caster->secondarySlice == caster->slice + 1) &&
					caster->lastRenderedFrame != 0 && caster->lastSeenFrame == localShadowFrame) {
					light.localShadowIndex = static_cast<uint32_t>(caster->slice);
					light.lightFlags.set(LightFlags::LocalShadow);
					// Retain the native map index for bookkeeping; the shader selects the
					// cached depth for this resident caster and uses the native mask only
					// for lights without a valid cache entry.
				}
			}
		}

		strictLightDataTemp.StrictLights[writeIdx++] = light;
	}
	strictLightDataTemp.NumStrictLights = inWorld ? 0u : writeIdx;
	// Keep the engine-selected per-draw lights available as a bounded fallback
	// when the clustered grid returns an empty cell. World-space deferred shading
	// normally uses the full clustered list; the fallback prevents cluster-bound
	// disagreement from dropping all local illumination for an object.
	strictLightDataTemp.WorldEyePosition.w = inWorld ? static_cast<float>(writeIdx) : 0.0f;
	if (inWorld && writeIdx != 0 && !worldFallbackLightDiagnosticLogged) {
		logger::info("[RadiantGrid] Skyrim per-draw clustered-light fallback armed ({} engine-selected lights, 15-light bound).", writeIdx);
		worldFallbackLightDiagnosticLogged = true;
	}
	worldFallbackLightSignature = 0;
	if (inWorld && writeIdx != 0) {
		worldFallbackLightSignature = 1469598103934665603ull;
		const auto* bytes = reinterpret_cast<const uint8_t*>(strictLightDataTemp.StrictLights);
		const std::size_t byteCount = sizeof(LightData) * writeIdx;
		for (std::size_t i = 0; i < byteCount; ++i) {
			worldFallbackLightSignature ^= bytes[i];
			worldFallbackLightSignature *= 1099511628211ull;
		}
	}

	const uint32_t shadowLightCount = std::min(static_cast<uint32_t>(a_pass->numShadowLights), availableLights);
	for (uint32_t i = 0; i < shadowLightCount; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		if (!bsLight || !bsLight->IsShadowLight())
			continue;
		auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
		auto& maskIndex = shadowLight->GetRuntimeData().maskIndex;
		// The strict-light shader exposes a 32-bit mask. Some modded interior
		// light sets report larger indices; the shader treats these as unsupported,
		// so never perform an undefined shift by >= 32 on the CPU.
		if (maskIndex < kShadowMaskChannelCount && IsShadowMapRendered(activeShadowSceneNode, shadowLight))
			strictLightDataTemp.ShadowBitMask |= (1u << maskIndex);
	}
}

void RadiantGrid::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	const auto accumulatorAddress = globals::game::currentAccumulator.get();
	if (!shaderCache || !shaderCache->IsEnabled() || !context || !smState ||
		!strictLightDataCB || !accumulatorAddress || !*accumulatorAddress) {
		return;
	}

	auto accumulator = *accumulatorAddress;

	const auto isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == smState->shadowSceneNode[0];
	const auto roomIndex = strictLightDataTemp.RoomIndex;
	const auto shadowBitMask = strictLightDataTemp.ShadowBitMask;
	const auto firstPerson = strictLightDataTemp.FirstPerson;
	const auto worldFallbackLightCount = static_cast<uint32_t>(std::max(strictLightDataTemp.WorldEyePosition.w, 0.0f));
	const RE::NiPoint3 worldEyePosition{ strictLightDataTemp.WorldEyePosition.x, strictLightDataTemp.WorldEyePosition.y, strictLightDataTemp.WorldEyePosition.z };
	const bool worldEyeMoved = firstPerson != 0 && worldEyePosition != previousWorldEyePosition;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex ||
		shadowBitMask != previousShadowBitMask || firstPerson != previousFirstPerson || worldEyeMoved ||
		worldFallbackLightCount != previousWorldFallbackLightCount ||
		worldFallbackLightSignature != previousWorldFallbackLightSignature) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
		previousShadowBitMask = shadowBitMask;
		previousFirstPerson = firstPerson;
		previousWorldFallbackLightCount = worldFallbackLightCount;
		previousWorldFallbackLightSignature = worldFallbackLightSignature;
		previousWorldEyePosition = worldEyePosition;
	}

	if (frameChecker.IsNewFrame()) {
		ID3D11Buffer* buffer = { strictLightDataCB->CB() };
		context->PSSetConstantBuffers(3, 1, &buffer);
	}
}

void RadiantGrid::SetLightPosition(RadiantGrid::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached)
{
	RE::NiPoint3 eyePosition;

	if (a_cached) {
		eyePosition = eyePositionCached;
	} else {
		eyePosition = Util::GetEyePosition();
	}

	auto worldPos = a_initialPosition - eyePosition;
	a_light.positionWS.data.x = worldPos.x;
	a_light.positionWS.data.y = worldPos.y;
	a_light.positionWS.data.z = worldPos.z;
}

void RadiantGrid::Prepass()
{
	auto context = globals::d3d::context;
	auto state = globals::state;
	if (!context || !state || !lights || !lightIndexList || !lightGrid)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "RadiantGrid Prepass");
	state->BeginPerfEvent("RadiantGrid Prepass");
	UpdateLights();
	// StartDeferred uploads feature data before module prepasses. Publish the
	// dimensions actually used by this rebuild (including a DRS transition),
	// otherwise pixel shaders can index today's grid with yesterday's strides.
	state->UpdateFeatureData(state->inWorld);
	PIXL::Renderer::LightTransportWorld::Get().PublishLocalLights(
		lights->srv.get(), lightIndexList->srv.get(), lightGrid->srv.get(),
		particleLightBufferStart, particleLightBufferCount, lightCount);

	ID3D11ShaderResourceView* views[3]{};
	views[0] = lights->srv.get();
	views[1] = lightIndexList->srv.get();
	views[2] = lightGrid->srv.get();
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);

	state->EndPerfEvent();
}

bool RadiantGrid::IsValidLight(RE::BSLight* a_light)
{
	return a_light && a_light->light && !a_light->light->GetFlags().any(RE::NiAVObject::Flag::kHidden);
}

void RadiantGrid::ResetGeometryLightData()
{
	strictLightDataTemp.NumStrictLights = 0;
	strictLightDataTemp.ShadowBitMask = 0;
	strictLightDataTemp.WorldEyePosition.w = 0.0f;
	worldFallbackLightSignature = 0;
}

bool RadiantGrid::IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

void RadiantGrid::PostPostLoad()
{
	particleLightConfigs.Load();
	Hooks::Install();
	// Install the cache/scheduling hooks once, while leaving promotion controlled
	// by its explicit opt-in setting. The wrappers are transparent when the cache
	// is disabled and allow its UI toggle to take effect without restarting.
	Hooks::InstallLocalShadows();
}

void RadiantGrid::DataLoaded()
{
	auto iMagicLightMaxCount = globals::game::gameSettingCollection->GetSetting("iMagicLightMaxCount");
	if (iMagicLightMaxCount) {
		iMagicLightMaxCount->data.i = MAXINT32;
		logger::info("[RadiantGrid] Unlocked magic light limit");
	} else {
		logger::warn("[RadiantGrid] Could not find iMagicLightMaxCount; retaining the game default");
	}
}

void RadiantGrid::ClearShaderCache()
{
	if (clusterBuildingCS) {
		clusterBuildingCS->Release();
		clusterBuildingCS = nullptr;
	}
	if (clusterCullingCS) {
		clusterCullingCS->Release();
		clusterCullingCS = nullptr;
	}
	if (localShadowCopyCS) {
		localShadowCopyCS->Release();
		localShadowCopyCS = nullptr;
	}
	localShadowCopyCSCompiled = false;
	std::vector<std::pair<const char*, const char*>> clusterDefines;
	clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\PIXL-Shaders\\RadiantGrid\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
	clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\PIXL-Shaders\\RadiantGrid\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");
}

void RadiantGrid::ReleaseLocalShadowResources()
{
	if (globals::d3d::context && (localShadowCache || localShadowBuffer)) {
		ID3D11ShaderResourceView* nullViews[2]{};
		globals::d3d::context->PSSetShaderResources(112, ARRAYSIZE(nullViews), nullViews);
	}
	localShadowCache = nullptr;
	localShadowBuffer = nullptr;
	localShadowCacheSlots = 0;
	localShadowCacheLightCapacity = 0;
	localShadowRequestedSlots = 0;
	localShadowRequestedResolution = 0;
	localShadowCacheResolution = 0;
	localShadowEngineResolution = 0;
	localShadowCacheFormat = DXGI_FORMAT_UNKNOWN;
	localShadowAllocationFailed = false;
	localShadowAllocFailedFrame = 0;
	localShadowSliceOwner.clear();
	for (auto& caster : localShadowCasters) {
		caster.slice = -1;
		caster.secondarySlice = -1;
		caster.lastSelectedFrame = 0;
		caster.selectionStartFrame = 0;
		caster.lastRenderedFrame = 0;
	}
}

bool RadiantGrid::EnsureLocalShadowCopyShader()
{
	if (!localShadowCopyCSCompiled) {
		localShadowCopyCSCompiled = true;
		std::vector<std::pair<const char*, const char*>> defines;
		localShadowCopyCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\PIXL-Shaders\\RadiantGrid\\LocalShadowCopyCS.hlsl", defines, "cs_5_0"));
		if (!localShadowCopyCB)
			localShadowCopyCB = new ConstantBuffer(ConstantBufferDesc<LocalShadowCopyCB>());
	}
	return localShadowCopyCS && localShadowCopyCB;
}

void RadiantGrid::EnsureLocalShadowResources(REX::W32::ID3D11Texture2D* a_engineShadowMaps)
{
	if (!a_engineShadowMaps || !globals::d3d::device)
		return;
	D3D11_TEXTURE2D_DESC engineDesc{};
	REX::W32::D3D11_TEXTURE2D_DESC rexEngineDesc{};
	a_engineShadowMaps->GetDesc(&rexEngineDesc);
	static_assert(sizeof(engineDesc) == sizeof(rexEngineDesc));
	std::memcpy(&engineDesc, &rexEngineDesc, sizeof(engineDesc));
	const uint32_t engineResolution = std::max(engineDesc.Width, 1u);
	// Auto stores two full hemisphere maps for an omni light. Capping only the
	// cache at 2048 keeps this corrected representation within a practical VRAM
	// budget; minimum-depth downsampling preserves thin blockers from the native map.
	const uint32_t requestedResolution = settings.LocalShadowResolution == 0
		? std::min(engineResolution, 2048u)
		: std::max(settings.LocalShadowResolution, LOCAL_SHADOW_MIN_RESOLUTION);
	uint32_t cacheResolution = engineResolution;
	while (cacheResolution > requestedResolution && (cacheResolution % 2u) == 0u)
		cacheResolution /= 2u;
	if (engineResolution % cacheResolution != 0 || !std::has_single_bit(engineResolution / cacheResolution))
		cacheResolution = engineResolution;
	const uint32_t requestedSlots = std::clamp(settings.LocalShadowSlots, MIN_LOCAL_SHADOW_SLOTS, MAX_LOCAL_SHADOW_SLOTS);
	DXGI_FORMAT cacheFormat = DXGI_FORMAT_R32_FLOAT;
	if (engineDesc.Format == DXGI_FORMAT_R16_TYPELESS || engineDesc.Format == DXGI_FORMAT_D16_UNORM || engineDesc.Format == DXGI_FORMAT_R16_UNORM) {
		UINT support = 0;
		if (SUCCEEDED(globals::d3d::device->CheckFormatSupport(DXGI_FORMAT_R16_UNORM, &support)) &&
			(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
			cacheFormat = DXGI_FORMAT_R16_UNORM;
	}
	localShadowDirectCopy = CanDirectCopyLocalShadow(engineDesc, cacheResolution, cacheFormat);
	localShadowEngineMipLevels = std::max(engineDesc.MipLevels, 1u);
	localShadowEngineSlices = engineDesc.ArraySize;
	if (!localShadowDirectCopy && !EnsureLocalShadowCopyShader()) {
		ReleaseLocalShadowResources();
		logger::error("[RadiantGrid] Local shadow cache disabled: downsample shader failed to compile");
		return;
	}
	const uint32_t frame = globals::state ? globals::state->frameCount : 0;
	if (!localShadowCache && localShadowAllocationFailed &&
		requestedSlots == localShadowRequestedSlots && requestedResolution == localShadowRequestedResolution &&
		engineResolution == localShadowEngineResolution && cacheFormat == localShadowCacheFormat &&
		frame - localShadowAllocFailedFrame < LOCAL_SHADOW_ALLOC_RETRY_FRAMES)
		return;
	if (localShadowCache && requestedSlots == localShadowRequestedSlots && requestedResolution == localShadowRequestedResolution &&
		engineResolution == localShadowEngineResolution && cacheFormat == localShadowCacheFormat)
		return;
	ReleaseLocalShadowResources();
	// Omni lights need two independent paraboloid maps. Keep the user-facing
	// setting in lights, reserving a pair of array layers per possible light.
	// Bound the resident cache and progressively lower its resolution only if
	// the driver cannot allocate the requested quality/slot count. This keeps a
	// high user slot setting from reserving gigabytes of duplicate shadow depth.
	constexpr uint32_t minLayers = MIN_LOCAL_SHADOW_SLOTS * 2;
	D3D11_TEXTURE2D_DESC cacheDesc{};
	cacheDesc.MipLevels = 1;
	cacheDesc.Format = cacheFormat;
	cacheDesc.SampleDesc.Count = 1;
	cacheDesc.Usage = D3D11_USAGE_DEFAULT;
	cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	ID3D11Texture2D* cacheTexture = nullptr;
	const uint32_t requestedCacheResolution = cacheResolution;
	uint32_t slots = 0;
	while (cacheResolution >= LOCAL_SHADOW_MIN_RESOLUTION && !cacheTexture) {
		cacheDesc.Width = cacheResolution;
		cacheDesc.Height = cacheResolution;
		const uint64_t bytesPerLayer = static_cast<uint64_t>(cacheResolution) * cacheResolution *
			(cacheFormat == DXGI_FORMAT_R16_UNORM ? 2ull : 4ull);
		const uint32_t budgetLayers = static_cast<uint32_t>(std::min<uint64_t>(
			LOCAL_SHADOW_CACHE_MAX_BYTES / std::max<uint64_t>(bytesPerLayer, 1ull), UINT32_MAX));
		slots = std::min(requestedSlots * 2u, budgetLayers) & ~1u;
		while (slots >= minLayers) {
			cacheDesc.ArraySize = slots;
			if (SUCCEEDED(globals::d3d::device->CreateTexture2D(&cacheDesc, nullptr, &cacheTexture)) && cacheTexture)
				break;
			cacheTexture = nullptr;
			if (slots == minLayers)
				break;
			slots = std::max(slots / 2u, minLayers);
		}
		if (cacheTexture || cacheResolution == LOCAL_SHADOW_MIN_RESOLUTION)
			break;
		cacheResolution = std::max(cacheResolution / 2u, LOCAL_SHADOW_MIN_RESOLUTION);
	}
	localShadowRequestedSlots = requestedSlots;
	localShadowRequestedResolution = requestedResolution;
	localShadowCacheResolution = cacheResolution;
	localShadowEngineResolution = engineResolution;
	localShadowCacheFormat = cacheFormat;
	if (!cacheTexture) {
		localShadowAllocationFailed = true;
		localShadowAllocFailedFrame = frame;
		logger::warn("[RadiantGrid] Local shadow cache allocation failed ({} slots at requested {}x{}); using current game shadow maps",
			requestedSlots, requestedCacheResolution, requestedCacheResolution);
		return;
	}
	localShadowDirectCopy = CanDirectCopyLocalShadow(engineDesc, cacheResolution, cacheFormat);
	if (!localShadowDirectCopy && !EnsureLocalShadowCopyShader()) {
		cacheTexture->Release();
		ReleaseLocalShadowResources();
		logger::error("[RadiantGrid] Local shadow cache disabled: downsample shader failed to compile");
		return;
	}
	localShadowAllocationFailed = false;
	if (cacheResolution != requestedCacheResolution || slots < requestedSlots * 2u)
		logger::warn("[RadiantGrid] Local shadow cache reduced to {} effective slots at {}x{} to meet the 512 MiB cache budget or driver limits (requested {} slots at {}x{})",
			std::min(requestedSlots, slots / 2u), cacheResolution, cacheResolution,
			requestedSlots, requestedCacheResolution, requestedCacheResolution);
	localShadowCache = eastl::make_unique<Texture2D>(cacheTexture, "RadiantGrid::LocalShadowCache");
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = cacheFormat;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = slots;
	localShadowCache->CreateSRV(srvDesc);
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = cacheFormat;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.ArraySize = slots;
	localShadowCache->CreateUAV(uavDesc);
	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(LocalShadowData);
	bufferDesc.ByteWidth = slots * sizeof(LocalShadowData);
	localShadowBuffer = eastl::make_unique<Buffer>(bufferDesc, nullptr, "RadiantGrid::LocalShadowData");
	D3D11_SHADER_RESOURCE_VIEW_DESC bufferSrv{};
	bufferSrv.Format = DXGI_FORMAT_UNKNOWN;
	bufferSrv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	bufferSrv.Buffer.NumElements = slots;
	localShadowBuffer->CreateSRV(bufferSrv);
	localShadowSliceOwner.assign(slots, nullptr);
	localShadowCacheSlots = slots;
	localShadowCacheLightCapacity = std::min(requestedSlots, slots / 2);
	logger::info("[RadiantGrid] Local shadow cache ready: {} requested / {} effective light slots ({} map layers) at {}x{}, engine {}x{} / {} slices, {} copy",
		requestedSlots, localShadowCacheLightCapacity, slots, cacheResolution, cacheResolution, engineResolution, engineDesc.Height, engineDesc.ArraySize,
		localShadowDirectCopy ? "direct" : "downsampled");
}

void RadiantGrid::BindLocalShadowResources()
{
	if (!localShadowCache || !localShadowBuffer || !globals::d3d::context)
		return;
	ID3D11ShaderResourceView* views[2] = { localShadowBuffer->srv.get(), localShadowCache->srv.get() };
	globals::d3d::context->PSSetShaderResources(112, ARRAYSIZE(views), views);
}

void RadiantGrid::EarlyPrepass()
{
	if (!IsLocalShadowsEnabled()) {
		if (localShadowCache)
			ReleaseLocalShadowResources();
		return;
	}
	CopyLocalShadowMaps();
	BindLocalShadowResources();
}

void RadiantGrid::CopyLocalShadowMaps()
{
	localShadowStatRendered = 0;
	localShadowStatCached = 0;
	auto smState = globals::game::smState;
	auto renderer = globals::game::renderer;
	if (!smState || !renderer || !globals::d3d::context)
		return;
	auto* shadowSceneNode = GetActiveShadowSceneNode();
	if (!shadowSceneNode)
		return;
	auto& depthStencil = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS];
	if (!depthStencil.texture || !depthStencil.depthSRV)
		return;
	EnsureLocalShadowResources(depthStencil.texture);
	if (!localShadowCache || !localShadowBuffer || (!localShadowDirectCopy && (!localShadowCopyCS || !localShadowCopyCB)))
		return;
	// Some supported runtimes populate activeShadowLights without entering the
	// CalculateActiveShadowCasterLights detour each render frame. Reconcile the
	// engine's actual active set here so the cache can still capture its maps.
	const uint32_t rendererFrame = globals::state ? globals::state->GetFrameCount() : 0u;
	if (localShadowLastSchedulerFrame != rendererFrame) {
		ScheduleLocalShadowCasters();
		localShadowSelecting = false;
		for (auto& entry : shadowSceneNode->GetRuntimeData().activeShadowLights)
			if (auto* caster = FindLocalShadowCaster(entry.get()))
				caster->lastSelectedFrame = localShadowFrame;
	}
	auto* context = globals::d3d::context;
	// The cache remains bound as a pixel-shader SRV between frames. Explicitly
	// unbind before copying or binding it as a compute UAV; D3D11 otherwise may
	// silently null one side of the resource hazard and leave stale cache data.
	ID3D11ShaderResourceView* nullShadowViews[2]{};
	context->PSSetShaderResources(112, ARRAYSIZE(nullShadowViews), nullShadowViews);
	const uint32_t frame = localShadowFrame;
	const auto& runtimeData = shadowSceneNode->GetRuntimeData();
	auto* sunLight = static_cast<RE::BSShadowLight*>(runtimeData.sunShadowDirLight);
	uint32_t selectedCasterCount = 0;
	for (const auto& caster : localShadowCasters)
		selectedCasterCount += caster.lastSelectedFrame == frame ? 1u : 0u;
	bool computeBound = false;
	struct PendingCopy
	{
		RE::BSShadowLight* light;
		LocalShadowCaster* caster;
		RE::BSGraphics::DepthStencilData* source;
		uint32_t sourceMipLevels;
		uint32_t sourceScale;
		LocalShadowRenderInfo info[2];
		uint32_t mapCount;
	};
	eastl::vector<PendingCopy> pending;
	uint32_t validDescriptorCount = 0;
	uint32_t missingDescriptorCount = 0;
	uint32_t disabledDescriptorCount = 0;
	uint32_t wrongTargetCount = 0;
	uint32_t invalidSliceCount = 0;
	uint32_t invalidTransformCount = 0;
	struct RejectedSourceDiagnostic
	{
		struct TargetSlot
		{
			const void* texture = nullptr;
			const void* depthSRV = nullptr;
			const void* sliceDSV = nullptr;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t arraySize = 0;
			uint32_t format = 0;
		};
		bool valid = false;
		int32_t target = -1;
		uint32_t slice = UINT32_MAX;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t arraySize = 0;
		uint32_t format = 0;
		uint32_t srvDimension = 0;
		uint32_t srvFirstSlice = 0;
		uint32_t srvSliceCount = 0;
		uint32_t dsvDimension = 0;
		uint32_t dsvFirstSlice = 0;
		uint32_t dsvSliceCount = 0;
		uint32_t candidateRejectMask = 0;
		uint32_t descriptorIndex = UINT32_MAX;
		uint32_t descriptorMapIndex = UINT32_MAX;
		bool descriptorEnabled = false;
		bool srvResourceMatches = false;
		bool dsvResourceMatches = false;
		uint32_t candidateMask = 0;
		uint32_t validCandidateMask = 0;
		uint32_t mappingCount = 0;
		int32_t resolvedTarget = -1;
		const void* light = nullptr;
		const void* descriptor = nullptr;
		const void* descriptorCamera = nullptr;
		const void* descriptorAccumulator = nullptr;
		const void* activeAccumulator = nullptr;
		const void* shadowSceneNode = nullptr;
		const void* texture = nullptr;
		const void* srvResource = nullptr;
		const void* dsvResource = nullptr;
		TargetSlot targetSlots[3]{};
		const char* reason = "unknown";
	} firstRejectedSource;
	for (auto& entry : runtimeData.activeShadowLights) {
		auto* light = entry.get();
		if (!light || light == sunLight || !IsShadowMapRendered(shadowSceneNode, light))
			continue;
		auto* caster = FindLocalShadowCaster(light);
		// The engine keeps descriptors and atlas slices for lights it did not
		// render this frame. Only copy a light selected by the prior frame's
		// UpdateCamera pass; otherwise stale maps would look freshly rendered and
		// defeat age-based refresh/eviction.
		if (!caster || caster->lastSelectedFrame != frame || caster->lastRenderedFrame == frame)
			continue;
		const uint32_t mapCount = light->GetIsParabolicLight() && light->shadowMapCount == 2 ? 2u : 1u;
		PendingCopy copy{ light, caster, nullptr, 1u, 0u, {}, mapCount };
		bool valid = true;
		for (uint32_t map = 0; map < mapCount; ++map) {
			auto& lightRuntime = light->GetRuntimeData();
			if (map >= lightRuntime.shadowmapDescriptors.size()) {
				++missingDescriptorCount;
				valid = false;
				break;
			}
			if (!lightRuntime.shadowmapDescriptors[map].isEnabled) {
				++disabledDescriptorCount;
				valid = false;
				break;
			}
			if (!ReadLocalShadowRenderInfo(light, map, copy.info[map])) {
				++invalidTransformCount;
				valid = false;
				break;
			}
			auto& depthStencils = renderer->GetDepthStencilData().depthStencils;
			auto& info = copy.info[map];
			const uint32_t engineSlice = info.engineSlice;
			const int32_t descriptorTarget = info.renderTarget;
			struct Candidate
			{
				int32_t target = -1;
				RE::BSGraphics::DepthStencilData* data = nullptr;
				D3D11_TEXTURE2D_DESC textureDesc{};
				REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
				REX::W32::D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
				const void* texture = nullptr;
				const void* srvResource = nullptr;
				const void* dsvResource = nullptr;
				const void* srv = nullptr;
				const void* dsv = nullptr;
				uint32_t rejectMask = 0;
				bool valid = false;
			};
			auto inspectTarget = [&](int32_t candidateTarget, Candidate& candidate) {
				candidate.target = candidateTarget;
				if (candidateTarget < 0 || candidateTarget >= RE::RENDER_TARGETS_DEPTHSTENCIL::kTOTAL) {
					candidate.rejectMask = 1u;
					return;
				}
				auto* data = &depthStencils[candidateTarget];
				candidate.data = data;
				candidate.texture = data->texture;
				candidate.srv = data->depthSRV;
				if (engineSlice < std::size(data->views))
					candidate.dsv = data->views[engineSlice];
				if (data->texture) {
					REX::W32::D3D11_TEXTURE2D_DESC textureDesc{};
					data->texture->GetDesc(&textureDesc);
					static_assert(sizeof(candidate.textureDesc) == sizeof(textureDesc));
					std::memcpy(&candidate.textureDesc, &textureDesc, sizeof(textureDesc));
				}
				if (!data->texture || !data->depthSRV || engineSlice >= std::size(data->views) || !data->views[engineSlice]) {
					candidate.rejectMask = (!data->texture ? 1u : 0u) | (!data->depthSRV ? 2u : 0u) |
						(engineSlice >= std::size(data->views) || !data->views[engineSlice] ? 4u : 0u);
					return;
				}
				data->depthSRV->GetDesc(&candidate.srvDesc);
				data->views[engineSlice]->GetDesc(&candidate.dsvDesc);
				REX::W32::ID3D11Resource* srvResource = nullptr;
				REX::W32::ID3D11Resource* dsvResource = nullptr;
				data->depthSRV->GetResource(&srvResource);
				data->views[engineSlice]->GetResource(&dsvResource);
				auto* textureResource = static_cast<REX::W32::ID3D11Resource*>(data->texture);
				candidate.srvResource = srvResource;
				candidate.dsvResource = dsvResource;
				const bool sameSrvResource = SameD3DResourceIdentity(srvResource, textureResource);
				const bool sameDsvResource = SameD3DResourceIdentity(dsvResource, textureResource);
				const bool srvArray = candidate.srvDesc.viewDimension == REX::W32::D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				const uint32_t srvFirst = srvArray ? candidate.srvDesc.texture2DArray.firstArraySlice : UINT32_MAX;
				const uint32_t srvCount = srvArray ? candidate.srvDesc.texture2DArray.arraySize : 0u;
				const bool dsvArray = candidate.dsvDesc.viewDimension == REX::W32::D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				const uint32_t dsvFirst = dsvArray ? candidate.dsvDesc.texture2DArray.firstArraySlice : UINT32_MAX;
				const uint32_t dsvCount = dsvArray ? candidate.dsvDesc.texture2DArray.arraySize : 0u;
				const bool srvContains = srvCount && engineSlice >= srvFirst && engineSlice - srvFirst < srvCount;
				const bool dsvSelectsSlice = dsvCount == 1u && dsvFirst == engineSlice;
				const bool validDimensions = candidate.textureDesc.Width && candidate.textureDesc.Width == candidate.textureDesc.Height &&
					candidate.textureDesc.Width >= localShadowCacheResolution &&
					candidate.textureDesc.Width % localShadowCacheResolution == 0;
				const bool validArray = candidate.textureDesc.ArraySize == ENGINE_SHADOW_MAP_SLICES && engineSlice < candidate.textureDesc.ArraySize;
				candidate.rejectMask = (!sameSrvResource ? 8u : 0u) | (!sameDsvResource ? 16u : 0u) |
					(!srvArray || !srvContains ? 32u : 0u) | (!dsvArray || !dsvSelectsSlice ? 64u : 0u) |
					(!validDimensions ? 128u : 0u) | (!validArray ? 256u : 0u);
				candidate.valid = candidate.rejectMask == 0;
				if (srvResource)
					srvResource->Release();
				if (dsvResource)
					dsvResource->Release();
			};

			constexpr std::array<int32_t, 3> localShadowTargets = {
				RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS,
				RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS_ESRAM,
				RE::RENDER_TARGETS_DEPTHSTENCIL::kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM
			};
			Candidate resolved{};
			uint32_t candidateMask = 0;
			uint32_t validCandidateMask = 0;
			uint32_t mappingCount = 0;
			bool ambiguousMapping = false;
			const bool descriptorTargetSupported = std::find(localShadowTargets.begin(), localShadowTargets.end(), descriptorTarget) != localShadowTargets.end();
			if (descriptorTargetSupported) {
				Candidate fromDescriptor{};
				inspectTarget(descriptorTarget, fromDescriptor);
				candidateMask |= 1u << static_cast<uint32_t>(std::distance(localShadowTargets.begin(), std::find(localShadowTargets.begin(), localShadowTargets.end(), descriptorTarget)));
				if (fromDescriptor.valid) {
					resolved = fromDescriptor;
					validCandidateMask = candidateMask;
					mappingCount = 1;
				}
			}
			if (!resolved.valid) {
				for (uint32_t candidateIndex = 0; candidateIndex < localShadowTargets.size(); ++candidateIndex) {
					const int32_t candidateTarget = localShadowTargets[candidateIndex];
					Candidate candidate{};
					inspectTarget(candidateTarget, candidate);
				if (candidate.texture)
						candidateMask |= 1u << candidateIndex;
				if (!firstRejectedSource.valid) {
					auto& snapshot = firstRejectedSource.targetSlots[candidateIndex];
					snapshot.texture = candidate.texture;
					snapshot.depthSRV = candidate.srv;
					snapshot.sliceDSV = candidate.dsv;
					snapshot.width = candidate.textureDesc.Width;
					snapshot.height = candidate.textureDesc.Height;
					snapshot.arraySize = candidate.textureDesc.ArraySize;
					snapshot.format = static_cast<uint32_t>(candidate.textureDesc.Format);
					if (!firstRejectedSource.texture) {
						firstRejectedSource.texture = candidate.texture;
						firstRejectedSource.srvResource = candidate.srvResource;
						firstRejectedSource.dsvResource = candidate.dsvResource;
						firstRejectedSource.width = candidate.textureDesc.Width;
						firstRejectedSource.height = candidate.textureDesc.Height;
						firstRejectedSource.arraySize = candidate.textureDesc.ArraySize;
						firstRejectedSource.format = static_cast<uint32_t>(candidate.textureDesc.Format);
						firstRejectedSource.srvDimension = candidate.srvDesc.viewDimension;
						firstRejectedSource.srvFirstSlice = candidate.srvDesc.texture2DArray.firstArraySlice;
						firstRejectedSource.srvSliceCount = candidate.srvDesc.texture2DArray.arraySize;
						firstRejectedSource.dsvDimension = candidate.dsvDesc.viewDimension;
						firstRejectedSource.dsvFirstSlice = candidate.dsvDesc.texture2DArray.firstArraySlice;
						firstRejectedSource.dsvSliceCount = candidate.dsvDesc.texture2DArray.arraySize;
						firstRejectedSource.srvResourceMatches = (candidate.rejectMask & 8u) == 0;
						firstRejectedSource.dsvResourceMatches = (candidate.rejectMask & 16u) == 0;
						firstRejectedSource.candidateRejectMask = candidate.rejectMask;
					}
				}
					if (!candidate.valid)
						continue;
					validCandidateMask |= 1u << candidateIndex;
					if (!resolved.valid) {
						resolved = candidate;
						mappingCount = 1;
					} else if (resolved.texture == candidate.texture) {
						// Multiple target slots may alias the same D3D texture. That is
						// one resource mapping; retain the descriptor target when valid,
						// otherwise prefer the canonical engine shadow-map slot.
						++mappingCount;
						if (candidateTarget == RE::RENDER_TARGETS_DEPTHSTENCIL::kSHADOWMAPS)
							resolved = candidate;
					} else {
						++mappingCount;
						ambiguousMapping = true;
					}
				}
			}
			if (!resolved.valid || ambiguousMapping) {
				++wrongTargetCount;
				if (!firstRejectedSource.valid) {
					const auto& descriptor = light->GetRuntimeData().shadowmapDescriptors[map];
					firstRejectedSource.valid = true;
					firstRejectedSource.target = descriptorTarget;
					firstRejectedSource.resolvedTarget = ambiguousMapping ? -2 : (resolved.valid ? resolved.target : -1);
					firstRejectedSource.slice = engineSlice;
					firstRejectedSource.candidateMask = candidateMask;
					firstRejectedSource.validCandidateMask = validCandidateMask;
					firstRejectedSource.mappingCount = mappingCount;
					firstRejectedSource.light = light;
					firstRejectedSource.descriptor = &descriptor;
					firstRejectedSource.descriptorIndex = map;
					firstRejectedSource.descriptorMapIndex = descriptor.shadowmapIndex;
					firstRejectedSource.descriptorEnabled = descriptor.isEnabled;
					firstRejectedSource.descriptorCamera = descriptor.camera.get();
					firstRejectedSource.descriptorAccumulator = descriptor.shaderAccumulator.get();
					if (auto accumulatorAddress = globals::game::currentAccumulator.get(); accumulatorAddress)
						firstRejectedSource.activeAccumulator = *accumulatorAddress;
					firstRejectedSource.shadowSceneNode = shadowSceneNode;
					for (uint32_t candidateIndex = 0; candidateIndex < localShadowTargets.size(); ++candidateIndex) {
						auto& snapshot = firstRejectedSource.targetSlots[candidateIndex];
						Candidate candidate{};
						inspectTarget(localShadowTargets[candidateIndex], candidate);
						snapshot.texture = candidate.texture;
						snapshot.depthSRV = candidate.srv;
						snapshot.sliceDSV = candidate.dsv;
						snapshot.width = candidate.textureDesc.Width;
						snapshot.height = candidate.textureDesc.Height;
						snapshot.arraySize = candidate.textureDesc.ArraySize;
						snapshot.format = static_cast<uint32_t>(candidate.textureDesc.Format);
					}
					firstRejectedSource.reason = ambiguousMapping ? "ambiguous-runtime-target-mapping" : "no-validated-runtime-target-mapping";
				}
				valid = false;
				break;
			}
			auto* source = resolved.data;
			const auto& sourceDesc = resolved.textureDesc;
			const uint32_t srvFirstSlice = resolved.srvDesc.texture2DArray.firstArraySlice;
			info.srvSlice = engineSlice - srvFirstSlice;
			info.renderTarget = resolved.target;
			const uint32_t sourceScale = sourceDesc.Width / localShadowCacheResolution;
			if (copy.source && (copy.source != source || copy.sourceScale != sourceScale)) {
				++wrongTargetCount;
				valid = false;
				break;
			}
			copy.source = source;
			copy.sourceMipLevels = std::max(sourceDesc.MipLevels, 1u);
			copy.sourceScale = sourceScale;
		}
		if (!valid)
			continue;
		++validDescriptorCount;
		pending.push_back(copy);
	}
	for (auto& entry : pending) {
		auto* light = entry.light;
		auto* caster = entry.caster;
		if (caster->slice < 0) {
			caster->slice = AcquireLocalShadowSlice(light, frame, entry.mapCount == 2);
			if (caster->slice < 0) {
				caster->slotDeniedFrame = frame;
				continue;
			}
		}
		// Skyrim packs different lights into a shared shadow atlas. Their native
		// descriptors can therefore legitimately name the same source array slice;
		// each caster is copied into its own cache layer, so source-slice uniqueness
		// is neither required nor a safe reason to discard a valid shadow.
		const uint32_t firstDestinationSlice = static_cast<uint32_t>(caster->slice);
		if (caster->slice < 0 || firstDestinationSlice >= localShadowCacheSlots ||
			entry.mapCount > localShadowCacheSlots - firstDestinationSlice ||
			(entry.mapCount == 2 && caster->secondarySlice >= 0 &&
				static_cast<uint32_t>(caster->secondarySlice) != firstDestinationSlice + 1u)) {
			caster->slotDeniedFrame = frame;
			continue;
		}
		caster->secondarySlice = entry.mapCount == 2 ? caster->slice + 1 : -1;
		bool copyComplete = true;
		for (uint32_t map = 0; map < entry.mapCount; ++map) {
			const uint32_t engineSlice = entry.info[map].engineSlice;
			const uint32_t targetSlice = static_cast<uint32_t>(caster->slice) + map;
			D3D11_TEXTURE2D_DESC sourceDesc{};
			REX::W32::D3D11_TEXTURE2D_DESC rexSourceDesc{};
			entry.source->texture->GetDesc(&rexSourceDesc);
			static_assert(sizeof(sourceDesc) == sizeof(rexSourceDesc));
			std::memcpy(&sourceDesc, &rexSourceDesc, sizeof(sourceDesc));
			const bool directCopy = CanDirectCopyLocalShadow(sourceDesc, localShadowCacheResolution, localShadowCacheFormat);
			if (directCopy) {
				context->CopySubresourceRegion(localShadowCache->resource.get(), D3D11CalcSubresource(0, targetSlice, 1),
					0, 0, 0, reinterpret_cast<ID3D11Resource*>(entry.source->texture), D3D11CalcSubresource(0, engineSlice, entry.sourceMipLevels), nullptr);
			} else {
				if (!localShadowCopyCS || !localShadowCopyCB) {
					copyComplete = false;
					break;
				}
				if (!computeBound) {
					ID3D11UnorderedAccessView* uav = localShadowCache->uav.get();
					ID3D11Buffer* buffer = localShadowCopyCB->CB();
					context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
					context->CSSetConstantBuffers(0, 1, &buffer);
					context->CSSetShader(localShadowCopyCS, nullptr, 0);
					computeBound = true;
				}
				ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(entry.source->depthSRV);
				context->CSSetShaderResources(0, 1, &srv);
				const uint32_t sourceScale = entry.sourceScale;
				const uint32_t groups = (localShadowCacheResolution + LOCAL_SHADOW_COPY_GROUP_SIZE - 1) / LOCAL_SHADOW_COPY_GROUP_SIZE;
				LocalShadowCopyCB copyData{ entry.info[map].srvSlice, targetSlice, sourceScale, localShadowCacheResolution };
				localShadowCopyCB->Update(copyData);
				context->Dispatch(groups, groups, 1);
			}
		}
		// Never publish a newly rendered shadow when its atlas copy failed. Doing
		// so made the light appear valid for several frames while sampling an old
		// or uninitialized cache slice, which presents as angle/distance popping.
		if (!copyComplete)
			continue;
		DirectX::XMStoreFloat4x4(&caster->shadowProj, DirectX::XMLoadFloat4x4(&entry.info[0].lightTransform));
		DirectX::XMStoreFloat4x4(&caster->shadowProj2,
			DirectX::XMLoadFloat4x4(&entry.info[entry.mapCount == 2 ? 1 : 0].lightTransform));
		uint32_t type = LOCAL_SHADOW_TYPE_SPOT;
		float spotFalloff = LOCAL_SHADOW_DEFAULT_SPOT_FALLOFF;
		if (light->GetIsParabolicLight())
			type = light->shadowMapCount == 2 ? LOCAL_SHADOW_TYPE_OMNI : LOCAL_SHADOW_TYPE_HEMISPHERE;
		else if (light->GetIsFrustumLight()) {
			const float falloff = static_cast<RE::BSShadowFrustumLight*>(light)->GetShadowFrustumLightRuntimeData().falloff;
			if (std::isfinite(falloff) && falloff > 0.0f)
				spotFalloff = falloff;
		}
		const float biasScale = static_cast<float>(entry.sourceScale);
		caster->shadowParams = { static_cast<float>(type), caster->radius, entry.info[0].biasScale * LOCAL_SHADOW_DEPTH_BIAS * biasScale, 1.0f };
		caster->shadowParams2 = { spotFalloff, 0.0f,
			static_cast<float>(caster->secondarySlice >= 0 ? caster->secondarySlice : caster->slice), 0.0f };
		if (caster->lastRenderedFrame != 0)
			caster->intervalEma += LOCAL_SHADOW_INTERVAL_EMA_WEIGHT *
				(std::min(static_cast<float>(frame - caster->lastRenderedFrame), LOCAL_SHADOW_INTERVAL_EMA_MAX) - caster->intervalEma);
		else
			caster->shadowReadyFrame = frame;
		caster->lastRenderedFrame = frame;
		caster->renderedPosition = caster->position;
		caster->renderedRotation = caster->rotation;
		caster->renderedRadius = caster->radius;
		caster->rejectStreak = 0;
		++localShadowStatRendered;
	}
	if (computeBound) {
		ID3D11ShaderResourceView* nullSrv = nullptr;
		ID3D11UnorderedAccessView* nullUav = nullptr;
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetShaderResources(0, 1, &nullSrv);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		context->CSSetConstantBuffers(0, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);
	}
	const auto& eye = globals::game::frameBufferCached.GetCameraPosAdjust();
	const float frameTime = globals::game::deltaTime ? std::clamp(*globals::game::deltaTime, 0.0f, LOCAL_SHADOW_MAX_FRAME_TIME) : 0.0f;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	DX::ThrowIfFailed(context->Map(localShadowBuffer->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	auto* upload = static_cast<LocalShadowData*>(mapped.pData);
	std::fill_n(upload, localShadowCacheSlots, LocalShadowData{});
	for (auto& caster : localShadowCasters) {
		if (caster.slice < 0 || caster.lastRenderedFrame == 0 || static_cast<uint32_t>(caster.slice) >= localShadowCacheSlots)
			continue;
		LocalShadowData data{};
		data.ShadowProj = caster.shadowProj;
		data.ShadowProj2 = caster.shadowProj2;
		for (int column = 0; column < 4; column++)
			data.ShadowProj.m[3][column] = static_cast<float>(static_cast<double>(eye.x) * caster.shadowProj.m[0][column] +
				static_cast<double>(eye.y) * caster.shadowProj.m[1][column] + static_cast<double>(eye.z) * caster.shadowProj.m[2][column] +
				static_cast<double>(caster.shadowProj.m[3][column]));
		for (int column = 0; column < 4; column++)
			data.ShadowProj2.m[3][column] = static_cast<float>(static_cast<double>(eye.x) * caster.shadowProj2.m[0][column] +
				static_cast<double>(eye.y) * caster.shadowProj2.m[1][column] + static_cast<double>(eye.z) * caster.shadowProj2.m[2][column] +
				static_cast<double>(caster.shadowProj2.m[3][column]));
		data.Params = caster.shadowParams;
		data.Params.w = std::clamp(static_cast<float>(frame - caster.shadowReadyFrame + 1) / LOCAL_SHADOW_FADE_IN_FRAMES, 0.0f, 1.0f);
		data.Params2 = caster.shadowParams2;
		if (caster.dynamic) {
			const float expectedInterval = std::max(caster.intervalEma, static_cast<float>(frame - caster.lastRenderedFrame) + 1.0f);
			data.Params2.y = std::min((expectedInterval - 1.0f) * (caster.actorSpeed + LOCAL_SHADOW_ANIMATION_SPEED * frameTime), LOCAL_SHADOW_MAX_SLACK);
		}
		data.Origin = { eye.x, eye.y, eye.z, 0.0f };
		upload[caster.slice] = data;
		++localShadowStatCached;
	}
	context->Unmap(localShadowBuffer->resource.get(), 0);
	if (frame - localShadowLastCopyDiagnosticFrame >= 300u) {
		localShadowLastCopyDiagnosticFrame = frame;
		uint32_t deniedCount = 0;
		for (const auto& caster : localShadowCasters)
			deniedCount += caster.slotDeniedFrame == frame ? 1u : 0u;
		logger::info("[RadiantGrid] Local shadow flow: active={} accumulated={} ownedLights={} activeShadowRefs={} missingNiLight={} tracked={} selected={} validMaps={} copied={} resident={} denied={} rejected(descMissing={}, disabled={}, slice={}, transform={}) descriptorTargetMismatch={} layers={} resolution={}.",
			runtimeData.activeShadowLights.size(), runtimeData.shadowLightsAccum.size(), localShadowStatOwnedLights,
			localShadowStatActiveShadowRefs, localShadowStatMissingNiLight, localShadowStatTracked,
			selectedCasterCount, validDescriptorCount, localShadowStatRendered, localShadowStatCached,
			deniedCount, missingDescriptorCount, disabledDescriptorCount, invalidSliceCount,
			invalidTransformCount, wrongTargetCount, localShadowCacheSlots, localShadowCacheResolution);
		for (const auto& caster : localShadowCasters) {
			if (caster.lastSeenFrame != frame || caster.lastRenderedFrame != 0)
				continue;
			logger::info("[RadiantGrid] Unmapped shadow light: light={:p} pos=({:.0f},{:.0f},{:.0f}) radius={:.0f} mask={} selected={} eligible={} retryIn={}.",
				static_cast<const void*>(caster.light), caster.position.x, caster.position.y, caster.position.z,
				caster.radius, GetShadowMaskIndex(caster.light), caster.lastSelectedFrame == frame,
				caster.lastEligibleFrame == frame,
				frame < caster.rejectUntilFrame ? caster.rejectUntilFrame - frame : 0u);
		}
		if (firstRejectedSource.valid)
			logger::warn("[RadiantGrid] First local shadow source rejected: stage=descriptor-target-resolution pass=shadow-map-copy frame={} light={:p} descriptor={:p} descriptorIndex={} enabled={} descriptorMapIndex={} rawTarget={} resolvedTarget={} slice={} camera={:p} descriptorAccumulator={:p} activeAccumulator={:p} shadowSceneNode={:p} candidates=0x{:X} validCandidates=0x{:X} mappingCount={} rejectMask=0x{:X} sourceTexture={:p} srvResource={:p} dsvResource={:p} resourceMatch(srv={}, dsv={}) texture={}x{}x{} format={} srv(dim={}, first={}, count={}) dsv(dim={}, first={}, count={}) slots[kShadow={} ({:p}/{:p}/{:p},{}x{}x{},fmt={}),kShadowESRAM={} ({:p}/{:p}/{:p},{}x{}x{},fmt={}),kVolShadowESRAM={} ({:p}/{:p}/{:p},{}x{}x{},fmt={})].",
				frame, firstRejectedSource.light, firstRejectedSource.descriptor, firstRejectedSource.descriptorIndex,
				firstRejectedSource.descriptorEnabled, firstRejectedSource.descriptorMapIndex,
				firstRejectedSource.target, firstRejectedSource.resolvedTarget, firstRejectedSource.slice,
				firstRejectedSource.descriptorCamera, firstRejectedSource.descriptorAccumulator,
				firstRejectedSource.activeAccumulator, firstRejectedSource.shadowSceneNode,
				firstRejectedSource.candidateMask, firstRejectedSource.validCandidateMask, firstRejectedSource.mappingCount,
				firstRejectedSource.candidateRejectMask,
				firstRejectedSource.texture, firstRejectedSource.srvResource, firstRejectedSource.dsvResource,
				firstRejectedSource.srvResourceMatches, firstRejectedSource.dsvResourceMatches,
				firstRejectedSource.width, firstRejectedSource.height, firstRejectedSource.arraySize,
				firstRejectedSource.format, firstRejectedSource.srvDimension, firstRejectedSource.srvFirstSlice,
				firstRejectedSource.srvSliceCount, firstRejectedSource.dsvDimension,
				firstRejectedSource.dsvFirstSlice, firstRejectedSource.dsvSliceCount,
				firstRejectedSource.targetSlots[0].texture != nullptr, firstRejectedSource.targetSlots[0].texture,
				firstRejectedSource.targetSlots[0].depthSRV, firstRejectedSource.targetSlots[0].sliceDSV,
				firstRejectedSource.targetSlots[0].width, firstRejectedSource.targetSlots[0].height,
				firstRejectedSource.targetSlots[0].arraySize, firstRejectedSource.targetSlots[0].format,
				firstRejectedSource.targetSlots[1].texture != nullptr, firstRejectedSource.targetSlots[1].texture,
				firstRejectedSource.targetSlots[1].depthSRV, firstRejectedSource.targetSlots[1].sliceDSV,
				firstRejectedSource.targetSlots[1].width, firstRejectedSource.targetSlots[1].height,
				firstRejectedSource.targetSlots[1].arraySize, firstRejectedSource.targetSlots[1].format,
				firstRejectedSource.targetSlots[2].texture != nullptr, firstRejectedSource.targetSlots[2].texture,
				firstRejectedSource.targetSlots[2].depthSRV, firstRejectedSource.targetSlots[2].sliceDSV,
				firstRejectedSource.targetSlots[2].width, firstRejectedSource.targetSlots[2].height,
				firstRejectedSource.targetSlots[2].arraySize, firstRejectedSource.targetSlots[2].format);
	}
}

RadiantGrid::LocalShadowCaster* RadiantGrid::FindLocalShadowCaster(RE::BSShadowLight* a_light)
{
	if (!a_light)
		return nullptr;
	if (auto it = localShadowCasterLookup.find(a_light); it != localShadowCasterLookup.end() && it->second < localShadowCasters.size())
		return &localShadowCasters[it->second];
	return nullptr;
}

uint32_t RadiantGrid::GetEngineLocalShadowBudget()
{
	auto* shadowSceneNode = GetActiveShadowSceneNode();
	if (!shadowSceneNode)
		return SHADOW_MASK_CHANNEL_COUNT - 1;
	return shadowSceneNode->GetRuntimeData().sunShadowDirLight ? SHADOW_MASK_CHANNEL_COUNT - 1 : SHADOW_MASK_CHANNEL_COUNT;
}

bool RadiantGrid::IsLocalShadowCasterInView(const LocalShadowCaster& a_caster, uint32_t a_frame)
{
	return a_caster.lastEligibleFrame != 0 && a_frame - a_caster.lastEligibleFrame <= LOCAL_SHADOW_CAMERA_HOLD_FRAMES;
}

bool RadiantGrid::IsLocalShadowCasterOnScreen(const LocalShadowCaster& a_caster, uint32_t a_frame)
{
	return a_frame - a_caster.lastOnScreenFrame <= LOCAL_SHADOW_SCREEN_HOLD_FRAMES;
}

float RadiantGrid::GetLocalShadowFadeIn(const LocalShadowCaster& a_caster, uint32_t a_frame)
{
	return std::min(static_cast<float>(a_frame - a_caster.shadowReadyFrame + 1) / LOCAL_SHADOW_FADE_IN_FRAMES, 1.0f);
}

bool RadiantGrid::IsLocalShadowSliceReclaimable(const LocalShadowCaster* a_owner, uint32_t a_frame)
{
	if (!a_owner || a_owner->lastRenderedFrame == 0 || !IsLocalShadowCasterOnScreen(*a_owner, a_frame))
		return true;
	if (a_frame - a_owner->lastRenderedFrame < LOCAL_SHADOW_EVICT_AGE)
		return false;
	return !IsLocalShadowCasterInView(*a_owner, a_frame) || a_owner->hidden || a_owner->radius <= 0.0f || a_frame < a_owner->rejectUntilFrame;
}

int32_t RadiantGrid::AcquireLocalShadowSlice(RE::BSShadowLight* a_light, uint32_t a_frame, bool a_dualMap)
{
	const std::size_t required = a_dualMap ? 2u : 1u;
	uint32_t residentLights = 0u;
	for (std::size_t slice = 0; slice < localShadowSliceOwner.size(); ++slice) {
		const auto* owner = localShadowSliceOwner[slice];
		if (!owner)
			continue;
		bool seenEarlier = false;
		for (std::size_t previous = 0; previous < slice; ++previous)
			seenEarlier |= localShadowSliceOwner[previous] == owner;
		residentLights += seenEarlier ? 0u : 1u;
	}
	const auto* acquirer = FindLocalShadowCaster(a_light);
	const bool atLightCapacity = acquirer && acquirer->slice < 0 && residentLights >= localShadowCacheLightCapacity;
	int32_t candidateSlice = -1;
	uint32_t oldestFrame = UINT32_MAX;
	for (std::size_t slice = 0; slice + required <= localShadowSliceOwner.size(); ++slice) {
		bool free = true;
		bool reclaimable = true;
		uint32_t oldest = UINT32_MAX;
		for (std::size_t offset = 0; offset < required; ++offset) {
			auto* owner = FindLocalShadowCaster(localShadowSliceOwner[slice + offset]);
			if (!owner) {
				free &= localShadowSliceOwner[slice + offset] == nullptr;
				continue;
			}
			free = false;
			reclaimable &= IsLocalShadowSliceReclaimable(owner, a_frame);
			oldest = std::min(oldest, owner->lastRenderedFrame);
		}
		if (free && !atLightCapacity) {
			candidateSlice = static_cast<int32_t>(slice);
			break;
		}
		if (reclaimable && oldest < oldestFrame) {
			oldestFrame = oldest;
			candidateSlice = static_cast<int32_t>(slice);
		}
	}
	if (candidateSlice < 0) {
		if (acquirer && acquirer->dynamic) {
			float victimImportance = FLT_MAX;
			bool victimInView = true;
			for (std::size_t slice = 0; slice + required <= localShadowSliceOwner.size(); ++slice) {
				auto* ownerCaster = FindLocalShadowCaster(localShadowSliceOwner[slice]);
				if (!ownerCaster || ownerCaster == acquirer || ownerCaster->dynamic || ownerCaster->lastRenderedFrame == a_frame ||
					(ownerCaster->secondarySlice >= 0 && ownerCaster->secondarySlice != static_cast<int32_t>(slice + 1)))
					continue;
				bool pairAvailable = true;
				if (required == 2) {
					auto* secondOwner = FindLocalShadowCaster(localShadowSliceOwner[slice + 1]);
					pairAvailable = !secondOwner || secondOwner == ownerCaster;
				}
				if (!pairAvailable)
					continue;
				const bool inView = IsLocalShadowCasterInView(*ownerCaster, a_frame);
				if (inView && ownerCaster->importance >= acquirer->importance)
					continue;
				if (candidateSlice < 0 || (victimInView && !inView) ||
					(inView == victimInView && ownerCaster->importance < victimImportance)) {
					candidateSlice = static_cast<int32_t>(slice);
					victimInView = inView;
					victimImportance = ownerCaster->importance;
				}
			}
		}
	}
	if (candidateSlice < 0)
		return -1;
	for (std::size_t offset = 0; offset < required; ++offset) {
		auto* evicted = FindLocalShadowCaster(localShadowSliceOwner[candidateSlice + offset]);
		if (!evicted)
			continue;
		for (auto& owner : localShadowSliceOwner)
			if (owner == evicted->light)
				owner = nullptr;
		evicted->slice = -1;
		evicted->secondarySlice = -1;
		evicted->lastRenderedFrame = 0;
	}
	localShadowSliceOwner[candidateSlice] = a_light;
	if (required == 2)
		localShadowSliceOwner[candidateSlice + 1] = a_light;
	return candidateSlice;
}

void RadiantGrid::ScheduleLocalShadowCasters()
{
	localShadowAllowed.clear();
	localShadowSelecting = false;
	localShadowLastSchedulerFrame = globals::state ? globals::state->GetFrameCount() : 0u;
	if (!IsLocalShadowCacheActive() || !globals::game::smState)
		return;
	auto* scene = GetActiveShadowSceneNode();
	if (!scene)
		return;
	const uint32_t frame = ++localShadowFrame;
	const auto cameraPosition = globals::game::frameBufferCached.GetCameraPosAdjust();
	localShadowCameraPosition = { cameraPosition.x, cameraPosition.y, cameraPosition.z };
	localShadowActors.clear();
	localShadowActorHistoryNext.clear();
	auto trackActor = [&](RE::Actor* actor) {
		if (!actor || !actor->Get3D())
			return;
		const auto id = actor->GetFormID();
		const auto position = actor->GetPosition();
		float speed = 0.0f;
		if (auto it = localShadowActorHistory.find(id); it != localShadowActorHistory.end())
			speed = std::min(it->second.GetDistance(position), LOCAL_SHADOW_ACTOR_MAX_SPEED);
		localShadowActorHistoryNext.insert_or_assign(id, position);
		if (!actor->IsDead() || speed >= LOCAL_SHADOW_ACTOR_REST_SPEED)
			localShadowActors.push_back({ position, speed });
	};
	trackActor(RE::PlayerCharacter::GetSingleton());
	if (auto* lists = RE::ProcessLists::GetSingleton())
		for (auto& handle : lists->highActorHandles)
			if (auto actor = handle.get())
				trackActor(actor.get());
	std::swap(localShadowActorHistory, localShadowActorHistoryNext);

	const auto* camera = RE::Main::WorldRootCamera();
	// Use Skyrim's owning active-light lists here. activeLights retains shadow
	// sources even when its camera-dependent activeShadowLights selection drops
	// them, while shadowLightsAccum is a transient raw-pointer scratch list that
	// can retain entries across cell/list churn. Schedule both owning lists; map
	// capture still requires membership in activeShadowLights for the current view.
	auto trackShadowLight = [&](RE::BSShadowLight* light) {
		if (!light)
			return;
		auto* niLight = light->light.get();
		if (!niLight) {
			++localShadowStatMissingNiLight;
			return;
		}
		auto* caster = FindLocalShadowCaster(light);
		if (caster && caster->lastSeenFrame == frame)
			return;
		if (!caster) {
			localShadowCasterLookup[light] = static_cast<uint32_t>(localShadowCasters.size());
			caster = &localShadowCasters.emplace_back();
			caster->light = light;
		}
		const float radius = niLight->GetLightRuntimeData().radius.x;
		const float teleport = std::max(LOCAL_SHADOW_TELEPORT_DISTANCE, radius * LOCAL_SHADOW_TELEPORT_RADIUS_FRACTION);
		if (caster->niLight != niLight || (caster->lastRenderedFrame != 0 && caster->renderedPosition.GetSquaredDistance(niLight->world.translate) > teleport * teleport)) {
			const int32_t slice = caster->slice;
			const int32_t secondarySlice = caster->secondarySlice;
			*caster = LocalShadowCaster{};
			caster->light = light;
			caster->slice = slice;
			caster->secondarySlice = secondarySlice;
		}
		caster->niLight = niLight;
		caster->lastSeenFrame = frame;
		caster->position = niLight->world.translate;
		caster->rotation = niLight->world.rotate;
		caster->radius = radius;
		caster->hidden = niLight->GetFlags().any(RE::NiAVObject::Flag::kHidden);
		if (IsSphereInCameraFrustum(camera, caster->position, caster->radius))
			caster->lastOnScreenFrame = frame;
		caster->dynamic = false;
		caster->actorImportance = 0.0f;
		caster->actorSpeed = 0.0f;
		const float radiusSq = std::max(radius * radius, 1.0f);
		for (const auto& actor : localShadowActors) {
			const float distanceSq = actor.position.GetSquaredDistance(caster->position);
			if (distanceSq > (radius + LOCAL_SHADOW_ACTOR_EXTENT) * (radius + LOCAL_SHADOW_ACTOR_EXTENT))
				continue;
			caster->dynamic = true;
			caster->actorSpeed = std::max(caster->actorSpeed, actor.speed);
			const float falloff = std::clamp(1.0f - distanceSq / radiusSq, 0.0f, 1.0f);
			const float proximity = LOCAL_SHADOW_ACTOR_PROXIMITY_DISTANCE /
				std::max(actor.position.GetDistance(localShadowCameraPosition), LOCAL_SHADOW_ACTOR_PROXIMITY_DISTANCE);
			caster->actorImportance = std::max(caster->actorImportance,
				std::lerp(LOCAL_SHADOW_ACTOR_EDGE_WEIGHT, 1.0f, falloff) * proximity);
		}
	};
	auto& sceneRuntime = scene->GetRuntimeData();
	localShadowStatOwnedLights = static_cast<uint32_t>(sceneRuntime.activeLights.size());
	localShadowStatActiveShadowRefs = static_cast<uint32_t>(sceneRuntime.activeShadowLights.size());
	localShadowStatMissingNiLight = 0;
	for (auto& shadowEntry : sceneRuntime.activeShadowLights)
		trackShadowLight(shadowEntry.get());
	for (auto& lightEntry : sceneRuntime.activeLights) {
		auto* baseLight = lightEntry.get();
		if (!baseLight || !baseLight->IsShadowLight())
			continue;
		if (auto* shadowLight = skyrim_cast<RE::BSShadowLight*>(baseLight))
			trackShadowLight(shadowLight);
	}
	for (std::size_t i = 0; i < localShadowCasters.size();) {
		auto& caster = localShadowCasters[i];
		if (caster.lastSeenFrame == frame) {
			++i;
			continue;
		}
		// Skyrim's active shadow-light list is camera-dependent. Keep the cached
		// map and its slice owner through brief visibility/list changes so a camera
		// turn does not destroy a valid shadow and force a cold recapture on return.
		// During this grace period the pointer is only used as an identity token;
		// no engine object is dereferenced unless it appears in a live engine list.
		if (frame - caster.lastSeenFrame <= LOCAL_SHADOW_CAMERA_HOLD_FRAMES) {
			++i;
			continue;
		}
		for (auto& owner : localShadowSliceOwner)
			if (owner == caster.light)
				owner = nullptr;
		localShadowCasterLookup.erase(caster.light);
		if (i + 1 < localShadowCasters.size()) {
			caster = std::move(localShadowCasters.back());
			localShadowCasterLookup[caster.light] = static_cast<uint32_t>(i);
		}
		localShadowCasters.pop_back();
	}
	localShadowStatTracked = static_cast<uint32_t>(localShadowCasters.size());
	struct Candidate { LocalShadowCaster* caster; float score; };
	eastl::vector<Candidate> candidates;
	candidates.reserve(localShadowCasters.size());
	for (auto& caster : localShadowCasters) {
		if (caster.hidden || !std::isfinite(caster.radius) || caster.radius <= 0.0f ||
			!std::isfinite(caster.position.x) || !std::isfinite(caster.position.y) || !std::isfinite(caster.position.z))
			continue;
		// A promoted light can pass UpdateCamera yet lose Skyrim's small native
		// shadow-map budget. Do not let that unrendered request hold a selection
		// slot forever; retry after other candidates have had a chance to map.
		if (caster.lastRenderedFrame == 0 && caster.selectionStartFrame != 0 &&
			frame - caster.selectionStartFrame >= LOCAL_SHADOW_FIRST_MAP_RETRY_FRAMES) {
			caster.rejectUntilFrame = frame + LOCAL_SHADOW_FIRST_MAP_RETRY_FRAMES;
			caster.selectionStartFrame = 0;
		}
		if (caster.lastRenderedFrame == 0 && frame < caster.rejectUntilFrame)
			continue;
		// Skyrim's shadow-light list and UpdateCamera result are view dependent.
		// Use the light's influence sphere against the current player frustum so a
		// source behind the camera can remain active while its radius reaches visible
		// receivers. The grace check covers short camera/cell-list gaps only.
		const float viewRadius = caster.radius + std::max(LOCAL_SHADOW_VIEW_CULL_MARGIN, caster.radius * 0.1f);
		const bool influenceInView = IsSphereInCameraFrustum(camera, caster.position, viewRadius);
		const bool onScreen = influenceInView || IsLocalShadowCasterOnScreen(caster, frame);
		if (!influenceInView && !IsLocalShadowCasterInView(caster, frame))
			continue;
		// Keep resident maps eligible while the light source is outside the view.
		// The source can still illuminate an on-screen receiver; tying its cached
		// shadow to camera visibility made shadows pop as the player turned.
		if (!onScreen && caster.lastRenderedFrame == 0 && localShadowSliceOwner.size() != 0)
			continue;
		float score = caster.radius / std::max(caster.position.GetDistance(localShadowCameraPosition), caster.radius);
		if (caster.lastRenderedFrame == 0) {
			if (onScreen) {
				// Rotate first-time casters through Skyrim's small native render
				// budget. Without wait age, the first lights in activeShadowLights
				// remained tied as "new" forever and candles later in the list never
				// received a first map.
				const uint32_t waitFrames = caster.lastSelectedFrame == 0
					? frame
					: frame - caster.lastSelectedFrame;
				score += LOCAL_SHADOW_NEWCOMER_SCORE +
					static_cast<float>(std::min(waitFrames, LOCAL_SHADOW_STATIC_STARVE_FRAMES)) * LOCAL_SHADOW_NEWCOMER_WAIT_SCORE;
			}
		}
		else {
			const float moveThreshold = std::max(LOCAL_SHADOW_MOVE_THRESHOLD, caster.radius * LOCAL_SHADOW_MOVE_RADIUS_FRACTION);
			float rotationDelta = 0.0f;
			for (uint32_t row = 0; row < 3; ++row)
				for (uint32_t column = 0; column < 3; ++column)
					rotationDelta = std::max(rotationDelta, std::abs(caster.rotation.entry[row][column] - caster.renderedRotation.entry[row][column]));
			const float moved = caster.position.GetDistance(caster.renderedPosition);
			const float radiusDelta = std::abs(caster.radius - caster.renderedRadius);
			if (moved > moveThreshold || caster.radius * rotationDelta > moveThreshold ||
				radiusDelta > std::max(2.0f, caster.radius * LOCAL_SHADOW_MOVE_RADIUS_FRACTION))
				score += LOCAL_SHADOW_MOVED_SCORE;
			else if (caster.dynamic)
				score += LOCAL_SHADOW_ACTOR_SCORE * caster.actorImportance;
			else {
				const float age = static_cast<float>(frame - caster.lastRenderedFrame);
				score += LOCAL_SHADOW_AGE_URGENCY * age * (LOCAL_SHADOW_STATIC_IMPORTANCE_BASE + score) /
					(LOCAL_SHADOW_ACTOR_SCORE + LOCAL_SHADOW_AGE_URGENCY * age * (LOCAL_SHADOW_STATIC_IMPORTANCE_BASE + score));
			}
		}
		if (frame < caster.rejectUntilFrame)
			score *= 0.5f;
		caster.importance = score;
		candidates.push_back({ &caster, score });
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
		if (a.score != b.score)
			return a.score > b.score;
		// Equal-priority lights that have never received a slot (or waited longer)
		// go first. This keeps later candles from losing every tie to the first few
		// entries in Skyrim's accumulator list.
		if (a.caster->lastSelectedFrame != b.caster->lastSelectedFrame)
			return a.caster->lastSelectedFrame < b.caster->lastSelectedFrame;
		// Stable spatial tie breaks keep entry/list order from deciding whether
		// a fireplace or candle receives the first cache map after cell entry.
		if (a.caster->radius != b.caster->radius)
			return a.caster->radius > b.caster->radius;
		if (a.caster->position.x != b.caster->position.x)
			return a.caster->position.x < b.caster->position.x;
		if (a.caster->position.y != b.caster->position.y)
			return a.caster->position.y < b.caster->position.y;
		return a.caster->position.z < b.caster->position.z;
	});
	localShadowEngineBudget = std::min<uint32_t>(GetEngineLocalShadowBudget(), SHADOW_MASK_CHANNEL_COUNT);
	uint32_t allowedCapacity = localShadowEngineBudget;
	// Keep casters selected in the recent frames while their maps remain valid.
	// This hysteresis prevents small camera/player movements from changing the
	// selected set every frame. After the hold expires, score ranking lets newly
	// relevant lights take a slot and prevents distant candles from starving.
	eastl::vector<LocalShadowCaster*> selected;
	selected.reserve(allowedCapacity);
	for (const auto& candidate : candidates) {
		auto* caster = candidate.caster;
		const bool awaitingFirstMap = caster->lastRenderedFrame == 0u;
		const bool recentlySelected = caster->lastSelectedFrame + 2u >= frame;
		const bool holdActive = caster->selectionStartFrame != 0u &&
			frame - caster->selectionStartFrame < LOCAL_SHADOW_SELECTION_HOLD_FRAMES;
		// Do not rotate an unrendered light out of the native list just because
		// its first selected frame did not produce a map. Camera-dependent engine
		// eligibility can defer that render; retain the request until it succeeds.
		if ((!recentlySelected || !holdActive) && !awaitingFirstMap)
			continue;
		selected.push_back(caster);
		if (selected.size() >= allowedCapacity)
			break;
	}
	for (const auto& candidate : candidates) {
		if (selected.size() >= allowedCapacity)
			break;
		if (std::find(selected.begin(), selected.end(), candidate.caster) == selected.end())
			selected.push_back(candidate.caster);
	}
	if (allowedCapacity > 1 && candidates.size() > allowedCapacity && !selected.empty()) {
		const bool staticAlreadySelected = std::any_of(selected.begin(), selected.end(), [](const LocalShadowCaster* caster) {
			return !caster->dynamic && caster->importance < LOCAL_SHADOW_ACTOR_SCORE;
		});
		if (!staticAlreadySelected) {
			auto victim = candidates.end();
			auto replace = selected.end();
			uint32_t greatestAge = LOCAL_SHADOW_STATIC_STARVE_FRAMES - 1;
			for (auto it = candidates.begin(); it != candidates.end(); ++it) {
				if (it->caster->dynamic || it->caster->lastRenderedFrame == 0 ||
					std::find(selected.begin(), selected.end(), it->caster) != selected.end())
					continue;
				const uint32_t age = frame - it->caster->lastRenderedFrame;
				if (age > greatestAge) {
					greatestAge = age;
					victim = it;
				}
			}
			for (auto it = selected.end(); it != selected.begin();) {
				--it;
				auto* caster = *it;
				const bool selectionHoldExpired = caster->selectionStartFrame == 0u ||
					frame - caster->selectionStartFrame >= LOCAL_SHADOW_SELECTION_HOLD_FRAMES;
				if (!caster->dynamic && caster->lastRenderedFrame != 0u && selectionHoldExpired) {
					replace = it;
					break;
				}
			}
			if (victim != candidates.end() && replace != selected.end())
				*replace = victim->caster;
		}
	}
	for (auto* caster : selected) {
		if (caster->selectionStartFrame == 0u ||
			caster->lastSelectedFrame + 2u < frame)
			caster->selectionStartFrame = frame;
		caster->lastSelectedFrame = frame;
		localShadowAllowed.push_back(caster->light);
	}
	localShadowSelecting = true;
}

bool RadiantGrid::FilterLocalShadowCaster(RE::BSShadowLight* a_light, const RE::NiCamera* a_camera, bool a_result)
{
	if (!localShadowSelecting || !a_light)
		return a_result;
	if (a_camera)
		localShadowCameraPosition = a_camera->world.translate;
	auto* caster = FindLocalShadowCaster(a_light);
	if (!caster)
		return a_result;
	caster->lastEvaluatedFrame = localShadowFrame;
	const float viewRadius = caster->radius + std::max(LOCAL_SHADOW_VIEW_CULL_MARGIN, caster->radius * 0.1f);
	const bool influenceInView = a_camera && std::isfinite(caster->radius) && caster->radius > 0.0f &&
		IsSphereInCameraFrustum(a_camera, caster->position, viewRadius);
	if (a_result || influenceInView)
		caster->lastEligibleFrame = localShadowFrame;
	const bool selected = std::find(localShadowAllowed.begin(), localShadowAllowed.end(), a_light) != localShadowAllowed.end();
	// The engine's camera test can reject a promoted light because its shadow
	// caster is behind the player even though the light volume still intersects
	// the view. Keep that selected light in the active shadow-light set in this
	// bounded case; the normal engine update still owns its shadow camera setup.
	// Never remove a light Skyrim already accepted. The local cache is a sampled
	// copy of native shadow maps, so an unselected cache candidate must retain its
	// native shadow path instead of disappearing when the cache is enabled.
	if (a_result) {
		if (selected)
			caster->lastSelectedFrame = localShadowFrame;
		return true;
	}
	if (!(selected && influenceInView))
		return false;
	if (selected)
		caster->lastSelectedFrame = localShadowFrame;
	return true;
}

void RadiantGrid::UpdateLights()
{
	auto smState = globals::game::smState;
	auto& isl = globals::pipeline::naturalLighting;
	if (!smState || !globals::d3d::context || !lights || !lights->resource) {
		lightCount = 0;
		particleLightBufferStart = 0;
		particleLightBufferCount = 0;
		return;
	}

	auto* accumulatorAddress = globals::game::currentAccumulator.get();
	auto* activeSceneNode = accumulatorAddress && *accumulatorAddress ?
		GetActiveShadowSceneNode((*accumulatorAddress)->GetRuntimeData().activeShadowSceneNode) : GetActiveShadowSceneNode();
	auto shadowSceneNode = activeSceneNode;
	if (!shadowSceneNode) {
		lightCount = 0;
		particleLightBufferStart = 0;
		particleLightBufferCount = 0;
		return;
	}

	// Cache camera position from the FrameBuffer snapshot; shadowState::posAdjust can be stale in first-person

	{
		auto eyePosition = globals::game::frameBufferCached.GetCameraPosAdjust();
		eyePositionCached = { eyePosition.x, eyePosition.y, eyePosition.z };
		strictLightDataTemp.FirstPerson = localShadowFirstPersonPass ? 1u : 0u;
		strictLightDataTemp.WorldEyePosition = { eyePosition.x, eyePosition.y, eyePosition.z, 0.0f };
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);
    std::array<std::uint64_t, MAX_LIGHTS> emitterIdentities{};

	// Process point lights

	roomNodes.clear();
	std::array<std::uint64_t, MAX_LIGHTS> shadowRevisions{};

	auto addRoom = [&](RE::NiNode* node, LightData& light) {
		if (!node)
			return;

		uint8_t roomIndex = 0;
		if (auto it = roomNodes.find(node); it == roomNodes.cend()) {
			// LightData stores a 128-bit room mask. Never let a wrapped index
			// write beyond it in unusually complex or heavily modded interiors.
			if (roomNodes.size() >= 128u)
				return;
			roomIndex = static_cast<uint8_t>(roomNodes.size());
			roomNodes.insert_or_assign(node, roomIndex);
		} else {
			roomIndex = it->second;
		}
		light.roomFlags.SetBit(roomIndex, 1);
	};

	auto addLight = [&](const RE::NiPointer<RE::BSLight>& e) {
		if (auto bsLight = e.get()) {
			// The engine can expose one light in both owning lists. Preserve one
			// clustered entry so it cannot consume two lighting/contact slots.
			const auto identity = reinterpret_cast<std::uintptr_t>(bsLight);
			const auto knownEnd = emitterIdentities.cbegin() + std::min<std::size_t>(lightsData.size(), MAX_LIGHTS);
			if (std::find(emitterIdentities.cbegin(), knownEnd, identity) != knownEnd)
				return;
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
					auto& runtimeData = niLight->GetLightRuntimeData();

					LightData light{};
					std::uint64_t shadowRevision = 0;
					light.color = { runtimeData.diffuse.red, runtimeData.diffuse.green, runtimeData.diffuse.blue };
					light.lightFlags = std::bit_cast<LightFlags>(runtimeData.ambient.red);

					if (isl.loaded) {
						isl.ProcessLight(light, bsLight, niLight);
					} else {
						light.radius = runtimeData.radius.x;
						// light.color *= runtimeData.fade;
						light.fade = runtimeData.fade;
					}

					light.fade *= bsLight->lodDimmer;
					if (light.lightFlags.any(LightFlags::Disabled) ||
						!std::isfinite(light.fade) || !std::isfinite(light.radius) ||
						!std::isfinite(light.color.x) || !std::isfinite(light.color.y) || !std::isfinite(light.color.z))
						return;

					if (!IsGlobalLight(bsLight)) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							addRoom(roomPtr, light);
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							if (portalPtr)
								addRoom(portalPtr->portalSharedNode.get(), light);
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						TryAssignShadowMask(light, shadowLight, shadowSceneNode);
						light.lightFlags.set(LightFlags::ShadowCaster);
						if (localShadowCache && localShadowBuffer) {
							if (auto* caster = FindLocalShadowCaster(shadowLight); caster && caster->slice >= 0 &&
								(caster->shadowParams.x != LOCAL_SHADOW_TYPE_OMNI || caster->secondarySlice == caster->slice + 1) &&
								caster->lastRenderedFrame != 0 && caster->lastSeenFrame == localShadowFrame) {
								light.localShadowIndex = static_cast<uint32_t>(caster->slice);
								light.lightFlags.set(LightFlags::LocalShadow);
					// Preserve the native map index for bookkeeping; cached casters sample
					// their own depth, and lights without residency use the native mask.
								shadowRevision = caster->lastRenderedFrame;
							}
						}
					}

					// A missing/stale engine shadow map disables shadow sampling for this
					// frame, not the light itself. Keep the real source contributing light.
					SetLightPosition(light, niLight->world.translate);
					if (!std::isfinite(light.positionWS.data.x) ||
						!std::isfinite(light.positionWS.data.y) ||
						!std::isfinite(light.positionWS.data.z))
						return;

					if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
						if (lightsData.size() < MAX_LIGHTS) {
                            emitterIdentities[lightsData.size()] = reinterpret_cast<std::uintptr_t>(bsLight);
							shadowRevisions[lightsData.size()] = shadowRevision;
						}
						lightsData.push_back(light);
					}
				}
			}
		}
	};

	for (auto& e : shadowSceneNode->GetRuntimeData().activeLights) {
		addLight(e);
	}
	for (auto& e : shadowSceneNode->GetRuntimeData().activeShadowLights) {
		addLight(e);
	}
	uint32_t localShadowLightCount = 0;
	uint32_t nativeShadowLightCount = 0;
	for (const auto& light : lightsData) {
		localShadowLightCount += light.lightFlags.any(LightFlags::LocalShadow) ? 1u : 0u;
		nativeShadowLightCount += light.lightFlags.any(LightFlags::Shadow) ? 1u : 0u;
	}
	const uint32_t diagnosticFrame = globals::state ? globals::state->frameCount : 0u;
	if (diagnosticFrame - localShadowLastLightDiagnosticFrame >= 300u) {
		localShadowLastLightDiagnosticFrame = diagnosticFrame;
		const char* cacheState = !settings.EnableLocalShadows ? "disabled-by-setting" :
			!localShadowHooksInstalled ? "hook-unavailable" :
			(localShadowCache && localShadowBuffer) ? "ready" :
			localShadowAllocationFailed ? "allocation-failed" : "waiting-for-engine-shadow-maps";
		uint32_t shadowCasters = 0;
		for (const auto& light : lightsData)
			shadowCasters += light.lightFlags.any(LightFlags::ShadowCaster) ? 1u : 0u;
		logger::info("[RadiantGrid] Local light shadows: enabled={} cache={} lights={} shadowCasters={} cached={} native={}.",
			settings.EnableLocalShadows, cacheState, lightsData.size(), shadowCasters,
			localShadowLightCount, nativeShadowLightCount);
	}

	particleLightBufferStart = static_cast<std::uint32_t>(std::min<std::size_t>(lightsData.size(), MAX_LIGHTS));
	AddParticleLightsToBuffer(lightsData, emitterIdentities, shadowRevisions);
	particleLightBufferCount = static_cast<std::uint32_t>(
		std::min<std::size_t>(lightsData.size(), MAX_LIGHTS) - particleLightBufferStart);

	auto context = globals::d3d::context;

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	const size_t bytes = sizeof(LightData) * lightCount;
	if (bytes > 0)
		memcpy_s(mapped.pData, sizeof(LightData) * MAX_LIGHTS, lightsData.data(), bytes);
	context->Unmap(lights->resource.get(), 0);
    if (globals::pipeline::hybridGI.settings.EnableCacheIntelligence) {
        std::array<PIXL::Renderer::EmitterObservation, MAX_LIGHTS> observations{};
        for (uint i = 0; i < lightCount; ++i) {
            const auto& light = lightsData[i];
            observations[i] = { emitterIdentities[i],
                {light.positionWS.data.x + eyePositionCached.x, light.positionWS.data.y + eyePositionCached.y,
                 light.positionWS.data.z + eyePositionCached.z, light.radius},
                {light.color.x * light.fade, light.color.y * light.fade, light.color.z * light.fade},
                shadowRevisions[i] };
        }
        PIXL::Renderer::LightTransportWorld::Get().PublishEmitterObservations({observations.data(), lightCount});
    }

	UpdateStructure();
}

void RadiantGrid::UpdateStructure()
{
	auto context = globals::d3d::context;
	if (!context || !clusterBuildingCS || !clusterCullingCS || !lightBuildingCB || !lightCullingCB ||
		!clusters || !lightIndexCounter || !lightIndexList || !lightGrid || !lights ||
		!globals::game::cameraNear || !globals::game::cameraFar)
		return;

	lightsNear = *globals::game::cameraNear;
	lightsFar = *globals::game::cameraFar;

	auto renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
	clusterSize[0] = ((uint)renderSize.x + 63) / 64;
	clusterSize[1] = ((uint)renderSize.y + 63) / 64;
	clusterSize[2] = 32;

	// Own our camera dependency rather than inheriting CS state from another
	// module. Both construction and culling must use the current world camera.
	winrt::com_ptr<ID3D11Buffer> previousFrameBuffer;
	context->CSGetConstantBuffers(12, 1, previousFrameBuffer.put());
	ID3D11Buffer* frameBuffer = *globals::game::perFrame;
	context->CSSetConstantBuffers(12, 1, &frameBuffer);

	{
		LightBuildingCB updateData{};
		updateData.LightsNear = lightsNear;
		updateData.LightsFar = lightsFar;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightBuildingCB->Update(updateData);

		ID3D11Buffer* buffer = lightBuildingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11UnorderedAccessView* clusters_uav = clusters->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &clusters_uav, nullptr);

		context->CSSetShader(clusterBuildingCS, nullptr, 0);
		globals::profiler->BeginPass("RadiantGrid::ClusterBuild");
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
		globals::profiler->EndPass();

		ID3D11UnorderedAccessView* null_uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
	}

	{
		LightCullingCB updateData{};
		updateData.LightCount = lightCount;
		std::copy(clusterSize, clusterSize + 3, updateData.ClusterSize);

		lightCullingCB->Update(updateData);

		UINT counterReset[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(lightIndexCounter->uav.get(), counterReset);

		ID3D11Buffer* buffer = lightCullingCB->CB();
		context->CSSetConstantBuffers(0, 1, &buffer);

		ID3D11ShaderResourceView* srvs[] = { clusters->srv.get(), lights->srv.get() };
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { lightIndexCounter->uav.get(), lightIndexList->uav.get(), lightGrid->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		globals::profiler->BeginPass("RadiantGrid::ClusterCull");
		context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		globals::profiler->EndPass();
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11ShaderResourceView* null_srvs[2] = { nullptr };
	context->CSSetShaderResources(0, 2, null_srvs);

	ID3D11UnorderedAccessView* null_uavs[3] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 3, null_uavs, nullptr);
	frameBuffer = previousFrameBuffer.get();
	context->CSSetConstantBuffers(12, 1, &frameBuffer);
}

void RadiantGrid::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& singleton = globals::pipeline::radiantGrid;
	// HybridGI copies supported rigid opaque mesh triangles while the Skyrim draw
	// pass is valid. Its capture stores owned coordinates only and has a strict cap.
	globals::pipeline::hybridGI.CaptureRenderGeometry(Pass);
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	func(This, Pass, RenderFlags);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
}

void RadiantGrid::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	ExternalEmittance::UpdatePermutation(Pass);
	auto& singleton = globals::pipeline::radiantGrid;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

void RadiantGrid::Hooks::BSWaterShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	func(This, Pass, RenderFlags);
	auto& singleton = globals::pipeline::radiantGrid;
	singleton.BSLightingShader_SetupGeometry_Before(Pass);
	singleton.BSLightingShader_SetupGeometry_After(Pass);
};

namespace
{
	struct VertexColor
	{
		std::uint8_t data[4];
	};

	bool TryGetAlphaWeightedVertexColor(const std::uint8_t* a_rawVertexData, std::uint32_t a_vertexSize, std::uint32_t a_colorOffset, std::uint32_t a_vertexCount, VertexColor& a_outVertexColor)
	{
		if (!a_rawVertexData || a_vertexSize < sizeof(VertexColor) || a_vertexCount == 0)
			return false;
		if (a_colorOffset > (a_vertexSize - sizeof(VertexColor)))
			return false;

		float weightedR = 0.f, weightedG = 0.f, weightedB = 0.f;
		float totalAlpha = 0.f;
		std::uint8_t maxAlpha = 0;

		for (std::uint32_t v = 0; v < a_vertexCount; ++v) {
			const auto byteOffset = static_cast<std::size_t>(a_vertexSize) * v + a_colorOffset;
			const auto* vertex = reinterpret_cast<const VertexColor*>(a_rawVertexData + byteOffset);
			float alpha = vertex->data[3];
			weightedR += vertex->data[0] * alpha;
			weightedG += vertex->data[1] * alpha;
			weightedB += vertex->data[2] * alpha;
			totalAlpha += alpha;
			if (vertex->data[3] > maxAlpha)
				maxAlpha = vertex->data[3];
		}

		if (totalAlpha == 0.f)
			return false;

		a_outVertexColor.data[0] = static_cast<std::uint8_t>(std::min(weightedR / totalAlpha, 255.f));
		a_outVertexColor.data[1] = static_cast<std::uint8_t>(std::min(weightedG / totalAlpha, 255.f));
		a_outVertexColor.data[2] = static_cast<std::uint8_t>(std::min(weightedB / totalAlpha, 255.f));
		a_outVertexColor.data[3] = maxAlpha;
		return true;
	}

	RE::NiColorA BuildEffectMaterialEmissiveTint(RE::BSEffectShaderMaterial* a_material, RE::BSEffectShaderProperty* a_shaderProperty)
	{
		RE::NiColorA tint{
			a_material->baseColor.red * a_material->baseColorScale,
			a_material->baseColor.green * a_material->baseColorScale,
			a_material->baseColor.blue * a_material->baseColorScale,
			1.0f
		};
		if (auto emittance = a_shaderProperty->emittanceColor) {
			tint.red *= emittance->red;
			tint.green *= emittance->green;
			tint.blue *= emittance->blue;
		}
		return tint;
	}

	std::optional<std::string> GetLowercaseStem(const char* a_path)
	{
		std::filesystem::path p(a_path);
		auto stem = p.stem().string();
		if (stem.empty())
			return std::nullopt;
		std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return stem;
	}

	struct IncandescentEmitterProfile
	{
		float particleRadiusScale;
		float particleMinimumRadius;
		float particleIntensityScale;
		float geometryRadiusScale;
		float geometryMinimumRadius;
		float geometryIntensityScale;
		RE::NiColor geometryTint;
		float flickerStrength;
	};

	std::optional<IncandescentEmitterProfile> GetIncandescentEmitterProfile(std::string_view a_evidence)
	{
		// This is deliberately restricted to practical incandescent emitters.
		// Particle candidates retain the existing billboard/depth checks. Ordinary
		// geometry additionally requires a real GlowMap material before reaching
		// this name evidence, so a diffuse filename alone cannot create a light.
		auto contains = [&](std::string_view token) {
			return a_evidence.find(token) != std::string_view::npos;
		};

		if (contains("candle") || contains("wick"))
			return IncandescentEmitterProfile{
				8.0f, 272.0f, 0.87f,
				4.0f, 350.0f, 1.49f,
				{ 1.0f, 0.52f, 0.18f }, 0.055f
			};
		if (contains("torch") || contains("sconce"))
			return IncandescentEmitterProfile{
				10.0f, 408.0f, 1.00f,
				4.5f, 505.0f, 1.92f,
				{ 1.0f, 0.43f, 0.12f }, 0.085f
			};
		if (contains("brazier") || contains("bonfire") || contains("campfire") || contains("hearth"))
			return IncandescentEmitterProfile{
				12.0f, 600.0f, 1.10f,
				5.0f, 870.0f, 2.65f,
				{ 1.0f, 0.36f, 0.08f }, 0.105f
			};
		if (contains("fire") || contains("flame") || contains("ember") || contains("burn"))
			return IncandescentEmitterProfile{
				0.72f, 172.0f, 0.96f,
				3.0f, 268.0f, 2.18f,
				{ 1.0f, 0.38f, 0.09f }, 0.095f
			};

		return std::nullopt;
	}

	std::string LowercasePath(const char* a_path)
	{
		if (!a_path)
			return {};
		std::string result(a_path);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}
}

void RadiantGrid::ParticleLightConfigStore::Load()
{
	configs.clear();

	configs["default"] = ParticleLightConfig{};
	logger::info("[LLF] Particle lights config conflict policy: first-win");

	if (std::filesystem::exists("Data\\ParticleLights")) {
		logger::info("[LLF] Loading particle lights configs");

		auto iniFiles = clib_util::distribution::get_configs("Data\\ParticleLights", "", ".ini");
		std::sort(iniFiles.begin(), iniFiles.end());

		if (iniFiles.empty()) {
			logger::warn("[LLF] No .ini files in Data\\ParticleLights");
			return;
		}

		logger::info("[LLF] {} matching inis found", iniFiles.size());

		for (auto& path : iniFiles) {
			logger::info("[LLF] loading ini: {}", path);

			CSimpleIniA ini;
			ini.SetUnicode();
			ini.SetMultiKey();

			if (const auto rc = ini.LoadFile(path.c_str()); rc < 0) {
				logger::error("\t\t[LLF] couldn't read INI");
				continue;
			}

			ParticleLightConfig data{};
			data.cull = ini.GetBoolValue("Light", "Cull", false);

			const auto filename = GetLowercaseStem(path.c_str());
			if (!filename)
				continue;

			if (configs.contains(*filename)) {
				logger::warn("[LLF] Duplicate config '{}'; keeping first, ignoring {}", *filename, path);
				continue;
			}

			logger::debug("[LLF] Inserting {}", *filename);
			configs.emplace(*filename, data);
		}
	}
}

RadiantGrid::VertexColorCacheEntry RadiantGrid::GetParticleLightConfig(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return {};

	if (!settings.EnableParticleLights)
		return {};

	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty || shaderProperty->lightData)
		return {};

	auto material = static_cast<RE::BSEffectShaderMaterial*>(shaderProperty->GetMaterial());
	if (!material)
		return {};

	auto* node = a_pass->geometry;

	{
		// This lookup runs inside Skyrim's immediate render hook. Never stall a
		// draw waiting for cache maintenance on another render worker; missing the
		// optional light for one frame is preferable to blocking the renderer.
	std::shared_lock lock{ particleLightCacheMutex, std::try_to_lock };
		if (!lock.owns_lock())
			return {};
		auto it = vertexColorCache.find(node);
		if (it != vertexColorCache.end()) {
			return it->second;
		}
	}

	auto cacheInvalid = [&](RE::BSGeometry* a_node) {
		VertexColorCacheEntry invalid{};
		invalid.valid = false;
	std::unique_lock lock{ particleLightCacheMutex, std::try_to_lock };
		if (lock.owns_lock())
			vertexColorCache[a_node] = invalid;
		return invalid;
	};

	if (material->sourceTexturePath.empty())
		return cacheInvalid(node);

	auto textureName = GetLowercaseStem(material->sourceTexturePath.c_str());
	if (!textureName)
		return cacheInvalid(node);

	auto& configs = particleLightConfigs.configs;
	auto configIt = configs.find(*textureName);
	// Keep the complete path and nearby scene-node names as classification
	// evidence. Many fire replacers use generic texture stems such as "black" or
	// "fxglowenb", while their folder/node still identifies a torch or brazier.
	std::string emitterEvidence = LowercasePath(material->sourceTexturePath.c_str());
	for (RE::NiAVObject* parent = a_pass->geometry; parent != nullptr; parent = parent->parent) {
		if (const char* name = parent->name.c_str(); name && *name) {
			emitterEvidence.push_back('|');
			emitterEvidence += LowercasePath(name);
		}
	}
	const auto incandescentProfile = GetIncandescentEmitterProfile(emitterEvidence);
	if (configIt == configs.end() && !incandescentProfile)
		return cacheInvalid(node);

	// Preserve the strict billboard requirement for arbitrary external particle
	// configurations. Named incandescent textures may pass through wrapper nodes
	// introduced by modern fire and ember mesh replacers.
	bool billboardOwned = false;
	for (auto* parent = a_pass->geometry->parent; parent != nullptr; parent = parent->parent) {
		if (parent->GetRTTI() == globals::rtti::NiBillboardNodeRTTI.get()) {
			billboardOwned = true;
			break;
		}
	}
	if (!billboardOwned && !incandescentProfile)
		return cacheInvalid(node);

	ParticleLightConfig config = configIt != configs.end() ? configIt->second : ParticleLightConfig{};

	VertexColorCacheEntry entry{};
	entry.valid = true;
	entry.applyEffectMaterialTint = true;
	entry.practicalIncandescent = incandescentProfile.has_value();
	entry.config = config;
	entry.baseColor = { 1, 1, 1, 1 };
	if (incandescentProfile) {
		entry.radiusScale = incandescentProfile->particleRadiusScale;
		entry.minimumRadius = incandescentProfile->particleMinimumRadius;
		entry.intensityScale = incandescentProfile->particleIntensityScale;
		entry.incandescentTint = incandescentProfile->geometryTint;
		entry.flickerStrength = incandescentProfile->flickerStrength;
	}
	bool hasVertexTint = false;
	if (auto rendererData = a_pass->geometry->GetGeometryRuntimeData().rendererData) {
		if (auto triShape = a_pass->geometry->AsTriShape()) {
			const std::uint32_t vertexSize = rendererData->vertexDesc.GetSize();
			if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS) && rendererData->rawVertexData && vertexSize > 0u) {
				const std::uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);
				const std::uint32_t vertexCount = static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount);

				VertexColor weightedVC{};
				if (TryGetAlphaWeightedVertexColor(rendererData->rawVertexData, vertexSize, offset, vertexCount, weightedVC)) {
					entry.baseColor.red *= weightedVC.data[0] / 255.f;
					entry.baseColor.green *= weightedVC.data[1] / 255.f;
					entry.baseColor.blue *= weightedVC.data[2] / 255.f;
					hasVertexTint = true;
					if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha))
						entry.baseColor.alpha *= weightedVC.data[3] / 255.f;
				}
			}
		}
	}

	if (!hasVertexTint) {
		entry.baseColor = BuildEffectMaterialEmissiveTint(material, shaderProperty);
		entry.applyEffectMaterialTint = false;
	}

	{
	std::unique_lock lock{ particleLightCacheMutex, std::try_to_lock };
		if (lock.owns_lock())
			vertexColorCache[node] = entry;
	}
	return entry;
}

RadiantGrid::VertexColorCacheEntry RadiantGrid::GetIncandescentGeometryLightConfig(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry || !settings.EnableParticleLights)
		return {};

	auto* geometry = a_pass->geometry;
	auto& property = geometry->GetGeometryRuntimeData().shaderProperty;
	auto* lightingProperty = property && property->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ?
	                             static_cast<RE::BSLightingShaderProperty*>(property.get()) :
	                             nullptr;
	if (!lightingProperty)
		return {};

	auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(lightingProperty->material);
	{
		// Geometry setup is also on the render path; do not wait behind particle
		// queue/cache maintenance merely to classify an optional emissive proxy.
		std::shared_lock lock{ particleLightCacheMutex, std::try_to_lock };
		if (!lock.owns_lock())
			return {};
		if (auto it = incandescentGeometryCache.find(geometry);
			it != incandescentGeometryCache.end() && it->second.material == material) {
			return it->second.light;
		}
	}

	auto cacheResult = [&](const VertexColorCacheEntry& a_entry) {
		std::unique_lock lock{ particleLightCacheMutex, std::try_to_lock };
		if (lock.owns_lock())
			incandescentGeometryCache[geometry] = { material, a_entry };
		return a_entry;
	};

	if (!material || material->GetFeature() != RE::BSShaderMaterial::Feature::kGlowMap)
		return cacheResult({});

	auto* textures = material->textureSet.get();
	if (!textures)
		return cacheResult({});

	const std::string diffusePath = LowercasePath(textures->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse));
	const std::string glowPath = LowercasePath(textures->GetTexturePath(RE::BSTextureSet::Texture::kGlowMap));
	if (glowPath.empty())
		return cacheResult({});

	const std::string evidence = diffusePath + '|' + glowPath;
	const auto profile = GetIncandescentEmitterProfile(evidence);
	if (!profile)
		return cacheResult({});

	VertexColorCacheEntry entry{};
	entry.valid = true;
	entry.applyEffectMaterialTint = false;
	entry.radiusScale = profile->geometryRadiusScale;
	entry.minimumRadius = profile->geometryMinimumRadius;
	entry.intensityScale = profile->geometryIntensityScale;
	entry.incandescentTint = profile->geometryTint;
	entry.flickerStrength = profile->flickerStrength;

	RE::NiColor tint = profile->geometryTint;
	float emissiveEvidence = 1.0f;
	if (lightingProperty->emissiveColor) {
		const RE::NiColor authored{
			std::max(lightingProperty->emissiveColor->red, 0.0f),
			std::max(lightingProperty->emissiveColor->green, 0.0f),
			std::max(lightingProperty->emissiveColor->blue, 0.0f)
		};
		const float authoredMax = std::max({ authored.red, authored.green, authored.blue });
		const float authoredMin = std::min({ authored.red, authored.green, authored.blue });
		if (authoredMax > 0.05f) {
			emissiveEvidence = authoredMax;
			// Preserve deliberately coloured magical/blue flames while retaining a
			// candle-like warm default for the common neutral-white emissive colour.
			if ((authoredMax - authoredMin) / authoredMax > 0.08f) {
				const RE::NiColor authoredChroma{ authored.red / authoredMax, authored.green / authoredMax, authored.blue / authoredMax };
				tint.red = tint.red * 0.30f + authoredChroma.red * 0.70f;
				tint.green = tint.green * 0.30f + authoredChroma.green * 0.70f;
				tint.blue = tint.blue * 0.30f + authoredChroma.blue * 0.70f;
			}
		}
	}

	const float authoredMultiplier = std::isfinite(lightingProperty->emissiveMult) ?
	                                     std::max(lightingProperty->emissiveMult, 0.0f) :
	                                     0.0f;
	const float response = std::clamp(std::sqrt(std::max(authoredMultiplier * emissiveEvidence, 0.25f)), 0.65f, 2.25f);
	entry.baseColor = {
		tint.red * entry.intensityScale * response,
		tint.green * entry.intensityScale * response,
		tint.blue * entry.intensityScale * response,
		1.0f
	};

	return cacheResult(entry);
}

bool RadiantGrid::QueueResolvedEmitterLight(RE::NiAVObject* a_owner, const ResolvedParticleLight& a_resolved)
{
	if (!a_owner)
		return false;

	// Particle lights are additive enhancement only. If another render worker is
	// updating the queue, leave the stock draw untouched instead of blocking it.
	std::unique_lock lock{ particleLightQueueMutex, std::try_to_lock };
	if (!lock.owns_lock())
		return false;
	if (auto ownerIt = queuedParticleLightOwners.find(a_owner); ownerIt != queuedParticleLightOwners.end()) {
		auto& aggregate = queuedParticleLights[ownerIt->second];

		const float aggregateEnergy = std::max({ aggregate.color.red, aggregate.color.green, aggregate.color.blue }) * aggregate.color.alpha;
		const float resolvedEnergy = std::max({ a_resolved.color.red, a_resolved.color.green, a_resolved.color.blue }) * a_resolved.color.alpha;
		if (resolvedEnergy > aggregateEnergy) {
			aggregate.color = a_resolved.color;
			aggregate.source = a_resolved.source;
			aggregate.flickerStrength = a_resolved.flickerStrength;
		}

		if (a_resolved.radius > aggregate.radius) {
			aggregate.position = a_resolved.position;
			aggregate.radius = a_resolved.radius;
		}
		return true;
	}

	queuedParticleLightOwners.emplace(a_owner, queuedParticleLights.size());
	auto resolved = a_resolved;
	// Owner identity is an opaque per-session phase seed; it is not dereferenced
	// after submission. This keeps neighbouring emitters from pulsing in unison.
	std::uintptr_t seed = reinterpret_cast<std::uintptr_t>(a_owner);
	seed ^= seed >> 17;
	seed *= static_cast<std::uintptr_t>(0xed5ad4bbU);
	seed ^= seed >> 11;
	resolved.flickerPhase = static_cast<float>(seed & 0xffffu) * (6.28318530718f / 65536.0f);
	queuedParticleLights.push_back(resolved);
	return true;
}

bool RadiantGrid::QueueIncandescentGeometryLight(RE::BSRenderPass* a_pass)
{
	if (!a_pass || !a_pass->geometry)
		return false;

	const auto reference = GetIncandescentGeometryLightConfig(a_pass);
	if (!reference.valid)
		return false;

	ResolvedParticleLight resolved{};
	resolved.position = a_pass->geometry->worldBound.center;
	resolved.color = reference.baseColor;
	resolved.radius = std::clamp(
		std::max(a_pass->geometry->worldBound.radius * reference.radiusScale, reference.minimumRadius),
		1.0f,
		2000.0f);
	resolved.source = ResolvedParticleLight::Source::GlowMappedGeometry;
	resolved.flickerStrength = reference.flickerStrength;

	if (!std::isfinite(resolved.position.x) || !std::isfinite(resolved.position.y) || !std::isfinite(resolved.position.z) ||
		!std::isfinite(resolved.color.red) || !std::isfinite(resolved.color.green) || !std::isfinite(resolved.color.blue) ||
		!std::isfinite(resolved.radius)) {
		return false;
	}

	return QueueResolvedEmitterLight(a_pass->geometry, resolved);
}

bool RadiantGrid::QueueParticleLight(RE::BSRenderPass* a_pass, VertexColorCacheEntry& a_reference)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return false;

	auto shaderProperty = a_pass->shaderProperty->GetRTTI() == globals::rtti::BSEffectShaderPropertyRTTI.get() ?
	                          static_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty) :
	                          nullptr;
	if (!shaderProperty)
		return false;

	auto material = static_cast<RE::BSEffectShaderMaterial*>(shaderProperty->GetMaterial());
	if (!material)
		return false;

	RE::NiColorA color = a_reference.baseColor;
	if (a_reference.applyEffectMaterialTint) {
		color.red *= material->baseColor.red * material->baseColorScale;
		color.green *= material->baseColor.green * material->baseColorScale;
		color.blue *= material->baseColor.blue * material->baseColorScale;

		if (auto emittance = shaderProperty->emittanceColor) {
			color.red *= emittance->red;
			color.green *= emittance->green;
			color.blue *= emittance->blue;
		}
	}
	color.red *= a_reference.intensityScale;
	color.green *= a_reference.intensityScale;
	color.blue *= a_reference.intensityScale;
	if (a_reference.practicalIncandescent) {
		// Most flame textures are neutral masks. Warm those masks while preserving
		// deliberately coloured magical fire from the material/vertex evidence.
		const float peak = std::max({ color.red, color.green, color.blue, 1.0e-4f });
		const float floor = std::min({ color.red, color.green, color.blue });
		const float chroma = (peak - floor) / peak;
		const float authoredWeight = std::clamp((chroma - 0.08f) / 0.24f, 0.0f, 1.0f);
		const RE::NiColor authored{ color.red / peak, color.green / peak, color.blue / peak };
		color.red = peak * std::lerp(a_reference.incandescentTint.red, authored.red, authoredWeight);
		color.green = peak * std::lerp(a_reference.incandescentTint.green, authored.green, authoredWeight);
		color.blue = peak * std::lerp(a_reference.incandescentTint.blue, authored.blue, authoredWeight);
	}

	ResolvedParticleLight resolved;
	// The transformed bound centre tracks the visible flame card more reliably
	// than a wrapper node's origin.
	resolved.position = a_pass->geometry->worldBound.center;
	resolved.color = color;
	resolved.radius = std::clamp(
		std::max(a_pass->geometry->worldBound.radius * a_reference.radiusScale, a_reference.minimumRadius),
		1.0f,
		1600.0f);
	if (!std::isfinite(resolved.position.x) || !std::isfinite(resolved.position.y) || !std::isfinite(resolved.position.z) ||
		!std::isfinite(resolved.color.red) || !std::isfinite(resolved.color.green) || !std::isfinite(resolved.color.blue) ||
		!std::isfinite(resolved.color.alpha) || !std::isfinite(resolved.radius)) {
		return false;
	}
	resolved.color.red = std::max(resolved.color.red, 0.0f);
	resolved.color.green = std::max(resolved.color.green, 0.0f);
	resolved.color.blue = std::max(resolved.color.blue, 0.0f);
	resolved.color.alpha = std::clamp(resolved.color.alpha, 0.0f, 1.0f);
	resolved.source = ResolvedParticleLight::Source::Particle;
	resolved.flickerStrength = a_reference.flickerStrength;

	// One billboard owner represents one physical fire/torch/brazier emitter.
	// Skyrim may submit the same owner through multiple immediate-render paths,
	// and layered particle geometry may contribute several eligible draw calls.
	// Keep one non-additive representative so render-pass count cannot multiply
	// local-light energy or consume clustered-light capacity unpredictably.
	RE::NiAVObject* owner = a_pass->geometry->parent;
	if (!owner)
		owner = a_pass->geometry;
	return QueueResolvedEmitterLight(owner, resolved);
}

bool RadiantGrid::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return true;

	using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
	auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get());
	auto reference = GetParticleLightConfig(a_pass);
	if (reference.valid) {
		const bool legacyParticleLayout =
			a_pass->shaderProperty->flags.all(Flag::kSoftEffect, Flag::kZBufferTest) &&
			alphaProperty && alphaProperty->alphaFlags == 4109;
		if (!legacyParticleLayout && !reference.practicalIncandescent)
			return true;
		if (QueueParticleLight(a_pass, reference))
			return !(settings.EnableParticleLightsCulling && reference.config.cull);
	}
	return true;
}

void RadiantGrid::AddParticleLightsToBuffer(eastl::vector<LightData>& a_lightsData,
	std::array<std::uint64_t, MAX_LIGHTS>& a_emitterIdentities,
	std::array<std::uint64_t, MAX_LIGHTS>& a_shadowRevisions)
{
	particleShadowMatchCount = 0;
	glowShadowMatchCount = 0;
	static float& lightFadeStart = *reinterpret_cast<float*>(REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = *reinterpret_cast<float*>(REL::RelocationID(527669, 414583).address());

	// Detach producer work under a short lock so immediate render hooks rarely
	// encounter contention. Persistence and light conversion happen separately.
	eastl::vector<ResolvedParticleLight> pendingLights;
	eastl::hash_map<RE::NiAVObject*, std::size_t> pendingOwners;
	const auto sceneGeneration = particleLightSceneGeneration.load(std::memory_order_acquire);
	{
		std::lock_guard lock{ particleLightQueueMutex };
		pendingLights.swap(queuedParticleLights);
		pendingOwners.swap(queuedParticleLightOwners);
	}

	struct FrameParticleLight
	{
		RE::NiAVObject* owner;
		ResolvedParticleLight light;
	};
	eastl::vector<FrameParticleLight> frameLights;
	double frameTime = 0.0;
	{
		std::lock_guard lock{ particleLightStateMutex };
		if (sceneGeneration != particleLightSceneGeneration.load(std::memory_order_acquire)) {
			pendingLights.clear();
			pendingOwners.clear();
		}

		const float frameDelta = static_cast<float>(RE::GetSecondsSinceLastFrame());
		if (std::isfinite(frameDelta))
			particleLightTime += std::clamp(frameDelta, 0.0f, 0.25f);
		frameTime = particleLightTime;
		for (const auto& [owner, index] : pendingOwners) {
			if (owner && index < pendingLights.size()) {
				auto& persisted = persistedParticleLights[owner];
				persisted.light = pendingLights[index];
				persisted.lastSeenTime = frameTime;
			}
		}

		particleEmitterLightCount = 0;
		glowMappedEmitterLightCount = 0;
		// Always drain queued work while disabled so re-enabling cannot inject
		// stale emitters from an earlier frame.
		if (!settings.EnableParticleLights) {
			persistedParticleLights.clear();
			return;
		}

		// Preserve scalar emitter state briefly while Skyrim omits an occluded
		// billboard, then fade it smoothly instead of popping on camera turns.
		constexpr double kFullHoldSeconds = 2.0 / 60.0;
		constexpr double kLifetimeSeconds = 1.0 / 3.0;
		for (auto it = persistedParticleLights.begin(); it != persistedParticleLights.end();) {
			const double age = frameTime - it->second.lastSeenTime;
			if (age > kLifetimeSeconds) {
				it = persistedParticleLights.erase(it);
				continue;
			}

			ResolvedParticleLight resolved = it->second.light;
			if (age > kFullHoldSeconds) {
				const float fade = 1.0f - static_cast<float>((age - kFullHoldSeconds) /
					(kLifetimeSeconds - kFullHoldSeconds));
				resolved.color.alpha *= std::clamp(fade, 0.0f, 1.0f);
			}
			frameLights.push_back({ it->first, resolved });
			++it;
		}
	}

	for (const auto& frameLight : frameLights) {
		if (a_lightsData.size() >= MAX_LIGHTS)
			break;
		const auto& pl = frameLight.light;

		LightData light{};
		constexpr float invPI = 1.f / std::numbers::pi_v<float>;
		// Smooth, band-limited flame movement is evaluated once and shared by all
		// consumers (direct light, fog and GI), avoiding independent shader noise.
		const float phase = static_cast<float>(frameTime) * 6.1f + pl.flickerPhase;
		const float flameWave =
			std::sin(phase) * 0.55f +
			std::sin(phase * 1.73f + 1.91f) * 0.30f +
			std::sin(phase * 0.37f + 4.13f) * 0.15f;
		const float flicker = std::clamp(1.0f + flameWave * pl.flickerStrength, 0.78f, 1.16f);
		const float warmth = std::clamp(1.0f - flameWave * pl.flickerStrength * 0.18f, 0.97f, 1.03f);
		light.color.x = pl.color.red * flicker * invPI;
		light.color.y = pl.color.green * flicker * invPI;
		light.color.z = pl.color.blue * flicker * warmth * invPI;
		light.color *= pl.color.alpha;

		// ResolvedParticleLight already carries its final conservative world
		// radius. Configured emitters preserve the historic 0.5x default while
		// named flames/candles/torches use their bounded practical-light profile.
		light.radius = pl.radius;

		light.lightFlags.set(LightFlags::Simple);
		SetLightPosition(light, pl.position);

		// Incandescent particles/glow cards are analytic emitter lights, not
		// engine lights. When one coincides closely with a real local caster,
		// reuse that caster's rendered native shadow map for this contribution.
		// The narrow radius-scaled match avoids borrowing shadows from unrelated
		// lights elsewhere in a densely lit interior.
		if (IsLocalShadowCacheActive()) {
			const float matchRadius = std::clamp(pl.radius * 0.1f, 12.0f, 48.0f);
			float bestDistanceSq = matchRadius * matchRadius;
			const LocalShadowCaster* matchedCaster = nullptr;
			for (const auto& caster : localShadowCasters) {
				if (caster.slice < 0 || caster.lastRenderedFrame == 0 || caster.lastSeenFrame != localShadowFrame)
					continue;
				const float distanceSq = caster.position.GetSquaredDistance(pl.position);
				if (distanceSq < bestDistanceSq) {
					bestDistanceSq = distanceSq;
					matchedCaster = &caster;
				}
			}
			if (matchedCaster) {
				light.localShadowIndex = static_cast<uint32_t>(matchedCaster->slice);
				light.lightFlags.set(LightFlags::LocalShadow);
				a_shadowRevisions[a_lightsData.size()] = matchedCaster->lastRenderedFrame;
				if (pl.source == ResolvedParticleLight::Source::GlowMappedGeometry)
					++glowShadowMatchCount;
				else
					++particleShadowMatchCount;
			}
		}

		// These engine fade settings are linear Skyrim units. Comparing squared
		// distance against them rejected practical emitters almost immediately.
		const float centreDistance = std::sqrt(
			(light.positionWS.data.x * light.positionWS.data.x) +
			(light.positionWS.data.y * light.positionWS.data.y) +
			(light.positionWS.data.z * light.positionWS.data.z));
		const float distance = std::max(centreDistance - light.radius, 0.0f);

		float dimmer = 0.0f;
		if (distance < lightFadeStart || lightFadeEnd == 0.0f || lightFadeEnd <= lightFadeStart)
			dimmer = 1.0f;
		else if (distance <= lightFadeEnd)
			dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));

		light.fade = dimmer;
		if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
			light.invRadius = 1.f / light.radius;
			// The owner key already scopes the short-lived persisted emitter. Use
			// its pointer only as an identity token; the tracker never dereferences it.
			a_emitterIdentities[a_lightsData.size()] = reinterpret_cast<std::uintptr_t>(frameLight.owner);
			a_lightsData.push_back(light);
			if (pl.source == ResolvedParticleLight::Source::GlowMappedGeometry)
				++glowMappedEmitterLightCount;
			else
				++particleEmitterLightCount;
		}
	}
	if (!localShadowEmitterMatchLogged && (particleShadowMatchCount > 0 || glowShadowMatchCount > 0)) {
		logger::info("[RadiantGrid] Synthetic emitter shadow reuse active: {} particle and {} glow-mapped emitters matched native local shadows",
			particleShadowMatchCount, glowShadowMatchCount);
		localShadowEmitterMatchLogged = true;
	}
}

template <int N>
void RadiantGrid::Hooks::BSBatchRenderer_RenderPassImmediately<N>::thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
{
	if (globals::pipeline::radiantGrid.CheckParticleLights(a_pass, a_technique))
		func(a_pass, a_technique, a_alphaTest, a_renderFlags);
}

void RadiantGrid::Hooks::BSGeometry_Destroy::thunk(RE::BSGeometry* This)
{
	{
		std::unique_lock lock{ globals::pipeline::radiantGrid.particleLightCacheMutex };
		globals::pipeline::radiantGrid.vertexColorCache.erase(This);
		globals::pipeline::radiantGrid.incandescentGeometryCache.erase(This);
	}
	func(This);
}

void RadiantGrid::Hooks::Install()
{
	stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x6, BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
	stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

	stl::write_thunk_call<ValidLight1>(REL::RelocationID(100994, 107781).address() + 0x92);
	stl::write_thunk_call<ValidLight2>(REL::RelocationID(100997, 107784).address() + REL::Relocate(0x139, 0x12A));
	stl::write_thunk_call<ValidLight3>(REL::RelocationID(101296, 108283).address() + REL::Relocate(0xB7, 0x7E));

	stl::write_thunk_call<RenderPass1>(REL::RelocationID(100877, 107667).address() + REL::Relocate(0x1E5, 0xED));
	stl::write_thunk_call<RenderPass2>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
	if (REL::Module::IsSE())
		stl::write_thunk_call<RenderPass3>(REL::RelocationID(100871, 107661).address() + 0xEE);
	stl::detour_thunk<BSGeometry_Destroy>(REL::RelocationID(69535, 70936));

	logger::info("[LLF] Installed hooks");
}

void RadiantGrid::Hooks::CalculateActiveShadowCasterLights::thunk()
{
	auto& radiantGrid = globals::pipeline::radiantGrid;
	radiantGrid.ScheduleLocalShadowCasters();
	func();
	radiantGrid.localShadowSelecting = false;
}

RE::BSLight* RadiantGrid::Hooks::ShadowSceneNode_AddLight::thunk(RE::ShadowSceneNode* This, RE::NiLight* a_light,
	const RE::ShadowSceneNode::LIGHT_CREATE_PARAMS& a_params)
{
	auto params = a_params;
	const auto& settings = globals::pipeline::radiantGrid.settings;
	if (settings.EnableLocalShadows && settings.PromoteLocalLightsToShadowCasters &&
		IsPromotableLocalLight(a_light) && !params.shadowLight) {
		// Non-shadow point-light creation parameters do not carry a meaningful
		// shadow cone (Light Placer uses 1 radian as its non-shadow default).
		// Promote them as full-sphere point casters; native shadow casters retain
		// their existing spot/hemisphere parameters through the branch above.
		params.shadowLight = true;
		params.dynamic = true;
		params.fov = RE::NI_TWO_PI;
		params.nearDistance = std::isfinite(params.nearDistance) && params.nearDistance > 0.0f ? params.nearDistance : 5.0f;
		params.falloff = std::isfinite(params.falloff) && params.falloff > 0.0f ? params.falloff : 1.0f;
		params.depthBias = std::isfinite(params.depthBias) && params.depthBias > 0.0f ? params.depthBias : 1.0f;
	}
	return func(This, a_light, params);
}

void RadiantGrid::Hooks::ShadowSceneNode_AddLightConvenience::thunk(RE::ShadowSceneNode* This, RE::NiLight* a_light)
{
	const auto& settings = globals::pipeline::radiantGrid.settings;
	if (!settings.EnableLocalShadows || !settings.PromoteLocalLightsToShadowCasters || !IsPromotableLocalLight(a_light)) {
		func(This, a_light);
		return;
	}

	// Mirror Skyrim's convenience overload defaults while requesting its native
	// full-sphere point-light shadow caster through the parameterized overload.
	RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
	params.dynamic = true;
	params.shadowLight = true;
	params.affectLand = true;
	params.affectWater = true;
	params.neverFades = true;
	params.fov = RE::NI_TWO_PI;
	params.falloff = 1.0f;
	params.nearDistance = 5.0f;
	params.depthBias = 1.0f;
	params.sceneGraphIndex = This->GetRuntimeData().sceneGraphIndex;
	ShadowSceneNode_AddLight::func(This, a_light, params);
}

void RadiantGrid::Hooks::Main_RenderFirstPersonView::thunk(bool a1, bool a2)
{
	auto& radiantGrid = globals::pipeline::radiantGrid;
	radiantGrid.localShadowFirstPersonPass = radiantGrid.IsLocalShadowCacheActive();
	func(a1, a2);
	radiantGrid.localShadowFirstPersonPass = false;
}

bool RadiantGrid::Hooks::BSShadowParabolicLight_UpdateCamera::thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera)
{
	const bool result = func(This, a_camera);
	return globals::pipeline::radiantGrid.FilterLocalShadowCaster(This, a_camera, result);
}

bool RadiantGrid::Hooks::BSShadowFrustumLight_UpdateCamera::thunk(RE::BSShadowLight* This, const RE::NiCamera* a_camera)
{
	const bool result = func(This, a_camera);
	return globals::pipeline::radiantGrid.FilterLocalShadowCaster(This, a_camera, result);
}

void RadiantGrid::Hooks::InstallLocalShadows()
{
	if (globals::pipeline::radiantGrid.localShadowHooksInstalled)
		return;

	stl::detour_thunk<ShadowSceneNode_AddLight>(REL::RelocationID(99692, 106326));
	stl::detour_thunk<ShadowSceneNode_AddLightConvenience>(REL::RelocationID(99691, 106325));
	stl::detour_thunk<CalculateActiveShadowCasterLights>(REL::RelocationID(100419, 107137));
	stl::write_vfunc<0x10, BSShadowParabolicLight_UpdateCamera>(RE::VTABLE_BSShadowParabolicLight[0]);
	stl::write_vfunc<0x10, BSShadowFrustumLight_UpdateCamera>(RE::VTABLE_BSShadowFrustumLight[0]);
	stl::detour_thunk<Main_RenderFirstPersonView>(REL::RelocationID(100411, 107129));
	globals::pipeline::radiantGrid.localShadowHooksInstalled = true;
	logger::info("[RadiantGrid] Installed local shadow scheduling hooks; caster promotion is {}",
		globals::pipeline::radiantGrid.settings.PromoteLocalLightsToShadowCasters ? "enabled by setting" : "disabled by setting");
}

#undef I18N_KEY_PREFIX
