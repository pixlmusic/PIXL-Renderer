#include "NaturalLighting.h"
#include "Modules/NaturalLighting/Common.h"
#include "RadiantGrid.h"
#include <numbers>

namespace
{
	bool IsPlayerCastingLight(RE::BSLight* light)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !light) return false;
		for (auto hand : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
			auto* caster = skyrim_cast<RE::ActorMagicCaster*>(player->GetMagicCaster(hand));
			if (caster && caster->light.get() == light) return true;
		}
		return false;
	}
}

void NaturalLighting::PostPostLoad()
{
	stl::detour_thunk<CreatePointLight>(REL::RelocationID(17208, 17610));
	stl::detour_thunk<BSLight_GetLuminance>(REL::RelocationID(101303, 108292));

	logger::info("[NaturalLighting] Installed hooks");
}

RE::NiPointLight* NaturalLighting::CreatePointLight::thunk(RE::TESObjectLIGH* ligh, RE::TESObjectREFR* refr, RE::NiAVObject* root, bool forceDynamic, bool useLightRadius, bool affectRequesterOnly)
{
	const auto niLight = func(ligh, refr, root, forceDynamic, useLightRadius, affectRequesterOnly);

	if (ligh && root && niLight)
		SetExtLightData(niLight, ligh);

	return niLight;
}

void NaturalLighting::SetExtLightData(RE::NiLight* niLight, const RE::TESObjectLIGH* ligh)
{
	if (!niLight || !ligh)
		return;

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);
	runtimeData->flags.set(RadiantGrid::LightFlags::Initialised);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kInverseSquare)))
		runtimeData->flags.set(RadiantGrid::LightFlags::InverseSquare);
	if (ligh->data.flags.any(static_cast<RE::TES_LIGHT_FLAGS>(ISLCommon::TES_LIGHT_FLAGS_EXT::kLinear)))
		runtimeData->flags.set(RadiantGrid::LightFlags::Linear);
	runtimeData->cutoffOverride = std::clamp(ligh->data.fallofExponent, 0.01f, 1.f);
	runtimeData->lighFormId = ligh->formID;
	const float size = ligh->data.fov >= 50.0f ? std::numbers::sqrt2_v<float> : ligh->data.fov;
	runtimeData->size = std::clamp(size, 0.01f, 50.0f);
}

void NaturalLighting::ProcessLight(RadiantGrid::LightData& light, RE::BSLight* bsLight, RE::NiLight* niLight) const
{
	if (!bsLight || !niLight)
		return;

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (light.lightFlags.none(RadiantGrid::LightFlags::Initialised)) {
		const auto userData = niLight->GetUserData();
		logger::debug("[NaturalLighting] FormID: 0x{:08X} | Light*: {:p} | Name: {} - light uninitialised", userData ? userData->formID : 0, static_cast<void*>(niLight), niLight->name);
		runtimeData->flags.set(RadiantGrid::LightFlags::Initialised);
	}

	light.lightFlags = runtimeData->flags;
	light.color = { runtimeData->diffuse.red, runtimeData->diffuse.green, runtimeData->diffuse.blue };

	const bool isInvSq = light.lightFlags.any(RadiantGrid::LightFlags::InverseSquare);
	if (bsLight->pointLight && isInvSq) {
		const float intensity = runtimeData->fade * 4;
		light.radius = CalculateRadius(intensity, bsLight->IsShadowLight(), runtimeData->cutoffOverride, runtimeData->size);
		runtimeData->radius = light.radius;
		light.invRadius = 1.f / std::max(light.radius, 1.0f);
		light.fadeZone = 1.f / (light.radius * std::clamp(FadeZoneBase * light.invRadius, 0.f, 1.f));
		light.sizeBias = ScaledUnitsSq * runtimeData->size * runtimeData->size * 0.5f;
		// light.color *= intensity;
		light.fade = intensity;
	} else {
		light.radius = runtimeData->radius;
		light.invRadius = 1.f / std::max(light.radius, 1.0f);
		// light.color *= runtimeData->fade;
		light.fade = runtimeData->fade;
	}
	// Match the actual hand caster light, never asset names or all practicals.
	// Keep the source softer nearby and retire it over a wider useful radius.
	if (IsPlayerCastingLight(bsLight)) {
		light.fade *= 0.65f;
		light.radius = std::max(light.radius * 1.5f, 180.0f);
		light.invRadius = 1.0f / light.radius;
		light.fadeZone = 1.0f / std::max(light.radius * 0.35f, 1.0f);
		if (isInvSq)
			light.sizeBias = std::max(light.sizeBias, ScaledUnitsSq * 2.0f);
	}
}

float NaturalLighting::CalculateRadius(const float intensity, const bool shadowCaster, const float cutoffOverride, const float size)
{
	float cutoff = shadowCaster ? DefaultShadowCasterCutoff : DefaultCutoff;
	cutoff = cutoffOverride == 1.f ? cutoff : cutoffOverride;
	const float safeCutoff = std::max(cutoff, 0.001f);
	const float radicand = ScaledUnitsSq * std::max(
		(2.0f * std::max(intensity, 0.0f) - safeCutoff * size * size) / (2.0f * safeCutoff), 0.0f);
	return std::max(std::sqrt(radicand) * InverseSquareRangeScale, 1.0f);
}

inline float NaturalLighting::SmoothStep(const float edge0, const float edge1, const float x)
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float NaturalLighting::GetAttenuation(const float distance, const float radius, const float size)
{
	const float attenuation = ScaledUnitsSq / (distance * distance + ScaledUnitsSq * size * size / 2);
	const float safeRadius = std::max(radius, 1.0f);
	const float fadeZone = std::clamp(FadeZoneBase / safeRadius, 0.0f, 1.0f);
	const float fade = SmoothStep(0, std::max(safeRadius * fadeZone, 1.0f), safeRadius - distance);
	return attenuation * fade;
}

float NaturalLighting::BSLight_GetLuminance::thunk(RE::BSLight* bsLight, RE::NiPoint3* targetPosition, RE::NiLight* refLight)
{
	if (!bsLight || !targetPosition)
		return 0.0f;

	auto* niLight = bsLight->light.get();
	if (!niLight)
		return 0.0f;

	const auto runtimeData = ISLCommon::RuntimeLightDataExt::Get(niLight);

	if (refLight == niLight || runtimeData->flags.any(RadiantGrid::LightFlags::Disabled))
		return 0.0f;

	if (!bsLight->pointLight || runtimeData->flags.none(RadiantGrid::LightFlags::InverseSquare))
		return func(bsLight, targetPosition, refLight);

	const float dist = niLight->world.translate.GetDistance(*targetPosition);
	const bool handLight = IsPlayerCastingLight(bsLight);
	const float radius = handLight ? std::max(runtimeData->radius * 1.5f, 180.0f) : runtimeData->radius;
	const float size = handLight ? std::max(runtimeData->size, 2.0f) : runtimeData->size;
	const float sourceAttenuation = ScaledUnitsSq / std::max(dist * dist + ScaledUnitsSq * size * size / 2.0f, 1.0f);
	const float attenuation = handLight ? sourceAttenuation * SmoothStep(0.0f, radius * 0.35f, radius - dist) : GetAttenuation(dist, radius, size);
	const float luminance = (runtimeData->diffuse.red + runtimeData->diffuse.green + runtimeData->diffuse.blue) * runtimeData->fade * (handLight ? 2.6f : 4.0f) * attenuation * (1.0f / 3.0f);
	bsLight->luminance = luminance;

	return luminance;
}
