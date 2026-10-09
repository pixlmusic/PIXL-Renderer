#pragma once

#include "RenderModule.h"
#include "Buffer.h"

#include <memory>

/** Experimental renderer-owned spatial weather field. Skyrim's TESWeather remains authoritative. */
struct Microclimates : RenderModule
{
	struct alignas(16) FieldConstants
	{
		float4 originExtent{};       // absolute field origin XY, world extent, simulation dt
		float4 previousOriginWind{}; // previous origin XY, wind velocity XY in world units/sec
		float4 baseWeather{};         // humidity, fog, precipitation, cloud potential
		float4 emitterPosition{};     // absolute XY, radius, enabled
		float4 emitterWeather{};      // humidity, fog, precipitation, storm potential
		float4 controls{};            // enabled, debug visualization, decay, reserved
		float4 extendedControls{};    // approximate lowland fog influence
	};
	STATIC_ASSERT_ALIGNAS_16(FieldConstants);
	static_assert(sizeof(FieldConstants) == 112);

	struct Settings
	{
		bool Enable = false;
		float fieldRadiusMeters = 30000.0f;
		float windSpeed = 7.0f;
		float windDirectionDegrees = 42.0f;
		float localizedFog = 0.7f;
		float localizedPrecipitation = 0.65f;
		float terrainInfluence = 0.35f;
		bool debugEmitter = false;
		float emitterDistanceMeters = 8000.0f;
		float emitterRadiusMeters = 4500.0f;
		float emitterAngleDegrees = 35.0f;
		float emitterHumidity = 0.9f;
		float emitterFog = 0.8f;
		float emitterPrecipitation = 0.7f;
		float emitterStorm = 0.4f;
		bool freezeSimulation = false;
		std::uint32_t debugView = 0;
	};

	struct ReadOnlyFieldResources
	{
		ID3D11ShaderResourceView* weather = nullptr;
		ID3D11ShaderResourceView* storm = nullptr;
		ID3D11Buffer* constants = nullptr;
		std::uint32_t resolution = 0;
	};

	Microclimates();
	~Microclimates();
	Microclimates(const Microclimates&) = delete;
	Microclimates& operator=(const Microclimates&) = delete;

	std::string GetName() override { return "PIXL Microclimates"; }
	std::string GetDisplayName() override { return "PIXL Microclimates"; }
	std::string GetShortName() override { return "Microclimates"; }
	std::string_view GetCategory() const override { return ModuleGroups::kOther; }
	std::string GetModuleSupportLink() override { return {}; }
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;
	bool IsDisabledByDefault() const override { return true; }
	// Field resources are runtime-bound; Sky/Particle consumers do not use a
	// module permutation define. Standalone compute shaders compile separately.
	bool HasNoPipelinePermutationDependencies() const override { return true; }

	void SetupResources() override;
	void Reset() override;
	void Prepass() override;
	void DrawSettings() override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void RestoreDefaultSettings() override;

	// Called by Atmosphere immediately before its froxel material pass.
	void UpdateField();
	void BindAtmosphereField();
	void UnbindAtmosphereField();
	void BindSkyCloudField();
	void BindParticleField();
	[[nodiscard]] ReadOnlyFieldResources GetReadOnlyFieldResources() const;
	[[nodiscard]] FieldConstants GetCurrentFieldConstants() const;
	[[nodiscard]] bool FieldReady() const;
	[[nodiscard]] std::uint32_t SimulationTickCount() const;

	Settings settings{};

private:
	struct Runtime;
	std::unique_ptr<Runtime> runtime;
};
