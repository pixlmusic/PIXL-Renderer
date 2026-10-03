// Community Shaders Water Effects-derived file.
// Modified for PIXL Renderer, 2026: Water Optics interface and physical tuning.
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include <winrt/base.h>

#include "Renderer/RendererMetadata.h"

/** @brief Enhances water rendering with realistic caustics and underwater lighting effects. */
struct WaterOptics : RenderModule
{
public:
	struct alignas(16) Settings
	{
		uint32_t EnableEnhancedCaustics = true;
		float CausticsStrength = static_cast<float>(PIXL::Metadata::Settings::WaterCausticsStrength.defaultValue);
		float CausticsDispersion = 0.5f;
		float CausticsFocus = 1.0f;

		uint32_t EnableEnhancedSSR = true;
		float SSRThicknessScale = 1.0f;
		float SSRDistanceScale = 1.0f;
		float SSREdgeFade = 1.0f;

		// Keep distant water subdued in fog; users can raise this for a brighter,
		// more reflective look without changing the physically based default.
		float SurfaceSSRStrength = 0.70f;
		float CausticsVisibility = 1.25f;
		// Reuses the historical padding lane; FeatureData size and following
		// settings offsets remain unchanged.
		float WaterTintStrength = 0.35f;
		float ReflectionBrightness = 0.72f;

		uint32_t EnableDynamicFoam = true;
		float FoamStrength = 0.78f;
		float FoamScale = 1.0f;
		// Reuses the retired player-wake lane. 0 Low, 1 Medium, 2 High, 3 Cinematic.
		// This is a workload control; it does not alter the authored water balance.
		float SSRTraceQuality = 2.0f;
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == PIXL::Metadata::ABI::WaterOptics.sizeBytes, "WaterOptics settings must match the four-register FeatureData block.");

	Settings settings;
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
	winrt::com_ptr<ID3D11ShaderResourceView> foamStencilView;
	winrt::com_ptr<ID3D11ShaderResourceView> rapidWaterView;
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

	/** @brief Loads water optics, rapid-water coverage, and linear foam-mask textures from disk. */
	virtual void SetupResources() override;

	/** @brief Binds water optics, rapid-water coverage, and foam-mask SRVs to the pixel shader. */
	virtual void Prepass() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; };
};
