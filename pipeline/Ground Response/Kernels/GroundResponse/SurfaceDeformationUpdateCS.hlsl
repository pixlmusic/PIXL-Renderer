// PIXL Ground Response 3.0
// PIXL_GR_13AD_DISPLACED_SNOW_TWO_STAGE_V1
// Persistent normalized world-space snow + mud deformation plus displaced snow.
//
// t101 / SurfaceField (u0):
// x = current compaction  (0 pristine, 1 fully compressed)
// y = current freshness   (1 freshly stamped, 0 hold expired)
// z = previous compaction (for motion vectors)
// w = previous freshness
//
// t102 / SurfaceDisplacementField (u1 when updating, t102 when rendering):
// x = current displaced-snow pile amount
// y = current stage-1 settling state (1 fresh interaction -> 0 equalized)
// z = previous displaced-snow pile amount
// w = previous settling state
//
// Stage 1 begins as soon as a contact stops refreshing a texel: displaced snow
// settles while the deep core rises toward a residual shallow trough. Stage 2
// retains the existing TrackHoldSeconds + SurfaceRecoveryRate behaviour, so the
// broad residual track then returns to pristine height without popping.
//
// The physical textures are toroidal. Moving the player changes only the logical
// window origin/array origin; surviving texels continue to represent the exact
// same absolute world XY. No camera position, camera height, equipment state or
// first/third-person transform participates in history.

#define MAX_STAMP_BOXES 64

cbuffer SurfaceFieldCB : register(b0)
{
	float2 SurfaceOriginAbsolute;
	uint2 SurfaceArrayOrigin;

	int2 SurfaceValidMargin;
	float SurfaceTimeDelta;
	uint SurfaceStampBoxCount;

	float SurfaceTrackHoldSeconds;
	float SurfaceRecoveryRate;
	float SurfaceStampStrength;
	float SurfaceElementalRecoveryRate;
}

struct SurfaceStampBoxPacked
{
	float2 MinExtent;
	float2 MaxExtent;
	uint IndexStart;
	uint IndexEnd;
	float2 pad0;
};

struct SurfaceStampPacked
{
	float2 CurrentPosition;
	float2 PreviousPosition;
	float Radius;
	float PreviousRadius;
	float Strength;
	float DisplacementScale;
	float ElementalDelta;
	float Smoothing;
	uint Flags;
	float ContactDepth;
};

StructuredBuffer<SurfaceStampBoxPacked> StampBoxes : register(t0);
StructuredBuffer<SurfaceStampPacked> Stamps : register(t1);
RWTexture2D<float4> SurfaceField : register(u0);
RWTexture2D<float4> SurfaceDisplacementField : register(u1);
// x=current signed snow height delta (world units), y=current heat smoothing,
// z=previous signed height delta, w=previous heat smoothing.
RWTexture2D<float4> SurfaceElementalField : register(u2);

groupshared SurfaceStampBoxPacked SharedStampBoxes[MAX_STAMP_BOXES];

static const uint TEXTURE_SIZE = 1024u;
static const uint TEXTURE_MASK = TEXTURE_SIZE - 1u;
static const float WORLD_SIZE = 4096.0f;
static const float CELL_SIZE = WORLD_SIZE / float(TEXTURE_SIZE);

// Conservative first-pass physical tuning. These are shader-local on purpose:
// 13AD does not alter the proven 48-byte surface CB or the established 160-byte
// prefix of terrain b13. GroundResponse 3.3 appends weather snow after that prefix.
// PIXL_GR_13AM_EXTENDED_SNOW_PHASES_V1
static const float SETTLING_SECONDS = 11.50f;
static const float SETTLING_CORE_RESIDUAL = 0.28f;
static const float SETTLING_CORE_LATE_RECOVERY_RATE = 0.040f;
static const float SETTLING_CORE_LATE_START = 0.08f;
static const float DISPLACED_SNOW_SETTLE_RATE = 0.024f;
static const float DISPLACED_SNOW_SETTLE_CAP = 0.92f;
static const float DISPLACED_SNOW_LATERAL_RETENTION = 0.90f;
static const float MIN_PUSH_SPEED = 18.0f;
static const float FULL_PUSH_SPEED = 200.0f;
static const float FINAL_RECOVERY_TIME_SCALE = 0.62f;
static const float ELEMENTAL_HEIGHT_HARD_LIMIT = 32.0f;
static const float HEAT_SMOOTHING_DECAY_RATE = 0.42f;

static const uint STAMP_FLAG_DISTANCE_FALLOFF = 1u << 0;

float2 ClosestPointOnSegment(float2 p, float2 a, float2 b, out float segmentT)
{
	float2 ab = b - a;
	float lenSq = dot(ab, ab);
	segmentT =
		lenSq > 1e-5f
			? saturate(dot(p - a, ab) / lenSq)
			: 0.0f;
	return a + ab * segmentT;
}

float DistanceSqToSegment(float2 p, float2 a, float2 b)
{
	float segmentT;
	float2 closest = ClosestPointOnSegment(p, a, b, segmentT);
	float2 d = p - closest;
	return dot(d, d);
}

float SmoothPulse(float x, float rise0, float rise1, float fall0, float fall1)
{
	return
		smoothstep(rise0, rise1, x) *
		(1.0f - smoothstep(fall0, fall1, x));
}

[numthreads(8, 8, 1)]
void main(
	uint3 groupId : SV_GroupID,
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex)
{
	if (groupIndex < SurfaceStampBoxCount)
		SharedStampBoxes[groupIndex] = StampBoxes[groupIndex];

	GroupMemoryBarrierWithGroupSync();

	// Inverse toroidal mapping: this physical texel resolves to one logical cell.
	int2 relativeCell =
		int2(dispatchThreadId.xy) -
		int2(SurfaceArrayOrigin);
	uint2 logicalCell =
		uint2(relativeCell) &
		uint2(TEXTURE_MASK, TEXTURE_MASK);

	float2 worldPos =
		SurfaceOriginAbsolute +
		(float2(logicalCell) + 0.5f - float(TEXTURE_SIZE) * 0.5f) *
		CELL_SIZE;

	int2 validMin =
		max(SurfaceValidMargin.xy, int2(0, 0));
	int2 validMax =
		int2(TEXTURE_SIZE - 1, TEXTURE_SIZE - 1) +
		min(SurfaceValidMargin.xy, int2(0, 0));
	bool historyValid =
		all(int2(logicalCell) >= validMin) &&
		all(int2(logicalCell) <= validMax);

	float compaction = 0.0f;
	float freshness = 0.0f;
	float previousCompaction = 0.0f;
	float previousFreshness = 0.0f;

	float displacedSnow = 0.0f;
	float settling = 0.0f;
	float previousDisplacedSnow = 0.0f;
	float previousSettling = 0.0f;

	float elementalHeight = 0.0f;
	float heatSmoothing = 0.0f;
	float previousElementalHeight = 0.0f;
	float previousHeatSmoothing = 0.0f;

	float dt = clamp(SurfaceTimeDelta, 0.0f, 0.25f);

	if (historyValid)
	{
		float4 history =
			saturate(SurfaceField[dispatchThreadId.xy]);
		float4 displacementHistory =
			saturate(SurfaceDisplacementField[dispatchThreadId.xy]);
		float4 elementalHistory =
			SurfaceElementalField[dispatchThreadId.xy];

		compaction = history.x;
		freshness = history.y;
		previousCompaction = compaction;
		previousFreshness = freshness;

		displacedSnow = displacementHistory.x;
		settling = displacementHistory.y;
		previousDisplacedSnow = displacedSnow;
		previousSettling = settling;

		elementalHeight =
			clamp(
				elementalHistory.x,
				-ELEMENTAL_HEIGHT_HARD_LIMIT,
				 ELEMENTAL_HEIGHT_HARD_LIMIT);
		heatSmoothing = saturate(elementalHistory.y);
		previousElementalHeight = elementalHeight;
		previousHeatSmoothing = heatSmoothing;

		// Elemental snow changes are persistent but weather/time slowly restores
		// the authored baseline. Heat smoothing is intentionally short-lived.
		float elementalRecovery =
			max(SurfaceElementalRecoveryRate, 0.0f) * dt;
		if (elementalHeight > 0.0f)
			elementalHeight = max(elementalHeight - elementalRecovery, 0.0f);
		else if (elementalHeight < 0.0f)
			elementalHeight = min(elementalHeight + elementalRecovery, 0.0f);
		heatSmoothing =
			max(heatSmoothing - dt * HEAT_SMOOTHING_DECAY_RATE, 0.0f);

		float holdSeconds =
			max(SurfaceTrackHoldSeconds, 0.0f);

		if (holdSeconds > 1e-4f)
			freshness =
				max(
					freshness -
						dt / holdSeconds,
					0.0f);
		else
			freshness = 0.0f;
	}

	bool interactionThisFrame = false;

	[loop] for (uint i = 0u; i < SurfaceStampBoxCount; ++i)
	{
		SurfaceStampBoxPacked box =
			SharedStampBoxes[i];

		if (!all(
			worldPos >= box.MinExtent &&
			worldPos <= box.MaxExtent))
			continue;

		[loop] for (uint j = box.IndexStart;
			j < box.IndexEnd;
			++j)
		{
			SurfaceStampPacked stamp = Stamps[j];
			float endRadius = max(stamp.Radius, 0.0f);
			float startRadius = max(stamp.PreviousRadius, 0.0f);
			if (max(endRadius, startRadius) <= 0.0f ||
				(stamp.Strength <= 0.0f &&
				 abs(stamp.ElementalDelta) <= 1e-5f &&
				 stamp.Smoothing <= 1e-5f))
				continue;

			float2 motion =
				stamp.CurrentPosition -
				stamp.PreviousPosition;
			float motionLength = length(motion);
			float2 direction =
				motionLength > 1e-4f
					? motion / motionLength
					: float2(1.0f, 0.0f);
			float2 lateralDirection =
				float2(-direction.y, direction.x);

			float speed =
				motionLength /
				max(dt, 1.0f / 240.0f);
			float pushSpeed =
				smoothstep(
					MIN_PUSH_SPEED,
					FULL_PUSH_SPEED,
					speed);

			float segmentT;
			float2 closest =
				ClosestPointOnSegment(
					worldPos,
					stamp.PreviousPosition,
					stamp.CurrentPosition,
					segmentT);
			float radius =
				max(lerp(startRadius, endRadius, segmentT), 1e-3f);
			float longitudinalFalloff = 1.0f;
			if ((stamp.Flags & STAMP_FLAG_DISTANCE_FALLOFF) != 0u)
			{
				// Shout runs can be split around roofs/bridges. CPU strength already
				// contains the absolute falloff at this run's start; ContactDepth is
				// repurposed for flagged stamps as the end/start falloff ratio so a
				// resumed run never jumps back to full force after an obstruction.
				float endRatio = clamp(stamp.ContactDepth, 0.02f, 1.0f);
				longitudinalFalloff =
					lerp(1.0f, endRatio, smoothstep(0.0f, 1.0f, segmentT));
			}
			float2 fromSegment = worldPos - closest;
			float distanceFromSegment = length(fromSegment);
			float radial =
				distanceFromSegment /
				max(radius, 1e-3f);

			// PIXL_GR_13AH_FLAT_SWEPT_CAPSULE_V1
			//
			// The motion segment already supplies trail continuity. Write a flat
			// compacted core and reserve interpolation for the outer 30% rim,
			// rather than encoding a spherical/bowl gradient across the collider.
			float falloff = 0.0f;
			if (radial < 1.0f)
			{
				const float coreRadius = 0.70f;

				if (radial <= coreRadius)
				{
					falloff = 1.0f;
				}
				else
				{
					float t =
						saturate(
							(radial - coreRadius) /
							max(1.0f - coreRadius, 1.0e-4f));

					float smoothEdge =
						t * t * t *
						(t * (t * 6.0f - 15.0f) + 10.0f);

					falloff = 1.0f - smoothEdge;
				}

				float stampAmount =
					saturate(
						stamp.Strength *
						max(SurfaceStampStrength, 0.0f) *
						falloff *
						longitudinalFalloff);

				compaction = max(compaction, stampAmount);
				freshness = max(freshness, falloff * longitudinalFalloff);
			}

			// Snow removed from the channel is not simply deleted. A smooth berm
			// exists outside both sides of the swept capsule. Stationary contact
			// receives a small rim; locomotion strengthens the lateral push.
			float sideBand =
				SmoothPulse(
					radial,
					0.72f,
					1.02f,
					1.08f,
					1.92f);

			// Keep the side band symmetric but slightly favour true lateral points
			// over the circular end caps, preventing a string of round "orb" rims.
			float lateralAmount =
				abs(dot(fromSegment, lateralDirection)) /
				max(distanceFromSegment, 1e-3f);
			float lateralShape =
				lerp(0.72f, 1.0f, saturate(lateralAmount));
			float sidePush =
				sideBand *
				lateralShape *
				(0.18f + 0.36f * pushSpeed);

			// A short velocity-sensitive accumulation wedge forms immediately in
			// front of the actor. It follows movement direction and is intentionally
			// small enough to read as pushed powder rather than a permanent snowball.
			float2 frontCenter =
				stamp.CurrentPosition +
				direction * (endRadius * 0.92f);
			float2 frontDelta = worldPos - frontCenter;
			float frontLong =
				dot(frontDelta, direction) /
				max(endRadius * 1.22f, 1e-3f);
			float frontLat =
				dot(frontDelta, lateralDirection) /
				max(endRadius * 0.92f, 1e-3f);
			float frontDistance =
				length(float2(frontLong, frontLat));
			float frontLobe =
				(1.0f - smoothstep(0.18f, 1.0f, frontDistance)) *
				pushSpeed;

			float displacedMask =
				max(
					sidePush,
					frontLobe * 0.50f);
			float displacedAmount =
				saturate(
					stamp.Strength *
					max(SurfaceStampStrength, 0.0f) *
					displacedMask *
					max(stamp.DisplacementScale, 0.0f) *
					longitudinalFalloff *
					DISPLACED_SNOW_LATERAL_RETENTION);

			if (displacedAmount > 1e-5f)
			{
				displacedSnow =
					max(displacedSnow, displacedAmount);
				freshness =
					max(
						freshness,
						saturate(displacedMask * 0.72f));
			}

			// Elemental modifiers are TARGET heights, not additive impulses. This
			// makes repeated concentration/projectile callbacks stable: frost cannot
			// grow without bound and fire cannot tunnel below the configured melt
			// target simply because the engine reports several impacts.
			float elementalMask =
				saturate(falloff * longitudinalFalloff);
			if (elementalMask > 1e-5f)
			{
				float elementalTarget =
					clamp(
						stamp.ElementalDelta * elementalMask,
						-ELEMENTAL_HEIGHT_HARD_LIMIT,
						 ELEMENTAL_HEIGHT_HARD_LIMIT);

				if (elementalTarget > 0.0f)
					elementalHeight = max(elementalHeight, elementalTarget);
				else if (elementalTarget < 0.0f)
					elementalHeight = min(elementalHeight, elementalTarget);

				heatSmoothing =
					max(
						heatSmoothing,
						saturate(stamp.Smoothing * elementalMask));
			}

			float settleRefresh =
				max(falloff, saturate(displacedMask));
			if (settleRefresh > 1e-5f && stamp.Strength > 1e-5f)
			{
				settling = max(settling, settleRefresh);
				interactionThisFrame = true;
			}
		}
	}

	if (historyValid && !interactionThisFrame)
	{
		// PIXL_GR_13AK_THREE_PHASE_RECOVERY_V1
		//
		// PHASE A — BULK SPLIT / SLIDE (~first 5 seconds)
		// Keep the cut deep while the tall walls physically slump outward.
		//
		// PHASE B — EDGE SETTLING (final portion of Stage 1)
		// Round the new outer toe and permit only a small late equalisation.
		//
		// PHASE C — LEVELLING / FILL
		// After settling reaches zero, existing hold/recovery raises the broad
		// shallow path back to the pristine drift.
		if (settling > 1e-4f)
		{
			settling =
				max(
					settling -
						dt / SETTLING_SECONDS,
					0.0f);

			displacedSnow =
				max(
					displacedSnow -
						dt * DISPLACED_SNOW_SETTLE_RATE,
					0.0f);

			displacedSnow =
				min(
					displacedSnow,
					settling * DISPLACED_SNOW_SETTLE_CAP);

			float lateEqualisation =
				1.0f -
				smoothstep(
					SETTLING_CORE_LATE_START,
					SETTLING_CORE_LATE_START + 0.16f,
					settling);

			if (lateEqualisation > 1e-4f &&
				compaction > SETTLING_CORE_RESIDUAL)
			{
				compaction =
					max(
						SETTLING_CORE_RESIDUAL,
						compaction -
							dt *
							SETTLING_CORE_LATE_RECOVERY_RATE *
							lateEqualisation);
			}
		}
		// STAGE 2 — NORMALISATION
		// Once the pushed snow is settled, the existing hold/recovery system owns
		// the broad shallow track and raises the whole region back toward pristine.
		else
		{
			displacedSnow = 0.0f;

			if (freshness <= 1e-4f)
				compaction =
					max(
						compaction -
							dt * max(SurfaceRecoveryRate, 0.0f) * FINAL_RECOVERY_TIME_SCALE,
						0.0f);
		}
	}

	SurfaceField[dispatchThreadId.xy] =
		float4(
			saturate(compaction),
			saturate(freshness),
			saturate(previousCompaction),
			saturate(previousFreshness));

	SurfaceDisplacementField[dispatchThreadId.xy] =
		float4(
			saturate(displacedSnow),
			saturate(settling),
			saturate(previousDisplacedSnow),
			saturate(previousSettling));

	SurfaceElementalField[dispatchThreadId.xy] =
		float4(
			clamp(elementalHeight, -ELEMENTAL_HEIGHT_HARD_LIMIT, ELEMENTAL_HEIGHT_HARD_LIMIT),
			saturate(heatSmoothing),
			clamp(previousElementalHeight, -ELEMENTAL_HEIGHT_HARD_LIMIT, ELEMENTAL_HEIGHT_HARD_LIMIT),
			saturate(previousHeatSmoothing));
}
