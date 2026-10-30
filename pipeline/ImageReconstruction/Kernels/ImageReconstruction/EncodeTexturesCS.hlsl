#include "Common/SharedData.hlsli"

cbuffer ImageReconstructionData : register(b0)
{
	float2 TrueSamplingDim;
	float CloudMaskAvailable;
	float pad0;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);
// Deferred material masks: x is tissue amount, y is the profile selector on
// tissue and PIXL reconstruction activity when x is zero, z is ambient luma.
Texture2D<float3> MaterialMasks : register(t4);
Texture2D<float4> CloudReconstructionMask : register(t5);

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
	float3 materialMask = MaterialMasks[dispatchID.xy];
	float reconstructionActivity =
		materialMask.x <= 1.0e-5f ? saturate(materialMask.y) : 0.0f;
	float neighborhoodActivity = reconstructionActivity;

	// One-pixel coverage expansion catches the immediate disocclusion fringe of
	// a tessellated footprint without making broad, static terrain reactive.
	[unroll] for (int activityY = -1; activityY <= 1; activityY++)
	{
		[unroll] for (int activityX = -1; activityX <= 1; activityX++)
		{
			int2 activityPos = int2(dispatchID.xy) + int2(activityX, activityY);
			if (all(activityPos >= 0) && all(activityPos < int2(TrueSamplingDim)))
				neighborhoodActivity = max(
					neighborhoodActivity,
					(MaterialMasks[activityPos].x <= 1.0e-5f
						? saturate(MaterialMasks[activityPos].y)
						: 0.0f));
		}
	}

	// Current-frame weighting for changing displaced terrain.  This applies to
	// both DLSS/DLAA and FSR; only motion-vector conditioning below is DLSS-only.
	float groundResponseReactive = max(
		reconstructionActivity * 0.82f,
		neighborhoodActivity * 0.42f);
	reactiveMask = max(reactiveMask, groundResponseReactive);

	// Sky and geometry use different motion fields: sky vectors contain camera
	// rotation only, while the object vector includes parallax. Mark both sides of
	// their moving silhouette reactive so the upscaler sheds stale history at the
	// edge. This must work even when clouds (and their optional mask) are disabled.
	float centerDepth = DepthMask[dispatchID.xy];
	bool silhouetteCenterHasGeometry = isfinite(centerDepth) && centerDepth >= 0.0f && centerDepth < 1.0f;
	bool centerIsSky = !silhouetteCenterHasGeometry;
	float cloudOpacity = 0.0f;
	if (centerIsSky && CloudMaskAvailable > 0.5f) {
		uint cloudWidth, cloudHeight;
		CloudReconstructionMask.GetDimensions(cloudWidth, cloudHeight);
		if (cloudWidth > 0u && cloudHeight > 0u) {
			uint2 cloudPixel = min((uint2)(((float2(dispatchID.xy) + 0.5f) / TrueSamplingDim) *
				float2(cloudWidth, cloudHeight)), uint2(cloudWidth - 1u, cloudHeight - 1u));
			float transmittance = CloudReconstructionMask.Load(int3(cloudPixel, 0)).a;
			cloudOpacity = isfinite(transmittance) ? saturate(1.0f - transmittance) : 0.0f;
			// Cloud weather advection is independent of the sky's camera vector.
			reactiveMask = max(reactiveMask, cloudOpacity * 0.38f);
		}
	}

	float2 centerMotion = MotionVectorMask[dispatchID.xy];
	bool silhouetteMotionValid = all(isfinite(centerMotion));
	float edgeCenterLinearDepth = silhouetteCenterHasGeometry
		? SharedData::GetScreenDepth(centerDepth)
		: 0.0f;
	bool edgeCenterLinearDepthValid = isfinite(edgeCenterLinearDepth) && edgeCenterLinearDepth > 0.0f;
	if (centerIsSky && silhouetteMotionValid) {
		// A newly exposed sky pixel may no longer touch the current object edge,
		// while its reprojected history still contains the old silhouette. On a
		// fast camera pan, bias the whole sky toward current color so those
		// disoccluded shapes clear promptly instead of lingering for many frames.
		float skyMotionPixels = length(centerMotion * TrueSamplingDim);
		float skyPanReactivity = smoothstep(0.08f, 0.75f, skyMotionPixels) * 0.98f;
		reactiveMask = max(reactiveMask, skyPanReactivity);
	}
	float silhouetteMotionPixels = 0.0f;
	float movingGeometryEdge = 0.0f;
	bool touchesOppositeLayer = false;
	[unroll] for (int axis = 0; axis < 2; ++axis) {
		[unroll] for (int side = -1; side <= 1; side += 2) {
			int2 offset = axis == 0 ? int2(side, 0) : int2(0, side);
			int2 samplePos = int2(dispatchID.xy) + offset;
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;
			float neighborDepth = DepthMask[samplePos];
			bool neighborHasGeometry = isfinite(neighborDepth) && neighborDepth >= 0.0f && neighborDepth < 1.0f;
		float2 neighborMotion = MotionVectorMask[samplePos];
		if (neighborHasGeometry == silhouetteCenterHasGeometry) {
			// Reprojected history can still stick at indoor object/wall edges where
			// both samples have valid geometry. Mark both sides when depth separation
			// and their screen-space motion disagree, not just sky silhouettes.
			if (neighborHasGeometry && edgeCenterLinearDepthValid && silhouetteMotionValid && all(isfinite(neighborMotion))) {
				float neighborLinearDepth = SharedData::GetScreenDepth(neighborDepth);
				if (isfinite(neighborLinearDepth) && neighborLinearDepth > 0.0f) {
					float relativeDepthDelta = abs(neighborLinearDepth - edgeCenterLinearDepth) /
						max(min(neighborLinearDepth, edgeCenterLinearDepth), 128.0f);
					// Indoor camera translations often move a few tenths of a pixel per
					// frame. The previous thresholds ignored those subpixel edge motions,
					// letting DLSS retain a dark object-shaped history imprint for seconds.
					float depthDiscontinuity = smoothstep(0.003f, 0.035f, relativeDepthDelta);
					float motionDifferencePixels = length((neighborMotion - centerMotion) * TrueSamplingDim);
					float motionDiscontinuity = smoothstep(0.01f, 0.20f, motionDifferencePixels);
					movingGeometryEdge = max(movingGeometryEdge, depthDiscontinuity * motionDiscontinuity);
				}
			}
				continue;
		}
			touchesOppositeLayer = true;
			if (silhouetteMotionValid && all(isfinite(neighborMotion)))
				silhouetteMotionPixels = max(silhouetteMotionPixels,
					length((neighborMotion - centerMotion) * TrueSamplingDim));
		}
	}
	reactiveMask = max(reactiveMask, movingGeometryEdge * 0.95f);
	if (touchesOppositeLayer) {
		// A small threshold catches subpixel pans; the cap avoids forcing every
		// static skyline edge to reject all history.
		float edgeReactivity = smoothstep(0.20f, 1.50f, silhouetteMotionPixels) * 0.88f;
		reactiveMask = max(reactiveMask, edgeReactivity);
		if (centerIsSky && CloudMaskAvailable > 0.5f)
			reactiveMask = max(reactiveMask, cloudOpacity * 0.62f);
	}

#if defined(DLSS)
	float depth = DepthMask[dispatchID.xy];
	float2 motionVector = MotionVectorMask[dispatchID.xy];
	bool centerMotionValid = all(isfinite(motionVector));
	if (!centerMotionValid) {
		motionVector = 0.0f;
		reactiveMask = 1.0f; // Reject corrupted history, not ordinary static pixels.
	}
	float2 dilatedMotionVector = motionVector;

	// Skyrim's standard depth is 0 at the near plane and 1 at clear/far sky.
	// The old path compared non-linear device depth, selected the longest vector,
	// and then trusted that result *more* with distance.  Tiny depth differences
	// across distant terrain could consequently import unrelated motion from a
	// five-pixel neighborhood.  NR then interpreted the unstable guide as world
	// motion and changed reconstructed surface detail while the camera moved.
	// D3D's clear depth is 1.0. A tolerance here treats distant geometry as sky
	// and can propagate foreground motion onto its silhouette.
	const bool centerHasGeometry = isfinite(depth) && depth >= 0.0f && depth < 1.0f;
	const float centerLinearDepth = centerHasGeometry ? SharedData::GetScreenDepth(depth) : 1e20;
	float closestDeviceDepth = centerHasGeometry ? depth : 1.0f;
	bool foundCandidate = false;

	// Clear-depth sky already has a valid rotation-only vector from the deferred
	// composite. Dilating a foreground object's vector into those pixels makes
	// the sky reproject from the object's previous footprint, producing a clear
	// silhouette trail. Dilate only between geometry samples.
	if (centerHasGeometry) {
		[unroll] for (int y = -2; y <= 2; y++)
		{
			[unroll] for (int x = -2; x <= 2; x++)
			{
				int2 samplePos = int2(dispatchID.xy) + int2(x, y);

				// Bounds check
				if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
					continue;

				float neighborDepth = DepthMask[samplePos];
			const bool neighborHasGeometry = isfinite(neighborDepth) && neighborDepth >= 0.0f && neighborDepth < 1.0f;
				if (!neighborHasGeometry)
					continue;

				// Device depth is monotonic, so select the closest candidate first and
				// linearize only that sample after the loop. This avoids 25 divisions per
				// pixel in a full-screen pass while retaining world-space validation.
				float2 neighborMotion = MotionVectorMask[samplePos].xy;
				if (neighborDepth < closestDeviceDepth && all(isfinite(neighborMotion))) {
					closestDeviceDepth = neighborDepth;
					dilatedMotionVector = neighborMotion;
					foundCandidate = true;
				}
			}
		}
	}

	float dilationWeight = 0.0;
	if (foundCandidate) {
		float closestLinearDepth = SharedData::GetScreenDepth(closestDeviceDepth);
		// Require a meaningful world-space separation on geometry. The relative
		// term scales to Skyrim's large exterior ranges; the absolute term keeps
		// near-field thin geometry eligible without reacting to depth quantization.
		float minimumSeparation = max(8.0, min(centerLinearDepth, 100000.0) * 0.004);
		float depthSeparation = max(centerLinearDepth - closestLinearDepth, 0.0);
		dilationWeight = smoothstep(minimumSeparation, minimumSeparation * 3.0, depthSeparation);
	}

	// The displaced surface already owns a physically matched previous position.
	// Never replace that vector with a neighbor selected from the steep track wall.
	dilationWeight *= 1.0f - reconstructionActivity;
	// A blended foreground/background velocity corresponds to neither surface
	// and reprojects history between them. Select a real representative; keep
	// the soft weight solely for the bounded bias-current-colour hint.
	float2 conditionedMotion = dilationWeight > 0.5f ? dilatedMotionVector : motionVector;
	MotionVectorOutput[dispatchID.xy] = conditionedMotion;
	float2 currentUV = (float2(dispatchID.xy) + 0.5f) / TrueSamplingDim;
	float2 previousUV = currentUV + motionVector;
	if (centerHasGeometry && (!all(isfinite(previousUV)) || any(previousUV < 0.0f) || any(previousUV >= 1.0f)))
		reactiveMask = 1.0f;

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
