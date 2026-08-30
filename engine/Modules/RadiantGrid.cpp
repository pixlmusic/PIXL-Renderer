#include "RadiantGrid.h"
#include "NaturalLighting.h"
#include "LinearLightCore.h"

#include "I18n/I18n.h"
#include "Menu/ThemeManager.h"
#include "Shadercache.h"
#include "State.h"
#include "Utils/ExternalEmittance.h"

#include <numbers>

#define I18N_KEY_PREFIX "feature.radiant_grid."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RadiantGrid::Settings,
	EnableParticleLights,
	EnableParticleLightsCulling,
	EnableLightsVisualisation,
	LightsVisualisationMode)

// Keep synchronized with RadiantGrid/Common.hlsli::MAX_CLUSTER_LIGHTS. Dense
// interiors and particle-heavy scenes can legitimately exceed 128 overlapping
// lights; truncating the list there creates visible 64-pixel cluster boundaries.
// Reserve the original 256 entries per cluster on the CPU as well so the GPU
// never relies on the pre-audit undersized allocation.
static constexpr uint CLUSTER_MAX_LIGHTS = 256;

void RadiantGrid::DrawSettings()
{
	auto shaderCache = globals::shaderCache;

	ImGui::Checkbox(T(TKEY("enable_particle_lights"), "Enable Particle Lights"), &settings.EnableParticleLights);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds supported effect/particle emitters to the clustered local-light list. Updates in real time.");
	if (settings.EnableParticleLights) {
		ImGui::Checkbox("Cull Configured Particle Lights", &settings.EnableParticleLightsCulling);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Honours per-effect particle-light culling rules from configuration files. Updates in real time and can reduce unnecessary clustered lights.");
	}

	ImGui::Spacing();

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("statistics"), "Statistics"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Clustered Light Count: %u", lightCount);

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
	std::copy(clusterSize, clusterSize + 3, perFrame.ClusterSize);
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
		clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\RadiantGrid\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
		clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\RadiantGrid\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");

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

	if (!shaderCache->IsEnabled())
		return;

	strictLightDataTemp.NumStrictLights = 0;
	strictLightDataTemp.ShadowBitMask = 0;

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
	auto& isl = globals::pipeline::naturalLighting;

	auto accumulator = *globals::game::currentAccumulator.get();
	bool inWorld = accumulator->GetRuntimeData().activeShadowSceneNode == globals::game::smState->shadowSceneNode[0];

	strictLightDataTemp.NumStrictLights = inWorld ? 0 : (a_pass->numLights - 1);

	uint32_t writeIdx = 0;
	for (uint32_t i = 0; i < strictLightDataTemp.NumStrictLights; i++) {
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

		if (i < a_pass->numShadowLights) {
			auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
			auto& maskIndex = shadowLight->GetRuntimeData().maskIndex;
			light.shadowMaskIndex = maskIndex;
			light.lightFlags.set(LightFlags::Shadow);
		}

		strictLightDataTemp.StrictLights[writeIdx++] = light;
	}
	strictLightDataTemp.NumStrictLights = writeIdx;

	for (uint32_t i = 0; i < a_pass->numShadowLights; i++) {
		auto bsLight = a_pass->sceneLights[i + 1];
		if (!bsLight)
			continue;
		auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
		auto& maskIndex = shadowLight->GetRuntimeData().maskIndex;
		strictLightDataTemp.ShadowBitMask |= (1u << maskIndex);
	}
}

void RadiantGrid::BSLightingShader_SetupGeometry_After(RE::BSRenderPass*)
{
	auto shaderCache = globals::shaderCache;
	auto context = globals::d3d::context;
	auto smState = globals::game::smState;

	if (!shaderCache->IsEnabled())
		return;

	auto accumulator = *globals::game::currentAccumulator.get();

	auto shadowSceneNode = smState->shadowSceneNode[0];

	const auto isEmpty = strictLightDataTemp.NumStrictLights == 0;
	const bool isWorld = accumulator->GetRuntimeData().activeShadowSceneNode == shadowSceneNode;
	const auto roomIndex = strictLightDataTemp.RoomIndex;
	const auto shadowBitMask = strictLightDataTemp.ShadowBitMask;

	if (!isEmpty || (isEmpty && !wasEmpty) || isWorld != wasWorld || previousRoomIndex != roomIndex || shadowBitMask != previousShadowBitMask) {
		strictLightDataCB->Update(strictLightDataTemp);
		wasEmpty = isEmpty;
		wasWorld = isWorld;
		previousRoomIndex = roomIndex;
		previousShadowBitMask = shadowBitMask;
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

bool RadiantGrid::IsGlobalLight(RE::BSLight* a_light)
{
	return !(a_light->portalStrict || !a_light->portalGraph);
}

void RadiantGrid::PostPostLoad()
{
	particleLightConfigs.Load();
	Hooks::Install();
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
	std::vector<std::pair<const char*, const char*>> clusterDefines;
	clusterBuildingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\RadiantGrid\\ClusterBuildingCS.hlsl", clusterDefines, "cs_5_0");
	clusterCullingCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\RadiantGrid\\ClusterCullingCS.hlsl", clusterDefines, "cs_5_0");
}

void RadiantGrid::UpdateLights()
{
	auto smState = globals::game::smState;
	auto& isl = globals::pipeline::naturalLighting;
	if (!smState || !globals::d3d::context || !lights || !lights->resource) {
		lightCount = 0;
		return;
	}

	auto shadowSceneNode = smState->shadowSceneNode[0];
	if (!shadowSceneNode) {
		lightCount = 0;
		return;
	}

	// Cache camera position from the FrameBuffer snapshot; shadowState::posAdjust can be stale in first-person

	{
		auto eyePosition = globals::game::frameBufferCached.GetCameraPosAdjust();
		eyePositionCached = { eyePosition.x, eyePosition.y, eyePosition.z };
	}

	eastl::vector<LightData> lightsData{};
	lightsData.reserve(MAX_LIGHTS);

	// Process point lights

	roomNodes.clear();

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
			if (auto niLight = bsLight->light.get()) {
				if (IsValidLight(bsLight)) {
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

					if (!IsGlobalLight(bsLight)) {
						// List of BSMultiBoundRooms affected by a light
						for (const auto& roomPtr : bsLight->rooms) {
							addRoom(roomPtr, light);
						}
						// List of BSPortals affected by a light
						for (const auto& portalPtr : bsLight->portals) {
							addRoom(portalPtr->portalSharedNode.get(), light);
						}
						light.lightFlags.set(LightFlags::PortalStrict);
					}

					if (bsLight->IsShadowLight()) {
						auto* shadowLight = static_cast<RE::BSShadowLight*>(bsLight);
						auto& maskIndex = shadowLight->GetRuntimeData().maskIndex;
						light.shadowMaskIndex = maskIndex;
						light.lightFlags.set(LightFlags::Shadow);
					}

					// Check for inactive shadow light
					if (light.shadowMaskIndex != 255) {
						SetLightPosition(light, niLight->world.translate);

						if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
							lightsData.push_back(light);
						}
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

	AddParticleLightsToBuffer(lightsData);

	auto context = globals::d3d::context;

	lightCount = std::min((uint)lightsData.size(), MAX_LIGHTS);

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(lights->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	const size_t bytes = sizeof(LightData) * lightCount;
	if (bytes > 0)
		memcpy_s(mapped.pData, sizeof(LightData) * MAX_LIGHTS, lightsData.data(), bytes);
	context->Unmap(lights->resource.get(), 0);

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
}

void RadiantGrid::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	auto& singleton = globals::pipeline::radiantGrid;
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

	struct IncandescentParticleProfile
	{
		float radiusScale;
		float minimumRadius;
		float intensityScale;
	};

	std::optional<IncandescentParticleProfile> GetIncandescentParticleProfile(std::string_view a_stem)
	{
		// This is deliberately restricted to practical incandescent emitters.
		// The existing soft-effect/billboard/depth-test checks still apply, and
		// an attached Skyrim light remains authoritative (GetParticleLightConfig
		// rejects shader properties which already own lightData).
		auto contains = [&](std::string_view token) {
			return a_stem.find(token) != std::string_view::npos;
		};

		if (contains("candle") || contains("wick"))
			return IncandescentParticleProfile{ 8.0f, 280.0f, 0.90f };
		if (contains("torch") || contains("sconce"))
			return IncandescentParticleProfile{ 10.0f, 420.0f, 1.05f };
		if (contains("brazier") || contains("bonfire") || contains("campfire") || contains("hearth"))
			return IncandescentParticleProfile{ 12.0f, 620.0f, 1.15f };
		if (contains("fire") || contains("flame") || contains("ember") || contains("burn"))
			return IncandescentParticleProfile{ 9.0f, 380.0f, 1.00f };

		return std::nullopt;
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

	auto parent = a_pass->geometry->parent;
	if (!parent || parent->GetRTTI() != globals::rtti::NiBillboardNodeRTTI.get())
		return {};

	auto* node = a_pass->geometry;

	{
		std::shared_lock lock{ particleLightsMutex };
		auto it = vertexColorCache.find(node);
		if (it != vertexColorCache.end()) {
			return it->second;
		}
	}

	auto cacheInvalid = [&](RE::BSGeometry* a_node) {
		VertexColorCacheEntry invalid{};
		invalid.valid = false;
		std::unique_lock lock{ particleLightsMutex };
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
	const auto incandescentProfile = GetIncandescentParticleProfile(*textureName);
	if (configIt == configs.end() && !incandescentProfile)
		return cacheInvalid(node);

	ParticleLightConfig config = configIt != configs.end() ? configIt->second : ParticleLightConfig{};

	VertexColorCacheEntry entry{};
	entry.valid = true;
	entry.applyEffectMaterialTint = true;
	entry.config = config;
	entry.baseColor = { 1, 1, 1, 1 };
	if (incandescentProfile) {
		entry.radiusScale = incandescentProfile->radiusScale;
		entry.minimumRadius = incandescentProfile->minimumRadius;
		entry.intensityScale = incandescentProfile->intensityScale;
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
		std::unique_lock lock{ particleLightsMutex };
		vertexColorCache[node] = entry;
	}
	return entry;
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

	ResolvedParticleLight resolved;
	resolved.position = a_pass->geometry->world.translate;
	resolved.color = color;
	resolved.radius = std::clamp(
		std::max(a_pass->geometry->worldBound.radius * a_reference.radiusScale, a_reference.minimumRadius),
		1.0f,
		1600.0f);

	std::unique_lock lock{ particleLightsMutex };
	queuedParticleLights.push_back(resolved);

	return true;
}

bool RadiantGrid::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t)
{
	if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty)
		return true;

	using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
	if (!a_pass->shaderProperty->flags.all(Flag::kSoftEffect, Flag::kZBufferTest))
		return true;

	auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get());
	if (!alphaProperty || alphaProperty->alphaFlags != 4109)
		return true;

	auto reference = GetParticleLightConfig(a_pass);
	if (reference.valid) {
		if (QueueParticleLight(a_pass, reference))
			return !(settings.EnableParticleLightsCulling && reference.config.cull);
	}
	return true;
}

void RadiantGrid::AddParticleLightsToBuffer(eastl::vector<LightData>& a_lightsData)
{
	if (!settings.EnableParticleLights)
		return;

	static float& lightFadeStart = *reinterpret_cast<float*>(REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = *reinterpret_cast<float*>(REL::RelocationID(527669, 414583).address());

	std::unique_lock lock{ particleLightsMutex };

	currentParticleLights.clear();
	std::swap(currentParticleLights, queuedParticleLights);

	for (const auto& pl : currentParticleLights) {
		if (a_lightsData.size() >= MAX_LIGHTS)
			break;

		LightData light{};
		constexpr float invPI = 1.f / std::numbers::pi_v<float>;
		light.color.x = pl.color.red * invPI;
		light.color.y = pl.color.green * invPI;
		light.color.z = pl.color.blue * invPI;
		light.color *= pl.color.alpha;

		// ResolvedParticleLight already carries its final conservative world
		// radius. Configured emitters preserve the historic 0.5x default while
		// named flames/candles/torches use their bounded practical-light profile.
		light.radius = pl.radius;

		light.lightFlags.set(LightFlags::Simple);
		SetLightPosition(light, pl.position);

		float distance = (light.positionWS.data.x * light.positionWS.data.x) +
		                 (light.positionWS.data.y * light.positionWS.data.y) +
		                 (light.positionWS.data.z * light.positionWS.data.z) -
		                 (light.radius * light.radius);

		float dimmer = 0.0f;
		if (distance < lightFadeStart || lightFadeEnd == 0.0f || lightFadeEnd <= lightFadeStart)
			dimmer = 1.0f;
		else if (distance <= lightFadeEnd)
			dimmer = 1.0f - ((distance - lightFadeStart) / (lightFadeEnd - lightFadeStart));

		light.fade = dimmer;
		if ((light.color.x + light.color.y + light.color.z) * light.fade > 1e-4 && light.radius > 1e-4) {
			light.invRadius = 1.f / light.radius;
			a_lightsData.push_back(light);
		}
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
		std::unique_lock lock{ globals::pipeline::radiantGrid.particleLightsMutex };
		globals::pipeline::radiantGrid.vertexColorCache.erase(This);
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

#undef I18N_KEY_PREFIX
