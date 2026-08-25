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

Texture2D<float3> srcRadiance : register(t0);

RWTexture2D<float3> outRadiance0 : register(u0);
RWTexture2D<float3> outRadiance1 : register(u1);
RWTexture2D<float3> outRadiance2 : register(u2);
RWTexture2D<float3> outRadiance3 : register(u3);
RWTexture2D<float3> outRadiance4 : register(u4);

// [Improvement] Karis-style luminance-weighted average instead of a flat box
// filter. gi_cs samples progressively coarser mips of this chain for
// progressively longer-range GI rays (see mipLevelRadiance in gi_cs.hlsl), so
// a single very bright texel (window glare, a torch, a specular highlight)
// would otherwise dominate an entire coarse mip texel and get reused by every
// long-range ray that lands anywhere near it - a classic source of GI
// flicker/fireflies that swims with camera motion. Down-weighting samples by
// 1/(luminance+1) (the same trick used for bloom/mip downsampling in many
// engines) suppresses that without introducing energy loss for normal-range
// values, since the weights approach uniform as luminance drops.
float3 RadianceMIPFilter(float3 radiance0, float3 radiance1, float3 radiance2, float3 radiance3)
{
	float w0 = rcp(Luminance(radiance0) + 1.0);
	float w1 = rcp(Luminance(radiance1) + 1.0);
	float w2 = rcp(Luminance(radiance2) + 1.0);
	float w3 = rcp(Luminance(radiance3) + 1.0);
	float wSum = max(w0 + w1 + w2 + w3, 1e-5);

	return (radiance0 * w0 + radiance1 * w1 + radiance2 * w2 + radiance3 * w3) / wSum;
}

// Reject an isolated hot texel before it enters the persistent mip chain. This
// uses the four values already fetched by Gather, so it adds no bandwidth. A
// broad fire/window remains bright because its neighbouring luminance raises
// the reference; a lone stochastic/specular spike is bounded relative to the
// other three samples and cannot seed a multi-frame GI sparkle.
void ClampIsolatedRadiance4(inout float3 r0, inout float3 r1, inout float3 r2, inout float3 r3)
{
	float4 lum = float4(Luminance(r0), Luminance(r1), Luminance(r2), Luminance(r3));
	float brightest = max(max(lum.x, lum.y), max(lum.z, lum.w));
	float reference = max((dot(lum, 1.0f.xxxx) - brightest) * (1.0f / 3.0f), 0.0f);
	float relativeLimit = reference * 3.0f + 0.15f;
	float limit = RadianceFireflyClamp > 0.0f ? max(RadianceFireflyClamp, relativeLimit) : relativeLimit;

	float4 scale = min(1.0f.xxxx, limit / max(lum, 1e-5f.xxxx));
	r0 *= scale.x;
	r1 *= scale.y;
	r2 *= scale.z;
	r3 *= scale.w;
}

groupshared float3 g_scratchRadiance[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 rad0 = srcRadiance.GatherRed(samplerPointClamp, uv * frameScale);
	float4 rad1 = srcRadiance.GatherGreen(samplerPointClamp, uv * frameScale);
	float4 rad2 = srcRadiance.GatherBlue(samplerPointClamp, uv * frameScale);

	float3 radiance0 = float3(rad0.w, rad1.w, rad2.w);
	float3 radiance1 = float3(rad0.z, rad1.z, rad2.z);
	float3 radiance2 = float3(rad0.x, rad1.x, rad2.x);
	float3 radiance3 = float3(rad0.y, rad1.y, rad2.y);

	// [Improvement] Optional soft-knee firefly clamp (see RadianceFireflyClamp
	// in common.hlsli - disabled by default at 0, so existing setups are
	// unaffected). Applying it here, at MIP 0, suppresses outliers once before
	// they propagate into every coarser mip below, which is cheaper and more
	// consistent than re-clamping at every mip level.
	radiance0 = filterNaN(filterInf(ClampFireflies(radiance0, RadianceFireflyClamp)));
	radiance1 = filterNaN(filterInf(ClampFireflies(radiance1, RadianceFireflyClamp)));
	radiance2 = filterNaN(filterInf(ClampFireflies(radiance2, RadianceFireflyClamp)));
	radiance3 = filterNaN(filterInf(ClampFireflies(radiance3, RadianceFireflyClamp)));
	ClampIsolatedRadiance4(radiance0, radiance1, radiance2, radiance3);

	outRadiance0[pixCoord + uint2(0, 0)] = radiance0;
	outRadiance0[pixCoord + uint2(1, 0)] = radiance1;
	outRadiance0[pixCoord + uint2(0, 1)] = radiance2;
	outRadiance0[pixCoord + uint2(1, 1)] = radiance3;

	// MIP 1
	float3 rm1 = RadianceMIPFilter(radiance0, radiance1, radiance2, radiance3);
	outRadiance1[baseCoord] = rm1;
	g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 1];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 1];

		float3 rm2 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance2[baseCoord / 2] = rm2;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 2];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 2];

		float3 rm3 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance3[baseCoord / 4] = rm3;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 4];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 4];

		float3 rm4 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance4[baseCoord / 8] = rm4;
	}
}
