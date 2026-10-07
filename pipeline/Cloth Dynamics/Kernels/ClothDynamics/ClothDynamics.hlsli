#ifndef PIXL_CLOTHING_DAMAGE_HLSLI
#define PIXL_CLOTHING_DAMAGE_HLSLI

#include "ActorSurfaceEffects/CharacterRuntime.hlsli"

// Procedural health-driven surface wear. The marks modify albedo/roughness only;
// authored normal maps and the silhouette/depth path remain untouched. Tear marks
// are shaded cuts, not geometric holes, because Skyrim's opaque material path has
// no safe per-actor decal or cutout mask available here.
namespace ClothingDamage
{
#if defined(CLOTHING_DAMAGE) && defined(SKINNED) && !defined(SKIN) && !defined(EYE) && !defined(HAIR) && !defined(TREE_ANIM)
	float Hash(float2 p)
	{
		p = frac(p * float2(0.1031f, 0.1030f));
		p += dot(p, p.yx + 33.33f);
		return frac((p.x + p.y) * p.x);
	}

	float SegmentMask(float2 p, float2 a, float2 b, float width)
	{
		const float2 ab = b - a;
		const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1.0e-4f));
		const float distanceToSegment = length(p - (a + t * ab));
		return 1.0f - smoothstep(width, width * 2.2f, distanceToSegment);
	}

	float ScratchPattern(float2 uv, float seed)
	{
		const float2 grid = uv * float2(38.0f, 46.0f) + seed * float2(7.3f, 11.9f);
		const float2 cell = floor(grid);
		const float2 local = frac(grid);
		const float h = Hash(cell + seed * 19.7f);
		const float h2 = Hash(cell.yx + seed * 41.3f);
		const float2 start = float2(0.12f + 0.72f * h, 0.10f + 0.74f * h2);
		const float2 end = start + (h > 0.5f ? float2(0.12f, 0.68f) : float2(0.70f, -0.14f));
		return (h2 > 0.28f ? 1.0f : 0.0f) * SegmentMask(local, start, end, 0.010f + 0.007f * h);
	}

	float TearPattern(float2 uv, float seed)
	{
		const float2 grid = uv * float2(17.0f, 23.0f) + seed * float2(3.1f, 5.7f);
		const float2 cell = floor(grid);
		const float2 local = frac(grid);
		const float h = Hash(cell + seed * 13.1f);
		const float h2 = Hash(cell.yx + seed * 29.3f);
		const float2 start = float2(0.18f + 0.54f * h, 0.12f + 0.74f * h2);
		const float2 end = start + (h > 0.5f ? float2(-0.08f, 0.76f) : float2(0.76f, 0.10f));
		return (h2 > 0.52f ? 1.0f : 0.0f) * SegmentMask(local, start, end, 0.025f + 0.015f * h);
	}

	bool IsValid()
	{
		return PIXLCharacterRuntime::DamageParameters.z >= 0.0f &&
			PIXLCharacterRuntime::DamageState.w > 0.5f &&
			PIXLCharacterRuntime::DamageState.x > 0.01f;
	}

	void Apply(inout float3 baseColor, inout float roughness, float metallic, float2 uv)
	{
		if (!IsValid())
			return;

		const float severity = smoothstep(0.06f, 0.82f, saturate(PIXLCharacterRuntime::DamageState.x));
		const float seed = PIXLCharacterRuntime::DamageParameters.z;
		const float fabricWeight = 1.0f - smoothstep(0.18f, 0.62f, saturate(metallic));
		const float armorWeight = 1.0f - fabricWeight;
		const float tear = TearPattern(uv, seed);
		const float scratch = ScratchPattern(uv, seed + 0.37f);
		const float fabricAmount = severity * saturate(PIXLCharacterRuntime::DamageParameters.x) * fabricWeight;
		const float armorAmount = severity * saturate(PIXLCharacterRuntime::DamageParameters.y) * armorWeight;
		const uint debugMode = (uint)round(PIXLCharacterRuntime::DamageParameters.w);

		if (debugMode != 0u)
		{
			float3 debugColor = lerp(float3(0.04f, 0.04f, 0.04f), float3(1.0f, 0.12f, 0.02f), severity);
			if (debugMode == 2u)
				debugColor = lerp(float3(0.02f, 0.02f, 0.02f), float3(0.12f, 0.78f, 1.0f), scratch);
			else if (debugMode == 3u)
				debugColor = lerp(float3(0.02f, 0.02f, 0.02f), float3(1.0f, 0.55f, 0.08f), tear);
			baseColor = debugColor;
			roughness = 1.0f;
			return;
		}

		// Fabric tears read as narrow dark breaks with a restrained frayed edge.
		const float tearCore = saturate(tear * fabricAmount * 0.62f);
		const float tearEdge = saturate((smoothstep(0.010f, 0.055f, tear) - tear) * fabricAmount * 0.32f);
		baseColor = lerp(baseColor, baseColor * 0.16f, tearCore);
		baseColor = lerp(baseColor, min(baseColor * 1.20f + 0.018f, 1.0f.xxx), tearEdge);

		// Armor cracks are darker than the fine scratch pass. Metal scratches expose
		// a brighter, rougher surface; dielectric armor and leather retain a darker
		// abrasion so the material response stays plausible.
		const float armorCrack = saturate(tear * armorAmount * 0.34f);
		baseColor = lerp(baseColor, baseColor * 0.30f, armorCrack);
		const float scratchAmount = saturate(scratch * armorAmount * 0.58f);
		const float3 scratchedColor = lerp(baseColor * 0.62f, min(baseColor * 1.18f + 0.025f, 1.0f.xxx), saturate(metallic));
		baseColor = lerp(baseColor, scratchedColor, scratchAmount);
		roughness = saturate(roughness + scratchAmount * lerp(0.18f, 0.30f, saturate(metallic)));
	}
#else
	void Apply(inout float3 baseColor, inout float roughness, float metallic, float2 uv) {}
#endif
}

#endif
