#ifndef __HAIR_DEPENDENCY_HLSL__
#define __HAIR_DEPENDENCY_HLSL__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Game.hlsli"
#include "Common/Math.hlsli"

namespace Hair
{
	#ifndef USE_PIXL_HAIR_SCATTERING
	#	define USE_PIXL_HAIR_SCATTERING 1
	#endif

	Texture2D<float> TexTangentShift : register(t73);

	// Physical fibre constants hoisted out of hot lighting paths. These values
	// were previously recomputed through pow/sqrt expressions and are invariant.
	static const float PIXL_HAIR_F0 = 0.0465205690f;
	static const float PIXL_INV_SQRT_TAU = 0.3989422804f;
	static const float PIXL_INV_LN2_HALF = 0.7213475204f;

	float Pow4(float x)
	{
		float x2 = x * x;
		return x2 * x2;
	}

	float Pow8(float x)
	{
		float x4 = Pow4(x);
		return x4 * x4;
	}

	float3 ReorientTangent(float3 T, float3 N)
	{
		// Reorient tangent to be orthogonal to normal. Explicit rsqrt avoids NaNs
		// on degenerate strand tangents while compiling to the same normalization
		// class of instructions as normalize().
		float3 projectedT = T - N * dot(T, N);
		return projectedT * rsqrt(max(dot(projectedT, projectedT), EPSILON_LENGTH_SQ));
	}

	// [Kajiya et al. 1989, "Rendering fur with three dimensional textures."]
	// https://doi.org/10.1145/74334.74361
	float3 D_KajiyaKay(float3 T, float3 H, float n)
	{
		float TH = dot(T, H);
		float sinTH = saturate(1 - TH * TH);
		float dirAtten = saturate(TH + 1);
		float norm = (n + 2) / (2 * Math::PI);
		return dirAtten * norm * pow(sinTH, 0.5 * n);
	}

	float3 HairF0()
	{
		return PIXL_HAIR_F0.xxx;
	}

	float3 ShiftTangent(float3 T, float3 N, float shift)
	{
		return normalize(T + N * shift);
	}

	float3 ShiftNormal(float3 T, float3 N, float shift)
	{
		float3 T_shifted = ShiftTangent(T, N, shift);
		// cross(T, cross(N,T)) is the projection of N onto the plane normal to T.
		// Use the algebraic form to remove two cross products from this hot path.
		float3 N_shifted = N - T_shifted * dot(N, T_shifted);
		return N_shifted * rsqrt(max(dot(N_shifted, N_shifted), EPSILON_LENGTH_SQ));
	}

	float3 ShiftWorldNormal(float3 T, float3 N, float n, float2 uv)
	{
		const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, 0).x - 0.5f;
		return ShiftNormal(T, N, shift + n);
	}

	// [Scheuermann 2004, "Hair Rendering and Shading"]
	// https://web.engr.oregonstate.edu/~mjb/cs557/Projects/Papers/HairRendering.pdf
	void GetHairDirectLightScheuermann(out float3 dirDiffuse, out float3 dirSpecular, out float3 dirTransmission, float3 T, float3 L, float3 V, float3 N, float3 VN, DirectContext context, float shininess, float2 uv, float3 baseColor)
	{
		const float3 H = normalize(L + V);
		const float oNdotL = dot(N, L);
		const float NdotL = saturate(oNdotL);
		const float VNdotV = dot(VN, V);
		const float HdotL = saturate(dot(H, L));
		const float wrapped = 0.5;

		float3 lightColor = context.lightColor * context.detailedShadow;
		float3 softColor = context.lightColor * context.softShadow * context.hairShadow;

		// [Yibing Jiang 2016, "The Process of Creating Volumetric-based Materials in Uncharted 4"]
		// https://advances.realtimerendering.com/s2016
		dirDiffuse = saturate(oNdotL + wrapped) / (1 + wrapped);
		float3 scatterColor = sqrt(max(baseColor, 0.0f.xxx));
		dirDiffuse = saturate(scatterColor + NdotL) * dirDiffuse * lightColor * SharedData::strandShadingSettings.DiffuseMult;

		float3 TshiftPrimary;
		float3 TshiftSecondary;

		if (SharedData::strandShadingSettings.EnableTangentShift) {
			const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, 0).x - 0.5;
			TshiftPrimary = ShiftTangent(T, N, shift + SharedData::strandShadingSettings.PrimaryTangentShift);
			TshiftSecondary = ShiftTangent(T, N, shift + SharedData::strandShadingSettings.SecondaryTangentShift);
		} else {
			TshiftPrimary = T;
			TshiftSecondary = T;
		}

		const float3 specPrimary = D_KajiyaKay(TshiftPrimary, H, shininess);
		const float3 specSecondary = D_KajiyaKay(TshiftSecondary, H, shininess * 0.5);
		const float3 F = BRDF::F_Schlick(HairF0(), HdotL);
		float frontFiber = VNdotV > 0.0f ? 1.0f : 0.0f;
		float3 specR = 0.25f * F * (specPrimary + specSecondary * scatterColor) * NdotL * frontFiber;

		// Integer Fresnel exponents are cheaper and more deterministic as multiply
		// chains than generic pow() calls, with the same mathematical result.
		float scatterBase1 = saturate(-dot(L, V));
		float scatterBase2 = saturate(1.0f - VNdotV * VNdotV);
		float scatterBase3 = saturate(abs(1.0f - VNdotV));
		float scatterFresnel1 = (Pow8(scatterBase1) * scatterBase1) * (Pow8(scatterBase2) * Pow4(scatterBase2));
		float scatterFresnel2 = Pow8(scatterBase3);
		scatterFresnel2 *= scatterFresnel2 * Pow4(scatterBase3); // x^20
		float3 specT = (scatterFresnel1 + scatterFresnel2 * scatterColor) * SharedData::strandShadingSettings.Transmission;
		dirSpecular = specR * lightColor * SharedData::strandShadingSettings.SpecularMult;
		dirTransmission = specT * softColor * SharedData::strandShadingSettings.SpecularMult;
	}

	float Hair_g(float B, float Theta)
	{
		// Gaussian longitudinal lobe. Use one reciprocal and native exp2; clamping
		// B also prevents the zero-width singularity at perfectly smooth settings.
		float invB = rcp(max(B, 1e-3f));
		float thetaOverB = Theta * invB;
		return exp2(-PIXL_INV_LN2_HALF * thetaOverB * thetaOverB) * (PIXL_INV_SQRT_TAU * invB);
	}

	// [Marschner et al. 2003, "Light reflection from human hair fibers."]
	// https://graphics.stanford.edu/papers/hair/hair-sg03final.pdf
	// N is the vector parallel to hair pointing toward root
	float3 D_Marschner(float3 L, float3 V, float3 N, float roughness, float3 baseColor, float area, float backlit)
	{
		const float NdotL = dot(N, L);
		const float NdotV = dot(N, V);
		const float VdotL = dot(V, L);

		float cosThetaL = sqrt(max(0, 1 - NdotL * NdotL));
		float cosThetaV = sqrt(max(0, 1 - NdotV * NdotV));
		float cosThetaD = sqrt(max((1.0f + cosThetaL * cosThetaV + NdotV * NdotL) * 0.5f, 1e-6f));
		float invCosThetaD = rcp(max(cosThetaD, 1e-3f));

		const float3 Lp = L - NdotL * N;
		const float3 Vp = V - NdotV * N;
		const float cosPhi = dot(Lp, Vp) * rsqrt(dot(Lp, Lp) * dot(Vp, Vp) + EPSILON_DIVISION);
		const float cosHalfPhi = sqrt(saturate(0.5 + 0.5 * cosPhi));

		float n_prime = 1.19f * invCosThetaD + 0.36f * cosThetaD;

		const float Shift = 0.0499f;
		const float Alpha[] = {
			-Shift * 2,
			Shift,
			Shift * 4
		};
		float B[] = {
			area + roughness,
			area + roughness / 2,
			area + roughness * 2
		};

		float3 F0 = HairF0();
		float3 safeBaseColor = max(abs(baseColor), 1e-6f.xxx);
		float3 logBaseColor = log2(safeBaseColor);

		float3 Tp;
		float Mp, Np, Fp, a, h, f;
	#if USE_PIXL_HAIR_SCATTERING
		// Marschner's longitudinal distribution is defined over elevation angles,
		// not their sine-like tangent dot products. Using the actual half angle
		// keeps highlights stable as strands turn through grazing incidence.
		float ThetaH = 0.5f * (asin(clamp(NdotL, -1.0f, 1.0f)) + asin(clamp(NdotV, -1.0f, 1.0f)));
	#else
		float ThetaH = NdotL + NdotV;
	#endif

		float3 R, TT, TRT;

		// R
		Mp = Hair_g(B[0], ThetaH - Alpha[0]);
		Np = 0.25 * cosHalfPhi;
		Fp = BRDF::F_Schlick(F0, sqrt(saturate(0.5 + 0.5 * VdotL))).x;
		R = (Mp * Np) * (Fp * lerp(1, backlit, saturate(-VdotL)));

		// TT
		Mp = Hair_g(B[1], ThetaH - Alpha[1]);
		a = rcp(max(n_prime, 1e-3f));
		h = cosHalfPhi * (1 + a * (0.6 - 0.8 * cosPhi));
		f = BRDF::F_Schlick(F0, cosThetaD * sqrt(saturate(1 - h * h))).x;
		Fp = (1 - f) * (1 - f);
		float ttExponent = 0.5f * sqrt(saturate(1.0f - (h * a) * (h * a))) * invCosThetaD;
		Tp = exp2(logBaseColor * ttExponent);
		Np = exp(-3.65 * cosPhi - 3.98);
		TT = (Mp * Np) * (Fp * Tp) * backlit;

		// TRT
		Mp = Hair_g(B[2], ThetaH - Alpha[2]);
		f = BRDF::F_Schlick(F0, cosThetaD * 0.5f).x;
		Fp = (1 - f) * (1 - f) * f;
		Tp = exp2(logBaseColor * (0.8f * invCosThetaD));
		Np = exp(17 * cosPhi - 16.78);
		TRT = (Mp * Np) * (Fp * Tp);

		float3 scattering = max(R + TT + TRT, 0.0f.xxx);
	#if USE_PIXL_HAIR_SCATTERING
		// Cheap multiple-scattering approximation [d'Eon et al. 2011, "An
		// Energy-Conserving Hair Reflectance Model"]. A single-fibre BRDF alone goes
		// fully black wherever the direct lobes vanish (occluded strands, hair
		// roots, deep grazing angles) - real hair never does, because light bounces
		// between neighbouring fibres before reaching the eye. A small colour-tinted
		// floor, proportional to the fibre's own pigment and gated softly by NdotL,
		// restores that characteristic soft "glow" without touching the primary
		// highlight lobes above or requiring any new tunable.
		float3 multipleScatterFloor = safeBaseColor * (0.05f * saturate(NdotL * 0.5f + 0.5f));
		scattering += multipleScatterFloor;

		// Bound the analytic lobes to the energy carried by one fibre interaction.
		// This prevents pale hair from producing super-white fireflies under small
		// inverse-square lights without flattening the coloured transmission lobes.
		scattering *= rcp(1.0f + max(max(scattering.r, scattering.g), scattering.b));
	#endif
		scattering = (any(isnan(scattering)) || any(isinf(scattering))) ? 0.0f.xxx : scattering;
		return max(scattering, 0.0f.xxx);
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L, float shadow, float3 baseColor)
	{
		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 projectedV = V - N * NdotV;
		float3 fakeN = projectedV * rsqrt(max(dot(projectedV, projectedV), EPSILON_LENGTH_SQ));
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = max(Color::RGBToLuminance(baseColor), 1e-4);
		float3 scatterTint = shadow < 1 ? pow(abs(baseColor / luma), 1 - shadow) : 1;
		S += sqrt(max(baseColor, 0.0f.xxx)) * diffuseScatter * scatterTint;

		S = (any(isnan(S)) || any(isinf(S))) ? 0.0f.xxx : S;
		return max(S, 0);
	}

	void GetHairDirectLightMarschner(out float3 dirDiffuse, out float3 dirSpecular, out float3 dirTransmission, float3 T, float3 L, float3 V, float3 N, float3 VN, DirectContext context, float shininess, float2 uv, float3 baseColor)
	{
		float3 lightColor = context.lightColor * Color::PBRLightingCompensation;
		dirDiffuse = 0;
		dirSpecular = 0;
		dirTransmission = 0;
		const float roughness = 1 - saturate(shininess * 0.01);

		if (SharedData::strandShadingSettings.EnableTangentShift) {
			const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, 0).x - 0.5;
			T = ShiftTangent(T, N, shift);
		}

		float shadow = context.hairShadow * context.detailedShadow;

		float reconstructionTransmission = 1.0f;
#if defined(HAIR_RECONSTRUCTION)
		reconstructionTransmission = SharedData::hairReconstructionSettings.Enabled != 0u
			? SharedData::hairReconstructionSettings.Transmission
			: 1.0f;
#endif
		dirTransmission += D_Marschner(L, V, T, roughness, baseColor, 0, SharedData::strandShadingSettings.Transmission * reconstructionTransmission) * lightColor * shadow * SharedData::strandShadingSettings.SpecularMult;
		dirTransmission += GetHairDiffuseAttenuationKajiyaKay(T, V, L, shadow, baseColor) * lightColor * shadow * SharedData::strandShadingSettings.DiffuseMult;
	}

	void GetHairDirectLight(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv)
	{
		const float3 T = normalize(context.worldNormal);
		const float3 V = normalize(context.viewDir);
		const float3 N = normalize(context.vertexNormal);
		const float3 VN = normalize(tbnTr[2]);
		const float3 L = normalize(context.lightDir);

		if (SharedData::strandShadingSettings.HairMode == 0) {
			GetHairDirectLightScheuermann(lightingOutput.diffuse, lightingOutput.specular, lightingOutput.transmission, T, L, V, N, VN, context, material.Shininess, uv, material.BaseColor);
		} else {
			GetHairDirectLightMarschner(lightingOutput.diffuse, lightingOutput.specular, lightingOutput.transmission, T, L, V, N, VN, context, material.Shininess, uv, material.BaseColor);
		}
	}

	void GetHairIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, float2 uv)
	{
		lobeWeights = (IndirectLobeWeights)0;

		float3 T = normalize(context.worldNormal);
		const float3 V = normalize(context.viewDir);
		const float3 N = normalize(context.vertexNormal);

		if (SharedData::strandShadingSettings.HairMode == 1) {
			if (SharedData::strandShadingSettings.EnableTangentShift) {
				const float shift = TexTangentShift.SampleLevel(SampColorSampler, uv, 0).x - 0.5;
				T = ShiftTangent(T, N, shift);
			}
			float3 projectedV = V - T * dot(V, T);
			float3 L = projectedV * rsqrt(max(dot(projectedV, projectedV), EPSILON_LENGTH_SQ));

			float3 environmentSpecular = D_Marschner(L, V, T, 1 - saturate(material.Shininess * 0.01), material.BaseColor, 0.2, 0) * Math::PI;
			float3 environmentDiffuse = GetHairDiffuseAttenuationKajiyaKay(T, V, L, 1, material.BaseColor) * Math::PI;
		#if USE_PIXL_HAIR_SCATTERING
			lobeWeights.specular = saturate(environmentSpecular * SharedData::strandShadingSettings.SpecularIndirectMult);
			lobeWeights.diffuse = saturate(environmentDiffuse * SharedData::strandShadingSettings.DiffuseIndirectMult) *
			                      saturate(1.0f.xxx - lobeWeights.specular);
		#else
			lobeWeights.diffuse = environmentSpecular * SharedData::strandShadingSettings.SpecularIndirectMult;
			lobeWeights.diffuse += environmentDiffuse * SharedData::strandShadingSettings.DiffuseIndirectMult;
		#endif
			return;
		} else {
			lobeWeights.diffuse = saturate(material.BaseColor * SharedData::strandShadingSettings.DiffuseIndirectMult);
			float2 hairBRDF = BRDF::EnvBRDF(material.Roughness, saturate(dot(N, V)));
			float3 hairSpecularLobe = material.F0 * hairBRDF.x + hairBRDF.y;
			lobeWeights.diffuse *= (1 - hairSpecularLobe);
			lobeWeights.specular = saturate(hairSpecularLobe * SharedData::strandShadingSettings.SpecularIndirectMult);
		}
	}

	float3 Saturation(float3 color, float saturation)
	{
		float luminance = Color::RGBToLuminance(color);
		return saturate(lerp(float3(luminance, luminance, luminance), color, saturation));
	}

	float HairSelfShadow(float3 positionWS, float3 lightDirWS, float noise)
	{
		float selfShadowStrength = saturate(SharedData::strandShadingSettings.SelfShadowStrength);
		if (!SharedData::strandShadingSettings.EnableSelfShadow || selfShadowStrength <= 0.0f)
			return 1.0f;

		// Keep the original four-tap budget, but reject depth that is far in front of
		// the strand. The old test treated any foreground surface on the projected
		// ray as hair self-shadow, causing dark halos near heads, shoulders and walls.
		const int stepCount = 4;
		const float invStepCount = 0.25f;

		float3 positionVS = FrameBuffer::WorldToView(positionWS);
		float3 lightDirVS = FrameBuffer::WorldToView(lightDirWS, false);
		lightDirVS *= max(SharedData::strandShadingSettings.SelfShadowScale * GAME_UNIT_TO_CM, 0.05f);
		float3 rayStep = lightDirVS * invStepCount;

		// Positive-only sub-step jitter avoids starting behind the receiver surface.
		float jitter = 0.20f + saturate(noise) * 0.35f;
		float3 ray = positionVS + rayStep * jitter;
		float occlusionAccum = 0.0f;

		[unroll(stepCount)] for (int i = 0; i < stepCount; ++i)
		{
			ray += rayStep;
			float2 rayUV = FrameBuffer::ViewToUV(ray);
			if (FrameBuffer::IsOutsideFrame(rayUV))
				continue;

			float sampleDepth = SharedData::GetScreenDepth(rayUV);
			float depthDelta = ray.z - sampleDepth;
			float thickness = max(4.0f, abs(ray.z) * 0.0030f);

			// Soft in/out band instead of a hard boolean test. The hard test only
			// ever produces 5 discrete occlusion levels across the 4-tap budget,
			// which visibly stair-steps across smooth hair caps and ponytails.
			// Ramping in from the 0.5-unit self-offset and back out past the
			// thickness estimate gives a continuous gradient at the same cost.
			float rampIn = smoothstep(0.50f, 0.50f + 0.15f * thickness, depthDelta);
			float rampOut = 1.0f - smoothstep(0.70f * thickness, thickness, depthDelta);
			occlusionAccum += saturate(rampIn * rampOut);
		}

		if (occlusionAccum <= 0.0f)
			return 1.0f;

		float hitRatio = occlusionAccum * invStepCount;
		float occlusion = pow(saturate(hitRatio), max(SharedData::strandShadingSettings.SelfShadowExponent, 1e-3f));
		return 1.0f - occlusion * selfShadowStrength;
	}
}
#endif  //__HAIR_DEPENDENCY_HLSL__
