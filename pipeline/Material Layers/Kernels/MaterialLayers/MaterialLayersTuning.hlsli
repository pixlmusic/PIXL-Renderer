#ifndef PIXL_MATERIAL_LAYERS_TUNING_HLSLI
#define PIXL_MATERIAL_LAYERS_TUNING_HLSLI

// Dedicated material tuning buffer. Keeping this at b9 avoids changing the
// renderer-wide FeatureData ABI that must remain stable.
cbuffer PIXLMaterialLayersTuningCB : register(b9)
{
	// c0
	uint PIXLML_Magic;
	uint PIXLML_Version;
	uint PIXLML_EnableObjectAutoPOM;
	uint PIXLML_EnableTerrainAutoPOM;

	// c1
	uint PIXLML_TerrainHeightMode;
	uint PIXLML_EnableAutoPOMSelfShadows;
	uint PIXLML_EnableTerrainSelfShadows;
	uint PIXLML_EnableDetailReconstruction;

	// c2
	float PIXLML_ObjectAuthoredDepthScale;
	float PIXLML_ObjectAutoHeightScale;
	float PIXLML_ObjectMaxTexelShift;
	float PIXLML_ObjectGrazingProtection;

	// c3
	float PIXLML_ObjectFadeStart;
	float PIXLML_ObjectFadeEnd;
	float PIXLML_ObjectMaxMip;
	float PIXLML_AutoMinTexelShift;

	// c4
	uint PIXLML_ObjectNearSteps;
	uint PIXLML_ObjectMaxSteps;
	uint PIXLML_ObjectRefinementSteps;
	uint PIXLML_pad0;

	// c5
	float PIXLML_AutoHeightContrast;
	float PIXLML_AutoHeightNormalInfluence;
	float PIXLML_AutoHeightReferenceMipOffset;
	float PIXLML_AutoHeightChromaRejection;

	// c6
	float PIXLML_ObjectShadowStrength;
	float PIXLML_ObjectShadowBias;
	float PIXLML_ObjectShadowRayScale;
	float PIXLML_pad1;

	// c7
	float PIXLML_TerrainDepthScale;
	float PIXLML_TerrainMaxTexelShift;
	float PIXLML_TerrainGrazingProtection;
	float PIXLML_TerrainHeightStrength;

	// c8
	float PIXLML_TerrainFadeStart;
	float PIXLML_TerrainFadeEnd;
	float PIXLML_TerrainMaxMip;
	float PIXLML_TerrainHeightContrast;

	// c9
	uint PIXLML_TerrainNearSteps;
	uint PIXLML_TerrainMaxSteps;
	uint PIXLML_TerrainRefinementSteps;
	uint PIXLML_pad2;

	// c10
	float PIXLML_TerrainReferenceMipOffset;
	float PIXLML_TerrainHeightBlendStrength;
	float PIXLML_TerrainShadowStrength;
	float PIXLML_TerrainShadowBias;

	// c11
	float PIXLML_TerrainShadowRayScale;
	float PIXLML_DetailObjectStrength;
	float PIXLML_DetailTerrainStrength;
	float PIXLML_DetailAlbedoStrength;

	// c12
	float PIXLML_DetailNormalStrength;
	float PIXLML_DetailRoughnessStrength;
	float PIXLML_DetailMipSeparation;
	float PIXLML_DetailContrast;

	// c13
	float PIXLML_DetailAntiShimmer;
	float PIXLML_DetailFadeStart;
	float PIXLML_DetailFadeEnd;
	float PIXLML_DetailMaxMip;

	// c14
	float PIXLML_DetailDarkProtection;
	float PIXLML_DetailSmoothProtection;
	float PIXLML_DetailDominantTerrainMinWeight;
	uint PIXLML_DetailQuality;

	// c15
	float PIXLML_TerrainSyntheticPolarity;
	float PIXLML_TerrainSourceMipBias;
	float PIXLML_TerrainHeightDeadZone;
	float PIXLML_TerrainAlphaAssist;

	// c16
	float PIXLML_TerrainAlphaEvidenceThreshold;
	float PIXLML_TerrainSyntheticScaleFloor;
	float PIXLML_TerrainVirtualDepthStrength;
	float PIXLML_TerrainVirtualDepthMaxWorld;

	// c17
	float PIXLML_TerrainVirtualDepthMaxUV;
	float PIXLML_TerrainVirtualDepthProtrusion;
	uint PIXLML_EnableTerrainVirtualDepth;
	uint PIXLML_TerrainHeightDebugMode;

	// c18
	float PIXLML_TerrainReliefGamma;
	float PIXLML_TerrainSyntheticGain;
	float PIXLML_pad3;
	float PIXLML_pad4;
};

namespace MaterialLayersTuning
{
	static const uint Magic = 0x504D4C54u;
	static const uint Version = 2u;

	bool IsValid()
	{
		return PIXLML_Magic == Magic && PIXLML_Version == Version;
	}

	uint TerrainHeightMode()
	{
		if (IsValid())
			return min(PIXLML_TerrainHeightMode, 3u);
		// StableABI fallback: the legacy bool meant diffuse-alpha terrain.
		return SharedData::materialLayerSettings.EnableTerrainParallax ? 3u : 1u;
	}

	bool ObjectAutoPOMEnabled()
	{
		// Preserve the pre-v3.12 object Auto-POM behavior if b9 is unavailable.
		return !IsValid() || PIXLML_EnableObjectAutoPOM != 0u;
	}

	bool TerrainAutoPOMEnabled()
	{
		return IsValid() && PIXLML_EnableTerrainAutoPOM != 0u;
	}

	bool AutoPOMSelfShadowsEnabled()
	{
		return IsValid() && PIXLML_EnableAutoPOMSelfShadows != 0u;
	}

	bool TerrainSelfShadowsEnabled()
	{
		return !IsValid() || PIXLML_EnableTerrainSelfShadows != 0u;
	}

	bool DetailReconstructionEnabled()
	{
		return IsValid() && PIXLML_EnableDetailReconstruction != 0u && PIXLML_DetailQuality != 0u;
	}

	uint DetailQuality()
	{
		return IsValid() ? min(PIXLML_DetailQuality, 2u) : 0u;
	}

	float ObjectAuthoredDepthScale() { return IsValid() ? clamp(PIXLML_ObjectAuthoredDepthScale, 0.05f, 4.0f) : 1.0f; }
	float ObjectAutoHeightScale() { return IsValid() ? clamp(PIXLML_ObjectAutoHeightScale, 0.0005f, 0.08f) : 0.014f; }
	float ObjectMaxTexelShift() { return IsValid() ? clamp(PIXLML_ObjectMaxTexelShift, 1.0f, 64.0f) : 12.0f; }
	float ObjectGrazingProtection() { return IsValid() ? saturate(PIXLML_ObjectGrazingProtection) : 0.72f; }
	float ObjectFadeStart() { return IsValid() ? max(PIXLML_ObjectFadeStart, 0.0f) : 768.0f; }
	float ObjectFadeEnd() { return IsValid() ? max(PIXLML_ObjectFadeEnd, ObjectFadeStart() + 1.0f) : 1792.0f; }
	float ObjectMaxMip() { return IsValid() ? clamp(PIXLML_ObjectMaxMip, 1.0f, 12.0f) : 6.0f; }
	float AutoMinTexelShift() { return IsValid() ? max(PIXLML_AutoMinTexelShift, 0.0f) : 0.25f; }
	uint ObjectNearSteps() { return IsValid() ? clamp(PIXLML_ObjectNearSteps, 4u, 24u) : 8u; }
	uint ObjectMaxSteps() { return IsValid() ? clamp(PIXLML_ObjectMaxSteps, 4u, 32u) : 16u; }
	uint ObjectRefinementSteps() { return IsValid() ? clamp(PIXLML_ObjectRefinementSteps, 4u, 12u) : 4u; }
	float AutoHeightContrast() { return IsValid() ? clamp(PIXLML_AutoHeightContrast, 0.1f, 4.0f) : 1.35f; }
	float AutoHeightNormalInfluence() { return IsValid() ? clamp(PIXLML_AutoHeightNormalInfluence, 0.0f, 2.0f) : 1.0f; }
	float AutoHeightReferenceMipOffset() { return IsValid() ? clamp(PIXLML_AutoHeightReferenceMipOffset, 1.0f, 8.0f) : 4.0f; }
	float AutoHeightChromaRejection() { return IsValid() ? clamp(PIXLML_AutoHeightChromaRejection, 0.0f, 4.0f) : 0.60f; }
	float ObjectShadowStrength() { return IsValid() ? max(PIXLML_ObjectShadowStrength, 0.0f) : 0.58f; }
	float ObjectShadowBias() { return IsValid() ? max(PIXLML_ObjectShadowBias, 0.1f) : 1.0f; }
	float ObjectShadowRayScale() { return IsValid() ? max(PIXLML_ObjectShadowRayScale, 0.0f) : 1.0f; }

	float TerrainDepthScale() { return IsValid() ? clamp(PIXLML_TerrainDepthScale, 0.05f, 10.0f) : 1.0f; }
	float TerrainMaxTexelShift() { return IsValid() ? clamp(PIXLML_TerrainMaxTexelShift, 2.0f, 512.0f) : 32.0f; }
	float TerrainGrazingProtection() { return IsValid() ? saturate(PIXLML_TerrainGrazingProtection) : 0.45f; }
	float TerrainHeightStrength() { return IsValid() ? clamp(PIXLML_TerrainHeightStrength, 0.0f, 6.0f) : 1.0f; }
	float TerrainFadeStart() { return IsValid() ? max(PIXLML_TerrainFadeStart, 0.0f) : 1024.0f; }
	float TerrainFadeEnd() { return IsValid() ? max(PIXLML_TerrainFadeEnd, TerrainFadeStart() + 1.0f) : 2304.0f; }
	float TerrainMaxMip() { return IsValid() ? clamp(PIXLML_TerrainMaxMip, 1.0f, 12.0f) : 6.0f; }
	float TerrainHeightContrast() { return IsValid() ? clamp(PIXLML_TerrainHeightContrast, 0.1f, 8.0f) : 1.35f; }
	uint TerrainNearSteps() { return IsValid() ? clamp(PIXLML_TerrainNearSteps, 4u, 32u) : 12u; }
	uint TerrainMaxSteps() { return IsValid() ? clamp(PIXLML_TerrainMaxSteps, 4u, 64u) : 20u; }
	uint TerrainRefinementSteps() { return IsValid() ? clamp(PIXLML_TerrainRefinementSteps, 4u, 16u) : 4u; }
	float TerrainReferenceMipOffset() { return IsValid() ? clamp(PIXLML_TerrainReferenceMipOffset, 1.0f, 8.0f) : 4.0f; }
	float TerrainHeightBlendStrength() { return IsValid() ? clamp(PIXLML_TerrainHeightBlendStrength, 0.0f, 3.0f) : 1.0f; }
	float TerrainShadowStrength() { return IsValid() ? max(PIXLML_TerrainShadowStrength, 0.0f) : 0.75f; }
	float TerrainShadowBias() { return IsValid() ? max(PIXLML_TerrainShadowBias, 0.1f) : 1.0f; }
	float TerrainShadowRayScale() { return IsValid() ? max(PIXLML_TerrainShadowRayScale, 0.0f) : 1.0f; }
	float TerrainSyntheticPolarity() { return IsValid() ? (PIXLML_TerrainSyntheticPolarity >= 0.0f ? 1.0f : -1.0f) : -1.0f; }
	float TerrainSourceMipBias() { return IsValid() ? clamp(PIXLML_TerrainSourceMipBias, 0.0f, 4.0f) : 1.25f; }
	float TerrainHeightDeadZone() { return IsValid() ? clamp(PIXLML_TerrainHeightDeadZone, 0.0f, 0.20f) : 0.025f; }
	float TerrainAlphaAssist() { return IsValid() ? saturate(PIXLML_TerrainAlphaAssist) : 0.90f; }
	float TerrainAlphaEvidenceThreshold() { return IsValid() ? clamp(PIXLML_TerrainAlphaEvidenceThreshold, 0.001f, 0.25f) : 0.012f; }
	float TerrainSyntheticScaleFloor() { return IsValid() ? clamp(PIXLML_TerrainSyntheticScaleFloor, 0.05f, 8.0f) : 1.0f; }
	bool TerrainVirtualDepthEnabled() { return IsValid() && PIXLML_EnableTerrainVirtualDepth != 0u; }
	float TerrainVirtualDepthStrength() { return IsValid() ? clamp(PIXLML_TerrainVirtualDepthStrength, 0.0f, 3.0f) : 0.0f; }
	float TerrainVirtualDepthMaxWorld() { return IsValid() ? clamp(PIXLML_TerrainVirtualDepthMaxWorld, 1.0f, 128.0f) : 16.0f; }
	float TerrainVirtualDepthMaxUV() { return IsValid() ? clamp(PIXLML_TerrainVirtualDepthMaxUV, 0.01f, 1.0f) : 0.12f; }
	float TerrainVirtualDepthProtrusion() { return IsValid() ? clamp(PIXLML_TerrainVirtualDepthProtrusion, 0.0f, 0.75f) : 0.0f; }
	uint TerrainHeightDebugMode() { return IsValid() ? min(PIXLML_TerrainHeightDebugMode, 3u) : 0u; }
	float TerrainReliefGamma() { return IsValid() ? clamp(PIXLML_TerrainReliefGamma, 0.25f, 3.0f) : 1.0f; }
	float TerrainSyntheticGain() { return IsValid() ? clamp(PIXLML_TerrainSyntheticGain, 0.1f, 6.0f) : 1.0f; }

	float DetailObjectStrength() { return IsValid() ? max(PIXLML_DetailObjectStrength, 0.0f) : 0.0f; }
	float DetailTerrainStrength() { return IsValid() ? max(PIXLML_DetailTerrainStrength, 0.0f) : 0.0f; }
	float DetailAlbedoStrength() { return IsValid() ? max(PIXLML_DetailAlbedoStrength, 0.0f) : 0.0f; }
	float DetailNormalStrength() { return IsValid() ? max(PIXLML_DetailNormalStrength, 0.0f) : 0.0f; }
	float DetailRoughnessStrength() { return IsValid() ? max(PIXLML_DetailRoughnessStrength, 0.0f) : 0.0f; }
	float DetailMipSeparation() { return IsValid() ? clamp(PIXLML_DetailMipSeparation, 0.25f, 6.0f) : 1.5f; }
	float DetailContrast() { return IsValid() ? clamp(PIXLML_DetailContrast, 0.0f, 4.0f) : 1.0f; }
	float DetailAntiShimmer() { return IsValid() ? saturate(PIXLML_DetailAntiShimmer) : 0.65f; }
	float DetailFadeStart() { return IsValid() ? max(PIXLML_DetailFadeStart, 0.0f) : 384.0f; }
	float DetailFadeEnd() { return IsValid() ? max(PIXLML_DetailFadeEnd, DetailFadeStart() + 1.0f) : 1536.0f; }
	float DetailMaxMip() { return IsValid() ? clamp(PIXLML_DetailMaxMip, 1.0f, 10.0f) : 5.0f; }
	float DetailDarkProtection() { return IsValid() ? saturate(PIXLML_DetailDarkProtection) : 0.5f; }
	float DetailSmoothProtection() { return IsValid() ? saturate(PIXLML_DetailSmoothProtection) : 0.5f; }
	float DetailDominantTerrainMinWeight() { return IsValid() ? clamp(PIXLML_DetailDominantTerrainMinWeight, 0.25f, 0.98f) : 0.65f; }
}

#endif
