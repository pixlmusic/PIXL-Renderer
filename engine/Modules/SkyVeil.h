#pragma once

struct SkyVeil : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "139185";

public:
	static constexpr int kMaxCloudLayers = 32;

	struct alignas(16) Settings
	{
		float Opacity = 0.5f;
		uint32_t EnableVolumetricClouds = true;
		float CloudDensity = 1.0f;
		float CloudDepth = 0.45f;

		float SelfShadowStrength = 0.7f;
		float SilverLining = 0.65f;
		float AmbientLighting = 0.22f;
		float DetailStrength = 0.45f;

		float PhaseEccentricity = 0.62f;
		float HorizonFade = 0.35f;
		float pad[2];
	};

	Settings settings;

	virtual inline std::string GetName() override { return "Sky Veil"; }
	virtual std::string GetDisplayName() override { return T("feature.sky_veil.name", "Sky Veil"); }
	virtual inline std::string GetShortName() override { return "SkyVeil"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kSky; }
	virtual inline std::string_view GetShaderDefineName() override { return "SKY_VEIL"; }
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.sky_veil.description", "Adds realistic cloud shadows that move across the landscape, creating dynamic lighting changes as clouds pass overhead, enhancing atmospheric immersion."),
			{ T("feature.sky_veil.key_feature_1", "Dynamic cloud shadow projection on terrain and objects"),
				T("feature.sky_veil.key_feature_2", "Configurable shadow opacity for artistic control"),
				T("feature.sky_veil.key_feature_3", "Real-time shadow movement synchronized with cloud motion"),
				T("feature.sky_veil.key_feature_4", "Cubemap-based shadow calculation for accurate projection"),
				T("feature.sky_veil.key_feature_5", "Enhanced sky rendering integration") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	bool overrideSky = false;
	/**
	 * @brief Applies sky shader render state overrides for cloud shadow capture.
	 *
	 * When overrideSky is set, redirects rendering to the cloud occlusion cubemap
	 * and configures the appropriate blend state and depth resources.
	 */
	void SkyShaderHacks();

	Texture2D* texCloudShadowLayers[kMaxCloudLayers] = {};
	ID3D11RenderTargetView* cloudShadowLayerRTVs[kMaxCloudLayers][6] = {};
	Texture2D* texCubemapCloudOccCopy = nullptr;
	Texture2D* texSelfShadowCopy = nullptr;

	UINT cubemapMipLevels = 1;
	int currentLayerForDraw = 0;

	uint32_t renderedLayersMask[6] = {};
	uint32_t globalRenderedMask = 0;
	int previouslyRenderedSide = -1;

	ID3D11BlendState* cloudShadowBlendState = nullptr;

	/** @brief Creates cubemap textures, SRVs, RTVs, and blend state for cloud shadow rendering. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for cloud shadow opacity. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	Settings GetCommonBufferData();

	/**
	 * @brief Clears the cloud occlusion render target for a given cubemap face if not yet cleared this frame.
	 * @param side Cubemap face index (0-5).
	 */
	void CheckResourcesSide(int side);
	void PropagateToCompletion(int side);
	/**
	 * @brief Checks if the current sky render pass is rendering clouds to the reflections cubemap and flags it for override.
	 * @param Pass The BSRenderPass being set up for rendering.
	 */
	int FindCloudLayer(RE::BSRenderPass* Pass);
	void ModifySky(RE::BSRenderPass* Pass);

	/** @brief Copies the cloud occlusion cubemap and binds it as a shader resource for the reflections prepass. */
	virtual void ReflectionsPrepass() override;
	/** @brief Binds the cloud occlusion cubemap as a shader resource for the early prepass. */
	virtual void EarlyPrepass() override;

	/** @brief Installs the BSSkyShader hooks after all plugins have loaded. */
	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		struct BSSkyShader_SetupMaterial
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSSkyShader_SetupMaterial>(RE::VTABLE_BSSkyShader[0]);
			logger::info("[Sky Veil] Installed hooks");
		}
	};
};
