#pragma once

struct DistanceBlend : RenderModule
{
	virtual inline std::string GetName() override { return "Distance Blend"; }
	virtual std::string GetDisplayName() override { return T("feature.distance_blend.name", "Distance Blend"); }
	virtual inline std::string GetShortName() override { return "DistanceBlend"; }
	virtual inline std::string_view GetShaderDefineName() override { return "DISTANCE_BLEND"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLandscapeAndTextures; }
	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.distance_blend.description", "Provides seamless visual transitions between Level of Detail (LOD) objects and full-detail objects, eliminating harsh transitions and creating smooth visual continuity."),
			{ T("feature.distance_blend.key_feature_1", "Smooth LOD object brightness blending"),
				T("feature.distance_blend.key_feature_2", "Enhanced terrain LOD appearance matching"),
				T("feature.distance_blend.key_feature_3", "Snow-specific LOD brightness adjustment"),
				T("feature.distance_blend.key_feature_4", "Optional terrain vertex color modification"),
				T("feature.distance_blend.key_feature_5", "Seamless transition between detail levels") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		float LODTerrainBrightness = 1;
		float LODObjectBrightness = 1;
		float LODObjectSnowBrightness = 1;
		uint DisableTerrainVertexColors = false;
		float LODTerrainGamma = 1;
		float LODObjectGamma = 1;
		float LODObjectSnowGamma = 1;
		float pad;
	};

	Settings settings;

	/** @brief Draws the ImGui settings UI for LOD brightness, gamma, and vertex color configuration. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool IsCore() const override { return true; };
};
