#pragma once

/**
 * @brief Automatic runtime reconstruction for vanilla and conventionally-authored hair.
 *
 * The module deliberately augments StrandShading instead of replacing it.  It owns
 * conservative material classification, inferred fibre direction, restrained
 * deterministic motion and environmental response; StrandShading remains the
 * authoritative hair scattering implementation.
 */
struct HairReconstruction : RenderModule
{
public:
	virtual inline std::string GetName() override { return "Hair Reconstruction"; }
	virtual std::string GetDisplayName() override { return T("feature.hair_reconstruction.name", "Hair Reconstruction"); }
	virtual inline std::string GetShortName() override { return "HairReconstruction"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HAIR_RECONSTRUCTION"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kCharacters; }
	virtual bool HasShaderDefine(RE::BSShader::Type a_type) override { return a_type == RE::BSShader::Type::Lighting; }
	virtual bool AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage) override
	{
		if (a_type != RE::BSShader::Type::Lighting)
			return false;
		constexpr std::uint32_t kTechniqueMask = 0x3Fu << 24u;
		constexpr std::uint32_t kHairTechnique = 6u << 24u;
		constexpr std::uint32_t kAutoHairFlags = (1u << 6u) | (1u << 7u);
		return (a_descriptor & kTechniqueMask) == kHairTechnique || (a_descriptor & kAutoHairFlags) != 0u;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return {
			T("feature.hair_reconstruction.description", "Automatically interprets Skyrim hair cards and upgrades their direction, movement and environmental material response without authored groom data."),
			{
				T("feature.hair_reconstruction.key_feature_1", "Conservative vanilla and mod-hair detection"),
				T("feature.hair_reconstruction.key_feature_2", "Stable UV and geometry-derived fibre direction"),
				T("feature.hair_reconstruction.key_feature_3", "World-space wind and actor-motion response with matching history"),
				T("feature.hair_reconstruction.key_feature_4", "Rain-aware absorption, clumping and movement"),
				T("feature.hair_reconstruction.key_feature_5", "Safe material-only fallback for uncertain geometry")
			}
		};
	}

	struct alignas(16) Settings
	{
		// c0 - feature and conservative classifier thresholds
		uint Enabled = true;
		uint Quality = 1;  // 0 Low, 1 Medium, 2 High, 3 Ultra
		float DetectionThreshold = 0.82f;
		float ReconstructionThreshold = 0.92f;

		// c1 - appearance and direction reconstruction
		uint AnisotropicLighting = true;
		float DirectionBlend = 0.78f;
		float StrandDetail = 0.35f;
		float Transmission = 1.0f;

		// c2 - deterministic secondary motion
		uint SecondaryMotion = true;
		float WindResponse = 0.30f;
		float MotionStrength = 0.14f;
		float Damping = 0.82f;

		// c3 - rain/water response
		uint WetHair = true;
		float WetDarkening = 0.16f;
		float WetRoughness = 0.22f;
		float WetWeight = 0.35f;

		// c4 - high-quality virtual fibres and snow participation
		uint SnowResponse = true;
		uint ProceduralStrands = false;
		float StrandDensity = 0.20f;
		float SilhouetteDetail = 0.06f;

		// c5 - runtime bounds and diagnostics
		float SimulationDistance = 3000.0f;
		uint DebugMode = 0;
		float FrameDelta = 1.0f / 60.0f;
		uint Padding0 = 0;

		// c6-c7 - consume the historical 128-byte post-process reservation exactly.
		// These registers are deliberately inert and preserve every following shared
		// FeatureData offset for shaders compiled before Hair Reconstruction existed.
		uint Reserved0[4]{};
		uint Reserved1[4]{};
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);
	static_assert(sizeof(Settings) == 128, "HairReconstruction::Settings must replace the reserved FeatureData block exactly.");

	Settings settings;

	/** Returns a frame-safe copy so current/previous procedural motion share the real frame delta. */
	[[nodiscard]] Settings GetCommonBufferData() const;

	/** Applies workload-only defaults while preserving the renderer's authored hair colour/BRDF controls. */
	void ApplyQualityTier(std::uint32_t a_quality);

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	virtual void RestoreDefaultSettings() override;
};
