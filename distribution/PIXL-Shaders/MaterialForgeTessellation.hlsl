// Narrow authored-height displacement path for compatible opaque, non-skinned
// MaterialForge PBR Lighting draws. It is armed only for a dedicated height map;
// Complex Material uses its existing pixel-space POM path because environment
// mask alpha is not an unambiguous geometry-height signal.

struct MaterialForgeVertex
{
	float4 Position : SV_POSITION0;
	float2 TexCoord0 : TEXCOORD0;
	float3 TBN0 : TEXCOORD1;
	float3 TBN1 : TEXCOORD2;
	float3 TBN2 : TEXCOORD3;
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float4 Color : COLOR0;
	float4 FogParam : COLOR1;
	float3 ModelPosition : TEXCOORD12;
};

cbuffer MaterialForgeTessellationSettings : register(b13)
{
	float DisplacementScale : packoffset(c0.x);
	float MaximumTessellationFactor : packoffset(c0.y);
	float TessellationFadeStart : packoffset(c0.z);
	float TessellationFadeEnd : packoffset(c0.w);
	uint HeightChannel : packoffset(c1.x);
	uint RequireAuthoredRange : packoffset(c1.y);
};

Texture2D<float4> MaterialForgeHeightMap : register(t4);
SamplerState MaterialForgeHeightSampler : register(s4);

struct MaterialForgePatchConstants
{
	float Edge[3] : SV_TessFactor;
	float Inside : SV_InsideTessFactor;
	float AuthoredRange : TEXCOORD0;
	float ReferenceHeight : TEXCOORD1;
};

#ifdef HULLSHADER
float MaterialForgeDistanceFade(float clipW)
{
	const float fadeStart = max(TessellationFadeStart, 128.0f);
	const float fadeEnd = max(TessellationFadeEnd, fadeStart + 32.0f);
	return 1.0f - smoothstep(fadeStart, fadeEnd, abs(clipW));
}

float MaterialForgeEdgeFactor(float4 a, float4 b)
{
	if (a.w <= 1.0e-5f || b.w <= 1.0e-5f)
		return 1.0f;
	const float2 ndcA = a.xy / a.w;
	const float2 ndcB = b.xy / b.w;
	const float projectedLength = length(ndcA - ndcB);
	const float targetSubdivisions = projectedLength * 12.0f;
	const float nearFactor = clamp(targetSubdivisions, 1.0f, clamp(MaximumTessellationFactor, 1.0f, 8.0f));
	const float distanceFade = MaterialForgeDistanceFade(0.5f * (a.w + b.w));
	return lerp(1.0f, nearFactor, distanceFade);
}

MaterialForgePatchConstants MaterialForgePatchConstantFunction(InputPatch<MaterialForgeVertex, 3> patch)
{
	MaterialForgePatchConstants output;
	const float2 patchUV = (patch[0].TexCoord0 + patch[1].TexCoord0 + patch[2].TexCoord0) * (1.0f / 3.0f);
	const float coarseHeight = MaterialForgeHeightMap.SampleLevel(
		MaterialForgeHeightSampler, patchUV, 15.0f)[min(HeightChannel, 3u)];
	const bool hasAuthoredRange = RequireAuthoredRange == 0u ||
		(coarseHeight > (4.0f / 255.0f) && coarseHeight < (1.0f - 4.0f / 255.0f));
	output.AuthoredRange = hasAuthoredRange ? 1.0f : 0.0f;
	// Complex Material encodes height around a texture-specific baseline rather
	// than a universal 0.5. Its final mip supplies a stable material-wide mean,
	// shared by every patch and therefore free of cracks at triangle boundaries.
	// Dedicated PBR displacement retains the conventional mid-gray baseline.
	output.ReferenceHeight = RequireAuthoredRange != 0u ? coarseHeight : 0.5f;
	if (!hasAuthoredRange) {
		output.Edge[0] = output.Edge[1] = output.Edge[2] = output.Inside = 1.0f;
		return output;
	}
	// D3D edge factors are opposite their same-numbered control point.
	output.Edge[0] = MaterialForgeEdgeFactor(patch[1].Position, patch[2].Position);
	output.Edge[1] = MaterialForgeEdgeFactor(patch[2].Position, patch[0].Position);
	output.Edge[2] = MaterialForgeEdgeFactor(patch[0].Position, patch[1].Position);
	output.Inside = (output.Edge[0] + output.Edge[1] + output.Edge[2]) * (1.0f / 3.0f);
	return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("MaterialForgePatchConstantFunction")]
[maxtessfactor(8.0f)]
MaterialForgeVertex MaterialForgeHull(
	InputPatch<MaterialForgeVertex, 3> patch,
	uint controlPointId : SV_OutputControlPointID,
	uint patchId : SV_PrimitiveID)
{
	return patch[controlPointId];
}
#endif

#ifdef DOMAINSHADER
cbuffer MaterialForgePerFrame : register(b12)
{
	row_major float4x4 ViewProj : packoffset(c8);
};

MaterialForgeVertex MaterialForgeInterpolate(
	InputPatch<MaterialForgeVertex, 3> patch,
	float3 barycentric)
{
	MaterialForgeVertex output;
	output.Position = patch[0].Position * barycentric.x + patch[1].Position * barycentric.y + patch[2].Position * barycentric.z;
	output.TexCoord0 = patch[0].TexCoord0 * barycentric.x + patch[1].TexCoord0 * barycentric.y + patch[2].TexCoord0 * barycentric.z;
	output.TBN0 = patch[0].TBN0 * barycentric.x + patch[1].TBN0 * barycentric.y + patch[2].TBN0 * barycentric.z;
	output.TBN1 = patch[0].TBN1 * barycentric.x + patch[1].TBN1 * barycentric.y + patch[2].TBN1 * barycentric.z;
	output.TBN2 = patch[0].TBN2 * barycentric.x + patch[1].TBN2 * barycentric.y + patch[2].TBN2 * barycentric.z;
	output.WorldPosition = patch[0].WorldPosition * barycentric.x + patch[1].WorldPosition * barycentric.y + patch[2].WorldPosition * barycentric.z;
	output.PreviousWorldPosition = patch[0].PreviousWorldPosition * barycentric.x + patch[1].PreviousWorldPosition * barycentric.y + patch[2].PreviousWorldPosition * barycentric.z;
	output.Color = patch[0].Color * barycentric.x + patch[1].Color * barycentric.y + patch[2].Color * barycentric.z;
	output.FogParam = patch[0].FogParam * barycentric.x + patch[1].FogParam * barycentric.y + patch[2].FogParam * barycentric.z;
	output.ModelPosition = patch[0].ModelPosition * barycentric.x + patch[1].ModelPosition * barycentric.y + patch[2].ModelPosition * barycentric.z;
	return output;
}

[domain("tri")]
MaterialForgeVertex MaterialForgeDomain(
	MaterialForgePatchConstants patchConstants,
	float3 barycentric : SV_DomainLocation,
	const OutputPatch<MaterialForgeVertex, 3> patch)
{
	MaterialForgeVertex output = MaterialForgeInterpolate(patch, barycentric);
	const float3 interpolatedNormal = float3(output.TBN0.z, output.TBN1.z, output.TBN2.z);
	const float normalLengthSquared = dot(interpolatedNormal, interpolatedNormal);
	const float3 worldNormal = normalLengthSquared > 1.0e-8f
		? interpolatedNormal * rsqrt(normalLengthSquared)
		: float3(0.0f, 0.0f, 1.0f);

	// The same offset is applied to current and previous positions so existing
	// motion-vector reconstruction sees the same authored surface in both frames.
	const float patchFactor = max(1.0f, max(patchConstants.Edge[0], max(patchConstants.Edge[1], patchConstants.Edge[2])));
	const float heightMip = clamp(log2(max(1.0f, 8.0f / patchFactor)), 0.0f, 3.0f);
	const uint heightChannel = min(HeightChannel, 3u);
	const float sampledHeight = MaterialForgeHeightMap.SampleLevel(MaterialForgeHeightSampler, output.TexCoord0, heightMip)[heightChannel];
	const float height = patchConstants.AuthoredRange > 0.5f ? sampledHeight : 0.5f;
	const float fadeStart = max(TessellationFadeStart, 128.0f);
	const float fadeEnd = max(TessellationFadeEnd, fadeStart + 32.0f);
	const float distanceFade = 1.0f - smoothstep(fadeStart, fadeEnd, abs(output.Position.w));
	const float referenceHeight = saturate(patchConstants.ReferenceHeight);
	const float offset = (saturate(height) - referenceHeight) * 2.0f * clamp(DisplacementScale, 0.0f, 16.0f) * distanceFade;
	output.WorldPosition.xyz += worldNormal * offset;
	output.PreviousWorldPosition.xyz += worldNormal * offset;
	output.Position = mul(ViewProj, float4(output.WorldPosition.xyz, 1.0f));
	return output;
}
#endif
