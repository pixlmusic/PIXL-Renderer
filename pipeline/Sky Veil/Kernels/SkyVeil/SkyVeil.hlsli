#include "Common/Game.hlsli"

namespace SkyVeil
{
	#ifndef USE_PIXL_SKY_VEIL
	#	define USE_PIXL_SKY_VEIL 1
	#endif

	TextureCube<float> SkyVeilTexture : register(t25);

	const static float CloudHeight = (2e3f / GAME_UNIT_TO_M);
	const static float PlanetRadius = (6371e3f / GAME_UNIT_TO_M);
	const static float RcpHPlusR = (1.0 / (CloudHeight + PlanetRadius));

	float3 GetCloudShadowSampleDir(float3 rel_pos, float3 eye_to_sun)
	{
		float r = PlanetRadius;
		float3 p = (rel_pos + float3(0, 0, r)) * RcpHPlusR;
		float dotprod = dot(p, eye_to_sun);
		float lengthsqr = dot(p, p);
		float t = -dotprod + sqrt(dotprod * dotprod - dot(p, p) + 1);
		float3 v = (p + eye_to_sun * t) * (r + CloudHeight) - float3(0, 0, r);
		return v;
	}

	float GetCloudShadowMult(float3 worldPosition, SamplerState textureSampler)
	{
		float3 cloudSampleDir = GetCloudShadowSampleDir(worldPosition, SharedData::DirLightDirection.xyz).xyz;
	#if USE_PIXL_SKY_VEIL
		// Treat the cubemap as cloud optical depth instead of linearly subtracting
		// opacity. A small cross filter widens naturally as the sun approaches the
		// horizon, removing hard cubemap texel transitions without another texture.
		float sunElevation = saturate(abs(SharedData::DirLightDirection.z));
		float penumbra = lerp(0.0025f, 0.00065f, sunElevation);
		float3 up = abs(cloudSampleDir.z) < 0.95f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
		float3 tangent = normalize(cross(up, cloudSampleDir));
		float3 bitangent = normalize(cross(cloudSampleDir, tangent));

		float opticalDepth = SkyVeilTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x * 0.4f;
		opticalDepth += SkyVeilTexture.SampleLevel(textureSampler, normalize(cloudSampleDir + tangent * penumbra), 0).x * 0.15f;
		opticalDepth += SkyVeilTexture.SampleLevel(textureSampler, normalize(cloudSampleDir - tangent * penumbra), 0).x * 0.15f;
		opticalDepth += SkyVeilTexture.SampleLevel(textureSampler, normalize(cloudSampleDir + bitangent * penumbra), 0).x * 0.15f;
		opticalDepth += SkyVeilTexture.SampleLevel(textureSampler, normalize(cloudSampleDir - bitangent * penumbra), 0).x * 0.15f;

		// Beer-Lambert transmittance preserves bright thin clouds while giving
		// dense cloud cover a convincing, bounded shadow response.
		return saturate(exp2(-1.442695f * opticalDepth * max(SharedData::skyVeilSettings.Opacity, 0.0f)));
	#else
		float cloudCubeSample = SkyVeilTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x;
		return saturate(1.0 - cloudCubeSample * SharedData::skyVeilSettings.Opacity);
	#endif
	}
}
