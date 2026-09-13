#pragma once

#include "Buffer.h"

struct MaterialLayers : RenderModule
{
	virtual inline std::string GetName() override { return "Material Layers"; }
	virtual std::string GetDisplayName() override { return T("feature.material_layers.name", "Material Layers"); }
	virtual inline std::string GetShortName() override { return "MaterialLayers"; }
	virtual inline std::string_view GetShaderDefineName() override { return "MATERIAL_LAYERS"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kMaterials; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.material_layers.description", "Material Layers adds advanced material effects including parallax occlusion mapping and complex material blending.\nThis feature enhances surface detail and depth perception for more realistic textures."),
			{ T("feature.material_layers.key_feature_1", "Parallax occlusion mapping for depth"),
				T("feature.material_layers.key_feature_2", "Complex material blending"),
				T("feature.material_layers.key_feature_3", "Terrain heightmap support"),
				T("feature.material_layers.key_feature_4", "Parallax shadows"),
				T("feature.material_layers.key_feature_5", "Height-based texture blending") } };
	};

	/** @brief Returns true only for Lighting shader type. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;
	bool AffectsCachedShader(
		RE::BSShader::Type shaderType,
		std::uint32_t descriptor,
		CachedShaderStage stage) override
	{
		if (shaderType != RE::BSShader::Type::Lighting || stage != CachedShaderStage::Pixel)
			return false;

		const auto technique = (descriptor >> 24u) & 0x3Fu;
		switch (technique) {
		case 0u:   // Default object
		case 1u:   // Environment map
		case 2u:   // Glow map
		case 3u:   // Authored parallax
		case 7u:   // Parallax occlusion
		case 8u:   // Multi-texture landscape
		case 10u:  // Snow object (legacy/nominally unused)
		case 11u:  // Multi-layer parallax
		case 16u:  // Eye/environment-mask compatibility
		case 19u:  // Multi-texture landscape LOD blend
			return true;
		default:
			// Lighting.hlsl explicitly undefines MATERIAL_LAYERS for LOD families,
			// while skin, hair, tree and sparkle paths compile its material work away.
			return false;
		}
	}

	struct alignas(16) Settings
	{
		uint EnableComplexMaterial = 1;

		uint EnableParallax = 1;
		uint EnableTerrain = 0;
		uint EnableHeightBlending = 1;

		uint EnableShadows = 1;
		uint EnableParallaxWarpingFix = 1;

		uint pad[2]{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 32, "MaterialLayers::Settings must remain the StableABI 32-byte block.");

	Settings settings;

	static constexpr uint TuningMagic = 0x504D4C54u;  // "PMLT"
	static constexpr uint TuningVersion = 3u;

	/**
	 * @brief PIXL material tuning data bound through a dedicated PS constant buffer (b9).
	 *
	 * This is deliberately NOT part of PipelineBuffer/FeatureData. The original
	 * 32-byte MaterialLayers::Settings block remains ABI-stable.
	 */
	struct alignas(16) TuningSettings
	{
		// c0
		uint Magic = TuningMagic;
		uint Version = TuningVersion;
		uint EnableObjectAutoPOM = 1;
		uint EnableTerrainAutoPOM = 1;

		// c1
		uint TerrainHeightMode = 0;  // 0 Auto, 1 Authored, 2 Auto-POM, 3 Legacy Alpha
		uint EnableAutoPOMSelfShadows = 0;
		uint EnableTerrainSelfShadows = 1;
		uint EnableDetailReconstruction = 1;

		// c2
		float ObjectAuthoredDepthScale = 1.0f;
		float ObjectAutoHeightScale = 0.014f;
		float ObjectMaxTexelShift = 12.0f;
		float ObjectGrazingProtection = 0.72f;

		// c3
		float ObjectFadeStart = 768.0f;
		float ObjectFadeEnd = 1792.0f;
		float ObjectMaxMip = 6.0f;
		float AutoMinTexelShift = 0.25f;

		// c4
		uint ObjectNearSteps = 8;
		uint ObjectMaxSteps = 16;
		uint ObjectRefinementSteps = 4;
		uint pad0 = 0;

		// c5
		float AutoHeightContrast = 1.35f;
		float AutoHeightNormalInfluence = 1.0f;
		float AutoHeightReferenceMipOffset = 4.0f;
		float AutoHeightChromaRejection = 0.60f;

		// c6
		float ObjectShadowStrength = 0.58f;
		float ObjectShadowBias = 1.0f;
		float ObjectShadowRayScale = 1.0f;
		float pad1 = 0.0f;

		// c7
		float TerrainDepthScale = 0.90f;
		float TerrainMaxTexelShift = 32.0f;
		float TerrainGrazingProtection = 0.60f;
		float TerrainHeightStrength = 0.75f;

		// c8
		float TerrainFadeStart = 1024.0f;
		float TerrainFadeEnd = 2304.0f;
		float TerrainMaxMip = 6.0f;
		float TerrainHeightContrast = 1.15f;

		// c9
		uint TerrainNearSteps = 12;
		uint TerrainMaxSteps = 20;
		uint TerrainRefinementSteps = 4;
		uint pad2 = 0;

		// c10
		float TerrainReferenceMipOffset = 4.0f;
		float TerrainHeightBlendStrength = 1.0f;
		float TerrainShadowStrength = 0.65f;
		float TerrainShadowBias = 1.0f;

		// c11
		float TerrainShadowRayScale = 1.0f;
		float DetailObjectStrength = 0.55f;
		float DetailTerrainStrength = 0.85f;
		float DetailAlbedoStrength = 0.50f;

		// c12
		float DetailNormalStrength = 0.45f;
		float DetailRoughnessStrength = 0.25f;
		float DetailMipSeparation = 1.5f;
		float DetailContrast = 1.0f;

		// c13
		float DetailAntiShimmer = 0.65f;
		float DetailFadeStart = 384.0f;
		float DetailFadeEnd = 1536.0f;
		float DetailMaxMip = 5.0f;

		// c14
		float DetailDarkProtection = 0.50f;
		float DetailSmoothProtection = 0.50f;
		float DetailDominantTerrainMinWeight = 0.65f;
		uint DetailQuality = 2;  // 0 off, 1 albedo, 2 albedo+normal+roughness

		// c15 — terrain source interpretation / frequency rejection
		float TerrainSyntheticPolarity = -1.0f;
		float TerrainSourceMipBias = 1.25f;
		float TerrainHeightDeadZone = 0.025f;
		float TerrainAlphaAssist = 0.90f;

		// c16 — uniform fallback scale + hardware depth
		float TerrainAlphaEvidenceThreshold = 0.012f;
		float TerrainSyntheticScaleFloor = 1.0f;
		float TerrainVirtualDepthStrength = 1.0f;
		float TerrainVirtualDepthMaxWorld = 24.0f;

		// c17
		float TerrainVirtualDepthMaxUV = 0.20f;
		float TerrainVirtualDepthProtrusion = 0.0f;
		uint EnableTerrainVirtualDepth = 1;
		uint TerrainHeightDebugMode = 0;

		// c18 — relief shaping
		float TerrainReliefGamma = 0.85f;
		float TerrainSyntheticGain = 1.75f;
		float ObjectVirtualDepthStrength = 0.0f;
		float ObjectVirtualDepthMaxWorld = 4.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(TuningSettings);
	static_assert(sizeof(TuningSettings) == 304, "MaterialLayers::TuningSettings must match PS b9 v3.");
	static_assert(offsetof(TuningSettings, ObjectVirtualDepthStrength) == 296);
	static_assert(offsetof(TuningSettings, ObjectVirtualDepthMaxWorld) == 300);

	TuningSettings tuningSettings;
	ConstantBuffer* tuningCB = nullptr;
	winrt::com_ptr<ID3D11Texture2D> effectsDepth;
	winrt::com_ptr<ID3D11ShaderResourceView> effectsDepthSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView> effectsDepthUAV;
	winrt::com_ptr<ID3D11ComputeShader> effectsDepthCS;
	bool effectsDepthReady = false;
	bool effectsDepthFailed = false;
	bool showEffectsDepthDebug = false; // Session-only; never saved into release defaults.
	std::array<float, 8> lastEffectsDepthSettings{};
	void ResolveEffectsDepth(ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* masks);
	ID3D11ShaderResourceView* GetEffectsDepth(ID3D11ShaderResourceView* fallback) const
	{
		return effectsDepthReady ? effectsDepthSRV.get() : fallback;
	}
	void Reset() override { effectsDepthReady = false; }
	void ClearShaderCache() override { effectsDepthCS = nullptr; effectsDepthFailed = false; effectsDepthReady = false; }

	/** @brief Enables bLandSpecular INI setting when terrain parallax is active. */
	virtual void DataLoaded() override;

	/** @brief Creates the dedicated material tuning constant buffer. */
	virtual void SetupResources() override;
	/** @brief Uploads and binds PS b9 without changing renderer-wide FeatureData. */
	virtual void Prepass() override;

	/** @brief Draws the ImGui settings UI for complex material, parallax, and shadow options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual bool IsCore() const override { return true; };
};
