// PIXL Renderer - world-space volumetric cloud prototype.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VolumetricClouds.h"

#include "Deferred.h"
#include "Globals.h"
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
#include <cmath>

namespace
{
	constexpr std::uint32_t kWeatherMapSize = 512u;
	constexpr float kWeatherUpdateInterval = 0.20f;
	constexpr float kWeatherExtent = 1048576.0f;
	constexpr float kCloudCurvatureRadius = 445900000.0f;

	float FiniteClamp(float value, float fallback, float minimum, float maximum)
	{
		return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
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
		float thicknessScale{ 1.0f };
	};

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
		float dayBegin = sunriseEnd;
		while (dayBegin < sunriseBegin)
			dayBegin += 24.0f;
		float dayEnd = sunsetBegin;
		while (dayEnd < dayBegin)
			dayEnd += 24.0f;
		float nightBegin = sunsetEnd;
		while (nightBegin < dayEnd)
			nightBegin += 24.0f;
		float nextSunrise = sunriseBegin;
		while (nextSunrise < nightBegin)
			nextSunrise += 24.0f;
		const std::array<float, 4> anchors{
			(sunriseBegin + sunriseEnd) * 0.5f,
			(dayBegin + dayEnd) * 0.5f,
			(sunsetBegin + sunsetEnd) * 0.5f,
			(nightBegin + nextSunrise) * 0.5f
		};
		float sampleHour = std::isfinite(hour) ? hour : 12.0f;
		sampleHour = std::fmod(sampleHour, 24.0f);
		if (sampleHour < 0.0f)
			sampleHour += 24.0f;
		if (sampleHour < anchors[0])
			sampleHour += 24.0f;
		std::array<float, 4> timeAnchors = anchors;
		for (std::size_t index = 1; index < timeAnchors.size(); ++index)
			while (timeAnchors[index] < timeAnchors[index - 1])
				timeAnchors[index] += 24.0f;
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
		const float fromCloudsProfile = hasFromCloudLayers ? fromClouds : context.cloudiness;
		const float toCloudsProfile = hasToCloudLayers ? toClouds : context.cloudiness;
		const float clouds = std::clamp(std::lerp(fromCloudsProfile, toCloudsProfile, transition), 0.0f, 1.0f);
		const float precipitation = std::clamp(context.precipitation, 0.0f, 1.0f);
		const float storm = std::clamp(context.storminess, 0.0f, 1.0f);
		const float fog = std::clamp(context.fogIntensity, 0.0f, 1.0f);
		WeatherProfile profile{};
		// Preserve the tuned cloudy-day look while allowing authored clear records
		// and the semantic clear fallback (0.14) to reach exactly zero.
		const float clearT = std::clamp((clouds - 0.14f) / 0.20f, 0.0f, 1.0f);
		const float clearGate = clearT * clearT * (3.0f - 2.0f * clearT);
		profile.coverage = std::clamp((0.06f + clouds * 0.88f + precipitation * 0.12f) *
			std::max(clearGate, std::max(precipitation, storm)), 0.0f, 1.0f);
		profile.density = std::clamp(0.48f + clouds * 0.62f + precipitation * 0.34f + storm * 0.30f, 0.35f, 1.65f);
		profile.type = std::clamp(clouds * 0.58f + precipitation * 0.24f + storm * 0.34f, 0.0f, 1.0f);
		profile.precipitation = precipitation;
		profile.storm = storm;
		profile.fog = fog;
		profile.sunVisibility = std::clamp(context.sunVisibility, 0.0f, 1.0f);
		profile.baseOffset = -(precipitation * 4500.0f + storm * 7000.0f + fog * 1800.0f);
		profile.thicknessScale = 0.72f + clouds * 0.36f + storm * 0.54f;

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
		auto approach = [deltaTime](float current, float next, float seconds) {
			const float weight = 1.0f - std::exp(-std::clamp(deltaTime, 0.0f, 0.1f) / seconds);
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
		state.thicknessScale = approach(state.thicknessScale, target.thicknessScale, 30.0f);
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
		double time{};
	float weatherAccumulator{ kWeatherUpdateInterval };
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
		  "Persistent weather fronts, temporal reprojection, adaptive raymarching and cloud shadows" } };
}

bool VolumetricClouds::CompileShaders()
{
	if (!runtime)
		return false;
	constexpr wchar_t shaderPath[] = L"Data\\Shaders\\VolumetricClouds\\VolumetricCloudsCS.hlsl";
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
		sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
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
	static constexpr std::array<std::uint32_t, 4> downsample{ 4u, 4u, 2u, 2u };
	const std::uint32_t quality = std::min(settings.Quality, 3u);
	const std::uint32_t divisor = downsample[quality];
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
	main.texture->GetDesc(&mainDesc);
	const auto dynamicSize = Util::ConvertToDynamic(
		float2{ static_cast<float>(mainDesc.Width), static_cast<float>(mainDesc.Height) }, true);
	const std::uint32_t width = std::clamp(static_cast<std::uint32_t>(std::max(dynamicSize.x, 1.0f)), 1u, mainDesc.Width);
	const std::uint32_t height = std::clamp(static_cast<std::uint32_t>(std::max(dynamicSize.y, 1.0f)), 1u, mainDesc.Height);
	if (!EnsureFrameResources(width, height))
		return;

	const float deltaTime = globals::game::deltaTime && std::isfinite(*globals::game::deltaTime)
		? std::clamp(*globals::game::deltaTime, 0.0f, 0.10f)
		: 1.0f / 60.0f;
	// Keep cloud advection monotonic. Wrapping this clock made the density field
	// jump back to its old coordinates every 4.55 hours of rendered game time.
	runtime->time += static_cast<double>(deltaTime);
	runtime->weatherAccumulator = std::min(runtime->weatherAccumulator + deltaTime, 1.0f);
	const auto temporalSnapshot = PIXL::Renderer::TemporalContext::Get().GetFrameSnapshot();
	const float cameraX = static_cast<float>(temporalSnapshot.current.absoluteCameraPosition[0]);
	const float cameraY = static_cast<float>(temporalSnapshot.current.absoluteCameraPosition[1]);
	const float texelSize = kWeatherExtent / static_cast<float>(kWeatherMapSize);
	const float2 newOrigin{
		std::floor(cameraX / texelSize) * texelSize,
		std::floor(cameraY / texelSize) * texelSize
	};
	// Weather synthesis is a full 512x512 dispatch. Recenter requests are folded
	// into the existing cadence instead of triggering a second dispatch whenever
	// the camera crosses a snapped map texel.
	const bool weatherUpdate = !runtime->weatherValid || runtime->weatherAccumulator >= kWeatherUpdateInterval;
	if (weatherUpdate) {
		runtime->previousWeatherOrigin = runtime->weatherValid ? runtime->weatherOrigin : newOrigin;
		runtime->weatherOrigin = newOrigin;
	}

	const WeatherProfile targetProfile = BuildWeatherProfile(weatherContext);
	const WeatherProfile profile = SmoothWeatherProfile(targetProfile, runtime->weatherProfile,
		runtime->weatherProfileValid, deltaTime);
	const std::uint32_t quality = std::min(settings.Quality, 3u);
	static constexpr std::array<std::uint32_t, 4> primarySteps{ 32u, 48u, 80u, 96u };
	static constexpr std::array<std::uint32_t, 4> lightSteps{ 3u, 4u, 5u, 6u };
	float windAngle = 0.75f;
	if (globals::game::sky && std::isfinite(globals::game::sky->windAngle))
		windAngle = globals::game::sky->windAngle;
	const float windSpeed = (45.0f + weatherContext.windIntensity * 420.0f) *
		FiniteClamp(settings.WindSpeed, 0.28f, 0.0f, 4.0f);
	const float cloudBase = FiniteClamp(settings.CloudBase, 3500.0f, 0.0f, 120000.0f) + profile.baseOffset;
	const float thickness = FiniteClamp(settings.CloudThickness, 40893.0f, 2000.0f, 90000.0f) * profile.thicknessScale;
	CloudConstants constants{};
	constants.renderSize = { static_cast<float>(runtime->lowWidth), static_cast<float>(runtime->lowHeight),
		1.0f / runtime->lowWidth, 1.0f / runtime->lowHeight };
	constants.fullSize = { static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height };
	constants.layer = { cloudBase, cloudBase + thickness,
		FiniteClamp(settings.MaximumDistance, 320000.0f, 50000.0f, 800000.0f), kCloudCurvatureRadius };
	constants.weather = { std::clamp(profile.coverage * FiniteClamp(settings.Coverage, 0.76f, 0.0f, 2.0f), 0.0f, 1.0f),
		profile.density * FiniteClamp(settings.Density, 1.55f, 0.1f, 2.5f), profile.type, profile.precipitation };
	constants.weatherSecondary = { profile.storm, profile.fog,
		std::clamp(weatherContext.transition.lerpFactor, 0.0f, 1.0f), profile.sunVisibility };
	constants.windTime = { std::cos(windAngle) * windSpeed, std::sin(windAngle) * windSpeed,
		static_cast<float>(runtime->time), weatherUpdate ? runtime->weatherAccumulator : deltaTime };
	constants.cloudMotion = { constants.windTime.x, constants.windTime.y, deltaTime, 0.0f };
	constants.weatherMap = { runtime->weatherOrigin.x, runtime->weatherOrigin.y, kWeatherExtent, 1.0f / kWeatherExtent };
	constants.previousWeatherMap = { runtime->previousWeatherOrigin.x, runtime->previousWeatherOrigin.y,
		kWeatherExtent, runtime->weatherValid ? 1.0f : 0.0f };
	constants.shape = { FiniteClamp(settings.ShapeScale, 1.26f, 0.2f, 3.0f),
		FiniteClamp(settings.DetailScale, 1.25f, 0.2f, 3.0f), FiniteClamp(settings.Erosion, 0.62f, 0.0f, 1.5f),
		FiniteClamp(settings.Turbulence, 0.44f, 0.0f, 1.5f) };
	constants.lighting = { FiniteClamp(settings.Extinction, 1.01f, 0.1f, 3.0f),
		FiniteClamp(settings.Scattering, 0.8f, 0.0f, 3.0f), FiniteClamp(settings.AmbientLight, 0.83f, 0.0f, 3.0f),
		FiniteClamp(settings.Powder, 0.55f, 0.0f, 2.0f) };
	constants.phase = { FiniteClamp(settings.ForwardScattering, 0.72f, 0.0f, 0.92f),
		FiniteClamp(settings.BackScattering, -0.18f, -0.8f, 0.0f), FiniteClamp(settings.SilverLining, 0.85f, 0.0f, 2.0f),
		FiniteClamp(settings.MultipleScattering, 0.65f, 0.0f, 1.5f) };
	const bool temporalValid = runtime->temporalHistoryId != 0 &&
		PIXL::Renderer::TemporalContext::Get().IsHistoryValid(runtime->temporalHistoryId) &&
		temporalSnapshot.previousFrameValid;
	constants.temporal = { FiniteClamp(settings.TemporalResponse, 0.92f, 0.0f, 0.98f), temporalValid ? 1.0f : 0.0f,
		static_cast<float>(temporalSnapshot.current.frameIndex & 1023u), 0.12f };
	constants.composite = { FiniteClamp(settings.ShadowStrength, 0.52f, 0.0f, 1.0f), 180000.0f, 1.0f,
		static_cast<float>(std::min(settings.DebugMode, 2u)) };
	constants.primarySteps = primarySteps[quality];
	constants.lightSteps = lightSteps[quality];
	constants.weatherHistoryValid = runtime->weatherValid ? 1u : 0u;
	constants.updateWeather = weatherUpdate ? 1u : 0u;
	try {
		runtime->constants->Update(constants);
	} catch (const std::exception& error) {
		if (!runtime->resourceFailureLogged) {
			logger::error("[PIXL Volumetric Clouds] constant upload failed: {}", error.what());
			runtime->resourceFailureLogged = true;
		}
		return;
	}

	auto* context = globals::d3d::context;
	ID3D11Buffer* sharedBuffer = globals::state->sharedDataCB->CB();
	ID3D11Buffer* frameBuffer = *globals::game::perFrame;
	ID3D11Buffer* cloudBuffer = runtime->constants->CB();
	context->CSSetConstantBuffers(5, 1, &sharedBuffer);
	context->CSSetConstantBuffers(12, 1, &frameBuffer);
	context->CSSetConstantBuffers(13, 1, &cloudBuffer);
	ID3D11SamplerState* sampler = runtime->linearClamp.get();
	context->CSSetSamplers(0, 1, &sampler);

	if (weatherUpdate) {
		const std::uint32_t sourceIndex = runtime->weatherFlip ? 1u : 0u;
		const std::uint32_t destinationIndex = 1u - sourceIndex;
		ID3D11ShaderResourceView* weatherInput = runtime->weatherMap[sourceIndex]->srv.get();
		ID3D11UnorderedAccessView* weatherOutput = runtime->weatherMap[destinationIndex]->uav.get();
		context->CSSetShaderResources(0, 1, &weatherInput);
		context->CSSetUnorderedAccessViews(0, 1, &weatherOutput, nullptr);
		context->CSSetShader(runtime->weatherShader.get(), nullptr, 0);
		globals::profiler->BeginPass("VolumetricClouds::Weather");
		context->Dispatch(kWeatherMapSize / 8u, kWeatherMapSize / 8u, 1u);
		globals::profiler->EndPass();
		ID3D11ShaderResourceView* nullSRV = nullptr;
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetShaderResources(0, 1, &nullSRV);
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		runtime->weatherFlip = destinationIndex != 0u;
		runtime->weatherValid = true;
		runtime->weatherAccumulator = 0.0f;
	}
	const std::uint32_t currentWeatherIndex = runtime->weatherFlip ? 1u : 0u;
	ID3D11ShaderResourceView* rayViews[2]{ runtime->weatherMap[currentWeatherIndex]->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* rayOutputs[2]{ runtime->rawCloud->uav.get(), runtime->rawDepth->uav.get() };
	context->CSSetShaderResources(0, 2, rayViews);
	context->CSSetUnorderedAccessViews(0, 2, rayOutputs, nullptr);
	context->CSSetShader(runtime->raymarchShader.get(), nullptr, 0);
	globals::profiler->BeginPass("VolumetricClouds::Raymarch");
	context->Dispatch((runtime->lowWidth + 7u) / 8u, (runtime->lowHeight + 7u) / 8u, 1u);
	globals::profiler->EndPass();
	ID3D11ShaderResourceView* nullViews4[4]{};
	ID3D11UnorderedAccessView* nullOutputs2[2]{};
	context->CSSetShaderResources(0, 2, nullViews4);
	context->CSSetUnorderedAccessViews(0, 2, nullOutputs2, nullptr);

	const std::uint32_t previousIndex = runtime->historyFlip ? 1u : 0u;
	const std::uint32_t resolvedIndex = 1u - previousIndex;
	ID3D11ShaderResourceView* temporalViews[4]{ runtime->rawCloud->srv.get(), runtime->rawDepth->srv.get(),
		runtime->historyCloud[previousIndex]->srv.get(), runtime->historyDepth[previousIndex]->srv.get() };
	ID3D11UnorderedAccessView* temporalOutputs[2]{ runtime->historyCloud[resolvedIndex]->uav.get(),
		runtime->historyDepth[resolvedIndex]->uav.get() };
	context->CSSetShaderResources(0, 4, temporalViews);
	context->CSSetUnorderedAccessViews(0, 2, temporalOutputs, nullptr);
	context->CSSetShader(runtime->temporalShader.get(), nullptr, 0);
	globals::profiler->BeginPass("VolumetricClouds::Temporal");
	context->Dispatch((runtime->lowWidth + 7u) / 8u, (runtime->lowHeight + 7u) / 8u, 1u);
	globals::profiler->EndPass();
	runtime->frameCloudsAvailable = true;
	context->CSSetShaderResources(0, 4, nullViews4);
	context->CSSetUnorderedAccessViews(0, 2, nullOutputs2, nullptr);
	runtime->historyFlip = resolvedIndex != 0u;
	if (runtime->temporalHistoryId != 0)
		PIXL::Renderer::TemporalContext::Get().SetHistoryValid(runtime->temporalHistoryId, true);

	ID3D11ShaderResourceView* compositeViews[4]{ runtime->historyCloud[resolvedIndex]->srv.get(),
		runtime->historyDepth[resolvedIndex]->srv.get(), depthSRV, runtime->weatherMap[currentWeatherIndex]->srv.get() };
	ID3D11UnorderedAccessView* sceneOutput = main.UAV;
	const auto fogFrame = PIXL::Renderer::VolumetricContext::Get().Acquire();
	ID3D11ShaderResourceView* integratedFog = fogFrame.valid ? fogFrame.integratedScattering.get() : nullptr;
	context->CSSetShaderResources(19, 1, &integratedFog);
	context->CSSetShaderResources(0, 4, compositeViews);
	context->CSSetUnorderedAccessViews(0, 1, &sceneOutput, nullptr);
	context->CSSetShader(runtime->compositeShader.get(), nullptr, 0);
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

void VolumetricClouds::DrawSettings()
{
	bool changed = ImGui::Checkbox("Enable Volumetric Clouds", &settings.Enabled);
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Renders three-dimensional cloud cover from Skyrim's active weather and cloud-layer opacity. Weather changes blend smoothly. Vanilla cloud visibility controls how much of Skyrim's 2D cloud layer remains behind the volume.");
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
		"Scales weather-driven cloud coverage. Lower values open larger gaps; higher values fill more of the sky. It does not increase raymarch cost.");
	slider("Density", settings.Density, 0.1f, 2.5f, "%.2f",
		"Changes cloud opacity and interior darkness. Higher density can hide detail behind the first cloud layer but does not change the sample count.");
	slider("Vanilla Cloud Visibility", settings.VanillaCloudVisibility, 0.0f, 1.0f, "%.2f",
		"Blends Skyrim's authored 2D cloud layers behind the volume. Thin lines in clear gaps can be wisps from this retained layer; set to zero to confirm and remove them. Higher values retain more vanilla cirrus and overlap.");
	slider("Cloud Base", settings.CloudBase, 0.0f, 120000.0f, "%.0f units",
		"Sets the bottom altitude of the cloud layer. The 3,500-unit baseline lets banks cross Skyrim's mountain ridges; lower it to engulf more peaks.");
	slider("Cloud Thickness", settings.CloudThickness, 2000.0f, 90000.0f, "%.0f units",
		"Sets vertical cloud development. Thicker layers create deeper banks and longer in-cloud views, with a modest increase in raymarch work.");
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
		"Scales Skyrim weather wind for cloud motion. Zero still allows weather transitions to reshape formations, but stops their wind advection.");
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
			"Darkens nearby terrain beneath broad cloud formations. Higher values strengthen the world-space shadow approximation.");
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
	settings.Enabled = value.value("Enabled", true);
	settings.Quality = std::min(value.value("Quality", 2u), 3u);
	settings.Coverage = FiniteClamp(value.value("Coverage", 0.76f), 0.76f, 0.0f, 2.0f);
	settings.Density = FiniteClamp(value.value("Density", 1.55f), 1.55f, 0.1f, 2.5f);
	settings.VanillaCloudVisibility = FiniteClamp(value.value("VanillaCloudVisibility", 0.15f), 0.15f, 0.0f, 1.0f);
	settings.CloudBase = FiniteClamp(value.value("CloudBase", 18124.0f), 18124.0f, 0.0f, 120000.0f);
	settings.CloudThickness = FiniteClamp(value.value("CloudThickness", 40893.0f), 40893.0f, 2000.0f, 90000.0f);
	settings.MaximumDistance = FiniteClamp(value.value("MaximumDistance", 525100.0f), 525100.0f, 50000.0f, 800000.0f);
	settings.ShapeScale = FiniteClamp(value.value("ShapeScale", 1.08f), 1.08f, 0.2f, 3.0f);
	settings.DetailScale = FiniteClamp(value.value("DetailScale", 1.25f), 1.25f, 0.2f, 3.0f);
	settings.Erosion = FiniteClamp(value.value("Erosion", 0.62f), 0.62f, 0.0f, 1.5f);
	settings.Turbulence = FiniteClamp(value.value("Turbulence", 0.44f), 0.44f, 0.0f, 1.5f);
	settings.WindSpeed = FiniteClamp(value.value("WindSpeed", 0.41f), 0.41f, 0.0f, 4.0f);
	settings.Extinction = FiniteClamp(value.value("Extinction", 1.17f), 1.17f, 0.1f, 3.0f);
	settings.Scattering = FiniteClamp(value.value("Scattering", 0.8f), 0.8f, 0.0f, 3.0f);
	settings.AmbientLight = FiniteClamp(value.value("AmbientLight", 0.68f), 0.68f, 0.0f, 3.0f);
	settings.ForwardScattering = FiniteClamp(value.value("ForwardScattering", 0.72f), 0.72f, 0.0f, 0.92f);
	settings.BackScattering = FiniteClamp(value.value("BackScattering", -0.18f), -0.18f, -0.8f, 0.0f);
	settings.SilverLining = FiniteClamp(value.value("SilverLining", 0.85f), 0.85f, 0.0f, 2.0f);
	settings.Powder = FiniteClamp(value.value("Powder", 0.55f), 0.55f, 0.0f, 2.0f);
	settings.MultipleScattering = FiniteClamp(value.value("MultipleScattering", 0.65f), 0.65f, 0.0f, 1.5f);
	settings.ShadowStrength = FiniteClamp(value.value("ShadowStrength", 0.20f), 0.20f, 0.0f, 1.0f);
	settings.TemporalResponse = FiniteClamp(value.value("TemporalResponse", 0.92f), 0.92f, 0.0f, 0.98f);
	settings.DebugMode = 0u;
}

void VolumetricClouds::SaveSettings(json& value)
{
	value = {
		{ "Enabled", settings.Enabled }, { "Quality", settings.Quality }, { "Coverage", settings.Coverage },
		{ "VanillaCloudVisibility", settings.VanillaCloudVisibility },
		{ "Density", settings.Density }, { "CloudBase", settings.CloudBase }, { "CloudThickness", settings.CloudThickness },
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
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Coverage", "Cloud Coverage", "Per-weather multiplier applied after automatic weather classification.",
		&settings.Coverage, 0.76f, 0.0f, 2.0f));
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Density", "Cloud Density", "Per-weather optical-density multiplier.",
		&settings.Density, 1.55f, 0.1f, 2.5f));
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"CloudBase", "Cloud Base", "Base altitude for this weather in Skyrim world units.",
		&settings.CloudBase, 18124.0f, 0.0f, 120000.0f));
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"CloudThickness", "Cloud Thickness", "Vertical development for this weather in Skyrim world units.",
		&settings.CloudThickness, 40893.0f, 2000.0f, 90000.0f));
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"Erosion", "Cloud Erosion", "Fine breakup and wispy-edge strength for this weather.",
		&settings.Erosion, 0.62f, 0.0f, 1.5f));
	registry->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
		"ShadowStrength", "Cloud Shadow Strength", "Strength of broad world-space cloud shadows.",
		&settings.ShadowStrength, 0.20f, 0.0f, 1.0f));
}

