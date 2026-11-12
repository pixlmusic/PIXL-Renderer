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

	bool IsFiniteVector(float3 value)
	{
		return !any((asuint(value) & 0x7f800000u) == 0x7f800000u);
	}

	float3 SafeNormalize(float3 value, float3 fallback)
	{
		float lengthSq = dot(value, value);
		return IsFiniteVector(value) && (asuint(lengthSq) & 0x7f800000u) != 0x7f800000u && lengthSq > 1.0e-8f
			? value * rsqrt(lengthSq)
			: fallback;
	}

	float3 GetCloudShadowSampleDir(float3 rel_pos, float3 eye_to_sun)
	{
		if (!IsFiniteVector(rel_pos))
			return float3(0.0f, 0.0f, 1.0f);
		eye_to_sun = SafeNormalize(eye_to_sun, float3(0.0f, 0.0f, 1.0f));
		float r = PlanetRadius;
		float3 p = (rel_pos + float3(0, 0, r)) * RcpHPlusR;
		float dotprod = dot(p, eye_to_sun);
		float discriminant = max(dotprod * dotprod - dot(p, p) + 1, 0.0f);
		float t = max(-dotprod + sqrt(discriminant), 0.0f);
		float3 v = (p + eye_to_sun * t) * (r + CloudHeight) - float3(0, 0, r);
		return IsFiniteVector(v) ? v : float3(0.0f, 0.0f, 1.0f);
	}

	float GetCloudShadowMult(float3 worldPosition, SamplerState textureSampler)
	{
		float3 cloudSampleDir = GetCloudShadowSampleDir(worldPosition, SharedData::DirLightDirection.xyz).xyz;
		// Cubemap filtering offsets are angular: normalize before constructing
		// the tangent frame, independent of cloud height and world position.
		cloudSampleDir = SafeNormalize(cloudSampleDir, float3(0.0f, 0.0f, 1.0f));
	#if USE_PIXL_SKY_VEIL
		// Treat the cubemap as cloud optical depth instead of linearly subtracting
		// opacity. A small cross filter widens naturally as the sun approaches the
		// horizon, removing hard cubemap texel transitions without another texture.
		float3 safeSun = SafeNormalize(SharedData::DirLightDirection.xyz, float3(0.0f, 0.0f, 1.0f));
		float sunElevation = saturate(abs(safeSun.z));
		float penumbra = lerp(0.0025f, 0.00065f, sunElevation);
		float3 up = abs(cloudSampleDir.z) < 0.95f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
		float3 tangentRaw = cross(up, cloudSampleDir);
		float3 tangent = SafeNormalize(tangentRaw, float3(1.0f, 0.0f, 0.0f));
		float3 bitangentRaw = cross(cloudSampleDir, tangent);
		float3 bitangent = SafeNormalize(bitangentRaw, float3(0.0f, 1.0f, 0.0f));

		float opticalDepth = saturate(SkyVeilTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x) * 0.4f;
		opticalDepth += saturate(SkyVeilTexture.SampleLevel(textureSampler, SafeNormalize(cloudSampleDir + tangent * penumbra, cloudSampleDir), 0).x) * 0.15f;
		opticalDepth += saturate(SkyVeilTexture.SampleLevel(textureSampler, SafeNormalize(cloudSampleDir - tangent * penumbra, cloudSampleDir), 0).x) * 0.15f;
		opticalDepth += saturate(SkyVeilTexture.SampleLevel(textureSampler, SafeNormalize(cloudSampleDir + bitangent * penumbra, cloudSampleDir), 0).x) * 0.15f;
		opticalDepth += saturate(SkyVeilTexture.SampleLevel(textureSampler, SafeNormalize(cloudSampleDir - bitangent * penumbra, cloudSampleDir), 0).x) * 0.15f;

		// Beer-Lambert transmittance preserves bright thin clouds while giving
		// dense cloud cover a convincing, bounded shadow response.
		return saturate(exp2(-1.442695f * max(opticalDepth, 0.0f) * clamp(SharedData::skyVeilSettings.Opacity, 0.0f, 8.0f)));
	#else
		float cloudCubeSample = SkyVeilTexture.SampleLevel(textureSampler, cloudSampleDir, 0).x;
		return saturate(1.0 - cloudCubeSample * SharedData::skyVeilSettings.Opacity);
	#endif
	}
}
