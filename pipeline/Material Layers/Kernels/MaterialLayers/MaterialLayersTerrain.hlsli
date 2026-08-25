// Landscape height sampling and blending (namespace MaterialLayers, LANDSCAPE).

#ifndef MATERIAL_LAYERS_TERRAIN_HLSLI
#define MATERIAL_LAYERS_TERRAIN_HLSLI

	// Shared mip for all six landscape layers from color-map UV derivatives.
	// maxTexDim is for POM step sizing (one GetDimensions).
	void InitializeTerrainMipLevels(float2 coords, out float mipLevels[6], out float maxTexDim)
	{
		float2 textureDims;
		TexColorSampler.GetDimensions(textureDims.x, textureDims.y);
		maxTexDim = max(textureDims.x, textureDims.y);
		float mip = GetMipLevelFromDims(coords, textureDims);
		[unroll] for (uint i = 0; i < 6; i++)
			mipLevels[i] = mip;
	}

	inline float4 TerrainParallaxTexSample(Texture2D tex, float2 uv, float mipLevel, StochasticOffsets sharedOffset, uint layerIndex)
	{
#	if defined(TERRAIN_DETAIL)
		return StochasticEffectParallax(tex, SampTerrainParallaxSampler, uv, mipLevel, sharedOffset);
#	else
		return tex.SampleLevel(SampTerrainParallaxSampler, uv, mipLevel);
#	endif
	}

#	define HEIGHT_POWER 2
#	define HEIGHT_MULT 8

	// Skip layers whose weight × HeightScale is below this fraction of the dominant layer.
	// Only used for linear height blends; vertex weights fade smoothly before the gate trips.
	static const float TERRAIN_LAYER_GATE_EPS = 0.02;

	inline float TerrainMaxWeightedHeightScaleW(float4 w1, float2 w2, DisplacementParams params[6])
	{
		return max(params[0].HeightScale * w1.x, max(params[1].HeightScale * w1.y, max(params[2].HeightScale * w1.z,
																						  max(params[3].HeightScale * w1.w, max(params[4].HeightScale * w2.x, params[5].HeightScale * w2.y)))));
	}

	// Gate for march / secant / shadows (heightBlend ≤ 1). Height-sharpened passes keep every
	// layer so small weights can still rise.
	inline float TerrainLayerGateThreshold(float heightBlend, float4 w1, float2 w2, DisplacementParams params[6])
	{
		return heightBlend <= 1.0 ? TERRAIN_LAYER_GATE_EPS * TerrainMaxWeightedHeightScaleW(w1, w2, params) : 0.0;
	}

	float TerrainWeightedHeightSum(float heights[6], float weights[6])
	{
		float totalHeight = 0;
		[unroll] for (int i = 0; i < 6; i++)
		{
			totalHeight += heights[i] * weights[i];
		}
		return totalHeight;
	}

	// Normalize landscape weights; if heightBlend > 1, sharpen by height.
	// Sharpening is skipped when all heights are nearly equal, so pow() is not applied to
	// vertex weights alone (which hardens triangle borders).
	void ProcessTerrainHeightWeights(float heightBlend, float4 w1, float2 w2, float heights[6], inout float weights[6], out float totalHeight)
	{
		totalHeight = 0.0;
		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;

		float wsum = 0;
		[unroll] for (int j = 0; j < 6; j++)
		{
			wsum += weights[j];
		}
		float invwsum = rcp(max(wsum, 1e-6));
		[unroll] for (int k = 0; k < 6; k++)
		{
			weights[k] *= invwsum;
		}

		bool sharpen = heightBlend > 1.0;
		[branch] if (sharpen)
		{
			float hMin = heights[0];
			float hMax = heights[0];
			[unroll] for (int hi = 1; hi < 6; hi++)
			{
				hMin = min(hMin, heights[hi]);
				hMax = max(hMax, heights[hi]);
			}
			sharpen = (hMax - hMin) > 1e-3;
		}

		[branch] if (sharpen)
		{
			// pow(w * heightBlend^(HEIGHT_MULT * h), heightBlend)
			// = exp2(heightBlend * (log2(w) + HEIGHT_MULT * h * log2(heightBlend)))
			float clampedHeightBlend = max(abs(heightBlend), 0.0001);
			float logHeightBlend = log2(clampedHeightBlend);
			[unroll] for (int hbIdx = 0; hbIdx < 6; hbIdx++)
			{
				float logWeight = log2(max(weights[hbIdx], 1e-6f)) + (HEIGHT_MULT * heights[hbIdx]) * logHeightBlend;
				weights[hbIdx] = min(100, exp2(clampedHeightBlend * logWeight));
			}

			wsum = 0;
			[unroll] for (int k = 0; k < 6; k++)
			{
				wsum += weights[k];
			}
			invwsum = rcp(max(wsum, 1e-6));
			[unroll] for (int l = 0; l < 6; l++)
			{
				weights[l] *= invwsum;
			}
		}

		[unroll] for (int t = 0; t < 6; t++)
		{
			totalHeight += heights[t] * weights[t];
		}
	}

	// Four per-layer height vectors → one float4, matching four GetTerrainHeight calls.
	// weights come from the last UV (tap 3).
	float4 FinishTerrainHeightQuadBlend(float heightBlend, float4 w1, float2 w2,
		float qh0[6], float qh1[6], float qh2[6], float qh3[6], out float weights[6])
	{
		float4 result = 0.0;
		if (heightBlend <= 1.0) {
			float t3 = 0.0;
			ProcessTerrainHeightWeights(heightBlend, w1, w2, qh3, weights, t3);
			result = float4(TerrainWeightedHeightSum(qh0, weights), TerrainWeightedHeightSum(qh1, weights), TerrainWeightedHeightSum(qh2, weights), t3);
		} else {
			float wTmp[6];
			float t0 = 0.0, t1 = 0.0, t2 = 0.0, t3 = 0.0;
			ProcessTerrainHeightWeights(heightBlend, w1, w2, qh0, wTmp, t0);
			ProcessTerrainHeightWeights(heightBlend, w1, w2, qh1, wTmp, t1);
			ProcessTerrainHeightWeights(heightBlend, w1, w2, qh2, wTmp, t2);
			ProcessTerrainHeightWeights(heightBlend, w1, w2, qh3, weights, t3);
			result = float4(t0, t1, t2, t3);
		}
		return result;
	}


	inline float TerrainHeightBlendValue(float blendFactor)
	{
		return 1.0f +
			blendFactor * HEIGHT_POWER *
			MaterialLayersTuning::TerrainHeightBlendStrength();
	}

	inline float TerrainHeightLuminance(float3 color)
	{
		return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
	}

	inline float TerrainSyntheticRGBHeightNormalized(float3 sourceColor, float3 referenceColor)
	{
		float sourceLuma = TerrainHeightLuminance(sourceColor);
		float referenceLuma = TerrainHeightLuminance(referenceColor);
		float rawDelta = sourceLuma - referenceLuma;
		float lumaDelta = rawDelta * MaterialLayersTuning::TerrainSyntheticPolarity();
		float3 chromaResidual = (sourceColor - referenceColor) - rawDelta.xxx;
		float chromaEnergy = dot(chromaResidual, chromaResidual);
		float lumaEnergy = lumaDelta * lumaDelta + 1e-4f;
		float confidence = saturate(lumaEnergy / (lumaEnergy + chromaEnergy * MaterialLayersTuning::AutoHeightChromaRejection()));
		float deadZone = MaterialLayersTuning::TerrainHeightDeadZone();
		float magnitude = max(abs(lumaDelta) - deadZone, 0.0f);
		lumaDelta = lumaDelta < 0.0f ? -magnitude : magnitude;
		confidence = lerp(0.60f, 1.0f, confidence);
		float signedRelief = lumaDelta * MaterialLayersTuning::TerrainHeightContrast() * MaterialLayersTuning::TerrainSyntheticGain() * confidence;
		float signRelief = signedRelief < 0.0f ? -1.0f : 1.0f;
		float shapedMagnitude = pow(saturate(abs(signedRelief) * 2.0f), MaterialLayersTuning::TerrainReliefGamma());
		float h = saturate(0.5f + 0.5f * signRelief * shapedMagnitude);
		h = h * h * (3.0f - 2.0f * h);
		return saturate(0.5f + (h - 0.5f) * MaterialLayersTuning::TerrainHeightStrength());
	}

	inline float TerrainAutoHeightNormalized(float4 sourceColor, float4 referenceColor)
	{
		float syntheticHeight = TerrainSyntheticRGBHeightNormalized(sourceColor.rgb, referenceColor.rgb);

		// A constant alpha value is NOT evidence of a height map. v3.13 treated any
		// non-1 alpha (including flat 0/0.5 channels) as valid and blended strongly
		// toward that constant value, which could collapse Auto-POM almost entirely.
		// Only local alpha variation across mip scales is accepted as height evidence.
		float alphaEvidence = abs(sourceColor.a - referenceColor.a);
		float threshold = MaterialLayersTuning::TerrainAlphaEvidenceThreshold();
		float alphaConfidence =
			smoothstep(threshold, threshold * 4.0f, alphaEvidence) *
			MaterialLayersTuning::TerrainAlphaAssist();

		return lerp(syntheticHeight, sourceColor.a, alphaConfidence);
	}

	inline bool TerrainAuthoredHeightUsable(bool hasAuthoredFlag, float effectiveHeightScale)
	{
		return hasAuthoredFlag && abs(effectiveHeightScale) > 1e-4f;
	}

	inline bool TerrainShouldUseAutoHeight(bool hasAuthoredHeight, bool fullPBR)
	{
		if (!MaterialLayersTuning::TerrainAutoPOMEnabled())
			return false;

		uint mode = MaterialLayersTuning::TerrainHeightMode();
		if (mode == 1u || mode == 3u)
			return false;
		if (mode == 2u)
			return true;

		// Auto Hybrid: authored displacement is authoritative; every layer without
		// authored displacement may use alpha-assisted/RGB synthetic height.
		return !hasAuthoredHeight;
	}

	inline bool TerrainShouldUseLegacyAlpha(bool hasAuthoredHeight, bool fullPBR)
	{
		return MaterialLayersTuning::TerrainHeightMode() == 3u &&
		       !hasAuthoredHeight &&
		       !fullPBR;
	}

	inline float TerrainFallbackLayerScale(float authoredScale)
	{
		return max(authoredScale, MaterialLayersTuning::TerrainSyntheticScaleFloor());
	}

	inline float TerrainEffectiveLayerScale(bool hasAuthoredFlag, bool fullPBR, float authoredScale)
	{
		const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, authoredScale);
		if (TerrainShouldUseAutoHeight(hasAuthored, fullPBR) ||
		    TerrainShouldUseLegacyAlpha(hasAuthored, fullPBR))
			return TerrainFallbackLayerScale(authoredScale);
		return authoredScale;
	}

	inline float ScaleTerrainFallbackDisplacement(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5f) * TerrainFallbackLayerScale(params.HeightScale);
	}

#	if defined(MATERIAL_FORGE)

		static const uint TERRAIN_PBR_MASK =
			PBR::TerrainFlags::LandTile0PBR |
			PBR::TerrainFlags::LandTile1PBR |
			PBR::TerrainFlags::LandTile2PBR |
			PBR::TerrainFlags::LandTile3PBR |
			PBR::TerrainFlags::LandTile4PBR |
			PBR::TerrainFlags::LandTile5PBR;

		static const uint TERRAIN_DISPLACEMENT_MASK =
			PBR::TerrainFlags::LandTile0HasDisplacement |
			PBR::TerrainFlags::LandTile1HasDisplacement |
			PBR::TerrainFlags::LandTile2HasDisplacement |
			PBR::TerrainFlags::LandTile3HasDisplacement |
			PBR::TerrainFlags::LandTile4HasDisplacement |
			PBR::TerrainFlags::LandTile5HasDisplacement;

#define EM_PBR_SOURCE_FOREACH(M) \
		M(0, PBR::TerrainFlags::LandTile0PBR, PBR::TerrainFlags::LandTile0HasDisplacement, TexLandDisplacement0Sampler, TexColorSampler, w1.x) \
		M(1, PBR::TerrainFlags::LandTile1PBR, PBR::TerrainFlags::LandTile1HasDisplacement, TexLandDisplacement1Sampler, TexLandColor2Sampler, w1.y) \
		M(2, PBR::TerrainFlags::LandTile2PBR, PBR::TerrainFlags::LandTile2HasDisplacement, TexLandDisplacement2Sampler, TexLandColor3Sampler, w1.z) \
		M(3, PBR::TerrainFlags::LandTile3PBR, PBR::TerrainFlags::LandTile3HasDisplacement, TexLandDisplacement3Sampler, TexLandColor4Sampler, w1.w) \
		M(4, PBR::TerrainFlags::LandTile4PBR, PBR::TerrainFlags::LandTile4HasDisplacement, TexLandDisplacement4Sampler, TexLandColor5Sampler, w2.x) \
		M(5, PBR::TerrainFlags::LandTile5PBR, PBR::TerrainFlags::LandTile5HasDisplacement, TexLandDisplacement5Sampler, TexLandColor6Sampler, w2.y)

#define EM_BUILD_PBR_REFERENCE(N, PBRFLAG, DISPFLAG, DISPTEX, COLTEX, WGT) \
		{ \
			const bool hasAuthoredFlag = (PBRFlags & (DISPFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			const bool fullPbr = (PBRFlags & (PBRFLAG)) != 0; \
			[branch] if ((WGT) > 0.01f && TerrainShouldUseAutoHeight(hasAuthored, fullPbr)) \
				referenceColors[N] = TerrainParallaxTexSample(COLTEX, coords, mipLevels[N] + MaterialLayersTuning::TerrainReferenceMipOffset(), sharedOffset, N); \
			else \
				referenceColors[N] = float4(0.5f, 0.5f, 0.5f, 1.0f); \
		}

	void BuildTerrainAutoReferenceColors(
		float2 coords, float mipLevels[6], float4 w1, float2 w2,
		DisplacementParams params[6],
		StochasticOffsets sharedOffset, out float4 referenceColors[6])
	{
		EM_PBR_SOURCE_FOREACH(EM_BUILD_PBR_REFERENCE)
	}

#define EM_PBR_LAYER_SCALAR(N, PBRFLAG, DISPFLAG, DISPTEX, COLTEX, WGT) \
		[branch] if ((WGT) > 0.01f && (WGT) * TerrainEffectiveLayerScale((PBRFlags & (DISPFLAG)) != 0, (PBRFlags & (PBRFLAG)) != 0, params[N].HeightScale) >= layerGateThreshold) \
		{ \
			const bool hasAuthoredFlag = (PBRFlags & (DISPFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			const bool fullPbr = (PBRFlags & (PBRFLAG)) != 0; \
			const uint sourceMode = MaterialLayersTuning::TerrainHeightMode(); \
			[branch] if (hasAuthored && sourceMode != 2u) \
			{ \
				heights[N] = ScaleDisplacement(TerrainParallaxTexSample(DISPTEX, coords, mipLevels[N], sharedOffset, N).x, params[N]); \
			} \
			else if (TerrainShouldUseAutoHeight(hasAuthored, fullPbr)) \
			{ \
				float4 source = TerrainParallaxTexSample(COLTEX, coords, mipLevels[N] + MaterialLayersTuning::TerrainSourceMipBias(), sharedOffset, N); \
				DisplacementParams autoParams = params[N]; \
				autoParams.HeightScale = max(autoParams.HeightScale, MaterialLayersTuning::TerrainSyntheticScaleFloor()); \
				heights[N] = ScaleDisplacement(TerrainAutoHeightNormalized(source, referenceColors[N]), autoParams); \
			} \
			else if (TerrainShouldUseLegacyAlpha(hasAuthored, fullPbr)) \
			{ \
				heights[N] = ScaleTerrainFallbackDisplacement(TerrainParallaxTexSample(COLTEX, coords, mipLevels[N], sharedOffset, N).w, params[N]); \
			} \
		}

#define EM_PBR_LAYER_QUAD(N, PBRFLAG, DISPFLAG, DISPTEX, COLTEX, WGT) \
		[branch] if ((WGT) > 0.01f && (WGT) * TerrainEffectiveLayerScale((PBRFlags & (DISPFLAG)) != 0, (PBRFlags & (PBRFLAG)) != 0, params[N].HeightScale) >= layerGateThreshold) \
		{ \
			const bool hasAuthoredFlag = (PBRFlags & (DISPFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			const bool fullPbr = (PBRFlags & (PBRFLAG)) != 0; \
			const uint sourceMode = MaterialLayersTuning::TerrainHeightMode(); \
			[unroll] for (uint k = 0u; k < 4u; ++k) \
			{ \
				[branch] if (hasAuthored && sourceMode != 2u) \
					h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(DISPTEX, uvs[k], mipLevels[N], sharedOffset, N).x, params[N]); \
				else if (TerrainShouldUseAutoHeight(hasAuthored, fullPbr)) \
				{ \
					float4 source = TerrainParallaxTexSample(COLTEX, uvs[k], mipLevels[N] + MaterialLayersTuning::TerrainSourceMipBias(), sharedOffset, N); \
					DisplacementParams autoParams = params[N]; \
					autoParams.HeightScale = max(autoParams.HeightScale, MaterialLayersTuning::TerrainSyntheticScaleFloor()); \
					h4[k][N] = ScaleDisplacement(TerrainAutoHeightNormalized(source, referenceColors[N]), autoParams); \
				} \
				else if (TerrainShouldUseLegacyAlpha(hasAuthored, fullPbr)) \
					h4[k][N] = ScaleTerrainFallbackDisplacement(TerrainParallaxTexSample(COLTEX, uvs[k], mipLevels[N], sharedOffset, N).w, params[N]); \
			} \
		}

	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = TerrainHeightBlendValue(blendFactor);
		float layerGateThreshold = TerrainLayerGateThreshold(heightBlend, w1, w2, params);
		float heights[6] = { 0, 0, 0, 0, 0, 0 };
		float4 referenceColors[6];
		BuildTerrainAutoReferenceColors(coords, mipLevels, w1, w2, params, sharedOffset, referenceColors);

		EM_PBR_SOURCE_FOREACH(EM_PBR_LAYER_SCALAR)

		float total = 0.0f;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
		return total;
	}

	float4 GetTerrainHeightQuadRayMarch(float screenNoise, PS_INPUT input,
		float2 u0, float2 u1, float2 u2, float2 u3,
		float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset, float4 referenceColors[6],
		out float weights[6])
	{
		float heightBlend = TerrainHeightBlendValue(blendFactor);
		float layerGateThreshold = TerrainLayerGateThreshold(heightBlend, w1, w2, params);
		float2 uvs[4] = { u0, u1, u2, u3 };
		float h4[4][6];
		[unroll] for (uint qi = 0u; qi < 4u; ++qi)
			[unroll] for (uint lj = 0u; lj < 6u; ++lj)
				h4[qi][lj] = 0.0f;

		EM_PBR_SOURCE_FOREACH(EM_PBR_LAYER_QUAD)

		return FinishTerrainHeightQuadBlend(heightBlend, w1, w2, h4[0], h4[1], h4[2], h4[3], weights);
	}

#undef EM_BUILD_PBR_REFERENCE
#undef EM_PBR_LAYER_SCALAR
#undef EM_PBR_LAYER_QUAD
#undef EM_PBR_SOURCE_FOREACH

#	else

#define EM_LEGACY_SOURCE_FOREACH(M) \
		M(0, Permutation::ExtraFeatureFlags::THLand0HasDisplacement, TexLandTHDisp0Sampler, TexColorSampler, w1.x) \
		M(1, Permutation::ExtraFeatureFlags::THLand1HasDisplacement, TexLandTHDisp1Sampler, TexLandColor2Sampler, w1.y) \
		M(2, Permutation::ExtraFeatureFlags::THLand2HasDisplacement, TexLandTHDisp2Sampler, TexLandColor3Sampler, w1.z) \
		M(3, Permutation::ExtraFeatureFlags::THLand3HasDisplacement, TexLandTHDisp3Sampler, TexLandColor4Sampler, w1.w) \
		M(4, Permutation::ExtraFeatureFlags::THLand4HasDisplacement, TexLandTHDisp4Sampler, TexLandColor5Sampler, w2.x) \
		M(5, Permutation::ExtraFeatureFlags::THLand5HasDisplacement, TexLandTHDisp5Sampler, TexLandColor6Sampler, w2.y)

#define EM_BUILD_LEGACY_REFERENCE(N, THFLAG, THTEX, COLTEX, WGT) \
		{ \
			const bool hasAuthoredFlag = (Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			[branch] if ((WGT) > 0.01f && TerrainShouldUseAutoHeight(hasAuthored, false)) \
				referenceColors[N] = TerrainParallaxTexSample(COLTEX, coords, mipLevels[N] + MaterialLayersTuning::TerrainReferenceMipOffset(), sharedOffset, N); \
			else \
				referenceColors[N] = float4(0.5f, 0.5f, 0.5f, 1.0f); \
		}

	void BuildTerrainAutoReferenceColors(
		float2 coords, float mipLevels[6], float4 w1, float2 w2,
		DisplacementParams params[6],
		StochasticOffsets sharedOffset, out float4 referenceColors[6])
	{
		EM_LEGACY_SOURCE_FOREACH(EM_BUILD_LEGACY_REFERENCE)
	}

#define EM_LEGACY_LAYER_SCALAR(N, THFLAG, THTEX, COLTEX, WGT) \
		[branch] if ((WGT) > 0.01f && (WGT) * TerrainEffectiveLayerScale((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0, false, params[N].HeightScale) >= layerGateThreshold) \
		{ \
			const bool hasAuthoredFlag = (Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			const uint sourceMode = MaterialLayersTuning::TerrainHeightMode(); \
			[branch] if (hasAuthored && sourceMode != 2u) \
				heights[N] = ScaleDisplacement(TerrainParallaxTexSample(THTEX, coords, mipLevels[N], sharedOffset, N).x, params[N]); \
			else if (TerrainShouldUseAutoHeight(hasAuthored, false)) \
			{ \
				float4 source = TerrainParallaxTexSample(COLTEX, coords, mipLevels[N] + MaterialLayersTuning::TerrainSourceMipBias(), sharedOffset, N); \
				DisplacementParams autoParams = params[N]; \
				autoParams.HeightScale = max(autoParams.HeightScale, MaterialLayersTuning::TerrainSyntheticScaleFloor()); \
				heights[N] = ScaleDisplacement(TerrainAutoHeightNormalized(source, referenceColors[N]), autoParams); \
			} \
			else if (TerrainShouldUseLegacyAlpha(hasAuthored, false)) \
				heights[N] = ScaleTerrainFallbackDisplacement(TerrainParallaxTexSample(COLTEX, coords, mipLevels[N], sharedOffset, N).w, params[N]); \
		}

#define EM_LEGACY_LAYER_QUAD(N, THFLAG, THTEX, COLTEX, WGT) \
		[branch] if ((WGT) > 0.01f && (WGT) * TerrainEffectiveLayerScale((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0, false, params[N].HeightScale) >= layerGateThreshold) \
		{ \
			const bool hasAuthoredFlag = (Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0; \
			const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[N].HeightScale); \
			const uint sourceMode = MaterialLayersTuning::TerrainHeightMode(); \
			[unroll] for (uint k = 0u; k < 4u; ++k) \
			{ \
				[branch] if (hasAuthored && sourceMode != 2u) \
					h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(THTEX, uvs[k], mipLevels[N], sharedOffset, N).x, params[N]); \
				else if (TerrainShouldUseAutoHeight(hasAuthored, false)) \
				{ \
					float4 source = TerrainParallaxTexSample(COLTEX, uvs[k], mipLevels[N] + MaterialLayersTuning::TerrainSourceMipBias(), sharedOffset, N); \
					DisplacementParams autoParams = params[N]; \
					autoParams.HeightScale = max(autoParams.HeightScale, MaterialLayersTuning::TerrainSyntheticScaleFloor()); \
					h4[k][N] = ScaleDisplacement(TerrainAutoHeightNormalized(source, referenceColors[N]), autoParams); \
				} \
				else if (TerrainShouldUseLegacyAlpha(hasAuthored, false)) \
					h4[k][N] = ScaleTerrainFallbackDisplacement(TerrainParallaxTexSample(COLTEX, uvs[k], mipLevels[N], sharedOffset, N).w, params[N]); \
			} \
		}

	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = TerrainHeightBlendValue(blendFactor);
		float layerGateThreshold = TerrainLayerGateThreshold(heightBlend, w1, w2, params);
		float heights[6] = { 0, 0, 0, 0, 0, 0 };
		float4 referenceColors[6];
		BuildTerrainAutoReferenceColors(coords, mipLevels, w1, w2, params, sharedOffset, referenceColors);

		EM_LEGACY_SOURCE_FOREACH(EM_LEGACY_LAYER_SCALAR)

		float total = 0.0f;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
		return total;
	}

	float4 GetTerrainHeightQuadRayMarch(float screenNoise, PS_INPUT input,
		float2 u0, float2 u1, float2 u2, float2 u3,
		float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset, float4 referenceColors[6],
		out float weights[6])
	{
		float heightBlend = TerrainHeightBlendValue(blendFactor);
		float layerGateThreshold = TerrainLayerGateThreshold(heightBlend, w1, w2, params);
		float2 uvs[4] = { u0, u1, u2, u3 };
		float h4[4][6];
		[unroll] for (uint qi = 0u; qi < 4u; ++qi)
			[unroll] for (uint lj = 0u; lj < 6u; ++lj)
				h4[qi][lj] = 0.0f;

		EM_LEGACY_SOURCE_FOREACH(EM_LEGACY_LAYER_QUAD)

		return FinishTerrainHeightQuadBlend(heightBlend, w1, w2, h4[0], h4[1], h4[2], h4[3], weights);
	}

#undef EM_BUILD_LEGACY_REFERENCE
#undef EM_LEGACY_LAYER_SCALAR
#undef EM_LEGACY_LAYER_QUAD
#undef EM_LEGACY_SOURCE_FOREACH

#	endif
#	define TERRAIN_HEIGHT_AT(COORDS, MIP, QUALITY, WEIGHTS) \
		GetTerrainHeight(noise, input, COORDS, MIP, params, 0.0, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, WEIGHTS)

	float TerrainAutoFallbackWeight(float4 w1, float2 w2, DisplacementParams params[6])
	{
		float fallbackWeight = 0.0f;

#	if defined(MATERIAL_FORGE)
#		define PIXL_ACCUM_AUTO_WEIGHT(INDEX, PBRFLAG, DISPFLAG, WGT) \
			{ \
				const bool hasAuthoredFlag = (PBRFlags & (DISPFLAG)) != 0u; \
				const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[INDEX].HeightScale); \
				const bool fullPbr = (PBRFlags & (PBRFLAG)) != 0u; \
				if (TerrainShouldUseAutoHeight(hasAuthored, fullPbr) || \
				    TerrainShouldUseLegacyAlpha(hasAuthored, fullPbr)) \
					fallbackWeight = max(fallbackWeight, (WGT)); \
			}

		PIXL_ACCUM_AUTO_WEIGHT(0, PBR::TerrainFlags::LandTile0PBR, PBR::TerrainFlags::LandTile0HasDisplacement, w1.x)
		PIXL_ACCUM_AUTO_WEIGHT(1, PBR::TerrainFlags::LandTile1PBR, PBR::TerrainFlags::LandTile1HasDisplacement, w1.y)
		PIXL_ACCUM_AUTO_WEIGHT(2, PBR::TerrainFlags::LandTile2PBR, PBR::TerrainFlags::LandTile2HasDisplacement, w1.z)
		PIXL_ACCUM_AUTO_WEIGHT(3, PBR::TerrainFlags::LandTile3PBR, PBR::TerrainFlags::LandTile3HasDisplacement, w1.w)
		PIXL_ACCUM_AUTO_WEIGHT(4, PBR::TerrainFlags::LandTile4PBR, PBR::TerrainFlags::LandTile4HasDisplacement, w2.x)
		PIXL_ACCUM_AUTO_WEIGHT(5, PBR::TerrainFlags::LandTile5PBR, PBR::TerrainFlags::LandTile5HasDisplacement, w2.y)

#		undef PIXL_ACCUM_AUTO_WEIGHT
#	else
#		define PIXL_ACCUM_AUTO_WEIGHT(INDEX, DISPFLAG, WGT) \
			{ \
				const bool hasAuthoredFlag = \
					(Permutation::ExtraFeatureDescriptor & (DISPFLAG)) != 0u; \
				const bool hasAuthored = TerrainAuthoredHeightUsable(hasAuthoredFlag, params[INDEX].HeightScale); \
				if (TerrainShouldUseAutoHeight(hasAuthored, false) || \
				    TerrainShouldUseLegacyAlpha(hasAuthored, false)) \
					fallbackWeight = max(fallbackWeight, (WGT)); \
			}

		PIXL_ACCUM_AUTO_WEIGHT(0, Permutation::ExtraFeatureFlags::THLand0HasDisplacement, w1.x)
		PIXL_ACCUM_AUTO_WEIGHT(1, Permutation::ExtraFeatureFlags::THLand1HasDisplacement, w1.y)
		PIXL_ACCUM_AUTO_WEIGHT(2, Permutation::ExtraFeatureFlags::THLand2HasDisplacement, w1.z)
		PIXL_ACCUM_AUTO_WEIGHT(3, Permutation::ExtraFeatureFlags::THLand3HasDisplacement, w1.w)
		PIXL_ACCUM_AUTO_WEIGHT(4, Permutation::ExtraFeatureFlags::THLand4HasDisplacement, w2.x)
		PIXL_ACCUM_AUTO_WEIGHT(5, Permutation::ExtraFeatureFlags::THLand5HasDisplacement, w2.y)

#		undef PIXL_ACCUM_AUTO_WEIGHT
#	endif

		return fallbackWeight;
	}

	float TerrainEffectivePomScale(float4 w1, float2 w2, DisplacementParams params[6])
	{
		float authoredWeightedScale = TerrainMaxWeightedHeightScaleW(w1, w2, params);
		float fallbackWeight = TerrainAutoFallbackWeight(w1, w2, params);

		// Synthetic terrain must have a real march amplitude even when an authored
		// displacement flag exists with effective scale 0. Do not mutate params here:
		// source classification still needs the original authored scale later.
		float fallbackActivity = smoothstep(0.01f, 0.12f, fallbackWeight);
		float fallbackScale =
			MaterialLayersTuning::TerrainSyntheticScaleFloor() * fallbackActivity;
		return max(authoredWeightedScale, fallbackScale);
	}


	inline bool TerrainHasAnyDisplacement()
	{
		uint mode = MaterialLayersTuning::TerrainHeightMode();
#	if defined(MATERIAL_FORGE)
		bool hasAuthored = (PBRFlags & TERRAIN_DISPLACEMENT_MASK) != 0;
		if (mode == 1u)
			return hasAuthored;
		if (mode == 2u)
			return MaterialLayersTuning::TerrainAutoPOMEnabled();
		if (mode == 3u)
			return hasAuthored || ((PBRFlags & TERRAIN_PBR_MASK) != TERRAIN_PBR_MASK);

		// Auto Hybrid: authored height or any synthetic fallback can activate POM.
		return hasAuthored || MaterialLayersTuning::TerrainAutoPOMEnabled();
#	else
		bool hasAuthored =
			(Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement) != 0;
		if (mode == 1u)
			return hasAuthored;
		if (mode == 2u)
			return MaterialLayersTuning::TerrainAutoPOMEnabled();
		if (mode == 3u)
			return true;
		return hasAuthored || MaterialLayersTuning::TerrainAutoPOMEnabled();
#	endif
	}

	inline bool TerrainParallaxEnabled()
	{
		return SharedData::materialLayerSettings.EnableParallax &&
		       TerrainHasAnyDisplacement();
	}

	inline float TerrainMaxWeightedHeightScale(PS_INPUT input, DisplacementParams params[6])
	{
		return TerrainMaxWeightedHeightScaleW(input.LandBlendWeights1, input.LandBlendWeights2.xy, params);
	}

	inline uint TerrainDirectionalShadowTapCount(float quality)
	{
		return MaterialLayersTuning::TerrainSelfShadowsEnabled() && quality > 0.0f ? 1u : 0u;
	}

	bool ComputeTerrainParallaxShadowBaseHeight(PS_INPUT input, float2 coords, float mipLevels[6], float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, out float sh0)
	{
		sh0 = 0.0;
		if (!TerrainHasAnyDisplacement())
			return false;

		float weights[6] = { 0, 0, 0, 0, 0, 0 };
		sh0 = TERRAIN_HEIGHT_AT(coords, mipLevels, quality, weights);
		return true;
	}

	// One-tap terrain self-shadow with a receiver bias and light-angle-aware ray.
	// Sample count is unchanged; the response is simply less binary/noisy.
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset)
	{
		if (quality > 0.0f && L.z > 0.03f) {
			float maxScale = max(
				TerrainEffectivePomScale(
					input.LandBlendWeights1, input.LandBlendWeights2.xy, params),
				0.01f);
			float invLz = rcp(max(L.z, 0.25f));
			float2 rayDir = L.xy * invLz * 0.060f * MaterialLayersTuning::TerrainShadowRayScale();
			float heights[6] = { 0, 0, 0, 0, 0, 0 };
			float shi = TERRAIN_HEIGHT_AT(coords + rayDir * rcp(1.0f + noise), mipLevel, quality, heights);

			float heightBias = maxScale * 0.010f * MaterialLayersTuning::TerrainShadowBias();
			float delta = max(shi - sh0 - heightBias, 0.0f);
			float occlusion = saturate(delta * (ShadowIntensity * 3.2f));
			occlusion = occlusion * occlusion * (3.0f - 2.0f * occlusion);
			float lightGate = smoothstep(0.03f, 0.18f, L.z);
			return 1.0f - saturate(occlusion * lightGate * MaterialLayersTuning::TerrainShadowStrength());
		}
		return 1.0f;
	}

	float EvaluateTerrainDirectionalParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, float sh0)
	{
		if (TerrainDirectionalShadowTapCount(quality) == 0 || lightDirection.z <= 0.03f)
			return 1.0f;

		float maxScale = max(
				TerrainEffectivePomScale(
					input.LandBlendWeights1, input.LandBlendWeights2.xy, params),
				0.01f);
		float invLz = rcp(max(lightDirection.z, 0.25f));
		float2 rayDir = lightDirection.xy * invLz * 0.060f * MaterialLayersTuning::TerrainShadowRayScale();
		float heights[6] = { 0, 0, 0, 0, 0, 0 };
		float shi = TERRAIN_HEIGHT_AT(coords + rayDir * rcp(1.0f + noise), mipLevels, quality, heights);

		float heightBias = maxScale * 0.010f * MaterialLayersTuning::TerrainShadowBias();
		float delta = max(shi - sh0 - heightBias, 0.0f);
		float occlusion = saturate(delta * (ShadowIntensity * 1.65f));
		occlusion = occlusion * occlusion * (3.0f - 2.0f * occlusion);
		float lightGate = smoothstep(0.03f, 0.18f, lightDirection.z);
		return 1.0f - saturate(occlusion * lightGate * MaterialLayersTuning::TerrainShadowStrength());
	}

#	undef TERRAIN_HEIGHT_AT

#endif  // MATERIAL_LAYERS_TERRAIN_HLSLI
