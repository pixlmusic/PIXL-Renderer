#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture2D<float> PrecipitationDepth : register(t1);
RWTexture2D<float2> RoofEdgeMask : register(u0);

cbuffer RoofRunoffTuning : register(b13)
{
	float RunoffMaxDistance;
	float RunoffNearSizeDistance;
	float RunoffEmitterSpacing;
	float RunoffTuningPad0;
	float2 RunoffRenderSize;
	float2 RunoffInvRenderSize;
};

int2 GetSceneDepthCoord(int2 logicalPixel)
{
	uint texWidth = 0;
	uint texHeight = 0;
	SceneDepth.GetDimensions(texWidth, texHeight);

	float2 activeSize = min(float2(texWidth, texHeight), max(RunoffRenderSize, 1.0f.xx));
	int2 coord = logicalPixel;
	return clamp(
		coord,
		int2(0, 0),
		max(int2(activeSize) - 1, int2(0, 0)));
}

float LoadSceneDepth(int2 logicalPixel)
{
	return SceneDepth.Load(int3(GetSceneDepthCoord(logicalPixel), 0));
}

float3 SafeNormalize(float3 v)
{
	return v * rsqrt(max(dot(v, v), 1e-8f));
}

float3 ReconstructCameraRelativePosition(int2 pixel, float depth)
{
	float2 uv = (float2(pixel) + 0.5f) * RunoffInvRenderSize;

	float4 positionCS = float4(
		2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f,
		depth,
		1.0f);
	float4 positionWS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
	return positionWS.xyz / max(abs(positionWS.w), 1e-6f);
}

float GetPrecipitationBlockerConfidence(float3 cameraRelativePosition)
{
	uint occWidth = 0;
	uint occHeight = 0;
	PrecipitationDepth.GetDimensions(occWidth, occHeight);
	if (occWidth == 0 || occHeight == 0)
		return 0.0f;

	float4 occ = mul(
		SharedData::rainResponseSettings.OcclusionViewProj,
		float4(cameraRelativePosition, 1.0f));
	occ.xyz /= max(abs(occ.w), 1e-6f);
	occ.y = -occ.y;

	float2 uv = occ.xy * 0.5f + 0.5f;
	if (any(uv < 0.0f) || any(uv > 1.0f))
		return 0.0f;

	int2 coord = clamp(
		int2(uv * float2(occWidth, occHeight)),
		int2(0, 0),
		int2((int)occWidth - 1, (int)occHeight - 1));

	float blockerDepth = PrecipitationDepth.Load(int3(coord, 0));
	float visibility = blockerDepth - occ.z;

	// Multi-sample coherence below makes this safe to keep fairly tolerant.
	return 1.0f - smoothstep(0.018f, 0.155f, abs(visibility));
}

float GetSilhouetteScore(float centreLinear, float sampleDepth)
{
	if (sampleDepth >= 0.999999f)
		return 1.0f;

	float sampleLinear = SharedData::GetScreenDepth(sampleDepth);
	return smoothstep(22.0f, 150.0f, sampleLinear - centreLinear);
}

float GetDepthContinuity(float centreLinear, float sampleDepth)
{
	if (sampleDepth >= 0.999999f)
		return 0.0f;

	float sampleLinear = SharedData::GetScreenDepth(sampleDepth);
	float tolerance = max(72.0f, centreLinear * 0.045f);
	float delta = abs(sampleLinear - centreLinear);
	return 1.0f - smoothstep(tolerance * 0.34f, tolerance, delta);
}

float GetClearanceScore(int2 p, float centreLinear, int2 fullSize)
{
	int2 p4  = min(p + int2(0, 4),  fullSize - 1);
	int2 p8  = min(p + int2(0, 8),  fullSize - 1);
	int2 p14 = min(p + int2(0, 14), fullSize - 1);
	int2 p22 = min(p + int2(0, 22), fullSize - 1);

	float s4  = GetSilhouetteScore(centreLinear, LoadSceneDepth(p4));
	float s8  = GetSilhouetteScore(centreLinear, LoadSceneDepth(p8));
	float s14 = GetSilhouetteScore(centreLinear, LoadSceneDepth(p14));
	float s22 = GetSilhouetteScore(centreLinear, LoadSceneDepth(p22));

	float sustained = (s4 + s8 + s14 + s22) * 0.25f;
	float deep = max(s14, s22);

	return
		smoothstep(0.20f, 0.56f, sustained) *
		smoothstep(0.18f, 0.48f, deep);
}

float FindEaveNeighbour(
	int2 centrePixel,
	float centreLinear,
	int xOffset,
	int2 fullSize,
	out int2 bestPixel,
	out float bestDepth)
{
	float bestScore = 0.0f;
	bestPixel = centrePixel;
	bestDepth = LoadSceneDepth(centrePixel);

	// Search a narrow vertical band because a real eave can slope several pixels
	// across the screen. This avoids assuming that every roof edge is horizontal.
	[unroll]
	for (int i = 0; i < 5; ++i) {
		// Scale the diagonal search with the along-eave sample distance. Close
		// geometry needs a wider pixel span to cover the same physical roof patch.
		int dyStep = clamp(
			(int)round((float)abs(xOffset) * 0.125f),
			2,
			12);
		int dy = (i - 2) * dyStep;
		int2 q = clamp(
			centrePixel + int2(xOffset, dy),
			int2(0, 0),
			fullSize - 1);

		float d = LoadSceneDepth(q);
		float continuity = GetDepthContinuity(centreLinear, d);
		if (continuity <= 0.001f)
			continue;

		float qLinear = SharedData::GetScreenDepth(d);
		float clearance = GetClearanceScore(q, qLinear, fullSize);

		// The same-depth eave line and open space below must BOTH continue.
		float score = continuity * clearance;
		if (score > bestScore) {
			bestScore = score;
			bestPixel = q;
			bestDepth = d;
		}
	}

	return bestScore;
}

float PlaneResidual(float3 p, float3 origin, float3 normal)
{
	return abs(dot(p - origin, normal));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint edgeWidth = 0;
	uint edgeHeight = 0;
	RoofEdgeMask.GetDimensions(edgeWidth, edgeHeight);
	if (dispatchID.x >= edgeWidth || dispatchID.y >= edgeHeight)
		return;

	if (SharedData::rainResponseSettings.EnableRainParticleEnhancement == 0 ||
		SharedData::rainResponseSettings.RainRunoffStrength <= 0.001f ||
		SharedData::rainResponseSettings.Raining <= 0.01f) {
		RoofEdgeMask[dispatchID.xy] = 0.0f.xx;
		return;
	}

	const bool debugMode =
		SharedData::rainResponseSettings.RainRunoffStrength >= 1.49f;

	int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
	int2 p = min(int2(dispatchID.xy) * 2 + int2(1, 1), fullSize - 1);

	float depth = LoadSceneDepth(p);
	if (depth >= 0.999999f) {
		RoofEdgeMask[dispatchID.xy] = 0.0f.xx;
		return;
	}

	float centreLinear = SharedData::GetScreenDepth(depth);
	float3 posC = ReconstructCameraRelativePosition(p, depth);
	float sourceDistance = length(posC);

	// Choose screen offsets from a world-space footprint. Fixed 8/16-pixel tests
	// collapse to only a few world units near the camera and reject otherwise valid
	// eaves through sizeGate. At distance these naturally converge to the original
	// small offsets, preserving cost and the established far-field classifier.
	int2 footprintPixel = min(p + int2(1, 0), fullSize - 1);
	float3 footprintPosition =
		ReconstructCameraRelativePosition(footprintPixel, depth);
	float worldPerPixel = max(length(footprintPosition - posC), 0.04f);
	float targetAlongWorld = lerp(
		18.0f,
		34.0f,
		smoothstep(240.0f, 1500.0f, sourceDistance));
	int nearLineOffset = clamp(
		(int)round(targetAlongWorld / worldPerPixel),
		6,
		96);
	int farLineOffset = clamp(nearLineOffset * 2, 12, 160);

	// ---------------------------------------------------------------------
	// 1. SUSTAINED OPEN SPACE BELOW THE CANDIDATE
	// ---------------------------------------------------------------------
	// This immediately rejects most solid walls/floors. Grass/actors can still
	// produce this stage, so it is diagnostic only, never sufficient for runoff.
	float clearanceGate = GetClearanceScore(p, centreLinear, fullSize);
	if (clearanceGate <= 0.025f) {
		RoofEdgeMask[dispatchID.xy] = 0.0f.xx;
		return;
	}

	// ---------------------------------------------------------------------
	// 2. LONG COHERENT EAVE LINE
	// ---------------------------------------------------------------------
	// This is the key Phase 9 change. A roof lip is not merely "a silhouette";
	// it is a long same-depth edge with open space underneath it. Search both
	// directions, including a small Y range so diagonal/gabled eaves still pass.
	int2 leftNearPixel, rightNearPixel;
	int2 leftFarPixel, rightFarPixel;
	float leftNearDepth, rightNearDepth;
	float leftFarDepth, rightFarDepth;

	float leftNear = FindEaveNeighbour(
		p, centreLinear, -nearLineOffset, fullSize, leftNearPixel, leftNearDepth);
	float rightNear = FindEaveNeighbour(
		p, centreLinear, nearLineOffset, fullSize, rightNearPixel, rightNearDepth);
	float leftFar = FindEaveNeighbour(
		p, centreLinear, -farLineOffset, fullSize, leftFarPixel, leftFarDepth);
	float rightFar = FindEaveNeighbour(
		p, centreLinear, farLineOffset, fullSize, rightFarPixel, rightFarDepth);

	float nearLine = min(leftNear, rightNear);
	float farLine = min(leftFar, rightFar);

	float lineGate =
		smoothstep(0.14f, 0.42f, nearLine) *
		lerp(0.62f, 1.0f, smoothstep(0.08f, 0.34f, farLine));

	if (lineGate <= 0.02f) {
		RoofEdgeMask[dispatchID.xy] =
			debugMode ? float2(0.18f, depth) : 0.0f.xx;
		return;
	}

	// ---------------------------------------------------------------------
	// 3. ADAPTIVE ROOF-PLANE DIRECTION
	// ---------------------------------------------------------------------
	// Stop assuming "screen-up" is inside the roof. Build the eave tangent from
	// coherent neighbours, then test BOTH perpendicular directions and choose the
	// one that remains on the same depth surface.
	float2 line2D = float2(
		(float)(rightNearPixel.x - leftNearPixel.x),
		(float)(rightNearPixel.y - leftNearPixel.y));
	line2D /= max(length(line2D), 1e-4f);

	float2 perp2D = float2(-line2D.y, line2D.x);
	int planeNearOffset = clamp(
		(int)round(10.0f / worldPerPixel),
		4,
		64);
	int planeFarOffset = clamp(planeNearOffset * 2, 8, 112);
	int2 perp6 = int2(round(perp2D * (float)planeNearOffset));
	int2 perp12 = int2(round(perp2D * (float)planeFarOffset));

	int2 insideA6 = clamp(p + perp6, int2(0, 0), fullSize - 1);
	int2 insideB6 = clamp(p - perp6, int2(0, 0), fullSize - 1);
	int2 insideA12 = clamp(p + perp12, int2(0, 0), fullSize - 1);
	int2 insideB12 = clamp(p - perp12, int2(0, 0), fullSize - 1);

	float depthA6 = LoadSceneDepth(insideA6);
	float depthB6 = LoadSceneDepth(insideB6);
	float depthA12 = LoadSceneDepth(insideA12);
	float depthB12 = LoadSceneDepth(insideB12);

	float scoreA =
		GetDepthContinuity(centreLinear, depthA6) *
		GetDepthContinuity(centreLinear, depthA12);
	float scoreB =
		GetDepthContinuity(centreLinear, depthB6) *
		GetDepthContinuity(centreLinear, depthB12);

	bool useA = scoreA >= scoreB;
	float insideScore = max(scoreA, scoreB);

	if (insideScore <= 0.035f) {
		RoofEdgeMask[dispatchID.xy] =
			debugMode ? float2(0.36f, depth) : 0.0f.xx;
		return;
	}

	int2 inside6 = useA ? insideA6 : insideB6;
	int2 inside12 = useA ? insideA12 : insideB12;
	float insideDepth6 = useA ? depthA6 : depthB6;
	float insideDepth12 = useA ? depthA12 : depthB12;

	float3 posL = ReconstructCameraRelativePosition(leftNearPixel, leftNearDepth);
	float3 posR = ReconstructCameraRelativePosition(rightNearPixel, rightNearDepth);
	float3 posI6 = ReconstructCameraRelativePosition(inside6, insideDepth6);
	float3 posI12 = ReconstructCameraRelativePosition(inside12, insideDepth12);

	float3 tangentWS = posR - posL;
	float3 inwardWS = posI6 - posC;
	float3 roofNormal = SafeNormalize(cross(tangentWS, inwardWS));

	// Allow steep Skyrim roofs. Vertical walls approach zero world-Z upness.
	float roofOrientation =
		smoothstep(0.07f, 0.28f, abs(roofNormal.z));

	float patchScale = max(length(tangentWS), length(posI12 - posC));
	float planeResidual = PlaneResidual(posI12, posC, roofNormal);
	float planarity =
		1.0f - smoothstep(
			max(3.5f, patchScale * 0.045f),
			max(15.0f, patchScale * 0.16f),
			planeResidual);

	float worldLineWidth = length(posR - posL);
	float worldInwardDepth = length(posI12 - posC);
	float nearGeometry =
		1.0f - smoothstep(220.0f, 900.0f, sourceDistance);
	float minimumLineWidth = lerp(16.0f, 7.0f, nearGeometry);
	float minimumInwardDepth = lerp(6.0f, 3.0f, nearGeometry);
	float sizeGate =
		smoothstep(minimumLineWidth, max(44.0f, minimumLineWidth + 18.0f), worldLineWidth) *
		smoothstep(minimumInwardDepth, max(20.0f, minimumInwardDepth + 9.0f), worldInwardDepth);

	float planeGate =
		roofOrientation *
		planarity *
		sizeGate *
		smoothstep(0.06f, 0.30f, insideScore);

	if (planeGate <= 0.02f) {
		RoofEdgeMask[dispatchID.xy] =
			debugMode ? float2(0.58f, depth) : 0.0f.xx;
		return;
	}

	// ---------------------------------------------------------------------
	// 4. COHERENT PRECIPITATION BLOCKER
	// ---------------------------------------------------------------------
	// Require several independent samples from the eave and the roof plane.
	float pc0 = GetPrecipitationBlockerConfidence(posC);
	float pc1 = GetPrecipitationBlockerConfidence(posL);
	float pc2 = GetPrecipitationBlockerConfidence(posR);
	float pc3 = GetPrecipitationBlockerConfidence(posI6);
	float pc4 = GetPrecipitationBlockerConfidence(posI12);

	float precipAverage = (pc0 + pc1 + pc2 + pc3 + pc4) * 0.2f;
	float precipCoverage =
		(step(0.065f, pc0) +
		 step(0.065f, pc1) +
		 step(0.065f, pc2) +
		 step(0.065f, pc3) +
		 step(0.065f, pc4)) * 0.2f;

	float precipitationRoof =
		smoothstep(0.22f, 0.52f, precipCoverage) *
		smoothstep(0.025f, 0.15f, precipAverage);

	if (precipitationRoof <= 0.018f) {
		RoofEdgeMask[dispatchID.xy] =
			debugMode ? float2(0.78f, depth) : 0.0f.xx;
		return;
	}

	// ---------------------------------------------------------------------
	// 5. USER DISTANCE
	// ---------------------------------------------------------------------
	float maxDistance = max(RunoffMaxDistance, 600.0f);
	float distanceGate =
		1.0f - smoothstep(
			maxDistance * 0.88f,
			maxDistance,
			sourceDistance);

	if (distanceGate <= 0.001f) {
		RoofEdgeMask[dispatchID.xy] = 0.0f.xx;
		return;
	}

	float roofIdentity =
		clearanceGate *
		lineGate *
		planeGate *
		precipitationRoof;

	float edge =
		saturate(roofIdentity * distanceGate);

	if (debugMode) {
		// Phase 9 diagnostic:
		// RED    = open silhouette only
		// ORANGE = long coherent eave line
		// YELLOW = adaptive roof plane
		// CYAN   = coherent precipitation blocker
		// BLUE   = strong complete roof identity
		// GREEN  = final accepted runoff emitter
		float stage = 0.18f;

		if (lineGate > 0.12f)
			stage = 0.36f;
		if (stage >= 0.36f && planeGate > 0.10f)
			stage = 0.58f;
		if (stage >= 0.58f && precipitationRoof > 0.10f)
			stage = 0.78f;
		if (stage >= 0.78f && roofIdentity > 0.10f)
			stage = 0.92f;
		if (edge > 0.035f)
			stage = 1.00f;

		RoofEdgeMask[dispatchID.xy] = float2(stage, depth);
		return;
	}

	RoofEdgeMask[dispatchID.xy] = float2(edge, depth);
}
