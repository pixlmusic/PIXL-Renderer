#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float2> RoofEdgeMask : register(t0);
Texture2D<float4> RunoffState : register(t1);
Texture2D<float> SceneDepth : register(t2);
RWTexture2D<uint> RunoffMask : register(u0);

cbuffer RoofRunoffTuning : register(b13)
{
	float RunoffMaxDistance;
	float RunoffNearSizeDistance;
	float RunoffEmitterSpacing;
	float RunoffTuningPad0;
	float2 RunoffRenderSize;
	float2 RunoffInvRenderSize;
};

float Hash11(float p)
{
	return frac(sin(p * 91.3458f + 17.23f) * 47453.5453f);
}

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

float3 ReconstructCameraRelativePosition(int2 pixel, float depth)
{
	float2 uv = (float2(pixel) + 0.5f) * RunoffInvRenderSize;

	float4 positionCS = float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, depth, 1.0f);
	float4 positionWS = mul(FrameBuffer::CameraViewProjInverse, positionCS);
	return positionWS.xyz / max(abs(positionWS.w), 1e-6f);
}

bool ProjectWorldPoint(float3 cameraRelativePosition, out int2 pixel, out float deviceDepth)
{
	float4 cs = mul(
		FrameBuffer::CameraViewProj,
		float4(cameraRelativePosition, 1.0f));
	if (cs.w <= 1e-5f) {
		pixel = 0;
		deviceDepth = 1.0f;
		return false;
	}

	float3 ndc = cs.xyz / cs.w;
	if (ndc.z < 0.0f || ndc.z > 1.0f) {
		pixel = 0;
		deviceDepth = ndc.z;
		return false;
	}

	float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
	if (FrameBuffer::IsOutsideFrame(uv, false)) {
		pixel = 0;
		deviceDepth = ndc.z;
		return false;
	}

	int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
	pixel = clamp(
		int2(uv * float2(fullSize)),
		int2(0, 0),
		fullSize - 1);
	deviceDepth = ndc.z;
	return true;
}

uint EncodeRunoff(float intensity)
{
	return (uint)(saturate(intensity) * 65535.0f + 0.5f);
}

void WriteMask(int2 pixel, float intensity)
{
	uint width = 0;
	uint height = 0;
	RunoffMask.GetDimensions(width, height);

	if (pixel.x < 0 || pixel.y < 0 ||
		pixel.x >= (int)width || pixel.y >= (int)height)
		return;

	uint original = 0;
	InterlockedMax(RunoffMask[pixel], EncodeRunoff(intensity), original);
}

void WriteDebugMarker(int2 pixel, float stage)
{
	// Debug-only 3x3 marker. Phase 6-8 used 7x7, which exaggerated the apparent
	// scale difference between native and DLSS views and obscured the actual edge.
	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {
			WriteMask(pixel + int2(x, y), stage);
		}
	}
}

void WriteProductionBead(int2 pixel, float intensity, float viewDistance)
{
	// Far/medium: one-pixel head + one faint vertical companion.
	WriteMask(pixel, intensity);
	if (intensity > 0.48f)
		WriteMask(pixel + int2(0, 1), intensity * 0.50f);

	// Close droplets were being visually destroyed by TAA/DLSS because the old
	// footprint stayed one pixel at every physical distance. Add only a very faint
	// horizontal shoulder near the camera. This is still dramatically smaller than
	// the old Phase 4 half-resolution cards.
	float nearWeight =
		1.0f - smoothstep(
			RunoffNearSizeDistance * 0.62f,
			RunoffNearSizeDistance,
			viewDistance);

	if (nearWeight > 0.05f && intensity > 0.36f) {
		float sideIntensity = intensity * nearWeight * 0.30f;
		WriteMask(pixel + int2(-1, 0), sideIntensity);
		WriteMask(pixel + int2( 1, 0), sideIntensity);
		WriteMask(pixel + int2( 0, 2), sideIntensity * 0.58f);
	}
}

float GetSurfaceUpness(int2 pixel, float centreDepth)
{
	int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
	int2 px = min(pixel + int2(1, 0), fullSize - 1);
	int2 py = min(pixel + int2(0, 1), fullSize - 1);

	float dxDepth = LoadSceneDepth(px);
	float dyDepth = LoadSceneDepth(py);
	if (dxDepth >= 0.999999f || dyDepth >= 0.999999f)
		return 0.0f;

	float3 c = ReconstructCameraRelativePosition(pixel, centreDepth);
	float3 x = ReconstructCameraRelativePosition(px, dxDepth);
	float3 y = ReconstructCameraRelativePosition(py, dyDepth);
	float3 n = normalize(cross(x - c, y - c));
	return abs(n.z);
}

// Returns: 0 = offscreen/occluded, 1 = visible drop, 2 = ground-like collision.
int ClassifyDrop(float3 cameraRelativePosition, out int2 pixel)
{
	float dropDepth = 1.0f;
	if (!ProjectWorldPoint(cameraRelativePosition, pixel, dropDepth))
		return 0;

	float sceneDepth = LoadSceneDepth(pixel);
	if (sceneDepth >= 0.999999f)
		return 1;

	float sceneLinear = SharedData::GetScreenDepth(sceneDepth);
	float dropLinear = SharedData::GetScreenDepth(dropDepth);

	// The drop is still in front of visible geometry.
	if (dropLinear <= sceneLinear + 7.0f)
		return 1;

	// It has crossed visible geometry. Only upward/horizontal surfaces create a
	// terminal splash; walls and foreground characters simply occlude the drop.
	float upness = GetSurfaceUpness(pixel, sceneDepth);
	return upness > 0.48f ? 2 : 0;
}

void WriteSmallImpactSplash(int2 pixel, float intensity, float seed)
{
	// Deliberately tiny: one bright contact point and four 1-2px micro-glints.
	WriteMask(pixel, intensity);
	WriteMask(pixel + int2(-1, 0), intensity * 0.48f);
	WriteMask(pixel + int2( 1, 0), intensity * 0.48f);

	int side = seed < 0.5f ? -1 : 1;
	WriteMask(pixel + int2(side, -1), intensity * 0.34f);
	WriteMask(pixel + int2(-side * 2, -1), intensity * 0.22f);
}

void WriteWorldDrop(float3 cameraRelativePosition, float intensity)
{
	int2 pixel = 0;
	int classification = ClassifyDrop(cameraRelativePosition, pixel);
	if (classification == 1)
		WriteMask(pixel, intensity);
}

float3 CanonicalizeTangent(float3 tangent)
{
	tangent.z = 0.0f;
	float len2 = dot(tangent, tangent);
	if (len2 < 1e-6f)
		return float3(1.0f, 0.0f, 0.0f);

	tangent *= rsqrt(len2);

	// Prevent the stable spacing coordinate from flipping when screen-left/right
	// reverses. Pick a deterministic world-space sign.
	if (abs(tangent.x) >= abs(tangent.y)) {
		if (tangent.x < 0.0f)
			tangent = -tangent;
	} else if (tangent.y < 0.0f) {
		tangent = -tangent;
	}
	return tangent;
}

float3 GetEdgeTangent(int2 q, int2 fullSize, int stateWidth)
{
	float3 tangent = float3(1.0f, 0.0f, 0.0f);

	int2 qL = int2(max(q.x - 2, 0), q.y);
	int2 qR = int2(min(q.x + 2, stateWidth - 1), q.y);

	float4 sL = RunoffState.Load(int3(qL, 0));
	float4 sR = RunoffState.Load(int3(qR, 0));

	if (sL.w > 0.01f && sR.w > 0.01f) {
		int2 pL = min(qL * 2 + int2(1, 1), fullSize - 1);
		int2 pR = min(qR * 2 + int2(1, 1), fullSize - 1);
		float3 posL = ReconstructCameraRelativePosition(pL, sL.z);
		float3 posR = ReconstructCameraRelativePosition(pR, sR.z);
		tangent = CanonicalizeTangent(posR - posL);
	}

	return tangent;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint stateWidth = 0;
	uint stateHeight = 0;
	RunoffState.GetDimensions(stateWidth, stateHeight);
	if (dispatchID.x >= stateWidth || dispatchID.y >= stateHeight)
		return;

	int2 q = int2(dispatchID.xy);
	float2 edgeDebug = RoofEdgeMask.Load(int3(q, 0));

	const bool debugMode =
		SharedData::rainResponseSettings.RainRunoffStrength >= 1.49f;
	if (debugMode) {
		if (edgeDebug.x <= 0.001f)
			return;

		int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
		int2 sourcePixel = min(q * 2 + int2(1, 1), fullSize - 1);
		WriteDebugMarker(sourcePixel, edgeDebug.x);
		return;
	}

	float4 state = RunoffState.Load(int3(q, 0));
	if (state.x <= 0.018f || state.w <= 0.018f)
		return;

	float rain = saturate(SharedData::rainResponseSettings.Raining);
	if (rain <= 0.01f)
		return;

	// Neighbour charge simulates beads migrating/merging along the eave.
	float leftCharge = q.x > 0 ?
		RunoffState.Load(int3(q + int2(-1, 0), 0)).x : 0.0f;
	float rightCharge = q.x + 1 < (int)stateWidth ?
		RunoffState.Load(int3(q + int2(1, 0), 0)).x : 0.0f;
	float mergedCharge =
		saturate(state.x + max(leftCharge, rightCharge) * 0.30f);

	int2 fullSize = max(int2(RunoffRenderSize), int2(1, 1));
	int2 sourcePixel = min(q * 2 + int2(1, 1), fullSize - 1);
	float3 sourceCameraRelative =
		ReconstructCameraRelativePosition(sourcePixel, state.z);
	float sourceViewDistance = length(sourceCameraRelative);

	if (sourceViewDistance > RunoffMaxDistance)
		return;

	float3 absoluteSource =
		sourceCameraRelative + FrameBuffer::CameraPosAdjust.xyz;

	// -----------------------------------------------------------------
	// WORLD-SPACE IRREGULAR EAVE SPACING
	// -----------------------------------------------------------------
	// Phase 7 allowed many neighbouring accepted edge pixels to emit at once, which
	// appeared as a mechanical dotted row. Quantise ALONG the reconstructed eave
	// tangent in world space, then choose one jittered position inside each cell.
	float3 edgeTangent = GetEdgeTangent(q, fullSize, (int)stateWidth);
	float spacing = max(RunoffEmitterSpacing, 56.0f);
	float along = dot(absoluteSource, edgeTangent) / spacing;
	float bucket = floor(along);
	float local = frac(along);

	// Primary emitter exists at every weather intensity and NEVER moves when rain
	// strength changes. Heavy rain adds a second independently-jittered emitter
	// inside the SAME world cell rather than shrinking spacing and sliding all
	// existing droplets along the eave.
	float primaryTarget =
		lerp(0.14f, 0.46f, Hash11(bucket * 13.17f + 7.31f));
	float secondaryTarget =
		lerp(0.56f, 0.88f, Hash11(bucket * 29.63f + 19.41f));

	float primaryDistance = abs(local - primaryTarget);
	primaryDistance = min(primaryDistance, 1.0f - primaryDistance);

	float secondaryDistance = abs(local - secondaryTarget);
	secondaryDistance = min(secondaryDistance, 1.0f - secondaryDistance);

	float chargeResponse =
		smoothstep(0.025f, 0.38f, mergedCharge);

	// Light rain remains intermittent. Medium/heavy rain progressively fills more
	// stable world-space buckets.
	float primaryProbability =
		lerp(0.34f, 0.96f, pow(rain, 0.72f)) *
		chargeResponse;
	float primarySelector =
		Hash11(bucket * 41.73f + state.y * 9.17f);

	float primaryAperture =
		lerp(0.070f, 0.135f, rain);

	bool usePrimary =
		primaryDistance <= primaryAperture &&
		primarySelector <= primaryProbability;

	// Secondary bead population fades in only once rain becomes genuinely wet.
	// This is what turns a sparse drizzle into a busy storm without creating a
	// mechanical dotted row.
	float secondaryRain =
		smoothstep(0.38f, 0.88f, rain);
	float secondaryProbability =
		secondaryRain *
		lerp(0.30f, 0.88f, rain) *
		smoothstep(0.08f, 0.52f, mergedCharge);
	float secondarySelector =
		Hash11(bucket * 67.19f + state.y * 21.53f + 3.7f);
	float secondaryAperture =
		lerp(0.060f, 0.112f, rain);

	bool useSecondary =
		secondaryDistance <= secondaryAperture &&
		secondarySelector <= secondaryProbability;

	if (!usePrimary && !useSecondary)
		return;

	// Whichever emitter this pixel represents gets a distinct, stable timing seed.
	float emitterIndex = usePrimary ? 0.0f : 1.0f;
	float worldSelector =
		Hash11(
			bucket * 73.91f +
			emitterIndex * 31.17f +
			dot(
				floor(absoluteSource / 96.0f),
				float3(0.17f, 0.31f, 0.47f)));

	// Heavy rain cycles substantially faster. This increases release frequency
	// independently of spatial density.
	float cycleRate =
		lerp(0.18f, 1.08f, pow(rain, 0.82f)) *
		lerp(0.80f, 1.22f, worldSelector);
	float phase = frac(
		SharedData::rainResponseSettings.Time * cycleRate +
		state.y * 19.31f +
		emitterIndex * 0.43f);

	// In light rain beads hang/grow for longer. Heavy rain releases sooner because
	// the roof reservoir is replenished much faster.
	float releaseStart =
		lerp(0.36f, 0.20f, rain);
	float beadGrowth =
		saturate(phase / max(releaseStart, 0.01f)) *
		smoothstep(0.08f, 0.46f, mergedCharge);

	if (phase < releaseStart) {
		// The hanging lip bead is anchored directly to the CURRENT detected eave pixel.
		// This is not a screen-space falling effect: once released, the drop below is
		// still a true projected 3D world trajectory. Keeping the source bead direct
		// also guarantees a visible proof that the roof detector is alive.
		WriteProductionBead(
			sourcePixel,
			lerp(0.30f, 0.82f, beadGrowth),
			sourceViewDistance);
		return;
	}

	float fallT = saturate((phase - releaseStart) / (1.0f - releaseStart));
	float fallAcceleration = fallT * fallT;

	// Larger accumulated beads fall farther/faster but remain ~1 screen pixel wide.
	float maximumFall =
		lerp(240.0f, 980.0f, mergedCharge) *
		lerp(0.86f, 1.22f, rain);
	float fallDistance = fallAcceleration * maximumFall;

	// A few world units of stable lateral variation keeps parallel drops organic
	// without making them screen-space or wind-blown billboards.
	float2 tinyLateral =
		float2(worldSelector - 0.5f, Hash11(worldSelector * 31.7f) - 0.5f) *
		lerp(1.5f, 5.0f, rain);

	float3 headPosition =
		sourceCameraRelative +
		float3(tinyLateral, -fallDistance);

	int2 headPixel = 0;
	int headClass = ClassifyDrop(headPosition, headPixel);

	if (headClass == 2) {
		WriteSmallImpactSplash(
			headPixel,
			lerp(0.48f, 0.88f, mergedCharge) * rain,
			worldSelector);
		return;
	}
	if (headClass == 0)
		return;

	WriteProductionBead(
		headPixel,
		lerp(0.50f, 0.98f, mergedCharge),
		length(headPosition));

	// Heavy storms can briefly connect the bead into a short filament. This is
	// still world-space: every tail point is a separate 3D position projected by
	// the current camera. Only three segments are used to keep cost bounded.
	float streaminess =
		smoothstep(0.58f, 0.92f, rain) *
		smoothstep(0.48f, 0.86f, mergedCharge);

	[unroll]
	for (int i = 1; i <= 3; ++i) {
		float trailOffset =
			lerp(16.0f, 32.0f, worldSelector) * i;
		float trailDistance = max(0.0f, fallDistance - trailOffset);
		float3 trailPosition =
			sourceCameraRelative +
			float3(tinyLateral * (1.0f - i * 0.08f), -trailDistance);

		float trailIntensity =
			streaminess *
			lerp(0.34f, 0.15f, (float)(i - 1) / 2.0f);
		if (trailIntensity > 0.02f)
			WriteWorldDrop(trailPosition, trailIntensity);
	}
}
