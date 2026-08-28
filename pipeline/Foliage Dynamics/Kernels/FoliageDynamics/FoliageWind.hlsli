#ifndef PIXL_FOLIAGE_WIND_HLSLI
#define PIXL_FOLIAGE_WIND_HLSLI

// PIXL's grass wind deliberately stays small and deterministic. Skyrim's
// authored displacement remains the structural motion; this field supplies one
// broad travelling gust envelope and one restrained cross-wind flutter signal.
// Evaluating it with WindTimer and PreviousWindTimer yields exact motion history.
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

	float2 SafeDirection(float2 direction, float2 fallbackDirection)
	{
		float lengthSq = dot(direction, direction);
		return lengthSq > 1.0e-8f ? direction * rsqrt(lengthSq) : fallbackDirection;
	}

	struct GrassGustField
	{
		float Gust;
		float Crosswind;
		float Flutter;
	};

	GrassGustField SampleGrassGust(
		float2 absoluteWorldXY,
		float time,
		float2 windDirection,
		float spatialScale,
		float gustSpeed,
		float flutterSpeed,
		float seed)
	{
		GrassGustField field;
		float2 flow = SafeDirection(windDirection, float2(0.8192319f, 0.5734624f));
		float2 crossFlow = float2(-flow.y, flow.x);
		float scale = max(spatialScale, 0.25f);
		float speed = max(gustSpeed, 0.1f);

		// One smooth, very broad advected field avoids the conflicting frequencies
		// and material-looking folds produced by the retired multi-wind system.
		float2 gustPosition = absoluteWorldXY * (0.00048f * scale) -
			flow * (time * speed * 0.075f) + seed * 11.7f.xx;
		float gustNoise = ValueNoise(gustPosition);
		float wave = 0.5f + 0.5f * sin(
			dot(absoluteWorldXY, flow) * (0.00115f * scale) -
			time * speed * 0.82f + seed * 6.28318531f);
		float envelope = lerp(gustNoise, wave, 0.38f);
		field.Gust = smoothstep(0.34f, 0.82f, envelope);

		float crossPhase =
			dot(absoluteWorldXY, crossFlow) * (0.0042f * scale) +
			time * speed * 1.23f + seed * 17.0f;
		field.Crosswind = sin(crossPhase) * (0.35f + 0.65f * field.Gust);

		float flutterPhase =
			time * (2.4f + 2.2f * max(flutterSpeed, 0.1f)) +
			dot(absoluteWorldXY, flow) * 0.0105f + seed * 29.0f;
		field.Flutter = sin(flutterPhase) * (0.72f + 0.28f * sin(flutterPhase * 0.61f + 1.7f));
		return field;
	}
}

#endif
