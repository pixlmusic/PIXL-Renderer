#ifndef __SKIN_HLSLI__
#define __SKIN_HLSLI__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/LightingCommon.hlsli"
#include "Common/Math.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"

namespace SkinOptics
{
	#ifndef USE_PIXL_SKIN_OPTICS
	#	define USE_PIXL_SKIN_OPTICS 1
	#endif

	// Screen-space normal derivatives alone change with resolution and camera distance.
	// Divide them by the matching world-position footprint to recover an approximate
	// macro curvature in inverse world units. Using the geometric/vertex normal at the
	// call site keeps pores and authored micro normals from being mistaken for facial
	// curvature and over-broadening the oil lobes.
	float CalculateCurvature(float3 N, float3 worldPosition)
	{
		const float3 dNdx = ddx(N);
		const float3 dNdy = ddy(N);
		const float3 dPdx = ddx(worldPosition);
		const float3 dPdy = ddy(worldPosition);

		const float normalGradientSq = dot(dNdx, dNdx) + dot(dNdy, dNdy);
		const float positionGradientSq = dot(dPdx, dPdx) + dot(dPdy, dPdy);
		return saturate(sqrt(normalGradientSq / max(positionGradientSq, 1e-6f)));
	}

#if defined(PSHADER)
	cbuffer SkinPerGeometry : register(b7)
	{
		float4 skinPerGeometry;
	};
#endif
#if defined(SKIN)
	Texture2D<float4> TexSkinDetailNormal : register(t72);

	// [Jorge Jimenez, Diego Gutierrez 2015, "Separable Tissue Diffusion"]
	// https://www.iryoku.com/separable-sss/
	float3 SSSSTransmittance(
		float translucency,
		float sssWidth,
		float3 worldNormal,
		float3 light,
		float3 view,
		float d)
	{
		/**
		* Calculate the scale of the effect.
		*/
		float scale = 8.25 * (1.0 - saturate(translucency)) / max(sssWidth, 1e-3f);

		/**
		* First we shrink the position inwards the surface to avoid artifacts:
		* (Note that this can be done once for all the lights)
		*/
		// float4 shrinkedPos = float4(worldPosition - 0.005 * worldNormal, 1.0);

		/**
		* Now we calculate the thickness from the light point of view:
		*/
		// float4 shadowPosition = mul(shrinkedPos, lightViewProjection);
		// float d1 = SSSSSampleShadowmap(shadowPosition.xy / shadowPosition.w).r; // 'd1' has a range of 0..1
		// float d2 = shadowPosition.z; // 'd2' has a range of 0..'lightFarPlane'
		// d1 *= lightFarPlane; // So we scale 'd1' accordingly:
		// float d = scale * abs(d1 - d2);
		d = scale * abs(d);  // Use the passed 'd' value instead of calculating it here.

		/**
		* Armed with the thickness, we can now calculate the color by means of the
		* precalculated transmittance profile.
		* (It can be precomputed into a texture, for maximum performance):
		*/
		float dd = -d * d;
		float3 profile = float3(0.233, 0.455, 0.649) * exp(dd / 0.0064) +
		                 float3(0.1, 0.336, 0.344) * exp(dd / 0.0484) +
		                 float3(0.118, 0.198, 0.0) * exp(dd / 0.187) +
		                 float3(0.113, 0.007, 0.007) * exp(dd / 0.567) +
		                 float3(0.358, 0.004, 0.0) * exp(dd / 1.99) +
		                 float3(0.078, 0.0, 0.0) * exp(dd / 7.41);

		/**
		* Using the profile, we finally approximate the transmitted lighting from
		* the back of the object:
		*/
		// Transmission is a back/terminator response, not an unshadowed diffuse fill.
		// The former constant 0.3 floor added red energy even on front-lit cheeks and
		// was a major source of the waxy look. A gentle grazing lift keeps thin ears and
		// nostrils readable without turning the entire face translucent.
		const float backLight = smoothstep(-0.10f, 0.65f, dot(light, -worldNormal));
		const float viewGrazing = 1.0f - saturate(abs(dot(worldNormal, view)));
		return profile * backLight * lerp(0.78f, 1.10f, viewGrazing * viewGrazing);
	}

	float3 DualSpecularGGX(float AverageRoughness, float Lobe0Roughness, float Lobe1Roughness, float LobeMix, float3 SpecularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
	#if USE_PIXL_SKIN_OPTICS
		LobeMix = saturate(LobeMix);
		Lobe0Roughness = clamp(Lobe0Roughness, 0.035f, 1.0f);
		Lobe1Roughness = clamp(Lobe1Roughness, 0.035f, 1.0f);
		float lobe0 = BRDF::D_GGX(Lobe0Roughness, NdotH) * BRDF::Vis_SmithJointApprox(Lobe0Roughness, NdotV, NdotL);
		float lobe1 = BRDF::D_GGX(Lobe1Roughness, NdotH) * BRDF::Vis_SmithJointApprox(Lobe1Roughness, NdotV, NdotL);
		F = BRDF::F_Schlick(saturate(SpecularColor), VdotH);
		return lerp(lobe0, lobe1, LobeMix) * F;
	#else
		float D = lerp(BRDF::D_GGX(Lobe0Roughness, NdotH), BRDF::D_GGX(Lobe1Roughness, NdotH), LobeMix);
		float G = BRDF::Vis_SmithJointApprox(AverageRoughness, NdotV, NdotL);
		F = BRDF::F_Schlick(SpecularColor, VdotH);

		return D * G * F;
	#endif
	}

	void SkinDirectLightInput(
		out DirectLightingOutput lightingOutput,
		DirectContext context,
		MaterialProperties material)
	{
		lightingOutput = (DirectLightingOutput)0;
		const float3 incidentLight = context.lightColor * Color::PBRLightingCompensation;
		context.lightColor = incidentLight * saturate(context.detailedShadow);

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;
		const float3 H = context.halfVector;

		const float oNdotL = dot(N, L);
		float NdotL = clamp(oNdotL, 1e-5, 1.0);
		float NdotV = saturate(abs(dot(N, V)) + 1e-5);
		float NdotH = saturate(dot(N, H));
		float VdotH = saturate(dot(V, H));

		context.lightColor *= ApproximateDirectOcculusion(material.AO, NdotL);

		// Curvature broadens the oily microfacet lobes instead of suppressing F0.
		// The previous F0 reduction made noses, cheeks and ears unnaturally matte
		// precisely where grazing reflectance should remain visible.
		float curvatureResponse = saturate(material.Curvature * 4.0f);
		float primaryRoughness = lerp(material.Roughness, 1.0f, curvatureResponse * 0.18f);
		float secondaryRoughness = lerp(material.RoughnessSecondary, 1.0f, curvatureResponse * 0.12f);
		float averageRoughness = clamp(lerp(primaryRoughness, secondaryRoughness,
			saturate(material.SecondarySpecIntensity)), 0.035f, 1.0f);

		lightingOutput.diffuse += context.lightColor * NdotL * BRDF::Diffuse_Burley(averageRoughness, NdotV, NdotL, VdotH);
		float3 F;
		float3 F0 = saturate(material.F0);

		lightingOutput.specular += DualSpecularGGX(averageRoughness, primaryRoughness, secondaryRoughness, material.SecondarySpecIntensity, F0, NdotL, NdotV, NdotH, VdotH, F) * context.lightColor * NdotL;

		lightingOutput.specular *= BRDF::GGXMultiScatterCompensation(F0, averageRoughness, NdotV, 1.0f);
		lightingOutput.diffuse *= 1 - F;

		if (material.FuzzWeight > 0.0) {
			float3 FuzzF0 = saturate(material.FuzzColor);
			float fuzzD = BRDF::D_Charlie(material.FuzzRoughness, NdotH);
			float fuzzG = BRDF::Vis_Neubelt(NdotV, NdotL);
			float3 fuzzF = BRDF::F_Schlick(FuzzF0, VdotH);
			float3 fuzzSpecular = fuzzD * fuzzG * fuzzF * context.lightColor * NdotL;
			float fuzzWeight = saturate(material.FuzzWeight) * (1.0f - curvatureResponse * 0.25f);
			float3 layerTransmission = 1.0f.xxx - fuzzF * fuzzWeight;
			lightingOutput.diffuse *= layerTransmission;
			lightingOutput.specular = lightingOutput.specular * layerTransmission + fuzzSpecular * fuzzWeight;
		}

		float3 sssTransmittance = SSSSTransmittance(
									  SharedData::skinOpticsData.sssParams.x,
									  SharedData::skinOpticsData.sssParams.y,
									  N,
									  L,
									  V,
									  material.Thickness) *
		                          SharedData::skinOpticsData.sssParams.w;
		// Detailed and soft shadow terms describe the same visibility path. Multiplying
		// both squared local-light shadows and over-darkened directional transmission.
		const float transmissionVisibility = min(
			saturate(context.detailedShadow),
			saturate(context.softShadow));
		const float3 visibleIncidentLight = incidentLight * transmissionVisibility;
		lightingOutput.transmission = min(
			sssTransmittance * visibleIncidentLight * saturate(material.BaseColor),
			visibleIncidentLight);
	}

	void SkinIndirectLobeWeights(
		out IndirectLobeWeights lobeWeights,
		MaterialProperties material,
		IndirectContext context)
	{
		lobeWeights = (IndirectLobeWeights)0;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		// Preserve both oil/epidermal lobes in image-based lighting. Collapsing them
		// into one average roughness made dialogue highlights look flat and caused a
		// visible mismatch when a face moved between a key light and ambient probes.
		const float curvatureResponse = saturate(material.Curvature * 4.0f);
		const float primaryRoughness = clamp(
			lerp(material.Roughness, 1.0f, curvatureResponse * 0.18f), 0.035f, 1.0f);
		const float secondaryRoughness = clamp(
			lerp(material.RoughnessSecondary, 1.0f, curvatureResponse * 0.12f), 0.035f, 1.0f);
		const float lobeMix = saturate(material.SecondarySpecIntensity);
		const float averageRoughness = lerp(primaryRoughness, secondaryRoughness, lobeMix);

		const float3 F0 = saturate(material.F0);
		const float2 primaryBRDF = BRDF::EnvBRDF(primaryRoughness, NdotV);
		const float2 secondaryBRDF = BRDF::EnvBRDF(secondaryRoughness, NdotV);
		float3 primarySpecular = F0 * primaryBRDF.x + primaryBRDF.y;
		float3 secondarySpecular = F0 * secondaryBRDF.x + secondaryBRDF.y;
		primarySpecular *= BRDF::GGXMultiScatterCompensation(F0, primaryRoughness, NdotV, 1.0f);
		secondarySpecular *= BRDF::GGXMultiScatterCompensation(F0, secondaryRoughness, NdotV, 1.0f);
		lobeWeights.specular = lerp(primarySpecular, secondarySpecular, lobeMix);

		lobeWeights.diffuse = material.BaseColor * saturate(1.0f.xxx - lobeWeights.specular);

		// Add a restrained ambient peach-fuzz response. This is intentionally strongest
		// at grazing angles and remains energy layered over the skin lobes.
		if (material.FuzzWeight > 0.0f) {
			const float grazing = 1.0f - NdotV;
			const float fuzzWeight = saturate(material.FuzzWeight) *
				(1.0f - curvatureResponse * 0.25f);
			const float3 fuzzF = BRDF::F_Schlick(saturate(material.FuzzColor), NdotV);
			const float3 fuzzTransmission = 1.0f.xxx - fuzzF * fuzzWeight;
			const float3 fuzzSpecular = fuzzF *
				lerp(0.08f, 0.35f, grazing * grazing) * fuzzWeight;
			lobeWeights.diffuse *= fuzzTransmission;
			lobeWeights.specular = lobeWeights.specular * fuzzTransmission + fuzzSpecular;
		}

		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon;
		lobeWeights.specular *= horizon;

		float3 diffuseAO = MultiBounceAO(saturate(material.BaseColor), saturate(material.AO.x));
		float3 specularAO = MultiBounceAO(F0,
			saturate(SpecularAOLagarde(NdotV, material.AO, averageRoughness).x));

		lobeWeights.diffuse *= diffuseAO;
		lobeWeights.specular *= specularAO;
	}

	float FBM(float2 uv, float base_scale, int octaves, float lacunarity, float persistence, float z_offset_multiplier)
	{
		float total = 0.0;
		float frequency = base_scale;
		float amplitude = 1.0;
		float max_amplitude = 0.0;
		[unroll] for (int i = 0; i < octaves; i++) {
			total += amplitude * (Random::perlinNoise(float3(uv * frequency, (float)i * z_offset_multiplier)) + 1.0) * 0.5;

			max_amplitude += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		if (max_amplitude > 0.0) {
			return total / max_amplitude;
		}
		return 0.0;
	}

	float PerlinNoise(float2 uv, float scale, float lacunarity, float persistence, float strength)
	{
		if (strength <= 0.001f) {
			return 0.0f;
		}
		if (strength >= 0.999f) {
			return 1.0f;
		}
		int octaves = 5;
		float z_offset_multiplier = 7.375f;

		float noise_value = FBM(uv, scale, octaves, lacunarity, persistence, z_offset_multiplier);

		float dynamic_threshold = 1.0f - strength;

		float sweat_intensity = saturate((noise_value - dynamic_threshold) / strength);

		sweat_intensity = pow(sweat_intensity, 1.5f);

		if (strength > 0.8f) {
			sweat_intensity = sweat_intensity * saturate(0.99f - (strength - 0.8f) * 5.0f) + (strength - 0.8f) * 5.0f;
		}
		return pow(saturate(sweat_intensity), 0.1f);
	}
#endif

	float2 GetWetness(float z, float3 modelNormal)
	{
		if (skinPerGeometry.x == 0.f && skinPerGeometry.y == 0.f)
			return 0.f;

		float waterWet = 0.0f;
		float waterLevel = skinPerGeometry.z + skinPerGeometry.w;

		waterWet = skinPerGeometry.y * (1 - smoothstep(waterLevel - 2.5f, waterLevel + 2.5f, z));

		float sweatWet = skinPerGeometry.x;
#if !defined(SKIN)
		sweatWet *= 1.0f - saturate(dot(modelNormal, float3(0, 0, 1)));
#endif
		return float2(sweatWet, waterWet);
	}
}

#endif  // __SKIN_HLSLI__
