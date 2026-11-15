#include "Common/GBuffer.hlsli"
#include "HybridGI/common.hlsli"

Texture2D<float4> srcNormalRoughness : register(t0);

RWTexture2D<unorm float2> outNormal0 : register(u0);
RWTexture2D<unorm float2> outNormal1 : register(u1);
RWTexture2D<unorm float2> outNormal2 : register(u2);
RWTexture2D<unorm float2> outNormal3 : register(u3);
RWTexture2D<unorm float2> outNormal4 : register(u4);

float2 NormalMIPFilter(float2 enc0, float2 enc1, float2 enc2, float2 enc3)
{
	float3 avg = GBuffer::DecodeNormal(enc0) + GBuffer::DecodeNormal(enc1) + GBuffer::DecodeNormal(enc2) + GBuffer::DecodeNormal(enc3);

	// [Fix] Four near-opposing normals (common right on the silhouette of thin
	// geometry, e.g. foliage or a railing, once you're averaging 2x2 -> 4x4 ->
	// 8x8 texel neighborhoods several mips deep) can sum arbitrarily close to
	// the zero vector. normalize() on a near-zero vector is a NaN/Inf source;
	// that NaN then feeds every later cosine/ACos term in gi_cs's horizon
	// scan for every ray that samples this mip, which shows up as sporadic
	// black flecks in AO/GI that swim as the camera moves. Fall back to the
	// first (arbitrary but stable) input normal in that degenerate case
	// instead of dividing by near-zero.
	float lenSq = dot(avg, avg);
	float3 safeAvg = lenSq > 1e-10 ? avg * rsqrt(lenSq) : GBuffer::DecodeNormal(enc0);
	return GBuffer::EncodeNormal(safeAvg);
}

groupshared float2 g_scratchNormal[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RCP_OUT_FRAME_DIM;

	// Clamp each source receiver to the active *logical* viewport. The
	// allocations are full-backed even in Half/Quarter modes; hardware sampler
	// clamp alone would still read undefined pixels beyond the compact region.
	const uint2 maxPixel = max(uint2(OUT_FRAME_DIM), uint2(1, 1)) - 1u;
	const uint2 p00 = min(pixCoord, maxPixel);
	const uint2 p10 = min(pixCoord + uint2(1, 0), maxPixel);
	const uint2 p01 = min(pixCoord + uint2(0, 1), maxPixel);
	const uint2 p11 = min(pixCoord + uint2(1, 1), maxPixel);
	float2 normal0, normal1, normal2, normal3;
#if !defined(HALF_RES) && !defined(QUARTER_RES)
	if (all(pixCoord + uint2(1, 1) < uint2(OUT_FRAME_DIM))) {
		// Full-resolution interior retains the original Gather fast path.
		float4 nr0 = srcNormalRoughness.GatherRed(samplerPointClamp, FullFrameTextureUV(uv));
		float4 nr1 = srcNormalRoughness.GatherGreen(samplerPointClamp, FullFrameTextureUV(uv));
		normal0 = float2(nr0.w, nr1.w);
		normal1 = float2(nr0.z, nr1.z);
		normal2 = float2(nr0.x, nr1.x);
		normal3 = float2(nr0.y, nr1.y);
	} else
#endif
	{
		// Four independent receiver footprints in Half/Quarter; point-sample
		// the final texels in Full where Gather crosses the logical viewport.
		normal0 = srcNormalRoughness.SampleLevel(samplerPointClamp,
			FullFrameTextureUV((float2(p00) + 0.5f) * RCP_OUT_FRAME_DIM), 0).xy;
		normal1 = srcNormalRoughness.SampleLevel(samplerPointClamp,
			FullFrameTextureUV((float2(p10) + 0.5f) * RCP_OUT_FRAME_DIM), 0).xy;
		normal2 = srcNormalRoughness.SampleLevel(samplerPointClamp,
			FullFrameTextureUV((float2(p01) + 0.5f) * RCP_OUT_FRAME_DIM), 0).xy;
		normal3 = srcNormalRoughness.SampleLevel(samplerPointClamp,
			FullFrameTextureUV((float2(p11) + 0.5f) * RCP_OUT_FRAME_DIM), 0).xy;
	}
	if (all(pixCoord + uint2(0, 0) < uint2(OUT_FRAME_DIM))) outNormal0[pixCoord + uint2(0, 0)] = normal0;
	if (all(pixCoord + uint2(1, 0) < uint2(OUT_FRAME_DIM))) outNormal0[pixCoord + uint2(1, 0)] = normal1;
	if (all(pixCoord + uint2(0, 1) < uint2(OUT_FRAME_DIM))) outNormal0[pixCoord + uint2(0, 1)] = normal2;
	if (all(pixCoord + uint2(1, 1) < uint2(OUT_FRAME_DIM))) outNormal0[pixCoord + uint2(1, 1)] = normal3;

	// MIP 1
	float2 nm1 = NormalMIPFilter(normal0, normal1, normal2, normal3);
	if (all(baseCoord < ((uint2(OUT_FRAME_DIM) + 1u) >> 1))) outNormal1[baseCoord] = nm1;
	g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 1];
		float2 inBR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 1];

		float2 nm2 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 2 < ((uint2(OUT_FRAME_DIM) + 3u) >> 2))) outNormal2[baseCoord / 2] = nm2;
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 2];
		float2 inBR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 2];

		float2 nm3 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 4 < ((uint2(OUT_FRAME_DIM) + 7u) >> 3))) outNormal3[baseCoord / 4] = nm3;
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float2 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 0];
		float2 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 4];
		float2 inBR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 4];

		float2 nm4 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 8 < ((uint2(OUT_FRAME_DIM) + 15u) >> 4))) outNormal4[baseCoord / 8] = nm4;
	}
}
