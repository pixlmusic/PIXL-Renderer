#pragma once

#include <winrt/base.h>

/** @brief Enhances water rendering with realistic caustics and underwater lighting effects. */
struct WaterOptics : RenderModule
{
public:
	struct alignas(16) Settings
	{
		uint32_t EnableEnhancedCaustics = true;
		float CausticsStrength = 1.0f;
		float CausticsDispersion = 0.5f;
		float CausticsFocus = 1.0f;

		uint32_t EnableEnhancedSSR = true;
		float SSRThicknessScale = 1.0f;
		float SSRDistanceScale = 1.0f;
		float SSREdgeFade = 1.0f;

		float SurfaceSSRStrength = 0.82f;
		float CausticsVisibility = 1.25f;
		// Reuses the historical padding lane; FeatureData size and following
		// settings offsets remain unchanged.
		float WaterTintStrength = 0.35f;
		float ReflectionBrightness = 0.88f;

		uint32_t EnableDynamicFoam = true;
		float FoamStrength = 0.78f;
		float FoamScale = 1.0f;
		// Retained as a zeroed compatibility lane; player-projected wake foam was
		// removed in favour of water-owned flow and geometry contact.
		float PlayerWakeStrength = 0.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 64, "WaterOptics settings must match the four-register FeatureData block.");

	Settings settings;
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
	winrt::com_ptr<ID3D11ShaderResourceView> foamStencilView;
	virtual inline std::string GetName() override { return "Water Optics"; }
	virtual std::string GetDisplayName() override { return T("feature.water_optics.name", "Water Optics"); }
	/** @brief Returns the short identifier used for file paths and logging. */
	virtual inline std::string GetShortName() override { return "WaterOptics"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_OPTICS"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kWater; }

	/** @brief Returns a summary description and list of key features for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.water_optics.description", "Water Optics enhances water rendering with realistic caustics and underwater lighting effects.\nThis feature adds dynamic light patterns and improved water visual quality."),
			{ T("feature.water_optics.key_feature_1", "Realistic water caustics"),
				T("feature.water_optics.key_feature_2", "Enhanced underwater lighting"),
				T("feature.water_optics.key_feature_3", "Dynamic light patterns on water surfaces"),
				T("feature.water_optics.key_feature_4", "Improved water visual fidelity"),
				T("feature.water_optics.key_feature_5", "Atmospheric underwater effects") } };
	};

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;
	bool AffectsCachedShader(
		RE::BSShader::Type shaderType,
		std::uint32_t,
		CachedShaderStage stage) override
	{
		// All current WaterOptics integration is pixel-stage code. Preserve cached
		// vertex/compute entries when the module version changes.
		return stage == CachedShaderStage::Pixel && HasShaderDefine(shaderType);
	}

	/** @brief Loads the water caustics and linear foam-mask textures from disk. */
	virtual void SetupResources() override;

	/** @brief Binds the caustics and foam-mask SRVs to the pixel shader. */
	virtual void Prepass() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; };
};
