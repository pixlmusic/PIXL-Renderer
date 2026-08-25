#ifndef PIXL_DIALOGUE_FOCUS_HLSLI
#define PIXL_DIALOGUE_FOCUS_HLSLI

// DialogueFocus intentionally shares PS b13 with no character feature.
// GroundResponse owns b13 on LANDSCAPE permutations; this cbuffer is therefore
// declared only for SKIN/EYE/HAIR permutations. The CPU side likewise binds b13
// only for actor-owned geometry.
namespace DialogueFocus
{
	static const uint RuntimeMagic = 0x434F4644u;    // must match DialogueFocus::kMagic
	static const uint RuntimeVersion = 0x00010000u;  // must match DialogueFocus::kVersion

#if !defined(LANDSCAPE) && (defined(SKIN) || defined(EYE) || defined(HAIR))
	cbuffer Runtime : register(b13)
	{
		uint Magic;
		uint Version;
		float FocusBlend;
		float SessionActive;

		float4 HeadPositionRadius;
		float4 ShoulderPositionRadius;

		float4 Quality;      // skin, eye, hair, tissue
		float4 Lighting;     // contact shadow, local light, microdetail, eye reflection
		float4 Environment;  // coldness, wetness, breath, body steam
	};

	bool IsValid()
	{
		return Magic == RuntimeMagic && Version == RuntimeVersion;
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
		if (!IsValid() || FocusBlend <= 0.0f)
			return 0.0f;

		float head = SphereFocus(worldPosition, HeadPositionRadius);
		float shoulders = SphereFocus(worldPosition, ShoulderPositionRadius) * 0.72f;
		return saturate(FocusBlend * max(head, shoulders));
	}

	float SkinQuality() { return IsValid() ? saturate(Quality.x) : 0.0f; }
	float EyeQuality() { return IsValid() ? saturate(Quality.y) : 0.0f; }
	float HairQuality() { return IsValid() ? saturate(Quality.z) : 0.0f; }
	float TissueQuality() { return IsValid() ? saturate(Quality.w) : 0.0f; }

	float ContactShadowQuality() { return IsValid() ? saturate(Lighting.x) : 0.0f; }
	float LocalLightingQuality() { return IsValid() ? saturate(Lighting.y) : 0.0f; }
	float MicroDetailQuality() { return IsValid() ? saturate(Lighting.z) : 0.0f; }
	float EyeReflectionQuality() { return IsValid() ? saturate(Lighting.w) : 0.0f; }

	float Coldness() { return IsValid() ? saturate(Environment.x) : 0.0f; }
	float Wetness() { return IsValid() ? saturate(Environment.y) : 0.0f; }
	float BreathStrength() { return IsValid() ? saturate(Environment.z) : 0.0f; }
	float SteamStrength() { return IsValid() ? saturate(Environment.w) : 0.0f; }
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
