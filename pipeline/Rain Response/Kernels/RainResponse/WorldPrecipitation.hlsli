#ifndef PIXL_WORLD_PRECIPITATION_HLSLI
#define PIXL_WORLD_PRECIPITATION_HLSLI

cbuffer PIXLWorldPrecipitationTuning : register(b13)
{
	uint PIXL_EnableWorldSpaceRain;
	uint PIXL_EnableSnowEnhancement;
	float PIXL_Snowing;
	float PIXL_SnowDistanceVisibility;

	float PIXL_SnowLightingResponse;
	float PIXL_SnowWindDrift;
	float PIXL_SnowFlutter;
	float PIXL_SnowDensityBoost;

	float PIXL_SnowDepthStart;
	float PIXL_SnowDepthEnd;
	float PIXL_SnowFarVolume;
	float PIXL_SnowTumble;

	float PIXL_SnowFlakeScale;
	float PIXL_SnowWorldScale;
	float PIXL_PrecipPad0;
	float PIXL_PrecipPad1;
};

namespace PIXLPrecipitation
{
	float3 SafeNormalizeSnow(float3 value, float3 fallback)
	{
		float lengthSq = dot(value, value);
		return lengthSq > 1e-8f ? value * rsqrt(lengthSq) : fallback;
	}

	float Hash31(float3 p)
	{
		p = frac(p * 0.1031f);
		p += dot(p, p.yzx + 33.33f);
		return frac((p.x + p.y) * p.z);
	}

	float Wave(float2 p, float2 frequency, float time, float speed, float phase)
	{
		return sin(dot(p, frequency) + time * speed + phase);
	}

	float GetSnowDistanceWeight(float viewDistance)
	{
		float depthStart = max(PIXL_SnowDepthStart, 100.0f);
		float depthEnd = max(PIXL_SnowDepthEnd, depthStart + 512.0f);
		float d = max(viewDistance, 0.0f);

		float mid = smoothstep(depthStart * 0.34f, depthEnd * 0.56f, d);
		float far = smoothstep(depthStart * 0.62f, depthEnd, d);
		return saturate(mid * 0.72f + far * far * 0.62f);
	}

	float GetSnowDistanceVisibility(float viewDistance)
	{
		if (PIXL_EnableSnowEnhancement == 0u)
			return 1.0f;

		float snow = saturate(PIXL_Snowing);
		float weight = GetSnowDistanceWeight(viewDistance);
		float boost = max(PIXL_SnowDistanceVisibility, 0.0f) * snow;
		return 1.0f + boost * (0.10f + weight * 2.25f);
	}

	float GetSnowFinalAlphaFloor(float sourceAlpha, float viewDistance)
	{
		if (PIXL_EnableSnowEnhancement == 0u)
			return 0.0f;

		float authored = sqrt(saturate(sourceAlpha));
		float snow = saturate(PIXL_Snowing);
		float weight = GetSnowDistanceWeight(viewDistance);
		float density =
			max(PIXL_SnowDensityBoost, 0.0f) *
			lerp(0.22f, 0.78f, weight);

		return saturate(authored * density * snow);
	}

	float GetSnowFarShellScale(float normalizedRadius, float selector)
	{
		if (PIXL_EnableSnowEnhancement == 0u)
			return 1.0f;

		float outer = smoothstep(0.18f, 0.92f, saturate(normalizedRadius));
		float selected = smoothstep(0.34f, 0.92f, selector);
		float amount =
			max(PIXL_SnowFarVolume, 0.0f) *
			max(PIXL_SnowDistanceVisibility, 0.0f) *
			saturate(PIXL_Snowing);

		return 1.0f + outer * lerp(0.16f, 0.62f, selected) * amount;
	}

	float GetSnowLightingMultiplier(float viewDistance)
	{
		if (PIXL_EnableSnowEnhancement == 0u)
			return 1.0f;

		float weight = GetSnowDistanceWeight(viewDistance);
		float light =
			max(PIXL_SnowLightingResponse, 0.0f) *
			saturate(PIXL_Snowing);

		return 1.0f + light * lerp(0.12f, 1.15f, weight);
	}

	float GetSnowDirectionalScatter(float3 viewDirection)
	{
		if (PIXL_EnableSnowEnhancement == 0u)
			return 1.0f;

		float3 V = SafeNormalizeSnow(viewDirection, float3(0.0f, 0.0f, 1.0f));
		float3 lightAxis = SafeNormalizeSnow(SharedData::DirLightDirection.xyz, float3(0.0f, 0.0f, 1.0f));
		float alignment = saturate(abs(dot(V, lightAxis)));
		float lobe = pow(alignment, 3.5f);

		return 1.0f +
			saturate(PIXL_Snowing) *
			lobe *
			(0.08f + max(PIXL_SnowLightingResponse, 0.0f) * 0.42f);
	}

	struct SnowDynamics
	{
		float3 position;
		float3 axisA;
		float3 axisB;
		float selector;
	};

	SnowDynamics EvaluateSnowDynamics(float3 cameraRelativePosition, float3 wind)
	{
		SnowDynamics result;
		result.position = cameraRelativePosition;
		result.axisA = float3(1.0f, 0.0f, 0.0f);
		result.axisB = float3(0.0f, 1.0f, 0.0f);
		result.selector = 0.5f;

		if (PIXL_EnableSnowEnhancement == 0u)
			return result;

		float snow = saturate(PIXL_Snowing);
		float3 absolutePosition =
			cameraRelativePosition + SharedData::CameraPosAdjust.xyz;

		float worldScale = max(PIXL_SnowWorldScale, 256.0f);
		float3 worldCell = floor(absolutePosition / worldScale);

		float selector =
			Hash31(worldCell + floor(absolutePosition * 0.017f) * 0.013f);
		result.selector = selector;

		float2 windXY = wind.xy;
		float windLengthSq = dot(windXY, windXY);
		float2 windDirection =
			windLengthSq > 1e-6f ?
			windXY * rsqrt(windLengthSq) :
			float2(0.8192319f, 0.5734624f);
		float2 crossWind = float2(-windDirection.y, windDirection.x);
		float windLength = sqrt(max(windLengthSq, 0.0f));
		float windPresence =
			saturate(windLength / (windLength + 1.0f));

		float phase = selector * 6.2831853f;

		float broad = Wave(
			absolutePosition.xy,
			float2(0.0010f, 0.00155f),
			SharedData::Timer,
			0.30f,
			phase);

		float flutter = Wave(
			absolutePosition.xy,
			float2(-0.0067f, 0.0081f),
			SharedData::Timer,
			0.96f + selector * 0.62f,
			2.7f + phase * 1.7f);

		float micro = Wave(
			absolutePosition.xy,
			float2(0.015f, 0.012f),
			SharedData::Timer,
			1.82f,
			5.1f + phase * 2.1f);

		float driftStrength =
			max(PIXL_SnowWindDrift, 0.0f) *
			snow *
			lerp(0.42f, 1.0f, windPresence);

		float flutterStrength =
			max(PIXL_SnowFlutter, 0.0f) * snow;

		result.position.xy +=
			windDirection *
			broad *
			lerp(2.0f, 11.0f, windPresence) *
			driftStrength;

		result.position.xy +=
			crossWind *
			flutter *
			lerp(1.2f, 4.8f, selector) *
			flutterStrength;

		result.position.z +=
			micro *
			lerp(0.30f, 1.25f, selector) *
			flutterStrength;

		// Stable world-space flake basis. The camera only projects it.
		float tumble =
			max(PIXL_SnowTumble, 0.0f) *
			(SharedData::Timer * lerp(0.30f, 1.05f, selector) +
			 phase +
			 broad * 0.45f +
			 flutter * 0.28f);

		float3 normal = normalize(float3(
			sin(tumble) * 0.62f,
			cos(tumble * 0.83f) * 0.62f,
			1.0f));

		float3 reference =
			abs(normal.z) < 0.92f ?
			float3(0.0f, 0.0f, 1.0f) :
			float3(0.0f, 1.0f, 0.0f);

		float3 axisA = normalize(cross(reference, normal));
		float3 axisBBase = normalize(cross(normal, axisA));

		float c = cos(tumble * 0.73f);
		float s = sin(tumble * 0.73f);

		result.axisA =
			normalize(axisA * c + axisBBase * s);
		result.axisB =
			normalize(cross(normal, result.axisA));

		return result;
	}
}

#endif
