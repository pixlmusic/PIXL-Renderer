#ifndef PIXL_CHARACTER_RUNTIME_HLSLI
#define PIXL_CHARACTER_RUNTIME_HLSLI

// Shared actor-only PS b13 ABI. Ground Response owns b13 on LANDSCAPE, so this
// declaration is compile-time excluded there. The first six registers are the
// immutable DialogueFocus v1 payload; Actor Surface Effects appends data without
// invalidating an older DialogueFocus-only constant buffer.
namespace PIXLCharacterRuntime
{
	static const uint MaximumSurfaceEvents = 12u;

	struct SurfaceEvent
	{
		float4 LocalCenterRadius;
		float4 VerticalAmounts;
		float4 State;
	};

#if !defined(LANDSCAPE) && (defined(SKINNED) || defined(SKIN) || defined(EYE) || defined(HAIR))
	cbuffer Runtime : register(b13)
	{
		uint DialogueMagic;
		uint DialogueVersion;
		float DialogueFocusBlend;
		float DialogueSessionActive;

		float4 DialogueHeadPositionRadius;
		float4 DialogueShoulderPositionRadius;
		float4 DialogueQuality;
		float4 DialogueLighting;
		float4 DialogueEnvironment;

		uint ActorSurfaceMagic;
		uint ActorSurfaceVersion;
		uint ActorSurfaceEventCount;
		uint ActorSurfaceFlags;

		float4 ActorOriginScale;
		float4 ActorRotationHeight;
		float4 ActorSurfaceTuning;
		float4 ActorSnowAppearance;
		SurfaceEvent ActorSurfaceEvents[MaximumSurfaceEvents];
	};
#endif
}

#endif  // PIXL_CHARACTER_RUNTIME_HLSLI
