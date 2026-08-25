#pragma once

#include "Buffer.h"

#define SSSS_N_SAMPLES 21

/** @brief Simulates light penetration through translucent materials like skin using screen-space blur techniques. */
struct TissueDiffusion : RenderModule
{
public:
	struct DiffusionProfile
	{
		float BlurRadius;
		float Thickness;
		float3 Strength;
		float3 Falloff;
	};

	enum ScatterMode : int
	{
		kPreScatter = 0,
		kPostScatter = 1,
		kPreAndPostScatter = 2,
	};

	struct Settings
	{
		uint EnableCharacterLighting = false;
		float CharacterLightingStrength = 1.0f;
		int SSMode = 1;
		int ScatterMode = kPreAndPostScatter;
		DiffusionProfile BaseProfile{ 1.0f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 0.56f, 0.56f, 0.56f } };
		DiffusionProfile HumanProfile{ 1.0f, 1.0f, { 0.48f, 0.41f, 0.28f }, { 1.0f, 0.37f, 0.3f } };
		uint BurleySamples = 16;
		float4 MeanFreePathBase = { 0.56f, 0.56f, 0.56f, 2.67f };
		float4 MeanFreePathHuman = { 1.0f, 0.37f, 0.3f, 2.67f };
	};

	Settings settings;

	float CharacterLightingStrengthOriginal = -1.0f;

	struct alignas(16) Kernel
	{
		float4 Sample[SSSS_N_SAMPLES];
	};
	STATIC_ASSERT_ALIGNAS_16(Kernel);

	struct alignas(16) BlurCB
	{
		Kernel BaseKernel;
		Kernel HumanKernel;
		float4 BaseProfile;
		float4 HumanProfile;
		float SSSS_FOVY;
		uint BurleySamples;
		uint ScatterMode;
		uint pad;
		float4 MeanFreePathBase;
		float4 MeanFreePathHuman;
	};
	STATIC_ASSERT_ALIGNAS_16(BlurCB);

	ConstantBuffer* blurCB = nullptr;
	BlurCB blurCBData{};

	bool validMaterial = true;
	bool updateKernels = true;
	bool validMaterials = false;

	Texture2D* blurHorizontalTemp = nullptr;
	Texture2D* diffuseNoAlbedoTex = nullptr;

	ID3D11ComputeShader* prepassSS = nullptr;
	ID3D11ComputeShader* horizontalSSBlur = nullptr;
	ID3D11ComputeShader* verticalSSBlur = nullptr;
	ID3D11ComputeShader* burleySS = nullptr;
	RE::BGSKeyword* isBeastRaceKeyword = nullptr;

	virtual inline std::string GetName() override { return "Tissue Diffusion"; }
	virtual std::string GetDisplayName() override { return T("feature.subsurface_scattering.name", "Tissue Diffusion"); }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TissueDiffusion"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TISSUE_DIFFUSION"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kCharacters; }

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.subsurface_scattering.description", "Tissue Diffusion simulates light penetration through translucent materials like skin, creating more realistic character lighting.\nThis technique makes organic materials appear more lifelike and natural."),
			{ T("feature.subsurface_scattering.key_feature_1", "Realistic skin lighting"),
				T("feature.subsurface_scattering.key_feature_2", "Light penetration simulation"),
				T("feature.subsurface_scattering.key_feature_3", "Separate profiles for different materials"),
				T("feature.subsurface_scattering.key_feature_4", "Enhanced character appearance"),
				T("feature.subsurface_scattering.key_feature_5", "Configurable scattering properties") } };
	};

	// TISSUE_DIFFUSION is consumed by Lighting.hlsl skin permutations only.
	// Advertising it to every shader family made a profile/version change invalidate
	// the entire disk library even though grass, water, effects, sky, and the other
	// families cannot observe this module.
	bool HasShaderDefine(RE::BSShader::Type a_type) override
	{
		return a_type == RE::BSShader::Type::Lighting;
	};
	bool AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t descriptor, CachedShaderStage stage) override
	{
		if (a_type != RE::BSShader::Type::Lighting || stage != CachedShaderStage::Pixel)
			return false;
		const auto technique = (descriptor >> 24u) & 0x3Fu;
		return technique == 4u || technique == 5u;  // FaceGen and FaceGen RGB tint (SKIN)
	}

	/** @brief Creates GPU resources including constant buffers and temporary render textures. */
	virtual void SetupResources() override;
	/** @brief Resets per-frame state including character lighting and kernel recalculation. */
	virtual void Reset() override;
	virtual void RestoreDefaultSettings() override;

	/** @brief Draws the ImGui settings panel for Tissue Diffusion configuration. */
	virtual void DrawSettings() override;

	/**
	 * @brief Evaluates a Gaussian function modulated by a diffusion profile's falloff.
	 * @param a_profile The diffusion profile providing falloff parameters.
	 * @param variance The Gaussian variance.
	 * @param r The radial distance from the sample center.
	 * @return Per-channel Gaussian weights.
	 */
	float3 Gaussian(DiffusionProfile& a_profile, float variance, float r);

	/**
	 * @brief Computes a multi-Gaussian skin diffusion profile at the given distance.
	 * @param a_profile The diffusion profile providing falloff parameters.
	 * @param r The radial distance from the sample center.
	 * @return Per-channel profile weights combining multiple Gaussian terms.
	 */
	float3 Profile(DiffusionProfile& a_profile, float r);

	/**
	 * @brief Precomputes a separable SSS blur kernel from a diffusion profile.
	 * @param a_profile The diffusion profile to generate the kernel from.
	 * @param kernel Output kernel structure populated with sample offsets and weights.
	 */
	void CalculateKernel(DiffusionProfile& a_profile, Kernel& kernel);

	/** @brief Dispatches the SSS blur compute shaders (prepass, horizontal, vertical or Burley). */
	void DrawSSS();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Releases all cached compute shaders for recompilation. */
	virtual void ClearShaderCache() override;
	/** @brief Returns the diffuse extraction prepass compute shader, compiling on first use. */
	ID3D11ComputeShader* GetComputeShaderPrepass();
	/** @brief Returns the horizontal separable SSS blur compute shader, compiling on first use. */
	ID3D11ComputeShader* GetComputeShaderHorizontalBlur();
	/** @brief Returns the vertical separable SSS blur compute shader, compiling on first use. */
	ID3D11ComputeShader* GetComputeShaderVerticalBlur();
	/** @brief Returns the Burley SSS compute shader, compiling on first use. */
	ID3D11ComputeShader* GetComputeShaderBurley();

	/** @brief Looks up the beast race keyword after game data is loaded. */
	virtual void DataLoaded() override;
	/** @brief Installs the BSLightingShader geometry setup hook after plugin load. */
	virtual void PostPostLoad() override;

	/**
	 * @brief Detects face/face-gen materials and flags beast race status for shader permutations.
	 * @param Pass The render pass being set up.
	 */
	void BSLightingShader_SetupSkin(RE::BSRenderPass* Pass);

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			logger::info("[SSS] Installed hooks");
		}
	};

};
