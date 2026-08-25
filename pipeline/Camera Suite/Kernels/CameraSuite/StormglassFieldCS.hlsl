/**
 * @file StormglassFieldCS.hlsl
 * @brief Builds PIXL's procedural wet-lens optical field at quarter resolution.
 *
 * v3.8:
 *  - reuses Skyrim's precipitation-occlusion depth map to suppress fresh lens
 *    rain under roofs, bridges and awnings;
 *  - keeps residual wetness / emerge film independent of shelter;
 *  - computes the shelter value once per 8x8 group to keep the added cost tiny.
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "CameraSuite/PhysicalCameraCommon.hlsli"
#include "CameraSuite/Stormglass.hlsli"

#define SKY_BOUNCE_PROBE_REGISTER t1
#include "SkyBounce/SkyBounce.hlsli"

Texture2D<float> PrecipOcclusionTex : register(t0);
RWTexture2D<float4> StormglassField : register(u0);

groupshared float PixlGroupRainExposure;

float PixlStormglassCameraRainExposure()
{
	uint maskWidth = 0u;
	uint maskHeight = 0u;
	PrecipOcclusionTex.GetDimensions(maskWidth, maskHeight);

	// Null/unavailable SRV: preserve the existing behavior rather than
	// accidentally suppressing Stormglass.
	if (maskWidth == 0u || maskHeight == 0u)
		return 1.0f;

	float4x4 occlusionMatrix = SharedData::rainResponseSettings.OcclusionViewProj;
	const float4 ones = float4(1.0f, 1.0f, 1.0f, 1.0f);
	float matrixEnergy =
		dot(abs(occlusionMatrix[0]), ones) +
		dot(abs(occlusionMatrix[1]), ones) +
		dot(abs(occlusionMatrix[2]), ones) +
		dot(abs(occlusionMatrix[3]), ones);

	// Rain Response may be disabled while CameraSuite's direct weather signal is
	// still active. In that case no valid precipitation projection is available.
	if (matrixEnergy < 1e-5f)
		return 1.0f;

	// Camera/eye is the origin in the model-space convention used by PIXL's
	// precipitation/skylighting projection.
	float3 cameraOS = mul(occlusionMatrix, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
	cameraOS.y = -cameraOS.y;
	float2 maskUV = cameraOS.xy * 0.5f + 0.5f;

	if (any(maskUV <= 0.0f.xx) || any(maskUV >= 1.0f.xx))
		return 1.0f;

	float2 maskSize = float2(maskWidth, maskHeight);
	int2 centre = int2(maskUV * maskSize);
	int2 maxCoord = int2(maskWidth - 1u, maskHeight - 1u);

	// Match the engine/SkyBounce LESS_EQUAL depth comparison. A tiny receiver
	// bias avoids flicker when the camera sits almost exactly on the mask plane.
	float receiverDepth = cameraOS.z - 0.0012f;
	float visibility = 0.0f;

	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			int2 coord = clamp(centre + int2(x, y), int2(0, 0), maxCoord);
			float occluderDepth = PrecipOcclusionTex.Load(int3(coord, 0));
			visibility += step(receiverDepth, occluderDepth);
		}
	}

	visibility *= (1.0f / 9.0f);

	// Soft threshold prevents rapid on/off chatter at awning edges while still
	// decisively stopping fresh drops when fully covered.
	return smoothstep(0.18f, 0.72f, visibility);
}

float PixlStormglassSkyBounceExposure()
{
	uint probeWidth = 0u;
	uint probeHeight = 0u;
	uint probeDepth = 0u;
	SkyBounce::SkyBounceProbeArray.GetDimensions(probeWidth, probeHeight, probeDepth);

	// SkyBounce can be disabled/unavailable independently. In that case retain
	// the precipitation-map behavior rather than suppressing all rain.
	if (probeWidth == 0u || probeHeight == 0u || probeDepth == 0u)
		return 1.0f;

	// Camera-relative model space puts the camera/eye at the origin.
	sh2 cameraSky = SkyBounce::SampleNoBias(float3(0.0f, 0.0f, 0.0f));
	float visibility = SkyBounce::EvaluateVisibility(cameraSky);

	// SkyBounce retains a configurable minimum visibility, so use a soft range
	// above that floor. This gives stable edge behavior under awnings while
	// decisively shutting off fresh drops beneath solid cover.
	return smoothstep(0.22f, 0.68f, visibility);
}

[numthreads(8, 8, 1)]
void main(
	uint3 dispatchID : SV_DispatchThreadID,
	uint3 groupThreadID : SV_GroupThreadID)
{
	if (groupThreadID.x == 0u && groupThreadID.y == 0u)
	{
		float precipExposure = PixlStormglassCameraRainExposure();
		float skyBounceExposure = PixlStormglassSkyBounceExposure();
		PixlGroupRainExposure = min(precipExposure, skyBounceExposure);
	}

	GroupMemoryBarrierWithGroupSync();

	uint width, height;
	StormglassField.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	uint2 fieldDimensions = max(uint2(width, height), 1u.xx);
	uint2 presentationDimensions = max(fieldDimensions * 4u, 1u.xx);
	float2 invFieldDimensions = rcp(float2(fieldDimensions));
	float2 uv = (float2(dispatchID.xy) + 0.5f) * invFieldDimensions;

	PixlStormglassWarpData lens = PixlApplyStormglassSampledExposure(
		uv,
		presentationDimensions,
		invFieldDimensions,
		PixlGroupRainExposure);

	StormglassField[dispatchID.xy] =
		float4(lens.uv - uv, lens.coverage, lens.rim);
}
