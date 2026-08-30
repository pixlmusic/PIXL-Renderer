#include "Atmosphere.h"
#include "RainResponse.h"

#include "Deferred.h"
#include "Modules/SkyVeil.h"
#include "Globals.h"
#include "Modules/AmbientProbe.h"
#include "Modules/RadiantGrid.h"
#include "Modules/SkyBounce.h"
#include "Modules/TerrainOcclusion.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "WeatherVariableRegistry.h"

#define I18N_KEY_PREFIX "feature.atmosphere."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Atmosphere::Settings,
	enabled,
	useWorldProbes,
	startDistance,
	fogHeight,
	fogHeightFalloff,
	fogDensity,
	directionalInscatteringMultiplier,
	directionalInscatteringAnisotropy,
	inscatteringTint,
	cubemapMipLevel,
	sunlightAttenuationAmount,
	respectVanillaFogFade,
	disableVanillaFog,
	fogInscatteringColor,
	originalFogColorAmount,
	volumetricFogEnabled,
	volumetricGridPixelSize,
	volumetricGridSizeZ,
	volumetricFogDistance,
	volumetricFogStartDistance,
	volumetricFogNearFadeInDistance,
	volumetricFogExtinctionScale,
	volumetricFogScatteringDistribution,
	volumetricFogAlbedo,
	volumetricFogEmissive,
	volumetricDirectionalScatteringIntensity,
	volumetricShadowBias,
	volumetricDepthDistributionScale,
	volumetricSkyLightingIntensity,
	volumetricHistoryWeight,
	volumetricHistoryMissSampleCount,
	volumetricSampleJitterMultiplier,
	volumetricUpsampleJitterMultiplier,
	volumetricLocalLightScatteringIntensity,
	volumetricUseDisplayResolutionGrid,
	volumetricDepthAwareUpsampling,
	volumetricDepthAwareUpsamplingStrength,
	volumetricHistoryRadianceClamp,
	volumetricHistoryDepthRejection,
	mapAtmosphereEnabled,
	mapDisableVolumetricFog,
	mapDisableVanillaFog,
	mapFogDensityMultiplier,
	mapFogHeightFalloffMultiplier,
	mapStartDistance,
	mapMinimumTransmittance,
	mapAmbientInscatteringMultiplier,
	mapDirectionalInscatteringMultiplier,
	mapSunlightAttenuationMultiplier,
	mapWorldProbeMultiplier)

namespace
{
	// Lighting already consumes a dense set of low/mid SRV slots, and FXC may
	// auto-assign unregistered resources into gaps such as t20. Keep the existing
	// integrated volume at t19, but place the auxiliary depth-range texture in a
	// deliberately high, currently unused PS slot to avoid permutation-dependent
	// register collisions. D3D11 pixel shaders expose 128 SRV slots (t0..t127).
	constexpr UINT kIntegratedFogPSSlot = 19u;
	constexpr UINT kDepthRangePSSlot = 99u;

	float Halton(uint32_t a_index, uint32_t a_base)
	{
		float result = 0.0f;
		float invBase = 1.0f / static_cast<float>(a_base);
		float fraction = invBase;
		while (a_index > 0) {
			result += static_cast<float>(a_index % a_base) * fraction;
			a_index /= a_base;
			fraction *= invBase;
		}
		return result;
	}
}

void Atmosphere::RestoreDefaultSettings()
{
	settings = {};
}

void Atmosphere::LoadSettings(json& o_json)
{
	settings = o_json;
	auto finiteOr = [](float value, float fallback) {
		return std::isfinite(value) ? value : fallback;
	};
	settings.enabled = settings.enabled ? 1u : 0u;
	settings.useWorldProbes = settings.useWorldProbes ? 1u : 0u;
	settings.startDistance = std::clamp(finiteOr(settings.startDistance, 0.0f), 0.0f, 100000.0f);
	settings.fogHeight = std::clamp(finiteOr(settings.fogHeight, 0.0f), -22000.0f, 22000.0f);
	settings.fogHeightFalloff = std::clamp(finiteOr(settings.fogHeightFalloff, 0.2f), 0.001f, 2.0f);
	settings.fogDensity = std::clamp(finiteOr(settings.fogDensity, 0.005f), 0.0f, 1.0f);
	settings.directionalInscatteringMultiplier = std::clamp(finiteOr(settings.directionalInscatteringMultiplier, 1.0f), 0.0f, 10.0f);
	settings.directionalInscatteringAnisotropy = std::clamp(finiteOr(settings.directionalInscatteringAnisotropy, 0.2f), -0.99f, 0.99f);
	settings.cubemapMipLevel = std::clamp(finiteOr(settings.cubemapMipLevel, 7.0f), 1.0f, 7.0f);
	settings.sunlightAttenuationAmount = std::clamp(finiteOr(settings.sunlightAttenuationAmount, 1.0f), 0.0f, 1.0f);
	settings.respectVanillaFogFade = settings.respectVanillaFogFade ? 1u : 0u;
	settings.disableVanillaFog = settings.disableVanillaFog ? 1u : 0u;
	settings.originalFogColorAmount = std::clamp(finiteOr(settings.originalFogColorAmount, 0.0f), 0.0f, 1.0f);
	settings.volumetricFogEnabled = settings.volumetricFogEnabled ? 1u : 0u;
	settings.volumetricGridPixelSize = std::clamp(settings.volumetricGridPixelSize, 4u, 64u);
	settings.volumetricGridSizeZ = std::clamp(settings.volumetricGridSizeZ, 16u, 160u);
	settings.volumetricFogDistance = std::clamp(finiteOr(settings.volumetricFogDistance, 60000.0f), 1000.0f, 200000.0f);
	settings.volumetricFogStartDistance = std::clamp(finiteOr(settings.volumetricFogStartDistance, 0.0f), 0.0f, 200000.0f);
	settings.volumetricFogNearFadeInDistance = std::clamp(finiteOr(settings.volumetricFogNearFadeInDistance, 1000.0f), 0.0f, 20000.0f);
	settings.volumetricFogExtinctionScale = std::clamp(finiteOr(settings.volumetricFogExtinctionScale, 1.0f), 0.0f, 10.0f);
	settings.volumetricDirectionalScatteringIntensity = std::clamp(finiteOr(settings.volumetricDirectionalScatteringIntensity, 1.0f), 0.0f, 10.0f);
	settings.volumetricShadowBias = std::clamp(finiteOr(settings.volumetricShadowBias, 0.002f), 0.0f, 0.05f);
	settings.volumetricDepthDistributionScale = std::clamp(finiteOr(settings.volumetricDepthDistributionScale, 8.0f), 1.0f, 128.0f);
	settings.volumetricSkyLightingIntensity = std::clamp(finiteOr(settings.volumetricSkyLightingIntensity, 1.0f), 0.0f, 10.0f);
	settings.volumetricFogScatteringDistribution = std::clamp(finiteOr(settings.volumetricFogScatteringDistribution, 0.2f), -0.9f, 0.9f);
	settings.volumetricHistoryWeight = std::clamp(finiteOr(settings.volumetricHistoryWeight, 0.96f), 0.0f, 0.99f);
	settings.volumetricHistoryMissSampleCount = std::clamp(settings.volumetricHistoryMissSampleCount, 1u, 16u);
	settings.volumetricSampleJitterMultiplier = std::clamp(finiteOr(settings.volumetricSampleJitterMultiplier, 0.0f), 0.0f, 1.0f);
	settings.volumetricUpsampleJitterMultiplier = std::clamp(finiteOr(settings.volumetricUpsampleJitterMultiplier, 0.0f), 0.0f, 1.0f);
	settings.volumetricLocalLightScatteringIntensity = std::clamp(finiteOr(settings.volumetricLocalLightScatteringIntensity, 1.0f), 0.0f, 100.0f);
	settings.volumetricUseDisplayResolutionGrid = settings.volumetricUseDisplayResolutionGrid ? 1u : 0u;
	settings.volumetricDepthAwareUpsampling = settings.volumetricDepthAwareUpsampling ? 1u : 0u;
	settings.volumetricDepthAwareUpsamplingStrength = std::clamp(finiteOr(settings.volumetricDepthAwareUpsamplingStrength, 8.0f), 0.0f, 32.0f);
	settings.volumetricHistoryRadianceClamp = std::clamp(finiteOr(settings.volumetricHistoryRadianceClamp, 4.0f), 1.0f, 12.0f);
	settings.volumetricHistoryDepthRejection = std::clamp(finiteOr(settings.volumetricHistoryDepthRejection, 8.0f), 0.0f, 32.0f);
	settings.mapAtmosphereEnabled = settings.mapAtmosphereEnabled ? 1u : 0u;
	settings.mapDisableVolumetricFog = settings.mapDisableVolumetricFog ? 1u : 0u;
	settings.mapDisableVanillaFog = settings.mapDisableVanillaFog ? 1u : 0u;
	settings.mapFogDensityMultiplier = std::clamp(finiteOr(settings.mapFogDensityMultiplier, 0.18f), 0.0f, 1.0f);
	settings.mapFogHeightFalloffMultiplier = std::clamp(finiteOr(settings.mapFogHeightFalloffMultiplier, 1.5f), 0.05f, 2.0f);
	settings.mapStartDistance = std::clamp(finiteOr(settings.mapStartDistance, 2500.0f), 0.0f, 50000.0f);
	settings.mapMinimumTransmittance = std::clamp(finiteOr(settings.mapMinimumTransmittance, 0.55f), 0.0f, 1.0f);
	settings.mapAmbientInscatteringMultiplier = std::clamp(finiteOr(settings.mapAmbientInscatteringMultiplier, 0.65f), 0.0f, 2.0f);
	settings.mapDirectionalInscatteringMultiplier = std::clamp(finiteOr(settings.mapDirectionalInscatteringMultiplier, 0.35f), 0.0f, 2.0f);
	settings.mapSunlightAttenuationMultiplier = std::clamp(finiteOr(settings.mapSunlightAttenuationMultiplier, 0.25f), 0.0f, 1.0f);
	settings.mapWorldProbeMultiplier = std::clamp(finiteOr(settings.mapWorldProbeMultiplier, 0.35f), 0.0f, 1.0f);
}

void Atmosphere::SaveSettings(json& o_json)
{
	o_json = settings;
}

Atmosphere::Settings Atmosphere::GetCommonBufferData() const
{
	return settings;
}

void Atmosphere::DrawSettings()
{
	Util::UIntCheckbox(T(TKEY("enable_exp_height_fog"), "Enable Atmosphere"), &settings.enabled);
	Util::WeatherUI::SliderFloat(T(TKEY("start_distance"), "Start Distance"), this, "startDistance", &settings.startDistance, 0.0f, 100000.0f, "%.1f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_height"), "Fog Height"), this, "fogHeight", &settings.fogHeight, -22000.0f, 22000.0f, "%.1f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_height_falloff"), "Fog Height Falloff"), this, "fogHeightFalloff", &settings.fogHeightFalloff, 0.001f, 2.0f, "%.3f");
	Util::WeatherUI::ColorEdit4(T(TKEY("fog_inscattering_color"), "Fog Inscattering Color"), this, "fogInscatteringColor", (float*)&settings.fogInscatteringColor);
	Util::WeatherUI::SliderFloat(T(TKEY("original_fog_color_amount"), "Original Fog Color Amount"), this, "originalFogColorAmount", &settings.originalFogColorAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("fog_density"), "Fog Density"), this, "fogDensity", &settings.fogDensity, 0.0f, 1.0f, "%.3f");
	Util::WeatherUI::SliderFloat(T(TKEY("dir_inscattering_mul"), "Directional Light Inscattering Multiplier"), this, "directionalInscatteringMultiplier", &settings.directionalInscatteringMultiplier, 0.0f, 10.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("sunlight_attenuation"), "Sunlight Attenuation Amount"), this, "sunlightAttenuationAmount", &settings.sunlightAttenuationAmount, 0.0f, 1.0f, "%.2f");
	Util::WeatherUI::SliderFloat(T(TKEY("dir_inscattering_anisotropy"), "Directional Light Inscattering Anisotropy"), this, "directionalInscatteringAnisotropy", &settings.directionalInscatteringAnisotropy, -0.99f, 0.99f, "%.3f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("dir_inscattering_anisotropy_tooltip"),
							  "Controls the asymmetry of inscattering via the Henyey-Greenstein phase function.\n"
							  "Positive values produce forward scattering (glow around sun).\n"
							  "Zero is isotropic. Negative values produce back scattering."));
	}
	Util::UIntCheckbox(T(TKEY("disable_vanilla_fog"), "Disable Vanilla Fog"), &settings.disableVanillaFog);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("disable_vanilla_fog_tooltip"), "Disables the vanilla fog entirely. Only exponential height fog will be applied."));
	}
	Util::WeatherUI::UIntCheckbox(T(TKEY("apply_vanilla_fade"), "Apply Vanilla Fade"), this, "respectVanillaFogFade", &settings.respectVanillaFogFade);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T(TKEY("apply_vanilla_fade_tooltip"), "Applies vanilla fade brightness to exponential height fog."));
	}
	Util::UIntCheckbox(T(TKEY("use_dynamic_cubemaps"), "Use World Probes for Inscattering"), &settings.useWorldProbes);
	Util::WeatherUI::ColorEdit4(T(TKEY("inscattering_cubemap_tint"), "Inscattering Cubemap Tint"), this, "inscatteringTint", (float*)&settings.inscatteringTint);
	ImGui::SliderFloat(T(TKEY("cubemap_mip_level"), "Environment Blur"), &settings.cubemapMipLevel, 1.0f, 7.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SeparatorText(T(TKEY("volumetric_fog"), "Volumetric Fog"));
	Util::WeatherUI::UIntCheckbox(T(TKEY("enable_volumetric_fog"), "Enable Volumetric Fog"), this, "volumetricFogEnabled", &settings.volumetricFogEnabled);
	if (settings.volumetricFogEnabled) {
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_view_distance"), "Volumetric View Distance"), this, "volumetricFogDistance", &settings.volumetricFogDistance, 1000.0f, 200000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_start_distance"), "Volumetric Start Distance"), this, "volumetricFogStartDistance", &settings.volumetricFogStartDistance, 0.0f, 20000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("near_fade_in_distance"), "Near Fade In Distance"), this, "volumetricFogNearFadeInDistance", &settings.volumetricFogNearFadeInDistance, 0.0f, 20000.0f, "%.0f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_extinction_scale"), "Volumetric Extinction Scale"), this, "volumetricFogExtinctionScale", &settings.volumetricFogExtinctionScale, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("volumetric_scattering_distribution"), "Volumetric Scattering Distribution"), this, "volumetricFogScatteringDistribution", &settings.volumetricFogScatteringDistribution, -0.9f, 0.9f, "%.2f");
		Util::WeatherUI::ColorEdit4(T(TKEY("volumetric_albedo"), "Volumetric Albedo"), this, "volumetricFogAlbedo", (float*)&settings.volumetricFogAlbedo);
		Util::WeatherUI::ColorEdit4(T(TKEY("volumetric_emissive"), "Volumetric Emissive"), this, "volumetricFogEmissive", (float*)&settings.volumetricFogEmissive);
		Util::WeatherUI::SliderFloat(T(TKEY("directional_scattering_intensity"), "Directional Scattering Intensity"), this, "volumetricDirectionalScatteringIntensity", &settings.volumetricDirectionalScatteringIntensity, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("sky_lighting_scattering_intensity"), "Sky Lighting Scattering Intensity"), this, "volumetricSkyLightingIntensity", &settings.volumetricSkyLightingIntensity, 0.0f, 10.0f, "%.2f");
		Util::WeatherUI::SliderFloat(T(TKEY("local_light_scattering_intensity"), "Local Light Scattering Intensity"), this, "volumetricLocalLightScatteringIntensity", &settings.volumetricLocalLightScatteringIntensity, 0.0f, 10.0f, "%.2f");
		if (ImGui::TreeNode(T(TKEY("advanced_volumetric_quality"), "Advanced Volumetric Quality"))) {
			uint32_t minGridPixelSize = 4;
			uint32_t maxGridPixelSize = 64;
			uint32_t minGridSizeZ = 16;
			uint32_t maxGridSizeZ = 160;
			ImGui::SliderScalar(T(TKEY("grid_pixel_size"), "Grid Pixel Size"), ImGuiDataType_U32, &settings.volumetricGridPixelSize, &minGridPixelSize, &maxGridPixelSize, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderScalar(T(TKEY("grid_depth_slices"), "Grid Depth Slices"), ImGuiDataType_U32, &settings.volumetricGridSizeZ, &minGridSizeZ, &maxGridSizeZ, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("directional_shadow_bias"), "Directional Shadow Bias"), &settings.volumetricShadowBias, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("depth_distribution_scale"), "Depth Distribution Scale"), &settings.volumetricDepthDistributionScale, 1.0f, 128.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("temporal_history_weight"), "Temporal History Weight"), &settings.volumetricHistoryWeight, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			uint32_t minHistoryMissSampleCount = 1;
			uint32_t maxHistoryMissSampleCount = 16;
			ImGui::SliderScalar(T(TKEY("history_miss_samples"), "History Miss Samples"), ImGuiDataType_U32, &settings.volumetricHistoryMissSampleCount, &minHistoryMissSampleCount, &maxHistoryMissSampleCount, "%u", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("sample_jitter_multiplier"), "Sample Jitter Multiplier"), &settings.volumetricSampleJitterMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("sample_jitter_multiplier_tooltip"),
									  "Matches UE's r.VolumetricFog.LightScatteringSampleJitterMultiplier.\n"
									  "Adds per-voxel random offset on top of the Halton sequence.\n"
									  "0 = UE default; nonzero values need stronger temporal filtering."));
			}
			ImGui::SliderFloat(T(TKEY("upsample_jitter_multiplier"), "Upsample Jitter Multiplier"), &settings.volumetricUpsampleJitterMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("upsample_jitter_multiplier_tooltip"),
									  "Legacy final-volume screen-space jitter. Atmosphere 2.0 defaults this to 0\n"
									  "because depth-aware reconstruction replaces the visible checker/interleave pattern."));
			}
			Util::UIntCheckbox(T(TKEY("display_resolution_grid"), "Display-Resolution Froxel Density"), &settings.volumetricUseDisplayResolutionGrid);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("display_resolution_grid_tooltip"),
									  "Keeps volumetric XY quality stable when DLSS/FSR lowers internal render resolution."));
			}
			Util::UIntCheckbox(T(TKEY("depth_aware_upsampling"), "Depth-Aware Volumetric Reconstruction"), &settings.volumetricDepthAwareUpsampling);
			if (settings.volumetricDepthAwareUpsampling) {
				ImGui::SliderFloat(T(TKEY("depth_aware_upsampling_strength"), "Depth Edge Rejection"), &settings.volumetricDepthAwareUpsamplingStrength, 0.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			}
			ImGui::SliderFloat(T(TKEY("history_radiance_clamp"), "History Radiance Clamp"), &settings.volumetricHistoryRadianceClamp, 1.0f, 12.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat(T(TKEY("history_depth_rejection"), "History Depth Rejection"), &settings.volumetricHistoryDepthRejection, 0.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}
	}

	ImGui::SeparatorText(T(TKEY("world_map_atmosphere"), "World Map Atmosphere"));
	Util::UIntCheckbox(T(TKEY("map_atmosphere_enabled"), "Use Dedicated Map Atmosphere"), &settings.mapAtmosphereEnabled);
	if (settings.mapAtmosphereEnabled) {
		Util::UIntCheckbox(T(TKEY("map_disable_volumetric"), "Disable Froxel Volumetrics In Map"), &settings.mapDisableVolumetricFog);
		Util::UIntCheckbox(T(TKEY("map_disable_vanilla"), "Disable Vanilla Fog In Map"), &settings.mapDisableVanillaFog);
		ImGui::SliderFloat(T(TKEY("map_density_multiplier"), "Map Fog Density Multiplier"), &settings.mapFogDensityMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_height_falloff_multiplier"), "Map Height Falloff Multiplier"), &settings.mapFogHeightFalloffMultiplier, 0.05f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_start_distance"), "Map Fog Start Distance"), &settings.mapStartDistance, 0.0f, 50000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_min_transmittance"), "Minimum Terrain Visibility"), &settings.mapMinimumTransmittance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_ambient_scattering"), "Map Ambient Scattering"), &settings.mapAmbientInscatteringMultiplier, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_directional_scattering"), "Map Sun Scattering"), &settings.mapDirectionalInscatteringMultiplier, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_sun_attenuation"), "Map Sunlight Fog Attenuation"), &settings.mapSunlightAttenuationMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T(TKEY("map_world_probe"), "Map World-Probe Scattering"), &settings.mapWorldProbeMultiplier, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
}

void Atmosphere::SetupResources()
{
	if (!globals::d3d::device || !globals::d3d::context) {
		logger::error("[PIXL Atmosphere] D3D11 device/context unavailable; volumetric resources were not created");
		return;
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, linearSampler.put()));
	Util::SetResourceName(linearSampler.get(), "Atmosphere::LinearSampler");

	samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, shadowSampler.put()));
	Util::SetResourceName(shadowSampler.get(), "Atmosphere::ShadowSampler");

	volumetricFogCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<VolumetricFogCB>(), "Atmosphere::VolumetricFogCB");
}

void Atmosphere::ClearShaderCache()
{
	if (materialSetupCS) {
		materialSetupCS->Release();
		materialSetupCS = nullptr;
	}
	if (conservativeDepthCS) {
		conservativeDepthCS->Release();
		conservativeDepthCS = nullptr;
	}
	if (lightScatteringCS) {
		lightScatteringCS->Release();
		lightScatteringCS = nullptr;
	}
	if (integrationCS) {
		integrationCS->Release();
		integrationCS = nullptr;
	}
}

void Atmosphere::CaptureDirectionalShadowMap()
{
	if (!globals::d3d::context)
		return;

	ID3D11ShaderResourceView* shadowMap = nullptr;
	globals::d3d::context->PSGetShaderResources(4, 1, &shadowMap);
	directionalShadowMap.copy_from(shadowMap);
	if (shadowMap)
		shadowMap->Release();
}

void Atmosphere::EnsureVolumetricResources()
{
	if (!globals::d3d::device || !globals::game::graphicsState)
		return;

	uint32_t pixelSize = std::clamp(settings.volumetricGridPixelSize, 4u, 64u);
	const uint32_t gridZ = std::clamp(settings.volumetricGridSizeZ, 16u, 160u);
	float2 screenSz{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	// Atmosphere 2.0 keeps the apparent froxel footprint tied to display pixels.
	// DLSS/FSR therefore no longer silently lowers volumetric XY resolution.
	auto renderSize = settings.volumetricUseDisplayResolutionGrid ? screenSz : Util::ConvertToDynamic(screenSz);

	auto getGridSize = [&renderSize, gridZ](uint32_t a_pixelSize) {
		return DirectX::XMUINT4{
			std::max(1u, static_cast<uint32_t>(std::ceil(renderSize.x / static_cast<float>(a_pixelSize)))),
			std::max(1u, static_cast<uint32_t>(std::ceil(renderSize.y / static_cast<float>(a_pixelSize)))),
			gridZ,
			0u
		};
	};
	DirectX::XMUINT4 gridSize = getGridSize(pixelSize);

	constexpr uint64_t maxVolumeVoxels = 16ull * 1024ull * 1024ull;
	while (pixelSize < 64u &&
		   static_cast<uint64_t>(gridSize.x) * gridSize.y * gridSize.z > maxVolumeVoxels) {
		pixelSize++;
		gridSize = getGridSize(pixelSize);
	}

	if (vBufferA && currentGridSize.x == gridSize.x && currentGridSize.y == gridSize.y && currentGridSize.z == gridSize.z)
		return;

	currentGridSize = gridSize;

	D3D11_TEXTURE3D_DESC texDesc{};
	texDesc.Width = gridSize.x;
	texDesc.Height = gridSize.y;
	texDesc.Depth = gridSize.z;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Texture3D.MipLevels = 1;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
	uavDesc.Texture3D.MipSlice = 0;
	uavDesc.Texture3D.FirstWSlice = 0;
	uavDesc.Texture3D.WSize = gridSize.z;

	vBufferA = std::make_unique<Texture3D>(texDesc, "Atmosphere::VBufferA");
	vBufferA->CreateSRV(srvDesc);
	vBufferA->CreateUAV(uavDesc);

	D3D11_TEXTURE2D_DESC conservativeDepthDesc{};
	conservativeDepthDesc.Width = gridSize.x;
	conservativeDepthDesc.Height = gridSize.y;
	conservativeDepthDesc.MipLevels = 1;
	conservativeDepthDesc.ArraySize = 1;
	conservativeDepthDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	conservativeDepthDesc.SampleDesc.Count = 1;
	conservativeDepthDesc.Usage = D3D11_USAGE_DEFAULT;
	conservativeDepthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	D3D11_SHADER_RESOURCE_VIEW_DESC conservativeDepthSrvDesc{};
	conservativeDepthSrvDesc.Format = conservativeDepthDesc.Format;
	conservativeDepthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	conservativeDepthSrvDesc.Texture2D.MipLevels = 1;

	D3D11_UNORDERED_ACCESS_VIEW_DESC conservativeDepthUavDesc{};
	conservativeDepthUavDesc.Format = conservativeDepthDesc.Format;
	conservativeDepthUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	conservativeDepth = std::make_unique<Texture2D>(conservativeDepthDesc, "Atmosphere::ConservativeDepth");
	conservativeDepth->CreateSRV(conservativeDepthSrvDesc);
	conservativeDepth->CreateUAV(conservativeDepthUavDesc);

	conservativeDepthHistory = std::make_unique<Texture2D>(conservativeDepthDesc, "Atmosphere::ConservativeDepthHistory");
	conservativeDepthHistory->CreateSRV(conservativeDepthSrvDesc);

	lightScattering = std::make_unique<Texture3D>(texDesc, "Atmosphere::LightScattering");
	lightScattering->CreateSRV(srvDesc);
	lightScattering->CreateUAV(uavDesc);

	lightScatteringHistory = std::make_unique<Texture3D>(texDesc, "Atmosphere::LightScatteringHistory");
	lightScatteringHistory->CreateSRV(srvDesc);

	integratedLightScattering = std::make_unique<Texture3D>(texDesc, "Atmosphere::IntegratedLightScattering");
	integratedLightScattering->CreateSRV(srvDesc);
	integratedLightScattering->CreateUAV(uavDesc);

	hasLightScatteringHistory = false;
	hasConservativeDepthHistory = false;
	hasSceneClassHistory = false;
	lastPrepassFrame = UINT32_MAX;
}

void Atmosphere::ReleaseVolumetricResources()
{
	vBufferA.reset();
	conservativeDepth.reset();
	conservativeDepthHistory.reset();
	lightScattering.reset();
	lightScatteringHistory.reset();
	integratedLightScattering.reset();
	currentGridSize = {};
	hasLightScatteringHistory = false;
	hasConservativeDepthHistory = false;
	hasSceneClassHistory = false;
	lastPrepassFrame = UINT32_MAX;
	ID3D11ShaderResourceView* nullSRV = nullptr;
	if (globals::d3d::context) {
		globals::d3d::context->PSSetShaderResources(kIntegratedFogPSSlot, 1, &nullSRV);
		globals::d3d::context->PSSetShaderResources(kDepthRangePSSlot, 1, &nullSRV);
	}
}

void Atmosphere::BindIntegratedLightScattering()
{
	if (!globals::d3d::context)
		return;

	ID3D11ShaderResourceView* integratedFogSRV = integratedLightScattering ? integratedLightScattering->srv.get() : nullptr;
	ID3D11ShaderResourceView* depthRangeSRV = conservativeDepth ? conservativeDepth->srv.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(kIntegratedFogPSSlot, 1, &integratedFogSRV);
	globals::d3d::context->PSSetShaderResources(kDepthRangePSSlot, 1, &depthRangeSRV);
}

ID3D11ComputeShader* Atmosphere::GetMaterialSetupCS()
{
	if (!materialSetupCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		if (globals::pipeline::rainResponse.loaded)
			defines.emplace_back("RAIN_RESPONSE", "");

		materialSetupCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\Atmosphere\\VolumetricFogMaterialCS.hlsl", defines, "cs_5_0"));
	}
	return materialSetupCS;
}

ID3D11ComputeShader* Atmosphere::GetConservativeDepthCS()
{
	if (!conservativeDepthCS)
		conservativeDepthCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Atmosphere\\VolumetricFogConservativeDepthCS.hlsl", {}, "cs_5_0"));
	return conservativeDepthCS;
}

ID3D11ComputeShader* Atmosphere::GetLightScatteringCS()
{
	if (!lightScatteringCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		if (globals::pipeline::radiantGrid.loaded) {
			defines.emplace_back("RADIANT_GRID", "");
		}
		if (globals::pipeline::terrainOcclusion.loaded) {
			defines.emplace_back("TERRAIN_OCCLUSION", "");
		}
		if (globals::pipeline::skyVeil.loaded) {
			defines.emplace_back("SKY_VEIL", "");
		}
		lightScatteringCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Atmosphere\\VolumetricFogLightScatteringCS.hlsl", defines, "cs_5_0"));
	}
	return lightScatteringCS;
}

ID3D11ComputeShader* Atmosphere::GetIntegrationCS()
{
	if (!integrationCS)
		integrationCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Atmosphere\\VolumetricFogIntegrationCS.hlsl", {}, "cs_5_0"));
	return integrationCS;
}

void Atmosphere::Prepass()
{
	if (!globals::d3d::context || !globals::state || !globals::game::graphicsState ||
		!volumetricFogCB || !linearSampler || !shadowSampler ||
		!globals::state->sharedDataCB || !globals::state->featureDataCB ||
		!*globals::game::perFrame.get()) {
		ReleaseVolumetricResources();
		return;
	}

	if (!settings.enabled || !settings.volumetricFogEnabled || settings.volumetricFogExtinctionScale <= 0.0f) {
		ReleaseVolumetricResources();
		return;
	}

	const bool inMapMenu = globals::state && globals::state->isMapMenuOpen;
	const bool mapProfileActive = inMapMenu && settings.mapAtmosphereEnabled != 0;
	const bool inInterior = Util::IsInterior();
	const bool hideSky = globals::game::sky && globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);

	// Previous-frame Lighting leaves the integrated volume/depth-range bound to PS.
	// Explicitly release those SRVs before the same resources become CS UAV targets;
	// this avoids D3D11 read/write hazards and debug-layer auto-unbinds.
	ID3D11ShaderResourceView* nullAtmosphereSRV = nullptr;
	globals::d3d::context->PSSetShaderResources(kIntegratedFogPSSlot, 1, &nullAtmosphereSRV);
	globals::d3d::context->PSSetShaderResources(kDepthRangePSSlot, 1, &nullAtmosphereSRV);

	// The map uses analytical aerial perspective by default. Avoid spending a full
	// volumetric pass on it, and never let stale gameplay history leak across the
	// map/game camera transition. Resources are retained to avoid a reallocation hitch.
	if (mapProfileActive && settings.mapDisableVolumetricFog != 0) {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
		hasSceneClassHistory = false;
		lastPrepassFrame = UINT32_MAX;
		return;
	}

	EnsureVolumetricResources();
	if (!vBufferA || !conservativeDepth || !conservativeDepthHistory ||
		!lightScattering || !lightScatteringHistory || !integratedLightScattering ||
		currentGridSize.x == 0u || currentGridSize.y == 0u || currentGridSize.z == 0u) {
		return;
	}

	if (settings.fogDensity <= 0.0f) {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
		hasSceneClassHistory = false;
		lastPrepassFrame = UINT32_MAX;
		return;
	}

	ID3D11ShaderResourceView* directionalShadowLightData = globals::deferred && globals::deferred->directionalShadowLights ? globals::deferred->directionalShadowLights->srv.get() : nullptr;
	auto& radiantGrid = globals::pipeline::radiantGrid;
	const bool hasLocalLightData =
		radiantGrid.loaded &&
		radiantGrid.lights &&
		radiantGrid.lightIndexList &&
		radiantGrid.lightGrid;
	auto* depthSrv = Util::GetCurrentSceneDepthSRV(true);
	auto& ambientProbe = globals::pipeline::ambientProbe;
	auto& skyBounce = globals::pipeline::skyBounce;
	const bool hasIBL = ambientProbe.loaded &&
	                    ambientProbe.settings.EnableAmbientProbe != 0 &&
	                    !ambientProbe.IsDisabledForCurrentScene() &&
	                    ambientProbe.envIBLTexture &&
	                    ambientProbe.skyIBLTexture;
	const bool hasSkyBounce = skyBounce.loaded && skyBounce.texProbeArray;

	const bool temporalReprojection = Util::GetTemporal();
	const auto& frameBuffer = globals::game::frameBufferCached;
	const auto& cameraPos = frameBuffer.GetCameraPosAdjust();
	const auto& previousCameraPos = frameBuffer.GetCameraPreviousPosAdjust();
	const float dx = cameraPos.x - previousCameraPos.x;
	const float dy = cameraPos.y - previousCameraPos.y;
	const float dz = cameraPos.z - previousCameraPos.z;
	const float cameraDeltaSq = dx * dx + dy * dy + dz * dz;
	constexpr float kCameraCutDistance = 4096.0f;
	const bool cameraCut = cameraDeltaSq > kCameraCutDistance * kCameraCutDistance;

	const auto& dr = frameBuffer.GetDynamicResolutionParams1();
	const bool dynamicResolutionChanged =
		std::abs(dr.x - dr.z) > 0.01f ||
		std::abs(dr.y - dr.w) > 0.01f;

	const bool sceneClassChanged =
		hasSceneClassHistory &&
		(inInterior != lastInInterior || hideSky != lastHideSky || inMapMenu != lastInMapMenu);

	const bool temporalHistoryValid =
		temporalReprojection &&
		hasLightScatteringHistory &&
		lastPrepassFrame != UINT32_MAX &&
		globals::state->frameCount == lastPrepassFrame + 1u &&
		!cameraCut &&
		!dynamicResolutionChanged &&
		!sceneClassChanged;

	VolumetricFogCB cb{};
	cb.gridSizeAndFlags = {
		currentGridSize.x,
		currentGridSize.y,
		currentGridSize.z,
		(directionalShadowMap && directionalShadowLightData ? 1u : 0u) |
			(depthSrv ? 2u : 0u) |
			(hasIBL ? 4u : 0u) |
			(hasSkyBounce ? 8u : 0u) |
			(depthSrv && temporalHistoryValid && hasConservativeDepthHistory ? 16u : 0u) |
			(hasLocalLightData ? 32u : 0u)
	};
	cb.invGridSizeAndNearFade = {
		1.0f / static_cast<float>(currentGridSize.x),
		1.0f / static_cast<float>(currentGridSize.y),
		1.0f / static_cast<float>(currentGridSize.z),
		settings.volumetricFogNearFadeInDistance > 0.0f ? 1.0f / settings.volumetricFogNearFadeInDistance : 100000000.0f
	};

	const auto cameraData = Util::GetCameraData();
	const double nearPlane = std::max(static_cast<double>(cameraData.y), static_cast<double>(std::max(settings.volumetricFogStartDistance, 0.0f)));
	const double farPlane = std::max(nearPlane + 1.0, static_cast<double>(std::max(settings.volumetricFogDistance, settings.volumetricFogStartDistance + 1.0f)));
	const double nearWithOffset = nearPlane + 0.095 * 100.0;
	const double depthDistributionScale = std::max(
		static_cast<double>(settings.volumetricDepthDistributionScale),
		static_cast<double>(currentGridSize.z) / 120.0);
	const double farExp = std::exp2(std::min(static_cast<double>(currentGridSize.z) / depthDistributionScale, 120.0));
	const double depthDenominator = farPlane - nearWithOffset;
	const double safeDepthDenominator =
		std::abs(depthDenominator) >= 1.0e-6
			? depthDenominator
			: std::copysign(1.0e-6, depthDenominator == 0.0 ? 1.0 : depthDenominator);
	const double gridZOffset = (farPlane - nearWithOffset * farExp) / safeDepthDenominator;
	const double gridZScale = (1.0 - gridZOffset) / nearWithOffset;
	cb.gridZParams = {
		static_cast<float>(gridZScale),
		static_cast<float>(gridZOffset),
		static_cast<float>(depthDistributionScale),
		0.0f
	};

	cb.clipToWorld = globals::game::frameBufferCached.GetCameraViewProjUnjittered().Invert();

	for (uint32_t i = 0; i < std::size(cb.frameJitterOffsets); i++) {
		const uint32_t temporalFrame = (globals::state->frameCount - i) & 1023u;
		cb.frameJitterOffsets[i] = {
			temporalReprojection ? Halton(temporalFrame, 2) : 0.5f,
			temporalReprojection ? Halton(temporalFrame, 3) : 0.5f,
			temporalReprojection ? Halton(temporalFrame, 5) : 0.5f,
			0.0f
		};
	}
	cb.historyParameters = {
		temporalHistoryValid ? std::clamp(settings.volumetricHistoryWeight, 0.0f, 0.99f) : 0.0f,
		static_cast<float>(std::clamp(settings.volumetricHistoryMissSampleCount, 1u, 16u)),
		0.0f,
		0.0f
	};
	cb.jitterParameters = {
		temporalReprojection ? std::max(settings.volumetricSampleJitterMultiplier, 0.0f) : 0.0f,
		static_cast<float>(globals::state->frameCount % 8u),
		0.0f,
		0.0f
	};
	auto* conservativeDepthShader = depthSrv ? GetConservativeDepthCS() : nullptr;
	auto* materialSetupShader = GetMaterialSetupCS();
	auto* lightScatteringShader = GetLightScatteringCS();
	auto* integrationShader = GetIntegrationCS();
	if ((depthSrv && !conservativeDepthShader) || !materialSetupShader ||
		!lightScatteringShader || !integrationShader) {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
		return;
	}

	volumetricFogCB->Update(cb);

	auto context = globals::d3d::context;
	ID3D11Buffer* cbuffers[1]{ volumetricFogCB->CB() };
	context->CSSetConstantBuffers(0, 1, cbuffers);

	ID3D11Buffer* sharedBuffers[2]{ globals::state->sharedDataCB->CB(), globals::state->featureDataCB->CB() };
	context->CSSetConstantBuffers(5, 2, sharedBuffers);

	ID3D11Buffer* frameBuffers[1]{ *globals::game::perFrame.get() };
	context->CSSetConstantBuffers(12, 1, frameBuffers);

	ID3D11SamplerState* samplers[2]{ linearSampler.get(), shadowSampler.get() };
	context->CSSetSamplers(0, 2, samplers);

	const uint32_t groupX = (currentGridSize.x + 7) / 8;
	const uint32_t groupY = (currentGridSize.y + 7) / 8;
	const uint32_t groupZ = (currentGridSize.z + 3) / 4;

	context->CSSetShaderResources(17, 1, &depthSrv);
	ID3D11ShaderResourceView* skyBounceSrv = hasSkyBounce ? skyBounce.texProbeArray->srv.get() : nullptr;
	ID3D11ShaderResourceView* iblSrvs[2]{
		hasIBL ? ambientProbe.envIBLTexture->srv.get() : nullptr,
		hasIBL ? ambientProbe.skyIBLTexture->srv.get() : nullptr
	};
	context->CSSetShaderResources(50, 1, &skyBounceSrv);
	context->CSSetShaderResources(76, 2, iblSrvs);

	if (depthSrv) {
		ID3D11UnorderedAccessView* uavs[1]{ conservativeDepth->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(conservativeDepthShader, nullptr, 0);
		context->Dispatch(groupX, groupY, 1);
		uavs[0] = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}

	{
		ID3D11UnorderedAccessView* uavs[1]{ vBufferA->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(materialSetupShader, nullptr, 0);
		context->Dispatch(groupX, groupY, groupZ);
		uavs[0] = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}

	{
		ID3D11ShaderResourceView* srvs[5]{
			vBufferA->srv.get(),
			directionalShadowMap.get(),
			temporalHistoryValid ? lightScatteringHistory->srv.get() : nullptr,
			conservativeDepth->srv.get(),
			temporalHistoryValid && hasConservativeDepthHistory ? conservativeDepthHistory->srv.get() : nullptr
		};
		ID3D11ShaderResourceView* localLightSrvs[3]{
			hasLocalLightData ? radiantGrid.lights->srv.get() : nullptr,
			hasLocalLightData ? radiantGrid.lightIndexList->srv.get() : nullptr,
			hasLocalLightData ? radiantGrid.lightGrid->srv.get() : nullptr
		};
		ID3D11UnorderedAccessView* uavs[1]{ lightScattering->uav.get() };
		context->CSSetShaderResources(0, 5, srvs);
		context->CSSetShaderResources(35, 3, localLightSrvs);
		context->CSSetShaderResources(98, 1, &directionalShadowLightData);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(lightScatteringShader, nullptr, 0);
		context->Dispatch(groupX, groupY, groupZ);
		uavs[0] = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}

	{
		ID3D11ShaderResourceView* srvs[1]{ lightScattering->srv.get() };
		ID3D11UnorderedAccessView* uavs[1]{ integratedLightScattering->uav.get() };
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(integrationShader, nullptr, 0);
		context->Dispatch(groupX, groupY, 1);
	}

	ID3D11ShaderResourceView* nullSrvs[5]{ nullptr, nullptr, nullptr, nullptr, nullptr };
	ID3D11ShaderResourceView* nullDepthSrv[1]{ nullptr };
	ID3D11UnorderedAccessView* nullUav[1]{ nullptr };
	ID3D11SamplerState* nullSamplers[2]{ nullptr, nullptr };
	ID3D11Buffer* nullCb[1]{ nullptr };
	ID3D11Buffer* nullSharedCbs[2]{ nullptr, nullptr };
	context->CSSetShaderResources(0, 5, nullSrvs);
	context->CSSetShaderResources(17, 1, nullDepthSrv);
	context->CSSetShaderResources(35, 3, nullSrvs);
	context->CSSetShaderResources(50, 1, nullDepthSrv);
	context->CSSetShaderResources(76, 2, nullSrvs);
	context->CSSetShaderResources(98, 1, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
	context->CSSetSamplers(0, 2, nullSamplers);
	context->CSSetConstantBuffers(0, 1, nullCb);
	context->CSSetConstantBuffers(5, 2, nullSharedCbs);
	context->CSSetConstantBuffers(12, 1, nullCb);
	context->CSSetShader(nullptr, nullptr, 0);

	if (temporalReprojection) {
		context->CopyResource(lightScatteringHistory->resource.get(), lightScattering->resource.get());
		hasLightScatteringHistory = true;
		if (depthSrv) {
			context->CopyResource(conservativeDepthHistory->resource.get(), conservativeDepth->resource.get());
			hasConservativeDepthHistory = true;
		} else {
			hasConservativeDepthHistory = false;
		}
	} else {
		hasLightScatteringHistory = false;
		hasConservativeDepthHistory = false;
	}

	lastInInterior = inInterior;
	lastHideSky = hideSky;
	lastInMapMenu = inMapMenu;
	hasSceneClassHistory = true;
	lastPrepassFrame = globals::state->frameCount;
	BindIntegratedLightScattering();
}

void Atmosphere::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetOrCreateFeatureRegistry(GetShortName());
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Start Distance",
		"startDistance",
		"Start distance of the fog, from the camera",
		&settings.startDistance,
		0.0f,
		0.0f, 100000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height",
		"fogHeight",
		"Base height of the fog effect",
		&settings.fogHeight,
		0.0f,
		-22000.0f, 22000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Height Falloff",
		"fogHeightFalloff",
		"Height density factor controls how the density increases as height decreases",
		&settings.fogHeightFalloff,
		0.2f,
		0.001f, 2.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Fog Inscattering Color",
		"fogInscatteringColor",
		"Color added to the fog inscattering contribution",
		&settings.fogInscatteringColor,
		float4{ 0.0f, 0.0f, 0.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Original Fog Color Amount",
		"originalFogColorAmount",
		"Amount of the original fog color added to fog inscattering",
		&settings.originalFogColorAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Fog Density",
		"fogDensity",
		"Overall density of the fog",
		&settings.fogDensity,
		0.02f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Multiplier",
		"directionalInscatteringMultiplier",
		"Multiplier for directional light inscattering",
		&settings.directionalInscatteringMultiplier,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Sunlight Attenuation Amount",
		"sunlightAttenuationAmount",
		"Amount of fog attenuation applied to direct sunlight",
		&settings.sunlightAttenuationAmount,
		1.0f,
		0.0f, 1.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Directional Inscattering Anisotropy",
		"directionalInscatteringAnisotropy",
		"Henyey-Greenstein asymmetry parameter. Positive = forward scattering, 0 = isotropic, negative = back scattering.",
		&settings.directionalInscatteringAnisotropy,
		0.2f,
		-0.99f, 0.99f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Inscattering Cubemap Tint",
		"inscatteringTint",
		"RGB tint for the inscattering cubemap with alpha for intensity",
		&settings.inscatteringTint,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"respectVanillaFogFade",
		"Apply Vanilla Fade",
		"Apply vanilla fade brightness to exponential height fog",
		(bool*)&settings.respectVanillaFogFade,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"disableVanillaFog",
		"Disable Vanilla Fog",
		"Disables vanilla fog entirely, only exponential height fog is applied",
		(bool*)&settings.disableVanillaFog,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::WeatherVariable<bool>>(
		"volumetricFogEnabled",
		"Enable Volumetric Fog",
		"Enables froxel-based volumetric fog for exponential height fog",
		(bool*)&settings.volumetricFogEnabled,
		false,
		[](const bool& from, const bool& to, float factor) {
			return factor > 0.5f ? to : from;
		}));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric View Distance",
		"volumetricFogDistance",
		"Maximum distance covered by exponential height volumetric fog",
		&settings.volumetricFogDistance,
		60000.0f,
		1000.0f, 200000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Start Distance",
		"volumetricFogStartDistance",
		"Start distance of volumetric fog from the camera",
		&settings.volumetricFogStartDistance,
		0.0f,
		0.0f, 200000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Near Fade In Distance",
		"volumetricFogNearFadeInDistance",
		"Distance over which volumetric fog fades in near the camera",
		&settings.volumetricFogNearFadeInDistance,
		1000.0f,
		0.0f, 20000.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Extinction Scale",
		"volumetricFogExtinctionScale",
		"Scale applied to volumetric fog extinction",
		&settings.volumetricFogExtinctionScale,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Scattering Distribution",
		"volumetricFogScatteringDistribution",
		"Henyey-Greenstein scattering distribution for volumetric fog",
		&settings.volumetricFogScatteringDistribution,
		0.2f,
		-0.9f, 0.9f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Directional Scattering Intensity",
		"volumetricDirectionalScatteringIntensity",
		"Scale applied to volumetric fog directional light scattering",
		&settings.volumetricDirectionalScatteringIntensity,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Volumetric Albedo",
		"volumetricFogAlbedo",
		"Volumetric fog albedo color",
		&settings.volumetricFogAlbedo,
		float4{ 1.0f, 1.0f, 1.0f, 1.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::Float4Variable>(
		"Volumetric Emissive",
		"volumetricFogEmissive",
		"Volumetric fog emissive color",
		&settings.volumetricFogEmissive,
		float4{ 0.0f, 0.0f, 0.0f, 0.0f }));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Sky Lighting Intensity",
		"volumetricSkyLightingIntensity",
		"Scale applied to volumetric fog sky lighting",
		&settings.volumetricSkyLightingIntensity,
		1.0f,
		0.0f, 10.0f));

	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Volumetric Local Light Scattering Intensity",
		"volumetricLocalLightScatteringIntensity",
		"Scale applied to volumetric fog local light scattering",
		&settings.volumetricLocalLightScatteringIntensity,
		1.0f,
		0.0f, 100.0f));
}
#undef I18N_KEY_PREFIX
