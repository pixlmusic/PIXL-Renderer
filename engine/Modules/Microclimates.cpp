#include "Microclimates.h"

#include "Globals.h"
#include "Modules/Atmosphere.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include "WeatherManager.h"
#include "Renderer/RenderOrigin.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr std::uint32_t kFieldSize = 384;
	constexpr float kSimulationInterval = 0.2f;
	constexpr UINT kAtmosphereSrvSlot = 101u;
	constexpr UINT kStormSrvSlot = 102u;
	constexpr UINT kAtmosphereCbSlot = 13u;
	constexpr UINT kMicroSamplerSlot = 4u;

	float FiniteClamp(float value, float fallback, float low, float high)
	{
		return std::clamp(std::isfinite(value) ? value : fallback, low, high);
	}

	std::uint64_t GetWorldContext()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player)
			return 0;
		if (auto* world = player->GetWorldspace())
			return world->GetFormID();
		if (auto* cell = player->GetParentCell())
			return cell->GetFormID();
		return 0;
	}
}

#pragma warning(push)
#pragma warning(disable : 4324)  // Runtime contains the explicitly 16-byte-aligned GPU constants ABI.
struct Microclimates::Runtime
{
	std::unique_ptr<Texture2D> fieldA;
	std::unique_ptr<Texture2D> fieldB;
	std::unique_ptr<Texture2D> stormA;
	std::unique_ptr<Texture2D> stormB;
	std::unique_ptr<ConstantBuffer> constants;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	ID3D11ComputeShader* updateShader = nullptr;
	bool flip = false;
	bool historyValid = false;
	bool emitterAnchored = false;
	bool unavailableLogged = false;
	FieldConstants publishedConstants{};
	std::uint32_t lastFrame = UINT32_MAX;
	std::uint32_t ticks = 0;
	std::uint64_t worldContext = 0;
	float accumulator = 0.0f;
	float3 fieldOrigin{};
	float3 previousFieldOrigin{};
	float3 emitterPosition{};
	float3 windDirection{ 1.0f, 0.0f, 0.0f };
};
#pragma warning(pop)

Microclimates::Microclimates() : runtime(std::make_unique<Runtime>()) {}
Microclimates::~Microclimates()
{
	if (runtime && runtime->updateShader)
		runtime->updateShader->Release();
}

std::pair<std::string, std::vector<std::string>> Microclimates::GetModuleSummary()
{
	return { "Adds an opt-in, renderer-owned spatial weather field layered over Skyrim's active weather.",
		{ "World-space low-resolution humidity, fog, precipitation and cloud potential",
		  "Fixed-rate GPU advection with a deterministic local test emitter",
		  "Localized fog integrates only into PIXL's existing volumetric atmosphere" } };
}

void Microclimates::SetupResources()
{
	if (!runtime || !globals::d3d::device || !globals::d3d::context) {
		loaded = false;
		settings.Enable = false;
		return;
	}
	if (runtime->updateShader)
		runtime->updateShader->Release();
	runtime->updateShader = static_cast<ID3D11ComputeShader*>(
		Util::CompileShader(L"Data\\Shaders\\Microclimates\\MicroclimateFieldCS.hlsl", {}, "cs_5_0"));
	if (!runtime->updateShader) {
		settings.Enable = false;
		loaded = false;
		logger::warn("[Microclimates] field shader unavailable; module remains disabled");
		return;
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = kFieldSize;
	desc.Height = kFieldSize;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = desc.Format;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
	uav.Format = desc.Format;
	uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uav.Texture2D.MipSlice = 0;
	try {
		runtime->fieldA = std::make_unique<Texture2D>(desc, "Microclimates::FieldA");
		runtime->fieldB = std::make_unique<Texture2D>(desc, "Microclimates::FieldB");
		runtime->fieldA->CreateSRV(srv);
		runtime->fieldA->CreateUAV(uav);
		runtime->fieldB->CreateSRV(srv);
		runtime->fieldB->CreateUAV(uav);
		runtime->constants = std::make_unique<ConstantBuffer>(ConstantBufferDesc<FieldConstants>(), "Microclimates::FieldConstants");
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&samplerDesc, runtime->sampler.put()));
		D3D11_TEXTURE2D_DESC stormDesc = desc;
		stormDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		D3D11_SHADER_RESOURCE_VIEW_DESC stormSrv = srv;
		stormSrv.Format = stormDesc.Format;
		D3D11_UNORDERED_ACCESS_VIEW_DESC stormUav = uav;
		stormUav.Format = stormDesc.Format;
		runtime->stormA = std::make_unique<Texture2D>(stormDesc, "Microclimates::StormA");
		runtime->stormB = std::make_unique<Texture2D>(stormDesc, "Microclimates::StormB");
		runtime->stormA->CreateSRV(stormSrv);
		runtime->stormA->CreateUAV(stormUav);
		runtime->stormB->CreateSRV(stormSrv);
		runtime->stormB->CreateUAV(stormUav);
	} catch (const std::exception& e) {
		logger::error("[Microclimates] resource creation failed: {}", e.what());
		runtime->fieldA.reset();
		runtime->fieldB.reset();
		runtime->stormA.reset();
		runtime->stormB.reset();
		runtime->constants.reset();
		settings.Enable = false;
		loaded = false;
		return;
	}
	runtime->historyValid = false;
	runtime->unavailableLogged = false;
	logger::info("[Microclimates] initialized: {}x{} RGBA16F world field, fixed-rate simulation {:.1f} Hz",
		kFieldSize, kFieldSize, 1.0f / kSimulationInterval);
}

void Microclimates::Reset()
{
	// RenderModule::Reset is called at the beginning of every frame. Preserve
	// field history while enabled; disabled frames invalidate it for clean resume.
	if (!runtime || settings.Enable)
		return;
	runtime->historyValid = false;
	runtime->accumulator = 0.0f;
	runtime->emitterAnchored = false;
	runtime->lastFrame = UINT32_MAX;
}

void Microclimates::Prepass()
{
	if (settings.Enable)
		UpdateField();
}

void Microclimates::UpdateField()
{
	if (!settings.Enable || !runtime || !runtime->fieldA || !runtime->fieldB || !runtime->stormA || !runtime->stormB || !runtime->constants || !runtime->updateShader ||
		!globals::d3d::context || !globals::state)
		return;
	const auto frame = globals::state->frameCount;
	if (frame == runtime->lastFrame)
		return;
	runtime->lastFrame = frame;
	auto* player = RE::PlayerCharacter::GetSingleton();
	const auto& weather = WeatherManager::GetSingleton()->GetContext();
	const bool exterior = player && weather.exterior && !Util::IsInterior();
	if (!exterior) {
		runtime->historyValid = false;
		runtime->emitterAnchored = false;
		return;
	}
	const auto playerPosition = player->GetPosition();
	const auto contextID = GetWorldContext();
	const bool worldChanged = contextID != runtime->worldContext;
	runtime->worldContext = contextID;
	const float radius = FiniteClamp(settings.fieldRadiusMeters, 30000.0f, 1000.0f, 60000.0f) / Util::Units::GAME_UNIT_TO_M;
	const float extent = radius * 2.0f;
	const float texel = extent / static_cast<float>(kFieldSize);
	const float3 nextOrigin{
		std::floor((playerPosition.x - radius) / texel) * texel,
		std::floor((playerPosition.y - radius) / texel) * texel,
		playerPosition.z
	};
	const bool largeMove = std::hypot(nextOrigin.x - runtime->fieldOrigin.x, nextOrigin.y - runtime->fieldOrigin.y) > extent * 0.45f;
	if (!runtime->historyValid || worldChanged || largeMove) {
		runtime->historyValid = false;
		runtime->fieldOrigin = nextOrigin;
		runtime->previousFieldOrigin = nextOrigin;
		if (worldChanged)
			runtime->emitterAnchored = false;
		logger::info("[Microclimates] worldspace field reset");
	} else {
		runtime->previousFieldOrigin = runtime->fieldOrigin;
		runtime->fieldOrigin = nextOrigin;
	}
	if (settings.debugEmitter && !runtime->emitterAnchored) {
		const float angle = player->GetAngleZ() + FiniteClamp(settings.emitterAngleDegrees, 35.0f, -180.0f, 180.0f) * DirectX::XM_PI / 180.0f;
		const float distance = FiniteClamp(settings.emitterDistanceMeters, 8000.0f, 100.0f, 25000.0f) / Util::Units::GAME_UNIT_TO_M;
		runtime->emitterPosition = { playerPosition.x + std::cos(angle) * distance,
			playerPosition.y + std::sin(angle) * distance, playerPosition.z };
		runtime->emitterAnchored = true;
	} else if (!settings.debugEmitter) {
		runtime->emitterAnchored = false;
	}
	const float dt = globals::game::deltaTime ? std::clamp(*globals::game::deltaTime, 0.0f, 0.1f) : 1.0f / 60.0f;
	runtime->accumulator = std::min(runtime->accumulator + dt, 0.5f);
	if (settings.freezeSimulation && runtime->historyValid)
		return;
	if (runtime->accumulator < kSimulationInterval && runtime->historyValid)
		return;
	const float simDt = runtime->historyValid ? std::min(runtime->accumulator, 0.5f) : kSimulationInterval;
	runtime->accumulator = 0.0f;
	const float windAngle = FiniteClamp(settings.windDirectionDegrees, 42.0f, -180.0f, 180.0f) * DirectX::XM_PI / 180.0f;
	runtime->windDirection = { std::cos(windAngle), std::sin(windAngle), 0.0f };
	const float weatherWindScale = std::lerp(0.25f, 1.0f, std::clamp(weather.windIntensity, 0.0f, 1.0f));
	const float windUnits = FiniteClamp(settings.windSpeed, 7.0f, 0.0f, 40.0f) * weatherWindScale / Util::Units::GAME_UNIT_TO_M;
	FieldConstants constants{};
	constants.originExtent = { runtime->fieldOrigin.x, runtime->fieldOrigin.y, extent, simDt };
	constants.previousOriginWind = { runtime->previousFieldOrigin.x, runtime->previousFieldOrigin.y,
		runtime->windDirection.x * windUnits, runtime->windDirection.y * windUnits };
	constants.baseWeather = { std::clamp(weather.residualHumidity, 0.0f, 1.0f),
		std::clamp(weather.fogIntensity * settings.localizedFog, 0.0f, 1.0f), std::clamp(weather.precipitation * settings.localizedPrecipitation, 0.0f, 1.0f),
		std::clamp(weather.cloudiness, 0.0f, 1.0f) };
	constants.emitterPosition = { runtime->emitterPosition.x, runtime->emitterPosition.y,
		FiniteClamp(settings.emitterRadiusMeters, 4500.0f, 100.0f, 12000.0f) / Util::Units::GAME_UNIT_TO_M,
		settings.debugEmitter ? 1.0f : 0.0f };
	constants.emitterWeather = { FiniteClamp(settings.emitterHumidity, 0.9f, 0.0f, 1.0f),
		FiniteClamp(settings.emitterFog, 0.8f, 0.0f, 1.0f) * settings.localizedFog,
		FiniteClamp(settings.emitterPrecipitation, 0.7f, 0.0f, 1.0f) * settings.localizedPrecipitation,
		FiniteClamp(settings.emitterStorm, 0.4f, 0.0f, 1.0f) };
	// Keep a short atmospheric tail as regional forcing eases out. Weather
	// clearing is handled continuously in the field shader, not as a hard reset.
	const float fieldPersistence = runtime->historyValid ? 0.985f : 0.0f;
	const float fogHeightAnchor = settings.debugEmitter ? runtime->emitterPosition.z : playerPosition.z;
	constants.controls = { 1.0f, static_cast<float>(settings.debugView), fieldPersistence, fogHeightAnchor };
	constants.extendedControls = { FiniteClamp(settings.terrainInfluence, 0.35f, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f };
	runtime->constants->Update(constants);
	runtime->publishedConstants = constants;
	auto* previous = runtime->flip ? runtime->fieldB.get() : runtime->fieldA.get();
	auto* output = runtime->flip ? runtime->fieldA.get() : runtime->fieldB.get();
	auto* previousStorm = runtime->flip ? runtime->stormB.get() : runtime->stormA.get();
	auto* outputStorm = runtime->flip ? runtime->stormA.get() : runtime->stormB.get();
	auto* context = globals::d3d::context;
	ID3D11ShaderResourceView* srvs[2]{ runtime->historyValid ? previous->srv.get() : nullptr,
		runtime->historyValid ? previousStorm->srv.get() : nullptr };
	ID3D11UnorderedAccessView* uavs[2]{ output->uav.get(), outputStorm->uav.get() };
	ID3D11Buffer* cb = runtime->constants->CB();
	ID3D11SamplerState* sampler = runtime->sampler.get();
	ID3D11ShaderResourceView* nullPixelSrvs[2]{};
	context->PSSetShaderResources(kAtmosphereSrvSlot, 2, nullPixelSrvs);
	context->CSSetShaderResources(0, 2, srvs);
	context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(kMicroSamplerSlot, 1, &sampler);
	context->CSSetShader(runtime->updateShader, nullptr, 0);
	context->Dispatch((kFieldSize + 7u) / 8u, (kFieldSize + 7u) / 8u, 1);
	ID3D11ShaderResourceView* nullSrvs[2]{};
	ID3D11UnorderedAccessView* nullUavs[2]{};
	ID3D11Buffer* nullCb = nullptr;
	ID3D11SamplerState* nullSampler = nullptr;
	context->CSSetShaderResources(0, 2, nullSrvs);
	context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
	context->CSSetConstantBuffers(0, 1, &nullCb);
	context->CSSetSamplers(kMicroSamplerSlot, 1, &nullSampler);
	context->CSSetShader(nullptr, nullptr, 0);
	runtime->flip = !runtime->flip;
	runtime->historyValid = true;
	++runtime->ticks;
}

void Microclimates::BindAtmosphereField()
{
	if (!runtime || !runtime->constants || !runtime->historyValid || !settings.Enable ||
		Util::IsInterior() || !globals::d3d::context)
		return;
	// flip identifies the next write target after UpdateField. The most recently
	// completed output is therefore the opposite side of the ping-pong pair.
	ID3D11ShaderResourceView* srv = (runtime->flip ? runtime->fieldB.get() : runtime->fieldA.get())->srv.get();
	ID3D11ShaderResourceView* storm = (runtime->flip ? runtime->stormB.get() : runtime->stormA.get())->srv.get();
	ID3D11Buffer* cb = runtime->constants->CB();
	globals::d3d::context->CSSetShaderResources(kAtmosphereSrvSlot, 1, &srv);
	globals::d3d::context->CSSetShaderResources(kStormSrvSlot, 1, &storm);
	globals::d3d::context->CSSetConstantBuffers(kAtmosphereCbSlot, 1, &cb);
	ID3D11SamplerState* sampler = runtime->sampler.get();
	globals::d3d::context->CSSetSamplers(kMicroSamplerSlot, 1, &sampler);
}

void Microclimates::BindSkyCloudField()
{
	if (!globals::d3d::context)
		return;
	const auto resources = GetReadOnlyFieldResources();
	ID3D11ShaderResourceView* srvs[2]{ resources.weather, resources.storm };
	ID3D11Buffer* cb = resources.constants;
	ID3D11SamplerState* sampler = resources.constants && runtime ? runtime->sampler.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(kAtmosphereSrvSlot, 2, srvs);
	globals::d3d::context->PSSetConstantBuffers(kAtmosphereCbSlot, 1, &cb);
	globals::d3d::context->PSSetSamplers(kMicroSamplerSlot, 1, &sampler);
}

void Microclimates::BindParticleField()
{
	if (!globals::d3d::context)
		return;
	const auto resources = GetReadOnlyFieldResources();
	ID3D11ShaderResourceView* srvs[2]{ resources.weather, resources.storm };
	ID3D11SamplerState* sampler = resources.constants && runtime ? runtime->sampler.get() : nullptr;
	globals::d3d::context->PSSetShaderResources(kAtmosphereSrvSlot, 2, srvs);
	globals::d3d::context->PSSetSamplers(kMicroSamplerSlot, 1, &sampler);
}

void Microclimates::UnbindAtmosphereField()
{
	if (!globals::d3d::context)
		return;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11SamplerState* sampler = nullptr;
	globals::d3d::context->CSSetShaderResources(kAtmosphereSrvSlot, 1, &srv);
	globals::d3d::context->CSSetShaderResources(kStormSrvSlot, 1, &srv);
	globals::d3d::context->CSSetConstantBuffers(kAtmosphereCbSlot, 1, &cb);
	globals::d3d::context->CSSetSamplers(kMicroSamplerSlot, 1, &sampler);
}

bool Microclimates::FieldReady() const { return runtime && runtime->historyValid && runtime->fieldA && runtime->fieldB; }
std::uint32_t Microclimates::SimulationTickCount() const { return runtime ? runtime->ticks : 0; }

Microclimates::ReadOnlyFieldResources Microclimates::GetReadOnlyFieldResources() const
{
	if (!settings.Enable || !FieldReady() || !runtime->constants)
		return {};
	const auto* weather = runtime->flip ? runtime->fieldB.get() : runtime->fieldA.get();
	const auto* storm = runtime->flip ? runtime->stormB.get() : runtime->stormA.get();
	return { weather->srv.get(), storm->srv.get(), runtime->constants->CB(), kFieldSize };
}

Microclimates::FieldConstants Microclimates::GetCurrentFieldConstants() const
{
	return settings.Enable && FieldReady() && runtime ? runtime->publishedConstants : FieldConstants{};
}

void Microclimates::DrawSettings()
{
	bool changed = ImGui::Checkbox("Enable Microclimates", &settings.Enable);
	if (auto tip = Util::HoverTooltipWrapper())
	ImGui::TextWrapped("Adds distant, world-anchored regional clouds and locally sampled fog/precipitation on top of Skyrim weather. It does not change TESWeather, sound, AI or gameplay weather.");
	ImGui::BeginDisabled(!settings.Enable);
	changed |= ImGui::SliderFloat("Regional Weather Reach", &settings.fieldRadiusMeters, 1000.0f, 60000.0f, "%.0f m");
	changed |= ImGui::SliderFloat("Weather Movement", &settings.windSpeed, 0.0f, 40.0f, "%.1f m/s");
	changed |= ImGui::SliderFloat("Localized Fog", &settings.localizedFog, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::SliderFloat("Localized Precipitation", &settings.localizedPrecipitation, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::SliderFloat("Terrain Influence", &settings.terrainInfluence, 0.0f, 1.0f, "%.2f");
	if (auto tip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Terrain height textures are not currently exposed to this low-resolution simulation. This prototype uses emitter elevation as a broad fog-height anchor; true valley pooling remains future work.");
	if (globals::state && globals::state->IsDeveloperMode() && ImGui::CollapsingHeader("Developer / Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= ImGui::Checkbox("Debug Weather Emitter", &settings.debugEmitter);
		changed |= ImGui::SliderFloat("Emitter Distance", &settings.emitterDistanceMeters, 100.0f, 25000.0f, "%.0f m");
		changed |= ImGui::SliderFloat("Emitter Radius", &settings.emitterRadiusMeters, 100.0f, 12000.0f, "%.0f m");
		changed |= ImGui::SliderFloat("Emitter Direction", &settings.emitterAngleDegrees, -180.0f, 180.0f, "%.0f deg");
		changed |= ImGui::SliderFloat("Wind Direction", &settings.windDirectionDegrees, -180.0f, 180.0f, "%.0f deg");
		changed |= ImGui::SliderFloat("Emitter Humidity", &settings.emitterHumidity, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Emitter Fog", &settings.emitterFog, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Emitter Precipitation", &settings.emitterPrecipitation, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Emitter Storm", &settings.emitterStorm, 0.0f, 1.0f);
		changed |= ImGui::Checkbox("Freeze Simulation", &settings.freezeSimulation);
		if (ImGui::Button("Reset Weather Field")) {
			runtime->historyValid = false;
			runtime->accumulator = 0.0f;
			runtime->emitterAnchored = false;
			changed = true;
		}
		static constexpr const char* modes[]{ "Off", "Fog", "Precipitation", "Humidity", "Cloud Potential", "Storm Potential" };
		int mode = static_cast<int>(settings.debugView);
		if (ImGui::Combo("Visualize Field", &mode, modes, static_cast<int>(std::size(modes)))) { settings.debugView = static_cast<std::uint32_t>(std::clamp(mode, 0, 5)); changed = true; }
		ImGui::TextDisabled("Regional field: 384x384 | simulation: 5 Hz | ticks: %u | ready: %s", SimulationTickCount(), FieldReady() ? "yes" : "warming");
		ImGui::TextDisabled("Existing Skyrim cloud layers carry regional high-cloud cover; fog/rain are sampled in world space. Clear weather fades regional forcing.");
	}
	ImGui::EndDisabled();
	if (!settings.Enable)
		Reset();
	if (changed && globals::state)
		globals::state->UpdateFeatureData(globals::state->inWorld);
}

void Microclimates::LoadSettings(json& a_json)
{
	settings.Enable = a_json.value("Enable", false);
	settings.fieldRadiusMeters = FiniteClamp(a_json.value("FieldRadiusMeters", 30000.0f), 30000.0f, 1000.0f, 60000.0f);
	settings.windSpeed = FiniteClamp(a_json.value("WindSpeed", 7.0f), 7.0f, 0.0f, 40.0f);
	settings.windDirectionDegrees = FiniteClamp(a_json.value("WindDirectionDegrees", 42.0f), 42.0f, -180.0f, 180.0f);
	settings.localizedFog = FiniteClamp(a_json.value("LocalizedFog", 0.7f), 0.7f, 0.0f, 1.0f);
	settings.localizedPrecipitation = FiniteClamp(a_json.value("LocalizedPrecipitation", 0.65f), 0.65f, 0.0f, 1.0f);
	settings.terrainInfluence = FiniteClamp(a_json.value("TerrainInfluence", 0.35f), 0.35f, 0.0f, 1.0f);
	settings.debugEmitter = a_json.value("DebugEmitter", false);
	settings.emitterDistanceMeters = FiniteClamp(a_json.value("EmitterDistanceMeters", 8000.0f), 8000.0f, 100.0f, 25000.0f);
	settings.emitterRadiusMeters = FiniteClamp(a_json.value("EmitterRadiusMeters", 4500.0f), 4500.0f, 100.0f, 12000.0f);
	settings.emitterAngleDegrees = FiniteClamp(a_json.value("EmitterAngleDegrees", 35.0f), 35.0f, -180.0f, 180.0f);
	settings.emitterHumidity = FiniteClamp(a_json.value("EmitterHumidity", 0.9f), 0.9f, 0.0f, 1.0f);
	settings.emitterFog = FiniteClamp(a_json.value("EmitterFog", 0.8f), 0.8f, 0.0f, 1.0f);
	settings.emitterPrecipitation = FiniteClamp(a_json.value("EmitterPrecipitation", 0.7f), 0.7f, 0.0f, 1.0f);
	settings.emitterStorm = FiniteClamp(a_json.value("EmitterStorm", 0.4f), 0.4f, 0.0f, 1.0f);
	settings.freezeSimulation = a_json.value("FreezeSimulation", false);
	settings.debugView = 0;
}

void Microclimates::SaveSettings(json& a_json)
{
	a_json = { {"Enable", settings.Enable}, {"FieldRadiusMeters", settings.fieldRadiusMeters},
		{"WindSpeed", settings.windSpeed}, {"WindDirectionDegrees", settings.windDirectionDegrees}, {"LocalizedFog", settings.localizedFog},
		{"LocalizedPrecipitation", settings.localizedPrecipitation}, {"TerrainInfluence", settings.terrainInfluence},
		{"DebugEmitter", settings.debugEmitter}, {"EmitterDistanceMeters", settings.emitterDistanceMeters},
		{"EmitterRadiusMeters", settings.emitterRadiusMeters}, {"EmitterAngleDegrees", settings.emitterAngleDegrees},
		{"EmitterHumidity", settings.emitterHumidity}, {"EmitterFog", settings.emitterFog},
		{"EmitterPrecipitation", settings.emitterPrecipitation}, {"EmitterStorm", settings.emitterStorm},
		{"FreezeSimulation", settings.freezeSimulation} };
}

void Microclimates::RestoreDefaultSettings() { settings = {}; }
