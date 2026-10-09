Texture2D<float4> WaterCaustics : register(t65);

#ifndef USE_PIXL_PHYSICAL_CAUSTICS
#define USE_PIXL_PHYSICAL_CAUSTICS 1
#endif

#ifndef USE_PIXL_JACOBIAN_CAUSTICS
#define USE_PIXL_JACOBIAN_CAUSTICS 1
#endif

namespace WaterOptics
{
	// Capture derivatives before divergent local-light loops. Implicit texture
	// gradients inside those loops are undefined and fail strict shader validation.
	static float2 ReceiverCausticDx = 0.0f.xx;
	static float2 ReceiverCausticDy = 0.0f.xx;
	void SetReceiverFootprint(float3 worldPosition)
	{
		ReceiverCausticDx = ddx_coarse(worldPosition.xy) * 0.005f;
		ReceiverCausticDy = ddy_coarse(worldPosition.xy) * 0.005f;
	}
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

	float2 WarpReceiverUV(float2 waterWorldXY, float2 uv, float receiverHeight)
	{
		// A cheap water-normal proxy: two slow, world-anchored waves bend the
		// projected footprint without sampling the water material on every receiver.
		float2 phase = float2(
			dot(waterWorldXY, float2(0.012f, 0.007f)),
			dot(waterWorldXY, float2(-0.006f, 0.015f)));
		float2 bend = sin(phase + SharedData::Timer * float2(0.43f, -0.31f));
		return uv + bend * lerp(0.003f, 0.010f, saturate(receiverHeight / 300.0f));
	}

	float SampleCaustics(float2 uv)
	{
		return WaterCaustics.SampleGrad(SampColorSampler, uv, ReceiverCausticDx, ReceiverCausticDy).x;
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
		float waterDataValid = waterData.w > -1.0e8f ? 1.0f : 0.0f;

		// WaterData exposes the tile height rather than a per-pixel water footprint.
		// Keep the useful above-water projection, but limit it to a short receiver
		// zone and fade by the projected footprint so it cannot become a world-sized
		// decal on snow or terrain elsewhere in the tile.
		const float maxAboveWaterHeight = clamp(
			SharedData::waterOpticsSettings.ProjectedCausticsDistance, 64.0f, 800.0f);
		if (causticsDistToWater < 0.0f && waterDataValid > 0.0f &&
			SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0) {
			float receiverHeight = -causticsDistToWater;
			float3 sunDirection = normalize(SharedData::DirLightDirection.xyz);
			float safeSunZ = max(sunDirection.z, 0.38f);
			float2 projectedOffset = sunDirection.xy * (receiverHeight / safeSunZ);
			float reachScale = maxAboveWaterHeight / 300.0f;
			float heightFade = 1.0f - smoothstep(max(16.0f, 16.0f * reachScale), maxAboveWaterHeight, receiverHeight);
			float footprintFade = 1.0f - smoothstep(64.0f * reachScale, 260.0f * reachScale, length(projectedOffset));
			float receiverFade = heightFade * footprintFade;
			float3 reflectedDirection = normalize(float3(-sunDirection.xy, max(sunDirection.z, 0.0f)));
			float3 normal = normalize(receiverNormal);
			// Water-bounced light arrives from below. Upward terrain is not a receiver.
			float normalResponse = saturate(dot(normal, -reflectedDirection)) *
				(1.0f - smoothstep(0.02f, 0.35f, normal.z));
			float sunVisibility = saturate(sunDirection.z * 4.0f);
			if (receiverFade > 0.0f && normalResponse > 0.0f && sunVisibility > 0.0f) {
				float2 absolutePosition = worldPosition.xy + FrameBuffer::CameraPosAdjust.xy + projectedOffset;
				float2 causticsUV = WarpReceiverUV(absolutePosition, absolutePosition * 0.005f, receiverHeight);
				float2 dispersionOffset = float2(0.6f, 0.8f) *
					(0.018f * saturate(receiverHeight / maxAboveWaterHeight));
				float2 causticsUV1 = PanCausticsUV(causticsUV, 0.10f, 1.0f);
				float2 causticsUV2 = PanCausticsUV(causticsUV, 0.20f, -0.5f);
				float3 caustics = CombineCausticLayers(
					SampleFocusedCaustics(causticsUV1, dispersionOffset),
					SampleFocusedCaustics(causticsUV2, dispersionOffset));
				float softness = saturate(receiverHeight / maxAboveWaterHeight) * 0.30f;
				float broad = SampleCaustics(PanCausticsUV(causticsUV * 0.58f, 0.05f, 1.0f));
				caustics = lerp(caustics, broad.xxx, softness);
				float strength = clamp(SharedData::waterOpticsSettings.ProjectedCausticsStrength, 0.0f, 3.0f);
				float3 folds = max(caustics / CausticAtlasMean - 0.72f.xxx, 0.0f.xxx);
				result = 1.0f.xxx + min(folds * strength * receiverFade *
					normalResponse * sunVisibility * 0.34f, 1.25f.xxx);
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
		float projectedReach = clamp(SharedData::waterOpticsSettings.ProjectedCausticsDistance, 64.0f, 800.0f);
		if (receiverHeight > projectedReach || receiverHeight < -560.0f || lightHeight <= 0.5f)
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
		// The reflected footprint must still belong to the same known water plane.
		// A receiver tile alone can project light across a neighbouring dry cell.
		float4 footprintWater = SharedData::GetWaterData(waterPoint);
		if (footprintWater.w < -1.0e8f || abs(footprintWater.w - waterHeight) > 8.0f)
			return 1.0.xxx;
		float sourcePath = length(waterPoint - lightPosition);
		// Skyrim point-light radii describe useful illumination, not the full
		// projected footprint of a refracted emitter. Expand the caustic footprint
		// modestly so torches and magic lights can reach a nearby cave floor without
		// turning the entire water volume into a light decal.
		float radius = max(lightRadius * 1.25f, 40.0f);
		float sourceCoverage = 1.0f - smoothstep(radius * 0.20f, radius * 1.20f, sourcePath);
		float receiverFade = receiverHeight >= 0.0f
			? 1.0f - smoothstep(max(16.0f, 16.0f * projectedReach / 300.0f), projectedReach, receiverHeight)
			: 1.0f;
		float3 reflectedDirection = normalize(worldPosition - waterPoint);
		float3 normal = normalize(receiverNormal);
		float normalResponse = receiverHeight >= 0.0f
			? saturate(dot(normal, -reflectedDirection)) * (1.0f - smoothstep(0.02f, 0.35f, normal.z))
			: lerp(0.22f, 1.0f, saturate(abs(dot(normal, reflectedDirection))));
		float sourceLuminance = dot(max(lightColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		if (sourceCoverage <= 0.0f || receiverFade <= 0.0f || normalResponse <= 0.0f || sourceLuminance <= 1.0e-4f)
			return 1.0.xxx;

		float2 absoluteWaterPoint = waterPoint.xy + FrameBuffer::CameraPosAdjust.xy;
		float2 projectedUV = absoluteWaterPoint * 0.0042f;
		if (receiverHeight > 0.0f)
			projectedUV = WarpReceiverUV(absoluteWaterPoint, projectedUV, receiverHeight);
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
		float receiverStrength = receiverHeight >= 0.0f
			? clamp(SharedData::waterOpticsSettings.ProjectedCausticsStrength, 0.0f, 3.0f)
			: SharedData::waterOpticsSettings.CausticsVisibility * SharedData::waterOpticsSettings.CausticsStrength;
		float boost = patternLuminance * receiverStrength *
			sourceCoverage * receiverFade * normalResponse * 0.90f;
		// This is a light multiplier, never an emissive overlay.
		return 1.0.xxx + min(boost, 1.35f).xxx;
	}
}
