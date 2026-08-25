#ifndef __PIXL_RAIN_RESPONSE_PRECIPITATION_HLSLI__
#define __PIXL_RAIN_RESPONSE_PRECIPITATION_HLSLI__

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

// Lightweight precipitation helpers shared by Skyrim's particle rain shader and
// PIXL's volumetric atmosphere.  This file deliberately never replaces the
// active weather texture/material: it only shapes the geometry/optical response
// already supplied by the game or weather mod.
namespace RainResponse
{
	static const float PIXL_RAIN_TAU = 6.28318530717958647692f;

	float RainHash12(float2 p)
	{
		float3 p3 = frac(p.xyx * 0.1031f);
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float RainValueNoise2D(float2 p)
	{
		float2 i = floor(p);
		float2 f = frac(p);
		float2 u = f * f * (3.0f - 2.0f * f);

		float a = RainHash12(i + float2(0.0f, 0.0f));
		float b = RainHash12(i + float2(1.0f, 0.0f));
		float c = RainHash12(i + float2(0.0f, 1.0f));
		float d = RainHash12(i + float2(1.0f, 1.0f));

		return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
	}

	float RainFBM2D(float2 p)
	{
		// Three inexpensive octaves are enough for broad storm curtains while
		// staying cheap in the precipitation VS and volumetric material pass.
		float n = RainValueNoise2D(p) * 0.5714286f;
		p = p * 2.03f + float2(17.17f, 9.31f);
		n += RainValueNoise2D(p) * 0.2857143f;
		p = p * 2.01f + float2(-11.73f, 23.41f);
		n += RainValueNoise2D(p) * 0.1428571f;
		return saturate(n);
	}

	float2 GetRainTravelDirection(float3 authoredVelocity)
	{
		// Skyrim's precipitation velocity already contains the weather-authored
		// lateral tendency.  Prefer it so PIXL follows custom weather motion.
		float2 lateral = authoredVelocity.xy;
		float lateralSq = dot(lateral, lateral);
		if (lateralSq > 1e-5f)
			return lateral * rsqrt(lateralSq);

		// Calm/vertical rain still needs a stable direction for rare gust sheets.
		// Keep this in world space (never camera space), so looking around cannot
		// rotate the rain pattern.
		return normalize(float2(0.79f, 0.61f));
	}

	struct RainParticleDynamics
	{
		float3 velocity;
		float clump;
		float gust;
		float slope;
		float selector;
	};

	RainParticleDynamics EvaluateRainParticleDynamics(float3 cameraRelativePosition, float3 authoredVelocity)
	{
		RainParticleDynamics result;
		result.velocity = authoredVelocity;
		result.clump = 1.0f;
		result.gust = 0.0f;
		result.slope = 0.0f;
		result.selector = 0.5f;

		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return result;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		if (rain <= 1e-4f)
			return result;

		float3 absolutePosition = cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;
		float2 travelDir = GetRainTravelDirection(authoredVelocity);
		float2 crossDir = float2(-travelDir.y, travelDir.x);

		float clumpSize = max(SharedData::rainResponseSettings.RainClumpSize, 128.0f);
		float2 fieldDrift = travelDir * SharedData::rainResponseSettings.Time * lerp(0.035f, 0.10f, rain);
		float2 fieldUV = absolutePosition.xy / clumpSize - fieldDrift;
		float densityNoise = RainFBM2D(fieldUV);
		float curtainNoise = RainValueNoise2D(fieldUV * float2(0.43f, 1.17f) + 31.7f);
		float densityField = saturate(densityNoise * 0.72f + curtainNoise * 0.38f);

		// Dense bands are allowed to become a little stronger than 1 while gaps
		// retain enough primary precipitation to avoid obvious particle popping.
		float clumpTarget = lerp(0.42f, 1.46f, smoothstep(0.12f, 0.90f, densityField));
		result.clump = lerp(1.0f, clumpTarget,
			saturate(SharedData::rainResponseSettings.RainClumpStrength) * rain);

		float2 populationCell = floor(absolutePosition.xy / 96.0f);
		float particleSelector = RainHash12(populationCell + float2(13.0f, 71.0f));
		float variationHash = RainHash12(populationCell + float2(-29.0f, 43.0f));
		result.selector = particleSelector;

		float variation = saturate(SharedData::rainResponseSettings.RainStreakVariation);
		float velocityScale = lerp(1.0f - 0.28f * variation, 1.0f + 0.42f * variation, variationHash);
		result.velocity *= velocityScale;

		float gustFrequency = max(SharedData::rainResponseSettings.RainGustFrequency, 0.001f);
		float gustPhase = SharedData::rainResponseSettings.Time * gustFrequency * PIXL_RAIN_TAU +
			dot(absolutePosition.xy, travelDir) / max(clumpSize * 1.8f, 1.0f);
		float temporalPulse = smoothstep(0.48f, 0.94f, 0.5f + 0.5f * sin(gustPhase));
		float spatialPulse = smoothstep(0.48f, 0.88f,
			RainValueNoise2D(absolutePosition.xy / (clumpSize * 1.65f) - fieldDrift * 0.67f));

		float populationChance = saturate(SharedData::rainResponseSettings.RainGustChance);
		float selected = 1.0f - step(populationChance, particleSelector);
		float gust = temporalPulse * spatialPulse * selected * rain *
			max(SharedData::rainResponseSettings.RainGustStrength, 0.0f);
		result.gust = saturate(gust);

		float authoredSpeed = max(length(authoredVelocity), 1e-3f);
		float crossBias = (variationHash * 2.0f - 1.0f) * 0.38f;
		float2 gustDir = normalize(travelDir + crossDir * crossBias);
		result.velocity.xy += gustDir * authoredSpeed * gust * 0.92f;

		// Passed to the PS for the second, sheared copy of the active weather
		// streak.  Sign/strength varies per world-stable population cell.
		float slopeSign = variationHash < 0.5f ? -1.0f : 1.0f;
		result.slope = slopeSign * lerp(0.55f, 1.35f, variationHash) * result.gust;
		return result;
	}

	float GetRainDistanceWeight(float viewDistance)
	{
		float depthStart = max(SharedData::rainResponseSettings.RainDepthStart, 0.0f);
		float depthEnd = max(SharedData::rainResponseSettings.RainDepthEnd, depthStart + 1.0f);
		float d = max(viewDistance, 0.0f);

		float mid = smoothstep(depthStart * 0.28f, depthEnd * 0.50f, d);
		float far = smoothstep(depthStart * 0.58f, depthEnd * 0.94f, d);
		return saturate(mid * 0.70f + far * far * 0.72f);
	}

	float GetRainDistanceVisibility(float viewDistance)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 1.0f;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		float weight = GetRainDistanceWeight(viewDistance);
		float boost =
			clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f) *
			rain;

		// Give the control visible leverage through the whole precipitation volume,
		// while still making the far field substantially stronger than near rain.
		return 1.0f + boost * (0.22f + weight * 3.10f);
	}

	float GetRainFinalAlphaFloor(float sourceAlpha, float clump, float viewDistance)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 0.0f;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		float weight = GetRainDistanceWeight(viewDistance);
		float boost =
			clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f);

		// Fine authored rain often uses very low alpha. sqrt preserves zero while
		// recovering the thin texels that TAA/DLSS otherwise erases.
		float authoredVisibility = sqrt(saturate(sourceAlpha));
		float distanceResponse = lerp(0.22f, 1.0f, weight);
		float floorStrength = saturate(0.12f + boost * 0.40f);

		return saturate(
			authoredVisibility *
			max(clump, 0.48f) *
			distanceResponse *
			floorStrength *
			rain);
	}

	float GetRainFarShellScale(float normalizedRadius, float selector)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 1.0f;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		float boost = clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f);
		float outer = smoothstep(0.22f, 0.90f, saturate(normalizedRadius));
		float selected = smoothstep(0.42f, 0.94f, selector);
		return 1.0f + outer * lerp(0.35f, 0.85f, selected) * boost * rain;
	}

	float GetRainLightingMultiplier(float viewDistance)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 1.0f;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		float weight = GetRainDistanceWeight(viewDistance);
		float lightBoost =
			max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f) * rain;
		float distanceBoost =
			clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f) *
			rain;

		return 1.0f +
			lightBoost * lerp(0.20f, 1.95f, weight) +
			distanceBoost * weight * 0.62f;
	}

	float GetRainDirectionalScatter(float3 viewDirection)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 1.0f;

		float3 V = normalize(viewDirection);
		float3 lightAxis = normalize(SharedData::DirLightDirection.xyz);

		// We deliberately use |dot| because Skyrim/weather pipelines can encode the
		// directional vector as either light travel direction or direction-to-light.
		// For a thin water streak the useful cue is axis alignment either way.
		float lightAlignment = saturate(abs(dot(V, lightAxis)));
		float forwardLobe = pow(lightAlignment, 5.0f);

		// Rain also reads better when viewed roughly across the world-up axis.
		float horizontalView =
			pow(saturate(1.0f - abs(V.z)), 1.5f);

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		float boost =
			max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f);

		return 1.0f +
			rain *
			(forwardLobe * (0.18f + boost * 0.70f) +
			 horizontalView * (0.04f + boost * 0.14f));
	}

	float GetRainLightAlignment(float3 viewDirection, float3 lightDirection)
	{
		float3 V = normalize(viewDirection);
		float3 L = normalize(lightDirection);
		return pow(saturate(abs(dot(V, L))), 5.0f);
	}

	float GetRainSecondaryLayerWeight(float gust, float selector)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0)
			return 0.0f;

		// Gust cells are selected from selector < RainGustChance. Remap that selected
		// interval instead of testing the high end of selector (which would suppress
		// the entire secondary population at normal gust chances).
		float populationRange = max(SharedData::rainResponseSettings.RainGustChance, 1e-3f);
		float layerSelection = lerp(0.55f, 1.0f, saturate(selector / populationRange));
		float gustWeight = smoothstep(0.08f, 0.72f, saturate(gust));
		return saturate(SharedData::rainResponseSettings.RainSecondaryLayerStrength) *
			layerSelection * gustWeight * saturate(SharedData::rainResponseSettings.Raining);
	}

	float2 GetRainSecondaryUV(float2 uv, float slope, float selector)
	{
		float2 result = uv;
		float localY = uv.y - 0.5f;
		result.x += localY * slope * 0.78f;
		result.x += (selector - 0.5f) * 0.10f;
		result.y += (0.5f - selector) * 0.035f;
		return result;
	}

	float IsRainUVInside(float2 uv)
	{
		return step(0.0f, uv.x) * step(uv.x, 1.0f) * step(0.0f, uv.y) * step(uv.y, 1.0f);
	}

	float EvaluateRainImpactProximity(float2 screenUV, float rainDeviceDepth, float selector)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0 ||
			SharedData::rainResponseSettings.RainImpactSplashStrength <= 0.0f)
			return 0.0f;

		// Compare values produced by the exact same depth linearisation. Phase 1
		// reconstructed the particle and transformed it back to view space, which
		// mixed coordinate conventions in some permutations and could miss impacts.
		float sceneDepth = SharedData::GetScreenDepth(screenUV);
		float rainDepth = SharedData::GetScreenDepth(rainDeviceDepth);
		if (sceneDepth <= 0.0f || rainDepth <= 0.0f)
			return 0.0f;

		float gap = sceneDepth - rainDepth;
		float thickness = clamp(rainDepth * 0.018f, 55.0f, 150.0f);
		float proximity = step(0.0f, gap) * (1.0f - smoothstep(4.0f, thickness, gap));

		// Short asynchronous bursts avoid a permanent bright contact line while
		// remaining long enough to survive temporal reconstruction.
		float phase = frac(SharedData::rainResponseSettings.Time * 10.5f + selector * 17.13f);
		float pulse = 1.0f - smoothstep(0.14f, 0.66f, phase);
		return proximity * pulse * max(SharedData::rainResponseSettings.RainImpactSplashStrength, 0.0f) *
			saturate(SharedData::rainResponseSettings.Raining);
	}

	float EvaluateRainImpactSprite(float2 uv, float slope, float selector)
	{
		// Put the crown at the lower end of the streak. Phase 1 drew it around card
		// centre, making it detached from contact and easy to bury in transparent RGB.
		float2 p = uv - float2(0.5f, 0.79f);
		float spread = lerp(0.52f, 0.88f, selector);
		float riseWindow = smoothstep(-0.48f, -0.36f, p.y) * (1.0f - smoothstep(-0.02f, 0.06f, p.y));
		float rise = -p.y;
		float lineA = 1.0f - saturate(abs(p.x - rise * (spread + abs(slope) * 0.16f)) * 19.0f);
		float lineB = 1.0f - saturate(abs(p.x + rise * (spread * 0.82f)) * 22.0f);
		float crownBand = (1.0f - smoothstep(0.025f, 0.085f, abs(p.y))) *
			(1.0f - smoothstep(0.28f, 0.48f, abs(p.x)));
		float centreBead = 1.0f - smoothstep(0.035f, 0.115f, length(float2(p.x, p.y * 1.7f)));
		return saturate(max(lineA, lineB * 0.78f) * riseWindow + crownBand * 0.82f + centreBead * 0.42f);
	}

	float GetRoofRunoffPhase(float selector)
	{
		float rain = saturate(SharedData::rainResponseSettings.Raining);
		return frac(SharedData::rainResponseSettings.Time * lerp(1.25f, 2.75f, rain) + selector * 23.71f);
	}


	void ApplyVolumetricRainMist(float3 cameraRelativePosition, float viewDepth, inout float3 scattering, inout float extinction)
	{
		if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0 ||
			SharedData::rainResponseSettings.RainMistStrength <= 0.0f)
			return;

		float rain = saturate(SharedData::rainResponseSettings.Raining);
		if (rain <= 1e-4f)
			return;

		float3 absolutePosition = cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;
		float scale = max(SharedData::rainResponseSettings.RainMistScale, 1e-6f);
		float2 driftDir = normalize(float2(0.83f, 0.56f));
		float2 drift = driftDir * SharedData::rainResponseSettings.Time * 0.045f;
		// Volumetric material setup runs for every froxel. Two decorrelated value-noise
		// evaluations give broad spray banks without paying the precipitation VS's
		// multi-octave FBM cost throughout the entire 3D VBuffer.
		float mistNoise = RainValueNoise2D(absolutePosition.xy * scale - drift);
		float filamentNoise = RainValueNoise2D(absolutePosition.xy * scale * float2(0.36f, 1.48f) + drift * 0.55f);
		float clouds = saturate(mistNoise * 0.72f + filamentNoise * 0.38f);
		clouds = smoothstep(0.24f, 0.90f, clouds);

		// The player camera is normally a little above local ground.  A broad
		// camera-relative vertical envelope produces a thin spray layer while still
		// tolerating rolling terrain; the normal atmosphere remains responsible for
		// the larger-scale height profile.
		float mistHeight = max(SharedData::rainResponseSettings.RainMistHeight, 64.0f);
		float relativeHeight = cameraRelativePosition.z + 90.0f;
		float heightMask = exp2(-abs(relativeHeight) / mistHeight * 1.35f);
		heightMask *= 1.0f - smoothstep(mistHeight * 0.65f, mistHeight * 2.0f, relativeHeight);

		float distanceMask = smoothstep(450.0f, 2600.0f, max(viewDepth, 0.0f));
		float rainStrength = pow(rain, 1.20f) * max(SharedData::rainResponseSettings.RainMistStrength, 0.0f);
		float rainExtinction = rainStrength * heightMask * distanceMask * lerp(0.32f, 1.0f, clouds) * 2.2e-5f;

		// Water aerosol is highly scattering but not emissive.  Feeding it into the
		// existing VBuffer means sun, sky, local lights, shadows and temporal
		// integration all treat the mist consistently with the rest of PIXL fog.
		float3 rainAlbedo = lerp(0.88f.xxx, 0.97f.xxx, clouds);
		scattering += rainExtinction * rainAlbedo;
		extinction += rainExtinction;
	}
}

#endif  // __PIXL_RAIN_RESPONSE_PRECIPITATION_HLSLI__
