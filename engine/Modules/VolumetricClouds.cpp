// PIXL Renderer - world-space volumetric cloud prototype.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VolumetricClouds.h"

#include "Deferred.h"
#include "Globals.h"
#include "Menu/TuningWorkspaceRenderer.h"
#include "Renderer/TemporalContext.h"
#include "Renderer/VolumetricContext.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
	constexpr std::uint32_t kWeatherMapSize = 512u;
	constexpr float kWeatherUpdateInterval = 0.20f;
	constexpr float kWeatherExtent = 1048576.0f;
	constexpr double kCloudAdvectionPeriod = 4096.0 / 0.000020;
	constexpr float kCloudCurvatureRadius = 445900000.0f;
	// Keep workload tiers together: Performance, Balanced, High, Ultra.
	constexpr std::array<std::uint32_t, 4> kCloudDownsample{ 4u, 4u, 2u, 2u };
	constexpr std::array<std::uint32_t, 4> kCloudPrimarySteps{ 32u, 48u, 80u, 96u };
	constexpr std::array<std::uint32_t, 4> kCloudLightSteps{ 3u, 4u, 5u, 6u };

	float FiniteClamp(float value, float fallback, float minimum, float maximum)
	{
		return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
	}

	void AdvancePeriodic(double& phase, double delta, double period)
	{
		if (!std::isfinite(delta) || !std::isfinite(period) || period <= 0.0)
			return;
		phase = std::fmod(phase + delta, period);
		if (phase < 0.0)
			phase += period;
	}

	struct WeatherProfile
	{
		float coverage{};
		float density{};
		float type{};
		float precipitation{};
		float storm{};
		float fog{};
		float sunVisibility{ 1.0f };
		float baseOffset{};
		float cloudTop{ 45000.0f };
		float windScale{ 0.20f };
		float detailScale{ 1.0f };
		float upperCoverage{ 0.32f };
		float upperDensity{ 0.34f };
		float upperType{ 0.08f };
		float upperDetail{ 0.65f };
		float upperWindScale{ 0.75f };
		float upperTop{ 95000.0f };
	};

	struct CloudWeatherPreset
	{
		float coverage{};
		float density{ 1.0f };
		float type{};
		float detail{ 1.0f };
		float wind{ 1.0f };
		float maximumTop{ 65000.0f };
		float upperCoverage{ 0.32f };
		float upperDensity{ 0.34f };
		float upperType{ 0.08f };
		float upperDetail{ 0.65f };
		float upperWind{ 0.75f };
		float upperTop{ 95000.0f };
	};

	CloudWeatherPreset GetCloudWeatherPreset(WeatherManager::WeatherClass weatherClass)
	{
		// Keep the established weather-class profiles. Authored Skyrim cloud
		// opacity remains authoritative per weather.
		switch (weatherClass) {
		case WeatherManager::WeatherClass::Clear: return { 0.85f, 0.82f, 0.10f, 0.78f, 0.72f, 45000.0f, 0.42f, 0.24f, 0.02f, 0.55f, 0.70f, 100000.0f };
		case WeatherManager::WeatherClass::Cloudy: return { 1.08f, 1.00f, 0.38f, 1.00f, 1.00f, 65000.0f, 0.34f, 0.32f, 0.10f, 0.68f, 0.78f, 95000.0f };
		case WeatherManager::WeatherClass::Fog: return { 0.98f, 0.94f, 0.16f, 0.84f, 0.82f, 58000.0f, 0.20f, 0.18f, 0.02f, 0.48f, 0.60f, 82000.0f };
		case WeatherManager::WeatherClass::Rain: return { 1.18f, 1.10f, 0.63f, 0.96f, 1.18f, 65000.0f, 0.24f, 0.26f, 0.12f, 0.56f, 0.82f, 90000.0f };
		case WeatherManager::WeatherClass::Storm: return { 1.25f, 1.20f, 0.86f, 1.08f, 1.38f, 65000.0f, 0.18f, 0.20f, 0.20f, 0.52f, 0.92f, 88000.0f };
		case WeatherManager::WeatherClass::Snow: return { 1.12f, 1.06f, 0.58f, 0.94f, 1.02f, 62000.0f, 0.30f, 0.28f, 0.12f, 0.62f, 0.72f, 92000.0f };
		case WeatherManager::WeatherClass::Special: return { 1.02f, 1.00f, 0.48f, 0.96f, 0.96f, 61000.0f, 0.28f, 0.30f, 0.12f, 0.72f, 0.76f, 96000.0f };
		default: return { 1.08f, 1.00f, 0.42f, 1.00f, 1.00f, 65000.0f, 0.32f, 0.30f, 0.10f, 0.65f, 0.76f, 95000.0f };
		}
	}

	const char* WeatherClassName(WeatherManager::WeatherClass weatherClass)
	{
		switch (weatherClass) {
		case WeatherManager::WeatherClass::Clear: return "Clear";
		case WeatherManager::WeatherClass::Cloudy: return "Cloudy";
		case WeatherManager::WeatherClass::Fog: return "Fog";
		case WeatherManager::WeatherClass::Rain: return "Rain";
		case WeatherManager::WeatherClass::Storm: return "Storm";
		case WeatherManager::WeatherClass::Snow: return "Snow";
		case WeatherManager::WeatherClass::Special: return "Special";
		default: return "Custom / unknown";
		}
	}

	float WeatherCloudFloor(WeatherManager::WeatherClass weatherClass)
	{
		switch (weatherClass) {
		case WeatherManager::WeatherClass::Clear: return 0.20f;
		case WeatherManager::WeatherClass::Cloudy: return 0.77f;
		case WeatherManager::WeatherClass::Fog: return 0.80f;
		case WeatherManager::WeatherClass::Rain: return 0.91f;
		case WeatherManager::WeatherClass::Storm: return 0.99f;
		case WeatherManager::WeatherClass::Snow: return 0.86f;
		case WeatherManager::WeatherClass::Special: return 0.60f;
		default: return 0.0f;
		}
	}
	float GetAuthoredCloudiness(const RE::TESWeather* weather, float hour, bool& hasAuthoredClouds)
	{
		hasAuthoredClouds = false;
		if (!weather)
			return 0.42f;
		auto* sky = globals::game::sky;
		float sunriseBegin = 6.0f;
		float sunriseEnd = 7.0f;
		float sunsetBegin = 17.0f;
		float sunsetEnd = 18.0f;
		if (sky && sky->currentClimate) {
			const auto& timing = sky->currentClimate->timing;
			sunriseBegin = timing.sunrise.begin / 6.0f;
			sunriseEnd = timing.sunrise.end / 6.0f;
			sunsetBegin = timing.sunset.begin / 6.0f;
			sunsetEnd = timing.sunset.end / 6.0f;
		}
		// Climate timing can be absent or malformed in custom weather plugins.
		// Keep the ordinary Skyrim day anchors rather than building a degenerate
		// interval (or looping indefinitely while unwrapping the day).
		if (!std::isfinite(sunriseBegin) || !std::isfinite(sunriseEnd) ||
			!std::isfinite(sunsetBegin) || !std::isfinite(sunsetEnd) ||
			sunriseEnd <= sunriseBegin || sunsetBegin <= sunriseEnd || sunsetEnd <= sunsetBegin ||
			sunriseBegin < 0.0f || sunsetEnd >= 24.0f) {
			sunriseBegin = 6.0f;
			sunriseEnd = 7.0f;
			sunsetBegin = 17.0f;
			sunsetEnd = 18.0f;
		}
		// Validated climate timestamps already increase monotonically within one day.
		const std::array<float, 4> timeAnchors{
			(sunriseBegin + sunriseEnd) * 0.5f,
			(sunriseEnd + sunsetBegin) * 0.5f,
			(sunsetBegin + sunsetEnd) * 0.5f,
			(sunsetEnd + (sunriseBegin + 24.0f)) * 0.5f
		};
		float sampleHour = std::isfinite(hour) ? hour : 12.0f;
		sampleHour = std::fmod(sampleHour, 24.0f);
		if (sampleHour < 0.0f)
			sampleHour += 24.0f;
		if (sampleHour < timeAnchors[0])
			sampleHour += 24.0f;

		const std::uint32_t layerCount = std::min<std::uint32_t>(
			weather->numCloudLayers > 0 ? weather->numCloudLayers : RE::TESWeather::kTotalLayers,
			RE::TESWeather::kTotalLayers);
		float combinedCoverage = 0.0f;
		for (std::uint32_t layer = 0; layer < layerCount; ++layer) {
			if ((weather->cloudLayerDisabledBits & (1u << layer)) != 0)
				continue;
			// A declared layer with zero alpha is still authored data (notably for
			// clear weather) and must not trigger the cloudy classification fallback.
			hasAuthoredClouds = true;
			float layerAlpha = 0.0f;
			for (std::size_t segment = 0; segment < timeAnchors.size(); ++segment) {
				const std::size_t next = (segment + 1u) % timeAnchors.size();
				const float start = timeAnchors[segment];
				const float end = next == 0u ? timeAnchors[0] + 24.0f : timeAnchors[next];
				if (sampleHour < start || sampleHour > end)
					continue;
				const float a = std::clamp(std::isfinite(weather->cloudAlpha[layer][segment]) ? weather->cloudAlpha[layer][segment] : 0.0f, 0.0f, 1.0f);
				const float b = std::clamp(std::isfinite(weather->cloudAlpha[layer][next]) ? weather->cloudAlpha[layer][next] : 0.0f, 0.0f, 1.0f);
				const float t = end > start ? std::clamp((sampleHour - start) / (end - start), 0.0f, 1.0f) : 0.0f;
				layerAlpha = std::lerp(a, b, t);
				break;
			}
			if (layerAlpha <= 0.001f)
				continue;
			combinedCoverage = 1.0f - (1.0f - combinedCoverage) * (1.0f - layerAlpha);
		}
		return hasAuthoredClouds ? combinedCoverage : 0.42f;
	}

	WeatherProfile BuildWeatherProfile(const WeatherManager::WeatherContext& context)
	{
		const float transition = std::clamp(context.transition.lerpFactor, 0.0f, 1.0f);
		const float gameHour = globals::game::sky && std::isfinite(globals::game::sky->currentGameHour)
			? globals::game::sky->currentGameHour
			: 12.0f;
		bool hasFromCloudLayers = false;
		bool hasToCloudLayers = false;
		const float fromClouds = GetAuthoredCloudiness(context.transition.lastWeather, gameHour, hasFromCloudLayers);
		const float toClouds = GetAuthoredCloudiness(context.transition.currentWeather, gameHour, hasToCloudLayers);
		// Skyrim's active weather records are authoritative when they declare cloud
		// layers, including zero-alpha clear weather. Weather mods without usable
		// layers fall back independently to PIXL's semantic classifier per side.
		auto* weatherManager = WeatherManager::GetSingleton();
		const auto fromClass = weatherManager->ClassifyWeather(context.transition.lastWeather);
		const auto toClass = weatherManager->ClassifyWeather(context.transition.currentWeather);
		const auto fromPreset = GetCloudWeatherPreset(fromClass);
		const auto toPreset = GetCloudWeatherPreset(toClass);
		auto blendPresetValue = [transition](float from, float to) { return std::lerp(from, to, transition); };
		auto authoredOrClassifiedCloudiness = [](float authored, bool hasAuthored,
			WeatherManager::WeatherClass weatherClass, float fallback) {
			const float floor = WeatherCloudFloor(weatherClass);
			float value = hasAuthored ? authored : (floor > 0.0f ? floor : fallback);
			// Keep weather-mod cloud maps authoritative when they are denser, while
			// giving classified cloudy/rain/snow/storm records a visible minimum.
			if (floor > 0.0f)
				value = std::lerp(value, std::max(value, floor), 0.75f);
			return std::clamp(value, 0.0f, 1.0f);
		};
		const float fromCloudsProfile = authoredOrClassifiedCloudiness(
			fromClouds, hasFromCloudLayers, fromClass, context.cloudiness);
		const float toCloudsProfile = authoredOrClassifiedCloudiness(
			toClouds, hasToCloudLayers, toClass, context.cloudiness);
		const float clouds = std::clamp(std::lerp(fromCloudsProfile, toCloudsProfile, transition), 0.0f, 1.0f);
		const float precipitation = std::clamp(context.precipitation, 0.0f, 1.0f);
		const float storm = std::clamp(context.storminess, 0.0f, 1.0f);
		const float fog = std::clamp(context.fogIntensity, 0.0f, 1.0f);
		WeatherProfile profile{};
		// Preserve the tuned cloudy-day look while allowing authored clear records
		// and the semantic clear fallback (0.14) to reach exactly zero.
		const float clearT = std::clamp((clouds - 0.10f) / 0.24f, 0.0f, 1.0f);
		const float clearGate = clearT * clearT * (3.0f - 2.0f * clearT);
		const float presetCoverage = blendPresetValue(fromPreset.coverage, toPreset.coverage);
		const float presetDensity = blendPresetValue(fromPreset.density, toPreset.density);
		const float presetType = blendPresetValue(fromPreset.type, toPreset.type);
		const float presetDetail = blendPresetValue(fromPreset.detail, toPreset.detail);
		const float presetWind = blendPresetValue(fromPreset.wind, toPreset.wind);
		const float presetMaximumTop = blendPresetValue(fromPreset.maximumTop, toPreset.maximumTop);
		profile.upperCoverage = blendPresetValue(fromPreset.upperCoverage, toPreset.upperCoverage);
		profile.upperDensity = blendPresetValue(fromPreset.upperDensity, toPreset.upperDensity);
		profile.upperType = blendPresetValue(fromPreset.upperType, toPreset.upperType);
		profile.upperDetail = blendPresetValue(fromPreset.upperDetail, toPreset.upperDetail);
		profile.upperWindScale = blendPresetValue(fromPreset.upperWind, toPreset.upperWind);
		profile.upperTop = blendPresetValue(fromPreset.upperTop, toPreset.upperTop);
		profile.coverage = std::clamp((0.06f + clouds * 0.88f + precipitation * 0.12f) *
			std::max(clearGate, std::max(precipitation, storm)) * presetCoverage, 0.0f, 1.0f);
		profile.density = std::clamp((0.48f + clouds * 0.62f + precipitation * 0.34f + storm * 0.30f) *
			presetDensity, 0.35f, 1.75f);
		const float semanticType = std::clamp(clouds * 0.58f + precipitation * 0.24f + storm * 0.34f, 0.0f, 1.0f);
		profile.type = std::lerp(semanticType, presetType, 0.42f);
		profile.precipitation = precipitation;
		profile.storm = storm;
		profile.fog = fog;
		profile.sunVisibility = std::clamp(context.sunVisibility, 0.0f, 1.0f);
		// Preserve the configured low cloud base for mountain immersion. Weather
		// variation controls the cloud top and vertical development instead of
		// sinking storm bases or replacing the user's tuned cloud layer.
		const float cloudSeverity = std::clamp(std::max(
			std::max((clouds - 0.14f) * (0.65f / 0.86f), 0.0f),
			std::max(precipitation * 0.90f, storm)), 0.0f, 1.0f);
		const float verticalDevelopment = cloudSeverity * cloudSeverity * (3.0f - 2.0f * cloudSeverity);
		profile.baseOffset = cloudSeverity * 3500.0f;
		profile.cloudTop = std::lerp(45000.0f, presetMaximumTop, verticalDevelopment);
		profile.windScale = std::lerp(0.20f, 0.58f, verticalDevelopment) * presetWind;
		profile.detailScale = std::lerp(0.70f, 1.15f, std::max(clouds, cloudSeverity)) * presetDetail;
		// The cirrus-like upper deck remains distinct from the lower storm body.
		// Weather classes shape it independently, then transitions blend smoothly.
		profile.upperCoverage *= std::lerp(0.72f, 1.0f, std::max(clouds, cloudSeverity));
		profile.upperDensity *= std::lerp(0.80f, 1.0f, std::max(clouds, cloudSeverity));

		// WeatherManager already interpolates its semantic signals between the
		// outgoing and incoming records. Keep this conversion continuous as well;
		// branching on its midpoint class would create a visible half-transition pop.
		return profile;
	}

	WeatherProfile SmoothWeatherProfile(const WeatherProfile& target, WeatherProfile& state,
		bool& initialized, float deltaTime)
	{
		if (!initialized) {
			state = target;
			initialized = true;
			return state;
		}
		const float boundedDelta = std::clamp(deltaTime, 0.0f, 0.1f);
		auto approach = [boundedDelta](float current, float next, float seconds) {
			const float weight = 1.0f - std::exp(-boundedDelta / seconds);
			return std::lerp(current, next, weight);
		};
		state.coverage = approach(state.coverage, target.coverage, 9.0f);
		state.density = approach(state.density, target.density, 18.0f);
		state.type = approach(state.type, target.type, 16.0f);
		state.precipitation = approach(state.precipitation, target.precipitation, 8.0f);
		state.storm = approach(state.storm, target.storm, 12.0f);
		state.fog = approach(state.fog, target.fog, 12.0f);
		state.sunVisibility = approach(state.sunVisibility, target.sunVisibility, 10.0f);
		state.baseOffset = approach(state.baseOffset, target.baseOffset, 32.0f);
		state.cloudTop = approach(state.cloudTop, target.cloudTop, 42.0f);
		state.windScale = approach(state.windScale, target.windScale, 14.0f);
		state.detailScale = approach(state.detailScale, target.detailScale, 20.0f);
		state.upperCoverage = approach(state.upperCoverage, target.upperCoverage, 18.0f);
		state.upperDensity = approach(state.upperDensity, target.upperDensity, 22.0f);
		state.upperType = approach(state.upperType, target.upperType, 18.0f);
		state.upperDetail = approach(state.upperDetail, target.upperDetail, 20.0f);
		state.upperWindScale = approach(state.upperWindScale, target.upperWindScale, 14.0f);
		state.upperTop = approach(state.upperTop, target.upperTop, 42.0f);
		return state;
	}

	std::unique_ptr<Texture2D> CreateTexture2D(
		std::uint32_t width,
		std::uint32_t height,
		DXGI_FORMAT format,
		const char* name)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto texture = std::make_unique<Texture2D>(desc, name);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		texture->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		texture->CreateUAV(uav);
		return texture;
	}
}

#pragma warning(push)
#pragma warning(disable : 4324)
struct VolumetricClouds::Runtime
{
	std::array<std::unique_ptr<Texture2D>, 2> weatherMap;
	std::unique_ptr<Texture2D> rawCloud;
	std::unique_ptr<Texture2D> rawDepth;
	std::array<std::unique_ptr<Texture2D>, 2> historyCloud;
	std::array<std::unique_ptr<Texture2D>, 2> historyDepth;
	std::unique_ptr<ConstantBuffer> constants;
	winrt::com_ptr<ID3D11SamplerState> linearClamp;
	winrt::com_ptr<ID3D11ComputeShader> weatherShader;
	winrt::com_ptr<ID3D11ComputeShader> raymarchShader;
	winrt::com_ptr<ID3D11ComputeShader> temporalShader;
	winrt::com_ptr<ID3D11ComputeShader> compositeShader;
	std::uint64_t temporalHistoryId{};
	std::uint32_t width{};
	std::uint32_t height{};
	std::uint32_t lowWidth{};
	std::uint32_t lowHeight{};
	std::uint32_t activeQuality{ ~0u };
	bool historyFlip{};
	bool weatherFlip{};
	bool weatherValid{};
	bool frameCloudsAvailable{};
	bool resourceFailureLogged{};
	double lastGameTimeDays{};
	bool gameTimeValid{};
	std::uint32_t lightningRandomState{ 0x6D2B79F5u };
	float lightningCountdown{ 4.0f };
	float lightningAge{ 10.0f };
	float lightningSeed{};
	double lightningX{};
	double lightningY{};
	double lightningGroundZ{};
	double lastKnownTimeScale{ 20.0 };
	std::chrono::steady_clock::time_point lastWallClockUpdate{ std::chrono::steady_clock::now() };
	float weatherAccumulator{ kWeatherUpdateInterval };
	float weatherSimulationAccumulator{};
	double cloudDisplacementX{};
	double cloudDisplacementY{};
	double upperCloudDisplacementX{};
	double upperCloudDisplacementY{};
	double weatherAdvectionX{};
	double weatherAdvectionY{};
	double weatherDisplacementX{};
	double weatherDisplacementY{};
	WeatherProfile weatherProfile{};
	bool weatherProfileValid{};
	float2 weatherOrigin{};
	float2 previousWeatherOrigin{};
};
#pragma warning(pop)

VolumetricClouds::VolumetricClouds() : runtime(std::make_unique<Runtime>()) {}

VolumetricClouds::~VolumetricClouds() = default;

std::pair<std::string, std::vector<std::string>> VolumetricClouds::GetModuleSummary()
{
	return { "Renders world-space, weather-driven three-dimensional cloud cover over Skyrim.",
		{ "Multi-scale procedural density with cloud bottoms, tops, erosion and storm development",
		  "Sun, moon and ambient scattering with self-shadowing and multiple-scattering approximation",
		  "Persistent weather fronts, temporal reprojection, adaptive raymarching and cloud shadows",
		  "Weather-aware aurora and procedural in-cloud lightning strikes" } };
}

bool VolumetricClouds::CompileShaders()
{
	if (!runtime)
		return false;
	constexpr wchar_t shaderPath[] = L"Data\\PIXL-Shaders\\VolumetricClouds\\VolumetricCloudsCS.hlsl";
	auto compile = [](const wchar_t* path, const char* entry) {
		winrt::com_ptr<ID3D11ComputeShader> shader;
		shader.attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(path, {}, "cs_5_0", entry)));
		return shader;
	};
	runtime->weatherShader = compile(shaderPath, "WeatherMapCS");
	runtime->raymarchShader = compile(shaderPath, "RaymarchCS");
	runtime->temporalShader = compile(shaderPath, "TemporalCS");
	runtime->compositeShader = compile(shaderPath, "CompositeCS");
	return runtime->weatherShader && runtime->raymarchShader && runtime->temporalShader && runtime->compositeShader;
}

void VolumetricClouds::SetupResources()
{
	if (!runtime || !globals::d3d::device || !globals::d3d::context) {
		loaded = false;
		return;
	}
	try {
		runtime->constants = std::make_unique<ConstantBuffer>(
			ConstantBufferDesc<CloudConstants>(), "VolumetricClouds::Constants");
		runtime->weatherMap[0] = CreateTexture2D(kWeatherMapSize, kWeatherMapSize,
			DXGI_FORMAT_R16G16B16A16_FLOAT, "VolumetricClouds::WeatherA");
		runtime->weatherMap[1] = CreateTexture2D(kWeatherMapSize, kWeatherMapSize,
			DXGI_FORMAT_R16G16B16A16_FLOAT, "VolumetricClouds::WeatherB");
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&sampler, runtime->linearClamp.put()));
	} catch (const std::exception& error) {
		logger::error("[PIXL Volumetric Clouds] resource initialization failed: {}", error.what());
		loaded = false;
		settings.Enabled = false;
		return;
	}
	if (!CompileShaders()) {
		logger::error("[PIXL Volumetric Clouds] one or more compute shaders failed to compile; module disabled");
		loaded = false;
		settings.Enabled = false;
		return;
	}
	if (runtime->temporalHistoryId == 0) {
		runtime->temporalHistoryId = PIXL::Renderer::TemporalContext::Get().RegisterHistory({
			.name = "PIXL Volumetric Clouds",
			.owner = "VolumetricClouds",
			.invalidateOn = PIXL::Renderer::AllTemporalInvalidations,
			.memoryBytes = 0,
			.reset = [this](PIXL::Renderer::TemporalInvalidationReason) { InvalidateHistory(); }
		});
	}
	logger::info("[PIXL Volumetric Clouds] initialized independent DX11 weather, raymarch, temporal and composite passes");
}

bool VolumetricClouds::EnsureFrameResources(std::uint32_t width, std::uint32_t height)
{
	if (!runtime || width == 0 || height == 0)
		return false;
	const std::uint32_t quality = std::min(settings.Quality, 3u);
	const std::uint32_t divisor = kCloudDownsample[quality];
	const std::uint32_t lowWidth = (width + divisor - 1u) / divisor;
	const std::uint32_t lowHeight = (height + divisor - 1u) / divisor;
	if (runtime->rawCloud && runtime->width == width && runtime->height == height &&
		runtime->lowWidth == lowWidth && runtime->lowHeight == lowHeight && runtime->activeQuality == quality)
		return true;
	try {
		runtime->rawCloud = CreateTexture2D(lowWidth, lowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
			"VolumetricClouds::RawRadiance");
		runtime->rawDepth = CreateTexture2D(lowWidth, lowHeight, DXGI_FORMAT_R16_FLOAT,
			"VolumetricClouds::RawDepth");
		for (std::uint32_t index = 0; index < 2; ++index) {
			runtime->historyCloud[index] = CreateTexture2D(lowWidth, lowHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
				index == 0 ? "VolumetricClouds::HistoryRadianceA" : "VolumetricClouds::HistoryRadianceB");
			runtime->historyDepth[index] = CreateTexture2D(lowWidth, lowHeight, DXGI_FORMAT_R16_FLOAT,
				index == 0 ? "VolumetricClouds::HistoryDepthA" : "VolumetricClouds::HistoryDepthB");
		}
	} catch (const std::exception& error) {
		if (!runtime->resourceFailureLogged) {
			logger::error("[PIXL Volumetric Clouds] frame resource creation failed: {}", error.what());
			runtime->resourceFailureLogged = true;
		}
		// A partial allocation must never pass the next frame's cache-hit check.
		runtime->width = runtime->height = runtime->lowWidth = runtime->lowHeight = 0u;
		runtime->activeQuality = ~0u;
		InvalidateHistory();
		return false;
	}
	runtime->width = width;
	runtime->height = height;
	runtime->lowWidth = lowWidth;
	runtime->lowHeight = lowHeight;
	runtime->activeQuality = quality;
	runtime->resourceFailureLogged = false;
	InvalidateHistory();
	const std::uint64_t historyBytes = static_cast<std::uint64_t>(lowWidth) * lowHeight * (8u + 2u) * 2u;
	if (runtime->temporalHistoryId != 0)
		PIXL::Renderer::TemporalContext::Get().SetHistoryMemory(runtime->temporalHistoryId, historyBytes);
	logger::info("[PIXL Volumetric Clouds] frame resources {}x{} -> {}x{} (quality {})",
		width, height, lowWidth, lowHeight, quality);
	return true;
}

void VolumetricClouds::InvalidateHistory()
{
	if (!runtime)
		return;
	runtime->historyFlip = false;
	if (runtime->temporalHistoryId != 0)
		PIXL::Renderer::TemporalContext::Get().SetHistoryValid(runtime->temporalHistoryId, false);
}

void VolumetricClouds::Reset()
{
	if (!settings.Enabled) {
		InvalidateHistory();
		if (runtime)
			runtime->weatherValid = false;
	}
}

void VolumetricClouds::ClearShaderCache()
{
	if (!CompileShaders()) {
		settings.Enabled = false;
		logger::error("[PIXL Volumetric Clouds] shader reload failed; rendering disabled");
	}
	InvalidateHistory();
}

void VolumetricClouds::DrawClouds()
{
	if (runtime)
		runtime->frameCloudsAvailable = false;
	if (!loaded || !settings.Enabled || !runtime || !globals::state || !globals::state->inWorld ||
		!globals::game::renderer || !globals::deferred || !globals::d3d::context || !runtime->constants ||
		!runtime->weatherShader || !runtime->raymarchShader || !runtime->temporalShader || !runtime->compositeShader ||
		!globals::game::perFrame.get() || !*globals::game::perFrame || !globals::state->sharedDataCB)
		return;
	// All subsequent runtime dereferences are covered by the early guard.
	auto& rt = *runtime;
	const auto& weatherContext = WeatherManager::GetSingleton()->GetContext();
	if (!weatherContext.exterior || Util::IsInterior()) {
		InvalidateHistory();
		return;
	}
	auto* renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[globals::deferred->forwardRenderTargets[0]];
	ID3D11ShaderResourceView* depthSRV = Util::GetCurrentSceneDepthSRV(false);
	if (!main.texture || !main.UAV || !depthSRV)
		return;
	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(REX::W32::AsW32(&mainDesc));
	if (mainDesc.Width == 0u || mainDesc.Height == 0u)
		return;
	const auto dynamicSize = Util::ConvertToDynamic(
		float2{ static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) }, true);
	const auto safeDimension = [](float size, std::uint32_t maximum) {
		const double finiteSize = std::isfinite(size) ? static_cast<double>(size) : 1.0;
		return static_cast<std::uint32_t>(std::clamp(finiteSize, 1.0, static_cast<double>(maximum)));
	};
	const std::uint32_t width = safeDimension(dynamicSize.x, mainDesc.Width);
	const std::uint32_t height = safeDimension(dynamicSize.y, mainDesc.Height);
	if (!EnsureFrameResources(width, height))
		return;

	const float gameDeltaTime = globals::game::deltaTime && std::isfinite(*globals::game::deltaTime)
		? std::clamp(*globals::game::deltaTime, 0.0f, 0.10f)
		: 1.0f / 60.0f;
	const auto wallClockNow = std::chrono::steady_clock::now();
	const double wallClockDelta = std::chrono::duration<double>(wallClockNow - rt.lastWallClockUpdate).count();
	rt.lastWallClockUpdate = wallClockNow;
	// Skyrim can report a zero frame delta while paused or while a camera tool
	// owns the view. Keep the atmospheric simulation alive during those states.
	const float deltaTime = gameDeltaTime > 1.0e-4f ? gameDeltaTime :
		(static_cast<float>(std::isfinite(wallClockDelta) ? wallClockDelta : 1.0 / 60.0));
	const bool directorVideo = TuningWorkspaceRenderer::IsDirectorVideoModeActive();
	const bool gamePaused = globals::game::ui && globals::game::ui->GameIsPaused();
	const bool freezeCloudsForPhoto = TuningWorkspaceRenderer::IsDirectorStillPhotoModeActive();
	const bool freezeForPause = gamePaused && !directorVideo;
	const float boundedDeltaTime = (freezeCloudsForPhoto || freezeForPause) ? 0.0f :
		std::clamp(deltaTime, 0.0f, 0.10f);
	// Cloud velocity is expressed per Skyrim simulation second. Advance ordinary
	// frames at the current timescale, and use the calendar for discrete waits.
	// The calendar is float-backed, so its normal frame-to-frame quantization is
	// deliberately ignored in favor of this continuous frame clock.
	double timeScale = 20.0;
	if (auto* calendar = RE::Calendar::GetSingleton()) {
		const double candidate = static_cast<double>(calendar->GetTimescale());
		if (std::isfinite(candidate) && candidate > 0.0)
			rt.lastKnownTimeScale = candidate;
		timeScale = rt.lastKnownTimeScale;
	}
	double simulationDelta = static_cast<double>(boundedDeltaTime) * timeScale;
	if (auto* calendar = RE::Calendar::GetSingleton()) {
		const double gameTimeDays = static_cast<double>(calendar->GetCurrentGameTime());
		if (std::isfinite(gameTimeDays)) {
			if (rt.gameTimeValid) {
				const double elapsedGameDays = gameTimeDays - rt.lastGameTimeDays;
				if (elapsedGameDays >= 0.0) {
					const double calendarDelta = elapsedGameDays * 86400.0;
					// During Wait, the game may be paused while its calendar advances.
					// Consume each positive calendar step so resumed clouds land at the
					// correct world-space location. While playing, ignore float-clock
					// quantization and only accept unmistakable time jumps.
					const double jumpThreshold = std::max(30.0, simulationDelta * 30.0);
					if (std::isfinite(calendarDelta) &&
						((gamePaused && calendarDelta > 0.0) || calendarDelta > jumpThreshold))
						simulationDelta = std::min(calendarDelta, 86400.0 * 31.0);
				} else {
					// Calendar edits can rewind time. Rebase without reversing cloud motion.
					InvalidateHistory();
				}
			} else {
				rt.gameTimeValid = true;
			}
			rt.lastGameTimeDays = gameTimeDays;
		}
	}
	if (freezeCloudsForPhoto || (freezeForPause && simulationDelta <= 0.0))
		simulationDelta = 0.0;
	// A large calendar jump (wait, fast travel, or console time change) is a
	// deliberate discontinuity: do not blend the old cloud image into the new one.
	if (simulationDelta > 2.0)
		InvalidateHistory();
	rt.weatherAccumulator = std::min(rt.weatherAccumulator + boundedDeltaTime, 1.0f);
	rt.weatherSimulationAccumulator = std::min(
		rt.weatherSimulationAccumulator + static_cast<float>(std::min(simulationDelta, 1.0e7)), 1.0e7f);
	const auto temporalSnapshot = PIXL::Renderer::TemporalContext::Get().GetFrameSnapshot();
	const float cameraX = static_cast<float>(temporalSnapshot.current.absoluteCameraPosition[0]);
	const float cameraY = static_cast<float>(temporalSnapshot.current.absoluteCameraPosition[1]);
	const float stormStrength = std::clamp(weatherContext.storminess, 0.0f, 1.0f);
	// Keep the PIXL aurora available alongside Skyrim's authored effect. The cloud
	// compositor naturally veils both layers where volumetric density is present.
	const float auroraWeatherGate = 1.0f - std::clamp(
		std::max(weatherContext.precipitation, weatherContext.snowIntensity) * 1.4f, 0.0f, 1.0f);
	const float auroraStrength = FiniteClamp(settings.AuroraIntensity, 0.35f, 0.0f, 1.5f) * auroraWeatherGate;
	float lightningFlash = 0.0f;
	const auto* lightningWeather = weatherContext.transition.currentWeather;
	const bool authoredLightning = lightningWeather &&
		(lightningWeather->data.thunderLightningFrequency > 0 || weatherContext.weatherClass == WeatherManager::WeatherClass::Storm);
	const bool rainyWeather = lightningWeather &&
		(lightningWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy) ||
			weatherContext.weatherClass == WeatherManager::WeatherClass::Storm ||
			weatherContext.weatherClass == WeatherManager::WeatherClass::Rain);
	const bool lightningWeatherActive = settings.LightningIntensity > 0.001f && authoredLightning &&
		rainyWeather && weatherContext.precipitation > 0.05f;
	// Wait/fast-travel must not replay skipped bolts; inactive storms reset the same timer.
	if (simulationDelta > 2.0 || !lightningWeatherActive) {
		rt.lightningAge = 10.0f;
		rt.lightningCountdown = 4.0f;
	} else {
		// Lightning is a visible real-time event, not accelerated Skyrim calendar
		// time. Using simulationDelta here reduced a sub-second flash to a few
		// hundredths of a second at the normal game timescale.
		const float lightningDelta = boundedDeltaTime;
		rt.lightningAge += lightningDelta;
		rt.lightningCountdown -= lightningDelta;
		if (rt.lightningCountdown <= 0.0f) {
			auto nextRandom = [&]() {
				rt.lightningRandomState ^= rt.lightningRandomState << 13;
				rt.lightningRandomState ^= rt.lightningRandomState >> 17;
				rt.lightningRandomState ^= rt.lightningRandomState << 5;
				return rt.lightningRandomState;
			};
			const auto random01 = [&]() { return (nextRandom() & 0x00FFFFFFu) / 16777215.0f; };
			rt.lightningSeed = random01();
			const float angle = random01() * 6.28318530718f;
			const float range = std::lerp(28000.0f, 11000.0f, stormStrength);
			rt.lightningX = temporalSnapshot.current.absoluteCameraPosition[0] + std::cos(angle) * range;
			rt.lightningY = temporalSnapshot.current.absoluteCameraPosition[1] + std::sin(angle) * range;
			// Resolve an actual LAND height at the strike point so the projected
			// leader reaches terrain. Keep the camera-relative estimate as a safe
			// fallback for unloaded cells, water, or worldspaces without LAND data.
			const double cameraZ = temporalSnapshot.current.absoluteCameraPosition[2];
			rt.lightningGroundZ = std::max(0.0, cameraZ - 2500.0);
			if (auto* tes = RE::TES::GetSingleton()) {
				float terrainHeight = static_cast<float>(rt.lightningGroundZ);
				const RE::NiPoint3 strikeSample{ static_cast<float>(rt.lightningX),
					static_cast<float>(rt.lightningY), static_cast<float>(cameraZ) };
				if (tes->GetLandHeight(strikeSample, terrainHeight) && std::isfinite(terrainHeight))
					rt.lightningGroundZ = static_cast<double>(terrainHeight) + 8.0;
			}
			rt.lightningAge = 0.0f;
			const float profileFrequency = lightningWeather && lightningWeather->data.thunderLightningFrequency > 0
				? std::clamp(static_cast<float>(lightningWeather->data.thunderLightningFrequency) / 10.0f, 0.25f, 1.0f)
				: stormStrength;
			const float baseInterval = std::lerp(38.0f, 13.0f, std::max(stormStrength, profileFrequency));
			const float variation = 0.65f + random01() * 0.70f;
			rt.lightningCountdown = baseInterval * variation /
				FiniteClamp(settings.LightningIntensity, 1.0f, 0.25f, 2.0f);
		}
		const float age = rt.lightningAge;
		if (age < 3.6f) {
			// Let the main discharge read for longer, then add two seeded, softer
			// re-strikes and a slow residual glow. The seed is fixed per strike so
			// the timing is randomized between events but temporally stable per event.
			const float pulseA = 0.42f + std::fmod(rt.lightningSeed * 17.13f, 1.0f) * 0.28f;
			const float pulseB = 1.02f + std::fmod(rt.lightningSeed * 43.71f, 1.0f) * 0.62f;
			const float first = 1.22f * std::exp2(-age * 3.6f);
			const float second = 0.42f * std::exp2(-std::abs(age - pulseA) * 11.0f);
			const float third = 0.24f * std::exp2(-std::abs(age - pulseB) * 8.0f);
			const float afterglow = 0.18f * std::exp2(-age * 0.9f);
			lightningFlash = std::clamp((first + second + third + afterglow) * 1.3f *
				FiniteClamp(settings.LightningIntensity, 1.0f, 0.0f, 2.0f), 0.0f, 2.0f);
		}
	}
	const float texelSize = kWeatherExtent / static_cast<float>(kWeatherMapSize);
	const float2 newOrigin{
		std::floor(cameraX / texelSize) * texelSize,
		std::floor(cameraY / texelSize) * texelSize
	};
	// Weather synthesis is a full 512x512 dispatch. Recenter requests are folded
	// into the existing cadence instead of triggering a second dispatch whenever
	// the camera crosses a snapped map texel.
	const bool weatherUpdate = !rt.weatherValid || rt.weatherAccumulator >= kWeatherUpdateInterval;
	if (weatherUpdate) {
		rt.previousWeatherOrigin = rt.weatherValid ? rt.weatherOrigin : newOrigin;
		rt.weatherOrigin = newOrigin;
	}

	const WeatherProfile targetProfile = BuildWeatherProfile(weatherContext);
	const WeatherProfile profile = SmoothWeatherProfile(targetProfile, rt.weatherProfile,
		rt.weatherProfileValid, boundedDeltaTime);
	const std::uint32_t quality = std::min(settings.Quality, 3u);
	float windAngle = 0.75f;
	if (globals::game::sky && std::isfinite(globals::game::sky->windAngle))
		windAngle = globals::game::sky->windAngle;
	const float windUserScale = FiniteClamp(settings.WindSpeed, 0.41f, 0.0f, 4.0f) / 0.41f;
	const float windSpeed = (45.0f + weatherContext.windIntensity * 420.0f) *
		profile.windScale * windUserScale;
	const float windVelocityX = std::cos(windAngle) * windSpeed;
	const float windVelocityY = std::sin(windAngle) * windSpeed;
	constexpr float degreesToRadians = 0.01745329251994329577f;
	const float upperWindAngle = windAngle +
		FiniteClamp(settings.HighCloudWindDirection, 28.0f, -180.0f, 180.0f) * degreesToRadians;
	const float upperWindBaseSpeed = (45.0f + weatherContext.windIntensity * 420.0f) *
		profile.upperWindScale * FiniteClamp(settings.HighCloudWindSpeed, 0.72f, 0.0f, 2.0f) / 0.72f;
	const float upperWindVelocityX = std::cos(upperWindAngle) * upperWindBaseSpeed;
	const float upperWindVelocityY = std::sin(upperWindAngle) * upperWindBaseSpeed;
	const double windX = static_cast<double>(windVelocityX);
	const double windY = static_cast<double>(windVelocityY);
	// Integrate each frame's wind vector into a persistent path. Applying the
	// latest direction to total elapsed time would rotate old travel whenever
	// Skyrim's wind direction changed, making clouds appear to ping-pong.
	AdvancePeriodic(rt.cloudDisplacementX, windX * simulationDelta, kCloudAdvectionPeriod);
	AdvancePeriodic(rt.cloudDisplacementY, windY * simulationDelta, kCloudAdvectionPeriod);
	AdvancePeriodic(rt.upperCloudDisplacementX,
		static_cast<double>(upperWindVelocityX) * simulationDelta, kCloudAdvectionPeriod);
	AdvancePeriodic(rt.upperCloudDisplacementY,
		static_cast<double>(upperWindVelocityY) * simulationDelta, kCloudAdvectionPeriod);
	AdvancePeriodic(rt.weatherAdvectionX, windX * simulationDelta,
		static_cast<double>(kWeatherExtent * 0.33f) * 4096.0);
	AdvancePeriodic(rt.weatherAdvectionY, windY * simulationDelta,
		static_cast<double>(kWeatherExtent * 0.33f) * 4096.0);
	// This interval-only offset reprojects the previous weather map and is reset
	// after its dispatch; unlike the two persistent phases it stays small.
	rt.weatherDisplacementX += windX * simulationDelta;
	rt.weatherDisplacementY += windY * simulationDelta;
	// Raise the full weather-class layer slightly while preserving its configured
	// thickness and the relative spacing between clear, rain, snow, and storm tops.
	constexpr float cloudAltitudeScale = 1.075f;
	const float cloudBase = std::max(
		(FiniteClamp(settings.CloudBase, 15000.0f, 0.0f, 120000.0f) + profile.baseOffset) * cloudAltitudeScale,
		0.0f);
	const float authoredThicknessScale = FiniteClamp(settings.CloudThickness, 50085.0f, 2000.0f, 90000.0f) / 50085.0f;
	const float thickness = std::max(profile.cloudTop - cloudBase / cloudAltitudeScale, 5000.0f) *
		cloudAltitudeScale * authoredThicknessScale;
	CloudConstants constants{};
	constants.renderSize = { static_cast<float>(rt.lowWidth), static_cast<float>(rt.lowHeight),
		1.0f / rt.lowWidth, 1.0f / rt.lowHeight };
	constants.fullSize = { static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height };
	constants.layer = { cloudBase, cloudBase + thickness,
		FiniteClamp(settings.MaximumDistance, 320000.0f, 50000.0f, 800000.0f), kCloudCurvatureRadius };
	constants.weather = { std::clamp(profile.coverage * FiniteClamp(settings.Coverage, 0.60f, 0.0f, 2.0f), 0.0f, 1.0f),
		profile.density * FiniteClamp(settings.Density, 1.55f, 0.1f, 2.5f), profile.type, profile.precipitation };
	constants.weatherSecondary = { profile.storm, profile.fog,
		std::clamp(weatherContext.transition.lerpFactor, 0.0f, 1.0f), profile.sunVisibility };
	constants.windTime = { windVelocityX, windVelocityY,
		FiniteClamp(settings.OriginalSkyExposure, 0.0f, -3.0f, 2.0f), rt.weatherSimulationAccumulator };
	float3 strikeColor{ 0.78f, 0.88f, 1.0f };
	if (lightningWeather) {
		strikeColor = { lightningWeather->data.lightningColor.red / 255.0f,
			lightningWeather->data.lightningColor.green / 255.0f,
			lightningWeather->data.lightningColor.blue / 255.0f };
		if (strikeColor.x + strikeColor.y + strikeColor.z < 0.05f)
			strikeColor = { 0.78f, 0.88f, 1.0f };
	}
	constants.cloudMotion = { constants.windTime.x, constants.windTime.y,
		static_cast<float>(std::min(simulationDelta, 0.10)), strikeColor.x };
	constants.cloudOffset = { static_cast<float>(rt.cloudDisplacementX),
		static_cast<float>(rt.cloudDisplacementY), static_cast<float>(rt.weatherDisplacementX),
		static_cast<float>(rt.weatherDisplacementY) };
	constants.weatherAdvection = { static_cast<float>(rt.weatherAdvectionX),
		static_cast<float>(rt.weatherAdvectionY), strikeColor.y, strikeColor.z };
	constants.weatherMap = { rt.weatherOrigin.x, rt.weatherOrigin.y, kWeatherExtent, 1.0f / kWeatherExtent };
	constants.previousWeatherMap = { rt.previousWeatherOrigin.x, rt.previousWeatherOrigin.y,
		kWeatherExtent, rt.weatherValid ? 1.0f : 0.0f };
	constants.shape = { FiniteClamp(settings.ShapeScale, 1.26f, 0.2f, 3.0f),
		FiniteClamp(settings.DetailScale, 1.5f, 0.2f, 3.0f) * profile.detailScale,
		FiniteClamp(settings.Erosion, 0.62f, 0.0f, 1.5f),
		FiniteClamp(settings.Turbulence, 0.44f, 0.0f, 1.5f) };
	constants.lighting = { FiniteClamp(settings.Extinction, 1.01f, 0.1f, 3.0f),
		FiniteClamp(settings.Scattering, 0.8f, 0.0f, 3.0f), FiniteClamp(settings.AmbientLight, 0.83f, 0.0f, 3.0f),
		FiniteClamp(settings.Powder, 0.55f, 0.0f, 2.0f) };
	constants.phase = { FiniteClamp(settings.ForwardScattering, 0.72f, 0.0f, 0.92f),
		FiniteClamp(settings.BackScattering, -0.18f, -0.8f, 0.0f), FiniteClamp(settings.SilverLining, 0.85f, 0.0f, 2.0f),
		FiniteClamp(settings.MultipleScattering, 0.65f, 0.0f, 1.5f) };
	const float baseTemporalResponse = FiniteClamp(settings.TemporalResponse, 0.92f, 0.0f, 0.98f);
	const float lightningTemporalResponse = lightningFlash > 0.02f
		? std::lerp(baseTemporalResponse, std::min(baseTemporalResponse, 0.22f),
			std::clamp(lightningFlash, 0.0f, 1.0f))
		: baseTemporalResponse;
	const bool temporalValid = rt.temporalHistoryId != 0 &&
		PIXL::Renderer::TemporalContext::Get().IsHistoryValid(rt.temporalHistoryId) &&
		temporalSnapshot.previousFrameValid;
	constants.temporal = { lightningTemporalResponse, temporalValid ? 1.0f : 0.0f,
		static_cast<float>(temporalSnapshot.current.frameIndex & 1023u), 0.12f };
	constants.composite = { FiniteClamp(settings.ShadowStrength, 0.52f, 0.0f, 1.0f), 180000.0f,
		FiniteClamp(settings.OriginalSkyContrast, 1.0f, 0.5f, 2.0f),
		static_cast<float>(std::min(settings.DebugMode, 2u)) };
	constants.skyEffects = { auroraStrength, rt.weatherSimulationAccumulator, lightningFlash,
		rt.lightningSeed };
	constants.lightningPosition = { static_cast<float>(rt.lightningX),
		static_cast<float>(rt.lightningY), static_cast<float>(rt.lightningGroundZ), 0.0f };
	constants.primarySteps = kCloudPrimarySteps[quality];
	constants.lightSteps = kCloudLightSteps[quality];
	constants.weatherHistoryValid = rt.weatherValid ? 1u : 0u;
	constants.updateWeather = weatherUpdate ? 1u : 0u;
	const float upperBase = FiniteClamp(settings.HighCloudBase, 30000.0f, 20000.0f, 70000.0f);
	const float requestedUpperTop = FiniteClamp(settings.HighCloudTop, 100000.0f, 65000.0f, 110000.0f);
	const float upperTop = std::max(upperBase + 2000.0f, std::min(requestedUpperTop, profile.upperTop));
	constants.upperLayer = { upperBase, upperTop,
		settings.EnableHighCloudLayer ? FiniteClamp(settings.HighCloudCoverage, 0.42f, 0.0f, 1.0f) * profile.upperCoverage : 0.0f,
		profile.upperDensity * FiniteClamp(settings.HighCloudDensity, 0.80f, 0.0f, 2.0f) };
	constants.upperShape = { profile.upperType,
		FiniteClamp(settings.ShapeScale, 1.08f, 0.2f, 3.0f) * 0.68f,
		FiniteClamp(settings.HighCloudDetail, 0.65f, 0.1f, 2.0f) * profile.upperDetail,
		FiniteClamp(settings.Erosion, 0.62f, 0.0f, 1.5f) * 0.72f };
	constants.upperWind = { upperWindVelocityX, upperWindVelocityY,
		static_cast<float>(rt.upperCloudDisplacementX), static_cast<float>(rt.upperCloudDisplacementY) };
	try {
		rt.constants->Update(constants);
	} catch (const std::exception& error) {
		if (!rt.resourceFailureLogged) {
			logger::error("[PIXL Volumetric Clouds] constant upload failed: {}", error.what());
			rt.resourceFailureLogged = true;
		}
		return;
	}

	auto* context = globals::d3d::context;
	ID3D11Buffer* sharedBuffer = globals::state->sharedDataCB->CB();
	ID3D11Buffer* frameBuffer = *globals::game::perFrame;
	ID3D11Buffer* cloudBuffer = rt.constants->CB();
	context->CSSetConstantBuffers(5, 1, &sharedBuffer);
	context->CSSetConstantBuffers(12, 1, &frameBuffer);
	context->CSSetConstantBuffers(13, 1, &cloudBuffer);
	ID3D11SamplerState* sampler = rt.linearClamp.get();
	context->CSSetSamplers(0, 1, &sampler);
	ID3D11ShaderResourceView* nullViews4[4]{};
	ID3D11UnorderedAccessView* nullOutputs2[2]{};

	if (weatherUpdate) {
		const std::uint32_t sourceIndex = rt.weatherFlip ? 1u : 0u;
		const std::uint32_t destinationIndex = 1u - sourceIndex;
		ID3D11ShaderResourceView* weatherInput = rt.weatherMap[sourceIndex]->srv.get();
		ID3D11UnorderedAccessView* weatherOutput = rt.weatherMap[destinationIndex]->uav.get();
		context->CSSetShaderResources(0, 1, &weatherInput);
		context->CSSetUnorderedAccessViews(0, 1, &weatherOutput, nullptr);
		context->CSSetShader(rt.weatherShader.get(), nullptr, 0);
		globals::profiler->BeginPass("VolumetricClouds::Weather");
		context->Dispatch(kWeatherMapSize / 8u, kWeatherMapSize / 8u, 1u);
		globals::profiler->EndPass();
		context->CSSetShaderResources(0, 1, nullViews4);
		context->CSSetUnorderedAccessViews(0, 1, nullOutputs2, nullptr);
		rt.weatherFlip = destinationIndex != 0u;
		rt.weatherValid = true;
		rt.weatherAccumulator = 0.0f;
		rt.weatherSimulationAccumulator = 0.0f;
		rt.weatherDisplacementX = 0.0;
		rt.weatherDisplacementY = 0.0;
	}
	const std::uint32_t currentWeatherIndex = rt.weatherFlip ? 1u : 0u;
	ID3D11ShaderResourceView* rayViews[2]{ rt.weatherMap[currentWeatherIndex]->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* rayOutputs[2]{ rt.rawCloud->uav.get(), rt.rawDepth->uav.get() };
	context->CSSetShaderResources(0, 2, rayViews);
	context->CSSetUnorderedAccessViews(0, 2, rayOutputs, nullptr);
	context->CSSetShader(rt.raymarchShader.get(), nullptr, 0);
	globals::profiler->BeginPass("VolumetricClouds::Raymarch");
	context->Dispatch((rt.lowWidth + 7u) / 8u, (rt.lowHeight + 7u) / 8u, 1u);
	globals::profiler->EndPass();
	context->CSSetShaderResources(0, 2, nullViews4);
	context->CSSetUnorderedAccessViews(0, 2, nullOutputs2, nullptr);

	const std::uint32_t previousIndex = rt.historyFlip ? 1u : 0u;
	const std::uint32_t resolvedIndex = 1u - previousIndex;
	ID3D11ShaderResourceView* temporalViews[4]{ rt.rawCloud->srv.get(), rt.rawDepth->srv.get(),
		rt.historyCloud[previousIndex]->srv.get(), rt.historyDepth[previousIndex]->srv.get() };
	ID3D11UnorderedAccessView* temporalOutputs[2]{ rt.historyCloud[resolvedIndex]->uav.get(),
		rt.historyDepth[resolvedIndex]->uav.get() };
	context->CSSetShaderResources(0, 4, temporalViews);
	context->CSSetUnorderedAccessViews(0, 2, temporalOutputs, nullptr);
	context->CSSetShader(rt.temporalShader.get(), nullptr, 0);
	globals::profiler->BeginPass("VolumetricClouds::Temporal");
	context->Dispatch((rt.lowWidth + 7u) / 8u, (rt.lowHeight + 7u) / 8u, 1u);
	globals::profiler->EndPass();
	rt.frameCloudsAvailable = true;
	context->CSSetShaderResources(0, 4, nullViews4);
	context->CSSetUnorderedAccessViews(0, 2, nullOutputs2, nullptr);
	rt.historyFlip = resolvedIndex != 0u;
	if (rt.temporalHistoryId != 0)
		PIXL::Renderer::TemporalContext::Get().SetHistoryValid(rt.temporalHistoryId, true);

	ID3D11ShaderResourceView* compositeViews[4]{ rt.historyCloud[resolvedIndex]->srv.get(),
		rt.historyDepth[resolvedIndex]->srv.get(), depthSRV, rt.weatherMap[currentWeatherIndex]->srv.get() };
	ID3D11UnorderedAccessView* sceneOutput = REX::W32::AsReal(main.UAV);
	const auto fogFrame = PIXL::Renderer::VolumetricContext::Get().Acquire();
	ID3D11ShaderResourceView* integratedFog = fogFrame.valid ? fogFrame.integratedScattering.get() : nullptr;
	context->CSSetShaderResources(19, 1, &integratedFog);
	context->CSSetShaderResources(0, 4, compositeViews);
	context->CSSetUnorderedAccessViews(0, 1, &sceneOutput, nullptr);
	context->CSSetShader(rt.compositeShader.get(), nullptr, 0);
	globals::profiler->BeginPass("VolumetricClouds::Composite");
	context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
	globals::profiler->EndPass();
	context->CSSetShaderResources(0, 4, nullViews4);
	ID3D11ShaderResourceView* nullFog = nullptr;
	context->CSSetShaderResources(19, 1, &nullFog);
	ID3D11UnorderedAccessView* nullScene = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &nullScene, nullptr);
	ID3D11Buffer* nullBuffers[2]{};
	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetConstantBuffers(5, 1, nullBuffers);
	context->CSSetConstantBuffers(12, 2, nullBuffers);
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetShader(nullptr, nullptr, 0);
}

ID3D11ShaderResourceView* VolumetricClouds::GetDofCloudMaskSRV() const noexcept
{
	if (!loaded || !settings.Enabled || !runtime || !runtime->frameCloudsAvailable ||
		!runtime->historyCloud[0] || !runtime->historyCloud[1])
		return nullptr;
	const auto index = runtime->historyFlip ? 1u : 0u;
	return runtime->historyCloud[index]->srv.get();
}

ID3D11ShaderResourceView* VolumetricClouds::GetReflectionCloudSRV() const noexcept
{
	return GetDofCloudMaskSRV();
}

ID3D11ShaderResourceView* VolumetricClouds::GetDofCloudDepthSRV() const noexcept
{
	if (!loaded || !settings.Enabled || !runtime || !runtime->frameCloudsAvailable ||
		!runtime->historyDepth[0] || !runtime->historyDepth[1])
		return nullptr;
	const auto index = runtime->historyFlip ? 1u : 0u;
	return runtime->historyDepth[index]->srv.get();
}

void VolumetricClouds::DrawSettings()
{
	bool changed = ImGui::Checkbox("Enable Volumetric Clouds", &settings.Enabled);
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Renders three-dimensional cloud cover from Skyrim's active weather and cloud-layer opacity. Weather changes blend smoothly. Vanilla cloud visibility controls how much of Skyrim's 2D cloud layer remains behind the volume.");
	const auto& activeWeather = WeatherManager::GetSingleton()->GetContext();
	const WeatherProfile weatherTarget = BuildWeatherProfile(activeWeather);
	const float currentWeatherCoverage = runtime && runtime->weatherProfileValid
		? runtime->weatherProfile.coverage : weatherTarget.coverage;
	ImGui::TextDisabled("Weather: %s | transition %.0f%% | coverage %.2f -> %.2f | density %.2f | top %.0f | wind %.2f",
		WeatherClassName(activeWeather.weatherClass), activeWeather.transition.lerpFactor * 100.0f,
		currentWeatherCoverage * settings.Coverage, weatherTarget.coverage * settings.Coverage,
		weatherTarget.density * settings.Density, weatherTarget.cloudTop, weatherTarget.windScale);
	ImGui::BeginDisabled(!settings.Enabled);
	static constexpr const char* qualityNames[]{ "Performance", "Balanced", "High", "Ultra" };
	int quality = static_cast<int>(std::min(settings.Quality, 3u));
	if (ImGui::Combo("Cloud Quality", &quality, qualityNames, static_cast<int>(std::size(qualityNames)))) {
		settings.Quality = static_cast<std::uint32_t>(quality);
		changed = true;
	}
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Controls ray steps and internal resolution. Balanced uses quarter resolution with temporal reconstruction; High and Ultra use half resolution.");
	auto slider = [&changed](const char* label, float& value, float minimum, float maximum,
							 const char* format, const char* description) {
		changed |= ImGui::SliderFloat(label, &value, minimum, maximum, format);
		if (auto tip = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", description);
	};
	slider("Coverage", settings.Coverage, 0.0f, 2.0f, "%.2f",
		"Scales the Cloudy Day 1 baseline after automatic clear, cloudy, rain, storm, snow and special-weather presets. Skyrim's authored cloud layers remain part of the response, including modded weather, and transitions blend smoothly. Clear weather opens broad gaps; storms build fuller, taller and faster cloud banks.");
	slider("Density", settings.Density, 0.1f, 2.5f, "%.2f",
		"Changes cloud opacity and interior darkness. Higher density can hide detail behind the first cloud layer but does not change the sample count.");
	slider("Vanilla Cloud Visibility", settings.VanillaCloudVisibility, 0.0f, 1.0f, "%.2f",
		"Blends Skyrim's authored 2D cloud layers behind the volume. Thin lines in clear gaps can be wisps from this retained layer; set to zero to confirm and remove them. Higher values retain more vanilla cirrus and overlap.");
	if (ImGui::CollapsingHeader("Original Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
		slider("Sky Exposure", settings.OriginalSkyExposure, -3.0f, 2.0f, "%.2f EV",
			"Adjusts the original Skyrim sky behind PIXL's volumetric clouds, including the moon and stars. Lower exposure darkens a washed-out night sky; it does not change cloud brightness, terrain, or fog.");
		slider("Sky Contrast", settings.OriginalSkyContrast, 0.5f, 2.0f, "%.2f",
			"Adjusts contrast in the original sky only. Higher values deepen dark sky tones and separate stars and cloud gaps while preserving sky hue and HDR headroom.");
	}
	if (ImGui::CollapsingHeader("Weather Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
		slider("Aurora Intensity", settings.AuroraIntensity, 0.0f, 1.5f, "%.2f",
			"Adds slow, procedurally layered aurora at night. It can layer with Skyrim-authored aurora, while volumetric clouds naturally veil both effects. Rain and snow fade it down as visibility falls.");
		slider("Lightning Intensity", settings.LightningIntensity, 0.0f, 2.0f, "%.2f",
			"Adds fine, bright branching bolts that reach sampled terrain, illuminate nearby clouds, and leave a brief afterglow during rainy storm weather. Stronger storms strike more often. Zero disables PIXL lightning. Paused time freezes strikes.");
	}
	slider("Cloud Base", settings.CloudBase, 0.0f, 120000.0f, "%.0f units",
		"Sets the bottom altitude of the cloud layer. Weather raises the base slightly in storms while preserving this low baseline so clouds can still cross mountain ridges.");
	slider("Cloud Thickness", settings.CloudThickness, 2000.0f, 90000.0f, "%.0f units",
		"Scales vertical development around weather-driven cloud tops: clear skies target about 45,000 units and severe storms up to 65,000. Thicker layers deepen the banks.");
	slider("View Distance", settings.MaximumDistance, 50000.0f, 800000.0f, "%.0f units",
		"Sets the far limit for cloud rendering. Longer distances extend formations toward the horizon and can increase the number of marched samples.");
	slider("Shape Scale", settings.ShapeScale, 0.2f, 3.0f, "%.2f",
		"Changes the scale of large cloud banks and billows. Lower values make broader formations; higher values make smaller structures.");
	slider("Edge Detail", settings.DetailScale, 0.2f, 3.0f, "%.2f",
		"Changes the scale of fine cloud breakup. Higher values add smaller wisps and cavities at the same raymarch sample count.");
	slider("Erosion", settings.Erosion, 0.0f, 1.5f, "%.2f",
		"Carves holes and softens cloud edges. Higher values produce thinner, more ragged formations; very high values can make clouds look noisy.");
	slider("Turbulence", settings.Turbulence, 0.0f, 1.5f, "%.2f",
		"Adds distortion to cloud shapes and local weather variation. Higher values create less uniform fronts without adding a render pass.");
	slider("Cloud Movement", settings.WindSpeed, 0.0f, 4.0f, "%.2f",
		"Scales weather-driven motion. PIXL transitions from a slow 0.20 clear-weather factor to about 0.58 in severe storms; zero stops wind advection while weather still reshapes clouds.");
	if (ImGui::CollapsingHeader("High Cloud Layer")) {
		changed |= ImGui::Checkbox("Enable High Cloud Layer", &settings.EnableHighCloudLayer);
		if (auto tip = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Adds a separate, thinner high-altitude cloud deck. It uses weather-specific coverage and height, a softer shape, and its own wind path. Disabled by default; enabling it uses the existing cloud pass and does not create shader permutations.");
		ImGui::BeginDisabled(!settings.EnableHighCloudLayer);
		slider("High Cloud Base", settings.HighCloudBase, 20000.0f, 70000.0f, "%.0f units",
			"Sets the high deck's lower altitude. The default begins at 30,000 units.");
		slider("High Cloud Top", settings.HighCloudTop, 65000.0f, 110000.0f, "%.0f units",
			"Sets the maximum high deck altitude; each weather profile can lower it to retain a distinct sky. Typical tops are 82,000–100,000 units.");
		slider("High Cloud Coverage", settings.HighCloudCoverage, 0.0f, 1.0f, "%.2f",
			"Sets how much of the upper sky is occupied. Weather transitions modulate it independently from the lower cloud body.");
		slider("High Cloud Density", settings.HighCloudDensity, 0.0f, 2.0f, "%.2f",
			"Controls opacity of the high deck separately from lower cloud density.");
		slider("High Cloud Detail", settings.HighCloudDetail, 0.1f, 2.0f, "%.2f",
			"Controls fine breakup on the high deck. Lower values create broader, smoother cirrus-like forms.");
		slider("Wind Direction Offset", settings.HighCloudWindDirection, -180.0f, 180.0f, "%.0f deg",
			"Rotates the upper layer's wind relative to Skyrim's current wind. Its accumulated path stays independent through weather direction changes.");
		slider("Wind Speed", settings.HighCloudWindSpeed, 0.0f, 2.0f, "%.2f",
			"Scales upper-layer movement separately from the lower cloud body.");
		ImGui::EndDisabled();
	}
	if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
		slider("Light Extinction", settings.Extinction, 0.1f, 3.0f, "%.2f",
			"Controls how quickly light is absorbed inside clouds. Higher values deepen interiors and shorten visible light penetration.");
		slider("Scattering", settings.Scattering, 0.0f, 3.0f, "%.2f",
			"Scales sunlight and ambient light scattered toward the camera. Higher values brighten cloud volumes but may flatten their contrast.");
		slider("Ambient Light", settings.AmbientLight, 0.0f, 3.0f, "%.2f",
			"Scales sky and ambient illumination inside clouds. Lower values preserve dark bases; higher values brighten shadowed regions.");
		slider("Silver Lining", settings.SilverLining, 0.0f, 2.0f, "%.2f",
			"Adds a restrained forward-scattering highlight around sunlit cloud edges. Higher values make the rim more pronounced.");
		slider("Powder Effect", settings.Powder, 0.0f, 2.0f, "%.2f",
			"Softens light through thin cloud edges and small cavities. Higher values brighten fine structure and can reduce edge contrast.");
		slider("Multiple Scattering", settings.MultipleScattering, 0.0f, 1.5f, "%.2f",
			"Approximates light bouncing between cloud particles. Higher values brighten dense interiors with a small fixed lighting cost.");
		slider("Cloud Shadows", settings.ShadowStrength, 0.0f, 1.0f, "%.2f",
			"Projects the local cloud shape along sunlight onto nearby terrain. Higher values deepen formation-shaped shadows; shadows fade when the sun is low or cloud coverage is sparse.");
	}
	if (globals::state && globals::state->IsDeveloperMode() && ImGui::CollapsingHeader("Advanced / Debug")) {
		slider("Forward Anisotropy", settings.ForwardScattering, 0.0f, 0.92f, "%.2f",
			"Controls the forward phase lobe. Higher values concentrate scattering around the sun direction and emphasize bright edges.");
		slider("Backward Anisotropy", settings.BackScattering, -0.8f, 0.0f, "%.2f",
			"Controls the weaker backward phase lobe. More negative values brighten cloud faces turned away from the sun.");
		slider("Temporal Stability", settings.TemporalResponse, 0.0f, 0.98f, "%.2f",
			"Sets how strongly reconstructed cloud history is retained. Higher values reduce noise but can trail during rapid motion.");
		static constexpr const char* debugNames[]{ "Off", "Cloud Lighting", "Cloud Opacity" };
		int debug = static_cast<int>(std::min(settings.DebugMode, 2u));
		if (ImGui::Combo("Debug View", &debug, debugNames, static_cast<int>(std::size(debugNames)))) {
			settings.DebugMode = static_cast<std::uint32_t>(debug);
			changed = true;
		}
		if (runtime)
			ImGui::TextDisabled("Cloud buffer: %ux%u | weather field: %ux%u | history: %s",
				runtime->lowWidth, runtime->lowHeight, kWeatherMapSize, kWeatherMapSize,
				runtime->temporalHistoryId && PIXL::Renderer::TemporalContext::Get().IsHistoryValid(runtime->temporalHistoryId) ? "valid" : "warming");
	}
	ImGui::EndDisabled();
	if (changed) {
		InvalidateHistory();
		if (runtime)
			runtime->weatherAccumulator = kWeatherUpdateInterval;
		PIXL::Renderer::TemporalContext::Get().Invalidate(
			PIXL::Renderer::TemporalInvalidationReason::SettingsChange, "PIXL Volumetric Clouds settings changed");
	}
}

void VolumetricClouds::LoadSettings(json& value)
{
	if (!value.is_object()) {
		settings = {};
		InvalidateHistory();
		return;
	}
	// Preserve historical defaults, JSON keys, and finite clamping for older profiles.
	const auto readFloat = [&value](const char* key, float fallback, float minimum, float maximum) {
		return FiniteClamp(value.value(key, fallback), fallback, minimum, maximum);
	};
	settings.Enabled = value.value("Enabled", true);
	settings.Quality = std::min(value.value("Quality", 2u), 3u);
	settings.Coverage = readFloat("Coverage", 0.60f, 0.0f, 2.0f);
	settings.Density = readFloat("Density", 1.55f, 0.1f, 2.5f);
	settings.VanillaCloudVisibility = readFloat("VanillaCloudVisibility", 0.25f, 0.0f, 1.0f);
	settings.OriginalSkyExposure = readFloat("OriginalSkyExposure", 0.0f, -3.0f, 2.0f);
	settings.OriginalSkyContrast = readFloat("OriginalSkyContrast", 1.0f, 0.5f, 2.0f);
	settings.AuroraIntensity = readFloat("AuroraIntensity", 0.35f, 0.0f, 1.5f);
	settings.LightningIntensity = readFloat("LightningIntensity", 1.0f, 0.0f, 2.0f);
	settings.CloudBase = readFloat("CloudBase", 15000.0f, 0.0f, 120000.0f);
	settings.CloudThickness = readFloat("CloudThickness", 50085.0f, 2000.0f, 90000.0f);
	settings.EnableHighCloudLayer = value.value("EnableHighCloudLayer", false);
	settings.HighCloudBase = readFloat("HighCloudBase", 30000.0f, 20000.0f, 70000.0f);
	settings.HighCloudTop = readFloat("HighCloudTop", 100000.0f, 65000.0f, 110000.0f);
	settings.HighCloudCoverage = readFloat("HighCloudCoverage", 0.42f, 0.0f, 1.0f);
	settings.HighCloudDensity = readFloat("HighCloudDensity", 0.80f, 0.0f, 2.0f);
	settings.HighCloudDetail = readFloat("HighCloudDetail", 0.65f, 0.1f, 2.0f);
	settings.HighCloudWindDirection = readFloat("HighCloudWindDirection", 28.0f, -180.0f, 180.0f);
	settings.HighCloudWindSpeed = readFloat("HighCloudWindSpeed", 0.72f, 0.0f, 2.0f);
	settings.MaximumDistance = readFloat("MaximumDistance", 466346.0f, 50000.0f, 800000.0f);
	settings.ShapeScale = readFloat("ShapeScale", 1.08f, 0.2f, 3.0f);
	settings.DetailScale = readFloat("DetailScale", 1.5f, 0.2f, 3.0f);
	settings.Erosion = readFloat("Erosion", 0.62f, 0.0f, 1.5f);
	settings.Turbulence = readFloat("Turbulence", 0.44f, 0.0f, 1.5f);
	settings.WindSpeed = readFloat("WindSpeed", 0.41f, 0.0f, 4.0f);
	settings.Extinction = readFloat("Extinction", 1.17f, 0.1f, 3.0f);
	settings.Scattering = readFloat("Scattering", 0.8f, 0.0f, 3.0f);
	settings.AmbientLight = readFloat("AmbientLight", 0.68f, 0.0f, 3.0f);
	settings.ForwardScattering = readFloat("ForwardScattering", 0.72f, 0.0f, 0.92f);
	settings.BackScattering = readFloat("BackScattering", -0.18f, -0.8f, 0.0f);
	settings.SilverLining = readFloat("SilverLining", 0.85f, 0.0f, 2.0f);
	settings.Powder = readFloat("Powder", 0.55f, 0.0f, 2.0f);
	settings.MultipleScattering = readFloat("MultipleScattering", 0.65f, 0.0f, 1.5f);
	settings.ShadowStrength = readFloat("ShadowStrength", 0.20f, 0.0f, 1.0f);
	settings.TemporalResponse = readFloat("TemporalResponse", 0.92f, 0.0f, 0.98f);
	settings.DebugMode = 0u;
}

void VolumetricClouds::SaveSettings(json& value)
{
	value = {
		{ "Enabled", settings.Enabled }, { "Quality", settings.Quality }, { "Coverage", settings.Coverage },
		{ "VanillaCloudVisibility", settings.VanillaCloudVisibility },
		{ "OriginalSkyExposure", settings.OriginalSkyExposure },
		{ "OriginalSkyContrast", settings.OriginalSkyContrast },
		{ "AuroraIntensity", settings.AuroraIntensity },
		{ "LightningIntensity", settings.LightningIntensity },
		{ "Density", settings.Density }, { "CloudBase", settings.CloudBase }, { "CloudThickness", settings.CloudThickness },
		{ "EnableHighCloudLayer", settings.EnableHighCloudLayer }, { "HighCloudBase", settings.HighCloudBase },
		{ "HighCloudTop", settings.HighCloudTop }, { "HighCloudCoverage", settings.HighCloudCoverage },
		{ "HighCloudDensity", settings.HighCloudDensity },
		{ "HighCloudDetail", settings.HighCloudDetail }, { "HighCloudWindDirection", settings.HighCloudWindDirection },
		{ "HighCloudWindSpeed", settings.HighCloudWindSpeed },
		{ "MaximumDistance", settings.MaximumDistance }, { "ShapeScale", settings.ShapeScale },
		{ "DetailScale", settings.DetailScale }, { "Erosion", settings.Erosion }, { "Turbulence", settings.Turbulence },
		{ "WindSpeed", settings.WindSpeed }, { "Extinction", settings.Extinction }, { "Scattering", settings.Scattering },
		{ "AmbientLight", settings.AmbientLight }, { "ForwardScattering", settings.ForwardScattering },
		{ "BackScattering", settings.BackScattering }, { "SilverLining", settings.SilverLining },
		{ "Powder", settings.Powder }, { "MultipleScattering", settings.MultipleScattering },
		{ "ShadowStrength", settings.ShadowStrength }, { "TemporalResponse", settings.TemporalResponse }
	};
}

void VolumetricClouds::RestoreDefaultSettings()
{
	settings = {};
}

void VolumetricClouds::RegisterWeatherVariables()
{
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetOrCreateFeatureRegistry(GetShortName());
	const auto addFloat = [registry](const char* key, const char* label, const char* tooltip,
		float& setting, float fallback, float minimum, float maximum) {
		registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
			key, label, tooltip, &setting, fallback, minimum, maximum));
	};
	addFloat("Coverage", "Cloud Coverage", "Per-weather multiplier applied after automatic weather classification.",
		settings.Coverage, 0.60f, 0.0f, 2.0f);
	addFloat("Density", "Cloud Density", "Per-weather optical-density multiplier.",
		settings.Density, 1.55f, 0.1f, 2.5f);
	addFloat("CloudBase", "Cloud Base", "Base altitude for this weather in Skyrim world units.",
		settings.CloudBase, 15000.0f, 0.0f, 120000.0f);
	addFloat("CloudThickness", "Cloud Thickness", "Scales weather-driven vertical development around the configured cloud base.",
		settings.CloudThickness, 50085.0f, 2000.0f, 90000.0f);
	addFloat("Erosion", "Cloud Erosion", "Fine breakup and wispy-edge strength for this weather.",
		settings.Erosion, 0.62f, 0.0f, 1.5f);
	addFloat("ShadowStrength", "Cloud Shadow Strength", "Strength of broad world-space cloud shadows.",
		settings.ShadowStrength, 0.20f, 0.0f, 1.0f);
}
