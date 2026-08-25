#ifndef PIXL_GROUND_RESPONSE_HLSLI
#define PIXL_GROUND_RESPONSE_HLSLI

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace GroundResponse
{
	Texture2D<float4> Collision : register(t100);

	// The clipmap transform lives in FeatureData/b6. Do not redeclare b5 here:
	// b5 belongs to SharedData in PIXL's global shader contract.
	static const uint TEXTURE_SIZE = 512u;
	static const uint TEXTURE_MASK = TEXTURE_SIZE - 1u;
	static const float WORLD_SIZE = 4096.0f;
	static const float CELL_SIZE = WORLD_SIZE / float(TEXTURE_SIZE);
	static const float2 ZRANGE = float2(2048.0f, -2048.0f);
	static const float EMPTY_EPSILON = 1e-5f;

	float DecodeHeight(float encodedHeight)
	{
		return lerp(ZRANGE.x, ZRANGE.y, saturate(encodedHeight));
	}

	bool HasContact(float encodedHeight)
	{
		return encodedHeight > EMPTY_EPSILON;
	}

	float DecodeContactHeight(float encodedHeight, float receiverHeight)
	{
		// Encoded zero is the +2048 empty sentinel. Never let that storage value
		// participate in a spatial derivative: use the receiver as the neutral
		// height at empty texels instead.
		return HasContact(encodedHeight)
			? min(DecodeHeight(encodedHeight), receiverHeight)
			: receiverHeight;
	}

	float ProceduralAnimation(float recoverySeparation, float distanceFromCenter)
	{
		recoverySeparation = max(recoverySeparation, 0.0f);
		float phase =
			recoverySeparation * (100.0f / 250.0f) *
			rcp(max(distanceFromCenter, 0.01f));
		return cos(phase * (4.0f * Math::PI)) * exp(-phase * 4.0f);
	}

	void GetCollision(
		float3 worldPosition,
		float maximumDepth,
		float distanceFromCenter,
		out float collisionHeight,
		out float collisionAmount,
		out float previousCollisionHeight,
		out float previousCollisionAmount)
	{
		collisionHeight = 0.0f;
		collisionAmount = 0.0f;
		previousCollisionHeight = 0.0f;
		previousCollisionAmount = 0.0f;

		float2 positionMSAdjusted =
			worldPosition.xy - SharedData::deformableGroundSettings.PosOffset;

		// The physical texture wraps, but logical interaction coverage does not.
		if (any(abs(positionMSAdjusted) > (WORLD_SIZE * 0.5f + CELL_SIZE)))
			return;

		float2 texelPosition =
			(positionMSAdjusted / WORLD_SIZE + 0.5f) *
			float(TEXTURE_SIZE) - 0.5f;
		int2 baseTexel = (int2)floor(texelPosition);
		float2 fraction = frac(texelPosition);
		int2 arrayOrigin = int2(SharedData::deformableGroundSettings.ArrayOrigin);

		float weightSum = 0.0f;
		float clampedMaximumDepth = max(maximumDepth, 0.0f);

		[unroll] for (int y = 0; y < 2; ++y)
		{
			[unroll] for (int x = 0; x < 2; ++x)
			{
				int2 logicalTexel = baseTexel + int2(x, y);
				if (any(logicalTexel < 0) ||
					any(logicalTexel >= int2(TEXTURE_SIZE, TEXTURE_SIZE)))
					continue;

				float2 w2 =
					1.0f.xx - abs(float2(x, y) - fraction);
				float weight = w2.x * w2.y;

				uint2 physicalTexel =
					uint2(logicalTexel + arrayOrigin) &
					uint2(TEXTURE_MASK, TEXTURE_MASK);

				float4 encoded =
					Collision.Load(int3(physicalTexel, 0));

				float currentPersistent =
					DecodeContactHeight(encoded.x, worldPosition.z);
				float currentFast =
					DecodeContactHeight(encoded.y, worldPosition.z);
				float previousPersistent =
					DecodeContactHeight(encoded.z, worldPosition.z);
				float previousFast =
					DecodeContactHeight(encoded.w, worldPosition.z);

				float currentDepth = min(
					clampedMaximumDepth,
					max(worldPosition.z - currentPersistent, 0.0f));
				float previousDepth = min(
					clampedMaximumDepth,
					max(worldPosition.z - previousPersistent, 0.0f));

				collisionHeight += currentPersistent * weight;
				collisionAmount +=
					currentDepth *
					ProceduralAnimation(
						currentFast - currentPersistent,
						distanceFromCenter) *
					weight;

				previousCollisionHeight += previousPersistent * weight;
				previousCollisionAmount +=
					previousDepth *
					ProceduralAnimation(
						previousFast - previousPersistent,
						distanceFromCenter) *
					weight;

				weightSum += weight;
			}
		}

		if (weightSum > 1e-6f)
		{
			float invWeight = rcp(weightSum);
			collisionHeight *= invWeight;
			collisionAmount *= invWeight;
			previousCollisionHeight *= invWeight;
			previousCollisionAmount *= invWeight;
		}
		else
		{
			collisionHeight = worldPosition.z;
			collisionAmount = 0.0f;
			previousCollisionHeight = worldPosition.z;
			previousCollisionAmount = 0.0f;
		}
	}

	float3 ComputeNormalFromHeights(
		float h0,
		float hX,
		float hY,
		float delta)
	{
		float safeDelta = max(delta, 1e-3f);
		float3 tangentX = float3(safeDelta, 0.0f, hX - h0);
		float3 tangentY = float3(0.0f, safeDelta, hY - h0);
		float3 n =
			cross(tangentX, tangentY) *
			float3(1.0f, 1.0f, 0.1f);
		float lenSq = dot(n, n);
		return lenSq > 1e-12f
			? -n * rsqrt(lenSq)
			: float3(0.0f, 0.0f, -1.0f);
	}

	void ComputeCollision(
		float3 worldPosition,
		float maximumDepth,
		float distanceFromCenter,
		float delta,
		out float3 collision,
		out float3 previousCollision)
	{
		float h0, hx, hy;
		float a0, ax, ay;
		float ph0, phx, phy;
		float pa0, pax, pay;

		// Use a true centre + forward finite difference. The previous diagonal
		// centre sample exaggerated gradients and could produce one-sided spikes.
		GetCollision(
			worldPosition,
			maximumDepth,
			distanceFromCenter,
			h0, a0, ph0, pa0);
		GetCollision(
			worldPosition + float3(delta, 0.0f, 0.0f),
			maximumDepth,
			distanceFromCenter,
			hx, ax, phx, pax);
		GetCollision(
			worldPosition + float3(0.0f, delta, 0.0f),
			maximumDepth,
			distanceFromCenter,
			hy, ay, phy, pay);

		float amount = (a0 + ax + ay) * (1.0f / 3.0f);
		float previousAmount = (pa0 + pax + pay) * (1.0f / 3.0f);

		collision =
			ComputeNormalFromHeights(h0, hx, hy, delta) * amount;
		previousCollision =
			ComputeNormalFromHeights(ph0, phx, phy, delta) *
			previousAmount;
	}

	void GetDisplacedPosition(
		VS_INPUT input,
		float3 position,
		out float3 displacement,
		out float3 previousDisplacement)
	{
		displacement = 0.0f;
		previousDisplacement = 0.0f;

		// EnableGroundResponse is represented by whether t100 is bound to VS and
		// remains independent from the optional terrain deformation path.
		if (input.Color.w <= 0.0f)
			return;

		float3 worldPosition =
			mul(World, float4(position.xyz, 1.0f)).xyz;

		float nearFactor =
			1.0f - smoothstep(0.0f, 2048.0f, length(worldPosition));
		if (nearFactor <= 0.0f)
			return;

		float3 worldPositionCentre =
			mul(World, float4(input.InstanceData1.xyz, 1.0f)).xyz;
		float3 remappedWorldPosition =
			lerp(
				worldPosition,
				worldPositionCentre,
				float3(0.95f, 0.95f, 0.0f));

		float distanceFromCenter =
			max(length(worldPosition - worldPositionCentre), 0.01f);
		float maximumDepth =
			max(worldPosition.z - worldPositionCentre.z, 0.0f);
		if (maximumDepth <= 0.0f)
			return;

		float3 collision, previousCollision;
		ComputeCollision(
			remappedWorldPosition,
			maximumDepth,
			distanceFromCenter,
			CELL_SIZE,
			collision,
			previousCollision);

		// Grass should part/down-bend around actors, never be pulled upward.
		collision.z = -abs(collision.z);
		previousCollision.z = -abs(previousCollision.z);

		float alpha = saturate(input.Color.w * 10.0f);
		float responseStrength = max(
			SharedData::deformableGroundSettings.GroundResponseStrength,
			0.0f);
		float scale =
			alpha * nearFactor * 0.75f * responseStrength;

		displacement = collision * scale;
		previousDisplacement = previousCollision * scale;
	}
}

#endif
