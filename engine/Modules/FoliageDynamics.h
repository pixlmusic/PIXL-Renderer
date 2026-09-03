#pragma once

#include "Buffer.h"

struct FoliageDynamics : RenderModule
{
public:
	virtual inline std::string GetName() override { return "Foliage Dynamics"; }
	virtual std::string GetDisplayName() override { return T("feature.foliage_dynamics.name", "Foliage Dynamics"); }
	virtual inline std::string GetShortName() override { return "FoliageDynamics"; }
	virtual inline std::string_view GetShaderDefineName() override { return "FOLIAGE_DYNAMICS"; }
	/** @brief Enables the shared vegetation model for grass and animated tree foliage. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Grass || shaderType == RE::BSShader::Type::Lighting; };
	virtual bool AffectsCachedShader(RE::BSShader::Type shaderType, std::uint32_t descriptor, CachedShaderStage) override
	{
		if (shaderType == RE::BSShader::Type::Grass)
			return true;
		if (shaderType != RE::BSShader::Type::Lighting)
			return false;

		// Lighting technique 12 is TREE_ANIM. All other Lighting permutations
		// preprocess the FoliageDynamics implementation away.
		return ((descriptor >> 24u) & 0x3Fu) == 12u;
	}
	virtual std::string_view GetCategory() const override { return ModuleGroups::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.foliage_dynamics.description", "Foliage Dynamics enhances grass rendering with improved lighting, specularity, and subsurface scattering.\nThis makes grass appear more natural and responsive to lighting conditions."),
			{ T("feature.foliage_dynamics.key_feature_1", "Enhanced grass lighting model"),
				T("feature.foliage_dynamics.key_feature_2", "Specular highlights on grass"),
				T("feature.foliage_dynamics.key_feature_3", "Subsurface scattering effects"),
				T("feature.foliage_dynamics.key_feature_4", "Improved grass visual quality"),
				T("feature.foliage_dynamics.key_feature_5", "Configurable material properties") } };
	};

	struct alignas(16) Settings
	{
		float Glossiness = 20.0f;
		float SpecularStrength = 0.5f;
		float TissueDiffusionAmount = 1.0f;
		uint OverrideComplexGrassSettings = false;
		float BasicGrassBrightness = 1.0f;
		float ComplexGrassThreshold = 0.03f;
		uint TreeFlipNormalY = false;  // c1.z, replaces padding; no ABI growth
		float GrassMacroSpecular = 0.85f;  // c1.w, replaces padding; 0=detail normal, 1=card plane

		uint EnableEnhancedVegetation = true;
		uint EnableEnhancedWind = true;
		float LeafTransmission = 0.8f;
		float LeafDiffuseWrap = 0.35f;

		float WindStrength = 1.0f;
		float GustStrength = 0.35f;
		float FlutterStrength = 0.22f;
		float WindSpatialScale = 1.0f;

		float GustSpeed = 1.0f;
		float FlutterSpeed = 1.0f;
		float SpecularAA = 0.65f;
		// 0 Auto Safe, 1 Basic/Vanilla, 2 Auto layout + DX Y, 3 Auto layout + Flip-Y.
		uint ComplexGrassMode = 0;  // c4.w; replaces padding without changing FeatureData ABI
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 80, "FoliageDynamics::Settings must remain 80 bytes.");

	Settings settings;

	// Dedicated grass-material tuning buffer. This deliberately stays outside
	// renderer-wide FeatureData so FoliageDynamics::Settings remains the stable
	// 80-byte ABI block.
	static constexpr uint TuningMagic = 0x50464754u;  // "PFGT"
	static constexpr uint TuningVersion = 4u;

	struct alignas(16) TuningSettings
	{
		// c0
		uint Magic = TuningMagic;
		uint Version = TuningVersion;
		uint EnableGrassAlphaControl = 0;
		uint GrassFlipNormalX = 0;

		// c1
		uint GrassFlipNormalY = 0;
		float GrassNormalStrength = 1.0f;
		float GrassCardNormalBlend = 0.0f;
		float GrassAlphaCoverage = 1.0f;

		// c2
		float GrassCutoutBias = 0.0f;
		float GrassAlphaPower = 1.0f;
		float GrassEdgeDither = 0.0f;
		float GrassSaturation = 1.0f;

		// c3
		float GrassContrast = 1.0f;
		float GrassWetSpecularBoost = 1.0f;
		float GrassTransmissionBoost = 1.0f;
		float GrassLocalLightBoost = 1.0f;

		// c4 -- dedicated b13 extension; FeatureData b6 remains unchanged.
		float GrassDetailDistanceScale = 1.35f;
		float GrassDetailTransitionSoftness = 1.0f;
		float GrassSpecularNormalization = 1.0f;
		float GrassComplexSpecularMapInfluence = 0.15f;

		// c5 -- complex-grass specular-only mirrored tangent-Y lobe.
		uint GrassMirrorSpecularY = 0;
		uint GrassTuningPadding0 = 0;
		uint GrassTuningPadding1 = 0;
		uint GrassTuningPadding2 = 0;
	};
	STATIC_ASSERT_ALIGNAS_16(TuningSettings);
	static_assert(sizeof(TuningSettings) == 96, "FoliageDynamics::TuningSettings must match PS b13.");

	TuningSettings tuningSettings;
	ConstantBuffer* tuningCB = nullptr;

	virtual void SetupResources() override;
	virtual void Prepass() override;

	/**
	 * @brief Rebinds the grass-only PS b13 payload at the BSGrassShader draw boundary.
	 *
	 * PS b13 is intentionally shared by several mutually exclusive shader families.
	 * Frame-level bindings from Rain/Ground Response may therefore replace it before
	 * grass draws. GroundResponse's existing BSGrassShader hook calls this after
	 * vanilla geometry setup so every FOLIAGE_DYNAMICS grass draw receives the
	 * current tuning values without changing the public FeatureData ABI.
	 */
	void BindGrassTuning() const;

	/** @brief Draws the ImGui settings UI for grass specular, SSS, and lighting options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

};
