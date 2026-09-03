#include "Common/SharedData.hlsli"

cbuffer ImageReconstructionData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float2> MotionVectorOutput : register(u2);
#if defined(DEPTH_OUTPUT)
RWTexture2D<float> DepthOutput : register(u3);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Bounds check
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float2 taaMask = TAAMask[dispatchID.xy];
	// Water's stencil pass stores its coverage in the blue channel of Skyrim's
	// normal/TAA/SSR target.  Opaque pixels are cleared to zero by PIXL's
	// deferred composite, so this is a transparency hint rather than glossiness
	// at the point where reconstruction runs.
	float transparencyCompositionMask = saturate(NormalsWaterMask[dispatchID.xy].z);
	float reactiveMask = saturate(taaMask.x * 0.1 + taaMask.y);

#if defined(DLSS)
	float depth = DepthMask[dispatchID.xy];
	float2 motionVector = MotionVectorMask[dispatchID.xy];
	float2 dilatedMotionVector = motionVector;

	// Skyrim's standard depth is 0 at the near plane and 1 at clear/far sky.
	// The old path compared non-linear device depth, selected the longest vector,
	// and then trusted that result *more* with distance.  Tiny depth differences
	// across distant terrain could consequently import unrelated motion from a
	// five-pixel neighborhood.  NR then interpreted the unstable guide as world
	// motion and changed reconstructed surface detail while the camera moved.
	const bool centerHasGeometry = depth > 1e-6 && depth < 0.999999;
	const float centerLinearDepth = centerHasGeometry ? SharedData::GetScreenDepth(depth) : 1e20;
	float closestDeviceDepth = centerHasGeometry ? depth : 1.0;
	bool foundCandidate = false;

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Bounds check
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			float neighborDepth = DepthMask[samplePos];
			const bool neighborHasGeometry = neighborDepth > 1e-6 && neighborDepth < 0.999999;
			if (!neighborHasGeometry)
				continue;

			// Device depth is monotonic, so select the closest candidate first and
			// linearize only that sample after the loop.  This avoids 25 divisions per
			// pixel in a full-screen pass while retaining world-space validation.
			if (neighborDepth < closestDeviceDepth) {
				closestDeviceDepth = neighborDepth;
				dilatedMotionVector = MotionVectorMask[samplePos];
				foundCandidate = true;
			}
		}
	}

	float dilationWeight = 0.0;
	if (foundCandidate) {
		if (!centerHasGeometry) {
			dilationWeight = 1.0;
		} else {
			float closestLinearDepth = SharedData::GetScreenDepth(closestDeviceDepth);
			// Require a meaningful world-space separation on geometry.  The relative
			// term scales to Skyrim's large exterior ranges; the absolute term keeps
			// near-field thin geometry eligible without reacting to depth quantization.
			float minimumSeparation = max(8.0, min(centerLinearDepth, 100000.0) * 0.004);
			float depthSeparation = max(centerLinearDepth - closestLinearDepth, 0.0);
			dilationWeight = smoothstep(minimumSeparation, minimumSeparation * 3.0, depthSeparation);
		}
	}

	float2 conditionedMotion = lerp(motionVector, dilatedMotionVector, dilationWeight);
	MotionVectorOutput[dispatchID.xy] = conditionedMotion;

	// Bias reconstruction toward the current sample only where the silhouette
	// actually has different motion.  Static depth edges retain full temporal
	// detail, while moving actors, foliage and displaced surfaces reject stale
	// background history without turning all terrain edges reactive.
	float2 motionDeltaPixels = (dilatedMotionVector - motionVector) * TrueSamplingDim;
	float motionDisagreement = saturate((length(motionDeltaPixels) - 0.25) / 1.5);
	reactiveMask = max(reactiveMask, dilationWeight * motionDisagreement * 0.35);
#endif

#if defined(DEPTH_OUTPUT)
	// Copy depth as R32_FLOAT so FSR DX11 backend receives a typed format.
	// The raw depth resource is R24G8_TYPELESS which maps to FFX_SURFACE_FORMAT_UNKNOWN.
	DepthOutput[dispatchID.xy] = DepthMask[dispatchID.xy];
#endif

	ReactiveMask[dispatchID.xy] = saturate(reactiveMask);

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
}
