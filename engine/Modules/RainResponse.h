#pragma once

#include "Buffer.h"

/** @brief Adds dynamic weather-driven wetness, puddle formation, shore wetness, and raindrop effects. */
struct RainResponse : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "112739";

public:
	virtual inline std::string GetName() override { return "Rain Response"; }
	virtual std::string GetDisplayName() override { return T("feature.rain_response.name", "Rain Response"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "RainResponse"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "RAIN_RESPONSE"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kWater; }

	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.rain_response.description", "Enhances the game's active rain material with world-stable depth, clumped precipitation, gust layers, impact response, roof-edge runoff, volumetric rain mist, wetness, puddles, and ripples."),
			{ T("feature.rain_response.key_feature_1", "Weather-synchronised falling rain with world-space depth and density variation"),
				T("feature.rain_response.key_feature_2", "Realistic puddle formation and shore wetness effects"),
				T("feature.rain_response.key_feature_3", "Animated raindrop effects with splashes and ripples"),
				T("feature.rain_response.key_feature_4", "Configurable wetness intensity and weather transitions"),
				T("feature.rain_response.key_feature_5", "Wind-driven gust layers, roof/awning runoff, rain mist, skin wetness, and material-specific responses") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		uint EnableRainResponse = true;
		float MaxRainWetness = 1.0f;
		float MaxPuddleWetness = 1.5f;
		float MaxShoreWetness = 1.0f;
		uint ShoreRange = 32;
		float PuddleRadius = 1.0f;
		float PuddleMaxAngle = 0.95f;
		float PuddleMinWetness = 0.85f;
		float MinRainWetness = 0.65f;
		float SkinWetness = 0.95f;
		float WeatherTransitionSpeed = 3.0f;

		// Raindrop fx settings
		uint EnableRaindropFx = true;
		uint EnableSplashes = true;
		uint EnableRipples = true;
		uint EnableVanillaRipples = false;
		float RaindropFxRange = 1000.f;
		float RaindropGridSize = 4.f;
		float RaindropInterval = 1.0f;
		float RaindropChance = 1.0f;
		float SplashesLifetime = 10.0f;
		float SplashesStrength = 1.05f;
		float SplashesMinRadius = .3f;
		float SplashesMaxRadius = .5f;
		float RippleStrength = 1.f;
		float RippleRadius = 1.f;
		float RippleBreadth = .5f;
		float RippleLifetime = .5f;

		// Falling-rain enhancement. These parameters intentionally shape the game's
		// supplied precipitation rather than replacing its material/texture.
		uint EnableRainParticleEnhancement = true;
		float RainClumpStrength = 0.55f;
		float RainClumpSize = 1800.0f;
		float RainStreakVariation = 0.35f;

		float RainGustStrength = 0.80f;
		float RainGustFrequency = 0.18f;
		float RainGustChance = 0.22f;
		float RainSecondaryLayerStrength = 0.32f;

		float RainDepthStart = 650.0f;
		float RainDepthEnd = 12000.0f;
		float RainDistanceBoost = 0.75f;
		float RainImpactSplashStrength = 0.65f;

		float RainMistStrength = 0.55f;
		float RainMistScale = 0.0008f;
		float RainMistHeight = 520.0f;
		float RainLightingBoost = 0.50f;

		// Reuses the old PerFrame padding slot, so FeatureData stays 256 bytes.
		// This is a master control; edge width/density remain physically-derived in HLSL.
		float RainRunoffStrength = 0.70f;
	};

	struct alignas(16) PerFrame
	{
		REX::W32::XMFLOAT4X4 OcclusionViewProj;
		float Time;
		float Raining;
		float Wetness;
		float PuddleWetness;
		Settings settings;
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);
	static_assert(sizeof(Settings) == 176, "RainResponse::Settings must consume the former PerFrame padding slot");
	static_assert(sizeof(PerFrame) == 256, "RainResponse::PerFrame constant-buffer ABI changed unexpectedly");

	struct DebugSettings
	{
		bool EnableWetnessOverride = false;
		bool EnablePuddleOverride = false;
		bool EnableRainOverride = false;
		bool EnableIntExOverride = false;
		float2 WetnessOverride = float2(0.0f, 0.0f);
		float2 PuddleWetnessOverride = float2(0.0f, 0.0f);
		float2 RainOverride = float2(0.0f, 0.0f);
	} debugSettings;

	Settings settings;
	// Climate preset system
	enum class ClimatePreset : uint32_t
	{
		Custom = 0,
		Legacy = 1,
		NordicStandard = 2,
		ArcticTundra = 3,
		TemperateCoastal = 4,
		MonsoonExtreme = 5
	};
	struct ClimateSettings
	{
		float wetnessMultiplier;
		float puddleMultiplier;
		float transitionSpeed;
		float raindropChance;
		float raindropGridSize;
		float raindropInterval;
	};
	static constexpr ClimatePreset defaultPreset = ClimatePreset::NordicStandard;
	ClimatePreset climatePreset = defaultPreset;

	/** @brief Builds the per-frame constant buffer data including weather state and settings. */
	PerFrame GetCommonBufferData() const;

	/** @brief Updates wetness state and binds the per-frame constant buffer. */
	virtual void Prepass() override;
	/** @brief Releases directly compiled runoff kernels so shader hot-reload can rebuild them. */
	virtual void ClearShaderCache() override;

	/** @brief Draws dedicated screen-space roof/awning runoff after the deferred scene composite. */
	void DrawRoofRunoff();
	/** @brief Detects Splashes of Storms mod presence for compatibility handling. */
	virtual void PostPostLoad() override;

	/** @brief Draws the ImGui settings panel for wetness effects configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	/** @brief Returns the weather analysis configuration for the debug weather analysis panel. */
	virtual WeatherAnalysisConfig GetWeatherAnalysisConfig() const override
	{
		return WeatherAnalysisConfig("Rain & Wetness Analysis", [this]() {
			this->DrawWeatherAnalysis();
		});
	}

	// Constants and utilities for rain intensity calculations
	static constexpr float MAX_RAIN_PARTICLE_DENSITY = 3.0f;

	/**
	 * @brief Extracts rain intensity from the active precipitation geometry and weather.
	 * @param precipObject The precipitation particle geometry.
	 * @param weather The current weather form.
	 * @return Normalized rain intensity in the range [0, 1].
	 */
	static float GetRainIntensity(RE::NiPointer<RE::BSGeometry> precipObject, RE::TESWeather* weather);
	/** @brief Returns live exterior rain intensity independently of the wetness feature toggle.
	 *  Uses Skyrim weather/transition state with Sky::IsRaining() as a visibility fallback,
	 *  so CameraSuite Stormglass cannot silently lose rain when Rain Response is disabled.
	 */
	float GetLiveRainIntensity() const;
	/**
	 * @brief Calculates the precipitation rate in mm/hr from raindrop shader parameters.
	 * @param raindropChance Probability of a raindrop spawning per grid cell per interval.
	 * @param raindropGridSizeGameUnits Size of each raindrop grid cell in game units.
	 * @param raindropIntervalSeconds Time between raindrop spawn attempts in seconds.
	 * @param mlPerDrop Volume of each raindrop in milliliters.
	 * @return Estimated precipitation rate in mm/hr.
	 */
	float CalculatePrecipitationRate(float raindropChance, float raindropGridSizeGameUnits, float raindropIntervalSeconds, float mlPerDrop = 0.01f) const;
	/**
	 * @brief Returns the climate settings for a given preset.
	 * @param preset The climate preset to look up.
	 * @return Reference to the preset's climate settings.
	 */
	static const ClimateSettings& GetClimateSettings(ClimatePreset preset);
	/**
	 * @brief Applies a climate preset, overwriting the current wetness and raindrop settings.
	 * @param preset The climate preset to apply.
	 */
	void ApplyClimatePreset(ClimatePreset preset);
	/**
	 * @brief Checks whether the current settings exactly match a given climate preset.
	 * @param preset The climate preset to compare against.
	 * @return True if all settings match the preset values.
	 */
	bool DoesCurrentSettingsMatchPreset(ClimatePreset preset) const;
	/** @brief Detects which climate preset matches the current settings, if any. */
	void DetectCurrentPreset();

private:
	void DrawWeatherAnalysis() const;

	/** @brief Ensures the half-resolution runoff edge buffer matches the active scene target. */
	void EnsureRoofRunoffResources(uint32_t a_width, uint32_t a_height);
	ID3D11ComputeShader* GetRoofRunoffDetectCS();
	ID3D11ComputeShader* GetRoofRunoffResolveCS();

	std::unique_ptr<Texture2D> roofRunoffEdgeMask;
	winrt::com_ptr<ID3D11ComputeShader> roofRunoffDetectCS;
	winrt::com_ptr<ID3D11ComputeShader> roofRunoffResolveCS;
	bool roofRunoffDetectAttempted = false;
	bool roofRunoffResolveAttempted = false;

	bool splashesOfStormsLoaded = false;

	// Weather wetness calculation result for debug display
	struct WeatherWetnessResult
	{
		float wetness = 0.0f;
		float puddleWetness = 0.0f;
	};

	WeatherWetnessResult CalculateWeatherWetness(RE::TESWeather* weather, float weatherPct, bool isCurrentWeather) const;
};
