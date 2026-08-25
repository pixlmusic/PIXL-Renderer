#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

cbuffer PerFrameCB : register(b0)
{
	float2 PosOffset;   // snapped clipmap-cell origin in camera-relative world space
	uint2 ArrayOrigin;  // physical toroidal origin

	int2 ValidMargin;
	float TimeDelta;
	uint BoundingBoxCount;

	float CameraHeightDelta;
	uint LegacyDebugInteractionField; // retained byte slot; terrain debug uses b13 TerrainDebug
	float TrackHoldSeconds;
	float TrackRecoveryRate;
}

struct BoundingBoxPacked
{
	float2 MinExtent;
	float2 MaxExtent;
	uint IndexStart;
	uint IndexEnd;
	float2 pad0;
};

StructuredBuffer<BoundingBoxPacked> CollisionBoundingBoxes : register(t0);
StructuredBuffer<float4> CollisionInstances : register(t1);
RWTexture2D<float4> Collision : register(u0);

groupshared BoundingBoxPacked SharedBoundingBoxes[64];

static const uint TEXTURE_SIZE = 512u;
static const uint TEXTURE_MASK = TEXTURE_SIZE - 1u;
static const float WORLD_SIZE = 4096.0f;
static const float2 ZRANGE = float2(2048.0f, -2048.0f);
static const float MAX_GPU_COLLISION_RADIUS = 512.0f;

float2 DecodeField(float2 encoded)
{
	float2 value = lerp(ZRANGE.x.xx, ZRANGE.y.xx, saturate(encoded));
	return clamp(value, ZRANGE.y.xx, ZRANGE.x.xx);
}

float2 EncodeField(float2 height)
{
	height = clamp(height, ZRANGE.y.xx, ZRANGE.x.xx);
	return saturate((height - ZRANGE.x.xx) / (ZRANGE.y - ZRANGE.x));
}

[numthreads(8, 8, 1)]
void main(
	uint3 groupId : SV_GroupID,
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (groupIndex < BoundingBoxCount)
		SharedBoundingBoxes[groupIndex] = CollisionBoundingBoxes[groupIndex];

	GroupMemoryBarrierWithGroupSync();

	// Inverse toroidal mapping. 512 is a power of two, so the mask provides
	// exact signed wrap without the negative-modulo failure mode.
	int2 relativeCell =
		int2(dispatchThreadId.xy) - int2(ArrayOrigin);
	uint2 cellID =
		uint2(relativeCell) & uint2(TEXTURE_MASK, TEXTURE_MASK);

	float2 cellCentreMS =
		(float2(cellID) + 0.5f - float(TEXTURE_SIZE) * 0.5f) *
		(WORLD_SIZE / float(TEXTURE_SIZE)) +
		PosOffset.xy;

	// ValidMargin identifies the portion of history that remains spatially valid
	// after the clipmap origin moves. A full-size margin intentionally invalidates
	// every cell and therefore performs a clean history reset.
	int2 validMin = max(ValidMargin.xy, int2(0, 0));
	int2 validMax =
		int2(TEXTURE_SIZE - 1, TEXTURE_SIZE - 1) +
		min(ValidMargin.xy, int2(0, 0));
	int2 logicalCell = int2(cellID);
	bool historyValid =
		all(logicalCell >= validMin) &&
		all(logicalCell <= validMax);

	// Empty field is +2048, encoded as exactly zero.
	float2 collision = ZRANGE.x.xx;
	float2 previousCollision = ZRANGE.x.xx;

	float slowRecovery = max(TrackRecoveryRate, 0.01f);
	float fastRecovery = max(8.0f, slowRecovery * 24.0f);
	float safeDelta = clamp(TimeDelta, 0.0f, 0.25f);
	float holdSeconds = clamp(TrackHoldSeconds, 0.0f, 30.0f);

	if (historyValid)
	{
		previousCollision =
			DecodeField(Collision[dispatchThreadId.xy].xy);

		// Stored heights are camera-relative. CPU-side camera discontinuity
		// rejection ensures only a sane rebase delta reaches this path.
		previousCollision =
			clamp(
				previousCollision + CameraHeightDelta.xx,
				ZRANGE.y.xx,
				ZRANGE.x.xx);

		// Y is the fast freshness clock. It rises at a known world-units/sec
		// rate from the same contact height as persistent X, so their separation
		// is a compact local age signal with no extra texture or timestamp buffer.
		float recoveredFast =
			min(previousCollision.y + safeDelta * fastRecovery, ZRANGE.x);
		float persistent = previousCollision.x;
		float ageDistance = max(recoveredFast - persistent, 0.0f);
		float holdDistance = fastRecovery * holdSeconds;
		float recoveryGate =
			holdSeconds <= 1e-4f || ageDistance >= holdDistance
				? 1.0f
				: 0.0f;

		persistent = min(
			persistent + safeDelta * slowRecovery * recoveryGate,
			ZRANGE.x);
		collision = float2(persistent, recoveredFast);
	}

	[loop] for (uint i = 0u; i < BoundingBoxCount; ++i)
	{
		BoundingBoxPacked boundingBox =
			SharedBoundingBoxes[i];

		if (!all(
			cellCentreMS >= boundingBox.MinExtent &&
			cellCentreMS <= boundingBox.MaxExtent))
			continue;

		[loop] for (uint j = boundingBox.IndexStart;
			j < boundingBox.IndexEnd;
			++j)
		{
			float4 collisionInstance =
				CollisionInstances[j];
			float radius = collisionInstance.w;

			// CPU validation should already guarantee this. Retain a GPU-side
			// guard so malformed physics bounds can never stamp the entire field.
			if (radius <= 0.0f ||
				radius > MAX_GPU_COLLISION_RADIUS)
				continue;

			float lowestPossibleHeight =
				collisionInstance.z - radius;
			if (lowestPossibleHeight >= collision.y)
				continue;

			float2 offset =
				collisionInstance.xy - cellCentreMS;
			float distSq = dot(offset, offset);
			float radiusSq = radius * radius;
			if (distSq >= radiusSq)
				continue;

			float heightFromCenter =
				sqrt(max(radiusSq - distSq, 0.0f));
			float height =
				clamp(
					collisionInstance.z - heightFromCenter,
					ZRANGE.y,
					ZRANGE.x);

			// X: persistent track. Y: fast contact/freshness field.
			collision.x = min(collision.x, height);
			collision.y = min(collision.y, height);
		}
	}

	float2 encodedCollision =
		EncodeField(collision);
	float2 encodedPrevious =
		EncodeField(previousCollision);

	Collision[dispatchThreadId.xy] =
		float4(encodedCollision, encodedPrevious);
}
