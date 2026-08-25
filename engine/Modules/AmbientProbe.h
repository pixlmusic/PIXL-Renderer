#pragma once

#include "Buffer.h"

struct AmbientProbe : RenderModule
{
public:
	virtual bool IsCore() const override { return true; };

	virtual inline std::string GetName() override { return "Ambient Probe"; }
	virtual std::string GetDisplayName() override { return T("feature.ambient_probe.name", "Ambient Probe"); }
	virtual inline std::string GetShortName() override { return "AmbientProbe"; }
	virtual inline std::string_view GetShaderDefineName() override { return "AMBIENT_PROBE"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLighting; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.ambient_probe.description", "Replaces the game's ambient lighting with physically-based AmbientProbe derived from cubemap spherical harmonics."),
			{ T("feature.ambient_probe.key_feature_1", "Projects environment and sky cubemaps into spherical harmonics (SH) for irradiance"),
				T("feature.ambient_probe.key_feature_2", "Dual AmbientProbe sources: environment cubemap (World Probes) and Skyrim's native sky reflections cubemap"),
				T("feature.ambient_probe.key_feature_3", "DALC brightness matching to keep AmbientProbe consistent with the game's ambient light levels"),
				T("feature.ambient_probe.key_feature_4", "Configurable per-source intensity, saturation, fog mixing, and per-weather overrides"),
				T("feature.ambient_probe.key_feature_5", "Static AmbientProbe fallback textures for out-of-world objects (e.g. inventory items)") } };
	};

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	Texture2D* envIBLTexture = nullptr;
	Texture2D* skyIBLTexture = nullptr;
	ID3D11ComputeShader* diffuseIBLCS = nullptr;
	bool diffuseAmbientCompileAttempted = false;
	bool unavailableResourcesReported = false;

	virtual void RestoreDefaultSettings() override;
	/** @brief Draws the ImGui settings UI for AmbientProbe intensity, saturation, DALC, and fog options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	/** @brief Registers AmbientProbe parameters as weather-interpolatable variables. */
	virtual void RegisterWeatherVariables() override;

	/** @brief Binds AmbientProbe and static fallback textures as pixel shader resources for the reflections prepass. */
	virtual void ReflectionsPrepass() override;
	/** @brief Projects environment and sky cubemaps into spherical harmonics and binds the resulting AmbientProbe textures. */
	virtual void Prepass() override;
	/** @brief Creates AmbientProbe textures, compiles the diffuse AmbientProbe compute shader, and loads static fallback cubemaps. */
	virtual void SetupResources() override;
	/** @brief Releases the cached diffuse AmbientProbe compute shader so it can be recompiled. */
	virtual void ClearShaderCache() override;

	struct Settings
	{
		uint EnableAmbientProbe = 0;
		uint PreserveFogLuminance = 0;
		uint UseStaticAmbientProbe = 1;
		float DALCAmount = 1.0f;
		float EnvironmentProbeScale = 1.0f;
		float SkyProbeScale = 1.0f;
		float EnvironmentProbeSaturation = 1.0f;
		float SkyProbeSaturation = 1.0f;
		float FogAmount = 0.0f;
		uint DALCMode = 2;  // 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky, 3: DALC + Sky (Directional)
		bool DisableInInteriors = true;
		bool DisableInWorldMap = true;
		bool DisableInLoadingScreen = true;
	} settings;

	struct alignas(16) PerFrame
	{
		uint EnableAmbientProbe;
		uint PreserveFogLuminance;
		uint UseStaticAmbientProbe;
		float DALCAmount;
		float EnvironmentProbeScale;
		float SkyProbeScale;
		float EnvironmentProbeSaturation;
		float SkyProbeSaturation;
		float FogAmount;
		uint DALCMode;
		float pad0[2];
	};
	STATIC_ASSERT_ALIGNAS_16(PerFrame);

	eastl::unique_ptr<Texture2D> staticDiffuseAmbientTexture = nullptr;
	eastl::unique_ptr<Texture2D> staticSpecularIBLTexture = nullptr;

	/** @brief Builds the GPU constant-buffer data, forcing EnableAmbientProbe off when IsDisabledForCurrentScene(). */
	PerFrame GetCommonBufferData() const;
	/** @brief Returns true when AmbientProbe should be suppressed in the current scene per the DisableIn* toggles (loading screens, world map, interiors). */
	bool IsDisabledForCurrentScene() const;
	/** @brief Returns the diffuse AmbientProbe spherical harmonics compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetDiffuseAmbientCS();
};
