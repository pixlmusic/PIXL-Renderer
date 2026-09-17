#ifndef MATERIAL_LAYERS_PARALLAX_CORE_HLSLI
#define MATERIAL_LAYERS_PARALLAX_CORE_HLSLI

// PIXL production controls. All optimizations preserve the existing interfaces
// and can be disabled independently for A/B testing.
#ifndef USE_PIXL_ADAPTIVE_POM_STEPS
#	define USE_PIXL_ADAPTIVE_POM_STEPS 1
#endif
#ifndef PIXL_POM_REFINEMENT_STEPS
#	define PIXL_POM_REFINEMENT_STEPS 4
#endif
#ifndef USE_PIXL_AUTOPOM_CHROMA_CONFIDENCE
#	define USE_PIXL_AUTOPOM_CHROMA_CONFIDENCE 1
#endif
#ifndef PIXL_AUTO_PARALLAX_MIN_TEXEL_SHIFT
#	define PIXL_AUTO_PARALLAX_MIN_TEXEL_SHIFT 0.25f
#endif
#ifndef PIXL_AUTHORED_POM_DEPTH_GAIN
#	define PIXL_AUTHORED_POM_DEPTH_GAIN 1.55f
#endif
#ifndef PIXL_AUTO_POM_DEPTH_GAIN
#	define PIXL_AUTO_POM_DEPTH_GAIN 1.45f
#endif

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float distance, float2 coords, float mipLevels[6], float maxTexDim, float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
		StochasticOffsets sharedOffset,
		out float pixelOffset,
		out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		pixelOffset = 0.0;
#if defined(LANDSCAPE)
		maxTexDim = maxTexDim;
#endif
		float3 viewDirTS = normalize(mul(tbn, viewDir));
		float viewZ = abs(viewDirTS.z);
#if defined(LANDSCAPE)
		viewDirTS.xy /= max(viewZ * 0.7f + 0.3f + params[0].FlattenAmount, 0.08f);
#else
		viewDirTS.xy /= max(viewZ * 0.7f + 0.3f + params.FlattenAmount, 0.08f);
#endif

#if defined(LANDSCAPE)
		float nearBlendToFar = smoothstep(
			MaterialLayersTuning::TerrainFadeStart(),
			MaterialLayersTuning::TerrainFadeEnd(),
			abs(distance));
		float terrainMipFade = 1.0f - smoothstep(
			max(MaterialLayersTuning::TerrainMaxMip() - 2.0f, 0.0f),
			MaterialLayersTuning::TerrainMaxMip(),
			mipLevels[0]);
		nearBlendToFar = max(nearBlendToFar, 1.0f - terrainMipFade);

		float blendFactor = SharedData::materialLayerSettings.EnableHeightBlending
			? sqrt(saturate(1.0f - nearBlendToFar))
			: 0.0f;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0, 1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0, 1, input.LandBlendWeights2.xy), blendFactor);
#	if defined(MATERIAL_FORGE)
		float scale = TerrainEffectivePomScale(w1, w2, params);
		float scalercp = rcp(max(scale, 1e-4f));
#	else
		float scale = max(1.0f, TerrainEffectivePomScale(w1, w2, params));
#	endif
		float maxHeight =
			0.1f * scale *
			MaterialLayersTuning::TerrainDepthScale();
#else
		float nearBlendToFar = smoothstep(
			MaterialLayersTuning::ObjectFadeStart(),
			MaterialLayersTuning::ObjectFadeEnd(),
			abs(distance));
		// Some PBR texture packages flag a valid displacement map but export an
		// extremely small height scale. That makes the user depth slider appear
		// broken because its multiplier is applied to an almost-flat source. Keep
		// a conservative authored floor; this branch is only reached after the
		// caller has confirmed a real authored displacement path.
		float scale = max(params.HeightScale, 0.30f);
		float maxHeight =
			0.1f * scale * PIXL_AUTHORED_POM_DEPTH_GAIN *
			MaterialLayersTuning::ObjectAuthoredDepthScale();
#endif

		// POM is most vulnerable to silhouette stretching when the tangent-space
		// view ray approaches the surface plane. Apply a nonlinear safety factor,
		// then enforce an absolute texel-space displacement budget.
#if defined(LANDSCAPE)
		float grazingSafety = lerp(
			1.0f,
			smoothstep(0.035f, 0.24f, viewZ),
			MaterialLayersTuning::TerrainGrazingProtection());
		maxHeight *= grazingSafety;
		float mipDimension = max(maxTexDim * exp2(-mipLevels[0]), 1.0f);
		float projectedTexelShift =
			max(abs(viewDirTS.x), abs(viewDirTS.y)) *
			maxHeight * mipDimension;
		float terrainShiftLimit = MaterialLayersTuning::TerrainMaxTexelShift();
		if (projectedTexelShift > terrainShiftLimit)
			maxHeight *= terrainShiftLimit / max(projectedTexelShift, 1e-5f);
#else
		float grazingSafety = lerp(
			1.0f,
			smoothstep(0.035f, 0.24f, viewZ),
			MaterialLayersTuning::ObjectGrazingProtection());
		maxHeight *= grazingSafety;
		float2 objectTextureDims;
		tex.GetDimensions(objectTextureDims.x, objectTextureDims.y);
		float2 sampledDims = max(objectTextureDims * exp2(-mipLevel), 1.0f.xx);
		float2 projectedTexelShift =
			abs(viewDirTS.xy * maxHeight) * sampledDims;
		float objectShift = max(projectedTexelShift.x, projectedTexelShift.y);
		float objectShiftLimit = MaterialLayersTuning::ObjectMaxTexelShift();
		if (objectShift > objectShiftLimit)
			maxHeight *= objectShiftLimit / max(objectShift, 1e-5f);
#endif
		float minHeight = maxHeight * 0.5f;

		float2 resultCoords = coords;

#if defined(LANDSCAPE)
		float4 terrainReferenceColors[6];
		BuildTerrainAutoReferenceColors(
			coords, mipLevels, w1, w2, params, sharedOffset, terrainReferenceColors);
		if (nearBlendToFar < 1.0) {
#else
#	if defined(MATERIAL_FORGE)
		if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0 || nearBlendToFar < 1.0)
#	else
		if (nearBlendToFar < 1.0)
#	endif
		{
#endif
#if defined(LANDSCAPE)
			uint maxSteps = MaterialLayersTuning::TerrainMaxSteps();
			uint nearSteps = min(MaterialLayersTuning::TerrainNearSteps(), maxSteps);
#else
			uint maxSteps = MaterialLayersTuning::ObjectMaxSteps();
			uint nearSteps = min(MaterialLayersTuning::ObjectNearSteps(), maxSteps);
#endif
#if defined(LANDSCAPE)
			maxSteps = max(4u, min(maxSteps, 64u));
#else
			maxSteps = max(4u, min(maxSteps, 32u));
#endif
			nearSteps = max(4u, min(nearSteps, maxSteps));
			uint numSteps;
#if USE_PIXL_ADAPTIVE_POM_STEPS
			float grazingQuality = smoothstep(0.10f, 0.88f, 1.0f - viewZ);
#if defined(LANDSCAPE)
			float detailMip = mipLevels[0];
#else
			float detailMip = mipLevel;
#endif
			float detailQuality = 1.0f - smoothstep(1.0f, 6.0f, detailMip);
			float stepQuality = saturate(max(
				grazingQuality * lerp(0.35f, 1.0f, detailQuality),
				(1.0f - nearBlendToFar) * 0.35f));
			numSteps = (uint)round(lerp((float)nearSteps, (float)maxSteps, stepQuality));
#else
			numSteps = (uint)round(lerp((float)nearSteps, (float)maxSteps, 1.0f - nearBlendToFar));
#endif

			// Stronger relief needs a matching sample budget.  Never let the old
			// HeightScale optimization reduce the march below configured Near Steps.
#if defined(LANDSCAPE)
			float marchDepthScale = max(MaterialLayersTuning::TerrainDepthScale(), 0.25f);
#else
			float marchDepthScale = max(MaterialLayersTuning::ObjectAuthoredDepthScale(), 0.25f);
#endif
			float marchScale = max(scale * marchDepthScale, 0.0f);
			uint scaleCap = max(
				nearSteps,
				min(maxSteps, (uint)ceil(max(marchScale * float(maxSteps), 4.0f))));
			scaleCap = max(4u, (scaleCap + 2u) & ~3u);
			numSteps = min(maxSteps, min(max(numSteps, nearSteps), scaleCap));
			numSteps = max(4u, (numSteps + 2u) & ~3u);

			float stepSize = rcp((float)numSteps);

			float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
			float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

			float prevBound = 1.0;
			float prevHeight = 1.0;

			float2 pt1 = 0;
			float2 pt2 = 0;

			bool contactRefinement = false;

			[loop] while (numSteps > 0)
			{
				float4 currentOffset[2];
				currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
				currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
				float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

				float4 currHeight;
#if defined(LANDSCAPE)
#	if defined(MATERIAL_FORGE)
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, terrainReferenceColors, weights) * scalercp + 0.5;
#	else
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, terrainReferenceColors, weights) + 0.5;
#	endif
#else
				currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
				currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
				currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
				currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

				currHeight = AdjustDisplacementNormalized(currHeight, params);
#endif

				bool4 testResult = currHeight >= currentBound;
				[branch] if (any(testResult))
				{
					float2 outOffset = 0;
					[flatten] if (testResult.w)
					{
						outOffset = currentOffset[1].xy;
						pt1 = float2(currentBound.w, currHeight.w);
						pt2 = float2(currentBound.z, currHeight.z);
					}
					[flatten] if (testResult.z)
					{
						outOffset = currentOffset[0].zw;
						pt1 = float2(currentBound.z, currHeight.z);
						pt2 = float2(currentBound.y, currHeight.y);
					}
					[flatten] if (testResult.y)
					{
						outOffset = currentOffset[0].xy;
						pt1 = float2(currentBound.y, currHeight.y);
						pt2 = float2(currentBound.x, currHeight.x);
					}
					[flatten] if (testResult.x)
					{
						outOffset = prevOffset;
						pt1 = float2(currentBound.x, currHeight.x);
						pt2 = float2(prevBound, prevHeight);
					}
					if (contactRefinement) {
						break;
					} else {
						contactRefinement = true;
						prevOffset = outOffset;
						prevBound = pt2.x;

						// The previous code refined a single coarse interval with another full
						// numSteps march (up to 16 extra samples). Four sub-steps plus the
						// existing secant solve already gives an effective 4x finer crossing
						// resolution while capping refinement cost to one quad.
						#if defined(LANDSCAPE)
						uint refineSteps = MaterialLayersTuning::TerrainRefinementSteps();
#else
						uint refineSteps = MaterialLayersTuning::ObjectRefinementSteps();
#endif
						refineSteps = max(4u, refineSteps);
						refineSteps = (refineSteps + 3u) & ~3u;
						numSteps = refineSteps;
						float refineRcp = rcp((float)refineSteps);
						stepSize *= refineRcp;
						offsetPerStep *= refineRcp;
						continue;
					}
				}

				prevOffset = currentOffset[1].zw;
				prevBound = currentBound.w;
				prevHeight = currHeight.w;
				numSteps -= 4;
			}

			float delta2 = pt2.x - pt2.y;
			float delta1 = pt1.x - pt1.y;
			float denominator = delta2 - delta1;

			// Robust secant intersection. A nearly parallel height/bound segment can make the
			// denominator tiny and explode the UV offset at grazing angles. Falling back to
			// the neutral midpoint preserves the original surface instead of creating a spike.
			float parallaxAmount = 0.5f;
			[flatten] if (abs(denominator) > 1e-5f)
			{
				parallaxAmount = saturate((pt1.x * delta2 - pt2.x * delta1) / denominator);
			}

#if defined(MATERIAL_FORGE)
			if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
				nearBlendToFar = 0;
			else
#endif
				nearBlendToFar *= nearBlendToFar;
			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = saturate(lerp(parallaxAmount, 0.5, nearBlendToFar));
			resultCoords = lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
		} else {
#if defined(LANDSCAPE)
			weights[0] = input.LandBlendWeights1.x;
			weights[1] = input.LandBlendWeights1.y;
			weights[2] = input.LandBlendWeights1.z;
			weights[3] = input.LandBlendWeights1.w;
			weights[4] = input.LandBlendWeights2.x;
			weights[5] = input.LandBlendWeights2.y;
#endif
			pixelOffset = 0.0;
		}

		return resultCoords;
	}

#	if !defined(LANDSCAPE)

	// -------------------------------------------------------------------------------------------------
	// PIXL Vanilla Auto-POM
	// -------------------------------------------------------------------------------------------------
	// Vanilla materials generally do not expose an authored displacement texture. This path synthesizes
	// a bounded local height field from diffuse luminance relative to a coarse-mip reference. The normal
	// map is used by Lighting.hlsl only to modulate displacement amplitude; its alpha/gloss channel is
	// preserved. Authored POM paths remain untouched and take priority.

// Runtime Auto-POM tuning comes from dedicated PS b9. The getters preserve
	// StableABI defaults if the buffer is absent or stale.

	inline float AutoParallaxDistanceFade(float distance)
	{
		float d = abs(distance);
		return 1.0f - smoothstep(
			MaterialLayersTuning::ObjectFadeStart(),
			MaterialLayersTuning::ObjectFadeEnd(),
			d);
	}

	inline float GetAutoParallaxMipLevel(float2 coords, Texture2D<float4> tex, out float2 textureDims)
	{
		tex.GetDimensions(textureDims.x, textureDims.y);
		float2 texCoordsPerSize = coords * textureDims;
		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);
		// Use the standard largest footprint for the synthesized height path.
		// This is deliberately independent from GetMipLevelFromDims so existing
		// authored parallax/complex-material mip behaviour is unchanged.
		float maxTexCoordDelta = max(dot(dxSize, dxSize), dot(dySize, dySize));
		float mipLevel = max(0.5f * log2(max(maxTexCoordDelta, 1e-8f)), 0.0f);
		return floor(max(mipLevel + SharedData::MipBias, 0.0f));
	}

	inline float AutoParallaxLuminance(float3 color)
	{
		return dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
	}

	inline float AutoParallaxSourceMipBias()
	{
		// Geometry must come from a lower frequency band than material microdetail.
		// Deriving this from the existing reference control keeps the b9 ABI stable.
		return clamp(MaterialLayersTuning::AutoHeightReferenceMipOffset() * 0.28f, 0.75f, 1.50f);
	}

	inline float AutoParallaxHeightFromLuma(float luma, float referenceLuma, float structuralConfidence)
	{
		// A confidence-dependent dead zone prevents compression grain, baked lighting
		// and tiny painted fibres from becoming literal geometry. Strong, coherent
		// mid-scale features still span most of the useful height range.
		float signedDelta = luma - referenceLuma;
		float deadZone = lerp(0.035f, 0.012f, saturate(structuralConfidence));
		float relief = max(abs(signedDelta) - deadZone, 0.0f) *
			MaterialLayersTuning::AutoHeightContrast();
		float boundedRelief = relief * rcp(relief + 0.18f);
		float h = saturate(0.5f + sign(signedDelta) * 0.48f * boundedRelief);
		return h * h * (3.0f - 2.0f * h);
	}

	inline float SampleAutoParallaxHeight(
		Texture2D<float4> tex, SamplerState texSampler, float2 coords, float mipLevel, float referenceLuma, float structuralConfidence)
	{
		float luma = AutoParallaxLuminance(tex.SampleLevel(texSampler, coords, mipLevel).rgb);
		return AutoParallaxHeightFromLuma(luma, referenceLuma, structuralConfidence);
	}

	inline float AutoParallaxStructuralConfidence(
		float3 fineColor, float3 structuralColor, float3 referenceColor,
		float normalActivity, float2 normalDirectionTS, float2 structuralGradient)
	{
#if USE_PIXL_AUTOPOM_CHROMA_CONFIDENCE
		// Evaluate once before marching. The fine-to-structural residual measures
		// texture noise, while structural-to-reference contrast measures shapes that
		// are large enough to justify displacement. Chroma-only paint and broad
		// colour changes without matching normal-map structure are rejected.
		float3 f = max(fineColor, 0.0f);
		float3 c = max(structuralColor, 0.0f);
		float3 r = max(referenceColor, 0.0f);
		float lumaDelta = AutoParallaxLuminance(c) - AutoParallaxLuminance(r);
		float3 chromaResidual = (c - r) - lumaDelta.xxx;
		float chromaEnergy = dot(chromaResidual, chromaResidual);
		float lumaEnergy = lumaDelta * lumaDelta + 1e-4f;
		float chromaConfidence = saturate(
			lumaEnergy * rcp(lumaEnergy + chromaEnergy * MaterialLayersTuning::AutoHeightChromaRejection()));

		float fineResidual = abs(AutoParallaxLuminance(f) - AutoParallaxLuminance(c));
		float structuralSignal = abs(lumaDelta);
		float scaleCoherence = saturate(
			structuralSignal * rcp(structuralSignal + fineResidual * 1.75f + 0.012f));
		float contrastEvidence = smoothstep(
			0.010f, 0.095f,
			structuralSignal + length(structuralGradient) * 0.65f);

		float normalSupport = smoothstep(0.06f, 0.55f, saturate(normalActivity));
		float normalLength = length(normalDirectionTS);
		float gradientLength = length(structuralGradient);
		float directionalReliability =
			smoothstep(0.004f, 0.055f, gradientLength) *
			smoothstep(0.05f, 0.45f, normalLength);
		float directionalAgreement = 1.0f;
		if (directionalReliability > 1e-4f)
		{
			float alignment = abs(dot(
				normalDirectionTS * rcp(max(normalLength, 1e-5f)),
				structuralGradient * rcp(max(gradientLength, 1e-5f))));
			directionalAgreement = lerp(1.0f, lerp(0.38f, 1.0f, alignment), directionalReliability);
		}

		float confidence =
			contrastEvidence *
			lerp(0.28f, 1.0f, scaleCoherence) *
			lerp(0.35f, 1.0f, chromaConfidence) *
			lerp(0.18f, 1.0f, normalSupport) *
			directionalAgreement;
		return smoothstep(0.06f, 0.82f, saturate(confidence));
#else
		return 1.0f;
#endif
	}

	float2 GetAutoParallaxCoords(
		float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float2 textureDims,
		float normalActivity, float2 normalDirectionTS, Texture2D<float4> tex, SamplerState texSampler,
		out float pixelOffset, out float baseHeight, out float appliedStrength)
	{
		pixelOffset = 0.0f;
		baseHeight = 0.5f;
		appliedStrength = 0.0f;

		if (!MaterialLayersTuning::ObjectAutoPOMEnabled())
			return coords;

		float distanceFade = AutoParallaxDistanceFade(distance);
		float autoMaxMip = MaterialLayersTuning::ObjectMaxMip();
		float mipFade = 1.0f - smoothstep(max(autoMaxMip - 2.0f, 0.0f), autoMaxMip, mipLevel);
		if (distanceFade <= 1e-4f || mipFade <= 1e-4f)
			return coords;

		float3 viewDirTS = normalize(mul(tbn, viewDir));
		float viewZ = abs(viewDirTS.z);
		float obliqueness = saturate(1.0f - viewZ);

		// The old broad grazing multiplier erased much of Auto-POM at ordinary
		// oblique views.  The ray already shrinks toward normal incidence, so keep
		// only a narrow near-normal safety fade.
		float incidenceFade = smoothstep(0.005f, 0.080f, obliqueness);
		if (incidenceFade <= 1e-4f)
			return coords;

		float normalContribution = lerp(
			1.0f,
			saturate(normalActivity),
			saturate(MaterialLayersTuning::AutoHeightNormalInfluence()));
		float untrustedStrength =
			MaterialLayersTuning::ObjectAutoHeightScale() * PIXL_AUTO_POM_DEPTH_GAIN *
			distanceFade * mipFade * normalContribution;
		if (untrustedStrength <= 1e-5f)
			return coords;

		// Three frequency bands distinguish usable material structure from noisy
		// albedo. Two one-texel neighbours provide an inexpensive shape/normal
		// agreement test before the considerably more expensive POM march.
		float sourceMip = mipLevel + AutoParallaxSourceMipBias();
		float referenceMip = max(
			sourceMip + 1.5f,
			mipLevel + MaterialLayersTuning::AutoHeightReferenceMipOffset());
		float3 fineColor = tex.SampleLevel(texSampler, coords, mipLevel).rgb;
		float3 structuralColor = tex.SampleLevel(texSampler, coords, sourceMip).rgb;
		float3 referenceColor = tex.SampleLevel(texSampler, coords, referenceMip).rgb;
		float2 structuralTexel = exp2(sourceMip) * rcp(max(textureDims, 1.0f.xx));
		float2 structuralGradient;
		structuralGradient.x = AutoParallaxLuminance(
			tex.SampleLevel(texSampler, coords + float2(structuralTexel.x, 0.0f), sourceMip).rgb) -
			AutoParallaxLuminance(structuralColor);
		structuralGradient.y = AutoParallaxLuminance(
			tex.SampleLevel(texSampler, coords + float2(0.0f, structuralTexel.y), sourceMip).rgb) -
			AutoParallaxLuminance(structuralColor);

		float structuralConfidence = AutoParallaxStructuralConfidence(
			fineColor, structuralColor, referenceColor,
			normalActivity, normalDirectionTS, structuralGradient);
		float strength = untrustedStrength * structuralConfidence;
		if (strength <= 1e-5f)
			return coords;

		// Center the synthetic displacement around the original surface so automatic
		// relief does not appear to inflate every mesh. The denominator is bounded to
		// avoid the extreme grazing offsets that make generic POM detach from geometry.
		float2 parallaxDir = viewDirTS.xy / (viewZ * 0.72f + 0.28f);
		float autoGrazingSafety = lerp(
			1.0f,
			smoothstep(0.035f, 0.24f, viewZ),
			MaterialLayersTuning::ObjectGrazingProtection());
		float2 totalOffset = parallaxDir * (strength * autoGrazingSafety);

		// If the final visible shift is below a fraction of one texel at the sampled
		// mip, marching cannot change the resolved texture meaningfully. This removes
		// most far/front-facing Auto-POM work with no visible quality loss.
		float2 mipTextureDims = max(textureDims * exp2(-mipLevel), 1.0f.xx);
		float2 visibleTexelShift = abs(totalOffset * incidenceFade) * mipTextureDims;
		float maxVisibleShift = max(visibleTexelShift.x, visibleTexelShift.y);
		if (maxVisibleShift < MaterialLayersTuning::AutoMinTexelShift())
			return coords;

		float shiftLimit = MaterialLayersTuning::ObjectMaxTexelShift();
		if (maxVisibleShift > shiftLimit)
			totalOffset *= shiftLimit / max(maxVisibleShift, 1e-5f);

		uint nearSteps = MaterialLayersTuning::ObjectNearSteps();
		uint maxSteps = MaterialLayersTuning::ObjectMaxSteps();
		float marchDemand = smoothstep(0.06f, 0.82f, obliqueness);
		float shiftDemand = saturate(maxVisibleShift / max(shiftLimit, 1.0f));
		float stepQuality = max(marchDemand, shiftDemand * 0.85f);
		uint numSteps = (uint)round(
			lerp((float)nearSteps, (float)maxSteps, stepQuality));
		numSteps = max(1u, min(numSteps, maxSteps));

		float layerStep = rcp((float)numSteps);
		float2 deltaUV = totalOffset * layerStep;
		float2 currentUV = coords + totalOffset * 0.5f;

		// Coarse reference removes broad material colour from the generated height
		// field. Only detail relative to the local average becomes relief.
		float referenceLuma = AutoParallaxLuminance(referenceColor);
		float3 currentColor = tex.SampleLevel(texSampler, currentUV, sourceMip).rgb;

		float currentLayer = 0.0f;
		float currentHeight = AutoParallaxHeightFromLuma(
			AutoParallaxLuminance(currentColor), referenceLuma, structuralConfidence);
		float2 previousUV = currentUV;
		float previousLayer = currentLayer;
		float previousHeight = currentHeight;

		[loop] for (uint stepIndex = 0u; stepIndex < 32u; ++stepIndex)
		{
			if (stepIndex >= numSteps || currentLayer >= currentHeight)
				break;

			previousUV = currentUV;
			previousLayer = currentLayer;
			previousHeight = currentHeight;

			currentUV -= deltaUV;
			currentLayer += layerStep;
			currentHeight = SampleAutoParallaxHeight(tex, texSampler, currentUV, sourceMip, referenceLuma, structuralConfidence);
		}

		// v3.14 exposed Object Refinement Steps but synthetic Auto-POM only did
		// one secant interpolation.  Refine the actual crossing first.
		uint refineSteps = min(MaterialLayersTuning::ObjectRefinementSteps(), 12u);
		[loop] for (uint refineIndex = 0u; refineIndex < 12u; ++refineIndex)
		{
			if (refineIndex >= refineSteps)
				break;

			float2 midUV = (previousUV + currentUV) * 0.5f;
			float midLayer = (previousLayer + currentLayer) * 0.5f;
			float midHeight = SampleAutoParallaxHeight(
				tex, texSampler, midUV, sourceMip, referenceLuma, structuralConfidence);

			if (midLayer < midHeight)
			{
				previousUV = midUV;
				previousLayer = midLayer;
				previousHeight = midHeight;
			}
			else
			{
				currentUV = midUV;
				currentLayer = midLayer;
				currentHeight = midHeight;
			}
		}

		float before = previousHeight - previousLayer;
		float after = currentHeight - currentLayer;
		float denom = before - after;
		float intersection = abs(denom) > 1e-5f ? saturate(before / denom) : 0.5f;
		float2 refinedUV = lerp(previousUV, currentUV, intersection);

		float visibleStrength = strength * incidenceFade;
		float2 finalUV = lerp(coords, refinedUV, incidenceFade);
		float finalHeight = lerp(previousHeight, currentHeight, intersection);

		pixelOffset = saturate(lerp(previousLayer, currentLayer, intersection));
		baseHeight = finalHeight;
		appliedStrength = visibleStrength;
		return finalUV;
	}

	inline float GetAutoParallaxSoftShadowMultiplier(
		float2 coords, float mipLevel, float3 L, float sh0, float parallaxStrength,
		Texture2D<float4> tex, SamplerState texSampler, float noise)
	{
		// Synthetic height is intentionally treated conservatively. Diffuse-derived
		// relief is excellent for parallax but dark albedo must never turn into a hard
		// geometric blocker. This function is only used when the optional Auto-POM
		// self-shadow feature is explicitly enabled.
		if (parallaxStrength <= 1e-5f || L.z <= 0.08f)
			return 1.0f;

		float sourceMip = mipLevel + AutoParallaxSourceMipBias();
		float referenceMip = max(
			sourceMip + 1.5f,
			mipLevel + MaterialLayersTuning::AutoHeightReferenceMipOffset());
		float3 referenceColor = tex.SampleLevel(texSampler, coords, referenceMip).rgb;
		float referenceLuma = AutoParallaxLuminance(referenceColor);
		float invLz = rcp(max(L.z, 0.25f));
		float2 rayDir = L.xy * invLz * min(parallaxStrength * 1.15f * MaterialLayersTuning::ObjectShadowRayScale(), 0.060f);
		float phase = frac(noise * 0.754877666f);

		float3 distances = float3(0.35f, 0.68f, 1.0f) + phase * 0.08f;
		float3 h;
		h.x = SampleAutoParallaxHeight(tex, texSampler, coords + rayDir * distances.x, sourceMip, referenceLuma, 1.0f);
		h.y = SampleAutoParallaxHeight(tex, texSampler, coords + rayDir * distances.y, sourceMip, referenceLuma, 1.0f);
		h.z = SampleAutoParallaxHeight(tex, texSampler, coords + rayDir * distances.z, sourceMip, referenceLuma, 1.0f);

		float heightBias = (0.035f + 0.006f * min(mipLevel, 4.0f)) * MaterialLayersTuning::ObjectShadowBias();
		float3 delta = max(h - sh0.xxx - heightBias.xxx, 0.0f);
		float3 occ = saturate(delta * 2.2f);
		float average = dot(occ, float3(0.50f, 0.32f, 0.18f));
		float peak = max(occ.x, max(occ.y, occ.z));
		float coherence = smoothstep(0.06f, 0.28f, average);
		float occlusion = lerp(average, peak, coherence * 0.20f) * coherence;
		float lightGate = smoothstep(0.08f, 0.22f, L.z);

		// Never let reconstructed vanilla height remove most of a direct light.
		return 1.0f - saturate(occlusion * lightGate * MaterialLayersTuning::ObjectShadowStrength());
	}

	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
	float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, float noise, DisplacementParams params)
	{
		float shadowMultiplier = 1.0f;
		[branch] if (quality > 0.0f && L.z > 0.03f)
		{
			float invLz = rcp(max(L.z, 0.20f));
			float2 rayDir = L.xy * invLz * (0.070f * params.HeightScale * MaterialLayersTuning::ObjectShadowRayScale());
			float4 multipliers = rcp(float4(1.0f, 2.0f, 3.0f, 4.0f) + noise.xxxx);
			float4 sh = sh0.xxxx;

			sh.x = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
			if (quality > 0.25f)
				sh.y = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
			if (quality > 0.50f)
				sh.z = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
			if (quality > 0.75f)
				sh.w = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);

			// The old implementation summed every positive height difference. Repeated
			// taps over the same ridge therefore accumulated into solid black islands.
			// Use a small receiver bias, weighted coverage and only a modest peak term.
			float heightBias = max(0.008f, 0.018f * abs(params.HeightScale)) * MaterialLayersTuning::ObjectShadowBias();
			float4 delta = max(sh - sh0.xxxx - heightBias.xxxx, 0.0f);
			float4 occ = saturate(delta * (ShadowIntensity * 1.65f));

			float4 active = float4(1.0f,
				quality > 0.25f ? 1.0f : 0.0f,
				quality > 0.50f ? 1.0f : 0.0f,
				quality > 0.75f ? 1.0f : 0.0f);
			float4 weights = float4(0.44f, 0.28f, 0.18f, 0.10f) * active;
			float weightSum = max(dot(weights, 1.0f.xxxx), 1e-4f);
			float average = dot(occ, weights) / weightSum;
			float peak = max(max(occ.x, occ.y), max(occ.z, occ.w));
			float coherence = smoothstep(0.04f, 0.30f, average);
			float occlusion = lerp(average, peak, coherence * 0.22f);
			float grazingGate = smoothstep(0.03f, 0.18f, L.z);
			float qualityScale = lerp(0.72f, 1.0f, saturate(quality));

			shadowMultiplier = 1.0f - saturate(
				occlusion * coherence * grazingGate * qualityScale *
				min(MaterialLayersTuning::ObjectShadowStrength(), 1.5f));
		}
		return shadowMultiplier;
	}

#	endif

#endif  // MATERIAL_LAYERS_PARALLAX_CORE_HLSLI
