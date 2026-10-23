#ifndef PIXL_FOLIAGE_WIND_HLSLI
#define PIXL_FOLIAGE_WIND_HLSLI

#include "FoliageDynamics/WorldWind.hlsli"

// PIXL's grass wind deliberately stays small and deterministic. Skyrim's
// authored displacement remains the structural motion; this field supplies one
// broad travelling gust envelope and one restrained cross-wind flutter signal.
// Evaluating it with WindTimer and PreviousWindTimer yields exact motion history.
namespace FoliageWind
{
	float Hash12(float2 p)
	{
		return PIXLWorldWind::Hash12(p);
	}

	float ValueNoise(float2 p)
	{
		return PIXLWorldWind::ValueNoise2D(p);
	}

	float2 SafeDirection(float2 direction, float2 fallbackDirection)
	{
		return PIXLWorldWind::SafeDirection(direction, fallbackDirection);
	}

	struct GrassGustField
	{
		float Gust;
		float Crosswind;
		float Flutter;
	};

	struct MultiScaleField
	{
		float Gust;
		float Macro;
		float2 Turbulence;
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
		PIXLWorldWind::Parameters parameters = PIXLWorldWind::MakeParameters(
			windDirection, spatialScale, gustSpeed, flutterSpeed);
		PIXLWorldWind::FieldSample sharedField = PIXLWorldWind::Evaluate(
			absoluteWorldXY, time, parameters, seed);

		GrassGustField field;
		field.Gust = sharedField.Gust;
		field.Crosswind = sharedField.Crosswind;
		field.Flutter = sharedField.Flutter;
		return field;
	}

	// Hierarchical tree motion shares the world-space gust envelope with grass,
	// then adds progressively smaller, phase-stable branch and leaf motion. The
	// pure time input lets Lighting.hlsl evaluate current and previous frames from
	// the same absolute anchor, preserving valid motion vectors.
	MultiScaleField SampleMultiScaleField(
		float2 absoluteWorldXY,
		float timeSeconds,
		float2 windDirection,
		float spatialScale,
		float gustSpeed,
		float flutterSpeed,
		float stableSeed)
	{
		float2 flow = SafeDirection(windDirection, PIXLWorldWind::DefaultDirection);
		float2 crossFlow = float2(-flow.y, flow.x);
		float scale = max(spatialScale, 0.25f);
		float speed = max(gustSpeed, 0.1f);
		GrassGustField grassField = SampleGrassGust(
			absoluteWorldXY, timeSeconds, flow, scale, speed, flutterSpeed, stableSeed);

		MultiScaleField field;
		field.Gust = grassField.Gust;
		float macroPhase = dot(absoluteWorldXY, flow) * (0.00019f * scale) -
			timeSeconds * speed * 0.19f + stableSeed * 6.28318531f;
		field.Macro = sin(macroPhase) * (0.45f + 0.55f * field.Gust);

		float branchPhase = dot(absoluteWorldXY, flow) * (0.00135f * scale) -
			timeSeconds * speed * 0.91f + stableSeed * 17.17f;
		float crossPhase = dot(absoluteWorldXY, crossFlow) * (0.0019f * scale) +
			timeSeconds * speed * 1.13f + stableSeed * 31.73f;
		field.Turbulence = float2(sin(branchPhase), sin(crossPhase)) *
			(0.30f + 0.70f * field.Gust);

		float leafPhase = timeSeconds * (2.4f + 2.2f * max(flutterSpeed, 0.1f)) +
			dot(absoluteWorldXY, flow + crossFlow * 0.37f) * (0.0105f * scale) +
			stableSeed * 29.0f;
		field.Flutter = sin(leafPhase) *
			(0.72f + 0.28f * sin(leafPhase * 0.61f + 1.7f)) *
			(0.55f + 0.45f * grassField.Gust);
		return field;
	}
}

#endif
