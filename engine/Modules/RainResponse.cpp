#include "RainResponse.h"
#include "I18n/I18n.h"
#include "State.h"
#include "Deferred.h"
#include "Utils/D3D.h"

#define I18N_KEY_PREFIX "feature.rain_response."

namespace
{
	struct SnowPrecipitationSettings
	{
		bool EnableWorldSpaceRain = true;
		bool EnableSnowEnhancement = true;
		float SnowDistanceVisibility = 0.90f;
		float SnowLightingResponse = 0.75f;
		float SnowWindDrift = 0.90f;
		float SnowFlutter = 0.80f;
		float SnowDensityBoost = 0.55f;
		float SnowDepthStart = 700.0f;
		float SnowDepthEnd = 14000.0f;
		float SnowFarVolume = 0.75f;
		float SnowTumble = 0.70f;
		float SnowFlakeScale = 1.00f;
		float SnowWorldScale = 1800.0f;
	};

	struct alignas(16) WorldPrecipitationTuning
	{
		std::uint32_t EnableWorldSpaceRain = 1u;
		std::uint32_t EnableSnowEnhancement = 1u;
		float Snowing = 0.0f;
		float SnowDistanceVisibility = 0.90f;

		float SnowLightingResponse = 0.75f;
		float SnowWindDrift = 0.90f;
		float SnowFlutter = 0.80f;
		float SnowDensityBoost = 0.55f;

		float SnowDepthStart = 700.0f;
		float SnowDepthEnd = 14000.0f;
		float SnowFarVolume = 0.75f;
		float SnowTumble = 0.70f;

		float SnowFlakeScale = 1.00f;
		float SnowWorldScale = 1800.0f;
		float pad0 = 0.0f;
		float pad1 = 0.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(WorldPrecipitationTuning);
	static_assert(sizeof(WorldPrecipitationTuning) == 64, "WorldPrecipitationTuning ABI mismatch");

	SnowPrecipitationSettings g_snowPrecipitation{};
	std::unique_ptr<ConstantBuffer> g_worldPrecipitationCB;

	float ResolveLiveSnowIntensity()
	{
		auto* sky = globals::game::sky;
		if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->IsSnowing())
			return 0.0f;

		auto weatherDensity = [](RE::TESWeather* weather) -> float {
			if (!weather || !weather->precipitationData)
				return 0.0f;

			const float density = weather->precipitationData->GetSettingValue(
				RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;

			// Vanilla precipitation density is authored on a small scale. This
			// normalizes strength while Sky::IsSnowing() remains the authoritative gate.
			return std::clamp(density / 3.0f, 0.0f, 1.0f);
		};

		const float weatherPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		const float currentDensity = weatherDensity(sky->currentWeather);
		const float lastDensity = weatherDensity(sky->lastWeather);

		float intensity = std::lerp(lastDensity, currentDensity, weatherPct);

		// Modded snow can have unusual density metadata. If Skyrim itself says the
		// snow system is visibly active, retain a useful optical floor.
		intensity = std::max(intensity, 0.38f);
		return std::pow(std::clamp(intensity, 0.0f, 1.0f), 0.78f);
	}

	struct alignas(16) RoofRunoffTuning
	{
		float MaxDistance = 5200.0f;
		float NearSizeDistance = 1700.0f;
		float EmitterSpacing = 92.0f;
		float pad0 = 0.0f;

		float2 RenderSize = { 1.0f, 1.0f };
		float2 InvRenderSize = { 1.0f, 1.0f };
	};
	STATIC_ASSERT_ALIGNAS_16(RoofRunoffTuning);
	static_assert(sizeof(RoofRunoffTuning) == 32, "RoofRunoffTuning must match CS b13");

	// Kept outside RainResponse::Settings on purpose: growing Settings would shift
	// every later feature inside SharedData::FeatureData. This dedicated b13 tuning
	// buffer adds the requested control without changing the existing b6 ABI.
	float g_roofRunoffDistance = 5200.0f;
	std::unique_ptr<ConstantBuffer> g_roofRunoffTuningCB;

	std::unique_ptr<Texture2D> g_roofRunoffStateA;
	std::unique_ptr<Texture2D> g_roofRunoffStateB;
	std::unique_ptr<Texture2D> g_roofRunoffDropMask;
	bool g_roofRunoffStateFlip = false;

	winrt::com_ptr<ID3D11ComputeShader> g_roofRunoffGenerateCS;
	winrt::com_ptr<ID3D11ComputeShader> g_roofRunoffCompositeCS;
	bool g_roofRunoffGenerateAttempted = false;
	bool g_roofRunoffCompositeAttempted = false;

	ID3D11ComputeShader* GetRoofRunoffGenerateCS()
	{
		if (!g_roofRunoffGenerateAttempted) {
			g_roofRunoffGenerateAttempted = true;
			auto* compiled = static_cast<ID3D11ComputeShader*>(
				Util::CompileShader(
					L"Data\\Shaders\\RainResponse\\RoofRunoffGenerateCS.hlsl",
					{},
					"cs_5_0"));
			if (compiled) {
				g_roofRunoffGenerateCS.attach(compiled);
				logger::info("[RainResponse] RoofRunoffGenerateCS compiled successfully");
			} else {
				logger::error("[RainResponse] RoofRunoffGenerateCS compilation failed; roof runoff disabled for this session");
			}
		}
		return g_roofRunoffGenerateCS.get();
	}

	ID3D11ComputeShader* GetRoofRunoffCompositeCS()
	{
		if (!g_roofRunoffCompositeAttempted) {
			g_roofRunoffCompositeAttempted = true;
			auto* compiled = static_cast<ID3D11ComputeShader*>(
				Util::CompileShader(
					L"Data\\Shaders\\RainResponse\\RoofRunoffCompositeCS.hlsl",
					{},
					"cs_5_0"));
			if (compiled) {
				g_roofRunoffCompositeCS.attach(compiled);
				logger::info("[RainResponse] RoofRunoffCompositeCS compiled successfully");
			} else {
				logger::error("[RainResponse] RoofRunoffCompositeCS compilation failed; roof runoff disabled for this session");
			}
		}
		return g_roofRunoffCompositeCS.get();
	}
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RainResponse::Settings,
	EnableRainResponse,
	MaxRainWetness,
	MaxPuddleWetness,
	MaxShoreWetness,
	ShoreRange,
	PuddleRadius,
	PuddleMaxAngle,
	PuddleMinWetness,
	MinRainWetness,
	SkinWetness,
	WeatherTransitionSpeed,
	EnableRaindropFx,
	EnableSplashes,
	EnableRipples,
	EnableVanillaRipples,
	RaindropFxRange,
	RaindropGridSize,
	RaindropInterval,
	RaindropChance,
	SplashesLifetime,
	SplashesStrength,
	SplashesMinRadius,
	SplashesMaxRadius,
	RippleStrength,
	RippleRadius,
	RippleBreadth,
	RippleLifetime,
	EnableRainParticleEnhancement,
	RainClumpStrength,
	RainClumpSize,
	RainStreakVariation,
	RainGustStrength,
	RainGustFrequency,
	RainGustChance,
	RainSecondaryLayerStrength,
	RainDepthStart,
	RainDepthEnd,
	RainDistanceBoost,
	RainImpactSplashStrength,
	RainMistStrength,
	RainMistScale,
	RainMistHeight,
	RainLightingBoost,
	RainRunoffStrength)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	RainResponse::DebugSettings,
	EnableWetnessOverride,
	EnablePuddleOverride,
	EnableRainOverride,
	EnableIntExOverride,
	WetnessOverride,
	PuddleWetnessOverride,
	RainOverride)

// Climate preset data - defines regional weather characteristics
// Precipitation rates calculated from actual shader mechanics: grid size, interval, and raindrop chance

struct ClimatePresetInfo
{
	const char* name;
	const char* shortDescription;
	const char* const* detailedDescription;
	const char* const* effectDescription;
	RainResponse::ClimateSettings settings;
};

// Climate preset detailed descriptions
static constexpr const char* LEGACY_DETAILED[] = {
	"Riverwood's original rain effect values for full backward compatibility.",
	"Max precipitation: ~0.66 mm/hr (very light)",
	"Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x.",
	"Raindrop: 30% chance, grid 4.0 units, interval 0.5s.",
	"Performance impact: Minimal (baseline)",
	nullptr
};
static constexpr const char* LEGACY_EFFECTS[] = {
	"Original wetness accumulation (1.0x)",
	"Original puddle formation (1.0x)",
	"Original weather transitions (1.0x)",
	"Original raindrop frequency (1.0x)",
	nullptr
};

static constexpr const char* ARCTIC_DETAILED[] = {
	"Cold, dry climate with minimal precipitation.",
	"Max precipitation: ~1.08 mm/hr (light)",
	"Multipliers: Wetness 0.5x, Puddle 0.3x, Transition 0.5x.",
	"Raindrop: 30% chance, grid 3.5 units, interval 0.4s.",
	"Performance impact: Minimal",
	nullptr
};
static constexpr const char* ARCTIC_EFFECTS[] = {
	"Slow wetness accumulation (0.5x)",
	"Minimal puddle formation (0.3x)",
	"Slow weather transitions (0.5x)",
	"Sparse precipitation (30% chance)",
	"",
	nullptr
};

static constexpr const char* NORDIC_DETAILED[] = {
	"Balanced temperate Nordic climate.",
	"Max precipitation: ~3.35 mm/hr (moderate)",
	"Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x.",
	"Raindrop: 100% chance, grid 3.0 units, interval 1.0s.",
	"Performance impact: Low",
	nullptr
};
static constexpr const char* NORDIC_EFFECTS[] = {
	"Standard wetness accumulation (1.0x)",
	"Standard puddle formation (1.0x)",
	"Standard weather transitions (1.0x)",
	"Moderate raindrop frequency (100% chance)",
	nullptr
};

static constexpr const char* COASTAL_DETAILED[] = {
	"Maritime climate with frequent, heavy precipitation.",
	"Max precipitation: ~8.06 mm/hr (heavy)",
	"Multipliers: Wetness 1.5x, Puddle 1.7x, Transition 1.7x.",
	"Raindrop: 80% chance, grid 2.5 units, interval 0.25s.",
	"Performance impact: Moderate",
	nullptr
};
static constexpr const char* COASTAL_EFFECTS[] = {
	"Fast wetness accumulation (1.5x)",
	"Enhanced puddle formation (1.7x)",
	"Rapid weather transitions (1.7x)",
	"Frequent rain events (80% chance)",
	nullptr
};

static constexpr const char* MONSOON_DETAILED[] = {
	"Tropical/monsoon climate with extreme precipitation.",
	"Max precipitation: ~22 mm/hr (extreme)",
	"Multipliers: Wetness 2.0x, Puddle 2.5x, Transition 2.0x.",
	"Raindrop: 100% chance, grid 2.0 units, interval 0.2s.",
	"Skyrim light rain will not match wetness.",
	"Performance impact: High (may impact GPU)",
	nullptr
};
static constexpr const char* MONSOON_EFFECTS[] = {
	"Rapid wetness accumulation (2.0x)",
	"Maximum puddle formation (2.5x)",
	"Very dynamic weather (2.0x)",
	"Maximum raindrop frequency (100% chance)",
	nullptr
};

static constexpr std::array<ClimatePresetInfo, 6> CLIMATE_PRESET_INFO = {
	{ { "Custom",
		  "User-defined custom settings",
		  nullptr,
		  nullptr,
		  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
		// Legacy (Original Skyrim)
		{
			"Legacy",
			"Original rain effect values (very light)",
			LEGACY_DETAILED,
			LEGACY_EFFECTS,
			{ 1.0f, 1.0f, 1.0f, 0.3f, 4.0f, 0.5f } },
		// Nordic Standard
		{
			"Nordic (Default)",
			"Balanced Nordic climate (moderate rain)",
			NORDIC_DETAILED,
			NORDIC_EFFECTS,
			{ 1.0f, 1.0f, 1.0f, 1.0f, 3.0f, 1.0f } },
		// Arctic Tundra
		{
			"Arctic Tundra",
			"Cold, dry Arctic climate (light rain)",
			ARCTIC_DETAILED,
			ARCTIC_EFFECTS,
			{ 0.5f, 0.3f, 0.5f, 0.3f, 3.5f, 0.4f } },
		// Temperate Coastal
		{
			"Temperate Coastal",
			"Maritime climate (heavy rain)",
			COASTAL_DETAILED,
			COASTAL_EFFECTS,
			{ 1.5f, 1.7f, 1.7f, 0.8f, 2.5f, 0.25f } },
		// Monsoon/Extreme
		{
			"Monsoon/Extreme",
			"Extreme monsoon climate (extreme rain)",
			MONSOON_DETAILED,
			MONSOON_EFFECTS,
			{ 2.0f, 2.5f, 2.0f, 1.0f, 2.0f, 0.2f } } }
};

// Extract just the settings for the actual climate preset array
static const std::array<RainResponse::ClimateSettings, 6> CLIMATE_PRESETS = { {
	CLIMATE_PRESET_INFO[0].settings,  // Custom (placeholder)
	CLIMATE_PRESET_INFO[1].settings,  // Legacy
	CLIMATE_PRESET_INFO[2].settings,  // Nordic Standard
	CLIMATE_PRESET_INFO[3].settings,  // Arctic Tundra
	CLIMATE_PRESET_INFO[4].settings,  // Temperate Coastal
	CLIMATE_PRESET_INFO[5].settings   // Monsoon/Extreme
} };

static const char* GetClimatePresetDisplayName(size_t a_index)
{
	switch (a_index) {
	case 0:
		return T(TKEY("climate_preset_custom"), "Custom");
	case 1:
		return T(TKEY("climate_preset_legacy"), "Legacy");
	case 2:
		return T(TKEY("climate_preset_nordic"), "Nordic (Default)");
	case 3:
		return T(TKEY("climate_preset_arctic"), "Arctic Tundra");
	case 4:
		return T(TKEY("climate_preset_coastal"), "Temperate Coastal");
	case 5:
		return T(TKEY("climate_preset_monsoon"), "Monsoon/Extreme");
	default:
		return T(TKEY("climate_preset_unknown"), "Unknown");
	}
}

static const char* GetClimatePresetShortDescription(size_t a_index)
{
	switch (a_index) {
	case 0:
		return T(TKEY("climate_preset_custom_desc"), "User-defined custom settings");
	case 1:
		return T(TKEY("climate_preset_legacy_desc"), "Original rain effect values (very light)");
	case 2:
		return T(TKEY("climate_preset_nordic_desc"), "Balanced Nordic climate (moderate rain)");
	case 3:
		return T(TKEY("climate_preset_arctic_desc"), "Cold, dry Arctic climate (light rain)");
	case 4:
		return T(TKEY("climate_preset_coastal_desc"), "Maritime climate (heavy rain)");
	case 5:
		return T(TKEY("climate_preset_monsoon_desc"), "Extreme monsoon climate (extreme rain)");
	default:
		return "";
	}
}

static void DrawWeatherAnalysisLabel(const char* a_label)
{
	const auto& palette = Menu::GetSingleton()->GetTheme().Palette;
	ImGui::TextColored(palette.Text, "%s", a_label);
	ImGui::Spacing();
}

static std::vector<const char*> GetClimatePresetDetailedDescription(size_t a_index)
{
	switch (a_index) {
	case 1:
		return {
			T(TKEY("climate_legacy_detail_0"), "Riverwood's original rain effect values for full backward compatibility."),
			T(TKEY("climate_legacy_detail_1"), "Max precipitation: ~0.66 mm/hr (very light)"),
			T(TKEY("climate_legacy_detail_2"), "Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x."),
			T(TKEY("climate_legacy_detail_3"), "Raindrop: 30% chance, grid 4.0 units, interval 0.5s."),
			T(TKEY("climate_legacy_detail_4"), "Performance impact: Minimal (baseline)")
		};
	case 2:
		return {
			T(TKEY("climate_nordic_detail_0"), "Balanced temperate Nordic climate."),
			T(TKEY("climate_nordic_detail_1"), "Max precipitation: ~3.35 mm/hr (moderate)"),
			T(TKEY("climate_nordic_detail_2"), "Multipliers: Wetness 1.0x, Puddle 1.0x, Transition 1.0x."),
			T(TKEY("climate_nordic_detail_3"), "Raindrop: 100% chance, grid 3.0 units, interval 1.0s."),
			T(TKEY("climate_nordic_detail_4"), "Performance impact: Low")
		};
	case 3:
		return {
			T(TKEY("climate_arctic_detail_0"), "Cold, dry climate with minimal precipitation."),
			T(TKEY("climate_arctic_detail_1"), "Max precipitation: ~1.08 mm/hr (light)"),
			T(TKEY("climate_arctic_detail_2"), "Multipliers: Wetness 0.5x, Puddle 0.3x, Transition 0.5x."),
			T(TKEY("climate_arctic_detail_3"), "Raindrop: 30% chance, grid 3.5 units, interval 0.4s."),
			T(TKEY("climate_arctic_detail_4"), "Performance impact: Minimal")
		};
	case 4:
		return {
			T(TKEY("climate_coastal_detail_0"), "Maritime climate with frequent, heavy precipitation."),
			T(TKEY("climate_coastal_detail_1"), "Max precipitation: ~8.06 mm/hr (heavy)"),
			T(TKEY("climate_coastal_detail_2"), "Multipliers: Wetness 1.5x, Puddle 1.7x, Transition 1.7x."),
			T(TKEY("climate_coastal_detail_3"), "Raindrop: 80% chance, grid 2.5 units, interval 0.25s."),
			T(TKEY("climate_coastal_detail_4"), "Performance impact: Moderate")
		};
	case 5:
		return {
			T(TKEY("climate_monsoon_detail_0"), "Tropical/monsoon climate with extreme precipitation."),
			T(TKEY("climate_monsoon_detail_1"), "Max precipitation: ~22 mm/hr (extreme)"),
			T(TKEY("climate_monsoon_detail_2"), "Multipliers: Wetness 2.0x, Puddle 2.5x, Transition 2.0x."),
			T(TKEY("climate_monsoon_detail_3"), "Raindrop: 100% chance, grid 2.0 units, interval 0.2s."),
			T(TKEY("climate_monsoon_detail_4"), "Skyrim light rain will not match wetness."),
			T(TKEY("climate_monsoon_detail_5"), "Performance impact: High (may impact GPU)")
		};
	default:
		return {};
	}
}

static std::vector<const char*> GetClimatePresetEffectDescription(size_t a_index)
{
	switch (a_index) {
	case 1:
		return {
			T(TKEY("climate_legacy_effect_0"), "Original wetness accumulation (1.0x)"),
			T(TKEY("climate_legacy_effect_1"), "Original puddle formation (1.0x)"),
			T(TKEY("climate_legacy_effect_2"), "Original weather transitions (1.0x)"),
			T(TKEY("climate_legacy_effect_3"), "Original raindrop frequency (1.0x)")
		};
	case 2:
		return {
			T(TKEY("climate_nordic_effect_0"), "Standard wetness accumulation (1.0x)"),
			T(TKEY("climate_nordic_effect_1"), "Standard puddle formation (1.0x)"),
			T(TKEY("climate_nordic_effect_2"), "Standard weather transitions (1.0x)"),
			T(TKEY("climate_nordic_effect_3"), "Moderate raindrop frequency (100% chance)")
		};
	case 3:
		return {
			T(TKEY("climate_arctic_effect_0"), "Slow wetness accumulation (0.5x)"),
			T(TKEY("climate_arctic_effect_1"), "Minimal puddle formation (0.3x)"),
			T(TKEY("climate_arctic_effect_2"), "Slow weather transitions (0.5x)"),
			T(TKEY("climate_arctic_effect_3"), "Sparse precipitation (30% chance)")
		};
	case 4:
		return {
			T(TKEY("climate_coastal_effect_0"), "Fast wetness accumulation (1.5x)"),
			T(TKEY("climate_coastal_effect_1"), "Enhanced puddle formation (1.7x)"),
			T(TKEY("climate_coastal_effect_2"), "Rapid weather transitions (1.7x)"),
			T(TKEY("climate_coastal_effect_3"), "Frequent rain events (80% chance)")
		};
	case 5:
		return {
			T(TKEY("climate_monsoon_effect_0"), "Rapid wetness accumulation (2.0x)"),
			T(TKEY("climate_monsoon_effect_1"), "Maximum puddle formation (2.5x)"),
			T(TKEY("climate_monsoon_effect_2"), "Very dynamic weather (2.0x)"),
			T(TKEY("climate_monsoon_effect_3"), "Maximum raindrop frequency (100% chance)")
		};
	default:
		return {};
	}
}

// Ripples code borrowed from po3 SplashesofStorms
// https://github.com/powerof3/SplashesOfStorms/blob/master/src/Hooks.cpp under MIT License
namespace Ripples
{
	// Cache settings to avoid repeated singleton access
	static bool s_isEnabled = false;
	static bool s_vanillaRipplesEnabled = false;

	struct ToggleWaterSplashes
	{
		static void thunk(RE::TESWaterSystem* a_waterSystem, bool a_enabled, float a_fadeAmount)
		{
			// Apply our logic only if wetness effects are enabled
			if (s_isEnabled) {
				a_enabled = a_enabled && s_vanillaRipplesEnabled;
			}
			for (auto& waterObject : a_waterSystem->waterObjects) {
				if (waterObject) {
					if (const auto& rippleObject = waterObject->waterRippleObject; rippleObject) {
						rippleObject->SetAppCulled(!a_enabled);
					}
				}
			}

			func(a_waterSystem, a_enabled, a_fadeAmount);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void UpdateSettings()
	{
		auto& rainResponse = globals::pipeline::rainResponse;
		s_isEnabled = rainResponse.settings.EnableRainResponse;
		s_vanillaRipplesEnabled = rainResponse.settings.EnableVanillaRipples;
		logger::debug("[{}] UpdateSettings: EnableRainResponse={}, EnableVanillaRipples={}",
			rainResponse.GetName(), s_isEnabled, s_vanillaRipplesEnabled);
	}

	void Install()
	{
		auto& rainResponse = globals::pipeline::rainResponse;
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(25638, 26179), REL::VariantOffset(0x238, 0x223, 0x238) };
		stl::write_thunk_call<ToggleWaterSplashes>(target.address());
		logger::info("[{}] Installed ripple hooks", rainResponse.GetName());
	}
}

void RainResponse::PostPostLoad()
{
	splashesOfStormsLoaded = static_cast<bool>(GetModuleHandle(L"po3_SplashesOfStorms.dll"));
	if (splashesOfStormsLoaded) {
		logger::info("[{}] Splashes of Storms detected, compatibility enabled", GetName());
		return;
	}

	// Only hook if SoS is not loaded
	Ripples::Install();
}

void RainResponse::DrawSettings()
{
	const auto tooltip = [](const char* text) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", text);
	};

	// Climate Preset Selection - Always visible at the top
	Util::DrawSectionHeader(T(TKEY("climate_presets"), "Climate Presets"), false, false);

	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.3f, 0.4f, 0.6f));    // Subtle blue background
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.45f, 0.8f));  // Slightly darker for button

	// Extract names for combo box
	const char* presetNames[CLIMATE_PRESET_INFO.size()];
	for (size_t i = 0; i < CLIMATE_PRESET_INFO.size(); ++i) {
		presetNames[i] = GetClimatePresetDisplayName(i);
	}
	// Map preset enum to combo index (Custom=0, Legacy=1, Nordic=2, Arctic=3, Coastal=4, Monsoon=5)
	int currentComboIndex = static_cast<int>(climatePreset);

	if (ImGui::Combo(T(TKEY("climate_preset"), "Climate Preset"), &currentComboIndex, presetNames, static_cast<int>(CLIMATE_PRESET_INFO.size()))) {  // Map combo index back to preset enum
		// Simplified: map combo index directly to enum, with bounds check
		ClimatePreset newPreset = (currentComboIndex >= 0 && currentComboIndex < static_cast<int>(CLIMATE_PRESET_INFO.size())) ? static_cast<ClimatePreset>(currentComboIndex) : defaultPreset;

		// Update the preset selection
		climatePreset = newPreset;

		// Apply preset settings (but not for Custom, which just means user-modified)
		if (newPreset != ClimatePreset::Custom) {
			ApplyClimatePreset(newPreset);
		}
	}

	ImGui::PopStyleColor(2);  // Pop both style colors
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (currentComboIndex >= 0 && currentComboIndex < static_cast<int>(CLIMATE_PRESET_INFO.size())) {
			// Handle Custom preset differently
			if (currentComboIndex == 0) {  // Custom preset
				Util::DrawMultiLineTooltip({ T(TKEY("custom_preset_tooltip_0"), "Custom settings - you have modified the preset values."),
					T(TKEY("custom_preset_tooltip_1"), "Select a preset above to apply predefined climate settings.") });
			} else {
				// Build combined description lines for actual presets
				std::vector<const char*> tooltipLines;
				tooltipLines.push_back(GetClimatePresetShortDescription(static_cast<size_t>(currentComboIndex)));
				// Add detailed description
				for (const char* line : GetClimatePresetDetailedDescription(static_cast<size_t>(currentComboIndex))) {
					tooltipLines.push_back(line);
				}
				tooltipLines.push_back(T(TKEY("effects"), "Effects:"));
				// Add effect descriptions
				for (const char* effect : GetClimatePresetEffectDescription(static_cast<size_t>(currentComboIndex))) {
					tooltipLines.push_back(effect);
				}

				std::vector<std::string> tooltipLinesStr;
				tooltipLinesStr.reserve(tooltipLines.size());
				for (const char* line : tooltipLines) {
					tooltipLinesStr.emplace_back(line);
				}
				Util::DrawMultiLineTooltip(tooltipLinesStr);
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("wetness_effects"), "Rain Response"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (Util::UIntCheckbox(T(TKEY("enable_wetness"), "Enable Wetness"), &settings.EnableRainResponse)) {
			Ripples::UpdateSettings();  // Update cache when settings change
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("enable_wetness_tooltip"), "Enables a wetness effect near water and when it is raining."));
		}
		ImGui::SliderFloat(T(TKEY("rain_wetness"), "Rain Wetness"), &settings.MaxRainWetness, 0.0f, 2.5f);
		tooltip("Maximum wet-material response reached during rain. Updates in real time and selecting a value marks the climate preset as Custom.");
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();

		ImGui::SliderFloat(T(TKEY("puddle_wetness"), "Puddle Wetness"), &settings.MaxPuddleWetness, 0.0f, 6.0f);
		tooltip("Maximum accumulated puddle response on sufficiently flat, wet surfaces. Updates in real time.");
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();

		ImGui::SliderFloat(T(TKEY("shore_wetness"), "Shore Wetness"), &settings.MaxShoreWetness, 0.0f, 1.0f);
		tooltip("Maximum wetness applied near bodies of water. Shore Range in Advanced controls how far the effect extends.");
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx("Falling Rain Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox("Enhanced Falling Rain", &settings.EnableRainParticleEnhancement);
		tooltip("Enhances the precipitation geometry supplied by the active Skyrim weather. PIXL keeps the weather/mod texture and emitter, then adds world-stable depth, clumping, gust structure, impact sparkle and atmospheric rain mist.");

		ImGui::BeginDisabled(settings.EnableRainParticleEnhancement == 0);

		ImGui::SeparatorText("Spatial Distribution");
		ImGui::SliderFloat("Clump Strength", &settings.RainClumpStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Breaks uniform precipitation into broad world-space curtains, dense pockets and lighter gaps. The noise is camera-independent so the pattern remains stable while looking around.");
		ImGui::SliderFloat("Clump Size", &settings.RainClumpSize, 400.0f, 6000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({ "World-space size of the broad rain-density cells.", Util::Units::FormatDistance(settings.RainClumpSize) });
		}
		ImGui::SliderFloat("Streak Variation", &settings.RainStreakVariation, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Adds stable per-drop variation to the authored streak length/speed so the rain does not read as one repeated sprite population.");

		ImGui::SeparatorText("Wind & Gust Layers");
		ImGui::SliderFloat("Gust Strength", &settings.RainGustStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Adds intermittent lateral motion on top of the weather emitter velocity. The main rain remains predominantly authored by the game; only coherent subsets are pushed into gust sheets.");
		ImGui::SliderFloat("Gust Frequency", &settings.RainGustFrequency, 0.03f, 0.80f, "%.2f Hz", ImGuiSliderFlags_AlwaysClamp);
		tooltip("How often broad wind pulses pass through the precipitation field.");
		ImGui::SliderFloat("Gust Population", &settings.RainGustChance, 0.0f, 0.75f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Fraction of local rain cells eligible for the stronger sideways gust population. Keeping this below 0.4 preserves a strong downward primary layer.");
		ImGui::SliderFloat("Secondary Angled Layer", &settings.RainSecondaryLayerStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Reuses the active weather rain material to form a faint sheared secondary streak inside gusting particles, producing layered diagonal rain without replacing the weather texture.");

		ImGui::SeparatorText("Depth & Atmosphere");
		ImGui::SliderFloat("Depth Boost Start", &settings.RainDepthStart, 100.0f, 6000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Depth Boost End", &settings.RainDepthEnd, 1000.0f, 20000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		settings.RainDepthEnd = std::max(settings.RainDepthEnd, settings.RainDepthStart + 1.0f);
		tooltip("Distance interval over which PIXL progressively strengthens fine rain visibility so precipitation retains depth instead of collapsing into a near-camera curtain.");
		ImGui::SliderFloat("Distance Visibility", &settings.RainDistanceBoost, 0.0f, 1.5f, "+%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Maximum multiplicative visibility lift applied to distant rain. This does not increase the game emitter count.");
		ImGui::SliderFloat("Impact Micro-Splashes", &settings.RainImpactSplashStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Adds very short diagonal/upward glints when visible rain approaches the scene depth, while also strengthening the procedural surface splash normal response.");
		ImGui::SliderFloat("Rain Mist", &settings.RainMistStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Adds low, clumpy water-aerosol extinction/scattering to PIXL volumetric fog when Atmosphere volumetrics are enabled. The mist is lit by the existing volumetric lighting pipeline rather than drawn as white smoke sprites.");
		ImGui::SliderFloat("Mist Pattern Scale", &settings.RainMistScale, 0.00015f, 0.0030f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
		tooltip("World-space frequency of the rain-mist field. Lower values create broader banks; higher values create smaller turbulent pockets.");
		ImGui::SliderFloat("Mist Vertical Depth", &settings.RainMistHeight, 100.0f, 1800.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		tooltip("Vertical thickness of the low rain-spray layer relative to camera/ground level.");
		ImGui::SliderFloat("Rain Lighting Boost", &settings.RainLightingBoost, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Optical visibility lift for illuminated/backlit rain, biased toward the mid/far field. It scales accumulated scene lighting rather than making rain emissive.");

			ImGui::Checkbox("World-Space Rain Streaks", &g_snowPrecipitation.EnableWorldSpaceRain);
			tooltip("Builds the visible rain streak direction from two projected world-space points instead of allowing the precipitation card to rotate with the camera.");

			ImGui::SeparatorText("Falling Snow Rendering");
			ImGui::Checkbox("Enhanced Falling Snow", &g_snowPrecipitation.EnableSnowEnhancement);
			tooltip("Enhances Skyrim's active vanilla/modded snow emitter and authored flake texture without replacing the weather system.");

			ImGui::BeginDisabled(!g_snowPrecipitation.EnableSnowEnhancement);

			ImGui::SliderFloat("Snow Distance Visibility", &g_snowPrecipitation.SnowDistanceVisibility, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Snow-specific mid/far visibility and stable outer-volume depth boost.");

			ImGui::SliderFloat("Snow Lighting Response", &g_snowPrecipitation.SnowLightingResponse, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Crystalline directional/light readability. This uses scene lighting rather than making flakes emissive.");

			ImGui::SliderFloat("Snow Wind Drift", &g_snowPrecipitation.SnowWindDrift, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Broad world-space drift along Skyrim's active precipitation wind.");

			ImGui::SliderFloat("Snow Flutter", &g_snowPrecipitation.SnowFlutter, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("World-space cross-wind flutter and micro-motion seeded from absolute world position.");

			ImGui::SliderFloat("Snow Density / Depth", &g_snowPrecipitation.SnowDensityBoost, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Recovers subtle authored flake alpha in the distance while preserving transparent texels.");

			ImGui::SliderFloat("Snow Far Volume", &g_snowPrecipitation.SnowFarVolume, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Moves a stable subset of flakes farther into the precipitation volume so distant terrain remains visibly inside snowfall.");

			ImGui::SliderFloat("Snow Tumble", &g_snowPrecipitation.SnowTumble, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("World-space flake tumble; camera rotation only changes projection.");

			ImGui::SliderFloat("Snow Flake Scale", &g_snowPrecipitation.SnowFlakeScale, 0.65f, 1.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SliderFloat("Snow Depth Start", &g_snowPrecipitation.SnowDepthStart, 100.0f, 6000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Snow Depth End", &g_snowPrecipitation.SnowDepthEnd, 3000.0f, 24000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			g_snowPrecipitation.SnowDepthEnd = std::max(
				g_snowPrecipitation.SnowDepthEnd,
				g_snowPrecipitation.SnowDepthStart + 512.0f);
			tooltip("Near/far bounds for snowfall depth visibility.");

			ImGui::EndDisabled();

			ImGui::SeparatorText("Roof / Awning Runoff");
			ImGui::SliderFloat("Roof Runoff Strength", &settings.RainRunoffStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			tooltip("World-space roof/awning runoff. Broad precipitation-blocking roof edges accumulate water, grow/merge beads, release intermittent gravity-driven drops and short heavy-rain streams, and create small terminal splashes.");

			ImGui::SliderFloat("Roof Runoff Distance", &g_roofRunoffDistance, 600.0f, 16000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Maximum camera distance at which roof/awning runoff is detected and rendered. The 1.50 debug view obeys this distance too. Lower it to keep runoff local and reduce the amount of roof geometry participating in the pass.");

		ImGui::EndDisabled();
		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("raindrop_effects"), "Raindrop Effects"), ImGuiTreeNodeFlags_DefaultOpen)) {
		Util::UIntCheckbox(T(TKEY("enable_raindrop_effects"), "Enable Raindrop Effects"), &settings.EnableRaindropFx);
		tooltip("Master switch for procedural splashes and ripples. Updates in real time.");

		ImGui::BeginDisabled(!settings.EnableRaindropFx);

		Util::UIntCheckbox(T(TKEY("enable_splashes"), "Enable Splashes"), &settings.EnableSplashes);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_splashes_tooltip"), "Enables small splashes of wetness on dry surfaces."));
		Util::UIntCheckbox(T(TKEY("enable_ripples"), "Enable Ripples"), &settings.EnableRipples);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("enable_ripples_tooltip"), "Enables circular ripples on puddles, and to a less extent other wet surfaces"));

		ImGui::BeginDisabled(splashesOfStormsLoaded);
		const char* checkboxLabel = splashesOfStormsLoaded ?
		                                T(TKEY("enable_vanilla_ripples_controlled"), "Enable Vanilla Ripples - Controlled by Splashes of Storms") :
		                                T(TKEY("enable_vanilla_ripples"), "Enable Vanilla Ripples");

		if (Util::UIntCheckbox(checkboxLabel, &settings.EnableVanillaRipples)) {
			Ripples::UpdateSettings();  // Update cache when settings change
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawMultiLineTooltip({ T(TKEY("vanilla_ripples_tooltip_0"), "Enables default ripples (e.g., Ripples01)."),
				T(TKEY("vanilla_ripples_tooltip_1"), "Disabling may not take effect until the next weather change.") });
		}
		ImGui::EndDisabled();
		ImGui::SliderFloat(T(TKEY("effect_range"), "Effect Range"), &settings.RaindropFxRange, 1e2f, 2e3f, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto meters = Util::Units::GameUnitsToMeters(settings.RaindropFxRange);
			std::vector<std::string> tooltipLines = {
				T(TKEY("effect_range_tooltip"), "Range for raindrop effects"),
				Util::Units::FormatDistance(settings.RaindropFxRange),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(meters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}
		if (ImGui::TreeNodeEx(T(TKEY("raindrops"), "Raindrops"))) {
			ImGui::BulletText(
				"%s",
				T(TKEY("raindrops_help"), "At every interval, a raindrop is placed within each grid cell.\nOnly a set portion of raindrops will actually trigger splashes and ripples.\n"));

			ImGui::SliderFloat(T(TKEY("grid_size"), "Grid Size"), &settings.RaindropGridSize, 1.0f, 10.0f, "%.1f units");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				std::vector<std::string> tooltipLines = {
					T(TKEY("grid_size_tooltip_0"), "Spatial grid size for raindrop placement (smaller = more grid cells, higher GPU cost)"),
					T(TKEY("grid_size_tooltip_1"), "This is the most performance-sensitive setting. Lower only if needed for realism."),
					Util::Units::FormatDistance(settings.RaindropGridSize)
				};
				Util::DrawMultiLineTooltip(tooltipLines);
			}
			ImGui::SliderFloat(T(TKEY("interval"), "Interval"), &settings.RaindropInterval, 0.1f, 2.0f, "%.1f sec");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("interval_tooltip"), "How often raindrop effects are checked (lower = more frequent, moderate performance impact)"));
			}
			ImGui::SliderFloat(T(TKEY("chance"), "Chance"), &settings.RaindropChance, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("chance_tooltip"), "Portion of raindrops that will actually cause splashes and ripples. Higher values increase effect density but have the least performance impact."));
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx(T(TKEY("splashes"), "Splashes"))) {
			ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.SplashesStrength, 0.f, 2.f, "%.2f");
			tooltip("Intensity of the splash normal/wetness response.");
			ImGui::SliderFloat(T(TKEY("min_radius"), "Min Radius"), &settings.SplashesMinRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("max_radius"), "Max Radius"), &settings.SplashesMaxRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			settings.SplashesMaxRadius = std::max(settings.SplashesMaxRadius, settings.SplashesMinRadius);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("lifetime"), "Lifetime"), &settings.SplashesLifetime, 0.1f, 20.f, "%.1f");
			tooltip("Seconds each splash remains active before fading out.");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx(T(TKEY("ripples"), "Ripples"))) {
			ImGui::SliderFloat(T(TKEY("strength"), "Strength"), &settings.RippleStrength, 0.f, 2.f, "%.2f");
			tooltip("Intensity of the animated ripple displacement/normal response.");
			ImGui::SliderFloat(T(TKEY("radius"), "Radius"), &settings.RippleRadius, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T(TKEY("portion_of_grid_size"), "As portion of grid size."));
			ImGui::SliderFloat(T(TKEY("breadth"), "Breadth"), &settings.RippleBreadth, 0.f, 1.f, "%.2f");
			tooltip("Width of the expanding ripple ring relative to its radius.");
			ImGui::SliderFloat(T(TKEY("lifetime"), "Lifetime"), &settings.RippleLifetime, 0.f, settings.RaindropInterval, "%.2f sec", ImGuiSliderFlags_AlwaysClamp);
			tooltip("Seconds a ripple remains visible. It is bounded by the current raindrop interval to avoid stale overlapping rings.");
			ImGui::TreePop();
		}

		ImGui::EndDisabled();

		ImGui::TreePop();
	}

	ImGui::Spacing();
	ImGui::Spacing();

	if (ImGui::TreeNodeEx(T(TKEY("advanced"), "Advanced"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("weather_transition_speed"), "Weather transition speed"), &settings.WeatherTransitionSpeed, 0.2f, 8.0f);
		if (ImGui::IsItemDeactivatedAfterEdit())
			DetectCurrentPreset();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("weather_transition_speed_tooltip"), "How fast wetness appears when raining and how quickly it dries after rain has stopped."));
		}

		ImGui::SliderFloat(T(TKEY("min_rain_wetness"), "Min Rain Wetness"), &settings.MinRainWetness, 0.0f, 0.9f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("min_rain_wetness_tooltip"), "The minimum amount an object gets wet from rain."));
		}

		ImGui::SliderFloat(T(TKEY("skin_wetness"), "Skin Wetness"), &settings.SkinWetness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("skin_wetness_tooltip"), "How wet character skin and hair get during rain."));
		}
		Util::UIntSlider(T(TKEY("shore_range"), "Shore Range"), &settings.ShoreRange, 1, 64);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto meters = Util::Units::GameUnitsToMeters(static_cast<float>(settings.ShoreRange));
			std::vector<std::string> tooltipLines = {
				T(TKEY("shore_range_tooltip"), "The maximum distance from a body of water that Shore Wetness affects"),
				Util::Units::FormatDistance(static_cast<float>(settings.ShoreRange)),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(meters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}
		ImGui::SliderFloat(T(TKEY("puddle_radius"), "Puddle Radius"), &settings.PuddleRadius, 0.3f, 3.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			auto puddleMeters = Util::Units::GameUnitsToMeters(settings.PuddleRadius);
			std::vector<std::string> tooltipLines = {
				T(TKEY("puddle_radius_tooltip"), "The radius used to determine puddle size and location"),
				Util::Units::FormatDistance(settings.PuddleRadius),
				std::vformat(T(TKEY("meters_format"), "{:.2f} meters"), std::make_format_args(puddleMeters))
			};
			Util::DrawMultiLineTooltip(tooltipLines);
		}

		ImGui::SliderFloat(T(TKEY("puddle_max_angle"), "Puddle Max Angle"), &settings.PuddleMaxAngle, 0.6f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("puddle_max_angle_tooltip"), "How flat a surface needs to be for puddles to form on it."));
		}

		ImGui::SliderFloat(T(TKEY("puddle_min_wetness"), "Puddle Min Wetness"), &settings.PuddleMinWetness, 0.0f, 1.0f);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("puddle_min_wetness_tooltip"), "The wetness value at which puddles start to form."));
		}

		ImGui::TreePop();
	}

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("debug"), "Debug"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("enable_wetness_override"), "Enable Wetness Override"), &debugSettings.EnableWetnessOverride);
		tooltip("Overrides weather-driven surface wetness with the values below for real-time diagnostics. Not intended for normal gameplay.");
		ImGui::Checkbox(T(TKEY("enable_puddle_override"), "Enable Puddle Override"), &debugSettings.EnablePuddleOverride);
		tooltip("Overrides calculated puddle wetness with the diagnostic values below.");
		ImGui::Checkbox(T(TKEY("enable_rain_override"), "Enable Rain Override"), &debugSettings.EnableRainOverride);
		tooltip("Overrides detected rain intensity with the diagnostic values below.");
		ImGui::Checkbox(T(TKEY("enable_interior_exterior_override"), "Enable Interior/Exterior Override"), &debugSettings.EnableIntExOverride);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s", T(TKEY("interior_exterior_override_tooltip"), "If disabled, will only use the exterior value. "));
		}

		if (debugSettings.EnableWetnessOverride) {
			ImGui::SliderFloat2(T(TKEY("wetness_in_exterior"), "Wetness In/Exterior"), &debugSettings.WetnessOverride.x, 0.0f, 2.0f);
		}

		if (debugSettings.EnablePuddleOverride) {
			ImGui::SliderFloat2(T(TKEY("puddle_wetness_in_exterior"), "Puddle Wetness In/Exterior"), &debugSettings.PuddleWetnessOverride.x, 0.0f, 2.0f);
		}

		if (debugSettings.EnableRainOverride) {
			ImGui::SliderFloat2(T(TKEY("rain_in_exterior"), "Rain In/Exterior"), &debugSettings.RainOverride.x, 0.0f, 1.0f);
		}
		ImGui::TreePop();
	}
}

// =====================
// UI/ImGui Helper Functions
// =====================

// Helper for meteorological rain type classification
static void DrawRainTypeLabel(const char* prefix, float rate)
{
	// Meteorological categories (mm/hr):
	// Light: <2.5, Moderate: 2.5-7.5, Heavy: 7.5-15, Extreme: >15
	const char* label = "";
	ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	Util::ColorCodedValueConfig config;
	config.format = "%.2f mm/hr";
	config.sameLine = true;
	config.tooltipText = nullptr;
	config.thresholds = {
		{ 2.5f, ImVec4(0.5f, 0.7f, 1.0f, 1.0f) },    // Light (Blue)
		{ 7.5f, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) },    // Moderate (Green)
		{ 15.0f, ImVec4(1.0f, 0.7f, 0.2f, 1.0f) },   // Heavy (Orange)
		{ FLT_MAX, ImVec4(1.0f, 0.2f, 0.2f, 1.0f) }  // Extreme (Red)
	};
	if (rate < 2.5f) {
		label = "Light Rain";
		valueColor = config.thresholds[0].color;
	} else if (rate < 7.5f) {
		label = "Moderate Rain";
		valueColor = config.thresholds[1].color;
	} else if (rate < 15.0f) {
		label = "Heavy Rain";
		valueColor = config.thresholds[2].color;
	} else {
		label = "Extreme Rain";
		valueColor = config.thresholds[3].color;
	}
	// Print prefix (uncolored), then value (colored), then meteorological label (colored, after value)
	ImGui::Text("%s:", prefix);
	ImGui::SameLine();
	ImGui::TextColored(valueColor, config.format, rate);
	ImGui::SameLine();
	ImGui::TextColored(valueColor, "(%s)", label);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Meteorological rain types:");
		ImGui::BulletText("Light: <2.5 mm/hr");
		ImGui::BulletText("Moderate: 2.5 - 7.5 mm/hr");
		ImGui::BulletText("Heavy: 7.5 - 15 mm/hr");
		ImGui::BulletText("Extreme: >15 mm/hr");
	}
}

// =====================
// Weather/Precipitation Analysis Helpers
// =====================

static float linearstep(float edge0, float edge1, float x)
{
	if (edge0 >= edge1)
		return x >= edge1 ? 1.0f : 0.0f;
	return std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
}

float RainResponse::GetLiveRainIntensity() const
{
	auto* sky = globals::game::sky;
	const bool exterior = sky && sky->mode.get() == RE::Sky::Mode::kFull;

	// Debug override is authoritative. This is deliberately independent of the
	// surface-wetness feature so CameraSuite/Stormglass can always be diagnosed.
	if (debugSettings.EnableRainOverride) {
		const float overrideValue = exterior ? debugSettings.RainOverride.y :
			(debugSettings.EnableIntExOverride ? debugSettings.RainOverride.x : debugSettings.RainOverride.y);
		return std::clamp(overrideValue, 0.0f, 1.0f);
	}

	if (!exterior)
		return 0.0f;

	// The most reliable indication of *visible* rain is the precipitation
	// geometry/emitter Skyrim is actually rendering. Do not require weather
	// particle-density metadata to decide whether rain exists: modded weathers
	// can have unusual/missing density data while still rendering a rain emitter.
	auto hasRainEmitter = [](const RE::NiPointer<RE::BSGeometry>& precipObject) -> bool {
		if (!precipObject)
			return false;

		auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
		auto* particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(effect.get());
		if (!particleShaderProperty || !particleShaderProperty->particleEmitter)
			return false;

		return particleShaderProperty->particleEmitter->emitterType.any(
			RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain);
	};

	auto weatherDensity = [](RE::TESWeather* weather) -> float {
		if (!weather || !weather->precipitationData)
			return 0.0f;

		const float density = weather->precipitationData->GetSettingValue(
			RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
		return std::clamp(density / MAX_RAIN_PARTICLE_DENSITY, 0.0f, 1.0f);
	};

	bool currentEmitterRain = false;
	bool lastEmitterRain = false;
	if (auto precip = sky->precip) {
		currentEmitterRain = hasRainEmitter(precip->currentPrecip);
		lastEmitterRain = hasRainEmitter(precip->lastPrecip);
	}

	// Build the trigger from three independent Skyrim signals. This deliberately
	// avoids making any one reverse-engineered field authoritative:
	//   1) Sky::IsRaining() -- engine-level weather state
	//   2) live rain particle emitter -- what is actually being rendered
	//   3) TESWeather::kRainy -- authored weather classification
	// The third signal is important for modded weather stacks where precipitation
	// geometry can be created/retired at a different point in the frame.
	const float weatherPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
	const bool currentWeatherRain = sky->currentWeather &&
		sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
		weatherPct > 0.015f;
	const bool lastWeatherRain = sky->lastWeather &&
		sky->lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
		weatherPct < 0.985f;

	const bool engineRain = sky->IsRaining();
	const bool visibleRain = engineRain || currentEmitterRain || lastEmitterRain ||
		currentWeatherRain || lastWeatherRain;
	if (!visibleRain)
		return 0.0f;

	const float currentBase = weatherDensity(sky->currentWeather);
	const float lastBase = weatherDensity(sky->lastWeather);

	// Density is still useful for *strength*, but not for the on/off decision.
	// Use straightforward transition blending and then impose a minimum optical
	// response whenever rain is visibly being rendered. This keeps light/modded
	// rain readable on the lens instead of resolving to an almost-zero signal.
	float densitySignal = std::lerp(lastBase, currentBase, weatherPct);

	// Prefer the source that is actually active. Weather classification is also
	// allowed to supply transition strength when modded precipitation metadata has
	// zero/missing density. The minimum floor is applied only after positive rain
	// classification, so clear/snow weather cannot accidentally activate the lens.
	if (currentEmitterRain && !lastEmitterRain)
		densitySignal = currentBase;
	else if (lastEmitterRain && !currentEmitterRain)
		densitySignal = lastBase;

	float transitionPresence = 0.0f;
	if (currentWeatherRain)
		transitionPresence = std::max(transitionPresence, weatherPct);
	if (lastWeatherRain)
		transitionPresence = std::max(transitionPresence, 1.0f - weatherPct);

	constexpr float kVisibleRainFloor = 0.42f;
	float intensity = std::max(densitySignal, kVisibleRainFloor * std::max(transitionPresence, 0.65f));

	// Perceptual remap: preserve 1.0 for heavy rain while lifting light/moderate
	// rain into the useful range for procedural lens droplets.
	intensity = std::pow(std::clamp(intensity, 0.0f, 1.0f), 0.72f);
	return std::clamp(intensity, 0.0f, 1.0f);
}

float RainResponse::GetRainIntensity(RE::NiPointer<RE::BSGeometry> precipObject, RE::TESWeather* weather)
{
	if (!precipObject || !weather || !weather->precipitationData) {
		return 0.0f;
	}

	auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
	auto shaderProp = effect.get();
	auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);

	if (!particleShaderProperty || !particleShaderProperty->particleEmitter) {
		return 0.0f;
	}

	auto rain = (RE::BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);
	if (!rain->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain)) {
		return 0.0f;
	}

	auto maxDensity = weather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;  // Use weather particle density as authoritative source for rain intensity
	// This provides consistent intensity scaling based on weather type (1-3 scale)
	// Note: rain->density equals maxDensity when fully active
	return (maxDensity > 0.0f) ? std::min(1.0f, maxDensity / MAX_RAIN_PARTICLE_DENSITY) : 0.0f;
}

RainResponse::WeatherWetnessResult RainResponse::CalculateWeatherWetness(RE::TESWeather* weather, float weatherPct, bool isCurrentWeather) const
{
	WeatherWetnessResult result{};

	if (!weather || !weather->precipitationData || !weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy)) {
		return result;
	}

	if (isCurrentWeather) {
		// Current weather uses fade-in logic
		float fadeValue = weather->data.precipitationBeginFadeIn;
		float fadeNormalized = fadeValue / 255.0f;
		float fadeThreshold = 255.0f * (1.0f - fadeNormalized);
		float weatherProgress = weatherPct * 255.0f;

		if (fadeNormalized == 0.0f) {
			// No fade-in period, use immediate wetness
			result.wetness = (weatherPct > 0.1f) ? 1.0f : 0.0f;
		} else {
			result.wetness = linearstep(fadeThreshold, 255.0f, weatherProgress);
		}
		result.puddleWetness = pow(result.wetness, 2.0f);
	} else {
		// Last weather uses fade-out logic
		float fadeValue = weather->data.precipitationEndFadeOut;
		float fadeNormalized = fadeValue / 255.0f;
		float fadeThreshold = 255.0f * fadeNormalized;
		float weatherProgress = weatherPct * 255.0f;

		result.wetness = 1.0f - linearstep(fadeThreshold, 255.0f, weatherProgress);
		result.puddleWetness = pow(std::max(result.wetness, 1.0f - weatherPct), 0.25f);
	}
	return result;
}

// Helper function to calculate precipitation rate from shader data and settings
float RainResponse::CalculatePrecipitationRate(float raindropChance, float raindropGridSizeGameUnits, float raindropIntervalSeconds, float mlPerDrop) const
{
	// Validate inputs to prevent division by zero and invalid calculations
	if (raindropGridSizeGameUnits <= 0.0f || raindropIntervalSeconds <= 0.0f) {
		logger::warn("[RainResponse] Invalid parameters: gridSize={}, interval={}", raindropGridSizeGameUnits, raindropIntervalSeconds);
		return 0.0f;
	}

	if (raindropChance < 0.0f || raindropChance > 1.0f) {
		logger::warn("[RainResponse] Invalid raindrop chance: {}, clamping to [0,1]", raindropChance);
		raindropChance = std::clamp(raindropChance, 0.0f, 1.0f);
	}
	// Use physically realistic default if not specified (10 microliters typical for large raindrop)
	if (mlPerDrop <= 0.0f)
		mlPerDrop = 0.01f;
	// Convert grid size from game units to meters
	float gridSizeMeters = Util::Units::GameUnitsToMeters(raindropGridSizeGameUnits);
	float gridAreaSqMeters = gridSizeMeters * gridSizeMeters;
	// Calculate drops per second per grid cell
	float dropsPerSecond = raindropChance / raindropIntervalSeconds;
	float dropsPerSqMeterPerSec = dropsPerSecond / gridAreaSqMeters;
	// Convert to equivalent mm/hr precipitation rate
	// 1 mm/hr = 1 L/m^2/hr = 1 mL/cm^2/hr = 1 mm depth
	const float SEC_PER_HOUR = 3600.0f;
	const float SQ_M_TO_SQ_CM = 10000.0f;
	return dropsPerSqMeterPerSec * mlPerDrop * SEC_PER_HOUR / SQ_M_TO_SQ_CM;
}

// =====================
// Preset/Configuration Helpers
// =====================

const RainResponse::ClimateSettings& RainResponse::GetClimateSettings(ClimatePreset preset)
{
	auto index = static_cast<size_t>(preset);
	if (index >= CLIMATE_PRESETS.size()) {
		index = magic_enum::enum_integer<ClimatePreset>(defaultPreset);  // Use defaultPreset
	}
	return CLIMATE_PRESETS[index];
}

void RainResponse::ApplyClimatePreset(ClimatePreset preset)
{
	const auto& climate = GetClimateSettings(preset);

	// Update the climate preset
	climatePreset = preset;

	// Set settings to preset base values instead of multiplying existing values
	// This ensures consistent, predictable behavior
	Settings defaultSettings{};  // Get default values

	settings.MaxRainWetness = defaultSettings.MaxRainWetness * climate.wetnessMultiplier;
	settings.MaxPuddleWetness = defaultSettings.MaxPuddleWetness * climate.puddleMultiplier;
	settings.WeatherTransitionSpeed = defaultSettings.WeatherTransitionSpeed * climate.transitionSpeed;

	// Use the preset's raindrop chance, grid size, and interval as the base values
	settings.RaindropChance = climate.raindropChance;
	settings.RaindropGridSize = climate.raindropGridSize;
	settings.RaindropInterval = climate.raindropInterval;

	// Removed clamping for all settings to allow full preset range
}

RainResponse::PerFrame RainResponse::GetCommonBufferData() const
{
	// GetPipelineBufferData and the final presentation pass can both request
	// weather data during one rendered frame. A render-thread-local cache avoids
	// advancing the rain timer twice without changing RainResponse's object ABI.
	static thread_local PerFrame cachedData{};
	static thread_local const RainResponse* cachedOwner = nullptr;
	static thread_local std::uint32_t cachedFrameIndex = 0;
	static thread_local bool cacheValid = false;
	const std::uint32_t frameIndex = globals::state ? globals::state->frameCount : 0u;
	if (cacheValid && cachedOwner == this && cachedFrameIndex == frameIndex)
		return cachedData;

	PerFrame data{};

	data.Raining = GetLiveRainIntensity();
	data.Wetness = 0.0f;
	data.PuddleWetness = 0.0f;

	if (settings.EnableRainResponse) {
		if (auto sky = globals::game::sky) {
			if (sky->mode.get() == RE::Sky::Mode::kFull) {
				if (auto precip = sky->precip) {
					{
						auto precipObject = precip->currentPrecip;
						if (!precipObject)
							precipObject = precip->lastPrecip;
						if (precipObject) {
							auto& effect = precipObject->GetGeometryRuntimeData().shaderProperty;
							auto* particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(effect.get());
							if (particleShaderProperty && particleShaderProperty->particleEmitter &&
								particleShaderProperty->particleEmitter->emitterType.any(RE::BSParticleShaderEmitter::EMITTER_TYPE::kRain)) {
								auto* rain = static_cast<RE::BSParticleShaderRainEmitter*>(particleShaderProperty->particleEmitter);
								data.OcclusionViewProj = rain->occlusionProjection;
							}
						}
					}

					// Live rain intensity is resolved independently above so material wetness
					// and CameraSuite share one authoritative precipitation signal.
				}

				WeatherWetnessResult currentWeatherResult = CalculateWeatherWetness(sky->currentWeather, sky->currentWeatherPct, true);
				WeatherWetnessResult lastWeatherResult = CalculateWeatherWetness(sky->lastWeather, sky->currentWeatherPct, false);
				float combinedWetness = std::min(1.0f, currentWeatherResult.wetness + lastWeatherResult.wetness);
				float combinedPuddleWetness = std::min(1.0f, currentWeatherResult.puddleWetness + lastWeatherResult.puddleWetness);
				data.Wetness = combinedWetness;
				data.PuddleWetness = combinedPuddleWetness;
				if (debugSettings.EnableWetnessOverride) {
					data.Wetness = debugSettings.WetnessOverride.y;
				}
				if (debugSettings.EnablePuddleOverride) {
					data.PuddleWetness = debugSettings.PuddleWetnessOverride.y;
				}
			} else {
				if (debugSettings.EnableWetnessOverride) {
					data.Wetness = debugSettings.EnableIntExOverride ? debugSettings.WetnessOverride.x : debugSettings.WetnessOverride.y;
				}
				if (debugSettings.EnablePuddleOverride) {
					data.PuddleWetness = debugSettings.EnableIntExOverride ? debugSettings.PuddleWetnessOverride.x : debugSettings.PuddleWetnessOverride.y;
				}
			}
		}
	}

	static size_t rainTimer = 0;  // size_t for precision
	if (!globals::game::ui->GameIsPaused())
		rainTimer += (size_t)(RE::GetSecondsSinceLastFrame() * 1000);  // BSTimer::delta is always 0 for some reason
	data.Time = rainTimer / 1000.f;

	data.settings = settings;
	data.settings.MaxShoreWetness = settings.EnableRainResponse ? settings.MaxShoreWetness : 0.0f;
	data.settings.RaindropChance *= data.Raining * data.Raining;

	// These members are uploaded in reciprocal/preprocessed form to match the
	// long-standing HLSL aliases. Clamp UI/JSON input here so malformed presets
	// cannot introduce INF/NaN into every material using SharedData.
	const float safeGridSize = std::max(settings.RaindropGridSize, 1e-4f);
	const float safeInterval = std::max(settings.RaindropInterval, 1e-4f);
	const float safeRippleLifetime = std::max(settings.RippleLifetime, 1e-4f);
	data.settings.RaindropGridSize = 1.0f / safeGridSize;
	data.settings.RaindropInterval = 1.0f / safeInterval;
	data.settings.RippleLifetime = safeInterval / safeRippleLifetime;

	// Keep the new precipitation controls numerically sane even when an older or
	// hand-edited preset supplies invalid values. Raining intentionally remains
	// independent of EnableRainResponse so every PIXL consumer sees one live signal.
	data.settings.EnableRainParticleEnhancement = settings.EnableRainParticleEnhancement ? 1u : 0u;
	data.settings.RainClumpStrength = std::max(settings.RainClumpStrength, 0.0f);
	data.settings.RainClumpSize = std::max(settings.RainClumpSize, 128.0f);
	data.settings.RainStreakVariation = std::max(settings.RainStreakVariation, 0.0f);
	data.settings.RainGustStrength = std::max(settings.RainGustStrength, 0.0f);
	data.settings.RainGustFrequency = std::max(settings.RainGustFrequency, 0.001f);
	data.settings.RainGustChance = std::clamp(settings.RainGustChance, 0.0f, 1.0f);
	data.settings.RainSecondaryLayerStrength = std::max(settings.RainSecondaryLayerStrength, 0.0f);
	data.settings.RainDepthStart = std::max(settings.RainDepthStart, 0.0f);
	data.settings.RainDepthEnd = std::max(settings.RainDepthEnd, data.settings.RainDepthStart + 1.0f);
	data.settings.RainDistanceBoost = std::max(settings.RainDistanceBoost, 0.0f);
	data.settings.RainImpactSplashStrength = std::max(settings.RainImpactSplashStrength, 0.0f);
	data.settings.RainMistStrength = std::max(settings.RainMistStrength, 0.0f);
	data.settings.RainMistScale = std::max(settings.RainMistScale, 1e-6f);
	data.settings.RainMistHeight = std::max(settings.RainMistHeight, 64.0f);
	data.settings.RainLightingBoost = std::max(settings.RainLightingBoost, 0.0f);
	data.settings.RainRunoffStrength = std::max(settings.RainRunoffStrength, 0.0f);

	cachedOwner = this;
	cachedFrameIndex = frameIndex;
	cacheValid = true;
	cachedData = data;
	return data;
}

void RainResponse::Prepass()
{
	static auto renderer = globals::game::renderer;
	static auto& precipOcclusionTexture =
		renderer->GetDepthStencilData().depthStencils[
			RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];

	auto* context = globals::d3d::context;
	context->PSSetShaderResources(70, 1, &precipOcclusionTexture.depthSRV);

	if (!g_worldPrecipitationCB) {
		g_worldPrecipitationCB = std::make_unique<ConstantBuffer>(
			ConstantBufferDesc<WorldPrecipitationTuning>(),
			"RainResponse::WorldPrecipitationTuning");
	}

	WorldPrecipitationTuning tuning{};
	tuning.EnableWorldSpaceRain =
		g_snowPrecipitation.EnableWorldSpaceRain ? 1u : 0u;
	tuning.EnableSnowEnhancement =
		g_snowPrecipitation.EnableSnowEnhancement ? 1u : 0u;
	tuning.Snowing = ResolveLiveSnowIntensity();
	tuning.SnowDistanceVisibility =
		std::clamp(g_snowPrecipitation.SnowDistanceVisibility, 0.0f, 1.5f);
	tuning.SnowLightingResponse =
		std::clamp(g_snowPrecipitation.SnowLightingResponse, 0.0f, 1.5f);
	tuning.SnowWindDrift =
		std::clamp(g_snowPrecipitation.SnowWindDrift, 0.0f, 2.0f);
	tuning.SnowFlutter =
		std::clamp(g_snowPrecipitation.SnowFlutter, 0.0f, 2.0f);
	tuning.SnowDensityBoost =
		std::clamp(g_snowPrecipitation.SnowDensityBoost, 0.0f, 1.5f);
	tuning.SnowDepthStart =
		std::max(g_snowPrecipitation.SnowDepthStart, 100.0f);
	tuning.SnowDepthEnd =
		std::max(
			g_snowPrecipitation.SnowDepthEnd,
			tuning.SnowDepthStart + 512.0f);
	tuning.SnowFarVolume =
		std::clamp(g_snowPrecipitation.SnowFarVolume, 0.0f, 1.5f);
	tuning.SnowTumble =
		std::clamp(g_snowPrecipitation.SnowTumble, 0.0f, 1.5f);
	tuning.SnowFlakeScale =
		std::clamp(g_snowPrecipitation.SnowFlakeScale, 0.65f, 1.50f);
	tuning.SnowWorldScale =
		std::max(g_snowPrecipitation.SnowWorldScale, 256.0f);

	g_worldPrecipitationCB->Update(tuning);
	ID3D11Buffer* buffer = g_worldPrecipitationCB->CB();

	// Particle precipitation consumes this on VS/PS. GroundResponse's b13 terrain
	// replay and roof runoff's CS b13 occur on different draw/stage contexts.
	context->VSSetConstantBuffers(13, 1, &buffer);
	context->PSSetConstantBuffers(13, 1, &buffer);
}


void RainResponse::EnsureRoofRunoffResources(uint32_t a_width, uint32_t a_height)
{
	const uint32_t edgeWidth = std::max(1u, (a_width + 1u) / 2u);
	const uint32_t edgeHeight = std::max(1u, (a_height + 1u) / 2u);

	const bool valid =
		roofRunoffEdgeMask &&
		g_roofRunoffStateA &&
		g_roofRunoffStateB &&
		g_roofRunoffDropMask &&
		roofRunoffEdgeMask->desc.Width == edgeWidth &&
		roofRunoffEdgeMask->desc.Height == edgeHeight &&
		g_roofRunoffStateA->desc.Width == edgeWidth &&
		g_roofRunoffStateA->desc.Height == edgeHeight &&
		g_roofRunoffStateB->desc.Width == edgeWidth &&
		g_roofRunoffStateB->desc.Height == edgeHeight &&
		g_roofRunoffDropMask->desc.Width == a_width &&
		g_roofRunoffDropMask->desc.Height == a_height;

	if (valid)
		return;

	auto createFloatTexture = [&](uint32_t a_texWidth,
								  uint32_t a_texHeight,
								  DXGI_FORMAT a_format,
								  const char* a_name) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_texWidth;
		desc.Height = a_texHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = a_format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		auto texture = std::make_unique<Texture2D>(desc, a_name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = a_format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		texture->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = a_format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		texture->CreateUAV(uavDesc);
		return texture;
	};

	// R16G16: current-frame roof edge strength + source device depth.
	roofRunoffEdgeMask = createFloatTexture(
		edgeWidth,
		edgeHeight,
		DXGI_FORMAT_R16G16_FLOAT,
		"RainResponse::RoofRunoffEdgeMask");

	// RGBA16F state:
	// x = accumulated water, y = stable world-space seed,
	// z = current source depth, w = current roof-edge strength.
	g_roofRunoffStateA = createFloatTexture(
		edgeWidth,
		edgeHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		"RainResponse::RoofRunoffStateA");
	g_roofRunoffStateB = createFloatTexture(
		edgeWidth,
		edgeHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		"RainResponse::RoofRunoffStateB");

	// Full-resolution integer mask accepts collision-safe InterlockedMax writes from
	// sparse world-space droplets.  This keeps drops one pixel wide by default and
	// avoids unordered float blending races when several roof emitters overlap.
	g_roofRunoffDropMask = createFloatTexture(
		a_width,
		a_height,
		DXGI_FORMAT_R32_UINT,
		"RainResponse::RoofRunoffDropMask");

	g_roofRunoffStateFlip = false;

	const float clearFloat[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
	const UINT clearUint[4]{ 0u, 0u, 0u, 0u };
	auto* context = globals::d3d::context;
	context->ClearUnorderedAccessViewFloat(roofRunoffEdgeMask->uav.get(), clearFloat);
	context->ClearUnorderedAccessViewFloat(g_roofRunoffStateA->uav.get(), clearFloat);
	context->ClearUnorderedAccessViewFloat(g_roofRunoffStateB->uav.get(), clearFloat);
	context->ClearUnorderedAccessViewUint(g_roofRunoffDropMask->uav.get(), clearUint);

	logger::info(
		"[RainResponse] Phase 5 world-space runoff buffers: edge={}x{}, dropMask={}x{}",
		edgeWidth,
		edgeHeight,
		a_width,
		a_height);
}

ID3D11ComputeShader* RainResponse::GetRoofRunoffDetectCS()
{
	if (!roofRunoffDetectAttempted) {
		roofRunoffDetectAttempted = true;
		auto* compiled = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(
				L"Data\\Shaders\\RainResponse\\RoofRunoffDetectCS.hlsl",
				{},
				"cs_5_0"));
		if (compiled) {
			roofRunoffDetectCS.attach(compiled);
			logger::info("[RainResponse] RoofRunoffDetectCS compiled successfully");
		} else {
			logger::error("[RainResponse] RoofRunoffDetectCS compilation failed; roof runoff disabled for this session");
		}
	}
	return roofRunoffDetectCS.get();
}

ID3D11ComputeShader* RainResponse::GetRoofRunoffResolveCS()
{
	if (!roofRunoffResolveAttempted) {
		roofRunoffResolveAttempted = true;
		auto* compiled = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(
				L"Data\\Shaders\\RainResponse\\RoofRunoffResolveCS.hlsl",
				{},
				"cs_5_0"));
		if (compiled) {
			roofRunoffResolveCS.attach(compiled);
			logger::info("[RainResponse] RoofRunoffResolveCS compiled successfully");
		} else {
			logger::error("[RainResponse] RoofRunoffResolveCS compilation failed; roof runoff disabled for this session");
		}
	}
	return roofRunoffResolveCS.get();
}

void RainResponse::ClearShaderCache()
{
	roofRunoffDetectCS = nullptr;
	roofRunoffResolveCS = nullptr;
	roofRunoffDetectAttempted = false;
	roofRunoffResolveAttempted = false;

	g_roofRunoffGenerateCS = nullptr;
	g_roofRunoffCompositeCS = nullptr;
	g_roofRunoffGenerateAttempted = false;
	g_roofRunoffCompositeAttempted = false;

	logger::info("[RainResponse] Released direct roof-runoff kernels for selective hot reload");
}

void RainResponse::DrawRoofRunoff()
{
	if (!loaded ||
		!settings.EnableRainResponse ||
		!settings.EnableRainParticleEnhancement ||
		settings.RainRunoffStrength <= 0.0f ||
		Util::IsInterior()) {
		return;
	}

	const float rain = GetLiveRainIntensity();
	if (rain <= 0.015f)
		return;

	auto* renderer = globals::game::renderer;
	auto* context = globals::d3d::context;
	auto* state = globals::state;
	auto* deferred = globals::deferred;
	if (!renderer || !context || !state || !deferred)
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[deferred->forwardRenderTargets[0]];
	if (!main.texture || !main.UAV)
		return;

	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);
	if (mainDesc.Width == 0 || mainDesc.Height == 0)
		return;

	// Skyrim allocates scene targets at display size, but DLSS/FSR render only into
	// the active dynamic-resolution rectangle. Roof runoff must be detected,
	// projected and composited in that same coordinate system before upscaling.
	const float2 activeSizeF = Util::ConvertToDynamic(
		float2{ static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) },
		true);
	const uint32_t activeWidth = std::clamp(
		static_cast<uint32_t>(activeSizeF.x), 1u, mainDesc.Width);
	const uint32_t activeHeight = std::clamp(
		static_cast<uint32_t>(activeSizeF.y), 1u, mainDesc.Height);

	EnsureRoofRunoffResources(activeWidth, activeHeight);
	if (!roofRunoffEdgeMask || !g_roofRunoffStateA || !g_roofRunoffStateB ||
		!g_roofRunoffDropMask ||
		!roofRunoffEdgeMask->srv || !roofRunoffEdgeMask->uav ||
		!g_roofRunoffStateA->srv || !g_roofRunoffStateA->uav ||
		!g_roofRunoffStateB->srv || !g_roofRunoffStateB->uav ||
		!g_roofRunoffDropMask->srv || !g_roofRunoffDropMask->uav)
		return;

	auto& precipDepth =
		renderer->GetDepthStencilData().depthStencils[
			RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];

	ID3D11ShaderResourceView* sceneDepth = Util::GetCurrentSceneDepthSRV(false);
	ID3D11ShaderResourceView* precipitationDepth = precipDepth.depthSRV;
	if (!sceneDepth || !precipitationDepth)
		return;

	ID3D11ComputeShader* detectCS = GetRoofRunoffDetectCS();
	ID3D11ComputeShader* accumulateCS = GetRoofRunoffResolveCS();
	ID3D11ComputeShader* generateCS = GetRoofRunoffGenerateCS();
	ID3D11ComputeShader* compositeCS = GetRoofRunoffCompositeCS();
	if (!detectCS || !accumulateCS || !generateCS || !compositeCS)
		return;

	if (!g_roofRunoffTuningCB) {
		g_roofRunoffTuningCB = std::make_unique<ConstantBuffer>(
			ConstantBufferDesc<RoofRunoffTuning>(),
			"RainResponse::RoofRunoffTuning");
	}

	RoofRunoffTuning runoffTuning{};
	runoffTuning.MaxDistance = std::clamp(g_roofRunoffDistance, 600.0f, 16000.0f);
	runoffTuning.NearSizeDistance = std::clamp(runoffTuning.MaxDistance * 0.34f, 850.0f, 2200.0f);
	runoffTuning.EmitterSpacing = 92.0f;
	runoffTuning.RenderSize = {
		static_cast<float>(activeWidth),
		static_cast<float>(activeHeight)
	};
	runoffTuning.InvRenderSize = {
		1.0f / static_cast<float>(activeWidth),
		1.0f / static_cast<float>(activeHeight)
	};
	g_roofRunoffTuningCB->Update(runoffTuning);

	ID3D11Buffer* runoffTuningBuffer = g_roofRunoffTuningCB->CB();
	context->CSSetConstantBuffers(13, 1, &runoffTuningBuffer);

	ID3D11Buffer* sharedBuffers[2]{
		state->sharedDataCB->CB(),
		state->featureDataCB->CB()
	};
	context->CSSetConstantBuffers(5, 2, sharedBuffers);

	ID3D11Buffer* frameBuffer = *globals::game::perFrame.get();
	context->CSSetConstantBuffers(12, 1, &frameBuffer);

	const uint32_t edgeGX = (roofRunoffEdgeMask->desc.Width + 7u) / 8u;
	const uint32_t edgeGY = (roofRunoffEdgeMask->desc.Height + 7u) / 8u;

	// Pass 1: strict roof/awning edge detection.  Phase 5 deliberately requires
	// broad precipitation-blocking geometry; the old generic geometry fallback is
	// removed so grass, characters and narrow props do not become water emitters.
	{
		ID3D11ShaderResourceView* srvs[2]{ sceneDepth, precipitationDepth };
		context->CSSetShaderResources(0, 2, srvs);

		ID3D11UnorderedAccessView* uav = roofRunoffEdgeMask->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(detectCS, nullptr, 0);
		context->Dispatch(edgeGX, edgeGY, 1);

		ID3D11ShaderResourceView* nullSrvs[2]{ nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetShaderResources(0, 2, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
	}

	Texture2D* previousState =
		g_roofRunoffStateFlip ? g_roofRunoffStateB.get() : g_roofRunoffStateA.get();
	Texture2D* nextState =
		g_roofRunoffStateFlip ? g_roofRunoffStateA.get() : g_roofRunoffStateB.get();

	// Pass 2: accumulate water at the current roof lip. Previous accumulation is
	// reprojected from the current WORLD position through the previous camera, not
	// copied from the same screen pixel. Camera rotation/translation therefore does
	// not drag the reservoir state around with the viewport.
	{
		ID3D11ShaderResourceView* srvs[2]{
			roofRunoffEdgeMask->srv.get(),
			previousState->srv.get()
		};
		context->CSSetShaderResources(0, 2, srvs);

		ID3D11UnorderedAccessView* uav = nextState->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(accumulateCS, nullptr, 0);
		context->Dispatch(edgeGX, edgeGY, 1);

		ID3D11ShaderResourceView* nullSrvs[2]{ nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetShaderResources(0, 2, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
	}

	// Pass 3: sparse scatter from world-space roof anchors. A full-res R32_UINT mask
	// uses InterlockedMax, so beads remain thin and overlapping emitters are safe.
	{
		const UINT clearUint[4]{ 0u, 0u, 0u, 0u };
		context->ClearUnorderedAccessViewUint(g_roofRunoffDropMask->uav.get(), clearUint);

		ID3D11ShaderResourceView* srvs[3]{
			roofRunoffEdgeMask->srv.get(),
			nextState->srv.get(),
			sceneDepth
		};
		context->CSSetShaderResources(0, 3, srvs);

		ID3D11UnorderedAccessView* uav = g_roofRunoffDropMask->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(generateCS, nullptr, 0);
		context->Dispatch(edgeGX, edgeGY, 1);

		ID3D11ShaderResourceView* nullSrvs[3]{ nullptr, nullptr, nullptr };
		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetShaderResources(0, 3, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
	}

	// Pass 4: cheap O(1) full-resolution composite. Roof Runoff Strength only
	// changes optical strength; it no longer increases search radius/density/work.
	{
		ID3D11ShaderResourceView* srv = g_roofRunoffDropMask->srv.get();
		context->CSSetShaderResources(0, 1, &srv);

		ID3D11UnorderedAccessView* uav = main.UAV;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(compositeCS, nullptr, 0);

		const uint32_t gx = (activeWidth + 7u) / 8u;
		const uint32_t gy = (activeHeight + 7u) / 8u;
		context->Dispatch(gx, gy, 1);

		ID3D11ShaderResourceView* nullSrv = nullptr;
		ID3D11UnorderedAccessView* nullUav = nullptr;
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetShaderResources(0, 1, &nullSrv);
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		context->CSSetConstantBuffers(12, 1, &nullBuffer);
		context->CSSetConstantBuffers(13, 1, &nullBuffer);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	g_roofRunoffStateFlip = !g_roofRunoffStateFlip;
}

void RainResponse::LoadSettings(json& o_json)
{
	settings = o_json;
	g_roofRunoffDistance = std::clamp(
		o_json.value("RainRunoffDistance", 5200.0f),
		600.0f,
		16000.0f);

	if (o_json.contains("SnowPrecipitation") &&
		o_json["SnowPrecipitation"].is_object()) {
		const auto& snow = o_json["SnowPrecipitation"];

		g_snowPrecipitation.EnableWorldSpaceRain =
			snow.value("WorldSpaceRain", true);
		g_snowPrecipitation.EnableSnowEnhancement =
			snow.value("Enable", true);
		g_snowPrecipitation.SnowDistanceVisibility =
			std::clamp(snow.value("DistanceVisibility", 0.90f), 0.0f, 1.5f);
		g_snowPrecipitation.SnowLightingResponse =
			std::clamp(snow.value("LightingResponse", 0.75f), 0.0f, 1.5f);
		g_snowPrecipitation.SnowWindDrift =
			std::clamp(snow.value("WindDrift", 0.90f), 0.0f, 2.0f);
		g_snowPrecipitation.SnowFlutter =
			std::clamp(snow.value("Flutter", 0.80f), 0.0f, 2.0f);
		g_snowPrecipitation.SnowDensityBoost =
			std::clamp(snow.value("DensityBoost", 0.55f), 0.0f, 1.5f);
		g_snowPrecipitation.SnowDepthStart =
			std::max(snow.value("DepthStart", 700.0f), 100.0f);
		g_snowPrecipitation.SnowDepthEnd =
			std::max(
				snow.value("DepthEnd", 14000.0f),
				g_snowPrecipitation.SnowDepthStart + 512.0f);
		g_snowPrecipitation.SnowFarVolume =
			std::clamp(snow.value("FarVolume", 0.75f), 0.0f, 1.5f);
		g_snowPrecipitation.SnowTumble =
			std::clamp(snow.value("Tumble", 0.70f), 0.0f, 1.5f);
		g_snowPrecipitation.SnowFlakeScale =
			std::clamp(snow.value("FlakeScale", 1.00f), 0.65f, 1.50f);
		g_snowPrecipitation.SnowWorldScale =
			std::max(snow.value("WorldScale", 1800.0f), 256.0f);
	}

	// Auto-detect which preset matches the loaded settings
	DetectCurrentPreset();

	Ripples::UpdateSettings();  // Sync cached values after loading

	if (o_json.contains("DebugSettings")) {
		debugSettings = o_json["DebugSettings"].get<DebugSettings>();
	}
}

void RainResponse::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["RainRunoffDistance"] = g_roofRunoffDistance;
	o_json["SnowPrecipitation"] = {
		{ "WorldSpaceRain", g_snowPrecipitation.EnableWorldSpaceRain },
		{ "Enable", g_snowPrecipitation.EnableSnowEnhancement },
		{ "DistanceVisibility", g_snowPrecipitation.SnowDistanceVisibility },
		{ "LightingResponse", g_snowPrecipitation.SnowLightingResponse },
		{ "WindDrift", g_snowPrecipitation.SnowWindDrift },
		{ "Flutter", g_snowPrecipitation.SnowFlutter },
		{ "DensityBoost", g_snowPrecipitation.SnowDensityBoost },
		{ "DepthStart", g_snowPrecipitation.SnowDepthStart },
		{ "DepthEnd", g_snowPrecipitation.SnowDepthEnd },
		{ "FarVolume", g_snowPrecipitation.SnowFarVolume },
		{ "Tumble", g_snowPrecipitation.SnowTumble },
		{ "FlakeScale", g_snowPrecipitation.SnowFlakeScale },
		{ "WorldScale", g_snowPrecipitation.SnowWorldScale }
	};

	o_json["DebugSettings"] = debugSettings;
}

void RainResponse::RestoreDefaultSettings()
{
	settings = {};
	g_roofRunoffDistance = 5200.0f;
	g_snowPrecipitation = {};
	climatePreset = defaultPreset;

	// Apply the default climate preset to ensure settings reflect the preset values
	ApplyClimatePreset(climatePreset);

	Ripples::UpdateSettings();  // Sync cached values after restoring defaults
}

void RainResponse::DrawWeatherAnalysis() const
{
	// Only show rain system analysis when it's raining and wetness effects are enabled
	if (!settings.EnableRainResponse)
		return;

	auto sky = globals::game::sky;
	if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->IsRaining())
		return;

	// Get the current frame data (reuses already calculated values)
	auto frameData = GetCommonBufferData();

	// Get weather particle density for precipitation calculations
	float weatherMaxParticleDensity = 0.0f;
	if (sky->currentWeather && sky->currentWeather->precipitationData) {
		weatherMaxParticleDensity = sky->currentWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	}

	// Check last weather if transitioning
	if (weatherMaxParticleDensity <= 0.0f && sky->lastWeather && sky->lastWeather->precipitationData) {
		weatherMaxParticleDensity = sky->lastWeather->precipitationData->GetSettingValue(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	}
	// Consolidated Shader & Weather Analysis
	{
		// Climate Preset Information Section
		DrawWeatherAnalysisLabel(T(TKEY("current_climate_preset"), "Current Climate Preset"));
		{
			// const auto& climate = GetClimateSettings(climatePreset); // Unused, remove to fix warning treated as error
			const auto& presetInfo = CLIMATE_PRESET_INFO[static_cast<size_t>(climatePreset)];

			ImGui::Text("Active Preset: %s", presetInfo.name);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", presetInfo.shortDescription);
			}

			ImGui::Text("Precipitation Rate Calculation");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				Util::DrawMultiLineTooltip({ "Precipitation rates are calculated using shader mechanics:",
					"- Raindrop chance (probability per interval)",
					"- Grid size (spatial density)",
					"- Interval (time between attempts)",
					"- All values reflect what is sent to the shader.",
					"Rates are shown in mm/hr, based on drops/sec and grid size." });
			}

			// Show current preset-applied values vs defaults
			Settings defaultSettings{};
			ImGui::Text("Current Settings (applied from preset):");
			ImGui::Indent();
			ImGui::Text("Rain Wetness: %.2f (default %.2f × %.1fx)", settings.MaxRainWetness, defaultSettings.MaxRainWetness, presetInfo.settings.wetnessMultiplier);
			ImGui::Text("Puddle Wetness: %.2f (default %.2f × %.1fx)", settings.MaxPuddleWetness, defaultSettings.MaxPuddleWetness, presetInfo.settings.puddleMultiplier);
			ImGui::Text("Transition Speed: %.2f (default %.2f × %.1fx)", settings.WeatherTransitionSpeed, defaultSettings.WeatherTransitionSpeed, presetInfo.settings.transitionSpeed);
			ImGui::Text("Raindrop Chance: %.1f%% (preset value)", settings.RaindropChance * 100.0f);
			ImGui::Unindent();
		}
		ImGui::Spacing();
		DrawWeatherAnalysisLabel(T(TKEY("rain_system_state"), "Rain System State"));
		if (sky->currentWeather) {
			float gridSizeGameUnits = 1.0f / frameData.settings.RaindropGridSize;
			float gridSizeMeters = Util::Units::GameUnitsToMeters(gridSizeGameUnits);
			float intervalSeconds = 1.0f / frameData.settings.RaindropInterval;
			float weatherBasedRainRate = CalculatePrecipitationRate(frameData.settings.RaindropChance, gridSizeGameUnits, intervalSeconds);
			float actualRainRate = weatherBasedRainRate;

			// Theoretical max using preset values and intensity = 1.0
			const auto& presetSettings = GetClimateSettings(climatePreset);
			float theoreticalMaxRainRate = CalculatePrecipitationRate(
				presetSettings.raindropChance, presetSettings.raindropGridSize, presetSettings.raindropInterval);

			if (ImGui::BeginTable("RainAnalysis", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders)) {
				ImGui::TableSetupColumn("Current Shader State", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableSetupColumn("Precipitation Analysis", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				Util::DrawColorCodedValue("Rain Intensity", frameData.Raining * 100.0f, std::format("{:.1f}%", frameData.Raining * 100.0f), Util::ColorCodedValueConfig::HighIsGood(10.0f, 50.0f, 80.0f));
				Util::DrawColorCodedValue("Wetness", frameData.Wetness * 100.0f, std::format("{:.1f}%", frameData.Wetness * 100.0f), Util::ColorCodedValueConfig::HighIsGood(25.0f, 60.0f, 85.0f));
				Util::DrawColorCodedValue("Puddle Wetness", frameData.PuddleWetness * 100.0f, std::format("{:.1f}%", frameData.PuddleWetness * 100.0f), Util::ColorCodedValueConfig::HighIsGood(15.0f, 40.0f, 70.0f));
				ImGui::Text("Puddle Formation: %.1f%% min wetness", frameData.settings.PuddleMinWetness * 100.0f);
				ImGui::Text("Weather Transition: %.1f%%", sky->currentWeatherPct * 100.0f);
				ImGui::Text("Raindrop Chance: %.1f%%", frameData.settings.RaindropChance * 100.0f);
				ImGui::Text("Grid Size: %.2f m (%.1f units)", gridSizeMeters, gridSizeGameUnits);
				ImGui::Text("Interval: %.1f sec", intervalSeconds);

				ImGui::TableNextColumn();
				// Live (Current):
				DrawRainTypeLabel("Current", actualRainRate);
				// Max (in Heavy Rain):
				DrawRainTypeLabel("Max (in Heavy Rain)", theoreticalMaxRainRate);
				ImGui::EndTable();
			}
		}
	}
}

// Helper function to auto-detect which preset matches current settings
void RainResponse::DetectCurrentPreset()
{
	if (DoesCurrentSettingsMatchPreset(ClimatePreset::Legacy)) {
		climatePreset = ClimatePreset::Legacy;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::NordicStandard)) {
		climatePreset = ClimatePreset::NordicStandard;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::ArcticTundra)) {
		climatePreset = ClimatePreset::ArcticTundra;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::TemperateCoastal)) {
		climatePreset = ClimatePreset::TemperateCoastal;
	} else if (DoesCurrentSettingsMatchPreset(ClimatePreset::MonsoonExtreme)) {
		climatePreset = ClimatePreset::MonsoonExtreme;
	} else {
		climatePreset = ClimatePreset::Custom;
	}
}

bool RainResponse::DoesCurrentSettingsMatchPreset(ClimatePreset preset) const
{
	// Custom preset never matches (it means user has customized settings)
	if (preset == ClimatePreset::Custom) {
		return false;
	}

	const auto& climate = GetClimateSettings(preset);
	Settings defaultSettings{};  // Get default values

	// Calculate what the settings should be for this preset
	float expectedMaxRainWetness = defaultSettings.MaxRainWetness * climate.wetnessMultiplier;
	float expectedMaxPuddleWetness = defaultSettings.MaxPuddleWetness * climate.puddleMultiplier;
	float expectedWeatherTransitionSpeed = defaultSettings.WeatherTransitionSpeed * climate.transitionSpeed;
	float expectedRaindropChance = climate.raindropChance;

	const float tolerance = 0.001f;
	return (std::abs(settings.MaxRainWetness - expectedMaxRainWetness) < tolerance &&
			std::abs(settings.MaxPuddleWetness - expectedMaxPuddleWetness) < tolerance &&
			std::abs(settings.WeatherTransitionSpeed - expectedWeatherTransitionSpeed) < tolerance &&
			std::abs(settings.RaindropChance - expectedRaindropChance) < tolerance);
}
