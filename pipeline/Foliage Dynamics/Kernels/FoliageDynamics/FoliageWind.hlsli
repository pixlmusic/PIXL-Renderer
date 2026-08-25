#ifndef PIXL_FOLIAGE_WIND_HLSLI
#define PIXL_FOLIAGE_WIND_HLSLI

namespace FoliageWind
{
	float Hash12(float2 p)
	{
		float3 p3 = frac(float3(p.xyx) * 0.1031f);
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float ValueNoise(float2 p)
	{
		float2 i = floor(p);
		float2 f = frac(p);
		float2 u = f * f * (3.0f - 2.0f * f);
		float a = Hash12(i);
		float b = Hash12(i + float2(1.0f, 0.0f));
		float c = Hash12(i + float2(0.0f, 1.0f));
		float d = Hash12(i + 1.0f.xx);
		return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
	}

	float FBM2(float2 p)
	{
		float n0 = ValueNoise(p);
		float n1 = ValueNoise(p * 2.071f + float2(17.17f, -9.31f));
		return n0 * 0.68f + n1 * 0.32f;
	}

	float SmoothGust(float noiseValue)
	{
		// Keep broad cells quiet for part of their lifetime, then ease into a
		// coherent gust. The second smoothstep avoids the mechanical sine-wave
		// cadence of the original enhancement.
		float pulse = smoothstep(0.24f, 0.82f, saturate(noiseValue));
		return pulse * pulse * (3.0f - 2.0f * pulse);
	}

	float2 SafeDirection(float2 direction, float2 fallbackDirection)
	{
		float lengthSq = dot(direction, direction);
		return lengthSq > 1e-8f
			? direction * rsqrt(lengthSq)
			: fallbackDirection;
	}

	float AdvectedNoise(
		float2 absoluteWorldXY,
		float time,
		float2 direction,
		float spatialScale,
		float speed,
		float frequency,
		float seed)
	{
		float2 p =
			absoluteWorldXY * (frequency * max(spatialScale, 0.05f)) -
			direction * (time * max(speed, 0.0f) * 0.055f);
		p += seed.xx;
		return FBM2(p);
	}
}

#endif
