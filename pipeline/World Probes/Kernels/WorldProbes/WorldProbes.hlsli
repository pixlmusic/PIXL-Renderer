#ifndef WORLDPROBES_HLSLI
#define WORLDPROBES_HLSLI

#include "Common/BRDF.hlsli"

#if defined(SKY_BOUNCE)
#	include "SkyBounce/SkyBounce.hlsli"
#endif

#if defined(AMBIENT_PROBE)
#	include "AmbientProbe/AmbientProbe.hlsli"
#endif

namespace WorldProbes
{
	TextureCube<float3> EnvReflectionsTexture : register(t30);
	TextureCube<float3> EnvTexture : register(t31);

#if !defined(WATER)

#	if defined(SKY_BOUNCE)
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness, sh2 skyBounce)
#	else
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness)
#	endif
	{
#	if defined(DEFERRED)
		return 1.0;
#	else
		float3 R = reflect(-V, N);

		float level = roughness * 7.0;

		float3 finalIrradiance = 0;

		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(
													max(0, SharedData::GetAmbient(R)))) *
		                                        Color::ReflectionNormalisationScale;

#		if defined(AMBIENT_PROBE) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		const bool useStaticIBL = SharedData::ambientProbeSettings.EnableAmbientProbe && SharedData::ambientProbeSettings.UseStaticAmbientProbe && !inWorld && !inReflection;
#		else
		const bool useStaticIBL = false;
#		endif

		if (!useStaticIBL) {
#		if defined(SKY_BOUNCE)
		float skyBounceSpecular = 0.0;
		float skyBounceVisibility = 1.0;
		if (!SharedData::InInterior) {
			skyBounceSpecular = SkyBounce::EvaluateSpecular(skyBounce, SphericalHarmonics::FauxSpecularLobe(N, V, roughness));
			skyBounceVisibility = SkyBounce::EvaluateVisibility(skyBounce);
		}
#		endif

#		if defined(AMBIENT_PROBE)
		if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
			float3 envSample = EnvTexture.SampleLevel(SampColorSampler, R, level);
			float3 fullSample = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float3 envSpecular = 0.0;
			float3 skySpecular = 0.0;

			if (SharedData::ambientProbeSettings.DALCMode >= 2) {
				// Mode 2/3: DALC-normalized env scaled by DALCAmount + sky overlay
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::ambientProbeSettings.DALCAmount;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::ambientProbeSettings.SkyProbeScale;
#			if defined(SKY_BOUNCE)
				envSpecular *= (SharedData::ambientProbeSettings.DALCMode == 3) ? skyBounceVisibility : 1.0;
				skySpecular *= skyBounceSpecular;
#			endif
			} else {
				// Mode 0/1: AmbientProbe ratio-based
				float3 ratio = AmbientProbe::GetAmbientRatio();
				envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::ambientProbeSettings.EnvironmentProbeScale;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::ambientProbeSettings.SkyProbeScale;
#			if defined(SKY_BOUNCE)
				skySpecular *= skyBounceSpecular;
#			endif
			}
			if (SharedData::InInterior) {
				skySpecular = 0;
			}

			finalIrradiance = envSpecular + skySpecular;
		} else
#		endif
		{
			// Fallback without AmbientProbe: normalize-by-luminance with DALC
#		if defined(SKY_BOUNCE)
			if (SharedData::InInterior) {
				float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
				float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
				finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
			} else {
				float3 specularIrradianceReflections = 0.0;
				if (skyBounceSpecular > 0.0) {
					specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
					specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
					specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
				}
				float3 specularIrradiance = 0.0;
				if (skyBounceSpecular < 1.0) {
					specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
					float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skyBounceSpecular);
					specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
					specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
				}
				finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skyBounceSpecular);
			}
#		else
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#		endif
		}
		} else {
#		if defined(AMBIENT_PROBE) && defined(LIGHTING)
			float3 specularIrradiance = AmbientProbe::StaticSpecularProbeTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance = specularIrradiance;
#		endif
		}

		return finalIrradiance;
#	endif
	}
#endif  // !WATER
}
#endif  // WORLDPROBES_HLSLI
