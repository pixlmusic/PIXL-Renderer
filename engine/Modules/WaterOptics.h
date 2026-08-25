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
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
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

	/** @brief Loads the water caustics DDS texture from disk. */
	virtual void SetupResources() override;

	/** @brief Binds the caustics texture SRV to the pixel shader for the current frame. */
	virtual void Prepass() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; };
};
