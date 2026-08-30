#ifndef LIGHTING_EVAL_HLSLI
#define LIGHTING_EVAL_HLSLI
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#if defined(MATERIAL_FORGE)
#	include "Common/PBR.hlsli"
#endif

#if defined(MATERIAL_FORGE)
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 coatWorldNormal, float3 vertexNormal, float3 viewDir, float3 coatViewDir, float3 lightDir, float3 coatLightDir, float3 lightColor, float detailedShadow, float softShadow)
#else
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir, float3 lightDir, float3 lightColor, float detailedShadow, float softShadow)
#endif
{
	DirectContext context = (DirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	context.lightDir = normalize(lightDir);
	context.halfVector = normalize(context.viewDir + context.lightDir);
	context.lightColor = lightColor;
	context.detailedShadow = detailedShadow;
	context.softShadow = softShadow;
#if defined(MATERIAL_FORGE)
	context.coatWorldNormal = normalize(coatWorldNormal);
	context.coatViewDir = normalize(coatViewDir);
	context.coatLightDir = normalize(coatLightDir);
	context.coatHalfVector = normalize(context.coatViewDir + context.coatLightDir);
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		context.coatLightColor = lightColor * softShadow;
	}
	else
	{
		context.coatLightColor = context.lightColor * detailedShadow;
	}
#endif
	return context;
}

IndirectContext CreateIndirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir)
{
	IndirectContext context = (IndirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	return context;
}

/** PIXL's finite-emitter inverse-square path before user blending. */
float GetPhysicalLocalLightAttenuation(float distance, float radius, float fadeZone, float sizeBias)
{
	float emitterRadius = max(SharedData::materialForgeSettings.LocalLightMinimumDistance, 1.0f);
	float denominator = distance * distance + max(sizeBias, emitterRadius * emitterRadius);
	float physicalAttenuation = (0.8f * METRES_TO_UNITS * METRES_TO_UNITS) / denominator;

	float effectiveFadeZone = fadeZone > 0.0f ? fadeZone : rcp(max(radius * 0.2f, 1.0f));
	float cutoff = saturate((radius - distance) * effectiveFadeZone);
	cutoff = cutoff * cutoff * (3.0f - 2.0f * cutoff);
	return physicalAttenuation * cutoff;
}

/**
 * PIXL local-light attenuation. The final float in MaterialForge's stable
 * five-register block is named pad0 in SharedData for cache/ABI compatibility;
 * C++ owns it as PhysicalLocalLightFalloffStrength at the same byte offset.
 */
float GetPhysicalLocalLightFalloffStrength()
{
	return SharedData::materialForgeSettings.EnablePhysicalLocalLightFalloff != 0 ?
		saturate(SharedData::materialForgeSettings.pad0) : 0.0f;
}

float GetLocalLightAttenuation(float distance, float radius, float fadeZone, float sizeBias)
{
	float normalizedDistance = saturate(distance / max(radius, 1.0f));
	float legacyAttenuation = 1.0f - normalizedDistance * normalizedDistance;
	float physicalBlend = GetPhysicalLocalLightFalloffStrength();
	if (physicalBlend <= 0.0f)
		return legacyAttenuation;

	float physicalAttenuation = GetPhysicalLocalLightAttenuation(distance, radius, fadeZone, sizeBias);
	return lerp(legacyAttenuation, physicalAttenuation, physicalBlend);
}

float3 VanillaSpecular(DirectContext context, float shininess, float2 uv, float2 uv_ddx, float2 uv_ddy)
{
	const float3 N = context.worldNormal;
	const float3 G = context.vertexNormal;
	float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;
	float HdotN;
#if defined(ANISO_LIGHTING)
	const float3 AN = normalize(N * 0.5 + G);
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#else
	HdotN = saturate(dot(H, N));
#endif

#if defined(SPECULAR)
	float lightColorMultiplier = exp2(shininess * log2(HdotN));

#elif defined(SPARKLE)
	float lightColorMultiplier = 0;
#else
	float lightColorMultiplier = HdotN;
#endif

#if defined(ANISO_LIGHTING)
	lightColorMultiplier *= 0.7 * max(0, L.z);
#endif

#if defined(SPARKLE) && !defined(SNOW)
	float3 sparkleUvScale = exp2(float3(1.3, 1.6, 1.9) * log2(abs(SparkleParams.x)).xxx);

	float sparkleColor1 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.xx, uv_ddx * sparkleUvScale.x, uv_ddy * sparkleUvScale.x).z;
	float sparkleColor2 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.yy, uv_ddx * sparkleUvScale.y, uv_ddy * sparkleUvScale.y).z;
	float sparkleColor3 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.zz, uv_ddx * sparkleUvScale.z, uv_ddy * sparkleUvScale.z).z;
	float sparkleColor = ProcessSparkleColor(sparkleColor1) + ProcessSparkleColor(sparkleColor2) + ProcessSparkleColor(sparkleColor3);
	float VdotN = dot(V, N);
	V += N * -(2 * VdotN);
	float sparkleMultiplier = exp2(SparkleParams.w * log2(max(saturate(dot(V, -L)), 1e-6f))) * (SparkleParams.z * sparkleColor);
	sparkleMultiplier = sparkleMultiplier >= 0.5 ? 1 : 0;
	lightColorMultiplier += sparkleMultiplier * HdotN;
#endif
	return lightColorMultiplier;
}

#if !defined(MATERIAL_FORGE)
/**
 * Evaluate a normalized GGX direct-light response for a converted Skyrim material.
 *
 * Color::PBRLightingCompensation preserves the diffuse calibration of Skyrim's
 * gamma-space lighting mode while remaining one in linear-lighting mode.
 */
void EvaluateLegacyPhysicalDirect(DirectContext context, MaterialProperties material, inout DirectLightingOutput lightingOutput)
{
	const float3 N = context.worldNormal;
	const float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;

	const float NdotL = saturate(dot(N, L));
	const float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	const float NdotH = saturate(dot(N, H));
	const float VdotH = saturate(dot(V, H));
	const float safeNdotL = max(NdotL, EPSILON_DOT_CLAMP);
	const float3 lightColor = context.lightColor * context.detailedShadow * Color::PBRLightingCompensation;

	lightingOutput.diffuse = NdotL * lightColor * BRDF::Diffuse_Lambert();

	[branch] if (any(material.F0 > 0.0f))
	{
		const float D = BRDF::D_GGX(material.Roughness, NdotH);
		const float visibility = BRDF::Vis_SmithJointApprox(material.Roughness, NdotV, safeNdotL);
		const float3 F = BRDF::F_Schlick(material.F0, VdotH);
		const float legacySpecularScale = max(SharedData::materialForgeSettings.LegacyPhysicalSpecularScale, 0.0f);

		lightingOutput.diffuse *= 1.0f - saturate(F * legacySpecularScale);
		float3 energyCompensation = float3(1.0f, 1.0f, 1.0f);
		[branch] if (SharedData::materialForgeSettings.EnableGGXMultiScatter != 0)
			energyCompensation = BRDF::GGXMultiScatterCompensation(
				material.F0, material.Roughness, NdotV,
				SharedData::materialForgeSettings.GGXMultiScatterStrength * material.Metallic);
		lightingOutput.specular = D * visibility * F * energyCompensation * NdotL * lightColor * legacySpecularScale;
	}
}

void EvaluateLegacyVanillaDirect(DirectContext context, MaterialProperties material, float2 uv, float2 uv_ddx, float2 uv_ddy, inout DirectLightingOutput lightingOutput)
{
	const float NdotL = dot(context.worldNormal, context.lightDir);
	const float3 diffuseLightColor = context.lightColor * context.detailedShadow;
	lightingOutput.diffuse = saturate(NdotL) * diffuseLightColor * Color::VanillaNormalization();
	lightingOutput.specular = VanillaSpecular(context, material.Shininess, uv, uv_ddx, uv_ddy) *
	                          material.SpecularColor * material.Glossiness * diffuseLightColor *
	                          Color::VanillaNormalization();
}
#endif

namespace PhysicalLighting
{
	/**
	 * Evaluate the selected surface model for one direct light.
	 *
	 * All material families enter through this function. Compile-time adapters preserve the
	 * shipping legacy, skin, hair, and Material Forge implementations while presenting one interface
	 * to raster lighting and future ray-based integrators.
	 */
	void EvaluateDirect(
		DirectContext context,
		MaterialProperties material,
		float3x3 tbnTr,
		float2 uv,
		float2 uv_ddx,
		float2 uv_ddy,
		bool allowEnhancedLighting,
		out DirectLightingOutput lightingOutput,
		out float3 legacyPhysicalDelta,
		out float legacyPhysicalApplied)
	{
		lightingOutput = (DirectLightingOutput)0;
		legacyPhysicalDelta = 0.0.xxx;
		legacyPhysicalApplied = 0.0;
#if defined(MATERIAL_FORGE)
		PBR::GetDirectLightInput(lightingOutput, context, material, tbnTr, uv);
#else
#	if !defined(USE_PIXL_WORLD_SCOPED_PHYSICAL_LIGHTING)
#		define USE_PIXL_WORLD_SCOPED_PHYSICAL_LIGHTING 1
#	endif
#	if USE_PIXL_WORLD_SCOPED_PHYSICAL_LIGHTING
		// Loading-screen, inventory and lockpick preview lights were authored for
		// Skyrim's legacy normalization. Preserve that response outside the world;
		// normalized GGX and inverse-square lighting remain active during gameplay.
		if (!allowEnhancedLighting) {
			EvaluateLegacyVanillaDirect(context, material, uv, uv_ddx, uv_ddy, lightingOutput);
			return;
		}
#	endif
#	if defined(HAIR) && defined(STRAND_SHADING)
		if (SharedData::strandShadingSettings.Enabled) {
			Hair::GetHairDirectLight(lightingOutput, context, material, tbnTr, uv);
			return;
		}
#	endif
#	if defined(SKIN) && defined(PIXL_SKIN)
		if (SharedData::skinOpticsData.skinParams.w > 0.0f) {
			SkinOptics::SkinDirectLightInput(lightingOutput, context, material);
			float3 softLightColor = context.lightColor * context.softShadow;

			// SSS fallback for forward skin rendering
#		if !defined(DEFERRED)
			const float NdotL = dot(context.worldNormal, context.lightDir);
#			if defined(SOFT_LIGHTING)
			lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor;
#			endif

#			if defined(RIM_LIGHTING)
			lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor;
#			endif

#			if defined(BACK_LIGHTING)
			lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor;
#			endif
#		endif
			return;
		}
#	endif
		const float NdotL = dot(context.worldNormal, context.lightDir);
		float3 softLightColor = context.lightColor * context.softShadow;
#	if !defined(SPARKLE)
		const bool physicalEnabled = SharedData::materialForgeSettings.EnableLegacyPhysicalDirectLighting != 0 && allowEnhancedLighting;
		const bool comparePhysical = SharedData::materialForgeSettings.LegacyPhysicalDebugMode == 9;
		DirectLightingOutput physicalOutput = (DirectLightingOutput)0;
		DirectLightingOutput vanillaOutput = (DirectLightingOutput)0;
		[branch] if (physicalEnabled || comparePhysical)
			EvaluateLegacyPhysicalDirect(context, material, physicalOutput);
		[branch] if (!physicalEnabled || comparePhysical)
			EvaluateLegacyVanillaDirect(context, material, uv, uv_ddx, uv_ddy, vanillaOutput);
		if (physicalEnabled)
			lightingOutput = physicalOutput;
		else
			lightingOutput = vanillaOutput;
		legacyPhysicalApplied = physicalEnabled ? 1.0f : 0.0f;
		if (comparePhysical) {
			legacyPhysicalDelta = abs(
				(physicalOutput.diffuse - vanillaOutput.diffuse) * material.BaseColor +
				physicalOutput.specular - vanillaOutput.specular);
		}
#	endif
#	if defined(SPARKLE)
		{
			EvaluateLegacyVanillaDirect(context, material, uv, uv_ddx, uv_ddy, lightingOutput);
		}
#	endif
#	if defined(SOFT_LIGHTING)
		lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor * Color::VanillaNormalization();
#	endif

#	if defined(RIM_LIGHTING)
		lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor * Color::VanillaNormalization();
#	endif

#	if defined(BACK_LIGHTING)
		lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor * Color::VanillaNormalization();
#	endif
#endif
	}

	/** @brief Evaluate diffuse and specular weights for image-based and indirect lighting. */
	void EvaluateIndirect(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, float2 uv)
	{
		lobeWeights = (IndirectLobeWeights)0;
#if defined(MATERIAL_FORGE)
		PBR::GetIndirectLobeWeights(lobeWeights, context, material);
#else
#	if defined(HAIR) && defined(STRAND_SHADING)
		if (SharedData::strandShadingSettings.Enabled) {
			Hair::GetHairIndirectLobeWeights(lobeWeights, context, material, uv);
			return;
		}
#	endif
#	if defined(SKIN) && defined(PIXL_SKIN)
		if (SharedData::skinOpticsData.skinParams.w > 0.0f) {
			SkinOptics::SkinIndirectLobeWeights(lobeWeights, material, context);
			return;
		}
#	endif
		lobeWeights.diffuse = material.BaseColor;
#	if defined(WORLD_PROBES)
		if (any(material.F0 > 0.0)) {
			const float3 N = context.worldNormal;
			const float3 V = context.viewDir;
			const float3 VN = context.vertexNormal;

			float NdotV = saturate(dot(N, V));

			float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
			lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;
			[branch] if (SharedData::materialForgeSettings.EnableGGXMultiScatter != 0)
				lobeWeights.specular *= BRDF::GGXMultiScatterCompensation(
					material.F0, material.Roughness, NdotV,
					SharedData::materialForgeSettings.GGXMultiScatterStrength * material.Metallic);
			lobeWeights.specular = saturate(lobeWeights.specular);
			lobeWeights.diffuse *= 1 - lobeWeights.specular;
		}
#	endif
#endif
	}
}

#if defined(RAIN_RESPONSE)
void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, inout DirectLightingOutput lightingOutput)
{
	const float wetnessStrength = saturate(1 - roughness);
#	if defined(MATERIAL_FORGE)
	const float3 lightColor = context.coatLightColor;
#	else
	const float3 lightColor = context.lightColor * context.detailedShadow;
#	endif

	const float wetnessF0 = 0.02;

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;

	float NdotL = clamp(dot(N, L), EPSILON_DOT_CLAMP, 1);
	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	float D = BRDF::D_GGX(roughness, NdotH);
	float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
	float3 F = BRDF::F_Schlick(wetnessF0, VdotH);

	// Separate physical Fresnel from effective contribution weighted by strength
	float3 wetnessF = F * wetnessStrength;

	float3 wetnessSpecular = D * G * wetnessF * NdotL * lightColor;

#	if !defined(MATERIAL_FORGE)
	wetnessSpecular *= Color::PBRLightingCompensation * Color::PBRLightingScale;  // Compensate for GGX on traditional specular
#	endif

	lightingOutput.diffuse *= 1 - wetnessF;
	lightingOutput.specular *= 1 - wetnessF;
	lightingOutput.specular += wetnessSpecular;
}

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, IndirectContext context)
{
	const float wetnessF0 = 0.02;
	const float wetnessStrength = saturate(1 - roughness);

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;

	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 specularLobeWeight = wetnessF0 * specularBRDF.x + specularBRDF.y;

	specularLobeWeight *= wetnessStrength;

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	return specularLobeWeight;
}
#endif
#endif
