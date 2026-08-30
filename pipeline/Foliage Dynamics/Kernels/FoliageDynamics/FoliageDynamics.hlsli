#include "Common/BRDF.hlsli"

namespace FoliageDynamics
{
	#ifndef USE_PIXL_VEGETATION_BRDF
	#	define USE_PIXL_VEGETATION_BRDF 1
	#endif

	float GlossinessToPerceptualRoughness(float glossiness)
	{
		// The UI uses a familiar 1..100 gloss control. Map it into a perceptual
		// GGX roughness range that is actually useful for foliage: low values are
		// broad/waxy; high values can produce a clearly readable wet leaf/blade lobe.
		float g = saturate((clamp(glossiness, 1.0f, 100.0f) - 1.0f) / 99.0f);
		return lerp(0.72f, 0.10f, pow(g, 0.65f));
	}

	float3 ThinSurfaceTransmissionTint(float3 baseColor, float thickness)
	{
		// A cheap Beer-Lambert proxy: darker/chlorophyll-rich channels absorb more
		// light as the sheet becomes thicker. Blend with the legacy square-root tint
		// so existing grass packs keep their authored colour while gaining believable
		// wavelength-dependent back-lighting.
		float3 saturatedBase = saturate(baseColor);
		float3 absorption = 1.0f.xxx - saturatedBase;
		float3 beerTint = exp2(-absorption * (0.75f + 1.65f * saturate(thickness)));
		return lerp(sqrt(saturatedBase), beerTint, 0.58f);
	}

	float ThinSurfaceDiffuseEnergy(float normalDotView, float transmission, float specularStrength)
	{
		// Reserve energy for the dielectric Fresnel lobe and transmitted light.
		// Artistic strength remains supported but cannot make diffuse + reflection
		// exceed the incident energy by several times as the previous stack could.
		float grazing = 1.0f - saturate(abs(normalDotView));
		float fresnel = 0.04f + 0.96f * grazing * grazing * grazing * grazing * grazing;
		float reflected = saturate(fresnel * clamp(specularStrength, 0.0f, 2.0f));
		float transmitted = saturate(transmission) * 0.34f;
		return saturate(1.0f - reflected - transmitted);
	}

	float ThinSurfaceDiffuseShape(
		float3 L, float3 V, float3 N, float perceptualRoughness)
	{
		float NdotL = saturate(dot(N, L));
		float NdotV = saturate(dot(N, V));
		float oren = BRDF::Diffuse_OrenNayar(
			clamp(perceptualRoughness, 0.08f, 1.0f), N, V, L, NdotV, NdotL).x;
		// Return a shape correction relative to PIXL's existing calibrated Lambert
		// response rather than changing the renderer's directional-light units.
		return clamp(oren / max(BRDF::Diffuse_Lambert(), 1e-4f), 0.72f, 1.20f);
	}

	float3 GetLightSpecularInput(float3 L, float3 V, float3 N, float3 lightColor, float shininess)
	{
		float3 Hsum = V + L;
		float hLenSq = dot(Hsum, Hsum);
		float3 H = hLenSq > 1e-6f ? Hsum * rsqrt(hLenSq) : L;

#if USE_PIXL_VEGETATION_BRDF
		// Grass cards and leaves are thin two-sided dielectrics. Absolute cosine
		// terms keep the physically-shaped reflection available on the visible
		// side of a two-sided card instead of dropping the entire lobe to zero.
		float NdotL = saturate(abs(dot(N, L)));
		float NdotV = saturate(abs(dot(N, V)));
		float HdotN = saturate(abs(dot(H, N)));
		float VdotH = saturate(dot(V, H));

		float perceptualRoughness = GlossinessToPerceptualRoughness(shininess);
		if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0) {
			float normalVariance = max(
				dot(ddx_coarse(N), ddx_coarse(N)),
				dot(ddy_coarse(N), ddy_coarse(N)));
			float filtered = sqrt(
				perceptualRoughness * perceptualRoughness +
				saturate(normalVariance * max(SharedData::foliageDynamicsSettings.SpecularAA, 0.0f)) * 0.10f);
			perceptualRoughness = clamp(filtered, 0.08f, 1.0f);
		}

		float D = BRDF::D_GGX(perceptualRoughness, HdotN);
		float Vis = BRDF::Vis_SmithJointApprox(perceptualRoughness, NdotV, NdotL);
		float3 F = BRDF::F_Schlick(0.04f.xxx, VdotH);
		// D contains the normalized 1/PI GGX NDF. Match PIXL's common direct-light
		// calibration so legacy/non-linear Skyrim lighting does not silently lose
		// most of the physically-shaped foliage lobe.
		return max(lightColor, 0.0f.xxx) * D * Vis * F * NdotL *
			Color::PBRLightingCompensation * Color::PBRLightingScale;
#else
		float lightColorMultiplier = exp2(max(shininess, 1.0f) * log2(max(HdotN, 1e-4f)));
		return lightColor * lightColorMultiplier.xxx;
#endif
	}

	float3 GetTransmissionInput(float3 L, float3 V, float3 N, float3 lightColor, float3 baseColor, float amount)
	{
	#if USE_PIXL_VEGETATION_BRDF
		float backLighting = saturate(-dot(N, L));
		float grazingView = sqrt(saturate(1.0f - abs(dot(N, V))));
		float forwardScatter = pow(saturate(dot(-L, V)), 4.0f);
		float transmission = saturate(backLighting * (0.65f + 0.35f * grazingView) + forwardScatter * 0.25f);
		float enhancedAmount = amount;
		if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0)
			enhancedAmount *= SharedData::foliageDynamicsSettings.LeafTransmission;
		float thickness = saturate(enhancedAmount);
		float3 transmissionTint = ThinSurfaceTransmissionTint(baseColor, thickness);
		return lightColor * transmissionTint * transmission * thickness * 0.86f;
	#else
		return 0.0f.xxx;
	#endif
	}

	float3 GetGrassDirectionalTransmissionInput(
		float3 L, float3 V, float3 N, float3 lightColor, float3 baseColor, float amount)
	{
	#if USE_PIXL_VEGETATION_BRDF
		float3 directionalTransmission =
			GetTransmissionInput(L, V, N, lightColor, baseColor, amount);

		// Grass cards are face-oriented for two-sided raster lighting, so a small
		// first/third-person camera displacement can flip which sheet hemisphere is
		// presented. Preserve the stronger physical back-light lobe, but provide a
		// modest normal-sign-invariant multiple-scatter floor from real sun incidence.
		// This is not an ambient brightness floor: zero directional light or grazing
		// incidence still produces zero, and normal world-shadow attenuation remains.
		float sheetIncidence = saturate(abs(dot(N, L)));
		float enhancedAmount = amount;
		if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0)
			enhancedAmount *= SharedData::foliageDynamicsSettings.LeafTransmission;
		float3 viewStableScatter =
			max(lightColor, 0.0f.xxx) *
			ThinSurfaceTransmissionTint(baseColor, saturate(enhancedAmount)) *
			(sheetIncidence * 0.18f) *
			saturate(enhancedAmount);
		return max(directionalTransmission, viewStableScatter);
	#else
		return 0.0f.xxx;
	#endif
	}

	float3 GetFoliageDiffuseWrap(float3 L, float3 N, float3 lightColor, float3 baseColor, float shadow)
	{
		if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation == 0)
			return 0.0f.xxx;
		float wrap = saturate(SharedData::foliageDynamicsSettings.LeafDiffuseWrap);
		float noL = dot(N, L);
		float wrappedNoL = saturate((noL + wrap) / (1.0f + wrap));
		float extra = max(wrappedNoL - saturate(noL), 0.0f) * wrap * 0.35f;
		return lightColor * baseColor * extra * saturate(shadow) / 3.14159265f;
	}

	float3 TransformNormal(float3 normal)
	{
		return normal * 2.0f - 1.0f.xxx;
	}

	float3 TransformVegetationNormal(float3 normal, bool flipY)
	{
		float3 normalTS = TransformNormal(normal);
		[flatten] if (flipY)
			normalTS.y = -normalTS.y;
		return normalTS;
	}

	// http://www.thetenthplanet.de/archives/1180
	float3x3 CalculateTBN(float3 N, float3 p, float2 uv)
	{
		// get edge vectors of the pixel triangle
		float3 dp1 = ddx_coarse(p);
		float3 dp2 = ddy_coarse(p);
		float2 duv1 = ddx_coarse(uv);
		float2 duv2 = ddy_coarse(uv);

		// solve the linear system
		float3 dp2perp = cross(dp2, N);
		float3 dp1perp = cross(N, dp1);
		float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

		// construct a scale-invariant frame
		float invmax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-8f));
		return float3x3(T * invmax, B * invmax, N);
	}
}
