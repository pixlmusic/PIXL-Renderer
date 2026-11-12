#ifndef __SKY_BOUNCE_DEPENDENCY_HLSL__
#define __SKY_BOUNCE_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace SkyBounce
{
#if defined(SKY_BOUNCE_PROBE_REGISTER)
	Texture3D<sh2> SkyBounceProbeArray : register(SKY_BOUNCE_PROBE_REGISTER);
#elif defined(PSHADER)
	Texture3D<sh2> SkyBounceProbeArray : register(t50);
#endif

#if defined(SKY_BOUNCE_SHADOW_VIS)
	Texture3D<float> ShadowVisibilityProbeArray : register(t53);
#endif

	const static sh2 UNIT_SH = float4(sqrt(4.0 * Math::PI), 0, 0, 0);

	const static uint3 ARRAY_DIM = uint3(256, 256, 128);
	const static uint3 ARRAY_MASK = ARRAY_DIM - 1u;
	const static float3 ARRAY_SIZE = 10000.f * float3(1, 1, 0.5);
	const static float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;

	float GetFadeOutFactor(float3 positionMS)
	{
		// The probe volume scrolls around PosOffset.  The old fade evaluated raw
		// model-space coordinates and saturated before measuring the edge, which
		// exposed a camera-following strip where SkyBounce abruptly appeared.
		// Evaluate the same scrolled coordinate used by Sample() and retire the
		// contribution over a broad inner band so the finite volume is invisible.
		if (any((asuint(positionMS) & 0x7f800000u) == 0x7f800000u))
			return 0.0f;
		float3 positionMSAdjusted = positionMS - SharedData::skyBounceSettings.PosOffset.xyz;
		float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;
		if (any(uvw <= 0.0f) || any(uvw >= 1.0f))
			return 0.0f;
		float3 dists = min(uvw, 1 - uvw);
		float edgeDist = min(dists.x, min(dists.y, dists.z));
		float fade = saturate(edgeDist / 0.18f);
		return fade * fade * fade * (fade * (fade * 6.0f - 15.0f) + 10.0f);
	}

	float MixDiffuse(float visibility)
	{
		return lerp(saturate(SharedData::skyBounceSettings.MinDiffuseVisibility), 1.0, saturate(visibility));
	}

	float MixSpecular(float visibility)
	{
		return lerp(saturate(SharedData::skyBounceSettings.MinSpecularVisibility), 1.0, saturate(visibility));
	}

	float CosineLobeVisibility(sh2 skyBounceSH, float3 normal, float fadeOutFactor)
	{
		float normalLengthSq = dot(normal, normal);
		normal = any((asuint(normal) & 0x7f800000u) == 0x7f800000u) ||
			(asuint(normalLengthSq) & 0x7f800000u) == 0x7f800000u || normalLengthSq < EPSILON_LENGTH_SQ
			? float3(0, 0, 1)
			: normal * rsqrt(normalLengthSq);
		float visibility = SphericalHarmonics::FuncProductIntegral(skyBounceSH, SphericalHarmonics::EvaluateCosineLobe(normal)) / Math::PI;
		return lerp(1.0, saturate(visibility), fadeOutFactor);
	}

	float EvaluateDiffuse(sh2 skyBounceSH, float3 normal, float fadeOutFactor = 1.0)
	{
		return MixDiffuse(CosineLobeVisibility(skyBounceSH, normal, fadeOutFactor));
	}

	float EvaluateVisibility(sh2 skyBounceSH)
	{
		return MixSpecular(CosineLobeVisibility(skyBounceSH, float3(0, 0, 1), 1.0));
	}

	float EvaluateSpecular(sh2 skyBounceSH, sh2 specularLobe, float fadeOutFactor = 1.0)
	{
		float visibility = SphericalHarmonics::FuncProductIntegral(skyBounceSH, specularLobe);
		visibility = lerp(1.0, saturate(visibility), saturate(fadeOutFactor));
		return MixSpecular(visibility);
	}

#if defined(PSHADER)
	void ApplySkyBounce(inout float3 diffuseColor, inout float3 directionalAmbientColor, float3 albedo, float skyBounceDiffuse)
	{
		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;

		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);

		float3 linAmbient = Color::IrradianceToLinear(directionalAmbientColor);
		float3 multiBounceSkyBounce = MultiBounceAO(albedo, skyBounceDiffuse);
		directionalAmbientColor = Color::IrradianceToGamma(linAmbient * multiBounceSkyBounce);

		diffuseColor += directionalAmbientColor;
	}
#endif

#if defined(PSHADER) || defined(SKY_BOUNCE_PROBE_REGISTER)
	sh2 Sample(float3 positionMS, float3 normalWS
#if defined(SKY_BOUNCE_SHADOW_VIS)
		, out float shadowVisibility
#endif
	)
		{
	#if defined(SKY_BOUNCE_SHADOW_VIS)
			shadowVisibility = 1.0;
	#endif

			const sh2 skyOnly = float4(3.5449077e10f, 0.0f, 0.0f, 0.0f);
			if (SharedData::InInterior || any((asuint(positionMS) & 0x7f800000u) == 0x7f800000u))
				return skyOnly;

				float normalLengthSq = dot(normalWS, normalWS);
				normalWS = any((asuint(normalWS) & 0x7f800000u) == 0x7f800000u) ||
					(asuint(normalLengthSq) & 0x7f800000u) == 0x7f800000u || normalLengthSq < EPSILON_LENGTH_SQ
					? float3(0.0f, 0.0f, 1.0f)
					: normalWS * rsqrt(normalLengthSq);
				positionMS.xyz += normalWS * CELL_SIZE * 0.5;  // Receiver normal bias

				float3 positionMSAdjusted = positionMS - SharedData::skyBounceSettings.PosOffset.xyz;
				float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

				if (all(uvw >= 0) && all(uvw <= 1)) {
					float3 cellVxCoord = uvw * ARRAY_DIM;
					int3 cell000 = floor(cellVxCoord - 0.5);
					float3 trilinearPos = cellVxCoord - 0.5 - cell000;
					float3 trilinearInv = 1.0f - trilinearPos;
					float3 cell000CentreMS = (float3(cell000) + 0.5f - float3(ARRAY_DIM) * 0.5f) * CELL_SIZE;
					uint3 arrayOrigin = uint3(SharedData::skyBounceSettings.ArrayOrigin.xyz);

					sh2 shSum = 0;
					float shWsum = 0;
	#if defined(SKY_BOUNCE_SHADOW_VIS)
					float shadowSum = 0;
					float shadowWsum = 0;
	#endif

					[unroll] for (int i = 0; i < 2; i++)
						[unroll] for (int j = 0; j < 2; j++)
							[unroll] for (int k = 0; k < 2; k++) {
								int3 cellOffset = int3(i, j, k);
								int3 cellID = cell000 + cellOffset;

								if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
									continue;

								float3 cellCentreMS = cell000CentreMS + float3(cellOffset) * CELL_SIZE;

								float triW =
									(i != 0 ? trilinearPos.x : trilinearInv.x) *
									(j != 0 ? trilinearPos.y : trilinearInv.y) *
									(k != 0 ? trilinearPos.z : trilinearInv.z);
				if (triW <= EPSILON_WEIGHT_SUM)
					continue;

								uint3 cellTexID = (uint3(cellID) + arrayOrigin) & ARRAY_MASK;

								// https://handmade.network/p/75/monter/blog/p/7288-engine_work__global_illumination_with_irradiance_probes
								// basic tangent checks
								float3 tangentDelta = cellCentreMS - positionMSAdjusted;
								float tangentInvLength = rsqrt(max(dot(tangentDelta, tangentDelta), EPSILON_LENGTH_SQ));
								float tangentWeight = saturate(dot(tangentDelta, normalWS) * tangentInvLength * 0.5f + 0.5f);
								tangentWeight *= tangentWeight * (3.0f - 2.0f * tangentWeight);
								float shW = triW * tangentWeight;
				if (shW > EPSILON_WEIGHT_SUM) {
					shSum = SphericalHarmonics::Add(shSum, SphericalHarmonics::Scale(SkyBounceProbeArray[cellTexID], shW));
					shWsum += shW;
				}

	#if defined(SKY_BOUNCE_SHADOW_VIS)
								shadowSum += ShadowVisibilityProbeArray[cellTexID] * triW;
								shadowWsum += triW;
	#endif
							}

	#if defined(SKY_BOUNCE_SHADOW_VIS)
					float fadeOut = GetFadeOutFactor(positionMS);
					shadowVisibility = lerp(1.0, saturate(shadowSum / max(shadowWsum, EPSILON_WEIGHT_SUM)), fadeOut);
	#endif
				if (shWsum > EPSILON_WEIGHT_SUM)
					return SphericalHarmonics::Scale(shSum, rcp(shWsum));
				}

			return skyOnly;
	}

	float GetSkyBounceDiffuse(sh2 skyBounceSH, float3 positionMS, float3 evalNormal, float vertexAO = 1.0)
	{
		if (SharedData::InInterior)
			return 1.0;
		if (any((asuint(positionMS) & 0x7f800000u) == 0x7f800000u))
			return 1.0f;

		float3 candidateNormal = float3(evalNormal.xy, max(0.0, evalNormal.z));
		float3 biasedNormal = dot(candidateNormal, candidateNormal) > 1e-6
			? normalize(candidateNormal)
			: float3(0, 0, 1);
		float fadeOutFactor = GetFadeOutFactor(positionMS);
		float skyBounceDiffuse = EvaluateDiffuse(skyBounceSH, biasedNormal, fadeOutFactor);

		return saturate(skyBounceDiffuse / max(vertexAO, EPSILON_DIVISION));
	}

		sh2 SampleNoBias(float3 positionMS)
		{
			const sh2 skyOnly = float4(3.5449077e10f, 0.0f, 0.0f, 0.0f);
			if (any((asuint(positionMS) & 0x7f800000u) == 0x7f800000u))
				return skyOnly;
			float3 positionMSAdjusted = positionMS - SharedData::skyBounceSettings.PosOffset.xyz;
			float3 uvw = positionMSAdjusted / ARRAY_SIZE + .5;

			if (!SharedData::InInterior && all(uvw >= 0) && all(uvw <= 1)) {
				float3 cellVxCoord = uvw * ARRAY_DIM;
				int3 cell000 = floor(cellVxCoord - 0.5);
				float3 trilinearPos = cellVxCoord - 0.5 - cell000;
				float3 trilinearInv = 1.0f - trilinearPos;
				uint3 arrayOrigin = uint3(SharedData::skyBounceSettings.ArrayOrigin.xyz);

				sh2 sum = 0;
				float wsum = 0;
				[unroll] for (int i = 0; i < 2; i++)
					[unroll] for (int j = 0; j < 2; j++)
						[unroll] for (int k = 0; k < 2; k++)
				{
					int3 offset = int3(i, j, k);
					int3 cellID = cell000 + offset;

					if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
						continue;

					float w =
						(i != 0 ? trilinearPos.x : trilinearInv.x) *
						(j != 0 ? trilinearPos.y : trilinearInv.y) *
						(k != 0 ? trilinearPos.z : trilinearInv.z);
				if (w <= EPSILON_WEIGHT_SUM)
					continue;

					uint3 cellTexID = (uint3(cellID) + arrayOrigin) & ARRAY_MASK;
					sum = SphericalHarmonics::Add(sum, SphericalHarmonics::Scale(SkyBounceProbeArray[cellTexID], w));
					wsum += w;
				}
			if (wsum > EPSILON_WEIGHT_SUM)
				return SphericalHarmonics::Scale(sum, rcp(wsum));
			}

			return skyOnly;
		}
#endif
}

#endif
