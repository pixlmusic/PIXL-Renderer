Texture2D<float4> WaterCaustics : register(t65);

#ifndef USE_PIXL_PHYSICAL_CAUSTICS
#define USE_PIXL_PHYSICAL_CAUSTICS 1
#endif

#ifndef USE_PIXL_JACOBIAN_CAUSTICS
#define USE_PIXL_JACOBIAN_CAUSTICS 1
#endif

namespace WaterOptics
{
	// The authored atlas is an energy mask whose average is below one. Receiver
	// paths must compare against that normalized energy, not against raw display
	// values, otherwise the wall/ceiling path is effectively always zero.
	static const float CausticAtlasMean = 0.1401f;

	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed) + (uv * tiling));
	}

	float2 PanCausticsUV(float2 uv, float2 velocity, float tiling)
	{
		// World-stable two-dimensional advection keeps the projection from reading
		// like a one-axis scrolling decal. Different layers use different flows.
		return frac(uv * tiling + velocity * SharedData::Timer);
	}

	float SampleCaustics(float2 uv)
	{
		return WaterCaustics.Sample(SampColorSampler, uv).x;
	}

	// Approximate wavelength-dependent refraction by offsetting red/blue around green.
	float3 SampleCausticsDispersion(float2 uv, float2 dispersionOffset)
	{
		float center = SampleCaustics(uv);
		float3 dispersed = float3(
			SampleCaustics(uv - dispersionOffset * 0.75),
			center,
			SampleCaustics(uv + dispersionOffset));
		return lerp(center.xxx, dispersed, 0.5);
	}

	float3 SampleFocusedCaustics(float2 uv, float2 dispersionOffset)
	{
		// Reconstruct a compact focusing proxy from local density curvature. This is
		// the texture-space analogue of measuring the area change of a refracted ray
		// footprint, with a bounded response for stable HDR output.
		const float2 tap = float2(0.0017f, 0.0023f);
		float center = SampleCaustics(uv);
		float samplePX = SampleCaustics(uv + float2(tap.x, 0.0f));
		float sampleNX = SampleCaustics(uv - float2(tap.x, 0.0f));
		float samplePY = SampleCaustics(uv + float2(0.0f, tap.y));
		float sampleNY = SampleCaustics(uv - float2(0.0f, tap.y));
		float filtered = center * 0.50f + (samplePX + sampleNX + samplePY + sampleNY) * 0.125f;
		float curvature = saturate(abs(4.0f * center - samplePX - sampleNX - samplePY - sampleNY) * 1.5f);
		float focus = lerp(1.0f, 1.0f + curvature * 1.75f, saturate(SharedData::waterOpticsSettings.CausticsFocus * 0.5f));
		float3 dispersed = SampleCausticsDispersion(uv, dispersionOffset);
		float3 antiAliased = lerp(filtered.xxx, dispersed, 0.65f);
		// Preserve mid-frequency energy. Squaring the texture here suppressed most
		// authored folds before the two animated layers were combined.
		return saturate(antiAliased * focus);
	}

	float3 CombineCausticLayers(float3 firstLayer, float3 secondLayer)
	{
		// Crossing wave trains should reinforce at their intersections. A max-biased
		// blend exposes those folds without the flicker of a pure maximum.
		return lerp(min(firstLayer, secondLayer), max(firstLayer, secondLayer), 0.72f);
	}

	float3 SampleMovingReceiverCaustics(float2 uv, float2 dispersionOffset, float2 velocity, float tiling, float seed)
	{
		float2 animatedUV = PanCausticsUV(uv, velocity, tiling);
		float3 sharp = SampleFocusedCaustics(animatedUV, dispersionOffset);
		// Blend a lower-frequency footprint with the focused folds. The focus
		// modulation is deterministic in world space and therefore stable through
		// temporal reconstruction while still mimicking changing water magnification.
		float phase = dot(uv, float2(0.071f, 0.053f)) + SharedData::Timer * (0.19f + seed * 0.07f);
		float focus = lerp(0.64f, 0.98f, 0.5f + 0.5f * sin(phase));
		float3 soft = SampleCausticsDispersion(
			PanCausticsUV(uv + float2(0.013f, -0.009f), velocity * 0.42f, tiling * 0.46f),
			dispersionOffset * 0.45f);
		return lerp(soft, sharp, focus);
	}

	float3 ComputeCaustics(float4 waterData, float3 worldPosition, float3 receiverNormal)
	{
		float3 result = 1.0.xxx;
		float causticsDistToWater = waterData.w - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);
		float waterDataValid = waterData.w > -1.0e20f ? 1.0f : 0.0f;

		// Reference-style receiver caustics: the water surface is treated as a
		// moving refractive projector, so a nearby wall/rock/shore receiver can
		// receive a soft reflected pattern instead of only the submerged path. This
		// uses PIXL's existing world-stable caustic atlas and water tile lookup; it
		// does not add a second zone buffer or per-light draw pass.
		if (SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0 &&
			waterDataValid > 0.0f && causticsDistToWater < 0.0f)
		{
			float receiverHeight = -causticsDistToWater;
			float receiverFade = 1.0f - smoothstep(4.0f, 1200.0f, receiverHeight);
			if (receiverFade > 0.0f)
			{
				float3 sunDirection = normalize(SharedData::DirLightDirection.xyz);
				float sunElevation = smoothstep(0.03f, 0.24f, sunDirection.z);
				float2 absolutePosition = worldPosition.xy + FrameBuffer::CameraPosAdjust.xy;
				float2 projectedPosition = absolutePosition - sunDirection.xy *
					receiverHeight / max(sunDirection.z, 0.20f);
				float2 receiverUV = projectedPosition * 0.0042f;
				float2 receiverDispersion = float2(0.55f, 0.80f) *
					(0.012f * saturate(SharedData::waterOpticsSettings.CausticsDispersion));
				float3 receiverLayerA = SampleMovingReceiverCaustics(
					receiverUV, receiverDispersion, normalize(float2(0.73f, 0.41f)) * 0.095f, 1.0f, 0.0f);
				float3 receiverLayerB = SampleMovingReceiverCaustics(
					receiverUV, receiverDispersion, normalize(float2(-0.37f, 0.82f)) * -0.067f, -0.57f, 1.0f);
				float3 receiverLayerC = SampleMovingReceiverCaustics(
					receiverUV * 0.52f + float2(0.17f, 0.31f), receiverDispersion * 0.35f,
					normalize(float2(0.19f, -0.61f)) * 0.041f, 1.0f, 2.0f);
				float3 receiverPattern = CombineCausticLayers(
					CombineCausticLayers(receiverLayerA, receiverLayerB), receiverLayerC);
				float normalResponse = lerp(0.30f, 1.0f,
					saturate(abs(dot(normalize(receiverNormal), sunDirection))));
				float depthResponse = exp(-receiverHeight / 720.0f);
				// Receiver caustics are a lighting multiplier, not a bloom layer. Keep
				// the broad fold response visible on indoor floors while retaining a
				// bounded highlight-only contribution.
				float3 receiverEnergy = receiverPattern / max(CausticAtlasMean, 1e-4f);
				float3 contrast = max(receiverEnergy - 0.38f.xxx, 0.0f.xxx);
				float3 broadEnergy = saturate(receiverEnergy * 0.30f);
				float boost = dot(contrast, float3(0.2126f, 0.7152f, 0.0722f)) *
					SharedData::waterOpticsSettings.CausticsVisibility *
					SharedData::waterOpticsSettings.CausticsStrength *
					 receiverFade * depthResponse * sunElevation * normalResponse * 0.34f;
				boost += dot(broadEnergy, float3(0.2126f, 0.7152f, 0.0722f)) *
					SharedData::waterOpticsSettings.CausticsVisibility *
					SharedData::waterOpticsSettings.CausticsStrength *
					receiverFade * depthResponse * sunElevation * normalResponse * 0.18f;
				result *= 1.0f + min(boost, 1.15f);
			}
		}

		if (shoreFactorCaustics > 0.0) {
			float causticsFade = 1.0 - saturate(causticsDistToWater / 1024.0);
			causticsFade *= causticsFade;

			float3 sunDirection = normalize(SharedData::DirLightDirection.xyz);
			float sunElevation = saturate(sunDirection.z * 4.0f);
			float2 absolutePosition = worldPosition.xy + FrameBuffer::CameraPosAdjust.xy;
		#if USE_PIXL_JACOBIAN_CAUSTICS
			if (SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0) {
				// Project the water-surface footprint along the sun ray so the caustic
				// pattern slides naturally with depth and solar elevation.
				absolutePosition -= sunDirection.xy * (causticsDistToWater / max(sunDirection.z, 0.18f));
			}
		#endif
			float2 causticsUV = absolutePosition * 0.005;
			float dispersionStrength = 0.5f;
		#if USE_PIXL_JACOBIAN_CAUSTICS
			if (SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0)
				dispersionStrength = SharedData::waterOpticsSettings.CausticsDispersion;
		#endif
			float2 dispersionOffset = float2(0.6, 0.8) * (0.025 * shoreFactorCaustics * saturate(causticsDistToWater / 256.0) * dispersionStrength);

			float2 causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.2, 1.0);
			float2 causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.2, -0.5);

			const float3 causticsHigh =
				(causticsFade > 0.0)
			#if USE_PIXL_PHYSICAL_CAUSTICS
					? (CombineCausticLayers(SampleFocusedCaustics(causticsUV1, dispersionOffset), SampleFocusedCaustics(causticsUV2, dispersionOffset)) * 3.75)
			#else
					? (min(SampleCausticsDispersion(causticsUV1, dispersionOffset), SampleCausticsDispersion(causticsUV2, dispersionOffset)) * 4.0)
			#endif
					: 1.0.xxx;

			causticsUV *= 0.5;
			dispersionOffset *= 0.5;

			causticsUV1 = PanCausticsUV(causticsUV, 0.5 * 0.1, 1.0);
			causticsUV2 = PanCausticsUV(causticsUV, 1.0 * 0.1, -0.5);

			const float3 causticsLow =
				(causticsFade < 1.0)
			#if USE_PIXL_PHYSICAL_CAUSTICS
					? (CombineCausticLayers(SampleFocusedCaustics(causticsUV1, dispersionOffset), SampleFocusedCaustics(causticsUV2, dispersionOffset)) * 3.75)
			#else
					? (min(SampleCausticsDispersion(causticsUV1, dispersionOffset), SampleCausticsDispersion(causticsUV2, dispersionOffset)) * 4.0)
			#endif
					: 1.0.xxx;

			const float3 caustics = lerp(causticsLow, causticsHigh, causticsFade);
		#if USE_PIXL_PHYSICAL_CAUSTICS
			// Beer-Lambert extinction prevents implausibly bright caustics in deep water.
			// Skyrim exterior units are approximately 70 units per metre.
			float depthMetres = max(causticsDistToWater, 0.0) / 70.0;
			float3 waterTransmittance = exp(-float3(0.055, 0.028, 0.018) * depthMetres);
			float strength = 1.0f;
		#if USE_PIXL_JACOBIAN_CAUSTICS
			if (SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0)
				strength = SharedData::waterOpticsSettings.CausticsStrength;
		#endif
			float visibility = max(SharedData::waterOpticsSettings.CausticsVisibility, 0.0f);
			// Keep a small daylight floor for low-elevation sun and overcast weather;
			// directional light colour still governs the actual delivered energy.
			float daylightVisibility = lerp(0.28f, 1.0f, sunElevation);
			float3 centeredCaustics = 1.0f.xxx + (clamp(caustics, 0.45f.xxx, 2.85f.xxx) - 1.0f.xxx) * visibility;
			// Intensity is deliberately allowed above 1.0. The former saturate()
			// made the upper half of the 0..2 UI slider a no-op.
			float causticsIntensity = clamp(strength, 0.0f, 2.0f);
			float3 energyBoundedCaustics = lerp(1.0.xxx, clamp(centeredCaustics, 0.50f.xxx, 3.0f.xxx), causticsIntensity * daylightVisibility);
			energyBoundedCaustics = clamp(energyBoundedCaustics, 0.35f.xxx, 3.25f.xxx);
			energyBoundedCaustics = lerp(1.0.xxx, energyBoundedCaustics, waterTransmittance);
			result = lerp(1.0.xxx, energyBoundedCaustics, shoreFactorCaustics);
		#else
			result = lerp(1.0.xxx, caustics, shoreFactorCaustics);
		#endif
		}

		return result;
	}

	float3 ComputeLocalReceiverCaustics(
		float4 waterData,
		float3 worldPosition,
		float3 receiverNormal,
		float3 lightPosition,
		float3 lightColor,
		float lightRadius)
	{
		if (SharedData::waterOpticsSettings.EnableEnhancedCaustics == 0 ||
			waterData.w <= -1.0e20f)
			return 1.0.xxx;

		float waterHeight = waterData.w;
		float receiverHeight = worldPosition.z - waterHeight;
		float lightHeight = lightPosition.z - waterHeight;
		// Skyrim's water grid is coarse and many indoor emitters sit just above
		// the water plane. The stricter reference exclusion band made this path
		// disappear for cave floors, shallow pools and particle lights.
		if (receiverHeight < 2.0f || receiverHeight > 560.0f || lightHeight <= 0.5f)
			return 1.0.xxx;

		// Mirror the local emitter beneath the water plane. The line from that
		// virtual emitter to the receiver intersects the surface at the point whose
		// animated atlas pattern is reflected onto the nearby wall/floor.
		float3 mirroredLight = float3(lightPosition.xy, 2.0f * waterHeight - lightPosition.z);
		float3 receiverRay = worldPosition - mirroredLight;
		float safeRayZ = abs(receiverRay.z) > 1.0e-4f ? receiverRay.z : 1.0e-4f;
		float planeT = (waterHeight - mirroredLight.z) / safeRayZ;
		if (planeT <= 0.0f || planeT >= 1.0f)
			return 1.0.xxx;

		float3 waterPoint = mirroredLight + receiverRay * planeT;
		float sourcePath = length(waterPoint - lightPosition);
		// Skyrim point-light radii describe useful illumination, not the full
		// projected footprint of a refracted emitter. Expand the caustic footprint
		// modestly so torches and magic lights can reach a nearby cave floor without
		// turning the entire water volume into a light decal.
		float radius = max(lightRadius * 1.75f, 96.0f);
		float sourceCoverage = 1.0f - smoothstep(radius * 0.20f, radius * 1.45f, sourcePath);
		float receiverFade = 1.0f - smoothstep(4.0f, 1200.0f, receiverHeight);
		float3 reflectedDirection = normalize(worldPosition - waterPoint);
		// Oblique walls and ceilings can still receive refracted light. Retain a
		// small grazing response without turning the effect into a flat decal.
		float normalResponse = lerp(0.22f, 1.0f,
			saturate(abs(dot(normalize(receiverNormal), reflectedDirection))));
		float sourceLuminance = dot(max(lightColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		if (sourceCoverage <= 0.0f || receiverFade <= 0.0f || normalResponse <= 0.0f || sourceLuminance <= 1.0e-4f)
			return 1.0.xxx;

		float2 projectedUV = waterPoint.xy * 0.0042f;
		float2 dispersion = float2(0.55f, 0.80f) *
			(0.012f * saturate(SharedData::waterOpticsSettings.CausticsDispersion));
		float3 layerA = SampleMovingReceiverCaustics(
			projectedUV, dispersion, normalize(float2(0.73f, 0.41f)) * 0.12f, 1.0f, 0.0f);
		float3 layerB = SampleMovingReceiverCaustics(
			projectedUV, dispersion, normalize(float2(-0.37f, 0.82f)) * -0.085f, -0.57f, 1.0f);
		float3 layerC = SampleMovingReceiverCaustics(
			projectedUV * 0.52f + float2(0.17f, 0.31f), dispersion * 0.35f,
			normalize(float2(0.19f, -0.61f)) * 0.047f, 1.0f, 2.0f);
		float3 pattern = CombineCausticLayers(CombineCausticLayers(layerA, layerB), layerC);
		// Keep the local-light receiver path in the same normalized energy space.
		// Comparing the raw atlas texel to a unit threshold made torches and
		// particle lights appear to do nothing near indoor water.
		float3 receiverEnergy = pattern / max(CausticAtlasMean, 1e-4f);
		// Keep a broad low-energy component. The previous bright-fold-only
		// threshold discarded nearly every texel in the authored atlas.
		float3 contrast = max(receiverEnergy - 0.38f.xxx, 0.0f.xxx);
		float3 broadEnergy = saturate(receiverEnergy * 0.30f);
		float patternLuminance = dot(contrast + broadEnergy, float3(0.2126f, 0.7152f, 0.0722f));
		float boost = patternLuminance *
			SharedData::waterOpticsSettings.CausticsVisibility *
			SharedData::waterOpticsSettings.CausticsStrength *
			sourceCoverage * receiverFade * normalResponse * 0.90f;
		// This is a light multiplier, never an emissive overlay.
		return 1.0.xxx + min(boost, 1.35f).xxx;
	}
}
