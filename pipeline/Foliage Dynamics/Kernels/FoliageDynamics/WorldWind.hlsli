#ifndef PIXL_WORLD_WIND_HLSLI
#define PIXL_WORLD_WIND_HLSLI

// Resource-free world-wind sampling contract for DX11 / Shader Model 5.
//
// Callers own the source of weather direction and strength. This include only
// produces normalized spatial/temporal signals, so grass, trees, hair and
// precipitation can retain their independent material/response calibration.
// Positions MUST be absolute world positions (camera-relative position plus the
// matching origin adjustment), and times MUST use the same seconds domain.
namespace PIXLWorldWind
{
	static const float2 DefaultDirection = float2(0.8192319f, 0.5734624f);

	float Hash12(float2 p)
	{
		float3 p3 = frac(float3(p.xyx) * 0.1031f);
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float ValueNoise2D(float2 p)
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

	struct Parameters
	{
		float2 Direction;
		float SpatialScale;
		float GustSpeed;
		float FlutterSpeed;
	};

	struct FieldSample
	{
		float2 Direction;
		float Gust;
		float Crosswind;
		float Flutter;
	};

	struct TemporalPair
	{
		FieldSample Current;
		FieldSample Previous;
	};

	Parameters MakeParameters(
		float2 direction,
		float spatialScale,
		float gustSpeed,
		float flutterSpeed)
	{
		Parameters parameters;
		parameters.Direction = direction;
		parameters.SpatialScale = spatialScale;
		parameters.GustSpeed = gustSpeed;
		parameters.FlutterSpeed = flutterSpeed;
		return parameters;
	}

	FieldSample Evaluate(
		float2 absoluteWorldXY,
		float timeSeconds,
		Parameters parameters,
		float stableSeed)
	{
		FieldSample field;
		float2 flow = SafeDirection(parameters.Direction, DefaultDirection);
		float2 crossFlow = float2(-flow.y, flow.x);
		float scale = max(parameters.SpatialScale, 0.25f);
		float speed = max(parameters.GustSpeed, 0.1f);

		// This is the accepted Foliage Dynamics 2.x field exactly: one broad
		// advected cell field, one travelling wave and restrained cross/flutter
		// channels. Response amplitude remains deliberately outside this contract.
		float2 gustPosition = absoluteWorldXY * (0.00048f * scale) -
			flow * (timeSeconds * speed * 0.075f) + stableSeed * 11.7f.xx;
		float gustNoise = ValueNoise2D(gustPosition);
		float wave = 0.5f + 0.5f * sin(
			dot(absoluteWorldXY, flow) * (0.00115f * scale) -
			timeSeconds * speed * 0.82f + stableSeed * 6.28318531f);
		float envelope = lerp(gustNoise, wave, 0.38f);

		field.Direction = flow;
		field.Gust = smoothstep(0.34f, 0.82f, envelope);

		float crossPhase =
			dot(absoluteWorldXY, crossFlow) * (0.0042f * scale) +
			timeSeconds * speed * 1.23f + stableSeed * 17.0f;
		field.Crosswind = sin(crossPhase) * (0.35f + 0.65f * field.Gust);

		float flutterPhase =
			timeSeconds * (2.4f + 2.2f * max(parameters.FlutterSpeed, 0.1f)) +
			dot(absoluteWorldXY, flow) * 0.0105f + stableSeed * 29.0f;
		field.Flutter = sin(flutterPhase) *
			(0.72f + 0.28f * sin(flutterPhase * 0.61f + 1.7f));
		return field;
	}

	TemporalPair EvaluateTemporalPair(
		float2 currentAbsoluteWorldXY,
		float currentTimeSeconds,
		float2 previousAbsoluteWorldXY,
		float previousTimeSeconds,
		Parameters parameters,
		float stableSeed)
	{
		TemporalPair pair;
		// Both histories call the identical pure sampler. Do not approximate the
		// previous field by subtracting a force vector or frame delta.
		pair.Current = Evaluate(
			currentAbsoluteWorldXY, currentTimeSeconds, parameters, stableSeed);
		pair.Previous = Evaluate(
			previousAbsoluteWorldXY, previousTimeSeconds, parameters, stableSeed);
		return pair;
	}
}

#endif
