#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

namespace NaturalLighting
{
	static const float SCALE = 0.8f;
	static const float METRES_TO_UNITS_SQ = METRES_TO_UNITS * METRES_TO_UNITS;
	static const float SCALED_UNITS_SQ = SCALE * METRES_TO_UNITS_SQ;

	float GetAttenuation(float distance, RadiantGrid::Light light)
	{
		float isEnabled = 1.0f - float((light.lightFlags & RadiantGrid::LightFlags::Disabled) != 0);
		float isInvSq = float((light.lightFlags & RadiantGrid::LightFlags::InverseSquare) != 0);

		float invSq = SCALED_UNITS_SQ * rcp(max(distance * distance + light.sizeBias, 1.0f));
		float t = saturate((light.radius - distance) * light.fadeZone);
		float fastSmoothstep = t * t * (3.0f - 2.0f * t);
		invSq *= fastSmoothstep;

		float intensityFactor = saturate(distance * light.invRadius);
		float reg = 1.0f - intensityFactor * intensityFactor;

		return lerp(reg, invSq, isInvSq) * isEnabled;
	}
}
