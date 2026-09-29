// PIXL Renderer - distant loaded-light optical resolve.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/SharedData.hlsli"

struct DistantEmitter
{
	float3 Position;
	float Radius;
	float3 Color;
	float Distance;
};

StructuredBuffer<DistantEmitter> Emitters : register(t0);
Texture2D<float> SceneDepth : register(t1);
RWTexture2D<uint> OpticalMask : register(u0);

cbuffer DistantLifeTuning : register(b13)
{
	float2 RenderSize;
	float2 InvRenderSize;
	float2 OutputSize;
	float2 InvOutputSize;
	float MinimumDistance;
	float MaximumDistance;
	float GlobalIntensity;
	float AtmosphericAttenuation;
	uint EmitterCount;
	uint DebugMode;
	uint FarFieldActivity;
	float FarFieldDensity;
	float FarFieldMotion;
	float3 DistantLifePadding;
};

uint EncodeOpticalSource(float intensity, float3 color)
{
	uint encodedIntensity = (uint)round(saturate(intensity) * 2047.0f);
	uint3 encodedColor = (uint3)round(saturate(color) * 127.0f);
	return (encodedIntensity << 21u) | (encodedColor.r << 14u) |
		(encodedColor.g << 7u) | encodedColor.b;
}

void WriteSource(int2 pixel, float intensity, float3 color)
{
	if (any(pixel < 0) || any(pixel >= int2(RenderSize)))
		return;
	uint previous = 0u;
	InterlockedMax(OpticalMask[pixel], EncodeOpticalSource(intensity, color), previous);
}

[numthreads(16, 16, 1)]
void BuildMaskCS(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID)
{
	if (groupID.x >= EmitterCount)
		return;
	DistantEmitter emitter = Emitters[groupID.x];
	if (!all(isfinite(float4(emitter.Position, emitter.Distance))) || emitter.Distance <= 0.0f)
		return;

	// Emitters are uploaded in Skyrim's native camera-relative coordinate space,
	// which is the direct input contract of the engine view-projection matrix.
	float4 clip = mul(FrameBuffer::CameraViewProj, float4(emitter.Position, 1.0f));
	if (!all(isfinite(clip)) || clip.w <= 1.0e-5f)
		return;
	float3 ndc = clip.xyz / clip.w;
	if (ndc.z < 0.0f || ndc.z > 1.0f)
		return;
	float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
	if (any(uv <= 0.0f) || any(uv >= 1.0f))
		return;

	float sourceDepth = SharedData::GetScreenDepth(ndc.z);
	if (!isfinite(sourceDepth) || sourceDepth <= 0.0f)
		return;
	float distanceT = saturate((emitter.Distance - MinimumDistance) / max(MaximumDistance - MinimumDistance, 1.0f));
	float projectedRadius = emitter.Radius / max(sourceDepth, 1.0f) * RenderSize.y * 0.04f;
	float radiusPixels = clamp(max(projectedRadius, lerp(1.25f, 3.35f, distanceT)), 1.25f, 7.0f);
	int2 centre = int2(uv * RenderSize);
	int2 offset = int2(threadID.xy) - 8;
	float radius2 = dot(float2(offset), float2(offset));
	if (radius2 > radiusPixels * radiusPixels)
		return;
	int2 pixel = centre + offset;
	if (any(pixel < 0) || any(pixel >= int2(RenderSize)))
		return;

	if (DebugMode == 0u) {
		float sceneRaw = SceneDepth.Load(int3(pixel, 0));
		float sceneLinear = SharedData::GetScreenDepth(sceneRaw);
		// Allow only a small fixture-depth allowance. The previous 96-unit
		// minimum could make distant emitters leak through buildings and terrain.
		// Large sources are capped as well: their radius must not exempt a wall.
		float depthTolerance = clamp(emitter.Radius * 0.18f, 8.0f, 24.0f);
		if (sceneRaw < 0.999999f && isfinite(sceneLinear) && sceneLinear + depthTolerance < sourceDepth)
			return;
	}

	float transitionWidth = max(256.0f, (MaximumDistance - MinimumDistance) * 0.12f);
	float nearFade = smoothstep(MinimumDistance, MinimumDistance + transitionWidth, emitter.Distance);
	float farFade = 1.0f - smoothstep(MaximumDistance * 0.82f, MaximumDistance, emitter.Distance);
	// Preserve a faint optical signal at Skyrim's far LOD. The former extinction
	// left less than 0.1% energy at 200k units, making the extended range moot.
	float atmosphere = lerp(1.0f, exp(-emitter.Distance * 0.000012f), saturate(AtmosphericAttenuation));
	float radialFalloff = lerp(3.0f, 1.65f, distanceT);
	float radial = exp2(-radialFalloff * radius2 / max(radiusPixels * radiusPixels, 1.0f));
	float authoredStrength = max(0.20f, saturate(emitter.Radius / 640.0f));
	float distanceDim = lerp(1.0f, 0.38f, smoothstep(0.10f, 1.0f, distanceT));
	float intensity = radial * nearFade * farFade * atmosphere * authoredStrength * GlobalIntensity * distanceDim;
	float3 color = max(emitter.Color, 0.015f.xxx);
	if (DebugMode == 1u) {
		intensity = radius2 <= 2.0f ? 1.0f : 0.0f;
		color = float3(0.1f, 1.0f, 0.2f);
	} else if (DebugMode == 2u) {
		intensity = radial;
		color = float3(0.1f, 0.45f, 1.0f);
	}
	if (intensity > 1.0e-4f)
		WriteSource(pixel, intensity, color);
}

Texture2D<uint> CompositeMask : register(t0);
Texture2D<float> CompositeDepth : register(t1);
RWTexture2D<float4> SceneColor : register(u0);

float Hash21(float2 value)
{
	return frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);
}

float3 ReconstructCompositePosition(uint2 pixel, float depth)
{
	float2 uv = (float2(pixel) + 0.5f) * InvRenderSize;
	float4 clip = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
	float4 position = mul(FrameBuffer::CameraViewProjInverse, clip);
	return position.xyz / max(abs(position.w), 1.0e-6f);
}

float EvaluateFarFieldActivity(uint2 pixel, float sceneDepth)
{
	if (FarFieldActivity == 0u || sceneDepth >= 0.999999f)
		return 0.0f;

	float3 centre = ReconstructCompositePosition(pixel, sceneDepth);
	float viewDistance = length(centre);
	float farStart = max(MinimumDistance * 1.35f, 5000.0f);
	if (viewDistance < farStart || viewDistance > MaximumDistance)
		return 0.0f;

	uint2 leftPixel = pixel - uint2(pixel.x > 0u ? 1u : 0u, 0u);
	uint2 upPixel = pixel - uint2(0u, pixel.y > 0u ? 1u : 0u);
	float leftDepth = CompositeDepth.Load(int3(leftPixel, 0));
	float upDepth = CompositeDepth.Load(int3(upPixel, 0));
	if (leftDepth >= 0.999999f || upDepth >= 0.999999f)
		return 0.0f;
	float3 left = ReconstructCompositePosition(leftPixel, leftDepth);
	float3 up = ReconstructCompositePosition(upPixel, upDepth);
	float3 surfaceCross = cross(left - centre, up - centre);
	float surfaceLengthSq = dot(surfaceCross, surfaceCross);
	if (surfaceLengthSq < 1.0e-8f)
		return 0.0f;
	float3 surfaceNormal = surfaceCross * rsqrt(surfaceLengthSq);
	// The visible depth surface provides occlusion for free. Restrict activity
	// to upward-facing land-like surfaces so walls and architecture do not glow.
	// Reconstruction winding can flip with the active projection convention;
	// use the magnitude so valid upward terrain is not rejected wholesale.
	if (abs(surfaceNormal.z) < 0.62f)
		return 0.0f;

	float cellSize = 768.0f;
	float2 cellPosition = (centre.xy + FrameBuffer::CameraPosAdjust.xy) / cellSize;
	float2 cell = floor(cellPosition);
	float seed = Hash21(cell + float2(19.7f, 47.3f));
	// Group adjacent cells into occasional settlement-like clusters while
	// leaving most wilderness almost empty.
	float2 settlementCell = floor(cell / 4.0f);
	float settlementSeed = Hash21(settlementCell + float2(83.2f, 11.6f));
	float settlementBias = step(settlementSeed, 0.22f) * 0.24f;
	float activityThreshold = saturate(FarFieldDensity * 0.72f + settlementBias);
	if (seed > activityThreshold)
		return 0.0f;
	float2 pointOffset = float2(
		Hash21(cell + float2(7.1f, 13.9f)),
		Hash21(cell + float2(29.4f, 61.2f)));
	// Most points represent fixed homes, braziers, or campfires. A smaller
	// deterministic subset becomes a slow traveller, selling distant life
	// without making every hillside shimmer in sync.
	float traveller = step(0.76f, seed);
	float phase = SharedData::Timer * (0.018f + FarFieldMotion * 0.035f) + seed * 6.2831853f;
	pointOffset = frac(pointOffset + traveller * float2(sin(phase), cos(phase * 0.83f)) * (0.045f + FarFieldMotion * 0.035f));
	float2 local = frac(cellPosition) - pointOffset;
	local = local - round(local);
	float pointDistance = length(local);
	float distanceT = saturate((viewDistance - farStart) / max(MaximumDistance - farStart, 1.0f));
	float bokehScale = lerp(0.85f, 1.65f, distanceT);
	float footprint = 1.0f - smoothstep(0.012f * bokehScale, 0.070f * bokehScale, pointDistance);
	footprint = pow(saturate(footprint), lerp(1.25f, 0.72f, distanceT));
	float pulse = lerp(0.86f, 0.58f + 0.42f * sin(SharedData::Timer * (0.10f + seed * 0.07f) + seed * 13.0f), traveller);
	float distanceDim = lerp(1.0f, 0.30f, distanceT);
	return footprint * saturate(pulse) * distanceDim * (1.0f - smoothstep(farStart, MaximumDistance, viewDistance));
}

[numthreads(8, 8, 1)]
void CompositeCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (any(dispatchID.xy >= uint2(RenderSize)))
		return;
	uint packed = CompositeMask[dispatchID.xy];
	float syntheticIntensity = EvaluateFarFieldActivity(dispatchID.xy, CompositeDepth.Load(int3(dispatchID.xy, 0)));
	if (packed == 0u && syntheticIntensity <= 1.0e-4f)
		return;
	float intensity = float((packed >> 21u) & 2047u) / 2047.0f;
	float3 color = float3(
		(packed >> 14u) & 127u,
		(packed >> 7u) & 127u,
		packed & 127u) / 127.0f;
	float4 scene = SceneColor[dispatchID.xy];
	scene.rgb += color * intensity * 1.60f;
	// Synthetic activity is deliberately dimmer and warmer than real emitters;
	// it is an optical suggestion, never a gameplay light.
	scene.rgb += float3(1.0f, 0.42f, 0.12f) * syntheticIntensity * GlobalIntensity * 0.20f;
	SceneColor[dispatchID.xy] = scene;
}
