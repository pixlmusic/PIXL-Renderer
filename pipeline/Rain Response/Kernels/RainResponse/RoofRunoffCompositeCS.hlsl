#include "Common/SharedData.hlsli"

Texture2D<uint> RunoffMask : register(t0);
RWTexture2D<float4> MainRW : register(u0);

float LoadRunoffMask(int2 pixel, int2 size)
{
	pixel = clamp(pixel, int2(0, 0), size - 1);
	float rawMask = saturate((float)RunoffMask.Load(int3(pixel, 0)) / 65535.0f);
	// Reject only the faint quantised halo. The bright bead remains intact while
	// its optical falloff reads tighter and less fuzzy after TAA/DLSS.
	return smoothstep(0.055f, 0.86f, rawMask);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width = 0;
	uint height = 0;
	MainRW.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;
	uint runoffWidth = 0;
	uint runoffHeight = 0;
	RunoffMask.GetDimensions(runoffWidth, runoffHeight);
	if (dispatchID.x >= runoffWidth || dispatchID.y >= runoffHeight)
		return;

	uint encoded = RunoffMask.Load(int3(dispatchID.xy, 0));
	float rawMask = saturate((float)encoded / 65535.0f);
	float mask = smoothstep(0.055f, 0.86f, rawMask);
	float rain = saturate(SharedData::rainResponseSettings.Raining);
	float strength = max(SharedData::rainResponseSettings.RainRunoffStrength, 0.0f);
	const bool debugMode = strength >= 1.49f;

	if (debugMode) {
		float4 scene = MainRW[dispatchID.xy];

		// Faint violet tint proves the runoff composite reaches this render target.
		scene.xyz = lerp(scene.xyz, float3(0.07f, 0.01f, 0.10f), 0.10f);

		// Top-left legend: red, orange, yellow, cyan, blue, green.
		if (dispatchID.y < 10u && dispatchID.x < 150u) {
			uint band = min(dispatchID.x / 25u, 5u);
			float3 legend =
				band == 0u ? float3(1.0f, 0.02f, 0.02f) :
				band == 1u ? float3(1.0f, 0.28f, 0.02f) :
				band == 2u ? float3(1.0f, 0.92f, 0.02f) :
				band == 3u ? float3(0.02f, 1.0f, 1.0f) :
				band == 4u ? float3(0.08f, 0.20f, 1.0f) :
				             float3(0.02f, 1.0f, 0.08f);
			scene.xyz = legend;
		}

		if (encoded != 0u) {
			float3 debugColor =
				mask < 0.27f ? float3(1.0f, 0.02f, 0.02f) :
				mask < 0.47f ? float3(1.0f, 0.28f, 0.02f) :
				mask < 0.68f ? float3(1.0f, 0.92f, 0.02f) :
				mask < 0.84f ? float3(0.02f, 1.0f, 1.0f) :
				mask < 0.97f ? float3(0.08f, 0.20f, 1.0f) :
				               float3(0.02f, 1.0f, 0.08f);
			scene.xyz = lerp(scene.xyz, debugColor, 0.92f);
		}

		MainRW[dispatchID.xy] = scene;
		return;
	}

	if (encoded == 0u)
		return;

	// Strength still changes ONLY optical response, not emitter count/work.
	// Phase 7 needed ~1.45 before drops became readable. Remap the useful range so
	// 0.8-1.0 is already strong, while keeping 1.50 reserved for debug mode.
	float normalizedStrength = saturate(strength / 1.25f);
	float opticalGain =
		0.30f + pow(normalizedStrength, 0.62f) * 1.52f;
	float a = saturate(mask * rain * opticalGain);
	if (a <= 1e-4f)
		return;

	float4 scene = MainRW[dispatchID.xy];
	int2 p = int2(dispatchID.xy);
	int2 runoffSize = int2(runoffWidth, runoffHeight);
	float maskLeft = LoadRunoffMask(p + int2(-1, 0), runoffSize);
	float maskRight = LoadRunoffMask(p + int2(1, 0), runoffSize);
	float maskUp = LoadRunoffMask(p + int2(0, -1), runoffSize);
	float maskDown = LoadRunoffMask(p + int2(0, 1), runoffSize);
	float2 lensGradient = float2(maskRight - maskLeft, maskDown - maskUp);
	float lensEdge = saturate(length(lensGradient) * 1.65f);
	float lensCore = saturate(mask - max(max(maskLeft, maskRight), max(maskUp, maskDown)) * 0.32f);

	float directionalLuma = dot(
		max(SharedData::DirLightColor.xyz, 0.0f),
		float3(0.299f, 0.587f, 0.114f));
	float optical = saturate(
		0.34f +
		directionalLuma * 0.20f +
		max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f) * 0.24f);

	// Smaller/subtler than Phase 4. No neighbour dilation/broadening is performed.
	// A restrained dark rim plus a cool upper/core glint gives each projected mask
	// a convex water-lens cue. The effect stays in the active pre-upscale render
	// space, allowing DLSS to reconstruct it together with the scene instead of
	// magnifying a display-space overlay.
	float rimDarkening = a * lensEdge * 0.075f;
	scene.xyz *= 1.0f - rimDarkening;

	float3 waterTarget = max(
		scene.xyz * (1.035f + lensCore * 0.045f),
		lerp(0.36f.xxx, float3(0.72f, 0.80f, 0.86f), optical));
	float highlight = a * (0.48f + lensCore * 0.36f) * (1.0f - lensEdge * 0.22f);
	scene.xyz = lerp(scene.xyz, waterTarget, highlight);

	MainRW[dispatchID.xy] = scene;
}
