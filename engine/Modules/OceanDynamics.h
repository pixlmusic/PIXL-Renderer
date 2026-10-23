// PIXL Renderer - weather-driven spectral ocean simulation.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Buffer.h"
#include "RenderModule.h"
#include "PIXL/OceanAPI.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

class Texture2D;

/** Experimental DX11 ocean simulation. Disabled at boot until live geometry validation. */
struct OceanDynamics : RenderModule
{
	static constexpr std::uint32_t CascadeCount = 4;
	static constexpr std::uint32_t Resolution = 128;

	struct Settings
	{
		std::uint32_t EnableSimulation = 1;
		std::uint32_t EnableGeometryDisplacement = 1;
		// 0 Soft, 1 Balanced, 2 Detailed, 3 Strong. Controls projected hull
		// tessellation and shore-crest slope response without rebuilding resources.
		std::uint32_t GeometryQuality = 0;
		float WindSpeed = 8.0f;
		float WindDirectionDegrees = 63.0f;
		float SwellHeight = 0.0f;
		float SwellPeriod = 11.7f;
		float SwellDirectionDegrees = 76.0f;
		float CrossSwellDirectionDegrees = 135.0f;
		float CrossSwellStrength = 0.24f;
		float DeepRogueWaveStrength = 1.0f;
		float Choppiness = 0.49f;
		float ShoalingStrength = 1.5f;
		float BreakerStrength = 2.0f;
		float FoamStrength = 0.80f;
		float FoamLifetime = 5.5f;
		float CrestTranslucency = 1.0f;
		std::uint32_t EnableSpray = 1;
		std::uint32_t EnableBreakingWaves = 1;
		float SprayDensity = 1.5f;
	};

	struct alignas(16) SimulationConstants
	{
		float timeSeconds{};
		float windSpeed{};
		float windDirectionRadians{};
		float swellHeight{};
		float swellPeriod{};
		float swellDirectionRadians{};
		float choppiness{};
		float resolution{};
		float inverseResolution{};
		float deltaSeconds{};
		float foamLifetime{};
		float foamStrength{};
		float cascadeExtent[CascadeCount]{};
		float weatherEnergy{ 1.0f };
		float crossSwellDirectionRadians{};
		float crossSwellStrength{};
		float deepRogueWaveStrength{};
	};
	static_assert(sizeof(SimulationConstants) == 80);

	struct alignas(16) FFTConstants
	{
		std::uint32_t resolution{};
		std::uint32_t vertical{};
		std::uint32_t padding[2]{};
	};
	static_assert(sizeof(FFTConstants) == 16);

	struct alignas(16) BathymetryConstants
	{
		float worldBounds[4]{};  // source map origin XY, opposite corner XY
		float heightEncoding[4]{};  // minimum and maximum source height encoding, z-range metadata
		float shoreSettings[4]{};  // valid, attenuation depth in Skyrim units, foam strength, active
		float geometrySettings[4]{}; // geometry tier, crest transmission, reserved, sea level for stable pixel shoreline depth
		float phaseSettings[4]{}; // swell travel direction XY, phase-warp strength, breaker profile
		float timingSettings[4]{}; // current and previous simulation time in seconds
		float surfBoardPosition[4]{}; // absolute world XY/Z and active flag from Surfing V2 telemetry
		float surfBoardVelocity[4]{}; // XY velocity, speed, reserved
		float localTerrainPatch[4]{}; // stable world-cell atlas origin XY, sample spacing, valid
	};
	static_assert(sizeof(BathymetryConstants) == 144);

	struct alignas(16) WaveCageConstants
	{
		float resolution{};
		float valid{};
		float blend{};
		float maximumRadius{};
	};
	static_assert(sizeof(WaveCageConstants) == 16);

	struct WaveCageField
	{
		static constexpr std::uint32_t Resolution = 512;
		std::string worldspace;
		std::uint32_t worldspaceFormID{};
		float seaLevel{};
		float bounds[4]{};
		std::vector<std::array<float, 4>> radii;
	};

	std::string GetName() override { return "Ocean Dynamics"; }
	std::string GetDisplayName() override { return T("feature.ocean_dynamics.name", "PIXL Ocean Dynamics"); }
	std::string GetShortName() override { return "OceanDynamics"; }
	std::string_view GetShaderDefineName() override { return "OCEAN_DYNAMICS"; }
	std::string_view GetCategory() const override { return ModuleGroups::kWater; }
	bool HasShaderDefine(RE::BSShader::Type type) override { return type == RE::BSShader::Type::Water; }
	bool IsDisabledByDefault() const override { return true; }

	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;
	void SetupResources() override;
	void Reset() override;
	void EarlyPrepass() override;
	void DrawSettings() override;
	void LoadSettings(json& value) override;
	void SaveSettings(json& value) override;
	void RestoreDefaultSettings() override;
	void ClearShaderCache() override;

	bool IsActive() const noexcept;
	ID3D11ShaderResourceView* GetDisplacementSRV() const noexcept;
	ID3D11ShaderResourceView* GetPreviousDisplacementSRV() const noexcept;
	void BindWaterGeometryResources(const RE::BSGeometry* geometry, bool openOceanTile) noexcept;
	// Called by PIXL's existing D3D11 draw bridge around classified native water
	// draws. Begin/End must bracket the same draw call.
	bool BeginWaterDraw(ID3D11DeviceContext* context, bool indexedDraw = true) noexcept;
	bool HasOceanSprayDrawPending() const noexcept;
	void EndWaterDraw(ID3D11DeviceContext* context) noexcept;
	void DrawOceanSpray(ID3D11DeviceContext* context) noexcept;
	void OnWorldspaceChanged() noexcept;
	std::uint32_t QueryBatch(const PIXL_OceanRequestV1* requests,
		PIXL_OceanSampleV1* samples, std::uint32_t count) const noexcept;
	bool SubmitTerrainPatch(const PIXL_OceanTerrainPatchV1* patch,
		const float* heights, std::uint32_t count) noexcept;

	Settings settings{};

private:
	bool CreateResources();
	void ReleaseResources() noexcept;
	bool EnsureShaders();
	void UpdateWaveCageField() noexcept;
	void RequestWaveCageField(float seaLevel) noexcept;
	struct WaterTessShaders
	{
		winrt::com_ptr<ID3D11HullShader> hull;
		winrt::com_ptr<ID3D11DomainShader> domain;
		winrt::com_ptr<ID3D11GeometryShader> shoreline;
	};
	bool EnsureSprayResources() noexcept;
	void UpdateSpray(ID3D11DeviceContext* context, float seaLevel) noexcept;
	WaterTessShaders* GetWaterTessShaders(std::uint32_t descriptor, bool counterClockwise);
	void RunSimulation(float deltaSeconds);
	void UpdateSimulationForCurrentFrame() noexcept;
	void QueueGameplayReadback(ID3D11DeviceContext* context) noexcept;
	struct QueryField
	{
		std::array<float, Resolution * Resolution * 4> texels{};
		double simulationSeconds{};
		std::chrono::steady_clock::time_point readyAt{};
	};
	struct QueryHistory
	{
		QueryField current;
		QueryField previous;
		bool hasPrevious{};
		std::uint64_t generation{};
	};

	std::unique_ptr<Texture2D> spectrumInitial;
	std::array<std::unique_ptr<Texture2D>, 2> spectrumFields;
	std::array<std::unique_ptr<Texture2D>, 2> fftScratch;
	std::unique_ptr<Texture2D> displacement;
	std::unique_ptr<Texture2D> previousDisplacement;
	std::unique_ptr<Texture2D> previousRenderedDisplacement;
	bool renderHistoryDirty{};
	std::array<winrt::com_ptr<ID3D11Texture2D>, 2> gameplayReadback;
	std::array<double, 2> gameplayReadbackTimes{};
	std::array<std::chrono::steady_clock::time_point, 2> gameplayReadbackSubmittedAt{};
	std::array<bool, 2> gameplayReadbackPending{};
	std::uint32_t gameplayReadbackSlot{};
	std::uint32_t gameplayReadbackCadence{};
	std::uint64_t queryGeneration{ 1 };
	std::atomic<std::shared_ptr<const QueryHistory>> gameplayHistory{};
	std::unique_ptr<ConstantBuffer> simulationCB;
	std::unique_ptr<ConstantBuffer> fftCB;
	std::unique_ptr<ConstantBuffer> bathymetryCB;
	std::unique_ptr<ConstantBuffer> waveCageCB;
	std::unique_ptr<Texture2D> waveCageTexture;
	std::atomic<std::shared_ptr<const WaveCageField>> waveCageField{};
	std::future<std::shared_ptr<const WaveCageField>> waveCageBuild;
	std::atomic<float> requestedWaveCageSeaLevel{ std::numeric_limits<float>::quiet_NaN() };
	std::atomic<float> waveCageBlend{};
	static constexpr std::uint32_t LocalTerrainResolution = 33;
	static constexpr std::uint32_t LocalTerrainSampleCount = LocalTerrainResolution * LocalTerrainResolution;
	struct LocalTerrainData
	{
		PIXL_OceanTerrainPatchV1 metadata{};
		std::array<float, LocalTerrainSampleCount> heights{};
		std::uint64_t generation{};
	};
	mutable std::mutex localTerrainMutex;
	LocalTerrainData pendingLocalTerrain{};
	std::vector<LocalTerrainData> localTerrainTiles;
	std::unique_ptr<Texture2D> localTerrainAtlas;
	std::vector<float> localAtlasHeights, localAtlasTarget;
	std::array<float,4> localAtlasBounds{};
	std::uint32_t localAtlasWorld{};
	std::uint64_t localAtlasRevision{};
	bool localAtlasUpdating{}, localAtlasFailed{};
	std::atomic_bool localAtlasInvalidated{};
	void UpdateLocalTerrainAtlas() noexcept;
	winrt::com_ptr<ID3D11ComputeShader> spectrumInitCS;
	winrt::com_ptr<ID3D11ComputeShader> spectrumUpdateCS;
	winrt::com_ptr<ID3D11ComputeShader> fftCS;
	winrt::com_ptr<ID3D11ComputeShader> packDisplacementCS;
	winrt::com_ptr<ID3D11SamplerState> displacementSampler;
	std::unordered_map<std::uint64_t, WaterTessShaders> waterTessShaders;
	float bathymetrySimulationTime{ std::numeric_limits<float>::quiet_NaN() };
	// Water setup can run ahead of the immediate-context draw. Queue only a small
	// number of classified packets and consume by the exact vertex-buffer identity.
	struct PendingOceanDraw
	{
		const RE::BSGeometry* geometry{};
		ID3D11Buffer* vertexBuffer{};
		ID3D11Buffer* rendererVertexBuffer{};
		UINT vertexStride{};
		float seaLevel{};
		std::uint64_t frame{};
		std::uint32_t setupThread{};
	};
	std::mutex pendingOceanDrawMutex;
	std::deque<PendingOceanDraw> pendingOceanDraws;
	struct SprayParticleGPU
	{
		float position[3]{};
		float age{};
		float velocity[3]{};
		float lifetime{};
		float size{};
		float seed{};
		float padding[2]{};
	};
	static_assert(sizeof(SprayParticleGPU) == 48);
	static constexpr std::uint32_t SprayParticleCount = 4096;
	winrt::com_ptr<ID3D11Buffer> sprayParticles;
	winrt::com_ptr<ID3D11ShaderResourceView> sprayParticlesSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> sprayParticlesUAV;
	winrt::com_ptr<ID3D11ComputeShader> sprayUpdateCS;
	winrt::com_ptr<ID3D11VertexShader> sprayVertexShader;
	winrt::com_ptr<ID3D11PixelShader> sprayPixelShader;
	winrt::com_ptr<ID3D11Buffer> sprayConstants;
	winrt::com_ptr<ID3D11BlendState> sprayBlendState;
	winrt::com_ptr<ID3D11DepthStencilState> sprayDepthState;
	winrt::com_ptr<ID3D11RasterizerState> sprayRasterizer;
	static constexpr std::uint32_t BreakerAlongCount = 32;
	static constexpr std::uint32_t BreakerCrossCount = 24;
	struct BreakerNodeGPU { float root[4]{}; float heading[4]{}; float grid[4]{}; };
	static_assert(sizeof(BreakerNodeGPU) == 48);
	winrt::com_ptr<ID3D11Buffer> breakerNodes;
	winrt::com_ptr<ID3D11ShaderResourceView> breakerNodesSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> breakerNodesUAV;
	winrt::com_ptr<ID3D11ComputeShader> breakerUpdateCS;
	winrt::com_ptr<ID3D11VertexShader> breakerVS;
	winrt::com_ptr<ID3D11PixelShader> breakerPS;
	bool breakerCompileFailed{};
	bool EnsureBreakerResources() noexcept;
	std::uint64_t sprayUpdateFrame{ std::numeric_limits<std::uint64_t>::max() };
	std::uint64_t sprayDrawFrame{ std::numeric_limits<std::uint64_t>::max() };
	std::uint64_t sprayDrawEligibleFrame{ std::numeric_limits<std::uint64_t>::max() };
	double simulationTime{};
	float simulationSecondsSinceDispatch{};
	std::uint64_t lastSimulationFrame{ std::numeric_limits<std::uint64_t>::max() };
	float weatherEnergySmoothed{ 1.0f };
	float windSpeedSmoothed{ 8.0f };
	bool resourcesReady{};
	bool initializationPending{ true };
	bool previousDisplacementValid{};
	bool bathymetryUploadPending{ true };
	std::atomic_bool openOceanTileAvailable{ false };
	std::atomic_bool geometryBindingLogged{ false };
	std::atomic_bool simulationDispatchLogged{ false };
	bool oceanSurfaceWasBound{};
	std::string bathymetryWorldspace;
	ID3D11ShaderResourceView* bathymetryMapSRV{};
	bool failureLogged{};
};
