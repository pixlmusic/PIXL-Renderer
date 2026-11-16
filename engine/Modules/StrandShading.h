#pragma once

struct StrandShading : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "149011";

public:
	virtual inline std::string GetName() override { return "Strand Shading"; }
	virtual std::string GetDisplayName() override { return T("feature.strand_shading.name", "Strand Shading"); }
	virtual inline std::string GetShortName() override { return "StrandShading"; }
	// The module is intentionally disabled at boot in the public defaults. Keep
	// its cache version visible anyway so a version bump can invalidate only Hair
	// pixel permutations without enabling strand shading or Hair Reconstruction.
	virtual std::string GetCacheVersion() const override
	{
		return installed ? RenderModule::GetModuleRequiredVersion("StrandShading") : version;
	}
	virtual inline std::string_view GetShaderDefineName() override { return "STRAND_SHADING"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kCharacters; }
	virtual bool AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage a_stage) override
	{
		if (a_type != RE::BSShader::Type::Lighting || a_stage != CachedShaderStage::Pixel)
			return false;
		constexpr std::uint32_t kTechniqueMask = 0x3Fu << 24u;
		constexpr std::uint32_t kHairTechnique = 6u << 24u;
		return (a_descriptor & kTechniqueMask) == kHairTechnique;
	}
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.strand_shading.description", "Provides better hair shading with realistic specular highlights and tangent-based light interaction for more lifelike hair appearance."),
			{ T("feature.strand_shading.key_feature_1", "Realistic hair specular highlights"),
				T("feature.strand_shading.key_feature_2", "Enhanced hair glossiness and saturation controls"),
				T("feature.strand_shading.key_feature_3", "Separate specular and diffuse lighting multipliers"),
				T("feature.strand_shading.key_feature_4", "Tangent shift texture support for varied hair highlights") } };
	};

	/** @brief Returns true only for Lighting shader type. */
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; };

	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }

	/** @brief Binds the tangent shift texture as a pixel shader resource. */
	virtual void Prepass() override;

	/** @brief Loads the tangent shift DDS texture from disk and creates its SRV. */
	virtual void SetupResources() override;

	struct alignas(16) Settings
	{
		uint Enabled = true;
		float HairGlossiness = 70.0f;
		float SpecularMult = 1.0f;
		float DiffuseMult = 1.0f;
		uint EnableTangentShift = true;
		float PrimaryTangentShift = 0.5f;
		float SecondaryTangentShift = -0.25f;
		float HairSaturation = 1.0f;
		float SpecularIndirectMult = 1.0f;
		float DiffuseIndirectMult = 1.0f;
		float BaseColorMult = 1.5f;
		float Transmission = 1.0f;
		uint EnableSelfShadow = true;
		float SelfShadowStrength = 1.0f;
		float SelfShadowExponent = 0.1f;
		float SelfShadowScale = 2.5f;
		uint HairMode = 1;  // 0: Kajiya-Kay, 1: Marschner
		uint pad[3];
	} settings;

	eastl::unique_ptr<Texture2D> texTangentShift = nullptr;

	/** @brief Draws the ImGui settings UI for hair shading model, specular, and self-shadow options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

};
