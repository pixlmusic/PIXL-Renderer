#pragma once

#include "Buffer.h"

struct Atmosphere : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "180146";

public:
	virtual inline std::string GetName() override { return "Atmosphere"; }
	virtual std::string GetDisplayName() override { return T("feature.exponential_height_fog.name", "Atmosphere"); }
	virtual inline std::string GetShortName() override { return "Atmosphere"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLighting; }

	virtual inline std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.exponential_height_fog.description", "Atmosphere adds a realistic fog effect that increases in density with height, enhancing atmospheric depth and immersion in the game environment."),
			{ T("feature.exponential_height_fog.key_feature_1", "Added exponential height fog effect"),
				T("feature.exponential_height_fog.key_feature_2", "Adapted to vanilla fog settings"),
				T("feature.exponential_height_fog.key_feature_3", "Creates atmospheric depth") } };
	};

	virtual inline std::string_view GetShaderDefineName() override { return "ATMOSPHERE_PIPELINE"; }
	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	/** @brief Draws the ImGui settings UI for fog parameters and volumetric fog options. */
	virtual void DrawSettings() override;
	/** @brief Creates samplers and the volumetric fog constant buffer. */
	virtual void SetupResources() override;
	/** @brief Releases all cached volumetric fog compute shaders so they can be recompiled. */
	virtual void ClearShaderCache() override;
	/**
	 * @brief Runs the volumetric fog pipeline: material setup, conservative depth,
	 * light scattering, and front-to-back integration.
	 */
	virtual void Prepass() override;

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Registers all fog parameters as weather-interpolatable variables. */
	void RegisterWeatherVariables() override;
	/** @brief Captures the current directional shadow map SRV for use in volumetric fog light scattering. */
	void CaptureDirectionalShadowMap();

	struct alignas(16) Settings
	{
		uint enabled = 1;
		uint useWorldProbes = 1;
		float startDistance = 58680.6f;
		float fogHeight = 11304.1f;
		float fogHeightFalloff = 1.243f;
		float fogDensity = 0.138f;
		float directionalInscatteringMultiplier = 1.17f;
		float directionalInscatteringAnisotropy = 0.158f;
		float4 inscatteringTint = { 1.0f, 1.0f, 1.0f, 0.41568628f };
		float cubemapMipLevel = 5.0f;
		float sunlightAttenuationAmount = 0.15f;
		uint respectVanillaFogFade = 0;
		uint disableVanillaFog = 1;
		float4 fogInscatteringColor = { 0.623529434f, 0.796078444f, 0.8117647f, 0.450980395f };
		float originalFogColorAmount = 0.0f;
		uint volumetricFogEnabled = 1;
		uint volumetricGridPixelSize = 24;
		uint volumetricGridSizeZ = 64;
		float volumetricFogDistance = 32506.0f;
		float volumetricFogStartDistance = 0.0f;
		float volumetricFogNearFadeInDistance = 0.0f;
		float volumetricFogExtinctionScale = 0.55f;
		float4 volumetricFogAlbedo = { 0.9529412f, 0.968627453f, 1.0f, 0.647058845f };
		float4 volumetricFogEmissive = { 1.0f, 1.0f, 1.0f, 0.09411765f };
		float volumetricDirectionalScatteringIntensity = 1.22f;
		float volumetricShadowBias = 0.004f;
		float volumetricDepthDistributionScale = 8.1f;
		float volumetricSkyLightingIntensity = 1.0f;
		float volumetricFogScatteringDistribution = 0.19f;
		float volumetricHistoryWeight = 0.96f;
		uint volumetricHistoryMissSampleCount = 4;
		float volumetricSampleJitterMultiplier = 0.0f;
		float volumetricUpsampleJitterMultiplier = 1.0f;
		float volumetricLocalLightScatteringIntensity = 1.43f;
		float skyProtection = 0.2f;
		float pad0 = 0.0f;

		// Atmosphere 2.0 reconstruction / temporal quality.
		uint volumetricUseDisplayResolutionGrid = 1;
		uint volumetricDepthAwareUpsampling = 1;
		float volumetricDepthAwareUpsamplingStrength = 8.1f;
		float volumetricHistoryRadianceClamp = 3.9f;

		float volumetricHistoryDepthRejection = 8.0f;
		uint mapAtmosphereEnabled = 1;
		uint mapDisableVolumetricFog = 1;
		uint mapDisableVanillaFog = 1;

		// World/local map visibility profile. Gameplay values remain untouched.
		float mapFogDensityMultiplier = 0.24f;
		float mapFogHeightFalloffMultiplier = 0.52f;
		float mapStartDistance = 0.0f;
		float mapMinimumTransmittance = 0.32f;

		float mapAmbientInscatteringMultiplier = 0.0f;
		float mapDirectionalInscatteringMultiplier = 1.13f;
		float mapSunlightAttenuationMultiplier = 0.29f;
		float mapWorldProbeMultiplier = 0.53f;

		// Automatic mode preserves the authored sliders as a look/quality baseline,
		// then adapts visibility, phase and range from Skyrim's live weather.
		uint automaticWeatherFog = 1;
		float automaticWeatherStrength = 0.90f;
		float minimumAtmosphereTransmittance = 0.14f;
		float weatherMieStrength = 1.0f;
	} settings;
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 272, "Atmosphere settings must match the 17-register FeatureData block.");

	Settings GetCommonBufferData() const;

private:
	Settings ResolveRuntimeSettings() const;

	struct VolumetricFogCB
	{
		DirectX::XMUINT4 gridSizeAndFlags = {};
		float4 invGridSizeAndNearFade = {};
		float4 gridZParams = {};
		float4x4 clipToWorld = {};
		float4 frameJitterOffsets[16] = {};
		float4 historyParameters = {};
		float4 jitterParameters = {};  // x = LightScatteringSampleJitterMultiplier, y = StateFrameIndexMod8, zw = unused
	};
	STATIC_ASSERT_ALIGNAS_16(VolumetricFogCB);

	void EnsureVolumetricResources();
	void ReleaseVolumetricResources();
	void BindIntegratedLightScattering();
	ID3D11ComputeShader* GetMaterialSetupCS();
	ID3D11ComputeShader* GetConservativeDepthCS();
	ID3D11ComputeShader* GetLightScatteringCS();
	ID3D11ComputeShader* GetIntegrationCS();

	std::unique_ptr<Texture3D> vBufferA;
	std::unique_ptr<Texture2D> conservativeDepth;
	std::unique_ptr<Texture2D> conservativeDepthHistory;
	std::unique_ptr<Texture3D> lightScattering;
	std::unique_ptr<Texture3D> lightScatteringHistory;
	std::unique_ptr<Texture3D> integratedLightScattering;
	std::unique_ptr<ConstantBuffer> volumetricFogCB;
	winrt::com_ptr<ID3D11SamplerState> linearSampler;
	winrt::com_ptr<ID3D11SamplerState> shadowSampler;
	winrt::com_ptr<ID3D11ShaderResourceView> directionalShadowMap;
	ID3D11ComputeShader* materialSetupCS = nullptr;
	ID3D11ComputeShader* conservativeDepthCS = nullptr;
	ID3D11ComputeShader* lightScatteringCS = nullptr;
	ID3D11ComputeShader* integrationCS = nullptr;
	DirectX::XMUINT4 currentGridSize = {};
	bool hasLightScatteringHistory = false;
	bool hasConservativeDepthHistory = false;
	bool lastInInterior = false;
	bool lastHideSky = false;
	bool lastInMapMenu = false;
	bool hasSceneClassHistory = false;
	uint32_t lastLightingInputFlags = 0;
	bool hasLightingInputHistory = false;
	uint32_t lastPrepassFrame = UINT32_MAX;
	uint64_t temporalHistoryId = 0;
};
