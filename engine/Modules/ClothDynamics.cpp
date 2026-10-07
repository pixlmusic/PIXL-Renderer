#include "ClothDynamics.h"

#include "Globals.h"
#include "Modules/ActorSurfaceEffects.h"
#include "State.h"
#include "Utils/UI.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace
{
	constexpr std::size_t kMaximumTrackedActors = 64;

	float ClampFinite(float a_value, float a_min, float a_max)
	{
		return std::clamp(std::isfinite(a_value) ? a_value : 0.0f, a_min, a_max);
	}

	float Approach(float a_current, float a_target, float a_response, float a_dt)
	{
		if (!std::isfinite(a_current) || !std::isfinite(a_target) || !std::isfinite(a_dt) || a_dt <= 0.0f)
			return a_target;
		const float alpha = 1.0f - std::exp(-std::max(a_response, 0.01f) * a_dt);
		return a_current + (a_target - a_current) * std::clamp(alpha, 0.0f, 1.0f);
	}

	RE::Actor* ResolveHierarchy(RE::NiAVObject* a_root)
	{
		for (auto* current = a_root; current; current = current->parent) {
			if (auto* owner = current->GetUserData()) {
				if (auto* actor = owner->As<RE::Actor>())
					return actor;
			}
		}
		return nullptr;
	}

	RE::Actor* ResolveOwningActor(RE::BSGeometry* a_geometry)
	{
		if (!a_geometry)
			return nullptr;
		if (auto* actor = ResolveHierarchy(a_geometry))
			return actor;
		auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
		if (!skin)
			return nullptr;
		if (auto* actor = ResolveHierarchy(skin->rootParent))
			return actor;
		if (!skin->bones)
			return nullptr;
		for (std::uint32_t index = 0; index < std::min(skin->numMatrices, 256u); ++index) {
			if (auto* actor = ResolveHierarchy(skin->bones[index]))
				return actor;
		}
		return nullptr;
	}

	bool IsEligibleSurface(RE::BSRenderPass* a_pass)
	{
		if (!a_pass || !a_pass->geometry || !a_pass->shaderProperty ||
			!a_pass->geometry->GetGeometryRuntimeData().skinInstance ||
			!a_pass->shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSkinned))
			return false;
		auto* material = a_pass->shaderProperty->GetBaseMaterial();
		if (!material)
			return false;
		const auto feature = material->GetFeature();
		return feature != RE::BSShaderMaterial::Feature::kFaceGen &&
			feature != RE::BSShaderMaterial::Feature::kFaceGenRGBTint &&
			feature != RE::BSShaderMaterial::Feature::kHairTint &&
			feature != RE::BSShaderMaterial::Feature::kEye &&
			feature != RE::BSShaderMaterial::Feature::kTreeAnim;
	}

	bool ReadHealthFraction(RE::Actor* a_actor, float& a_fraction)
	{
		if (!a_actor)
			return false;
		auto* values = a_actor->AsActorValueOwner();
		if (!values)
			return false;
		const float current = values->GetActorValue(RE::ActorValue::kHealth);
		const float permanent = values->GetPermanentActorValue(RE::ActorValue::kHealth);
		const float temporary = a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kHealth);
		const float maximum = permanent + temporary;
		if (!std::isfinite(current) || !std::isfinite(maximum) || maximum <= 1.0e-3f)
			return false;
		a_fraction = std::clamp(current / maximum, 0.0f, 1.0f);
		return true;
	}
}

struct ClothDynamics::Runtime
{
	struct ActorState
	{
		float smoothedSeverity = 0.0f;
		float healthFraction = 1.0f;
		std::uint32_t lastFrame = 0;
		std::uint32_t lastSeenFrame = 0;
		ClothingDamageState damage{};
	};

	std::unordered_map<std::uint32_t, ActorState> actors{};
	mutable std::mutex mutex{};
	std::uint32_t updatedThisFrame = 0;
	bool hooksInstalled = false;
	bool unavailableLogged = false;
};

namespace
{
	struct ClothingDamageSetupHook
	{
		static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags)
		{
			// Publish health state before ActorSurfaceEffects binds the shared b13 payload.
			globals::pipeline::clothDynamics.PrepareActorForGeometry(a_pass);
			func(a_this, a_pass, a_renderFlags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

ClothDynamics::ClothDynamics() : runtime(std::make_unique<Runtime>()) {}
ClothDynamics::~ClothDynamics() = default;

std::pair<std::string, std::vector<std::string>> ClothDynamics::GetModuleSummary()
{
	return {
		"Experimental health-driven wear patterns for eligible skinned clothing and armor.",
		{
			"Actor health smoothly controls deterministic fabric tear and armor scratch patterns",
			"Original normal maps remain completely untouched",
			"Damage is a reversible shader overlay and does not alter Skyrim gameplay or meshes",
			"Bounded actor tracking with distance culling and safe material fallbacks"
		}
	};
}

bool ClothDynamics::HasShaderDefine(RE::BSShader::Type a_type)
{
	return a_type == RE::BSShader::Type::Lighting;
}

bool ClothDynamics::AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage a_stage)
{
	if (a_type != RE::BSShader::Type::Lighting || a_stage != CachedShaderStage::Pixel)
		return false;
	const auto technique = (a_descriptor >> 24u) & 0x3Fu;
	const bool skinned = (a_descriptor & (1u << 1u)) != 0u;
	return skinned || technique == 4u || technique == 5u || technique == 6u || technique == 16u;
}

void ClothDynamics::SetupResources()
{
	if (!runtime || !globals::d3d::device || !globals::pipeline::actorSurfaceEffects.loaded) {
		if (runtime && !runtime->unavailableLogged) {
			logger::warn("[ClothingWear] Requires the established ActorSurfaceEffects character payload; staying disabled");
			runtime->unavailableLogged = true;
		}
		loaded = false;
		settings.Enable = false;
		return;
	}
	logger::info("[ClothingWear] Ready (health-driven damage overlay; original normal maps preserved)");
}

void ClothDynamics::InstallLateHooks()
{
	if (!runtime || runtime->hooksInstalled || !loaded || !globals::pipeline::actorSurfaceEffects.loaded)
		return;
	stl::write_vfunc<0x6, ClothingDamageSetupHook>(RE::VTABLE_BSLightingShader[0]);
	runtime->hooksInstalled = true;
	logger::info("[ClothingWear] Installed actor draw preparation hook");
}

void ClothDynamics::Reset()
{
	if (!runtime || settings.Enable)
		return;
	std::scoped_lock lock(runtime->mutex);
	runtime->actors.clear();
	runtime->updatedThisFrame = 0;
}

void ClothDynamics::Prepass()
{
	if (!runtime)
		return;
	globals::pipeline::actorSurfaceEffects.ClearClothingDamage();
	std::scoped_lock lock(runtime->mutex);
	runtime->updatedThisFrame = 0;
	const auto frame = globals::state ? globals::state->GetFrameCount() : 0u;
	for (auto it = runtime->actors.begin(); it != runtime->actors.end();) {
		if (frame > it->second.lastSeenFrame + 120u)
			it = runtime->actors.erase(it);
		else
			++it;
	}
}

void ClothDynamics::PrepareActorForGeometry(RE::BSRenderPass* a_pass)
{
	if (!runtime || !loaded || !settings.Enable || !IsEligibleSurface(a_pass))
		return;
	auto* actor = ResolveOwningActor(a_pass->geometry);
	if (!actor || !actor->Is3DLoaded())
		return;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return;
	const float distanceSquared = actor->GetPosition().GetSquaredDistance(player->GetPosition());
	const float distanceLimit = std::clamp(settings.ActorDistance, 256.0f, 8000.0f);
	if (!std::isfinite(distanceSquared) || distanceSquared > distanceLimit * distanceLimit)
		return;
	float healthFraction = 1.0f;
	if (!ReadHealthFraction(actor, healthFraction))
		return;

	const auto frame = globals::state ? globals::state->GetFrameCount() : 0u;
	float dt = globals::game::deltaTime ? *globals::game::deltaTime : RE::GetSecondsSinceLastFrame();
	if (!std::isfinite(dt) || dt <= 0.0f)
		dt = 1.0f / 60.0f;
	dt = std::clamp(dt, 1.0e-4f, 0.10f);

	std::scoped_lock lock(runtime->mutex);
	const auto formID = actor->GetFormID();
	auto found = runtime->actors.find(formID);
	if (found == runtime->actors.end()) {
		if (runtime->actors.size() >= kMaximumTrackedActors) {
			auto victim = std::min_element(runtime->actors.begin(), runtime->actors.end(),
				[](const auto& a_left, const auto& a_right) { return a_left.second.lastSeenFrame < a_right.second.lastSeenFrame; });
			if (victim != runtime->actors.end())
				runtime->actors.erase(victim);
		}
		found = runtime->actors.emplace(formID, Runtime::ActorState{}).first;
	}
	auto& state = found->second;
	state.lastSeenFrame = frame;
	if (state.lastFrame == frame)
		return;
	state.lastFrame = frame;
	const float targetSeverity = std::pow(1.0f - healthFraction, std::clamp(settings.HealthInfluence, 0.25f, 2.0f));
	state.smoothedSeverity = ClampFinite(Approach(state.smoothedSeverity, targetSeverity, settings.ResponseSpeed, dt), 0.0f, 1.0f);
	state.healthFraction = healthFraction;
	const float seed = static_cast<float>((formID * 747796405u + 2891336453u) >> 8u) / 16777215.0f;
	state.damage.Damage = { state.smoothedSeverity, healthFraction, 0.0f, 1.0f };
	state.damage.Parameters = {
		ClampFinite(settings.ClothingTearStrength, 0.0f, 2.0f),
		ClampFinite(settings.ArmorScratchStrength, 0.0f, 2.0f),
		seed,
		static_cast<float>(settings.Debug)
	};
	state.damage.frame = std::max(frame, 1u);
	state.damage.actorFormID = formID;
	runtime->updatedThisFrame++;
	globals::pipeline::actorSurfaceEffects.SetClothingDamage(formID, state.damage);
}

bool ClothDynamics::GetDamageState(std::uint32_t a_formID, ClothingDamageState& a_out) const
{
	if (!runtime || a_formID == 0u)
		return false;
	std::scoped_lock lock(runtime->mutex);
	const auto found = runtime->actors.find(a_formID);
	if (found == runtime->actors.end() || found->second.damage.actorFormID != a_formID)
		return false;
	a_out = found->second.damage;
	return true;
}

std::uint32_t ClothDynamics::GetActiveActorCount() const
{
	if (!runtime)
		return 0;
	std::scoped_lock lock(runtime->mutex);
	return static_cast<std::uint32_t>(runtime->actors.size());
}

std::uint32_t ClothDynamics::GetUpdatedActorCount() const
{
	if (!runtime)
		return 0;
	std::scoped_lock lock(runtime->mutex);
	return runtime->updatedThisFrame;
}

void ClothDynamics::DrawSettings()
{
	bool changed = ImGui::Checkbox("Enable Clothing Wear & Damage", &settings.Enable);
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds reversible procedural fabric tears and armor scratches as actor health falls. It preserves authored normal maps; marks are shading overlays, not mesh holes or Skyrim damage state.");
	ImGui::BeginDisabled(!settings.Enable);
	changed |= ImGui::SliderFloat("Fabric Tear Strength", &settings.ClothingTearStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat("Armor Scratch Strength", &settings.ArmorScratchStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat("Health Influence", &settings.HealthInfluence, 0.25f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat("Response Speed", &settings.ResponseSpeed, 0.5f, 12.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	changed |= ImGui::SliderFloat("Actor Distance", &settings.ActorDistance, 256.0f, 8000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
	if (globals::state && globals::state->IsDeveloperMode()) {
		static constexpr const char* debugNames[] = { "Off", "Damage Severity", "Scratch Mask", "Tear Mask" };
		int debug = static_cast<int>(settings.Debug);
		if (ImGui::Combo("Debug Mode", &debug, debugNames, static_cast<int>(std::size(debugNames)))) {
			settings.Debug = static_cast<DebugMode>(std::clamp(debug, 0, 3));
			changed = true;
		}
	}
	ImGui::TextDisabled("Actors tracked: %u | updated this frame: %u", GetActiveActorCount(), GetUpdatedActorCount());
	ImGui::EndDisabled();
	if (changed && globals::state)
		globals::state->UpdateFeatureData(globals::state->inWorld);
}

void ClothDynamics::LoadSettings(json& a_json)
{
	settings.Enable = a_json.value("Enable", false);
	// Migrate the old prototype's strength into both new visual layers.
	const float legacyStrength = ClampFinite(a_json.value("WrinkleStrength", 1.0f), 0.0f, 2.0f);
	settings.ClothingTearStrength = ClampFinite(a_json.value("ClothingTearStrength", legacyStrength), 0.0f, 2.0f);
	settings.ArmorScratchStrength = ClampFinite(a_json.value("ArmorScratchStrength", legacyStrength), 0.0f, 2.0f);
	settings.HealthInfluence = ClampFinite(a_json.value("HealthInfluence", 1.0f), 0.25f, 2.0f);
	settings.ResponseSpeed = ClampFinite(a_json.value("ResponseSpeed", 3.0f), 0.5f, 12.0f);
	settings.ActorDistance = ClampFinite(a_json.value("ActorDistance", 1800.0f), 256.0f, 8000.0f);
	settings.Debug = DebugMode::Off;
}

void ClothDynamics::SaveSettings(json& a_json)
{
	a_json = {
		{ "Enable", settings.Enable },
		{ "ClothingTearStrength", settings.ClothingTearStrength },
		{ "ArmorScratchStrength", settings.ArmorScratchStrength },
		{ "HealthInfluence", settings.HealthInfluence },
		{ "ResponseSpeed", settings.ResponseSpeed },
		{ "ActorDistance", settings.ActorDistance }
	};
}

void ClothDynamics::RestoreDefaultSettings()
{
	settings = {};
}
