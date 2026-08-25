#pragma once

#include "RenderModule.h"
#include "Buffer.h"

struct GlintParameters
{
	bool enabled = false;
	float screenSpaceScale = 1.5f;
	float logMicrofacetDensity = 40.f;
	float microfacetRoughness = .015f;
	float densityRandomization = 2.f;
};

/**
 * @brief RenderModule that replaces Skyrim's legacy material system with physically-based rendering.
 *
 * Mod authors can supply PBR texture sets (roughness, metallic, displacement, etc.)
 * that are interpreted in a physically correct BRDF, producing realistic surface
 * response to lighting across all weather and time-of-day conditions.
 */
struct MaterialForge : RenderModule
{
public:
	/** @brief Returns the internal name of the feature. */
	virtual std::string GetName() override { return "Material Forge"; }
	/** @brief Returns the user-facing translated display name. */
	virtual std::string GetDisplayName() override { return "PIXL PBR SKYRIM"; }
	/** @brief Returns the short identifier used in logs and shader defines. */
	virtual std::string GetShortName() override { return "MaterialForge"; }
	/** @brief Returns the feature category for menu grouping. */
	virtual std::string_view GetCategory() const override { return ModuleGroups::kMaterials; }
	/** @brief Indicates this is a core feature bundled with the main mod. */
	virtual bool IsCore() const override { return true; }
	/** @brief Indicates this feature appears in the in-game settings menu. */
	virtual bool IsInMenu() const override { return true; }
	/** @brief Suppresses the fail-to-load message for this always-available feature. */
	virtual bool DrawFailLoadMessage() const override { return false; }

	/** @brief Returns a human-readable summary and bullet-point list of feature capabilities. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return std::make_pair(
			"PIXL PBR SKYRIM unifies legacy and authored materials under physical rendering and extends PIXL Renderer' proven screen-space GI with a fast world-space radiance cache.",
			std::vector<std::string>{
				"Physically-based BRDF (energy-conserving specular & diffuse)",
				"Roughness, metallic, and displacement map support",
				"Hybrid screen-space and cached world-space indirect lighting",
				"Clearcoat, subsurface scattering, and fuzz layers",
				"Procedural glint rendering for sparkle effects",
				"Full landscape and decal PBR support" });
	}

	/** @brief Draws the ImGui settings UI for PBR texture sets and material objects. */
	virtual void DrawSettings() override;
	/** @brief Loads PBR texture set and material object configurations from disk. */
	virtual void SetupResources() override;
	/** @brief Per-frame prepass that initializes the glint noise texture and binds it to the pipeline. */
	virtual void Prepass() override;
	/** @brief Installs all Detours hooks for PBR material, shader, and decal interception. */
	virtual void PostPostLoad() override;
	/** @brief Looks up the default PBR land texture set by editor ID after data files are loaded. */
	virtual void DataLoaded() override;

	/**
	 * @brief Serializes current PBR settings to JSON.
	 * @param o_json Output JSON object to write settings into.
	 */
	virtual void SaveSettings(json& o_json) override;
	/**
	 * @brief Deserializes PBR settings from JSON.
	 * @param o_json Input JSON object to read settings from.
	 */
	virtual void LoadSettings(json& o_json) override;
	/** @brief Resets all PBR settings to their default values. */
	virtual void RestoreDefaultSettings() override;

	struct alignas(16) Settings
	{
		float VertexAOStrength = 1.0f;
		/** Enables the Stage 5 GGX adapter for non-True-PBR direct lighting. */
		uint EnableLegacyPhysicalDirectLighting = 1;
		/** Selects an in-game diagnostic visualization for physical material inputs. */
		uint LegacyPhysicalDebugMode = 0;
		/** Calibrates normalized GGX highlights against Skyrim's authored legacy lighting. */
		float LegacyPhysicalSpecularScale = 0.35f;
		/** Enables conservative per-pixel metalness reconstruction for legacy materials. */
		uint EnableLegacyMetalInference = 1;
		/** Scales both Material Layers conductor data and inferred vanilla metalness. */
		float LegacyMetalInferenceStrength = 0.85f;
		/** Minimum confidence required before a vanilla environment-mapped surface becomes metallic. */
		float LegacyMetalInferenceThreshold = 0.32f;
		/** Upper bound for guessed vanilla metalness; authored/Material Layers data is not capped by this. */
		float LegacyMetalInferenceMaximum = 0.55f;
		/** Promotes calibrated inverse-square attenuation for all local lights. */
		uint EnablePhysicalLocalLightFalloff = 1;
		/** Enables a short depth-buffer contact ray for the most influential local lights. */
		uint EnableLocalContactShadows = 1;
		/** Maximum number of local contact-shadow rays evaluated per pixel (one or two). */
		uint LocalContactShadowLightCount = 2;
		/** Maximum world-space length of each local contact-shadow ray. */
		float LocalContactShadowLength = 96.0f;
		/** Opacity of local contact shadows. */
		float LocalContactShadowStrength = 0.7f;
		/** Enables derivative-based geometric specular anti-aliasing. */
		uint EnableSpecularAA = 1;
		/** Scales shading-normal variance before widening GGX roughness. */
		float SpecularAAStrength = 0.75f;
		/** Bounds the variance contribution so silhouettes do not become uniformly rough. */
		float SpecularAAVarianceClamp = 0.18f;
		/** Restores energy lost by the single-scatter GGX approximation on rough conductors. */
		uint EnableGGXMultiScatter = 1;
		/** Blends from single-scatter GGX to the compensated response. */
		float GGXMultiScatterStrength = 1.0f;
		/** Finite emitter radius used to regularize inverse-square attenuation near a light. */
		float LocalLightMinimumDistance = 28.0f;
		float pad0 = 0.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 80, "MaterialForge::Settings must match its five-register shader ABI.");
	// distribution/Shaders/Common/SharedData.hlsli::MaterialForgeSettings mirrors this exact
	// layout. PipelineBuffer.cpp copies this complete struct immediately before
	// SkinOpticsData, so any CPU/HLSL size drift corrupts every following feature.
	static_assert(offsetof(Settings, EnableLegacyPhysicalDirectLighting) == 4);
	static_assert(offsetof(Settings, LegacyPhysicalDebugMode) == 8);
	static_assert(offsetof(Settings, LegacyPhysicalSpecularScale) == 12);
	static_assert(offsetof(Settings, EnableLegacyMetalInference) == 16);
	static_assert(offsetof(Settings, LegacyMetalInferenceStrength) == 20);
	static_assert(offsetof(Settings, LegacyMetalInferenceThreshold) == 24);
	static_assert(offsetof(Settings, LegacyMetalInferenceMaximum) == 28);
	static_assert(offsetof(Settings, EnablePhysicalLocalLightFalloff) == 32);
	static_assert(offsetof(Settings, EnableLocalContactShadows) == 36);
	static_assert(offsetof(Settings, LocalContactShadowLightCount) == 40);
	static_assert(offsetof(Settings, LocalContactShadowLength) == 44);
	static_assert(offsetof(Settings, LocalContactShadowStrength) == 48);
	static_assert(offsetof(Settings, EnableSpecularAA) == 52);
	static_assert(offsetof(Settings, SpecularAAStrength) == 56);
	static_assert(offsetof(Settings, SpecularAAVarianceClamp) == 60);
	static_assert(offsetof(Settings, EnableGGXMultiScatter) == 64);
	static_assert(offsetof(Settings, GGXMultiScatterStrength) == 68);
	static_assert(offsetof(Settings, LocalLightMinimumDistance) == 72);
	static_assert(offsetof(Settings, pad0) == 76);

	Settings settings;

	static constexpr uint LegacyTuningMagic = 0x504D4654u;  // "PMFT"
	static constexpr uint LegacyTuningVersion = 1u;

	/**
	 * @brief Legacy-to-physical calibration bound through dedicated PS b10.
	 *
	 * Authored Material Forge PBR bypasses this adapter. Keeping it out of the
	 * five-register Settings block preserves the renderer-wide FeatureData ABI.
	 */
	struct alignas(16) LegacyTuningSettings
	{
		// c0
		uint Magic = LegacyTuningMagic;
		uint Version = LegacyTuningVersion;
		uint EnableTuning = 1;
		uint pad0 = 0;

		// c1
		float ConversionStrength = 1.0f;
		float ShininessScale = 1.0f;
		float RoughnessCurve = 1.0f;
		float RoughnessScale = 1.0f;

		// c2
		float RoughnessBias = 0.0f;
		float MinRoughness = 0.04f;
		float MaxRoughness = 1.0f;
		float DielectricF0 = 0.04f;

		// c3
		float SpecularColorInfluence = 1.0f;
		float SpecularStrengthInfluence = 1.0f;
		float BaseColorEnergy = 1.0f;
		float MetallicDiffuseSuppression = 1.0f;

		// c4
		float MetalEnvironmentWeight = 0.30f;
		float MetalSpecularWeight = 0.28f;
		float MetalSmoothnessWeight = 0.24f;
		float MetalColorWeight = 0.18f;

		// c5
		float MetalNoMaskPenalty = 0.72f;
		float MetalNeutralPrior = 0.58f;
		float MetalCrossEvidenceFloor = 0.55f;
		float F0Scale = 1.0f;

		// c6
		float MetalTintChromaLow = 0.06f;
		float MetalTintChromaHigh = 0.32f;
		float MetalColorCorrelationLow = 0.76f;
		float MetalColorCorrelationHigh = 0.97f;
	};
	STATIC_ASSERT_ALIGNAS_16(LegacyTuningSettings);
	static_assert(sizeof(LegacyTuningSettings) == 112, "MaterialForge::LegacyTuningSettings must match PS b10.");

	LegacyTuningSettings legacyTuningSettings;
	ConstantBuffer* legacyTuningCB = nullptr;
	/**
	 * @brief Sets up PBR landscape materials for all quads of a land cell.
	 *
	 * Scans the land's texture sets for PBR configurations and, if any are found,
	 * creates BSLightingShaderMaterialPBRLandscape instances for each quad.
	 *
	 * @param land The land object whose material to set up.
	 * @return True if PBR materials were applied, false if no PBR texture sets were found.
	 */
	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	/**
	 * @brief Configures per-material shader constants and textures for PBR rendering.
	 *
	 * Handles both standard PBR and landscape PBR materials by binding appropriate
	 * textures, PBR parameter constants, and shader flags to the graphics pipeline.
	 *
	 * @param shader The lighting shader being set up.
	 * @param material The material to configure for PBR rendering.
	 * @return True if PBR setup was performed, false if the material is not PBR.
	 */
	bool BSLightingShader_SetupMaterial(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material);

	/**
	 * @brief Binds extended PBR shader resources (displacement, RMAOS textures) to the device context.
	 *
	 * Only issues PSSetShaderResources calls for slots that have been modified since the last flush,
	 * batching consecutive dirty slots into single API calls.
	 *
	 * @param a_context The D3D11 device context to bind resources on.
	 */
	void SetShaderResources(ID3D11DeviceContext* a_context);
	/**
	 * @brief Pre-compiles all PBR-specific lighting shader permutations.
	 * @param shader The BSShader whose PBR permutations to generate.
	 */
	virtual void GenerateShaderPermutations(RE::BSShader* shader) override;

	/** @brief Creates and populates the 128x128 glint noise texture via a compute shader dispatch. */
	void SetupGlintsTexture();
	eastl::unique_ptr<Texture2D> glintsNoiseTexture = nullptr;

	std::unordered_map<uint32_t, std::string> editorIDs;

	struct PBRTextureSetData
	{
		float roughnessScale = 1.f;
		float displacementScale = 1.f;
		float specularLevel = 0.04f;

		RE::NiColor subsurfaceColor;
		float subsurfaceOpacity = 0.f;

		RE::NiColor coatColor = { 1.f, 1.f, 1.f };
		float coatStrength = 1.f;
		float coatRoughness = 1.f;
		float coatSpecularLevel = 0.04f;
		float innerLayerDisplacementOffset = 0.f;

		RE::NiColor fuzzColor;
		float fuzzWeight = 0.f;

		GlintParameters glintParameters;
	};

	/** @brief Loads all PBR texture set configurations from JSON files in Data/PBRTextureSets. */
	void SetupTextureSetData();
	/**
	 * @brief Reloads PBR texture set configurations from disk, updating existing entries in place.
	 *
	 * Also refreshes all active landscape materials with the updated parameters.
	 */
	void ReloadTextureSetData();
	/**
	 * @brief Looks up PBR texture set data by the form's editor ID.
	 * @param textureSet The texture set form to look up.
	 * @return Pointer to the PBR data if found, nullptr otherwise.
	 */
	PBRTextureSetData* GetPBRTextureSetData(const RE::TESForm* textureSet);
	/**
	 * @brief Checks whether a texture set has an associated PBR configuration.
	 * @param textureSet The texture set form to check.
	 * @return True if PBR data exists for this texture set.
	 */
	bool IsPBRTextureSet(const RE::TESForm* textureSet);

	/**
	 * @brief Replaces the default land texture set with a PBR variant if one named "DefaultPBRLand" exists.
	 *
	 * Only performs the replacement once; subsequent calls are no-ops.
	 */
	void SetupDefaultPBRLandTextureSet();

	std::unordered_map<std::string, PBRTextureSetData> pbrTextureSets;
	RE::BGSTextureSet* defaultPbrLandTextureSet = nullptr;
	bool defaultLandTextureSetReplaced = false;
	std::string selectedPbrTextureSetName;
	PBRTextureSetData* selectedPbrTextureSet = nullptr;

	struct PBRMaterialObjectData
	{
		std::array<float, 3> baseColorScale = { 1.f, 1.f, 1.f };
		float roughness = 1.f;
		float specularLevel = 1.f;

		GlintParameters glintParameters;
	};

	/** @brief Loads all PBR material object configurations from JSON files in Data/PBRMaterialObjects. */
	void SetupMaterialObjectData();
	/**
	 * @brief Looks up PBR material object data by the form's editor ID.
	 * @param materialObject The material object form to look up.
	 * @return Pointer to the PBR data if found, nullptr otherwise.
	 */
	PBRMaterialObjectData* GetPBRMaterialObjectData(const RE::TESForm* materialObject);
	/**
	 * @brief Checks whether a material object has an associated PBR configuration.
	 * @param materialObject The material object form to check.
	 * @return True if PBR data exists for this material object.
	 */
	bool IsPBRMaterialObject(const RE::TESForm* materialObject);

	std::unordered_map<std::string, PBRMaterialObjectData> pbrMaterialObjects;
	std::string selectedPbrMaterialObjectName;
	PBRMaterialObjectData* selectedPbrMaterialObject = nullptr;

	RE::BGSTextureSet* currentTextureSet = nullptr;
};
