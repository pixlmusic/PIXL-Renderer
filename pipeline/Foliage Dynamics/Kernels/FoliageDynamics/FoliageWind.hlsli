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
}

#endif
