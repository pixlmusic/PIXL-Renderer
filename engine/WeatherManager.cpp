#include "WeatherManager.h"

#include "Globals.h"
#include "State.h"
#include "Utils/Form.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace
{
	void RestoreFeatureUserSettings(WeatherVariables::GlobalWeatherRegistry* registry, const std::string& featureName)
	{
		if (!registry)
			return;

		registry->EndFeatureTransition(featureName);
		auto* featureRegistry = registry->GetFeatureRegistry(featureName);
		if (!featureRegistry)
			return;

		for (const auto& var : featureRegistry->GetVariables())
			var->SetToUserSettings();
	}

	bool FeatureHasActiveTransition(WeatherVariables::GlobalWeatherRegistry* registry, const std::string& featureName)
	{
		if (!registry)
			return false;
		auto* featureRegistry = registry->GetFeatureRegistry(featureName);
		if (!featureRegistry)
			return false;
		for (const auto& var : featureRegistry->GetVariables()) {
			if (var && var->IsInTransition())
				return true;
		}
		return false;
	}

	std::string Lower(std::string_view value)
	{
		std::string result(value);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}

	bool ContainsAny(std::string_view value, std::initializer_list<std::string_view> tokens)
	{
		for (const auto token : tokens) {
			if (value.find(token) != std::string_view::npos)
				return true;
		}
		return false;
	}

	float Saturate(float value)
	{
		return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
	}

	float ApproachExp(float current, float target, float rate, float dt)
	{
		if (dt <= 0.0f || rate <= 0.0f)
			return current;
		const float response = 1.0f - std::exp(-rate * dt);
		return std::lerp(current, target, Saturate(response));
	}
}

WeatherManager::CurrentWeathers WeatherManager::GetCurrentWeathers()
{
	CurrentWeathers result{};
	auto* sky = globals::game::sky;
	if (!sky)
		return result;

	result.currentWeather = sky->currentWeather;
	result.lerpFactor = std::isfinite(sky->currentWeatherPct) ?
		std::clamp(sky->currentWeatherPct, 0.0f, 1.0f) : 1.0f;

	// When Skyrim swaps the destination before publishing lastWeather, the previous
	// current form is the only correct transition source. Capture it before updating
	// lastObservedCurrentWeather so an interrupted A->B->C transition cannot fall
	// back to stale A while B is visually on screen.
	if (result.currentWeather != lastObservedCurrentWeather) {
		if (!sky->lastWeather && lastObservedCurrentWeather)
			cachedLastWeather = lastObservedCurrentWeather;
		lastObservedCurrentWeather = result.currentWeather;
	}

	if (sky->lastWeather)
		cachedLastWeather = sky->lastWeather;

	if (result.lerpFactor < 1.0f)
		result.lastWeather = sky->lastWeather ? sky->lastWeather : cachedLastWeather;
	else
		result.lastWeather = sky->lastWeather;

	// Once stable, make the settled weather the fallback source for the next
	// transition. This is what prevents a completed A->B transition from leaving A
	// in the cache when B->C begins later.
	if (result.lerpFactor >= 1.0f && result.currentWeather && !sky->lastWeather)
		cachedLastWeather = result.currentWeather;

	return result;
}

float WeatherManager::GetWeatherPrecipitationDensity(RE::TESWeather* weather)
{
	if (!weather || !weather->precipitationData)
		return 0.0f;

	const float density = weather->precipitationData->GetSettingValue(
		RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
	return Saturate(density / 3.0f);
}

WeatherManager::WeatherClass WeatherManager::ClassifyWeather(RE::TESWeather* weather) const
{
	if (!weather)
		return WeatherClass::Unknown;

	// Engine state wins for active weather because it is more reliable than names
	// for weather-overhaul forms and generated records.
	if (auto* sky = globals::game::sky; sky && sky->mode.get() == RE::Sky::Mode::kFull) {
		if (weather == sky->currentWeather || weather == sky->lastWeather) {
			if (sky->IsSnowing())
				return WeatherClass::Snow;
			if (sky->IsRaining()) {
				const char* editorID = weather->GetFormEditorID();
				const std::string lowered = editorID ? Lower(editorID) : std::string{};
				return ContainsAny(lowered, { "storm", "thunder", "tempest", "squall" }) ?
					WeatherClass::Storm : WeatherClass::Rain;
			}
		}
	}

	std::string identity;
	if (const char* name = weather->GetName(); name && name[0] != '\0') {
		identity += name;
		identity.push_back(' ');
	}
	if (const char* editorID = weather->GetFormEditorID(); editorID && editorID[0] != '\0')
		identity += editorID;
	identity = Lower(identity);

	// Names/editor IDs are deliberately only a fallback. They allow inactive forms
	// (notably Director candidates) to be classified without maintaining any list of
	// weather mods or plugin filenames.
	if (ContainsAny(identity, { "snow", "blizzard", "flurry", "frost", "whiteout" }))
		return WeatherClass::Snow;
	if (ContainsAny(identity, { "storm", "thunder", "tempest", "squall" }))
		return WeatherClass::Storm;
	if (weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) ||
		ContainsAny(identity, { "rain", "shower", "drizzle" }))
		return WeatherClass::Rain;
	if (ContainsAny(identity, { "cloud", "overcast", "fog", "mist", "haze", "grey", "gray" }))
		return WeatherClass::Cloudy;
	if (ContainsAny(identity, { "clear", "sunny", "pleasant", "fair" }))
		return WeatherClass::Clear;

	return WeatherClass::Unknown;
}

float WeatherManager::ClassCloudiness(WeatherClass weatherClass)
{
	switch (weatherClass) {
	case WeatherClass::Clear: return 0.14f;
	case WeatherClass::Cloudy: return 0.72f;
	case WeatherClass::Rain: return 0.88f;
	case WeatherClass::Storm: return 1.0f;
	case WeatherClass::Snow: return 0.86f;
	case WeatherClass::Special: return 0.55f;
	default: return 0.42f;
	}
}

float WeatherManager::ClassFog(WeatherClass weatherClass)
{
	switch (weatherClass) {
	case WeatherClass::Clear: return 0.08f;
	case WeatherClass::Cloudy: return 0.34f;
	case WeatherClass::Rain: return 0.44f;
	case WeatherClass::Storm: return 0.58f;
	case WeatherClass::Snow: return 0.50f;
	case WeatherClass::Special: return 0.35f;
	default: return 0.22f;
	}
}

float WeatherManager::ClassWind(WeatherClass weatherClass)
{
	switch (weatherClass) {
	case WeatherClass::Clear: return 0.16f;
	case WeatherClass::Cloudy: return 0.28f;
	case WeatherClass::Rain: return 0.52f;
	case WeatherClass::Storm: return 0.92f;
	case WeatherClass::Snow: return 0.48f;
	case WeatherClass::Special: return 0.30f;
	default: return 0.22f;
	}
}

float WeatherManager::ClassStorminess(WeatherClass weatherClass)
{
	switch (weatherClass) {
	case WeatherClass::Storm: return 1.0f;
	case WeatherClass::Rain: return 0.48f;
	case WeatherClass::Snow: return 0.38f;
	case WeatherClass::Cloudy: return 0.16f;
	default: return 0.0f;
	}
}

const WeatherManager::WeatherContext& WeatherManager::GetContext()
{
	UpdateWeatherContext();
	return context;
}

void WeatherManager::UpdateWeatherContext()
{
	const CurrentWeathers weathers = GetCurrentWeathers();
	auto* sky = globals::game::sky;
	const bool exterior = sky && sky->mode.get() == RE::Sky::Mode::kFull;
	const bool overrideActive = sky && static_cast<bool>(sky->overrideWeather);

	const std::uint32_t frame = globals::state ? globals::state->frameCount : contextFrame + 1u;
	const bool sameFrame = globals::state && contextFrame == frame;
	const bool transitionUnchanged =
		context.transition.currentWeather == weathers.currentWeather &&
		context.transition.lastWeather == weathers.lastWeather &&
		std::abs(context.transition.lerpFactor - weathers.lerpFactor) <= 1.0e-5f &&
		context.exterior == exterior && context.overrideActive == overrideActive;
	if (sameFrame && transitionUnchanged)
		return;

	float dt = sameFrame ? 0.0f : static_cast<float>(RE::GetSecondsSinceLastFrame());
	if (!std::isfinite(dt) || dt < 0.0f || (globals::game::ui && globals::game::ui->GameIsPaused()))
		dt = 0.0f;
	dt = std::clamp(dt, 0.0f, 0.25f);

	context.transition = weathers;
	context.exterior = exterior;
	context.overrideActive = overrideActive;
	context.source = temporarySourceActive ? temporarySource :
		(overrideActive ? WeatherSource::EngineOverride : WeatherSource::Game);

	if (!exterior) {
		context.weatherClass = WeatherClass::Unknown;
		context.precipitation = 0.0f;
		context.rainIntensity = 0.0f;
		context.snowIntensity = 0.0f;
		context.cloudiness = 0.0f;
		context.fogIntensity = 0.0f;
		context.storminess = 0.0f;
		context.windIntensity = 0.0f;
		context.sunVisibility = 1.0f;
		context.wetnessTarget = 0.0f;
		context.persistentWetness = std::max(0.0f, context.persistentWetness - dt * 0.018f);
		context.residualHumidity = std::max(0.0f, context.residualHumidity - dt * 0.012f);
		context.snowfallMemory = std::max(0.0f, context.snowfallMemory - dt * 0.006f);
		contextFrame = frame;
		return;
	}

	const WeatherClass fromClass = ClassifyWeather(weathers.lastWeather);
	const WeatherClass toClass = ClassifyWeather(weathers.currentWeather);
	const float t = Saturate(weathers.lerpFactor);
	context.weatherClass = t >= 0.5f ? toClass : fromClass;
	if (fromClass == WeatherClass::Unknown)
		context.weatherClass = toClass;

	const auto isRainClass = [](WeatherClass c) {
		return c == WeatherClass::Rain || c == WeatherClass::Storm;
	};
	const auto isSnowClass = [](WeatherClass c) { return c == WeatherClass::Snow; };

	const float fromDensity = GetWeatherPrecipitationDensity(weathers.lastWeather);
	const float toDensity = GetWeatherPrecipitationDensity(weathers.currentWeather);
	const float density = std::lerp(fromDensity, toDensity, t);

	float rainPresence = 0.0f;
	if (isRainClass(fromClass)) rainPresence = std::max(rainPresence, 1.0f - t);
	if (isRainClass(toClass)) rainPresence = std::max(rainPresence, t);
	if (sky->IsRaining()) rainPresence = std::max(rainPresence, 0.85f);

	float snowPresence = 0.0f;
	if (isSnowClass(fromClass)) snowPresence = std::max(snowPresence, 1.0f - t);
	if (isSnowClass(toClass)) snowPresence = std::max(snowPresence, t);
	if (sky->IsSnowing()) snowPresence = std::max(snowPresence, 0.85f);

	const bool precipitationPresent = sky->precip &&
		(static_cast<bool>(sky->precip->currentPrecip) || static_cast<bool>(sky->precip->lastPrecip));
	if (precipitationPresent && !sky->IsSnowing() && rainPresence <= 0.001f)
		rainPresence = 0.60f;

	context.rainIntensity = rainPresence > 0.001f ?
		std::pow(Saturate(std::max(density, 0.42f * std::max(rainPresence, 0.65f))), 0.72f) : 0.0f;
	context.snowIntensity = snowPresence > 0.001f ?
		std::pow(Saturate(std::max(density, 0.38f * std::max(snowPresence, 0.65f))), 0.78f) : 0.0f;
	context.precipitation = std::max(context.rainIntensity, context.snowIntensity);

	context.cloudiness = Saturate(std::lerp(ClassCloudiness(fromClass), ClassCloudiness(toClass), t));
	context.fogIntensity = Saturate(std::lerp(ClassFog(fromClass), ClassFog(toClass), t));
	context.windIntensity = Saturate(std::lerp(ClassWind(fromClass), ClassWind(toClass), t));
	context.storminess = Saturate(std::lerp(ClassStorminess(fromClass), ClassStorminess(toClass), t));

	// Precipitation pushes semantic response beyond the class baseline. This makes
	// unusual modded rainy forms behave sensibly even if their names classify poorly.
	context.cloudiness = std::max(context.cloudiness, context.precipitation * 0.84f);
	context.fogIntensity = std::max(context.fogIntensity, context.precipitation * 0.36f);
	context.windIntensity = std::max(context.windIntensity,
		context.rainIntensity * 0.45f + context.storminess * 0.45f);
	context.sunVisibility = Saturate(1.0f - context.cloudiness * 0.72f - context.precipitation * 0.16f);

	context.wetnessTarget = Saturate(context.rainIntensity * 0.96f + context.residualHumidity * 0.18f);
	const float humidityTarget = Saturate(context.cloudiness * 0.42f + context.rainIntensity * 0.75f + context.snowIntensity * 0.28f);
	context.residualHumidity = ApproachExp(
		context.residualHumidity,
		humidityTarget,
		humidityTarget > context.residualHumidity ? 0.75f : 0.035f,
		dt);
	context.persistentWetness = ApproachExp(
		context.persistentWetness,
		context.wetnessTarget,
		context.wetnessTarget > context.persistentWetness ? 0.90f : 0.028f,
		dt);
	context.snowfallMemory = ApproachExp(
		context.snowfallMemory,
		context.snowIntensity,
		context.snowIntensity > context.snowfallMemory ? 0.18f : 0.008f,
		dt);

	contextFrame = frame;
}

void WeatherManager::SetTemporaryWeatherSource(WeatherSource source, bool active)
{
	temporarySourceActive = active;
	temporarySource = active ? source : WeatherSource::Game;
	// Force the next GetContext call to re-evaluate the source even in the same frame.
	contextFrame = ~0u;
}

void WeatherManager::LoadPerWeatherSettingsFromDisk()
{
	const std::filesystem::path weathersPath = Util::PathHelpers::GetPluginPath() / "World" / "Weather";
	std::map<std::string, std::map<std::string, json>> newCache;

	std::error_code ec;
	if (!std::filesystem::exists(weathersPath, ec)) {
		if (ec) {
			logger::warn("Could not inspect Weathers directory ({}): {}", weathersPath.string(), ec.message());
			return;
		}
		perWeatherSettingsCache.clear();
		logger::info("Weathers directory does not exist: {}", weathersPath.string());
		return;
	}
	if (!std::filesystem::is_directory(weathersPath, ec) || ec) {
		logger::warn("Weather settings path is not a readable directory: {}", weathersPath.string());
		return;
	}

	logger::info("Loading per-weather settings from: {}", weathersPath.string());

	try {
		for (const auto& entry : std::filesystem::directory_iterator(weathersPath)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json")
				continue;

			const std::string weatherKey = entry.path().stem().string();
			std::ifstream settingsFile(entry.path());
			if (!settingsFile.good() || !settingsFile.is_open()) {
				logger::warn("Failed to open weather settings file: {}", entry.path().string());
				if (const auto old = perWeatherSettingsCache.find(weatherKey); old != perWeatherSettingsCache.end())
					newCache[weatherKey] = old->second;
				continue;
			}

			try {
				json weatherData;
				settingsFile >> weatherData;
				if (weatherData.is_object() && weatherData.contains("featureSettings") && weatherData["featureSettings"].is_object()) {
					for (auto& [featureName, featureSettings] : weatherData["featureSettings"].items())
						newCache[weatherKey][featureName] = featureSettings;
					logger::info("Loaded settings for weather: {}", weatherKey);
				}
			} catch (const std::exception& e) {
				logger::warn("Error parsing weather settings file ({}): {}", entry.path().string(), e.what());
				if (const auto old = perWeatherSettingsCache.find(weatherKey); old != perWeatherSettingsCache.end())
					newCache[weatherKey] = old->second;
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Weather settings reload aborted; keeping previous cache: {}", e.what());
		return;
	}

	perWeatherSettingsCache.swap(newCache);
	logger::info("Finished loading per-weather settings. Total weathers: {}", perWeatherSettingsCache.size());
}

void WeatherManager::UpdateFeatures()
{
	// Semantic consumers may ask for context outside this function, but advancing it
	// here guarantees normal renderer frames always refresh even if no consumer does.
	UpdateWeatherContext();
	const auto currentWeathers = GetCurrentWeathers();

	const bool weatherChanged =
		currentWeathers.currentWeather != lastKnownWeather.currentWeather ||
		currentWeathers.lastWeather != lastKnownWeather.lastWeather;
	const bool transitionStarting = weatherChanged && currentWeathers.lerpFactor < 1.0f;
	const bool interruptedTransition = weatherChanged &&
		lastKnownWeather.lerpFactor < 1.0f && currentWeathers.lerpFactor < 1.0f;
	const bool transitionEnding = lastKnownWeather.lerpFactor < 1.0f && currentWeathers.lerpFactor >= 1.0f;
	const bool progressChanged = std::abs(currentWeathers.lerpFactor - lastKnownWeather.lerpFactor) > 0.001f;

	if (!(weatherChanged || transitionEnding || progressChanged))
		return;

	auto* globalRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	if (!globalRegistry) {
		lastKnownWeather = currentWeathers;
		return;
	}

	for (auto* feature : RenderModule::GetModuleList()) {
		if (!feature || !feature->loaded)
			continue;

		const std::string featureName = feature->GetShortName();
		if (!globalRegistry->HasWeatherSupport(featureName))
			continue;

		json fromSettings;
		json toSettings;
		const bool hasFromOverride = currentWeathers.lastWeather && currentWeathers.lerpFactor < 1.0f &&
			LoadSettingsFromWeather(currentWeathers.lastWeather, featureName, fromSettings);
		const bool hasToOverride = currentWeathers.currentWeather &&
			LoadSettingsFromWeather(currentWeathers.currentWeather, featureName, toSettings);
		const bool hasAnyOverride = hasFromOverride || hasToOverride;
		const bool wasTransitioning = FeatureHasActiveTransition(globalRegistry, featureName);
		json previousWeatherSettings;
		const bool previousWeatherHadOverride = weatherChanged && lastKnownWeather.currentWeather &&
			LoadSettingsFromWeather(lastKnownWeather.currentWeather, featureName, previousWeatherSettings);

		if (transitionStarting && hasAnyOverride) {
			// On A->B interrupted by B->C, the live value is already between A and B.
			// Passing B's authored override would snap the new transition start to B.
			// A null JSON tells WeatherVariableRegistry to capture the live value instead.
			globalRegistry->BeginFeatureTransition(
				featureName, interruptedTransition ? json{} : fromSettings);
		}

		// Settled destination is authoritative. This branch intentionally comes
		// before the no-overrides branch: arriving at a weather with no override must
		// restore the captured user baseline rather than merely ending the transition.
		if (currentWeathers.lerpFactor >= 1.0f) {
			if (hasToOverride) {
				globalRegistry->UpdateFeatureFromWeathers(featureName, json{}, toSettings, 1.0f);
				globalRegistry->EndFeatureTransition(featureName);
			} else if (wasTransitioning || previousWeatherHadOverride) {
				// Only restore when weather actually owned the value. This avoids stomping
				// scene/photo/runtime controls during unrelated no-override weather swaps.
				RestoreFeatureUserSettings(globalRegistry, featureName);
			} else {
				globalRegistry->EndFeatureTransition(featureName);
			}
			continue;
		}

		if (!hasAnyOverride) {
			if (wasTransitioning)
				RestoreFeatureUserSettings(globalRegistry, featureName);
			else
				globalRegistry->EndFeatureTransition(featureName);
			continue;
		}

		globalRegistry->UpdateFeatureFromWeathers(
			featureName, fromSettings, toSettings, currentWeathers.lerpFactor);
	}

	lastKnownWeather = currentWeathers;
}

void WeatherManager::SaveSettingsToWeather(RE::TESWeather* weather, const std::string& featureName, const json& settings)
{
	if (!weather)
		return;

	const std::string weatherKey = GetWeatherKey(weather);
	if (settings.is_object() && settings.empty()) {
		if (auto wkIt = perWeatherSettingsCache.find(weatherKey); wkIt != perWeatherSettingsCache.end()) {
			wkIt->second.erase(featureName);
			if (wkIt->second.empty())
				perWeatherSettingsCache.erase(wkIt);
		}
	} else {
		perWeatherSettingsCache[weatherKey][featureName] = settings;
	}

	const std::filesystem::path weathersPath = Util::PathHelpers::GetPluginPath() / "World" / "Weather";
	const std::filesystem::path filePath = weathersPath / (weatherKey + ".json");
	const std::filesystem::path tempPath = filePath.string() + ".tmp";

	std::error_code ec;
	std::filesystem::create_directories(weathersPath, ec);
	if (ec) {
		logger::warn("Error creating Weathers directory ({}): {}", weathersPath.string(), ec.message());
		return;
	}

	json weatherData = json::object();
	if (std::filesystem::exists(filePath, ec) && !ec) {
		std::ifstream existingFile(filePath);
		if (existingFile.good() && existingFile.is_open()) {
			try {
				existingFile >> weatherData;
			} catch (const std::exception& e) {
				logger::warn("Error parsing existing weather file ({}): {}", filePath.string(), e.what());
				weatherData = json::object();
			}
		}
	}

	if (!weatherData.is_object())
		weatherData = json::object();
	if (!weatherData.contains("featureSettings") || !weatherData["featureSettings"].is_object())
		weatherData["featureSettings"] = json::object();

	auto& featureSettings = weatherData["featureSettings"];
	if (settings.is_object() && settings.empty())
		featureSettings.erase(featureName);
	else
		featureSettings[featureName] = settings;

	if (featureSettings.empty() && weatherData.size() == 1 && weatherData.contains("featureSettings")) {
		std::filesystem::remove(filePath, ec);
		if (ec && ec != std::errc::no_such_file_or_directory)
			logger::warn("Failed to remove empty weather settings file ({}): {}", filePath.string(), ec.message());
		return;
	}

	try {
		{
			std::ofstream settingsFile(tempPath, std::ios::out | std::ios::trunc);
			if (!settingsFile.good() || !settingsFile.is_open()) {
				logger::warn("Failed to open temporary weather settings file for writing: {}", tempPath.string());
				return;
			}
			settingsFile << weatherData.dump(1);
			settingsFile.flush();
			if (!settingsFile.good())
				throw std::runtime_error("flush failed");
		}

		// std::filesystem::rename does not replace an existing target on Windows, so
		// remove only after the complete temporary file is safely written.
		std::filesystem::remove(filePath, ec);
		ec.clear();
		std::filesystem::rename(tempPath, filePath, ec);
		if (ec) {
			logger::warn("Failed to publish weather settings file ({}): {}", filePath.string(), ec.message());
			std::filesystem::remove(tempPath, ec);
			return;
		}
		logger::info("Saved {} settings for weather: {}", featureName, weatherKey);
	} catch (const std::exception& e) {
		logger::warn("Error writing weather settings file ({}): {}", filePath.string(), e.what());
		std::filesystem::remove(tempPath, ec);
	}
}

bool WeatherManager::LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json)
{
	if (!weather)
		return false;

	const std::string weatherKey = GetWeatherKey(weather);
	const auto weatherIt = perWeatherSettingsCache.find(weatherKey);
	if (weatherIt == perWeatherSettingsCache.end())
		return false;

	const auto featureIt = weatherIt->second.find(featureName);
	if (featureIt == weatherIt->second.end())
		return false;

	const json& featureJson = featureIt->second;
	if (!featureJson.is_object() || !featureJson.value("__enabled", false))
		return false;

	o_json = featureJson;
	return true;
}

std::string WeatherManager::GetWeatherKey(RE::TESWeather* weather)
{
	return Util::GetFormFileKey(weather);
}

bool WeatherManager::HasWeatherSettings(RE::TESWeather* weather) const
{
	if (!weather)
		return false;
	return perWeatherSettingsCache.find(GetWeatherKey(weather)) != perWeatherSettingsCache.end();
}

void WeatherManager::ClearAllFeatureSettingsForWeather(RE::TESWeather* weather)
{
	if (weather)
		perWeatherSettingsCache.erase(GetWeatherKey(weather));
}

void WeatherManager::ClearCache()
{
	perWeatherSettingsCache.clear();
	lastKnownWeather = {};
	cachedLastWeather = nullptr;
	lastObservedCurrentWeather = nullptr;
	context = {};
	contextFrame = ~0u;
	temporarySourceActive = false;
	temporarySource = WeatherSource::Game;
	logger::info("Cleared WeatherManager cache and runtime weather context");
}
