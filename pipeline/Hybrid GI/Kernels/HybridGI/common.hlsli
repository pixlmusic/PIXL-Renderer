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

// with additional edits by FiveLimbedCat/ProfJack

#ifndef HybridGI_COMMON
#define HybridGI_COMMON

///////////////////////////////////////////////////////////////////////////////

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Color.hlsli"

cbuffer HybridGICB : register(b1)
{
	float4x4 PrevInvViewMat;
	float4 NDCToViewMul;
	float4 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;

	uint FrameIndex;

	uint NumSlices;
	uint NumSteps;

	float MinScreenRadius;
	float AORadius;
	float GIRadius;
	float EffectRadius;
	float Thickness;
	float2 DepthFadeRange;
	float DepthFadeScaleConst;

	float GISaturation;
	float GIDistanceCompensation;
	float GICompensationMaxDist;
	float pad1;

	float AOPower;
	float GIStrength;

	float DepthDisocclusion;
	float NormalDisocclusion;
	uint MaxAccumFrames;

	float BlurRadius;
	float DistanceNormalisation;

	uint WorldCacheEnabled;
	uint WorldCacheMaxAge;
	uint WorldCacheSampleCount;
	uint WorldCacheTraceSteps;

	float WorldCacheStrength;
	float WorldCacheCellSizeNear;
	float WorldCacheCellSizeFar;
	float WorldCacheRadius;

	float WorldCacheLeakReduction;
	uint WorldCacheInjectionStride;
	uint WorldCacheDirectionalOcclusionEnabled;
	float WorldCacheDirectionalOcclusionStrength;

	uint DebugView;
	float DebugGain;
	float WorldCacheTemporalResponse;
	uint WorldCacheReflectionEnabled;
	float WorldCacheReflectionStrength;
	float WorldCacheReflectionRoughnessCutoff;
	uint WorldCacheSecondBounceEnabled;
	float WorldCacheSecondBounceStrength;
	float2 pad2;

	// PIXL Rendering vNext. All fields are appended: the original 304-byte
	// HybridGI ABI and every pre-existing offset remain untouched.
	uint BentNormalEnabled;
	float BentNormalStrength;
	uint SpecularOcclusionEnabled;
	float SpecularOcclusionStrength;

	float RadianceFireflyClamp;
	float UpsampleEdgeThreshold;
	float ReflectionIntensity;
	float ReflectionMaxRoughness;

	float ReflectionMaxDistance;
	float ReflectionThickness;
	uint ReflectionSteps;
	float ReflectionTemporalResponse;

	float ReflectionFireflyClamp;
	float ReflectionWorldFallbackStrength;
	float ReflectionRayBias;
	float ReflectionRoughnessJitter;

	// PIXL Contact Depth is append-only to preserve every historical binding.
	uint ContactDepthEnabled;
	float ContactDepthStrength;
	float ContactDepthRadius;
	float ContactDepthBias;

	// Task 09 adaptive ray allocation is append-only. The permutation is
	// enabled at shader compile time; this value controls its work floor.
	float AdaptiveRayMinimum;
	float3 pad3;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

///////////////////////////////////////////////////////////////////////////////

// first person z
#define FP_Z (18.0)

// Small denominators recur throughout the slice-marching (angle/length) and the
// bilinear history-reprojection (weight sum) code. These were referenced from
// multiple shaders but never actually defined anywhere in the shared header -
// centralising them here so every translation unit agrees on the same guard.
#ifndef EPSILON_LENGTH_SQ
#define EPSILON_LENGTH_SQ (1e-7)
#endif
#ifndef EPSILON_WEIGHT_SUM
#define EPSILON_WEIGHT_SUM (1e-5)
#endif

#define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
float filterNaN(float v)
{
	return ISNAN(v) ? 0 : v;
}
float2 filterNaN(float2 v) { return float2(filterNaN(v.x), filterNaN(v.y)); }
float3 filterNaN(float3 v) { return float3(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z)); }
float4 filterNaN(float4 v) { return float4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

float filterInf(float v) { return isinf(v) ? 0 : v; }
float2 filterInf(float2 v) { return float2(filterInf(v.x), filterInf(v.y)); }
float3 filterInf(float3 v) { return float3(filterInf(v.x), filterInf(v.y), filterInf(v.z)); }
float4 filterInf(float4 v) { return float4(filterInf(v.x), filterInf(v.y), filterInf(v.z), filterInf(v.w)); }

// [Optimization] Half-precision overloads. gi_cs and other passes carry their
// temporal accumulators in half registers (currY, currCoCg, currGIAOSpecular,
// srcPrevGeo, etc). Calling the float overloads above on half data silently
// upconverts to float for the call and back down again afterwards; these
// overloads let the compiler stay in half precision end-to-end, which is
// strictly cheaper on hardware with native fp16 throughput and avoids
// widening register pressure in already register-heavy passes.
half filterNaN(half v) { return ISNAN(v) ? (half)0 : v; }
half2 filterNaN(half2 v) { return half2(filterNaN(v.x), filterNaN(v.y)); }
half3 filterNaN(half3 v) { return half3(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z)); }
half4 filterNaN(half4 v) { return half4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

half filterInf(half v) { return isinf(v) ? (half)0 : v; }
half2 filterInf(half2 v) { return half2(filterInf(v.x), filterInf(v.y)); }
half3 filterInf(half3 v) { return half3(filterInf(v.x), filterInf(v.y), filterInf(v.z)); }
half4 filterInf(half4 v) { return half4(filterInf(v.x), filterInf(v.y), filterInf(v.z), filterInf(v.w)); }

// Rec.709 relative luminance. Several passes (world-cache injection, radiance
// prefiltering, firefly suppression) need this; centralising it avoids drift
// between copies and gives the compiler one canonical constant-folded form.
float Luminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Soft-knee luminance clamp: values under the threshold are untouched, values
// over it are compressed towards the threshold instead of being hard-clipped,
// which avoids a visible hue shift while still preventing a single very bright
// texel (a window, a candle flame, a specular glint) from blowing out an
// entire GI mip chain or a world-cache voxel for many frames.
// clampLuminance <= 0 disables the clamp and returns the input unchanged, so
// call sites that don't have an artist-set threshold behave exactly as before.
float3 ClampFireflies(float3 color, float clampLuminance)
{
	float3 result = color;
	float lum = Luminance(color);
	if (clampLuminance > 0.0 && lum > clampLuminance) {
		// Keep the knee proportional to the selected exposure range. The previous
		// fixed +4.0 asymptote made a Low-tier threshold of 4 almost indistinguishable
		// from an unclamped value of 8, while behaving acceptably at the Ultra value.
		// A 50% shoulder is predictable across every PIXL quality tier.
		float excess = lum - clampLuminance;
		float shoulder = max(clampLuminance * 0.5, 0.25);
		float compressed = clampLuminance + excess / (1.0 + excess / shoulder);
		result *= compressed / max(lum, 1e-5);
	}
	return result;
}

// screenPos - normalised position in FrameDim
// uv - normalised position in FrameDim
// texCoord - texture coordinate

#ifdef HALF_RES
#	define RES_MIP 1
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.5)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 2)
#	define OUT_FRAME_SCALE (frameScale * 0.5)
#elif defined(QUARTER_RES)
#	define RES_MIP 2
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.25)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 4)
#	define OUT_FRAME_SCALE (frameScale * 0.25)
#else
#	define RES_MIP 0
#	define READ_DEPTH(tex, px) tex[px]
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex[px]
#	define OUT_FRAME_DIM FrameDim
#	define RCP_OUT_FRAME_DIM RcpFrameDim
#	define OUT_FRAME_SCALE frameScale
#endif

///////////////////////////////////////////////////////////////////////////////

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (NDCToViewMul.xy * screenPos.xy + NDCToViewAdd.xy) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

// Exact inverse of ScreenToViewPosition for positive view-space Z. Used by
// stochastic reflection rays to project a view-space sample back into the
// current frame without requiring another projection matrix in the HybridGI CB.
float2 ViewToScreenPosition(const float3 viewPosition)
{
	float safeZ = max(abs(viewPosition.z), 1e-5f) * (viewPosition.z < 0.0f ? -1.0f : 1.0f);
	return (viewPosition.xy / safeZ - NDCToViewAdd.xy) / NDCToViewMul.xy;
}

float ScreenToViewDepth(const float screenDepth)
{
	return (SharedData::CameraData.w / (-screenDepth * SharedData::CameraData.z + SharedData::CameraData.x));
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

///////////////////////////////////////////////////////////////////////////////

// "Efficiently building a matrix to rotate one vector to another"
// http://cs.brown.edu/research/pubs/pdfs/1999/Moller-1999-EBA.pdf / https://dl.acm.org/doi/10.1080/10867651.1999.10487509
// (using https://github.com/assimp/assimp/blob/master/include/assimp/matrix3x3.inl#L275 as a code reference as it seems to be best)
float3x3 RotFromToMatrix(float3 from, float3 to)
{
	const float e = dot(from, to);
	const float f = abs(e);  //(e < 0)? -e:e;

	// WARNING: This has not been tested/worked through, especially not for 16bit floats; seems to work in our special use case (from is always {0, 0, -1}) but wouldn't use it in general
	if (f > float(1.0 - 0.0003))
		return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);

	const float3 v = cross(from, to);
	/* ... use this hand optimized version (9 mults less) */
	const float h = (1.0) / (1.0 + e); /* optimization by Gottfried Chen */
	const float hvx = h * v.x;
	const float hvz = h * v.z;
	const float hvxy = hvx * v.y;
	const float hvxz = hvx * v.z;
	const float hvyz = hvz * v.y;

	float3x3 mtx;
	mtx[0][0] = e + hvx * v.x;
	mtx[0][1] = hvxy - v.z;
	mtx[0][2] = hvxz + v.y;

	mtx[1][0] = hvxy + v.z;
	mtx[1][1] = e + h * v.y * v.y;
	mtx[1][2] = hvyz - v.x;

	mtx[2][0] = hvxz - v.y;
	mtx[2][1] = hvyz + v.x;
	mtx[2][2] = e + hvz * v.z;

	return mtx;
}

///////////////////////////////////////////////////////////////////////////////

// credit: Olivier Therrien
float specularLobeHalfAngle(float roughness)
{
	float roughness2 = roughness * roughness;
	return clamp(4.1679 * roughness2 * roughness2 - 9.0127 * roughness2 * roughness + 4.6161 * roughness2 + 1.7048 * roughness + 0.1, 0, Math::HALF_PI);
}

// https://www.gdcvault.com/play/1026701/Fast-Denoising-With-Self-Stabilizing
float3 getSpecularDominantDirection(float3 N, float3 V, float roughness)
{
	roughness = saturate(roughness);
	float f = (1 - roughness) * (sqrt(1 - roughness) + roughness);
	float3 R = reflect(-V, N);
	float3 D = lerp(N, R, f);

	return D * rsqrt(max(dot(D, D), EPSILON_LENGTH_SQ));
}

#endif
