#pragma once

#include "WeatherVariableRegistry.h"

#include <cstdint>
#include <map>
#include <string>

using json = nlohmann::json;

/**
 * @brief Central PIXL weather authority.
 *
 * Keeps the existing exact TESWeather override system intact while also exposing
 * one semantic live-weather context for renderer modules that need rain, snow,
 * cloud/fog, wind and persistence signals. Exact per-weather JSON remains an
 * artist/user override layer; semantic context never pretends to be a saved
 * override and therefore does not lock WeatherUI controls.
 */
class WeatherManager
{
public:
	enum class WeatherClass : std::uint8_t
	{
		Unknown = 0,
		Clear,
		Cloudy,
		Rain,
		Storm,
		Snow,
		Special
	};

	enum class WeatherSource : std::uint8_t
	{
		Game = 0,
		EngineOverride,
		Director
	};

	/** @brief Returns the global singleton instance. */
	static WeatherManager* GetSingleton()
	{
		static WeatherManager singleton;
		return &singleton;
	}

	/** @brief Snapshot of the active Skyrim weather transition. */
	struct CurrentWeathers
	{
		RE::TESWeather* currentWeather = nullptr;
		RE::TESWeather* lastWeather = nullptr;
		float lerpFactor = 1.0f;
	};

	/** @brief Renderer-facing semantic weather state. */
	struct WeatherContext
	{
		CurrentWeathers transition{};
		WeatherClass weatherClass = WeatherClass::Unknown;
		WeatherSource source = WeatherSource::Game;

		bool exterior = false;
		bool overrideActive = false;

		float precipitation = 0.0f;
		float rainIntensity = 0.0f;
		float snowIntensity = 0.0f;
		float cloudiness = 0.0f;
		float fogIntensity = 0.0f;
		float storminess = 0.0f;
		float windIntensity = 0.0f;
		float sunVisibility = 1.0f;

		// Slowly varying environment memory. These are renderer signals only;
		// GroundResponse/ActorSurfaceEffects still own their physical material state.
		float wetnessTarget = 0.0f;
		float persistentWetness = 0.0f;
		float residualHumidity = 0.0f;
		float snowfallMemory = 0.0f;
	};

	/** @brief Queries Skyrim for the current transition with interrupted-transition protection. */
	CurrentWeathers GetCurrentWeathers();

	/** @brief Returns an up-to-date semantic context, advancing persistence at most once per rendered frame. */
	const WeatherContext& GetContext();

	/** @brief Classifies any TESWeather without requiring a hard-coded weather-mod list. */
	WeatherClass ClassifyWeather(RE::TESWeather* weather) const;

	/**
	 * @brief Tags a temporary engine weather override with its PIXL owner.
	 *
	 * This does not call ForceWeather/SetWeather and never owns restoration. Director
	 * continues to own its existing snapshot/restore transaction; WeatherManager only
	 * reports the source correctly to renderer consumers and diagnostics.
	 */
	void SetTemporaryWeatherSource(WeatherSource source, bool active);

	/** @brief Loads all exact per-weather JSON settings from Data/.../World/Weather. */
	void LoadPerWeatherSettingsFromDisk();

	/** @brief Updates exact per-weather registered feature overrides and semantic context. */
	void UpdateFeatures();

	/** @brief Persists exact feature settings for a TESWeather to cache and disk. */
	void SaveSettingsToWeather(RE::TESWeather* weather, const std::string& featureName, const json& settings);

	/**
	 * @brief Loads an exact saved override only.
	 *
	 * Automatic semantic weather response is intentionally not returned here so the
	 * existing WeatherUI only locks controls for explicit user/artist overrides.
	 */
	bool LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json);

	/** @brief Generates a stable local-form/plugin key used by existing weather JSON files. */
	static std::string GetWeatherKey(RE::TESWeather* weather);

	/** @brief Removes all cached exact feature settings for one weather. */
	void ClearAllFeatureSettingsForWeather(RE::TESWeather* weather);

	/** @brief Returns true when the exact-weather cache contains any entries for this weather. */
	bool HasWeatherSettings(RE::TESWeather* weather) const;

	/** @brief Clears exact settings plus transition/context tracking state. */
	void ClearCache();

private:
	WeatherManager() = default;
	~WeatherManager() = default;
	WeatherManager(const WeatherManager&) = delete;
	WeatherManager& operator=(const WeatherManager&) = delete;

	void UpdateWeatherContext();
	static float GetWeatherPrecipitationDensity(RE::TESWeather* weather);
	static float ClassCloudiness(WeatherClass weatherClass);
	static float ClassFog(WeatherClass weatherClass);
	static float ClassWind(WeatherClass weatherClass);
	static float ClassStorminess(WeatherClass weatherClass);

	// Exact saved overrides: weatherKey -> featureName -> settings.
	std::map<std::string, std::map<std::string, json>> perWeatherSettingsCache;

	CurrentWeathers lastKnownWeather{};
	RE::TESWeather* cachedLastWeather = nullptr;
	RE::TESWeather* lastObservedCurrentWeather = nullptr;

	WeatherContext context{};
	std::uint32_t contextFrame = ~0u;

	bool temporarySourceActive = false;
	WeatherSource temporarySource = WeatherSource::Game;
};
