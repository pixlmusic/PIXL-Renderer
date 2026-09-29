// PIXL Renderer - distant loaded-light representation.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "DistantLife.h"

#include "Deferred.h"
#include "Globals.h"
#include "State.h"
#include "Util.h"

#include <RE/A/ActorMagicCaster.h>
#include <RE/N/NiLight.h>
#include <RE/N/NiNode.h>
#include <RE/P/ProcessLists.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DistantLife::Settings,
	Enabled, Intensity, MinimumDistance, MaximumDistance, StaticLights,
	ActorTorches, AtmosphericAttenuation, FarFieldActivity, FarFieldDensity,
	FarFieldMotion, DebugMode)

namespace
{
	constexpr float kScanInterval = 0.75f;
	constexpr float kStaleLifetime = 2.5f;

	bool Finite(const RE::NiPoint3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	float3 LinearLightColor(const RE::Color& color)
	{
		const auto decode = [](std::uint8_t channel) {
			return std::pow(static_cast<float>(channel) / 255.0f, 2.2f);
		};
		return { decode(color.red), decode(color.green), decode(color.blue) };
	}

	float3 LinearLightColor(const RE::NiColor& color, float fade)
	{
		const auto decode = [](float channel) {
			return std::pow(std::clamp(channel, 0.0f, 1.0f), 2.2f);
		};
		const float scale = std::clamp(std::isfinite(fade) ? fade : 1.0f, 0.25f, 8.0f);
		return { decode(color.red) * scale, decode(color.green) * scale, decode(color.blue) * scale };
	}

	// Static light references often use an object pivot that is above, below, or
	// offset from the actual lamp/campfire node. Prefer the loaded NiLight
	// transform when available; the reference position remains the safe fallback
	// for unloaded or asset-specific light objects.
	bool FindStaticLightNodePosition(RE::TESObjectREFR* reference, RE::NiPoint3& result)
	{
		if (!reference)
			return false;
		auto* root = reference->Get3D(false);
		if (!root)
			return false;

		std::vector<RE::NiAVObject*> pending{ root };
		constexpr std::size_t kMaxNodes = 1024;
		for (std::size_t cursor = 0; cursor < pending.size() && cursor < kMaxNodes; ++cursor) {
			auto* object = pending[cursor];
			if (!object)
				continue;
			if (auto* light = skyrim_cast<RE::NiLight*>(object)) {
				if (Finite(light->world.translate)) {
					result = light->world.translate;
					return true;
				}
			}
			if (auto* node = object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (child)
						pending.push_back(child.get());
				}
			}
		}
		return false;
	}
}

std::pair<std::string, std::vector<std::string>> DistantLife::GetModuleSummary()
{
	return {
		"Experimental distant optical presence for genuine loaded exterior light references. It never changes Skyrim simulation or saves.",
		{ "Bounded local discovery", "Worldspace-safe source identity", "Depth-aware HDR composition", "Render-origin compatible coordinates" }
	};
}

void DistantLife::SetupResources()
{
	emitterBuffer = nullptr;
	emitterSRV = nullptr;
	tuningCB.reset();
	mask.reset();
	ClearShaderCache();

	auto* device = globals::d3d::device;
	if (!device)
		return;

	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = static_cast<UINT>(sizeof(GPUEmitter) * kMaxEmitters);
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufferDesc.StructureByteStride = sizeof(GPUEmitter);
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, emitterBuffer.put()))) {
		logger::error("[DistantLife] Emitter buffer creation failed; module will remain inert");
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = kMaxEmitters;
	if (FAILED(device->CreateShaderResourceView(emitterBuffer.get(), &srvDesc, emitterSRV.put()))) {
		logger::error("[DistantLife] Emitter SRV creation failed; module will remain inert");
		emitterBuffer = nullptr;
		return;
	}

	Util::SetResourceName(emitterBuffer.get(), "DistantLife::Emitters");
	Util::SetResourceName(emitterSRV.get(), "DistantLife::Emitters SRV");
	tuningCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<TuningData>(), "DistantLife::Tuning");
	GetBuildMaskShader();
	GetCompositeShader();
}

void DistantLife::ObserveStaticLight(
	const RE::TESObjectLIGH* light,
	const RE::TESObjectREFR* reference,
	const RE::NiPoint3* sourcePosition,
	bool actorSource)
{
	if (!settings.Enabled || !light || !reference || reference->IsDisabled())
		return;
	if (actorSource) {
		if (!settings.ActorTorches || !reference->As<RE::Actor>())
			return;
	} else if (!settings.StaticLights || light->CanBeCarried()) {
		return;
	}
	if (light->data.flags.any(RE::TES_LIGHT_FLAGS::kNegative, RE::TES_LIGHT_FLAGS::kOffByDefault))
		return;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell() || player->GetParentCell()->IsInteriorCell())
		return;
	auto* currentWorld = player->GetWorldspace();
	if (!currentWorld)
		return;

	const auto position = sourcePosition ? *sourcePosition : reference->GetPosition();
	if (!Finite(position))
		return;

	const std::uint64_t key = (std::uint64_t(currentWorld->GetFormID()) << 32u) | reference->GetFormID();
	auto& emitter = emitters[key];
	emitter.position = position;
	emitter.radius = std::clamp(static_cast<float>(light->data.radius), 32.0f, 4096.0f);
	emitter.color = LinearLightColor(light->data.color);
	emitter.formID = reference->GetFormID();
	emitter.worldspace = currentWorld->GetFormID();
	emitter.ownerID = actorSource ? reference->GetFormID() : 0;
	emitter.transient = false;
	emitter.lastSeen = elapsedTime;
}

void DistantLife::ObserveRuntimeLight(
	const RE::NiLight* light,
	const RE::Actor* owner,
	std::uint64_t salt,
	bool transient)
{
	if (!settings.Enabled || !settings.ActorTorches || !light || !owner || owner->IsDisabled())
		return;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->GetParentCell() || player->GetParentCell()->IsInteriorCell())
		return;
	auto* currentWorld = player->GetWorldspace();
	if (!currentWorld || !Finite(light->world.translate))
		return;
	const auto& runtime = light->GetLightRuntimeData();
	const float runtimeRadius = std::max({ runtime.radius.x, runtime.radius.y, runtime.radius.z });
	const std::uint64_t key = (std::uint64_t(currentWorld->GetFormID()) << 32u) ^
		(std::uint64_t(owner->GetFormID()) << 1u) ^ salt;
	auto& emitter = emitters[key];
	emitter.position = light->world.translate;
	emitter.radius = std::clamp(std::isfinite(runtimeRadius) ? runtimeRadius : 256.0f, 32.0f, 4096.0f);
	emitter.color = LinearLightColor(runtime.diffuse, runtime.fade);
	emitter.formID = owner->GetFormID();
	emitter.ownerID = owner->GetFormID();
	emitter.worldspace = currentWorld->GetFormID();
	emitter.transient = transient;
	emitter.lastSeen = elapsedTime;
}

void DistantLife::DiscoverStaticLights(RE::PlayerCharacter* player)
{
	auto* tes = RE::TES::GetSingleton();
	if (!tes || !player || !settings.StaticLights)
		return;

	// Keep CPU/reference discovery bounded to Skyrim's meaningfully loaded world.
	// The GPU far-field representation may continue to 200k without turning the
	// periodic reference scan into an unbounded exterior-world query.
	const float radius = std::clamp(settings.MaximumDistance * 1.05f, 512.0f, 120000.0f);
	tes->ForEachReferenceInRange(player, radius, [&](RE::TESObjectREFR* reference) {
		if (!reference || reference == player || reference->IsDisabled())
			return RE::BSContainer::ForEachResult::kContinue;
		if (auto* base = reference->GetBaseObject()) {
			if (auto* light = base->As<RE::TESObjectLIGH>()) {
				RE::NiPoint3 nodePosition{};
				const auto* sourcePosition = FindStaticLightNodePosition(reference, nodePosition) ?
					std::addressof(nodePosition) : nullptr;
				ObserveStaticLight(light, reference, sourcePosition);
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	while (emitters.size() > kMaxRegistryEntries) {
		auto oldest = emitters.end();
		for (auto it = emitters.begin(); it != emitters.end(); ++it) {
			if (oldest == emitters.end() || it->second.lastSeen < oldest->second.lastSeen)
				oldest = it;
		}
		if (oldest == emitters.end())
			break;
		emitters.erase(oldest);
	}
}

void DistantLife::DiscoverActorLights(RE::PlayerCharacter* player)
{
	if (!player || !settings.ActorTorches)
		return;
	auto* processes = RE::ProcessLists::GetSingleton();
	if (!processes)
		return;
	processes->ForEachHighActor([&](RE::Actor* actor) {
		if (!actor || actor->IsDisabled() || !actor->Get3D(false))
			return RE::BSContainer::ForEachResult::kContinue;
		std::vector<RE::NiAVObject*> pending{ actor->Get3D(false) };
		constexpr std::size_t kMaxNodes = 2048;
		for (std::size_t cursor = 0; cursor < pending.size() && cursor < kMaxNodes; ++cursor) {
			auto* object = pending[cursor];
			if (!object)
				continue;
			if (auto* light = skyrim_cast<RE::NiLight*>(object))
				ObserveRuntimeLight(light, actor, reinterpret_cast<std::uintptr_t>(light), false);
			if (auto* node = object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (child)
						pending.push_back(child.get());
				}
			}
		}
		for (auto hand : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
			auto* caster = skyrim_cast<RE::ActorMagicCaster*>(actor->GetMagicCaster(hand));
			if (caster && caster->light && caster->light->light)
				ObserveRuntimeLight(caster->light->light.get(), actor, 0xC000u + (hand == RE::MagicSystem::CastingSource::kRightHand ? 1u : 0u), true);
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
}

void DistantLife::Prepass()
{
	uploadedCount = 0;
	const float dt = std::clamp(globals::game::deltaTime ? *globals::game::deltaTime : 0.0f, 0.0f, 0.25f);
	elapsedTime += dt;
	scanCountdown -= dt;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!settings.Enabled || !settings.StaticLights || !player || !player->GetWorldspace() || !player->GetParentCell() ||
		player->GetParentCell()->IsInteriorCell()) {
		emitters.clear();
		worldspace = 0;
		scanCountdown = 0.0f;
		return;
	}

	const auto activeWorld = player->GetWorldspace()->GetFormID();
	if (worldspace != 0 && worldspace != activeWorld)
		emitters.clear();
	worldspace = activeWorld;
	if (scanCountdown <= 0.0f) {
		DiscoverStaticLights(player);
		DiscoverActorLights(player);
		scanCountdown = kScanInterval;
	}

	const auto playerPosition = player->GetPosition();
	const float minimum = std::clamp(settings.MinimumDistance, 256.0f, 20000.0f);
	const float maximum = std::clamp(settings.MaximumDistance, minimum + 1.0f, 200000.0f);
	const float maximum2 = maximum * maximum;
	// Skyrim's view-projection matrix consumes native camera-relative positions.
	// Build those directly from the frame's authoritative engine origin. This is
	// also compatible with PIXL Render Origin because that service never mutates
	// Skyrim transforms or its b12 matrices.
	const auto& cameraAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();

	struct Candidate
	{
		float distance2;
		const Emitter* emitter;
	};
	std::array<Candidate, kMaxRegistryEntries> candidates{};
	std::size_t candidateCount = 0;
	for (auto it = emitters.begin(); it != emitters.end();) {
		const auto& emitter = it->second;
		const float distance2 = emitter.position.GetSquaredDistance(playerPosition);
		const float staleLifetime = emitter.transient ? kStaleLifetime : 8.0f;
		if (emitter.worldspace != activeWorld || !std::isfinite(distance2) ||
			distance2 > maximum2 * 1.21f || elapsedTime - emitter.lastSeen > staleLifetime) {
			it = emitters.erase(it);
			continue;
		}
		if (distance2 >= minimum * minimum && distance2 <= maximum2 && candidateCount < candidates.size())
			candidates[candidateCount++] = { distance2, std::addressof(emitter) };
		++it;
	}

	if (candidateCount > kMaxEmitters) {
		std::nth_element(candidates.begin(), candidates.begin() + kMaxEmitters, candidates.begin() + candidateCount,
			[](const Candidate& lhs, const Candidate& rhs) { return lhs.distance2 < rhs.distance2; });
		candidateCount = kMaxEmitters;
	}
	std::sort(candidates.begin(), candidates.begin() + candidateCount,
		[](const Candidate& lhs, const Candidate& rhs) { return lhs.distance2 < rhs.distance2; });

	for (std::size_t index = 0; index < candidateCount; ++index) {
		const auto& candidate = candidates[index];
		const auto& source = *candidate.emitter;
		const RE::NiPoint3 relative{
			source.position.x - cameraAdjust.x,
			source.position.y - cameraAdjust.y,
			source.position.z - cameraAdjust.z
		};
		if (!Finite(relative))
			continue;
		upload[uploadedCount++] = {
			{ relative.x, relative.y, relative.z },
			source.radius,
			source.color,
			std::sqrt(candidate.distance2)
		};
	}

	if (!uploadedCount || !emitterBuffer || !globals::d3d::context)
		return;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(globals::d3d::context->Map(emitterBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		uploadedCount = 0;
		return;
	}
	std::memcpy(mapped.pData, upload.data(), sizeof(GPUEmitter) * uploadedCount);
	globals::d3d::context->Unmap(emitterBuffer.get(), 0);
}

void DistantLife::EnsureMask(std::uint32_t width, std::uint32_t height)
{
	if (mask && mask->desc.Width == width && mask->desc.Height == height)
		return;

	mask.reset();
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	mask = std::make_unique<Texture2D>(desc, "DistantLife::OpticalMask");
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	mask->CreateSRV(srvDesc);
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	mask->CreateUAV(uavDesc);
}

ID3D11ComputeShader* DistantLife::GetBuildMaskShader()
{
	if (!buildMaskCS)
		buildMaskCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\DistantLife\\DistantLifeCS.hlsl", {}, "cs_5_0", "BuildMaskCS")));
	return buildMaskCS.get();
}

ID3D11ComputeShader* DistantLife::GetCompositeShader()
{
	if (!compositeCS)
		compositeCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\DistantLife\\DistantLifeCS.hlsl", {}, "cs_5_0", "CompositeCS")));
	return compositeCS.get();
}

void DistantLife::DrawDistantLife()
{
	if (!loaded || !settings.Enabled || (!settings.FarFieldActivity && uploadedCount == 0) || Util::IsInterior())
		return;
	auto* renderer = globals::game::renderer;
	auto* context = globals::d3d::context;
	auto* state = globals::state;
	auto* deferred = globals::deferred;
	if (!renderer || !context || !state || !deferred || !emitterSRV || !tuningCB ||
		!state->sharedDataCB || !*globals::game::perFrame.get())
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[deferred->forwardRenderTargets[0]];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	if (!main.texture || !main.UAV || !depth.depthSRV)
		return;
	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);
	if (!mainDesc.Width || !mainDesc.Height)
		return;
	const float2 active = Util::ConvertToDynamic(
		{ static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) }, true);
	const auto width = std::clamp(static_cast<std::uint32_t>(active.x), 1u, mainDesc.Width);
	const auto height = std::clamp(static_cast<std::uint32_t>(active.y), 1u, mainDesc.Height);
	EnsureMask(width, height);
	if (!mask || !mask->srv || !mask->uav)
		return;

	auto* buildShader = GetBuildMaskShader();
	auto* compositeShader = GetCompositeShader();
	if (!buildShader || !compositeShader) {
		if (!shaderFailureLogged) {
			logger::error("[DistantLife] Shader compilation failed; vanilla scene retained");
			shaderFailureLogged = true;
		}
		return;
	}

	TuningData tuning{};
	tuning.renderSize = { static_cast<float>(width), static_cast<float>(height) };
	tuning.invRenderSize = { 1.0f / width, 1.0f / height };
	tuning.outputSize = { static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) };
	tuning.invOutputSize = { 1.0f / mainDesc.Width, 1.0f / mainDesc.Height };
	tuning.minimumDistance = std::clamp(settings.MinimumDistance, 256.0f, 20000.0f);
	tuning.maximumDistance = std::clamp(settings.MaximumDistance, tuning.minimumDistance + 1.0f, 200000.0f);
	tuning.intensity = std::clamp(settings.Intensity, 0.0f, 4.0f);
	tuning.atmosphericAttenuation = settings.AtmosphericAttenuation ? 1.0f : 0.0f;
	tuning.emitterCount = uploadedCount;
	tuning.debugMode = settings.DebugMode;
	tuning.farFieldActivity = settings.FarFieldActivity ? 1u : 0u;
	tuning.farFieldDensity = std::clamp(settings.FarFieldDensity, 0.0f, 0.5f);
	tuning.farFieldMotion = std::clamp(settings.FarFieldMotion, 0.0f, 1.0f);
	tuningCB->Update(tuning);

	const UINT clear[4]{};
	context->ClearUnorderedAccessViewUint(mask->uav.get(), clear);
	ID3D11Buffer* constantBuffers[3]{ state->sharedDataCB->CB(), *globals::game::perFrame.get(), tuningCB->CB() };
	context->CSSetConstantBuffers(5, 1, &constantBuffers[0]);
	context->CSSetConstantBuffers(12, 2, &constantBuffers[1]);

	globals::profiler->BeginPass("DistantLifeBuild");
	ID3D11ShaderResourceView* buildSRVs[2]{ emitterSRV.get(), depth.depthSRV };
	ID3D11UnorderedAccessView* maskUAV = mask->uav.get();
	context->CSSetShaderResources(0, 2, buildSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &maskUAV, nullptr);
	context->CSSetShader(buildShader, nullptr, 0);
	if (uploadedCount > 0)
		context->Dispatch(uploadedCount, 1, 1);
	globals::profiler->EndPass();

	ID3D11ShaderResourceView* nullSRVs[2]{};
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	globals::profiler->BeginPass("DistantLifeComposite");
	ID3D11ShaderResourceView* maskSRV = mask->srv.get();
	ID3D11ShaderResourceView* compositeSRVs[2]{ maskSRV, depth.depthSRV };
	ID3D11UnorderedAccessView* mainUAV = main.UAV;
	context->CSSetShaderResources(0, 2, compositeSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &mainUAV, nullptr);
	context->CSSetShader(compositeShader, nullptr, 0);
	context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
	globals::profiler->EndPass();

	ID3D11Buffer* nullBuffers[2]{};
	context->CSSetShaderResources(0, 2, nullSRVs);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetConstantBuffers(5, 1, nullBuffers);
	context->CSSetConstantBuffers(12, 2, nullBuffers);
	context->CSSetShader(nullptr, nullptr, 0);
}

void DistantLife::Reset()
{
	uploadedCount = 0;
	if (!settings.Enabled)
		emitters.clear();
}

void DistantLife::ClearShaderCache()
{
	buildMaskCS = nullptr;
	compositeCS = nullptr;
	shaderFailureLogged = false;
}

void DistantLife::LoadSettings(json& j)
{
	settings = j;
	if (!std::isfinite(settings.Intensity))
		settings.Intensity = 1.0f;
	if (!std::isfinite(settings.MinimumDistance))
		settings.MinimumDistance = 1800.0f;
	if (!std::isfinite(settings.MaximumDistance))
		settings.MaximumDistance = 200000.0f;
	settings.Intensity = std::clamp(settings.Intensity, 0.0f, 4.0f);
	settings.MinimumDistance = std::clamp(settings.MinimumDistance, 256.0f, 20000.0f);
	settings.MaximumDistance = std::clamp(settings.MaximumDistance, settings.MinimumDistance + 1.0f, 200000.0f);
	settings.FarFieldDensity = std::clamp(std::isfinite(settings.FarFieldDensity) ? settings.FarFieldDensity : 0.18f, 0.0f, 0.5f);
	settings.FarFieldMotion = std::clamp(std::isfinite(settings.FarFieldMotion) ? settings.FarFieldMotion : 0.35f, 0.0f, 1.0f);
	settings.DebugMode = std::min(settings.DebugMode, 2u);
}

void DistantLife::SaveSettings(json& j) { j = settings; }

void DistantLife::RestoreDefaultSettings()
{
	settings = {};
	emitters.clear();
	uploadedCount = 0;
	worldspace = 0;
	scanCountdown = 0.0f;
}

void DistantLife::DrawSettings()
{
	ImGui::TextWrapped("EXPERIMENTAL. Adds distant optical presence for real exterior lights, actor torches, campfires, and active spell lights that Skyrim has loaded. It does not alter NPCs, AI, game lights, saves, or world state.");
	if (ImGui::Checkbox("Enable DistantLife", &settings.Enabled)) {
		emitters.clear();
		scanCountdown = 0.0f;
	}
	ImGui::BeginDisabled(!settings.Enabled);
	ImGui::SliderFloat("Distant light intensity", &settings.Intensity, 0.0f, 4.0f, "%.2fx");
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Controls distant glow strength. This changes only PIXL's optical overlay and has negligible CPU impact.");
	ImGui::SliderFloat("Transition distance", &settings.MinimumDistance, 256.0f, 12000.0f, "%.0f units");
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Starts the distant representation beyond this range so it does not compete with nearby light geometry.");
	ImGui::SliderFloat("Maximum distance", &settings.MaximumDistance, settings.MinimumDistance + 1.0f, 200000.0f, "%.0f units");
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Controls optical visibility out to Skyrim's far LOD. Real references remain bounded to the loaded-world discovery radius; far-field activity can continue to 200,000 units.");
	ImGui::Checkbox("Static exterior lights", &settings.StaticLights);
	ImGui::Checkbox("Actor torches and spell lights", &settings.ActorTorches);
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Tracks loaded actor-mounted lights and active magic lights from their real runtime positions. It does not simulate unloaded actors or create gameplay lights.");
	ImGui::Checkbox("Atmospheric attenuation", &settings.AtmosphericAttenuation);
	ImGui::Checkbox("Far-field ambient activity", &settings.FarFieldActivity);
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds sparse, world-stable activity suggestions from visible distant land depth. It is exterior-only, screen-space occluded, and does not create game lights.");
	ImGui::BeginDisabled(!settings.FarFieldActivity);
	ImGui::SliderFloat("Far-field activity density", &settings.FarFieldDensity, 0.0f, 0.5f, "%.2f");
	ImGui::SliderFloat("Far-field drift", &settings.FarFieldMotion, 0.0f, 1.0f, "%.2f");
	ImGui::EndDisabled();
	if (ImGui::TreeNode("Debug / DistantLife")) {
		int debug = static_cast<int>(settings.DebugMode);
		if (ImGui::Combo("Visualisation", &debug, "Off\0Source markers\0Optical footprint\0"))
			settings.DebugMode = static_cast<std::uint32_t>(debug);
		ImGui::Text("Live registry: %zu | uploaded: %u / %u", emitters.size(), uploadedCount, kMaxEmitters);
		ImGui::TreePop();
	}
	ImGui::EndDisabled();
}
