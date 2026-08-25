Texture2D<float4> WaterCaustics : register(t65);

#ifndef USE_PIXL_PHYSICAL_CAUSTICS
#define USE_PIXL_PHYSICAL_CAUSTICS 1
#endif

#ifndef USE_PIXL_JACOBIAN_CAUSTICS
#define USE_PIXL_JACOBIAN_CAUSTICS 1
#endif

namespace WaterOptics
{
	float2 PanCausticsUV(float2 uv, float speed, float tiling)
	{
		return frac((float2(1, 0) * SharedData::Timer * speed) + (uv * tiling));
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

	float3 ComputeCaustics(float4 waterData, float3 worldPosition)
	{
		float3 result = 1.0.xxx;
		float causticsDistToWater = waterData.w - worldPosition.z;
		float shoreFactorCaustics = saturate(causticsDistToWater / 64.0);

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
}
