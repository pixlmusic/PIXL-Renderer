#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

namespace NaturalLighting
{
	static const float SCALE = 0.8f;
	static const float METRES_TO_UNITS_SQ = METRES_TO_UNITS * METRES_TO_UNITS;
	static const float SCALED_UNITS_SQ = SCALE * METRES_TO_UNITS_SQ;

	float GetAttenuation(float distance, RadiantGrid::Light light)
	{
		if (!isfinite(distance) || (light.lightFlags & RadiantGrid::LightFlags::Disabled) != 0)
			return 0.0f;
		distance = abs(distance);

		float sizeBias = max(light.sizeBias, 0.0f);
		float radius = max(light.radius, 0.0f);
		float fadeZone = max(light.fadeZone, 0.0f);
		if ((light.lightFlags & RadiantGrid::LightFlags::InverseSquare) != 0)
		{
			if (!isfinite(sizeBias) || !isfinite(radius) || !isfinite(fadeZone))
				return 0.0f;
			if (distance >= radius)
				return 0.0f;
			float invSq = SCALED_UNITS_SQ * rcp(max(distance * distance + sizeBias, 1.0f));
			float t = saturate((radius - distance) * fadeZone);
			float fade = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
			return max(invSq * fade, 0.0f);
		}

		if (!isfinite(light.invRadius))
			return 0.0f;
		float intensityFactor = saturate(distance * max(light.invRadius, 0.0f));
		float reg = 1.0f - intensityFactor * intensityFactor;
		reg *= reg;
		return max(reg, 0.0f);
	}
}
