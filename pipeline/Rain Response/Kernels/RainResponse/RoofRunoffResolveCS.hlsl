#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float2> RoofEdgeMask : register(t0);
Texture2D<float4> PreviousRunoffState : register(t1);
RWTexture2D<float4> NextRunoffState : register(u0);

cbuffer RoofRunoffTuning : register(b13)
{
	float RunoffMaxDistance;
	float RunoffNearSizeDistance;
	float RunoffEmitterSpacing;
	float RunoffTuningPad0;
	float2 RunoffRenderSize;
	float2 RunoffInvRenderSize;
	float2 RunoffOutputSize;
	float2 RunoffInvOutputSize;
};

float Hash31(float3 p)
{
	p = frac(p * 0.1031f);
	p += dot(p, p.yzx + 33.33f);
	return frac((p.x + p.y) * p.z);
}

float3 ReconstructCameraRelativePosition(int2 pixel, float depth)
{
	float2 uv = (float2(pixel) + 0.5f) * RunoffInvRenderSize;

	float4 positionCS = float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, depth, 1.0f);
	float4 positionWS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
	float safeW = abs(positionWS.w) > 1e-6f ? positionWS.w : (positionWS.w < 0.0f ? -1e-6f : 1e-6f);
	return positionWS.xyz / safeW;
}

float SeedDistance(float a, float b)
{
	float d = abs(a - b);
	return min(d, 1.0f - d);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width = 0;
	uint height = 0;
	NextRunoffState.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	int2 q = int2(dispatchID.xy);
	float2 edge = RoofEdgeMask.Load(int3(q, 0));

	float rain = saturate(SharedData::rainResponseSettings.Raining);
	if (rain <= 0.01f || edge.x <= 0.012f) {
		NextRunoffState[q] = 0.0f.xxxx;
		return;
	}

	int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
	int2 sourcePixel = min(q * 2 + int2(1, 1), fullSize - 1);
	float3 cameraRelativePosition =
		ReconstructCameraRelativePosition(sourcePixel, edge.y);
	float3 absolutePosition =
		cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;

	// Stable world-cell identity. This is intentionally independent of camera
	// orientation and screen resolution.
	float3 stableCell = floor(absolutePosition / 72.0f);
	float seed = Hash31(stableCell + float3(13.0f, 71.0f, 37.0f));

	// Reproject this CURRENT world-space roof point into the previous frame.
	// Previous state is accepted only when the stable world seed matches, avoiding
	// history smearing onto a different edge after disocclusion/teleportation.
	float previousAccumulation = 0.0f;
	float3 previousCameraRelative =
		absolutePosition - FrameBuffer::CameraPreviousPosAdjust.xyz;
	float4 previousCS = mul(
		FrameBuffer::CameraPreviousViewProjUnjittered,
		float4(previousCameraRelative, 1.0f));

	if (previousCS.w > 1e-5f) {
		float2 previousUV =
			(previousCS.xy / previousCS.w) * float2(0.5f, -0.5f) + 0.5f;

		if (!FrameBuffer::IsOutsideFrame(previousUV, false)) {
			int2 previousQ = int2(previousUV * float2(width, height));
			previousQ = clamp(previousQ, int2(0, 0), int2((int)width - 1, (int)height - 1));

			// A half-resolution history coordinate can cross a texel boundary under
			// tiny camera motion. Search the immediate neighbourhood and retain only
			// the same world-stable seed; this prevents the reservoir disappearing
			// without allowing history to smear onto a different roof edge.
			[unroll]
			for (int oy = -1; oy <= 1; ++oy) {
				[unroll]
				for (int ox = -1; ox <= 1; ++ox) {
					int2 historyQ = clamp(
						previousQ + int2(ox, oy),
						int2(0, 0),
						int2((int)width - 1, (int)height - 1));
					float4 previousState =
						PreviousRunoffState.Load(int3(historyQ, 0));
					float seedMatch =
						1.0f - smoothstep(
							0.0015f,
							0.012f,
							SeedDistance(previousState.y, seed));
					previousAccumulation = max(
						previousAccumulation,
						previousState.x * seedMatch);
				}
			}
		}
	}

	// Rain-exposed roof water builds for seconds, not frames. World-stable capacity
	// variation prevents every eave releasing at the same rate.
	float capacityVariation =
		lerp(0.72f, 1.22f, Hash31(stableCell + float3(93.0f, 11.0f, 57.0f)));
	float inflow =
		edge.x * rain * lerp(0.0065f, 0.0240f, rain * rain) * capacityVariation;

	float retention = lerp(0.9964f, 0.99945f, rain);
	float accumulation =
		saturate(previousAccumulation * retention + inflow);

	// A valid current eave always receives a small instantaneous reservoir. This
	// keeps gameplay motion readable when a newly visible/disoccluded lip has no
	// matching history yet; the slower temporal accumulation still determines the
	// strong storm response.
	float currentReservoir =
		edge.x * lerp(0.12f, 0.34f, rain);
	accumulation = max(accumulation, currentReservoir);

	// Periodic discharge prevents permanent saturation. The same world seed drives
	// the visible release phase in GenerateCS, so accumulation and falling drops
	// feel causally linked rather than random.
	float dischargePhase = frac(
		SharedData::rainResponseSettings.Time * lerp(0.18f, 0.74f, rain) +
		seed * 17.31f);
	float dischargeWindow =
		smoothstep(0.58f, 0.62f, dischargePhase) *
		(1.0f - smoothstep(0.67f, 0.76f, dischargePhase));
	accumulation = max(
		0.0f,
		accumulation - dischargeWindow * smoothstep(0.26f, 0.78f, accumulation) * 0.022f);

	// x accumulated water, y world-stable seed, z source device depth, w edge strength.
	NextRunoffState[q] = float4(accumulation, seed, edge.y, edge.x);
}
