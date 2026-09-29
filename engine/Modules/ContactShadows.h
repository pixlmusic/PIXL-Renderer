// Community Shaders Screen-Space Shadows-derived file.
// Modified for PIXL Renderer, 2026: Directional SSS runtime interface.
// SPDX-License-Identifier: GPL-3.0-or-later
// Third-party Bend/Sony code keeps its separate notices in the adjacent files.

#pragma once

#include "Buffer.h"

struct ContactShadows : RenderModule
{
public:
	virtual inline std::string GetName() override { return "Contact Shadows"; }
	// Keep GetName()/GetShortName() stable: they are persisted in existing profiles/cache keys.
	virtual std::string GetDisplayName() override { return T("feature.contact_shadows.name", "Directional SSS"); }
	virtual inline std::string GetShortName() override { return "ContactShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CONTACT_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kLighting; }

	/** @brief Returns a localized description and list of key features for the UI summary panel. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.contact_shadows.description", "Directional SSS refines Skyrim's active sun or moon shadow with depth-aware screen-space ray marching. It follows the active directional source every frame. PBR Local Contact Shadows remains the separate control for point and clustered lights."),
			{ T("feature.contact_shadows.key_feature_1", "Active sun and moon source tracking"),
				T("feature.contact_shadows.key_feature_2", "Fine directional contact detail"),
				T("feature.contact_shadows.key_feature_3", "Depth-aware edge filtering"),
				T("feature.contact_shadows.key_feature_4", "Separate from local-light contact shadows"),
				T("feature.contact_shadows.key_feature_5", "Resolution-scaled ray quality") } };
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;
	bool AffectsCachedShader(
		RE::BSShader::Type shaderType,
		std::uint32_t,
		CachedShaderStage stage) override
	{
		// CONTACT_SHADOWS is consumed by the Lighting, Grass, and DistantTree
		// pixel shaders. The direct Bend raymarch kernel is module-owned and
		// released independently by ClearShaderCache().
		const bool supportedType =
			shaderType == RE::BSShader::Type::Lighting ||
			shaderType == RE::BSShader::Type::Grass ||
			shaderType == RE::BSShader::Type::DistantTree;
		return supportedType && stage == CachedShaderStage::Pixel;
	}

	struct BendSettings
	{
		float SurfaceThickness = 0.02f;
		float BilinearThreshold = 0.02f;
		float ShadowContrast = 1.0f;
		uint Enable = 1;
		uint SampleCount = 1;
		float Strength = 1.0f;
		uint pad0[2];
	};
	static_assert(sizeof(BendSettings) == 32, "ContactShadows::BendSettings must match RaymarchCS b1 exactly.");

	BendSettings bendSettings;

	struct alignas(16) RaymarchCB
	{
		// Runtime data returned from BuildDispatchList():
		float LightCoordinate[4];  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
		int WaveOffset[2];         // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

		// Renderer Specific Values:
		float FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
		float NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

		// Sampling data:
		float InvDepthTextureSize[2];  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
									   // If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
									   // The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

		float2 DynamicRes;

		BendSettings settings;
	};
	STATIC_ASSERT_ALIGNAS_16(RaymarchCB);

	ID3D11SamplerState* pointBorderSampler = nullptr;

	ConstantBuffer* raymarchCB = nullptr;
	ID3D11ComputeShader* raymarchCS = nullptr;

	Texture2D* contactShadowsTexture = nullptr;

	/** @brief Creates the raymarch constant buffer, point border sampler, and shadow output texture. */
	virtual void SetupResources() override;

	/** @brief Draws the ImGui settings UI for screen-space shadow configuration. */
	virtual void DrawSettings() override;

	/** @brief Releases the compiled raymarch compute shader for recompilation. */
	virtual void ClearShaderCache() override;
	/** @brief Releases the raymarch compute shader so it is recompiled on next use. */
	void InvalidateRaymarchShaders();
	/** @brief Calculates the resolution-scaled and quantized sample count for the raymarch shader. */
	uint GetScaledSampleCount();
	uint lastCompiledSampleCount = 0;
	/**
	 * @brief Returns the compiled raymarch compute shader, recompiling if the sample count changed.
	 * @return The compiled ID3D11ComputeShader, or nullptr on failure.
	 */
	ID3D11ComputeShader* GetComputeRaymarch();

	/** @brief Clears the shadow texture and dispatches shadow ray marching if conditions are met. */
	virtual void Prepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	/** @brief Dispatches the Bend SSS compute shader to generate screen-space contact shadows. */
	void DrawShadows();

	// The directional light is reacquired from Skyrim's active shadow scene every prepass.
	// This cached pointer exists only to report source transitions; it never supplies render data.
	void UpdateDirectionalSource(const void* source);
	const void* activeDirectionalSource = nullptr;
	std::uint64_t directionalSourceEpoch = 0;

	virtual void Reset() override;
	virtual void RestoreDefaultSettings() override;

};
