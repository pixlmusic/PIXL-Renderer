#ifndef PIXL_ACTOR_SURFACE_EFFECTS_HLSLI
#define PIXL_ACTOR_SURFACE_EFFECTS_HLSLI

#include "ActorSurfaceEffects/CharacterRuntime.hlsli"
#include "Common/PIXLAdvancedSnowMaterial.hlsli"

// Actor-anchored analytical contamination. Events are generated from accepted
// Ground Response body contacts, then transformed into actor local space on the
// CPU. No screen-space history or per-actor texture is involved, which keeps the
// result stable under DLSS, camera changes, equipment swaps and perspective swaps.
namespace ActorSurfaceEffects
{
	static const uint RuntimeMagic = 0x46555341u;
	static const uint RuntimeVersion = 0x00010000u;

	struct SurfaceSample
	{
		float SnowFresh;
		float SnowMelting;
		float MudWet;
		float MudDry;
		float Wetness;
		float Combined;
		float3 LocalPosition;
		float3 NormalDetail;
	};

	SurfaceSample EmptySample()
	{
		SurfaceSample result;
		result.SnowFresh = 0.0f;
		result.SnowMelting = 0.0f;
		result.MudWet = 0.0f;
		result.MudDry = 0.0f;
		result.Wetness = 0.0f;
		result.Combined = 0.0f;
		result.LocalPosition = 0.0f.xxx;
		result.NormalDetail = 0.0f.xxx;
		return result;
	}

#if defined(ACTOR_SURFACE_EFFECTS) && !defined(LANDSCAPE) && (defined(SKINNED) || defined(SKIN) || defined(EYE) || defined(HAIR))
	bool IsValid()
	{
		return
			PIXLCharacterRuntime::ActorSurfaceMagic == RuntimeMagic &&
			PIXLCharacterRuntime::ActorSurfaceVersion == RuntimeVersion &&
			PIXLCharacterRuntime::ActorSurfaceEventCount > 0u;
	}

	float3 ToActorLocal(float3 worldPosition)
	{
		float3 relative = worldPosition - PIXLCharacterRuntime::ActorOriginScale.xyz;
		float cosine = PIXLCharacterRuntime::ActorRotationHeight.x;
		float sine = PIXLCharacterRuntime::ActorRotationHeight.y;
		return float3(
			cosine * relative.x + sine * relative.y,
			-sine * relative.x + cosine * relative.y,
			relative.z);
	}

	float StableBreakup(float3 localPosition, float seed)
	{
		// Low-frequency, actor-local analytic breakup avoids texture repetition and
		// remains stable as the camera moves. Two incommensurate waves suppress the
		// obvious horizontal cutoff without introducing reconstruction shimmer.
		float waveA = sin(dot(localPosition, float3(0.117f, 0.073f, 0.091f)) + seed * 17.13f);
		float waveB = sin(dot(localPosition, float3(-0.061f, 0.139f, 0.047f)) - seed * 29.71f);
		return (waveA + waveB) * 0.25f;
	}

	float UnionCoverage(float current, float addition)
	{
		return saturate(current + addition * (1.0f - current));
	}

	SurfaceSample EvaluateLocal(float3 localPosition)
	{
		SurfaceSample result;
		result.SnowFresh = 0.0f;
		result.SnowMelting = 0.0f;
		result.MudWet = 0.0f;
		result.MudDry = 0.0f;
		result.Wetness = 0.0f;
		result.Combined = 0.0f;
		result.LocalPosition = 0.0f.xxx;
		result.NormalDetail = 0.0f.xxx;
		[branch] if (IsValid()) {
		result.LocalPosition = localPosition;
		float softness = clamp(PIXLCharacterRuntime::ActorSurfaceTuning.y, 0.04f, 0.55f);
		float breakupStrength = clamp(PIXLCharacterRuntime::ActorSurfaceTuning.z, 0.0f, 0.40f);
		float snowEnabled = (PIXLCharacterRuntime::ActorSurfaceFlags & 1u) != 0u ? 1.0f : 0.0f;
		float mudEnabled = (PIXLCharacterRuntime::ActorSurfaceFlags & 2u) != 0u ? 1.0f : 0.0f;
		uint eventCount = min(
			PIXLCharacterRuntime::ActorSurfaceEventCount,
			PIXLCharacterRuntime::MaximumSurfaceEvents);

		[loop]
		for (uint index = 0u; index < eventCount; ++index) {
			PIXLCharacterRuntime::SurfaceEvent eventData =
				PIXLCharacterRuntime::ActorSurfaceEvents[index];
			float3 delta = result.LocalPosition - eventData.LocalCenterRadius.xyz;
			float horizontalRadius = max(eventData.LocalCenterRadius.w, 1.0f);
			float verticalRadius = max(eventData.VerticalAmounts.x, 1.0f);
			float2 normalizedXY = delta.xy / horizontalRadius;
			float normalizedZ = abs(delta.z) / verticalRadius;
			float ellipsoidSquared = dot(normalizedXY, normalizedXY) + normalizedZ * normalizedZ;
			float breakup = StableBreakup(result.LocalPosition, eventData.State.z) * breakupStrength;
			float inner = max(1.0f - softness, 0.05f);
			float outer = 1.0f + softness;
			float coverage = 1.0f - smoothstep(inner * inner, outer * outer, ellipsoidSquared + breakup);

			// Moving mud/snow produces a small, irregular upward splash without
			// turning the primary contact into a uniform vertical band.
			float splash = saturate(eventData.State.w);
			float splashHeight = max(verticalRadius * (1.5f + splash * 3.0f), 2.0f);
			float splashCoverage = 0.0f;
			[branch] if (splash > 0.001f) {
				float splashShape =
					length(normalizedXY * 1.75f) +
					abs(delta.z - splashHeight * 0.35f) / splashHeight;
				splashCoverage =
					(1.0f - smoothstep(0.55f, 1.15f, splashShape + breakup * 1.4f)) * splash;
			}
			coverage = max(coverage, splashCoverage * 0.58f);

			result.SnowFresh = UnionCoverage(result.SnowFresh, coverage * eventData.VerticalAmounts.y * snowEnabled);
			result.SnowMelting = UnionCoverage(result.SnowMelting, coverage * eventData.VerticalAmounts.z * snowEnabled);
			result.MudWet = UnionCoverage(result.MudWet, coverage * eventData.VerticalAmounts.w * mudEnabled);
			result.MudDry = UnionCoverage(result.MudDry, coverage * eventData.State.x * mudEnabled);
			result.Wetness = UnionCoverage(result.Wetness, coverage * eventData.State.y);

			float3 micro = float3(
				sin(result.LocalPosition.y * 0.19f + eventData.State.z * 11.0f),
				sin(result.LocalPosition.z * 0.17f - eventData.State.z * 13.0f),
				sin(result.LocalPosition.x * 0.21f + eventData.State.z * 7.0f));
			result.NormalDetail += micro * coverage *
				(eventData.VerticalAmounts.y * snowEnabled * 0.035f +
				 eventData.VerticalAmounts.w * mudEnabled * 0.065f +
				 eventData.State.x * mudEnabled * 0.045f);
		}

		result.Combined = saturate(max(
			max(result.SnowFresh, result.SnowMelting),
			max(max(result.MudWet, result.MudDry), result.Wetness)));
		}
		return result;
	}

	SurfaceSample Evaluate(float3 worldPosition)
	{
		return EvaluateLocal(ToActorLocal(worldPosition));
	}

	SurfaceSample EvaluateSkinned(float3 modelPosition)
	{
		// Lighting's ModelPosition is the original bind/model-space vertex position,
		// before the current bone palette is applied. It therefore remains attached
		// to the material while walking, running, posing and jumping. Convert it to
		// actor-local world units so it matches CPU contact lobes and actor scale.
		float actorScale = clamp(PIXLCharacterRuntime::ActorOriginScale.w, 0.1f, 10.0f);
		return EvaluateLocal(modelPosition * actorScale);
	}

	float3 ApplyNormal(float3 worldNormal, SurfaceSample sample)
	{
		if (sample.Combined <= 0.001f)
			return worldNormal;
		float cosine = PIXLCharacterRuntime::ActorRotationHeight.x;
		float sine = PIXLCharacterRuntime::ActorRotationHeight.y;
		float3 localDetail = sample.NormalDetail;
		float3 worldDetail = float3(
			cosine * localDetail.x - sine * localDetail.y,
			sine * localDetail.x + cosine * localDetail.y,
			localDetail.z);
		return normalize(worldNormal + worldDetail);
	}

	void ApplyMaterial(
		inout float3 baseColor,
		inout float roughness,
		inout float3 f0,
		inout float metallic,
		SurfaceSample sample,
		float skinResponse,
		float hairResponse,
		float eyeResponse)
	{
		if (sample.Combined <= 0.001f)
			return;

		float materialAcceptance = lerp(1.0f, 0.76f, saturate(skinResponse));
		materialAcceptance *= lerp(1.0f, 0.88f, saturate(hairResponse));
		materialAcceptance *= lerp(1.0f, 0.42f, saturate(eyeResponse));
		float snowFresh = saturate(sample.SnowFresh * materialAcceptance);
		float snowMelting = saturate(sample.SnowMelting * materialAcceptance);
		float mudWet = saturate(sample.MudWet * materialAcceptance);
		float mudDry = saturate(sample.MudDry * materialAcceptance);
		float wetness = saturate(sample.Wetness * materialAcceptance);
		// Warm exposed skin retains less crystalline snow and reads as meltwater
		// sooner, while preserving the underlying Skin Optics material response.
		float warmSkinMelt = saturate(skinResponse) * snowFresh * 0.28f;
		snowFresh -= warmSkinMelt;
		snowMelting = UnionCoverage(snowMelting, warmSkinMelt * 0.82f);
		wetness = UnionCoverage(wetness, warmSkinMelt * 0.62f);

		float luminance = dot(baseColor, float3(0.2126f, 0.7152f, 0.0722f));
		float3 snowTint = max(
			PIXLCharacterRuntime::ActorSnowAppearance.rgb,
			PIXLAdvancedSnowMaterial::Tint() * 0.50f);
		float snowMaterialStrength = saturate(PIXLCharacterRuntime::ActorSnowAppearance.w);
		float3 detailedSnow = snowTint * lerp(0.48f, 1.05f, saturate(luminance * 1.8f));
		detailedSnow = lerp(baseColor * 0.62f, detailedSnow, snowMaterialStrength);
		float snowCoverage = UnionCoverage(snowFresh, snowMelting * 0.88f);
		baseColor = lerp(baseColor, detailedSnow, snowCoverage * 0.90f);
		roughness = lerp(
			roughness,
			PIXLAdvancedSnowMaterial::ThinAccumulationRoughness(snowFresh, snowMelting),
			snowCoverage * 0.88f);
		f0 = lerp(
			f0,
			PIXLAdvancedSnowMaterial::DielectricF0(snowFresh),
			snowCoverage * 0.72f);
		metallic = lerp(metallic, 0.0f, snowCoverage * 0.94f);

		float3 wetMudColor = float3(0.050f, 0.028f, 0.014f);
		float3 dryMudColor = float3(0.155f, 0.083f, 0.036f);
		baseColor = lerp(baseColor, lerp(baseColor * 0.28f, wetMudColor, 0.72f), mudWet * 0.90f);
		baseColor = lerp(baseColor, lerp(baseColor * 0.58f, dryMudColor, 0.62f), mudDry * 0.78f);
		roughness = lerp(roughness, 0.13f, mudWet * 0.88f);
		roughness = lerp(roughness, 0.82f, mudDry * 0.84f);
		f0 = lerp(f0, 0.046f.xxx, mudWet * 0.72f);
		metallic = lerp(metallic, 0.0f, max(mudWet, mudDry) * 0.92f);

		// Meltwater/rain is a thin dielectric film: darken the substrate and
		// smooth its microsurface rather than multiplying specular like metal.
		baseColor *= lerp(1.0f, 0.77f, wetness * 0.74f);
		roughness = lerp(roughness, 0.085f, wetness * 0.76f);
		f0 = lerp(f0, 0.0205f.xxx, wetness * 0.64f);
		metallic = lerp(metallic, 0.0f, wetness * 0.70f);

		uint debugMode = (uint)round(PIXLCharacterRuntime::ActorSurfaceTuning.w);
		if (debugMode != 0u) {
			float3 debugColor = 0.0f.xxx;
			if (debugMode == 1u)
				debugColor = sample.Combined.xxx;
			else if (debugMode == 2u)
				debugColor = float3(0.35f, 0.70f, 1.0f) * max(sample.SnowFresh, sample.SnowMelting);
			else if (debugMode == 3u)
				debugColor = float3(0.55f, 0.18f, 0.035f) * max(sample.MudWet, sample.MudDry);
			else if (debugMode == 4u)
				debugColor = float3(0.05f, 0.45f, 1.0f) * sample.Wetness;
			else if (debugMode == 5u)
				debugColor = float3(sample.SnowFresh, sample.MudWet, sample.Wetness);
			else
				debugColor = frac(abs(sample.LocalPosition) / max(PIXLCharacterRuntime::ActorRotationHeight.z, 1.0f));
			baseColor = debugColor;
			roughness = 1.0f;
			metallic = 0.0f;
		}
	}
#else
	SurfaceSample Evaluate(float3 worldPosition) { return EmptySample(); }
	SurfaceSample EvaluateSkinned(float3 modelPosition) { return EmptySample(); }
	float3 ApplyNormal(float3 worldNormal, SurfaceSample sample) { return worldNormal; }
	void ApplyMaterial(
		inout float3 baseColor,
		inout float roughness,
		inout float3 f0,
		inout float metallic,
		SurfaceSample sample,
		float skinResponse,
		float hairResponse,
		float eyeResponse) {}
#endif
}

#endif  // PIXL_ACTOR_SURFACE_EFFECTS_HLSLI
