#ifndef PIXL_DIALOGUE_FOCUS_HLSLI
#define PIXL_DIALOGUE_FOCUS_HLSLI

#include "ActorSurfaceEffects/CharacterRuntime.hlsli"

// DialogueFocus owns the immutable prefix of the shared character PS b13 ABI.
// Actor Surface Effects may append data, while LANDSCAPE remains exclusively
// owned by Ground Response.
namespace DialogueFocus
{
	static const uint RuntimeMagic = 0x434F4644u;    // must match DialogueFocus::kMagic
	static const uint RuntimeVersion = 0x00010000u;  // must match DialogueFocus::kVersion

#if !defined(LANDSCAPE) && (defined(SKINNED) || defined(SKIN) || defined(EYE) || defined(HAIR))

	bool IsValid()
	{
		return
			PIXLCharacterRuntime::DialogueMagic == RuntimeMagic &&
			PIXLCharacterRuntime::DialogueVersion == RuntimeVersion;
	}

	float SphereFocus(float3 worldPosition, float4 centerRadius)
	{
		float radius = max(centerRadius.w, 1.0f);
		float distanceToCenter = length(worldPosition - centerRadius.xyz);

		// Full response near the facial/upper-body anchor, then a very soft falloff.
		return 1.0f - smoothstep(radius * 0.42f, radius, distanceToCenter);
	}

	float GetFocus(float3 worldPosition)
	{
		if (!IsValid() || PIXLCharacterRuntime::DialogueFocusBlend <= 0.0f)
			return 0.0f;

		float head = SphereFocus(worldPosition, PIXLCharacterRuntime::DialogueHeadPositionRadius);
		float shoulders = SphereFocus(worldPosition, PIXLCharacterRuntime::DialogueShoulderPositionRadius) * 0.72f;
		return saturate(PIXLCharacterRuntime::DialogueFocusBlend * max(head, shoulders));
	}

	float SkinQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueQuality.x) : 0.0f; }
	float EyeQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueQuality.y) : 0.0f; }
	float HairQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueQuality.z) : 0.0f; }
	float TissueQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueQuality.w) : 0.0f; }

	float ContactShadowQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueLighting.x) : 0.0f; }
	float LocalLightingQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueLighting.y) : 0.0f; }
	float MicroDetailQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueLighting.z) : 0.0f; }
	float EyeReflectionQuality() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueLighting.w) : 0.0f; }

	float Coldness() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueEnvironment.x) : 0.0f; }
	float Wetness() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueEnvironment.y) : 0.0f; }
	float BreathStrength() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueEnvironment.z) : 0.0f; }
	float SteamStrength() { return IsValid() ? saturate(PIXLCharacterRuntime::DialogueEnvironment.w) : 0.0f; }
#else
	// Compile-away fallback for all non-character permutations.
	float GetFocus(float3 worldPosition) { return 0.0f; }
	float SkinQuality() { return 0.0f; }
	float EyeQuality() { return 0.0f; }
	float HairQuality() { return 0.0f; }
	float TissueQuality() { return 0.0f; }
	float ContactShadowQuality() { return 0.0f; }
	float LocalLightingQuality() { return 0.0f; }
	float MicroDetailQuality() { return 0.0f; }
	float EyeReflectionQuality() { return 0.0f; }
	float Coldness() { return 0.0f; }
	float Wetness() { return 0.0f; }
	float BreathStrength() { return 0.0f; }
	float SteamStrength() { return 0.0f; }
#endif
}

#endif  // PIXL_DIALOGUE_FOCUS_HLSLI
