#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace AmbientProbe
{
	#ifndef USE_PIXL_ENERGY_CALIBRATED_AMBIENT
	#	define USE_PIXL_ENERGY_CALIBRATED_AMBIENT 1
	#endif

#if defined(AMBIENT_PROBE_DEFERRED)
	Texture2D<sh2> EnvironmentProbeTexture : register(t14);
	Texture2D<sh2> SkyProbeTexture : register(t15);
#else
	Texture2D<sh2> EnvironmentProbeTexture : register(t76);
	Texture2D<sh2> SkyProbeTexture : register(t77);
	TextureCube<float4> StaticDiffuseProbeTexture : register(t78);
	TextureCube<float4> StaticSpecularProbeTexture : register(t79);
#endif

	// ============================================================================
	// Low-level SH sampling (raw, no user settings applied)
	// ============================================================================

	/// Get Env AmbientProbe color from environment cubemap SH (without sky)
	float3 GetEnvironmentAmbient(float3 rayDir)
	{
		sh2 shR = EnvironmentProbeTexture.Load(int3(0, 0, 0));
		sh2 shG = EnvironmentProbeTexture.Load(int3(1, 0, 0));
		sh2 shB = EnvironmentProbeTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return max(float3(colorR, colorG, colorB) / Math::PI, 0.0f.xxx);
	}

	/// Get Sky-only AmbientProbe color from game's native reflections cubemap SH
	float3 GetSkyAmbient(float3 rayDir)
	{
		sh2 shR = SkyProbeTexture.Load(int3(0, 0, 0));
		sh2 shG = SkyProbeTexture.Load(int3(1, 0, 0));
		sh2 shB = SkyProbeTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return max(float3(colorR, colorG, colorB) / Math::PI, 0.0f.xxx);
	}

	// ============================================================================
	// Ratio / settings helpers
	// ============================================================================

	/// Compute ratio between DALC and AmbientProbe for brightness/color matching.
	float3 GetAmbientRatio()
	{
		float3 dalc0 = Color::Ambient(SharedData::GetAmbient(0.f));

		sh2 iblSHR = EnvironmentProbeTexture.Load(int3(0, 0, 0));
		sh2 iblSHG = EnvironmentProbeTexture.Load(int3(1, 0, 0));
		sh2 iblSHB = EnvironmentProbeTexture.Load(int3(2, 0, 0));

		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHR, float3(0, 0, 0));
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHG, float3(0, 0, 0));
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHB, float3(0, 0, 0));
		float3 ibl0 = float3(colorR, colorG, colorB) / Math::PI;

		if (SharedData::ambientProbeSettings.DALCMode == 1) {
			float3 ratio = dalc0 / max(ibl0, 0.001);
		#if USE_PIXL_ENERGY_CALIBRATED_AMBIENT
			ratio = clamp(ratio, 0.25f.xxx, 4.0f.xxx);
		#endif
			return lerp(1.0, ratio, SharedData::ambientProbeSettings.DALCAmount);
		} else {
			float dalcLum = Color::RGBToLuminance(dalc0);
			float iblLum = Color::RGBToLuminance(ibl0);
			float ratio = (iblLum > 0.001) ? (dalcLum / iblLum) : 1.0;
		#if USE_PIXL_ENERGY_CALIBRATED_AMBIENT
			ratio = clamp(ratio, 0.25f, 4.0f);
		#endif
			return lerp(1.0, ratio, SharedData::ambientProbeSettings.DALCAmount);
		}
	}

	// ============================================================================
	// Mid-level: individual components with user settings applied
	// ============================================================================

	float3 GetEnvironmentAmbientColor(float3 rayDir)
	{
		float3 ratio = GetAmbientRatio();
		return Color::Saturation(GetEnvironmentAmbient(rayDir), SharedData::ambientProbeSettings.EnvironmentProbeSaturation) * SharedData::ambientProbeSettings.EnvironmentProbeScale * ratio;
	}

		float3 GetSkyAmbientColor(float3 rayDir)
		{
			float3 skyIBL = 0.0f;
			if (!SharedData::InInterior) {
				skyIBL = Color::Saturation(GetSkyAmbient(rayDir), SharedData::ambientProbeSettings.SkyProbeSaturation) * SharedData::ambientProbeSettings.SkyProbeScale;
		#if USE_PIXL_ENERGY_CALIBRATED_AMBIENT
				// The SH sky source has no ground visibility term. Fade it below the
				// horizon so upward-facing surfaces receive sky while undersides retain
				// environment/interior illumination instead of glowing blue.
				float directionLengthSq = dot(rayDir, rayDir);
				float horizonVisibility = directionLengthSq > 1e-6f
					? smoothstep(-0.25f, 0.45f, -rayDir.z * rsqrt(directionLengthSq))
					: 1.0f;
				skyIBL *= horizonVisibility;
		#endif
			}
			return skyIBL;
		}

	// ============================================================================
	// High-level: compute the full diffuse ambient replacement
	// ============================================================================

	/// Compute diffuse AmbientProbe ambient (gamma-space) without directional occlusion.
	float3 GetDiffuseAmbient(float3 vanillaDALC, float3 rayDir)
	{
		float3 linEnv, linSky;
		if (SharedData::ambientProbeSettings.DALCMode >= 2) {
			linEnv = vanillaDALC * SharedData::ambientProbeSettings.DALCAmount;
			linSky = GetSkyAmbientColor(rayDir);
		} else {
			linEnv = GetEnvironmentAmbientColor(rayDir);
			linSky = GetSkyAmbientColor(rayDir);
		}
		return linEnv + linSky;
	}

	/// Compute diffuse AmbientProbe ambient with a skyBounce visibility factor applied per DALCMode
	/// (mode 3 dims both DALC and sky; modes 0-2 dim only the sky contribution).
	float3 GetDiffuseAmbientOccluded(float3 vanillaDALC, float3 rayDir, float visibility)
	{
		float3 linEnv, linSky;
		if (SharedData::ambientProbeSettings.DALCMode == 3) {
			linEnv = vanillaDALC * SharedData::ambientProbeSettings.DALCAmount * visibility;
			linSky = GetSkyAmbientColor(rayDir) * visibility;
		} else if (SharedData::ambientProbeSettings.DALCMode == 2) {
			linEnv = vanillaDALC * SharedData::ambientProbeSettings.DALCAmount;
			linSky = GetSkyAmbientColor(rayDir) * visibility;
		} else {
			linEnv = GetEnvironmentAmbientColor(rayDir);
			linSky = GetSkyAmbientColor(rayDir) * visibility;
		}
		return linEnv + linSky;
	}

	/// Combined env + sky AmbientProbe color with a visibility factor applied to the sky term.
	float3 GetAmbientColorOccluded(float3 rayDir, float visibility)
	{
		return GetEnvironmentAmbientColor(rayDir) + GetSkyAmbientColor(rayDir) * visibility;
	}

#if defined(LIGHTING)
	float3 GetStaticDiffuseAmbient(float3 N, SamplerState samp)
	{
		return StaticDiffuseProbeTexture.SampleLevel(samp, N.xzy, 0).xyz / Math::PI;
	}
#endif

	float3 GetFogAmbientColor(float3 fogColor)
	{
		float3 iblColor;
		if (SharedData::ambientProbeSettings.DALCMode >= 2) {
			float3 dalc0 = Color::Ambient(SharedData::GetAmbient(0.f));
			iblColor = dalc0 * SharedData::ambientProbeSettings.DALCAmount + GetSkyAmbientColor(float3(0, 0, 0));
		} else {
			iblColor = GetEnvironmentAmbientColor(float3(0, 0, 0)) + GetSkyAmbientColor(float3(0, 0, 0));
		}
		if (SharedData::ambientProbeSettings.PreserveFogLuminance) {
			const float fogLuminance = Color::RGBToLuminance(fogColor);
			const float iblLuminance = Color::RGBToLuminance(iblColor);
			if (iblLuminance > 0) {
				const float scale = fogLuminance / iblLuminance;
				iblColor *= scale;
			} else {
				iblColor = fogColor;
			}
		}
		return lerp(fogColor, iblColor, SharedData::ambientProbeSettings.FogAmount);
	}
}

#endif  // __IBL_HLSLI__
