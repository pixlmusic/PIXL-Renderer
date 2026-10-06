///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation
//
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion",
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "HybridGI/common.hlsli"

Texture2D<float> srcNDCDepth : register(t0);

RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> outDepth1 : register(u1);
RWTexture2D<float> outDepth2 : register(u2);
RWTexture2D<float> outDepth3 : register(u3);
RWTexture2D<float> outDepth4 : register(u4);

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI),
// is required to be non-linear (i.e. very large outdoors environments).
float ClampDepth(float depth)
{
	depth = ScreenToViewDepth(depth);
	// [Fix] Guard against NaN/Inf sneaking in from degenerate depth-buffer
	// texels (e.g. depth == 0 at the far clip, or uninitialized skybox texels
	// depending on how the depth buffer is cleared). A single NaN here
	// propagates through every downstream mip via the min/max/average
	// filters below, and then into gi_cs's SampleLevel picks for that whole
	// mip - producing a visible black or "punched-through" blotch that can
	// persist for many pixels around the offending texel.
	depth = filterNaN(filterInf(depth));
	return clamp(depth, 0.0, 3.402823466e+38);
}

float DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
#ifdef LINEAR_FILTER
	float minDepth = min(min(depth0, depth1), min(depth2, depth3));
	float maxDepth = max(max(depth0, depth1), max(depth2, depth3));
	float averageDepth = (depth0 + depth1 + depth2 + depth3) * 0.25;
	// Averaging foreground and background depth manufactures a surface in empty
	// space. Hierarchical GI rays then collect radiance from that phantom plane,
	// especially around doorways and first-person camera transitions. Preserve
	// smooth planar depth, but choose the conservative real surface whenever a
	// 2x2 footprint crosses a discontinuity.
	float discontinuityThreshold = max(minDepth * 0.018f, 6.0f);
	return (maxDepth - minDepth) > discontinuityThreshold ? minDepth : averageDepth;
#elif defined(MAX_FILTER)
	return max(max(depth0, depth1), max(depth2, depth3));
#elif defined(MIN_FILTER)
	return min(min(depth0, depth1), min(depth2, depth3));
#else
	// Keep standalone validation and any future host permutation defined. The
	// production pass selects LINEAR_FILTER, so this is a safe equivalent
	// fallback rather than an undefined return value.
	return (depth0 + depth1 + depth2 + depth3) * 0.25;
#endif
}

groupshared float g_scratchDepths[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

#ifdef MIN_FILTER
	// Exact pixel ownership for Hi-Z. Padded group lanes must participate in
	// every barrier, but contribute far depth instead of clamped edge texels.
	float4 depths4 = 0.0f;
	float depth0 = 1e20f, depth1 = 1e20f, depth2 = 1e20f, depth3 = 1e20f;
	[unroll] for (uint lane = 0u; lane < 4u; ++lane) {
		uint2 coord = pixCoord + uint2(lane & 1u, lane >> 1u);
		float z = 1e20f;
		if (all(coord < uint2(FrameDim))) {
			float converted = ScreenToViewDepth(srcNDCDepth.Load(int3(coord, 0)));
			if (isfinite(converted) && converted > 0.0f)
				z = converted;
		}
		if (lane == 0u) depth0 = z;
		if (lane == 1u) depth1 = z;
		if (lane == 2u) depth2 = z;
		if (lane == 3u) depth3 = z;
		if (all(coord < uint2(TexDim))) outDepth0[coord] = z;
	}
#else
	float4 depths4 = srcNDCDepth.GatherRed(samplerPointClamp, FullFrameTextureUV(uv));
	float depth0 = ClampDepth(depths4.w);
	float depth1 = ClampDepth(depths4.z);
	float depth2 = ClampDepth(depths4.x);
	float depth3 = ClampDepth(depths4.y);
	outDepth0[pixCoord + uint2(0, 0)] = depth0;
	outDepth0[pixCoord + uint2(1, 0)] = depth1;
	outDepth0[pixCoord + uint2(0, 1)] = depth2;
	outDepth0[pixCoord + uint2(1, 1)] = depth3;
#endif

	// MIP 1
	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	if (all(baseCoord < (uint2(TexDim) >> 1))) outDepth1[baseCoord] = dm1;
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
		float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

		float dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 2 < (uint2(TexDim) >> 2))) outDepth2[baseCoord / 2] = dm2;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
		float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

		float dm3 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 4 < (uint2(TexDim) >> 3))) outDepth3[baseCoord / 4] = dm3;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
		float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

		float dm4 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 8 < (uint2(TexDim) >> 4))) outDepth4[baseCoord / 8] = dm4;
		//g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm4;
	}
}
