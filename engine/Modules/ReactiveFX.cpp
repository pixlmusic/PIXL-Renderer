// PIXL Renderer - physically reactive event and VFX framework.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "ReactiveFX.h"
#include "ReactiveFX/SpawnBatch.h"

#include "Deferred.h"
#include "Globals.h"
#include "State.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ReactiveFX::Settings,
	Enabled,
	Quality,
	MaximumDistance,
	ParticleIntensity,
	VegetationResponse,
	EnableParticleCollision,
	EnableVegetationResponse,
	EnableHeroDebris,
	EnableSecondaryImpacts,
	DebugMode)

namespace
{
	constexpr std::array<std::uint32_t, 4> kParticleCaps{ 2048u, 4096u, 8192u, 16384u };
	constexpr std::array<std::uint32_t, 4> kCollisionCaps{ 256u, 768u, 2048u, 4096u };
	constexpr std::array<float, 4> kSpawnScales{ 0.40f, 0.65f, 1.0f, 1.45f };
	constexpr float kMinimumLifetime = 0.04f;

	bool Finite(const RE::NiPoint3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	float Length(const RE::NiPoint3& value)
	{
		return std::sqrt(std::max(value.x * value.x + value.y * value.y + value.z * value.z, 0.0f));
	}

	RE::NiPoint3 Normalize(const RE::NiPoint3& value, const RE::NiPoint3& fallback = { 0.0f, 0.0f, 1.0f })
	{
		const float length = Length(value);
		if (!std::isfinite(length) || length <= 1.0e-4f)
			return fallback;
		const float inverse = 1.0f / length;
		return { value.x * inverse, value.y * inverse, value.z * inverse };
	}

	std::uint32_t Hash(std::uint32_t value)
	{
		value ^= value >> 16u;
		value *= 0x7FEB352Du;
		value ^= value >> 15u;
		value *= 0x846CA68Bu;
		return value ^ (value >> 16u);
	}

	float Random01(std::uint32_t& state)
	{
		state = Hash(state + 0x9E3779B9u);
		return static_cast<float>(state & 0x00FFFFFFu) * (1.0f / 16777215.0f);
	}

	float RandomRange(std::uint32_t& state, float minimum, float maximum)
	{
		return minimum + (maximum - minimum) * Random01(state);
	}

	bool ContainsNoCase(std::string_view value, std::string_view needle)
	{
		if (needle.empty() || needle.size() > value.size()) return false;
		for (std::size_t offset = 0; offset + needle.size() <= value.size(); ++offset) {
			bool match = true;
			for (std::size_t i = 0; i < needle.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
					std::tolower(static_cast<unsigned char>(needle[i]))) { match = false; break; }
			}
			if (match) return true;
		}
		return false;
	}

	void CreateDynamicStructuredBuffer(
		std::uint32_t stride,
		std::uint32_t count,
		const char* name,
		winrt::com_ptr<ID3D11Buffer>& buffer,
		winrt::com_ptr<ID3D11ShaderResourceView>& srv)
	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = stride * count;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = stride;
		DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&desc, nullptr, buffer.put()));
		Util::SetResourceName(buffer.get(), "%s", name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = count;
		DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(buffer.get(), &srvDesc, srv.put()));
		Util::SetResourceName(srv.get(), "%s SRV", name);
	}

	ReactiveFX::SourceType WeaponSource(const RE::TESObjectWEAP* weapon)
	{
		if (!weapon)
			return ReactiveFX::SourceType::Unknown;
		switch (weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kOneHandSword:
		case RE::WEAPON_TYPE::kOneHandDagger:
		case RE::WEAPON_TYPE::kOneHandAxe:
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return ReactiveFX::SourceType::Blade;
		case RE::WEAPON_TYPE::kOneHandMace:
			return ReactiveFX::SourceType::Blunt;
		case RE::WEAPON_TYPE::kBow:
		case RE::WEAPON_TYPE::kCrossbow:
			return ReactiveFX::SourceType::Arrow;
		default:
			return ReactiveFX::SourceType::Unknown;
		}
	}
}

std::pair<std::string, std::vector<std::string>> ReactiveFX::GetModuleSummary()
{
	return {
		"Adds bounded, surface-aware reactions to existing Skyrim combat, magic and environmental events without replacing gameplay.",
		{
			"Compute-driven sparks, fragments, dust, embers and magical particles",
			"Depth/normal collision with bounce, slide and energy loss",
			"Material-aware impact recipes",
			"World-stable shout and explosion impulse response for GPU particles",
			"Strict quality, distance and event budgets"
		}
	};
}

const char* ReactiveFX::SurfaceName(SurfaceType surface)
{
	switch (surface) {
	case SurfaceType::Stone: return "Stone";
	case SurfaceType::Dirt: return "Dirt";
	case SurfaceType::Mud: return "Mud";
	case SurfaceType::Snow: return "Snow";
	case SurfaceType::Ice: return "Ice";
	case SurfaceType::Grass: return "Grass";
	case SurfaceType::Wood: return "Wood";
	case SurfaceType::Metal: return "Metal";
	case SurfaceType::Flesh: return "Flesh";
	case SurfaceType::Water: return "Water";
	case SurfaceType::Sand: return "Sand";
	case SurfaceType::Ash: return "Ash";
	default: return "Unknown";
	}
}

const char* ReactiveFX::SourceName(SourceType source)
{
	switch (source) {
	case SourceType::Footstep: return "Footstep";
	case SourceType::Blade: return "Blade";
	case SourceType::Blunt: return "Blunt";
	case SourceType::Arrow: return "Arrow";
	case SourceType::Fire: return "Fire";
	case SourceType::Frost: return "Frost";
	case SourceType::Shock: return "Shock";
	case SourceType::Restoration: return "Restoration";
	case SourceType::Conjuration: return "Conjuration";
	case SourceType::Illusion: return "Illusion";
	case SourceType::Shout: return "Shout";
	case SourceType::HeavyImpact: return "Heavy Impact";
	case SourceType::Dwemer: return "Dwemer";
	case SourceType::AmbientFire: return "Ambient Fire";
	case SourceType::AmbientCandle: return "Ambient Candle";
	default: return "Unknown";
	}
}

ReactiveFX::SurfaceType ReactiveFX::ClassifyMaterial(const RE::BGSMaterialType* material)
{
	if (!material)
		return SurfaceType::Unknown;

	switch (material->materialID) {
	case RE::MATERIAL_ID::kStoneBroken:
	case RE::MATERIAL_ID::kStoneStairs:
	case RE::MATERIAL_ID::kStoneHeavy:
	case RE::MATERIAL_ID::kBoulderSmall:
	case RE::MATERIAL_ID::kBoulderLarge:
	case RE::MATERIAL_ID::kStoneAsStairs:
	case RE::MATERIAL_ID::kStoneStairsBroken:
	case RE::MATERIAL_ID::kStone:
	case RE::MATERIAL_ID::kBoulderMedium:
		return SurfaceType::Stone;
	case RE::MATERIAL_ID::kDirt:
	case RE::MATERIAL_ID::kGravel:
		return SurfaceType::Dirt;
	case RE::MATERIAL_ID::kMud:
		return SurfaceType::Mud;
	case RE::MATERIAL_ID::kSnow:
	case RE::MATERIAL_ID::kSnowStairs:
		return SurfaceType::Snow;
	case RE::MATERIAL_ID::kIce:
	case RE::MATERIAL_ID::kIceForm:
		return SurfaceType::Ice;
	case RE::MATERIAL_ID::kGrass:
		return SurfaceType::Grass;
	case RE::MATERIAL_ID::kWoodLight:
	case RE::MATERIAL_ID::kWood:
	case RE::MATERIAL_ID::kBarrel:
	case RE::MATERIAL_ID::kWoodStairs:
	case RE::MATERIAL_ID::kWoodAsStairs:
	case RE::MATERIAL_ID::kWoodHeavy:
		return SurfaceType::Wood;
	case RE::MATERIAL_ID::kMetalLight:
	case RE::MATERIAL_ID::kChainMetal:
	case RE::MATERIAL_ID::kMetalSolid:
	case RE::MATERIAL_ID::kMetalHeavy:
	case RE::MATERIAL_ID::kPotsPans:
	case RE::MATERIAL_ID::kChain:
	case RE::MATERIAL_ID::kArmorLight:
	case RE::MATERIAL_ID::kArmorHeavy:
	case RE::MATERIAL_ID::kShieldLight:
	case RE::MATERIAL_ID::kShieldHeavy:
		return SurfaceType::Metal;
	case RE::MATERIAL_ID::kMeat:
	case RE::MATERIAL_ID::kSkin:
	case RE::MATERIAL_ID::kSkinSmall:
	case RE::MATERIAL_ID::kSkinLarge:
	case RE::MATERIAL_ID::kOrganic:
	case RE::MATERIAL_ID::kOrganicLarge:
		return SurfaceType::Flesh;
	case RE::MATERIAL_ID::kWater:
	case RE::MATERIAL_ID::kWaterPuddle:
		return SurfaceType::Water;
	case RE::MATERIAL_ID::kSand:
		return SurfaceType::Sand;
	case RE::MATERIAL_ID::kAsh:
		return SurfaceType::Ash;
	default:
		break;
	}

	const std::string_view fallback = material->materialName.c_str();
	if (ContainsNoCase(fallback, "stone") || ContainsNoCase(fallback, "rock") || ContainsNoCase(fallback, "boulder")) return SurfaceType::Stone;
	if (ContainsNoCase(fallback, "metal") || ContainsNoCase(fallback, "steel") || ContainsNoCase(fallback, "iron") || ContainsNoCase(fallback, "dwemer")) return SurfaceType::Metal;
	if (ContainsNoCase(fallback, "wood") || ContainsNoCase(fallback, "timber")) return SurfaceType::Wood;
	if (ContainsNoCase(fallback, "snow")) return SurfaceType::Snow;
	if (ContainsNoCase(fallback, "ice")) return SurfaceType::Ice;
	if (ContainsNoCase(fallback, "mud")) return SurfaceType::Mud;
	if (ContainsNoCase(fallback, "dirt")) return SurfaceType::Dirt;
	return SurfaceType::Unknown;
}

ReactiveFX::SourceType ReactiveFX::ClassifyMagic(const RE::MagicItem* magic) const
{
	if (!magic)
		return SourceType::Unknown;
	if (magic->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower)
		return SourceType::Shout;

	for (const auto* effect : magic->effects) {
		if (!effect || !effect->baseEffect)
			continue;
		const auto* base = effect->baseEffect;
		if (base->HasKeywordString("MagicDamageFire") || base->HasKeywordString("MagicFire"))
			return SourceType::Fire;
		if (base->HasKeywordString("MagicDamageFrost") || base->HasKeywordString("MagicFrost") || base->HasKeywordString("MagicIce"))
			return SourceType::Frost;
		if (base->HasKeywordString("MagicDamageShock") || base->HasKeywordString("MagicShock"))
			return SourceType::Shock;
	}

	const std::string_view identity = magic->GetFormEditorID() ? magic->GetFormEditorID() : "";
	if (ContainsNoCase(identity, "fire") || ContainsNoCase(identity, "flame") || ContainsNoCase(identity, "burn")) return SourceType::Fire;
	if (ContainsNoCase(identity, "frost") || ContainsNoCase(identity, "ice") || ContainsNoCase(identity, "freeze")) return SourceType::Frost;
	if (ContainsNoCase(identity, "shock") || ContainsNoCase(identity, "lightning") || ContainsNoCase(identity, "electric")) return SourceType::Shock;
	if (ContainsNoCase(identity, "restore") || ContainsNoCase(identity, "heal")) return SourceType::Restoration;
	if (ContainsNoCase(identity, "conjur") || ContainsNoCase(identity, "summon") || ContainsNoCase(identity, "raise")) return SourceType::Conjuration;
	if (ContainsNoCase(identity, "illusion") || ContainsNoCase(identity, "fear") || ContainsNoCase(identity, "frenzy") || ContainsNoCase(identity, "calm")) return SourceType::Illusion;
	if (ContainsNoCase(identity, "dwemer") || ContainsNoCase(identity, "dwarven") || ContainsNoCase(identity, "centurion") || ContainsNoCase(identity, "automaton")) return SourceType::Dwemer;
	return SourceType::Unknown;
}

void ReactiveFX::SetupResources()
{
	if (particles || resourceCreationAttempted || resourceRetryDelay > 0.0f || !globals::d3d::device || !globals::d3d::context)
		return;
	resourceCreationAttempted = true;

	try {
		D3D11_BUFFER_DESC particleDesc{};
		particleDesc.ByteWidth = sizeof(GPUParticle) * kMaximumParticles;
		particleDesc.Usage = D3D11_USAGE_DEFAULT;
		particleDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		particleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		particleDesc.StructureByteStride = sizeof(GPUParticle);
		particles = std::make_unique<Buffer>(particleDesc, nullptr, "ReactiveFX::Particles");

		D3D11_SHADER_RESOURCE_VIEW_DESC particleSRVDesc{};
		particleSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		particleSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		particleSRVDesc.Buffer.NumElements = kMaximumParticles;
		particles->CreateSRV(particleSRVDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC particleUAVDesc{};
		particleUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
		particleUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		particleUAVDesc.Buffer.NumElements = kMaximumParticles;
		particles->CreateUAV(particleUAVDesc);

		CreateDynamicStructuredBuffer(
			sizeof(SpawnCommand), kMaximumSpawnCommands, "ReactiveFX::SpawnCommands", spawnBuffer, spawnSRV);
		CreateDynamicStructuredBuffer(
			sizeof(GPUImpulse), kMaximumImpulses, "ReactiveFX::Impulses", impulseBuffer, impulseSRV);
		tuningCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<TuningData>(), "ReactiveFX::TuningCB");

		const UINT clear[4]{};
		globals::d3d::context->ClearUnorderedAccessViewUint(particles->uav.get(), clear);
		EnsureShaders();
		for (auto& queue : eventQueues) queue.reserve(kMaximumQueuedEvents);
		spawnCommands.reserve(kMaximumSpawnCommands);
		logger::info(
			"[ReactiveFX] GPU foundation ready: particles={} spawnBatch={} impulses={} shaderReady={}",
			kMaximumParticles,
			kMaximumSpawnCommands,
			kMaximumImpulses,
			reactivePassReady ? 1 : 0);
	} catch (const std::exception& e) {
		particles.reset();
		spawnBuffer = nullptr;
		spawnSRV = nullptr;
		impulseBuffer = nullptr;
		impulseSRV = nullptr;
		tuningCB.reset();
		reactivePassReady = false;
		resourceCreationAttempted = false;
		resourceRetryDelay = 1.0f;
		if (!resourceFailureLogged) {
			logger::error("[ReactiveFX] Resource creation failed; module disabled safely: {}", e.what());
			resourceFailureLogged = true;
		}
	}
}

bool ReactiveFX::EnsureShaders()
{
	if (reactivePassReady)
		return true;
	if (shaderCompilationAttempted)
		return false;
	shaderCompilationAttempted = true;

	spawnCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
		L"Data\\Shaders\\ReactiveFX\\ReactiveFXCS.hlsl", {}, "cs_5_0", "SpawnCS")));
	simulateCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
		L"Data\\Shaders\\ReactiveFX\\ReactiveFXCS.hlsl", {}, "cs_5_0", "SimulateCS")));
	buildMaskCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
		L"Data\\Shaders\\ReactiveFX\\ReactiveFXCS.hlsl", {}, "cs_5_0", "BuildMaskCS")));
	compositeCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
		L"Data\\Shaders\\ReactiveFX\\ReactiveFXCS.hlsl", {}, "cs_5_0", "CompositeCS")));
	reconstructionMaskCS.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
		L"Data\\Shaders\\ReactiveFX\\ReactiveFXCS.hlsl", {}, "cs_5_0", "WriteReactiveMaskCS")));
	reactivePassReady = spawnCS && simulateCS && buildMaskCS && compositeCS && reconstructionMaskCS;
	if (!reactivePassReady)
		logger::error("[ReactiveFX] One or more shaders failed; no stale particle resources will be sampled");
	return reactivePassReady;
}

void ReactiveFX::EnsureMask(std::uint32_t width, std::uint32_t height)
{
	if (particleMask && particleMask->desc.Width == width && particleMask->desc.Height == height)
		return;
	particleMask.reset();

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	particleMask = std::make_unique<Texture2D>(desc, "ReactiveFX::ParticleMask");
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	particleMask->CreateSRV(srvDesc);
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	particleMask->CreateUAV(uavDesc);
}

void ReactiveFX::EnsureSceneColorCopy(const D3D11_TEXTURE2D_DESC& sourceDesc, std::uint32_t width, std::uint32_t height)
{
	if (sourceDesc.SampleDesc.Count != 1 || width == 0 || height == 0)
		throw std::runtime_error("ReactiveFX scene copy requires a single-sample active render extent");
	if (sceneColorCopy && sceneColorCopy->desc.Width == width &&
		sceneColorCopy->desc.Height == height &&
		sceneColorCopy->desc.Format == sourceDesc.Format)
		return;

	sceneColorCopy.reset();
	D3D11_TEXTURE2D_DESC copyDesc = sourceDesc;
	copyDesc.Width = width;
	copyDesc.Height = height;
	copyDesc.MipLevels = 1;
	copyDesc.ArraySize = 1;
	copyDesc.Usage = D3D11_USAGE_DEFAULT;
	copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	copyDesc.CPUAccessFlags = 0;
	copyDesc.MiscFlags = 0;
	copyDesc.SampleDesc.Count = 1;
	copyDesc.SampleDesc.Quality = 0;
	sceneColorCopy = std::make_unique<Texture2D>(copyDesc, "ReactiveFX::SceneColorCopy");
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = copyDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	sceneColorCopy->CreateSRV(srvDesc);
}

void ReactiveFX::QueueEvent(const Event& event)
{
	if (!settings.Enabled || !Finite(event.position) || !Finite(event.direction) ||
		!std::isfinite(event.radius) || !std::isfinite(event.strength)) {
		return;
	}

	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	if (Finite({ camera.x, camera.y, camera.z })) {
		const RE::NiPoint3 delta = event.position - RE::NiPoint3{ camera.x, camera.y, camera.z };
		if (Length(delta) > std::clamp(settings.MaximumDistance, 512.0f, 50000.0f))
			return;
	}

	Event sanitized = event;
	sanitized.radius = std::clamp(sanitized.radius, 4.0f, 4096.0f);
	sanitized.strength = std::clamp(sanitized.strength, 0.02f, 8.0f);
	sanitized.direction = Normalize(sanitized.direction);
	std::scoped_lock lock(eventMutex);
	auto& queue = eventQueues[producerQueue];
	if (queue.size() >= kMaximumQueuedEvents) {
		++droppedEvents;
		return;
	}
	if (sanitized.seed == 0)
		sanitized.seed = Hash(++lastEventSeed ^ sanitized.sourceFormID);
	queue.push_back(sanitized);
}

void ReactiveFX::ApplyImpulse(
	const RE::NiPoint3& position,
	const RE::NiPoint3& direction,
	float radius,
	float strength,
	float falloff,
	ImpulseType type)
{
	Event event{};
	event.position = position;
	event.direction = direction;
	event.radius = radius;
	event.strength = strength * std::clamp(falloff, 0.0f, 1.0f);
	event.source = type == ImpulseType::TravellingWave ? SourceType::Shout : SourceType::HeavyImpact;
	event.impulse = type;
	QueueEvent(event);
}

ReactiveFX::Recipe ReactiveFX::ResolveRecipe(SourceType source, SurfaceType surface) const
{
	Recipe recipe{};
	recipe.primaryColor = { 0.72f, 0.68f, 0.58f };
	recipe.secondaryColor = { 0.32f, 0.28f, 0.22f };
	recipe.primaryCount = 12;
	recipe.secondaryCount = 8;
	recipe.speedMin = 80.0f;
	recipe.speedMax = 320.0f;
	recipe.lifetimeMin = 0.35f;
	recipe.lifetimeMax = 1.2f;
	recipe.sizeMin = 1.2f;
	recipe.sizeMax = 3.8f;
	recipe.gravityScale = 1.0f;
	recipe.drag = 1.2f;
	recipe.restitution = 0.34f;
	recipe.friction = 0.42f;
        recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
        recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
	recipe.collisionPrimary = true;
	recipe.collisionSecondary = false;

	if (source == SourceType::Footstep) {
		recipe = { 6, 4, 26.0f, 150.0f, 0.24f, 0.82f, 0.55f, 1.8f,
			{ 0.62f, 0.52f, 0.36f }, { 0.26f, 0.22f, 0.18f }, 0.42f, 2.3f,
			0.08f, 0.72f, 2, 3, false, false, true, false };
	} else if (source == SourceType::Fire) {
		recipe = { 42, 16, 110.0f, 620.0f, 0.45f, 2.2f, 0.8f, 3.4f,
			{ 1.0f, 0.22f, 0.025f }, { 0.22f, 0.16f, 0.12f }, -0.08f, 0.72f,
			0.28f, 0.25f, 6, 3, true, false, true, false };
	} else if (source == SourceType::AmbientFire) {
		recipe = { 3, 2, 70.0f, 200.0f, 0.45f, 1.45f, 0.45f, 1.3f,
			{ 1.0f, 0.30f, 0.055f }, { 0.19f, 0.17f, 0.15f }, 0.28f, 0.42f,
			0.31f, 0.38f, 6, 3, true, false, true, false };
	} else if (source == SourceType::AmbientCandle) {
		recipe = { 1, 1, 12.0f, 46.0f, 0.24f, 0.72f, 0.30f, 0.75f,
			{ 1.0f, 0.68f, 0.24f }, { 0.54f, 0.53f, 0.51f }, -0.07f, 1.7f,
			0.08f, 0.62f, 0, 3, true, false, false, false };
	} else if (source == SourceType::Frost) {
		recipe = { 38, 14, 90.0f, 480.0f, 0.55f, 1.9f, 1.0f, 5.2f,
			{ 0.40f, 0.78f, 1.0f }, { 0.72f, 0.90f, 1.0f }, 0.72f, 0.55f,
			0.52f, 0.18f, 1, 4, true, false, true, true };
	} else if (source == SourceType::Shock) {
		recipe = { 52, 8, 420.0f, 1250.0f, 0.10f, 0.62f, 0.7f, 2.6f,
			{ 0.54f, 0.72f, 1.0f }, { 0.84f, 0.93f, 1.0f }, 0.12f, 0.28f,
			0.46f, 0.12f, 0, 5, true, true, true, false };
	} else if (source == SourceType::Restoration) {
		recipe = { 20, 12, 24.0f, 110.0f, 0.8f, 2.5f, 1.4f, 4.0f,
			{ 1.0f, 0.82f, 0.32f }, { 0.62f, 1.0f, 0.74f }, -0.18f, 0.38f,
			0.0f, 0.0f, 5, 5, true, true, false, false };
	} else if (source == SourceType::Conjuration) {
		recipe = { 28, 18, 65.0f, 260.0f, 0.7f, 2.4f, 1.1f, 4.2f,
			{ 0.48f, 0.18f, 0.72f }, { 0.14f, 0.08f, 0.18f }, -0.12f, 0.48f,
			0.1f, 0.2f, 5, 3, true, false, false, false };
	} else if (source == SourceType::Illusion) {
		recipe = { 24, 12, 35.0f, 180.0f, 0.45f, 1.8f, 1.0f, 3.8f,
			{ 0.72f, 0.30f, 0.86f }, { 0.18f, 0.56f, 0.72f }, -0.2f, 0.46f,
			0.0f, 0.0f, 5, 5, true, true, false, false };
	} else if (source == SourceType::Dwemer) {
		recipe = { 34, 12, 180.0f, 820.0f, 0.18f, 1.25f, 0.8f, 3.6f,
			{ 1.0f, 0.48f, 0.10f }, { 0.50f, 0.31f, 0.12f }, 0.48f, 0.52f,
			0.44f, 0.20f, 0, 2, true, false, true, true };
	} else if (source == SourceType::Shout || source == SourceType::HeavyImpact) {
		recipe.primaryCount = source == SourceType::Shout ? 44 : 58;
		recipe.secondaryCount = 22;
		recipe.speedMin = 120.0f;
		recipe.speedMax = source == SourceType::Shout ? 720.0f : 560.0f;
		recipe.lifetimeMin = 0.5f;
		recipe.lifetimeMax = 2.2f;
		recipe.sizeMin = 1.8f;
		recipe.sizeMax = 6.5f;
	}

	const bool physicalSource =
		source == SourceType::Unknown || source == SourceType::Blade ||
		source == SourceType::Blunt || source == SourceType::Arrow ||
		source == SourceType::Shout || source == SourceType::HeavyImpact ||
		source == SourceType::Footstep;

	if (surface == SurfaceType::Metal &&
		(source == SourceType::Blade || source == SourceType::Blunt || source == SourceType::Arrow)) {
		recipe.primaryCount = 34;
		recipe.secondaryCount = 4;
		recipe.speedMin = 320.0f;
		recipe.speedMax = 1150.0f;
		recipe.lifetimeMin = 0.16f;
		recipe.lifetimeMax = 0.85f;
		recipe.sizeMin = 0.8f;
		recipe.sizeMax = 2.4f;
		recipe.primaryColor = { 1.0f, 0.62f, 0.18f };
		recipe.secondaryColor = { 0.42f, 0.46f, 0.50f };
		recipe.gravityScale = 0.55f;
		recipe.drag = 0.36f;
		recipe.restitution = 0.48f;
		recipe.friction = 0.18f;
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Spark);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
		recipe.additivePrimary = true;
		recipe.collisionPrimary = true;
		recipe.collisionSecondary = true;
	} else if (surface == SurfaceType::Stone &&
		(source == SourceType::Blade || source == SourceType::Blunt || source == SourceType::Arrow)) {
		// Stone contact keeps a restrained hot-metal fan while the secondary
		// population carries pale chips rather than making every impact pure sparks.
		recipe.primaryCount = source == SourceType::Arrow ? 8u : 14u;
		recipe.secondaryCount = source == SourceType::Arrow ? 7u : 15u;
		recipe.speedMin = 180.0f;
		recipe.speedMax = 780.0f;
		recipe.lifetimeMin = 0.16f;
		recipe.lifetimeMax = 1.1f;
		recipe.primaryColor = { 1.0f, 0.52f, 0.14f };
		recipe.secondaryColor = { 0.58f, 0.55f, 0.49f };
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Spark);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
		recipe.additivePrimary = true;
		recipe.collisionPrimary = true;
		recipe.collisionSecondary = true;
	} else if (surface == SurfaceType::Wood && physicalSource) {
		recipe.primaryColor = { 0.42f, 0.22f, 0.08f };
		recipe.secondaryColor = { 0.24f, 0.16f, 0.09f };
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
		recipe.primaryCount += 8;
		recipe.collisionPrimary = true;
	} else if ((surface == SurfaceType::Snow || surface == SurfaceType::Ice) &&
		(physicalSource || source == SourceType::Frost)) {
		recipe.primaryColor = surface == SurfaceType::Ice
			? float3{ 0.52f, 0.82f, 1.0f }
			: float3{ 0.90f, 0.95f, 1.0f };
		recipe.secondaryColor = { 0.72f, 0.82f, 0.92f };
		recipe.primaryType = static_cast<std::uint32_t>(surface == SurfaceType::Ice ? ParticleType::FrostCrystal : ParticleType::SoftPuff);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SoftPuff);
		recipe.primaryCount += 12;
		recipe.gravityScale = surface == SurfaceType::Ice ? 0.72f : 0.32f;
		recipe.drag = surface == SurfaceType::Ice ? 0.48f : 1.7f;
	} else if ((surface == SurfaceType::Dirt || surface == SurfaceType::Mud ||
			surface == SurfaceType::Grass || surface == SurfaceType::Sand || surface == SurfaceType::Ash) &&
		physicalSource) {
		if (surface == SurfaceType::Mud) {
			recipe.primaryColor = { 0.20f, 0.15f, 0.10f };
			recipe.drag = 2.2f;
			recipe.restitution = 0.08f;
		} else if (surface == SurfaceType::Grass) {
			recipe.primaryColor = { 0.24f, 0.34f, 0.10f };
			recipe.secondaryColor = { 0.38f, 0.30f, 0.12f };
		} else if (surface == SurfaceType::Sand || surface == SurfaceType::Ash) {
			recipe.primaryColor = surface == SurfaceType::Ash
				? float3{ 0.24f, 0.24f, 0.25f }
				: float3{ 0.64f, 0.52f, 0.30f };
			recipe.drag = 1.8f;
		} else {
			recipe.primaryColor = { 0.38f, 0.28f, 0.17f };
		}
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
		recipe.additivePrimary = false;
	} else if (surface == SurfaceType::Flesh) {
		// Physical contact on flesh deliberately suppresses generic sparks/chips.
		recipe.primaryCount = source == SourceType::Fire || source == SourceType::Frost || source == SourceType::Shock
			? recipe.primaryCount
			: 0u;
		recipe.secondaryCount = 0;
	} else if (surface == SurfaceType::Water) {
		if (source == SourceType::Fire) {
			// Preserve incandescent embers but turn the secondary population into
			// short-lived steam instead of recolouring the whole fire recipe blue.
			recipe.secondaryColor = { 0.62f, 0.66f, 0.68f };
			recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
			recipe.secondaryCount = std::max(recipe.secondaryCount, 28u);
			recipe.collisionPrimary = false;
		} else if (physicalSource || source == SourceType::Frost) {
			recipe.primaryColor = { 0.52f, 0.72f, 0.82f };
			recipe.secondaryColor = { 0.40f, 0.56f, 0.68f };
			recipe.primaryType = static_cast<std::uint32_t>(ParticleType::SoftPuff);
			recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
			recipe.collisionPrimary = false;
		}
	}

	// This override must run after the source-specific branch above.  Fire is
	// resolved before surface-specific branches, so expressing this as an
	// `else if (source == Fire)` there would be unreachable and would leave soft
	// surfaces with the dense airborne fire recipe.  A smaller warm core plus
	// non-additive smoke prevents overlapping particles from bleaching foliage
	// into a silver/white patch while retaining the existing impact response.
	if (source == SourceType::Fire &&
		(surface == SurfaceType::Grass || surface == SurfaceType::Dirt || surface == SurfaceType::Mud ||
			surface == SurfaceType::Snow)) {
		recipe.primaryCount = 24;
		recipe.secondaryCount = 32;
		recipe.speedMin = 70.0f;
		recipe.speedMax = 360.0f;
		recipe.lifetimeMin = 0.35f;
		recipe.lifetimeMax = 1.85f;
		recipe.sizeMin = 0.65f;
		recipe.sizeMax = 2.6f;
		recipe.primaryColor = { 1.0f, 0.16f, 0.015f };
		recipe.secondaryColor = { 0.25f, 0.22f, 0.18f };
		recipe.gravityScale = -0.10f;
		recipe.drag = 1.15f;
		recipe.restitution = 0.12f;
		recipe.friction = 0.56f;
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Spark);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::SmokeDust);
		recipe.additivePrimary = true;
		recipe.additiveSecondary = false;
		recipe.collisionPrimary = true;
		recipe.collisionSecondary = false;
	}

	// Soft-surface force events carry a restrained second population of leaves.
	// These stay entirely in the bounded GPU particle pool: they settle, collide
	// and expire without touching Skyrim's Havok ownership or scene graph.
	if ((source == SourceType::Shout || source == SourceType::HeavyImpact) &&
		(surface == SurfaceType::Dirt || surface == SurfaceType::Mud || surface == SurfaceType::Grass ||
			surface == SurfaceType::Snow || surface == SurfaceType::Ash || surface == SurfaceType::Sand)) {
		recipe.primaryCount = source == SourceType::HeavyImpact ? 42u : 20u;
		recipe.secondaryCount = source == SourceType::HeavyImpact ? 30u : 22u;
		recipe.speedMin = source == SourceType::HeavyImpact ? 90.0f : 70.0f;
		recipe.speedMax = source == SourceType::HeavyImpact ? 460.0f : 300.0f;
		recipe.lifetimeMin = 0.55f;
		recipe.lifetimeMax = source == SourceType::HeavyImpact ? 2.8f : 2.4f;
		recipe.sizeMin = 1.0f;
		recipe.sizeMax = 3.2f;
		recipe.primaryColor = surface == SurfaceType::Snow
		? float3{ 0.82f, 0.88f, 0.92f }
		: float3{ 0.34f, 0.25f, 0.14f };
	recipe.secondaryColor = surface == SurfaceType::Grass
		? float3{ 0.42f, 0.30f, 0.12f }
		: float3{ 0.28f, 0.22f, 0.12f };
	recipe.gravityScale = surface == SurfaceType::Snow ? 0.48f : 0.86f;
	recipe.drag = surface == SurfaceType::Mud ? 2.5f : 1.45f;
	recipe.restitution = 0.14f;
	recipe.friction = 0.72f;
		recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
		recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::LeafCard);
	recipe.additivePrimary = false;
	recipe.additiveSecondary = false;
	recipe.collisionPrimary = true;
	recipe.collisionSecondary = true;
	recipe.pooledDebris = true;
	}

	if ((source == SourceType::Shout || source == SourceType::HeavyImpact) &&
		surface == SurfaceType::Stone) {
		recipe.primaryCount = source == SourceType::HeavyImpact ? 52u : 24u;
		recipe.secondaryCount = source == SourceType::HeavyImpact ? 18u : 10u;
		recipe.speedMin = 150.0f;
		recipe.speedMax = source == SourceType::HeavyImpact ? 820.0f : 520.0f;
		recipe.lifetimeMin = 0.32f;
		recipe.lifetimeMax = source == SourceType::HeavyImpact ? 2.2f : 1.6f;
		recipe.sizeMin = 1.0f;
		recipe.sizeMax = 4.8f;
		recipe.primaryColor = { 0.62f, 0.57f, 0.48f };
		recipe.secondaryColor = { 0.34f, 0.31f, 0.27f };
		recipe.gravityScale = 1.05f;
		recipe.drag = 0.82f;
		recipe.restitution = 0.28f;
		recipe.friction = 0.58f;
	recipe.primaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
	recipe.secondaryType = static_cast<std::uint32_t>(ParticleType::Fragment);
	recipe.additivePrimary = false;
	recipe.additiveSecondary = false;
	recipe.collisionPrimary = true;
	recipe.collisionSecondary = true;
	recipe.pooledDebris = true;
	}

	return recipe;
}

void ReactiveFX::SpawnEvent(const Event& event, float primaryScale, float secondaryScale)
{
	const Recipe recipe = ResolveRecipe(event.source, event.surface);
	const auto quality = std::min(settings.Quality, 3u);
	const float qualityScale = kSpawnScales[quality];
	const std::uint32_t capacity = kParticleCaps[quality];
	const std::uint32_t debrisCapacity = std::min(kHeroParticleReserve, capacity / 4u);
	const std::uint32_t particleCapacity = capacity - debrisCapacity;
	const std::uint32_t primaryCount = static_cast<std::uint32_t>(std::round(recipe.primaryCount * qualityScale * std::clamp(primaryScale, 0.0f, 1.0f)));
	const std::uint32_t secondaryCount = static_cast<std::uint32_t>(std::round(recipe.secondaryCount * qualityScale * std::clamp(secondaryScale, 0.0f, 1.0f)));
	const RE::NiPoint3 direction = Normalize(event.direction);
	const RE::NiPoint3 tangent = Normalize(
		std::abs(direction.z) < 0.88f ? RE::NiPoint3{ -direction.y, direction.x, 0.0f } : RE::NiPoint3{ 1.0f, 0.0f, 0.0f });
	const RE::NiPoint3 bitangent{
		direction.y * tangent.z - direction.z * tangent.y,
		direction.z * tangent.x - direction.x * tangent.z,
		direction.x * tangent.y - direction.y * tangent.x
	};

	auto spawnPopulation = [&](std::uint32_t count, bool secondary) {
		// Fire uses type 0 for incandescent particles and type 3 for smoke. Keep
		// the smoke close to each spark's launch path so it reads as a short-lived
		// black trail instead of a separate pale cloud. Metal/metal recipes use
		// type 2 for their secondary fragments and are deliberately unaffected.
		const bool fireSmokeTrail = secondary &&
			(recipe.primaryType == static_cast<std::uint32_t>(ParticleType::Spark) ||
				recipe.primaryType == static_cast<std::uint32_t>(ParticleType::HotStreak)) &&
			recipe.secondaryType == static_cast<std::uint32_t>(ParticleType::SmokeDust) && recipe.additivePrimary;
		for (std::uint32_t i = 0; i < count; ++i) {
			if (spawnCommands.size() >= kMaximumSpawnCommands) {
				droppedParticles += count - i;
				break;
			}
			std::uint32_t random = Hash(event.seed + i * 747796405u + (secondary ? 0xA511E9B3u : 0u));
			const float angle = Random01(random) * 6.283185307f;
			const float radial = std::sqrt(Random01(random));
			const float spread = fireSmokeTrail ? 0.34f : (secondary ? 0.88f : 0.62f);
			const float lateralX = std::cos(angle) * radial * spread;
			const float lateralY = std::sin(angle) * radial * spread;
			RE::NiPoint3 launch{
				direction.x + tangent.x * lateralX + bitangent.x * lateralY,
				direction.y + tangent.y * lateralX + bitangent.y * lateralY,
				direction.z + tangent.z * lateralX + bitangent.z * lateralY
			};
			launch = Normalize(launch);
			if (recipe.pooledDebris) {
				// A shout/impact launches loose material upward before gravity takes
				// over. Without this lift, the depth-aware mask can hide the entire
				// short-lived population inside the receiving surface.
				launch.z = std::max(launch.z, 0.34f + Random01(random) * 0.42f);
				launch = Normalize(launch);
			}
			const float speed = RandomRange(random, recipe.speedMin, recipe.speedMax) * event.strength *
				(fireSmokeTrail ? 0.13f : (secondary ? (recipe.pooledDebris ? 0.68f : 0.42f) : 1.0f));
			const float spawnRadius = std::min(event.radius * 0.10f, 18.0f) * radial;

			SpawnCommand command{};
			// Keep impact debris in a separate fixed ring so a large burst cannot
			// evict active sparks, fire cores or smoke trails.
			if (recipe.pooledDebris && debrisCapacity > 0u)
				command.slot = nextDebrisSlot++ % debrisCapacity;
			else
				command.slot = debrisCapacity + (nextParticleSlot++ % std::max(particleCapacity, 1u));
			auto& particle = command.particle;
			particle.position = {
				event.position.x + tangent.x * std::cos(angle) * spawnRadius + bitangent.x * std::sin(angle) * spawnRadius,
				event.position.y + tangent.y * std::cos(angle) * spawnRadius + bitangent.y * std::sin(angle) * spawnRadius,
				event.position.z + tangent.z * std::cos(angle) * spawnRadius + bitangent.z * std::sin(angle) * spawnRadius
			};
			particle.velocity = { launch.x * speed, launch.y * speed, launch.z * speed };
			particle.acceleration = { 0.0f, 0.0f, -980.0f * recipe.gravityScale };
			particle.age = 0.0f;
			particle.lifetime = std::max(RandomRange(random, recipe.lifetimeMin, recipe.lifetimeMax), kMinimumLifetime);
			particle.drag = std::max(recipe.drag, 0.0f);
			const float3 color = secondary ? recipe.secondaryColor : recipe.primaryColor;
			particle.colorEmission = { color.x, color.y, color.z,
				secondary ? (fireSmokeTrail ? 0.24f : 0.65f) : 1.0f };
			particle.size = RandomRange(random, recipe.sizeMin, recipe.sizeMax) *
				(fireSmokeTrail ? 0.42f : (secondary ? (recipe.pooledDebris ? 1.65f : 1.35f) : 1.0f));
			particle.rotation = Random01(random) * 6.283185307f;
			particle.angularVelocity = RandomRange(random, -8.0f, 8.0f);
			particle.restitution = recipe.restitution;
			particle.friction = recipe.friction;
			particle.collisionThickness = std::clamp(particle.size * 2.0f, 2.0f, 18.0f);
				particle.type = secondary ? recipe.secondaryType : recipe.primaryType;
			const bool additive = secondary ? recipe.additiveSecondary : recipe.additivePrimary;
			const bool collision = secondary ? recipe.collisionSecondary : recipe.collisionPrimary;
			particle.flags = (additive ? 1u : 0u) | (collision ? 2u : 0u);
			// GPU debris is a reusable fixed-slot pool. Short-lived impact pieces get
			// a small bounce budget and are retired by SimulateCS when their lifetime
			// or camera-distance bound is reached; no CPU object is created per hit.
			particle.maxBounces = collision ? (secondary ? 2u : 4u) : 0u;
			particle.fadeIn = std::min(0.08f, particle.lifetime * 0.2f);
			particle.fadeOut = std::max(0.12f, particle.lifetime * 0.35f);
			spawnCommands.push_back(command);
		}
	};

	spawnPopulation(primaryCount, false);
	spawnPopulation(secondaryCount, true);
	simulationTimeRemaining = std::max(simulationTimeRemaining, recipe.lifetimeMax + 0.25f);
	if (event.source == SourceType::Fire || event.source == SourceType::Frost)
		opticalTimeRemaining = std::max(opticalTimeRemaining, recipe.lifetimeMax + 0.1f);
	lastSurface = event.surface;
	lastSource = event.source;
}

void ReactiveFX::AddImpulseForEvent(const Event& event)
{
	if (event.source != SourceType::Shout && event.source != SourceType::HeavyImpact &&
		event.source != SourceType::Fire && event.source != SourceType::Frost && event.source != SourceType::Shock) {
		return;
	}

	std::size_t selected = kMaximumImpulses;
	float leastUseful = std::numeric_limits<float>::max();
	for (std::size_t i = 0; i < impulses.size(); ++i) {
		const auto& candidate = impulses[i].gpu;
		if (candidate.active == 0u) {
			selected = i;
			break;
		}
		const float remaining = std::max(candidate.duration - candidate.age, 0.0f);
		const float usefulness = remaining * std::max(candidate.strength, 0.01f);
		if (usefulness < leastUseful) {
			leastUseful = usefulness;
			selected = i;
		}
	}
	if (selected >= impulses.size())
		return;
	auto& impulse = impulses[selected].gpu;
	impulse = {};
	impulse.position = { event.position.x, event.position.y, event.position.z };
	impulse.radius = std::clamp(event.radius, 48.0f, 4096.0f);
	const auto direction = Normalize(event.direction, { 1.0f, 0.0f, 0.0f });
	impulse.direction = { direction.x, direction.y, direction.z };
	impulse.strength = std::clamp(event.strength, 0.05f, 4.0f);
	const bool shoutBurst = event.source == SourceType::Shout && event.impulse != ImpulseType::TravellingWave;
	impulse.duration = shoutBurst ? 0.55f : (event.impulse == ImpulseType::TravellingWave ? 2.8f : 1.6f);
	impulse.waveSpeed = shoutBurst ? 0.0f : (event.impulse == ImpulseType::TravellingWave
		? std::clamp(event.radius / 1.15f, 220.0f, 1800.0f)
		: 0.0f);
	impulse.type = static_cast<std::uint32_t>(event.impulse);
	impulse.active = 1u;
	impulse.falloff = shoutBurst ? 1.1f : (event.impulse == ImpulseType::TravellingWave ? 1.6f : 2.2f);
	impulse.verticalInfluence = event.source == SourceType::HeavyImpact ? 0.24f : 0.08f;
	simulationTimeRemaining = std::max(simulationTimeRemaining, impulse.duration + 0.12f);
}

void ReactiveFX::ProcessQueuedEvents()
{
	std::vector<Event>* events = nullptr;
	{
		std::scoped_lock lock(eventMutex);
		producerQueue = 1u - producerQueue;
		events = &eventQueues[1u - producerQueue];
	}
	if (!events || events->empty())
		return;

	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	const RE::NiPoint3 cameraPosition{ camera.x, camera.y, camera.z };
	auto importance = [&](const Event& event) {
		const float distance = Finite(cameraPosition) ? Length(event.position - cameraPosition) : 0.0f;
		const float relevance = 1.0f / (1.0f + distance * 0.0025f);
		const float sourceWeight = event.source == SourceType::AmbientFire || event.source == SourceType::AmbientCandle ? 0.30f :
			event.source == SourceType::HeavyImpact || event.source == SourceType::Shout ? 1.35f :
			event.source == SourceType::Fire || event.source == SourceType::Frost || event.source == SourceType::Shock ? 1.2f : 1.0f;
		return event.strength * std::sqrt(std::max(event.radius, 1.0f)) * sourceWeight * relevance;
	};
	std::stable_sort(events->begin(), events->end(), [&](const Event& left, const Event& right) {
		return importance(left) > importance(right);
	});
	const std::size_t eventBudget = std::array<std::size_t, 4>{ 8, 16, 32, 48 }[std::min(settings.Quality, 3u)];
	if (events->size() > eventBudget) {
		droppedEvents += static_cast<std::uint32_t>(events->size() - eventBudget);
		events->resize(eventBudget);
	}

	const float qualityScale = kSpawnScales[std::min(settings.Quality, 3u)];
	std::array<std::uint32_t, 48> primary{};
	std::array<std::uint32_t, 48> secondary{};
	std::uint32_t totalPrimary = 0;
	std::uint32_t totalSecondary = 0;
	for (std::size_t i = 0; i < events->size(); ++i) {
		const auto recipe = ResolveRecipe((*events)[i].source, (*events)[i].surface);
		primary[i] = static_cast<std::uint32_t>(std::round(recipe.primaryCount * qualityScale));
		secondary[i] = static_cast<std::uint32_t>(std::round(recipe.secondaryCount * qualityScale));
		totalPrimary += primary[i];
		totalSecondary += secondary[i];
	}
	const std::uint32_t primaryBudget = std::min<std::uint32_t>(kMaximumSpawnCommands, totalPrimary);
	const std::uint32_t secondaryBudget = std::min<std::uint32_t>(kMaximumSpawnCommands - primaryBudget, totalSecondary);
	std::uint32_t primaryRemaining = primaryBudget;
	std::uint32_t secondaryRemaining = secondaryBudget;
	std::uint32_t totalPrimaryRemaining = totalPrimary;
	std::uint32_t totalSecondaryRemaining = totalSecondary;
	for (std::size_t i = 0; i < events->size(); ++i) {
		const std::uint32_t primaryQuota = totalPrimaryRemaining ? std::min(primary[i], (primaryRemaining + totalPrimaryRemaining - 1u) / totalPrimaryRemaining) : 0u;
		const std::uint32_t secondaryQuota = totalSecondaryRemaining ? std::min(secondary[i], (secondaryRemaining + totalSecondaryRemaining - 1u) / totalSecondaryRemaining) : 0u;
		primaryRemaining -= std::min(primaryRemaining, primaryQuota);
		secondaryRemaining -= std::min(secondaryRemaining, secondaryQuota);
		totalPrimaryRemaining -= primary[i];
		totalSecondaryRemaining -= secondary[i];
		SpawnEvent((*events)[i], primary[i] ? static_cast<float>(primaryQuota) / primary[i] : 0.0f,
			secondary[i] ? static_cast<float>(secondaryQuota) / secondary[i] : 0.0f);
		AddImpulseForEvent((*events)[i]);
	}
	events->clear();
}

void ReactiveFX::UploadSpawnCommands()
{
	if (!spawnBuffer || spawnCommands.empty())
		return;
	ReactiveFXSafety::CompactSpawnBatch<kMaximumParticles>(spawnCommands);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	const HRESULT result = globals::d3d::context->Map(spawnBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result)) {
		// Never dispatch with the previous upload after a failed WRITE_DISCARD.
		spawnCommands.clear();
		static std::once_flag warning;
		std::call_once(warning, [result]() { logger::warn("[ReactiveFX] Spawn upload failed ({:08X}); burst skipped", static_cast<unsigned>(result)); });
		return;
	}
	std::memcpy(mapped.pData, spawnCommands.data(), spawnCommands.size() * sizeof(SpawnCommand));
	globals::d3d::context->Unmap(spawnBuffer.get(), 0);
}

void ReactiveFX::UploadImpulses(float deltaTime)
{
	if (!impulseBuffer)
		return;
	for (auto& active : impulses) {
		auto& impulse = active.gpu;
		impulse.previousAge = impulse.age;
		if (impulse.active != 0u) {
			impulse.age += deltaTime;
			if (!std::isfinite(impulse.age) || impulse.age >= impulse.duration)
				impulse.active = 0u;
		}
	}

	std::array<GPUImpulse, kMaximumImpulses> upload{};
	activeImpulseCount = 0;
	for (const auto& active : impulses) {
		if (active.gpu.active != 0u && activeImpulseCount < kMaximumImpulses)
			upload[activeImpulseCount++] = active.gpu;
	}
	D3D11_MAPPED_SUBRESOURCE mapped{};
	const HRESULT result = globals::d3d::context->Map(impulseBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(result)) {
		// Stale impulses must not be consumed after an upload failure.
		simulationTimeRemaining = 0.0f;
		static std::once_flag warning;
		std::call_once(warning, [result]() { logger::warn("[ReactiveFX] Impulse upload failed ({:08X}); simulation skipped", static_cast<unsigned>(result)); });
		return;
	}
	std::memcpy(mapped.pData, upload.data(), sizeof(upload));
	globals::d3d::context->Unmap(impulseBuffer.get(), 0);
}

void ReactiveFX::QueueAmbientEmitters(float deltaTime)
{
	if (deltaTime <= 0.0f || (settings.ParticleIntensity <= 0.01f && settings.DebugMode == 0))
		return;
	ambientScanCountdown -= deltaTime;
	if (ambientScanCountdown > 0.0f)
		return;
	ambientScanCountdown = 0.85f;
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* tes = RE::TES::GetSingleton();
	if (!tes || !player || !player->GetParentCell() || !player->Get3D(false))
		return;

	struct Candidate
	{
		RE::NiPoint3 position{};
		std::uint32_t formID{};
		SourceType source{ SourceType::Unknown };
		float distance{ std::numeric_limits<float>::max() };
	};
	std::array<Candidate, 6> nearest{};
	const auto camera = globals::game::frameBufferCached.GetCameraPosAdjust();
	const RE::NiPoint3 eye = Finite({ camera.x, camera.y, camera.z })
		? RE::NiPoint3{ camera.x, camera.y, camera.z } : player->GetPosition();
	tes->ForEachReferenceInRange(player, 1100.0f, [&](RE::TESObjectREFR* reference) {
		if (!reference || reference->IsDisabled() || !reference->Get3D(false))
			return RE::BSContainer::ForEachResult::kContinue;
		auto* base = reference->GetBaseObject();
		auto* light = base ? base->As<RE::TESObjectLIGH>() : nullptr;
		if (!light || light->data.flags.any(RE::TES_LIGHT_FLAGS::kNegative, RE::TES_LIGHT_FLAGS::kOffByDefault))
			return RE::BSContainer::ForEachResult::kContinue;
		const std::string_view model = light->GetModel() ? light->GetModel() : "";
		const std::string_view name = light->GetFullName() ? light->GetFullName() : "";
		const bool candle = ContainsNoCase(model, "candle") || ContainsNoCase(name, "candle");
		const bool fire = ContainsNoCase(model, "torch") || ContainsNoCase(model, "brazier") ||
			ContainsNoCase(model, "campfire") || ContainsNoCase(name, "torch") ||
			ContainsNoCase(name, "brazier") || ContainsNoCase(name, "campfire");
		if (!candle && !fire)
			return RE::BSContainer::ForEachResult::kContinue;
		const auto position = reference->GetPosition();
		const float distance = Length(position - eye);
		if (!Finite(position) || !std::isfinite(distance) || distance > 950.0f)
			return RE::BSContainer::ForEachResult::kContinue;
		auto farthest = std::max_element(nearest.begin(), nearest.end(), [](const Candidate& a, const Candidate& b) {
			return a.distance < b.distance;
		});
		if (distance < farthest->distance)
			*farthest = { position, reference->GetFormID(), candle ? SourceType::AmbientCandle : SourceType::AmbientFire, distance };
		return RE::BSContainer::ForEachResult::kContinue;
	});
	for (const auto& candidate : nearest) {
		if (candidate.source == SourceType::Unknown)
			continue;
		Event event{};
		event.position = candidate.position + RE::NiPoint3{ 0.0f, 0.0f,
			candidate.source == SourceType::AmbientCandle ? 9.0f : 24.0f };
		event.direction = { 0.0f, 0.0f, 1.0f };
		event.radius = 16.0f;
		event.strength = 0.55f;
		event.source = candidate.source;
		event.sourceFormID = candidate.formID;
		QueueEvent(event);
	}
}

void ReactiveFX::Prepass()
{
	const float deltaTime = globals::game::deltaTime && std::isfinite(*globals::game::deltaTime)
		? std::clamp(*globals::game::deltaTime, 0.0f, 1.0f / 15.0f)
		: 1.0f / 60.0f;
	resourceRetryDelay = std::max(resourceRetryDelay - deltaTime, 0.0f);
	opticalTimeRemaining = std::max(opticalTimeRemaining - deltaTime, 0.0f);
	if (!settings.Enabled) {
		if (wasEnabled) {
			if (particles && particles->uav && globals::d3d::context) {
				const UINT clear[4]{};
				globals::d3d::context->ClearUnorderedAccessViewUint(particles->uav.get(), clear);
			}
			for (auto& impulse : impulses)
				impulse.gpu = {};
			spawnCommands.clear();
			nextParticleSlot = 0;
			nextDebrisSlot = 0;
			{
				std::scoped_lock lock(eventMutex);
				for (auto& queue : eventQueues)
					queue.clear();
			}
			simulationTimeRemaining = 0.0f;
			opticalTimeRemaining = 0.0f;
			activeImpulseCount = 0;
			particleMaskFrame = ~0u;
			activeQuality = ~0u;
			ambientScanCountdown = 0.0f;
			wasEnabled = false;
		}
		return;
	}
	wasEnabled = true;
	const auto quality = std::min(settings.Quality, 3u);
	if (activeQuality != quality) {
		if (particles && particles->uav && globals::d3d::context) {
			const UINT clear[4]{};
			globals::d3d::context->ClearUnorderedAccessViewUint(particles->uav.get(), clear);
		}
		for (auto& impulse : impulses)
			impulse.gpu = {};
		activeImpulseCount = 0;
		nextParticleSlot = 0;
		nextDebrisSlot = 0;
		particleMaskFrame = ~0u;
		std::scoped_lock lock(eventMutex);
		for (auto& queue : eventQueues)
			queue.clear();
		activeQuality = quality;
	}
	if (!particles)
		SetupResources();
	if (!particles || (!reactivePassReady && !EnsureShaders()))
		return;

	spawnCommands.clear();
	QueueAmbientEmitters(deltaTime);
	ProcessQueuedEvents();
	footstepCooldown = std::max(footstepCooldown - deltaTime, 0.0f);
	simulationTimeRemaining = std::max(simulationTimeRemaining - deltaTime, 0.0f);
	try {
		UploadSpawnCommands();
		UploadImpulses(deltaTime);
	} catch (const std::exception& e) {
		spawnCommands.clear();
		activeImpulseCount = 0;
		if (!resourceFailureLogged) {
			logger::error("[ReactiveFX] Per-frame GPU upload failed; skipping unsafe uploads: {}", e.what());
			resourceFailureLogged = true;
		}
	}
}

void ReactiveFX::DrawReactiveFX()
{
	if (!settings.Enabled || simulationTimeRemaining <= 0.0f || !particles ||
		!reactivePassReady || !tuningCB || !globals::game::renderer)
		return;

	auto* context = globals::d3d::context;
	auto* state = globals::state;
	auto* renderer = globals::game::renderer;
	if (!context || !state || !state->sharedDataCB || !globals::deferred ||
		!globals::game::perFrame.get() || !*globals::game::perFrame)
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[0]];
	auto& normal = renderer->GetRuntimeData().renderTargets[NORMALROUGHNESS];
	ID3D11ShaderResourceView* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	if (!main.texture || !main.UAV || !depthSRV)
		return;
	const bool collisionEnabled = settings.EnableParticleCollision && normal.SRV;
	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);
	if (!mainDesc.Width || !mainDesc.Height)
		return;
	const auto active = Util::ConvertToDynamic(
		{ static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) }, true);
	if (!std::isfinite(active.x) || !std::isfinite(active.y) || active.x <= 0.0f || active.y <= 0.0f)
		return;
	const auto width = std::clamp(static_cast<std::uint32_t>(active.x), 1u, mainDesc.Width);
	const auto height = std::clamp(static_cast<std::uint32_t>(active.y), 1u, mainDesc.Height);
	try {
		EnsureMask(width, height);
		if (opticalTimeRemaining > 0.0f)
			EnsureSceneColorCopy(mainDesc, width, height);
	} catch (const std::exception& error) {
		particleMaskFrame = ~0u;
		if (!resourceFailureLogged) {
			logger::error("[ReactiveFX] Optional frame resource unavailable; continuing without optical effects: {}", error.what());
			resourceFailureLogged = true;
		}
	}
	if (!particleMask || !particleMask->srv || !particleMask->uav)
		return;

	const auto quality = std::min(settings.Quality, 3u);
	const std::uint32_t particleCapacity = kParticleCaps[quality];
	const float deltaTime = globals::game::deltaTime && std::isfinite(*globals::game::deltaTime)
		? std::clamp(*globals::game::deltaTime, 0.0f, 1.0f / 15.0f)
		: 1.0f / 60.0f;
	TuningData tuning{};
	tuning.renderSize = { static_cast<float>(width), static_cast<float>(height) };
	tuning.invRenderSize = { 1.0f / width, 1.0f / height };
	tuning.deltaTime = deltaTime;
	tuning.gravity = 980.0f;
	tuning.particleIntensity = std::clamp(settings.ParticleIntensity, 0.0f, 3.0f);
	tuning.maximumDistance = std::clamp(settings.MaximumDistance, 512.0f, 50000.0f);
	tuning.particleCapacity = particleCapacity;
	tuning.spawnCount = std::min<std::uint32_t>(static_cast<std::uint32_t>(spawnCommands.size()), kMaximumSpawnCommands);
	tuning.activeImpulseCount = activeImpulseCount;
	tuning.collisionPhase = collisionPhase++;
	tuning.collisionBudget = collisionEnabled ? kCollisionCaps[quality] : 0u;
	tuning.debugMode = std::min(settings.DebugMode, 4u);
	tuning.collisionEnabled = collisionEnabled ? 1.0f : 0.0f;
	tuning.opticalActive = opticalTimeRemaining > 0.0f && sceneColorCopy && sceneColorCopy->srv ? 1.0f : 0.0f;
	if (tuning.particleIntensity <= 0.0001f && tuning.debugMode == 0u) {
		particleMaskFrame = ~0u;
		return;
	}
	try {
		tuningCB->Update(tuning);
	} catch (const std::exception& error) {
		static std::once_flag warning;
		std::call_once(warning, [&error]() { logger::warn("[ReactiveFX] Tuning upload unavailable; frame skipped: {}", error.what()); });
		return;
	}

	ID3D11Buffer* constantBuffers[3]{ state->sharedDataCB->CB(), *globals::game::perFrame.get(), tuningCB->CB() };
	context->CSSetConstantBuffers(5, 1, &constantBuffers[0]);
	context->CSSetConstantBuffers(12, 2, &constantBuffers[1]);

	if (!spawnCommands.empty()) {
		globals::profiler->BeginPass("ReactiveFX::Spawn");
		ID3D11ShaderResourceView* spawnView = spawnSRV.get();
		ID3D11UnorderedAccessView* particleUAV = particles->uav.get();
		context->CSSetShaderResources(0, 1, &spawnView);
		context->CSSetUnorderedAccessViews(0, 1, &particleUAV, nullptr);
		context->CSSetShader(spawnCS.get(), nullptr, 0);
		context->Dispatch((tuning.spawnCount + 63u) / 64u, 1, 1);
		globals::profiler->EndPass();
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		spawnCommands.clear();
	}

	globals::profiler->BeginPass("ReactiveFX::Simulate");
	ID3D11ShaderResourceView* simulateViews[3]{ depthSRV, collisionEnabled ? normal.SRV : nullptr, impulseSRV.get() };
	ID3D11UnorderedAccessView* particleUAV = particles->uav.get();
	context->CSSetShaderResources(0, 3, simulateViews);
	context->CSSetUnorderedAccessViews(0, 1, &particleUAV, nullptr);
	context->CSSetShader(simulateCS.get(), nullptr, 0);
	context->Dispatch((particleCapacity + 63u) / 64u, 1, 1);
	globals::profiler->EndPass();
	ID3D11ShaderResourceView* nullViews[3]{};
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 3, nullViews);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	const UINT clear[4]{};
	context->ClearUnorderedAccessViewUint(particleMask->uav.get(), clear);
	globals::profiler->BeginPass("ReactiveFX::Raster");
	ID3D11ShaderResourceView* rasterViews[2]{ particles->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* maskUAV = particleMask->uav.get();
	context->CSSetShaderResources(0, 2, rasterViews);
	context->CSSetUnorderedAccessViews(0, 1, &maskUAV, nullptr);
	context->CSSetShader(buildMaskCS.get(), nullptr, 0);
	context->Dispatch((particleCapacity + 63u) / 64u, 1, 1);
	globals::profiler->EndPass();
	particleMaskFrame = globals::state ? globals::state->frameCount : ~0u;
	context->CSSetShaderResources(0, 2, nullViews);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	globals::profiler->BeginPass("ReactiveFX::Composite");
	// Keep the source immutable while the composite writes the destination UAV.
	// Copy only the active extent; small render targets must not pay for unused
	// backing-texture pixels. Restore the exact OM set after the copy.
	if (tuning.opticalActive > 0.5f) {
		ID3D11RenderTargetView* savedRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* savedDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, &savedDSV);
		context->OMSetRenderTargets(0, nullptr, nullptr);
		D3D11_BOX sourceBox{ 0, 0, 0, width, height, 1 };
		context->CopySubresourceRegion(sceneColorCopy->resource.get(), 0, 0, 0, 0, main.texture, 0, &sourceBox);
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs, savedDSV);
		for (auto*& view : savedRTVs)
			if (view) view->Release();
		if (savedDSV) savedDSV->Release();
	}
	ID3D11ShaderResourceView* compositeViews[4]{ particleMask->srv.get(), depthSRV, impulseSRV.get(), sceneColorCopy ? sceneColorCopy->srv.get() : nullptr };
	ID3D11UnorderedAccessView* mainUAV = main.UAV;
	context->CSSetShaderResources(0, 4, compositeViews);
	context->CSSetUnorderedAccessViews(0, 1, &mainUAV, nullptr);
	context->CSSetShader(compositeCS.get(), nullptr, 0);
	context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
	globals::profiler->EndPass();

	ID3D11Buffer* nullBuffers[2]{};
	ID3D11ShaderResourceView* nullCompositeViews[4]{};
	context->CSSetShaderResources(0, 4, nullCompositeViews);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetConstantBuffers(5, 1, nullBuffers);
	context->CSSetConstantBuffers(12, 2, nullBuffers);
	context->CSSetShader(nullptr, nullptr, 0);
}

void ReactiveFX::MarkReconstructionReactive(
	ID3D11UnorderedAccessView* target,
	std::uint32_t width,
	std::uint32_t height)
{
	if (!target || !width || !height || !settings.Enabled || !reactivePassReady ||
		!reconstructionMaskCS || !particleMask || !particleMask->srv || !globals::state ||
		particleMaskFrame != globals::state->frameCount || !globals::d3d::context ||
		!globals::game::renderer) {
		return;
	}

	auto* context = globals::d3d::context;
	ID3D11ShaderResourceView* source = particleMask->srv.get();
	context->CSSetShaderResources(0, 1, &source);
	context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
	context->CSSetShader(reconstructionMaskCS.get(), nullptr, 0);
	context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;
	context->CSSetShaderResources(0, 1, &nullSRV);
	context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void ReactiveFX::QueueProjectileImpact(
	RE::Projectile* projectile,
	const RE::NiPoint3& position,
	const RE::NiPoint3& velocity,
	const RE::BGSMaterialType* material,
	RE::TESObjectREFR* target)
{
	if (!settings.Enabled || !projectile || !Finite(position) || !Finite(velocity))
		return;
	const auto& runtime = projectile->GetProjectileRuntimeData();
	Event event{};
	event.position = position;
	event.direction = Normalize(velocity);
	event.strength = std::clamp(Length(velocity) / 900.0f, 0.25f, 2.4f);
	event.radius = 36.0f + event.strength * 24.0f;
	event.surface = material ? ClassifyMaterial(material) : (target && target->As<RE::Actor>() ? SurfaceType::Flesh : SurfaceType::Unknown);
	event.source = ClassifyMagic(runtime.spell);
	if (event.source == SourceType::Unknown)
		event.source = projectile->formType == RE::FormType::ProjectileArrow ? SourceType::Arrow : SourceType::Unknown;
	event.sourceFormID = runtime.spell ? runtime.spell->GetFormID() : projectile->GetFormID();
	if (event.source == SourceType::Fire || event.source == SourceType::Frost || event.source == SourceType::Shock) {
		event.radius *= 1.8f;
		event.impulse = ImpulseType::Radial;
	}
	QueueEvent(event);
}

void ReactiveFX::QueueFootstep(
	const RE::NiPoint3& position,
	const RE::NiPoint3& velocity,
	SurfaceType surface,
	float intensity)
{
	if (!settings.Enabled || footstepCooldown > 0.0f || !Finite(position) || !Finite(velocity))
		return;
	const float speed = Length(velocity);
	if (!std::isfinite(speed) || speed < 42.0f)
		return;
	Event event{};
	event.position = position;
	event.direction = Normalize(velocity, { 0.0f, 0.0f, 1.0f });
	event.radius = std::clamp(8.0f + speed * 0.035f, 10.0f, 30.0f);
	event.strength = std::clamp(intensity * (0.35f + speed / 260.0f), 0.20f, 1.15f);
	event.surface = surface;
	event.source = SourceType::Footstep;
	QueueEvent(event);
	footstepCooldown = 0.075f;
}

ReactiveFX::HitEventSink* ReactiveFX::HitEventSink::GetSingleton()
{
	static HitEventSink singleton;
	return &singleton;
}

RE::BSEventNotifyControl ReactiveFX::HitEventSink::ProcessEvent(
	const RE::TESHitEvent* event,
	RE::BSTEventSource<RE::TESHitEvent>*)
{
	if (!event || !event->target || event->projectile != 0)
		return RE::BSEventNotifyControl::kContinue;

	auto& reactive = globals::pipeline::reactiveFX;
	auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(event->source);
	const SourceType source = WeaponSource(weapon);
	if (source == SourceType::Unknown)
		return RE::BSEventNotifyControl::kContinue;

	Event effect{};
	effect.position = event->target->GetPosition();
	if (auto* actor = event->target->As<RE::Actor>()) {
		// TESHitEvent exposes a target reference, not a reliable contact point.
		// Keep the approximation inside the actor's body rather than spawning all
		// melee sparks at the feet/reference origin.
		effect.position.z += std::clamp(actor->GetHeight() * 0.5f, 24.0f, 72.0f);
	}
	if (event->cause)
		effect.direction = Normalize(effect.position - event->cause->GetPosition());
	else
		effect.direction = { 0.0f, 0.0f, 1.0f };
	effect.source = source;
	effect.surface = event->target->As<RE::Actor>() ? SurfaceType::Flesh : SurfaceType::Unknown;
	effect.sourceFormID = event->source;
	effect.strength = event->flags.any(RE::TESHitEvent::Flag::kPowerAttack) ? 1.65f : 0.75f;
	if (event->flags.any(RE::TESHitEvent::Flag::kBashAttack, RE::TESHitEvent::Flag::kHitBlocked)) {
		effect.surface = SurfaceType::Metal;
		effect.strength = std::max(effect.strength, 1.1f);
	}
	if (event->flags.any(RE::TESHitEvent::Flag::kPowerAttack) &&
		(source == SourceType::Blunt || (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandAxe))) {
		effect.source = SourceType::HeavyImpact;
		effect.radius = 180.0f;
		effect.impulse = ImpulseType::Radial;
	}
	reactive.QueueEvent(effect);
	return RE::BSEventNotifyControl::kContinue;
}

ReactiveFX::SpellCastEventSink* ReactiveFX::SpellCastEventSink::GetSingleton()
{
	static SpellCastEventSink singleton;
	return &singleton;
}

RE::BSEventNotifyControl ReactiveFX::SpellCastEventSink::ProcessEvent(
	const RE::TESSpellCastEvent* event,
	RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
	if (!event || !event->object)
		return RE::BSEventNotifyControl::kContinue;
	auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(event->spell);
	if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kVoicePower)
		return RE::BSEventNotifyControl::kContinue;

	auto* caster = event->object.get();
	const float angle = caster->GetAngleZ();
	const RE::NiPoint3 direction{ std::sin(angle), std::cos(angle), 0.04f };
	Event effect{};
	effect.position = caster->GetPosition() + direction * 72.0f;
	effect.direction = direction;
	effect.radius = 320.0f;
	effect.strength = 1.25f;
	effect.source = SourceType::Shout;
	effect.surface = SurfaceType::Unknown;
	// A shout is a gameplay event, not a persistent radial scan. Keep the
	// response as a short directional burst that disturbs nearby loose material.
	effect.impulse = ImpulseType::Directional;
	effect.sourceFormID = event->spell;
	globals::pipeline::reactiveFX.QueueEvent(effect);
	return RE::BSEventNotifyControl::kContinue;
}

void ReactiveFX::DataLoaded()
{
	if (!eventSinksRegistered) {
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESHitEvent>(HitEventSink::GetSingleton());
			holder->AddEventSink<RE::TESSpellCastEvent>(SpellCastEventSink::GetSingleton());
			eventSinksRegistered = true;
		}
	}
	logger::info(
		"[ReactiveFX] Event layer initialized: hit/spell sinks={} quality={} heroDebris={} (guarded)",
		eventSinksRegistered ? 1 : 0,
		settings.Quality,
		settings.EnableHeroDebris ? 1 : 0);
}

void ReactiveFX::QueueDebugEvent(SourceType source, SurfaceType surface, ImpulseType impulse)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return;
	const float angle = player->GetAngleZ();
	const RE::NiPoint3 direction{ std::sin(angle), std::cos(angle), 0.24f };
	Event event{};
	event.position = player->GetPosition() + direction * 180.0f + RE::NiPoint3{ 0.0f, 0.0f, 48.0f };
	event.direction = direction;
	event.radius = source == SourceType::Shout ? 1200.0f : 140.0f;
	event.strength = 1.25f;
	event.surface = surface;
	event.source = source;
	event.impulse = impulse;
	QueueEvent(event);
}

void ReactiveFX::DrawSettings()
{
	ImGui::TextWrapped("Experimental PIXL extension. Reactions are layered onto existing Skyrim events; combat, quests, animation and original impact rendering remain authoritative.");
	ImGui::Checkbox("Enable Reactive FX", &settings.Enabled);
	ImGui::BeginDisabled(!settings.Enabled);
	int quality = static_cast<int>(std::min(settings.Quality, 3u));
	if (ImGui::Combo("Quality", &quality, "Low\0Medium\0High\0Ultra\0"))
		settings.Quality = static_cast<std::uint32_t>(std::clamp(quality, 0, 3));
	ImGui::SliderFloat("Maximum reaction distance", &settings.MaximumDistance, 1000.0f, 30000.0f, "%.0f units");
	ImGui::SliderFloat("Particle response", &settings.ParticleIntensity, 0.0f, 3.0f, "%.2fx");
	ImGui::Checkbox("Scene collision", &settings.EnableParticleCollision);
	ImGui::TextWrapped("ReactiveFX no longer displaces grass or other vegetation. Existing PIXL wind and foliage systems retain ownership of vegetation motion; ReactiveFX impulses are used by GPU particles, smoke, dust and debris.");
	ImGui::BeginDisabled(true);
	ImGui::Checkbox("Secondary impact events (validation pending)", &settings.EnableSecondaryImpacts);
	ImGui::Checkbox("Hero Havok debris (ownership validation pending)", &settings.EnableHeroDebris);
	ImGui::EndDisabled();
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Secondary event recursion and real Havok debris remain hard-disabled until pooled ownership, save/load and cell-transition lifetime have passed live validation. GPU debris is active and does not touch Skyrim physics.");

	if (globals::state && globals::state->IsDeveloperMode() && ImGui::TreeNode("Debug / Reactive FX")) {
		int debug = static_cast<int>(std::min(settings.DebugMode, 4u));
		if (ImGui::Combo("Visualisation", &debug, "Off\0Particles\0Collision\0Impulse field\0Particle class\0"))
			settings.DebugMode = static_cast<std::uint32_t>(debug);
		if (ImGui::Button("Test metal strike"))
			QueueDebugEvent(SourceType::Blade, SurfaceType::Metal, ImpulseType::Directional);
		ImGui::SameLine();
		if (ImGui::Button("Test fire"))
			QueueDebugEvent(SourceType::Fire, SurfaceType::Stone, ImpulseType::Radial);
		ImGui::SameLine();
		if (ImGui::Button("Test frost"))
			QueueDebugEvent(SourceType::Frost, SurfaceType::Stone, ImpulseType::Radial);
		ImGui::SameLine();
		if (ImGui::Button("Test shock"))
			QueueDebugEvent(SourceType::Shock, SurfaceType::Metal, ImpulseType::Radial);
		if (ImGui::Button("Test shout wave"))
			QueueDebugEvent(SourceType::Shout, SurfaceType::Grass, ImpulseType::TravellingWave);
		if (ImGui::Button("Test stones + leaves"))
			QueueDebugEvent(SourceType::HeavyImpact, SurfaceType::Dirt, ImpulseType::Radial);
		const auto qualityIndex = std::min(settings.Quality, 3u);
		ImGui::Text("Particle capacity: %u | collision budget: %u", kParticleCaps[qualityIndex], kCollisionCaps[qualityIndex]);
		ImGui::Text("Active impulses: %u | collision phase: %u", activeImpulseCount, collisionPhase);
		ImGui::Text("Spawn commands: %zu | optical copy: %s", spawnCommands.size(), opticalTimeRemaining > 0.0f ? "active" : "idle");
		ImGui::Text("Last event: %s -> %s", SourceName(lastSource), SurfaceName(lastSurface));
		ImGui::Text("Dropped events: %u | dropped particles: %u", droppedEvents.load(std::memory_order_relaxed), droppedParticles);
		ImGui::Text("GPU pass: %s", reactivePassReady ? "ready" : "unavailable");
		ImGui::TreePop();
	}
	ImGui::EndDisabled();
}

void ReactiveFX::LoadSettings(json& value)
{
	settings = value;
	settings.Quality = std::min(settings.Quality, 3u);
	settings.MaximumDistance = std::clamp(
		std::isfinite(settings.MaximumDistance) ? settings.MaximumDistance : 12000.0f,
		512.0f,
		50000.0f);
	settings.ParticleIntensity = std::clamp(
		std::isfinite(settings.ParticleIntensity) ? settings.ParticleIntensity : 1.0f,
		0.0f,
		3.0f);
	// Retain the legacy fields during config migration, but do not allow an old
	// vegetation setting to re-enable the retired grass displacement path.
	settings.EnableVegetationResponse = false;
	settings.VegetationResponse = 1.0f;
	settings.DebugMode = std::min(settings.DebugMode, 4u);
	// The switch is serialized for forward migration, but real Havok objects stay
	// guarded until ownership and cell-transition validation is complete. Recursive
	// impact generation is likewise held until event deduplication is validated.
	settings.EnableHeroDebris = false;
	settings.EnableSecondaryImpacts = false;
}

void ReactiveFX::SaveSettings(json& value) { value = settings; }

void ReactiveFX::RestoreDefaultSettings()
{
	settings = {};
	settings.EnableVegetationResponse = false;
	settings.VegetationResponse = 1.0f;
	std::scoped_lock lock(eventMutex);
	for (auto& queue : eventQueues)
		queue.clear();
	producerQueue = 0;
}

void ReactiveFX::ClearShaderCache()
{
	spawnCS = nullptr;
	simulateCS = nullptr;
	buildMaskCS = nullptr;
	compositeCS = nullptr;
	reconstructionMaskCS = nullptr;
	shaderCompilationAttempted = false;
	reactivePassReady = false;
}

void ReactiveFX::Reset()
{
	// State::Reset is a normal per-frame boundary in PIXL. Particle and impulse
	// histories intentionally survive it; explicit resource/shader invalidation is
	// handled by setup and ClearShaderCache instead.
}
