// PIXL Renderer - curved-surface parallax occlusion mapping.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#ifndef PIXL_CSPOM_HLSLI
#define PIXL_CSPOM_HLSLI

// Curved Surface Mapping reuses Material Layers' b9 transport and height sources.
// It changes displacement only; downstream MaterialForge/Complex Material shading
// continues to consume the displaced UV through the normal PIXL lighting path.
namespace CSPOM
{
	bool CompileEligible()
	{
#if !defined(PIXL_CSPOM) || defined(SKINNED) || defined(MODELSPACENORMALS) || defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(HAIR) || defined(EYE) || defined(GRASS) || defined(SPARKLE) || defined(LODLANDSCAPE) || defined(LOD) || defined(DO_ALPHA_TEST)
		return false;
#elif defined(LANDSCAPE)
		return PIXL_CSPOM_Terrain != 0u;
#elif defined(TREE_ANIM)
		return PIXL_CSPOM_Trees != 0u;
#else
		// Skyrim does not expose a reliable rock/architecture semantic in every
		// Lighting permutation. Both controls therefore gate the eligible opaque
		// static-material family; exact texture-name classification is avoided.
		return PIXL_CSPOM_StaticOpaque != 0u;
#endif
	}

	bool Enabled()
	{
		return MaterialLayersTuning::IsValid() && PIXL_CSPOM_Enabled != 0u && CompileEligible();
	}

	float DistanceWeight(float distance)
	{
		float nearDistance = max(PIXL_CSPOM_FullQualityDistance, 1.0f);
		float farDistance = max(PIXL_CSPOM_MaxDistance, nearDistance + 1.0f);
		return 1.0f - smoothstep(nearDistance, farDistance, abs(distance));
	}

	float Weight(float distance, float viewZ, float mipLevel)
	{
		if (!Enabled())
			return 0.0f;
		float distanceWeight = DistanceWeight(distance);
		float mipWeight = 1.0f - smoothstep(4.0f, 7.0f, mipLevel);
		float grazingSafety = lerp(1.0f, smoothstep(0.025f, 0.18f, viewZ), saturate(PIXL_CSPOM_GrazingProtection));
		return saturate(distanceWeight * mipWeight * grazingSafety);
	}

	uint StepCount(float distance, float viewZ, float projectedShift, uint fallback)
	{
		uint minimumSteps = clamp(PIXL_CSPOM_MinSteps, 4u, 20u);
		uint maximumSteps = clamp(PIXL_CSPOM_MaxSteps, minimumSteps, 32u);
		float distanceQuality = DistanceWeight(distance);
		float angleQuality = smoothstep(0.12f, 0.88f, 1.0f - viewZ);
		float shiftQuality = saturate(projectedShift / max(PIXL_CSPOM_MaxTexelShift, 2.0f));
		float qualityScale = float(min(PIXL_CSPOM_Quality, 3u)) * (1.0f / 3.0f);
		float demand = saturate(max(angleQuality, shiftQuality) * distanceQuality * lerp(0.58f, 1.0f, qualityScale));
		return max(fallback, (uint)round(lerp((float)minimumSteps, (float)maximumSteps, demand)));
	}

	uint RefinementSteps(uint fallback)
	{
		return max(fallback, clamp(PIXL_CSPOM_BinarySteps, 2u, 6u));
	}

	float CurvatureMetric(float3x3 tangentToWorld)
	{
		if (PIXL_CSPOM_CurvedSurface == 0u || PIXL_CSPOM_Quality < 2u)
			return 0.0f;
		float3 n = normalize(tangentToWorld[2]);
		return min(length(ddx(n)) + length(ddy(n)), 0.30f);
	}

	float CurvatureBias(float curvature, float2 rayOffset, float layer)
	{
		float radial = dot(rayOffset, rayOffset);
		float centeredLayer = layer * (1.0f - layer);
		return curvature * radial * centeredLayer * clamp(PIXL_CSPOM_CurvatureStrength, 0.0f, 1.5f) * 6.0f;
	}

	float2 GuardSilhouette(float2 sourceUV, float2 candidateUV, float2 sampledDimensions, float weight)
	{
		if (PIXL_CSPOM_SilhouetteClipping == 0u || PIXL_CSPOM_Quality < 1u || weight <= 0.0f)
			return candidateUV;
		float2 texelDelta = (candidateUV - sourceUV) * max(sampledDimensions, 1.0f.xx);
		float limit = max(2.0f, PIXL_CSPOM_MaxTexelShift);
		float magnitude = max(abs(texelDelta.x), abs(texelDelta.y));
		float edgeConfidence = 1.0f - smoothstep(limit * 0.72f, limit, magnitude);
		edgeConfidence = lerp(1.0f, edgeConfidence, saturate(PIXL_CSPOM_SilhouetteStrength) * weight);
		return lerp(sourceUV, candidateUV, edgeConfidence);
	}

	float ShadowStrength(float legacyStrength)
	{
		if (!Enabled() || PIXL_CSPOM_SelfShadow == 0u)
			return legacyStrength;
		return min(legacyStrength, saturate(PIXL_CSPOM_ShadowStrength));
	}

	bool SelfShadowEnabled()
	{
		return Enabled() && PIXL_CSPOM_Quality >= 2u && PIXL_CSPOM_SelfShadow != 0u && PIXL_CSPOM_ShadowSteps != 0u;
	}

	float ShadowQuality(float legacyQuality)
	{
		if (!SelfShadowEnabled())
			return legacyQuality;
		// The established authored-height shadow path has four bounded taps. Map
		// the CSPOM budget onto that path instead of creating a second shadow loop.
		return max(legacyQuality, saturate((float)clamp(PIXL_CSPOM_ShadowSteps, 0u, 8u) * 0.125f));
	}

	float Occlusion(float hitHeight, float viewZ, float weight)
	{
		if (!Enabled() || PIXL_CSPOM_Quality < 1u || PIXL_CSPOM_SelfOcclusion == 0u)
			return 1.0f;
		float cavity = saturate((0.55f - hitHeight) * 2.0f);
		float angleGate = lerp(0.35f, 1.0f, saturate(1.0f - viewZ));
		return 1.0f - cavity * angleGate * saturate(PIXL_CSPOM_OcclusionStrength) * weight;
	}
}

#endif
