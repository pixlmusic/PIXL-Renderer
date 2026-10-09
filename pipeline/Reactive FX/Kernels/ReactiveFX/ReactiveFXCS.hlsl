// PIXL Renderer - Reactive FX compute simulation and optical resolve.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "Common/SharedData.hlsli"
#include "Common/GBuffer.hlsli"

struct Particle
{
	float3 Position;
	float Age;
	float3 Velocity;
	float Lifetime;
	float3 Acceleration;
	float Drag;
	float4 ColorEmission;
	float Size;
	float Rotation;
	float AngularVelocity;
	float Restitution;
	float Friction;
	float CollisionThickness;
	uint Type;
	uint Flags;
	uint BounceCount;
	uint MaxBounces;
	float FadeIn;
	float FadeOut;
};

struct SpawnCommand
{
	uint Slot;
	uint3 Padding;
	Particle Value;
};

struct ReactiveImpulse
{
	float3 Position;
	float Radius;
	float3 Direction;
	float Strength;
	float Age;
	float PreviousAge;
	float Duration;
	float WaveSpeed;
	uint Type;
	uint Active;
	float Falloff;
	float VerticalInfluence;
};

cbuffer ReactiveFXTuning : register(b13)
{
	float2 RenderSize;
	float2 InvRenderSize;
	float DeltaTime;
	float DisplayScale;
	float ParticleIntensity;
	float MaximumDistance;
	uint ParticleCapacity;
	uint SpawnCount;
	uint CollisionBudget;
	uint DebugMode;
	float CollisionEnabled;
	uint ActiveImpulseCount;
	uint CollisionPhase;
	float OpticalActive;
};

static const uint PARTICLE_SPARK = 0u;
static const uint PARTICLE_FROST_CRYSTAL = 1u;
static const uint PARTICLE_FRAGMENT = 2u;
static const uint PARTICLE_SMOKE_DUST = 3u;
static const uint PARTICLE_SOFT_PUFF = 4u;
static const uint PARTICLE_MAGIC_MOTE = 5u;
static const uint PARTICLE_HOT_STREAK = 6u;
static const uint PARTICLE_LEAF_CARD = 7u;
static const uint IMPULSE_RADIAL = 0u;
static const uint IMPULSE_DIRECTIONAL = 1u;
static const uint IMPULSE_TRAVELLING_WAVE = 2u;
// Particle.Flags keeps additive/collision in bits 0/1. Frost uses bit 2 for
// floor-contact settling eligibility and bits 3..7 for consecutive quiet hits.
static const uint PARTICLE_SETTLE_CANDIDATE = 4u;
static const uint PARTICLE_QUIET_HIT_MASK = 0xF8u;

StructuredBuffer<SpawnCommand> SpawnCommands : register(t0);
RWStructuredBuffer<Particle> ParticleState : register(u0);

[numthreads(64, 1, 1)]
void SpawnCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= SpawnCount)
		return;
	SpawnCommand command = SpawnCommands[dispatchID.x];
	if (command.Slot < ParticleCapacity) {
		Particle resident = ParticleState[command.Slot];
		// Preserve living ice when the ring wraps. The CPU compacts duplicate
		// slots before this pass, so each thread owns its candidate slot.
		if ((command.Value.Type == PARTICLE_FROST_CRYSTAL || resident.Type == PARTICLE_FROST_CRYSTAL) &&
			resident.Lifetime > 0.0f && resident.Age < resident.Lifetime)
			return;
		ParticleState[command.Slot] = command.Value;
	}
}

Texture2D<float> CollisionDepth : register(t0);
Texture2D<float4> CollisionNormalRoughness : register(t1);
StructuredBuffer<ReactiveImpulse> ParticleImpulses : register(t2);

float3 SafeDirection(float3 value, float3 fallback)
{
	float lengthSq = dot(value, value);
	return lengthSq > 1.0e-8f && all(isfinite(value)) ? value * rsqrt(lengthSq) : fallback;
}

float ImpulseEnvelope(ReactiveImpulse impulse, float3 position, out float3 forceDirection)
{
	forceDirection = 0.0f;
	if (impulse.Active == 0u || impulse.Duration <= 1.0e-4f || impulse.Age >= impulse.Duration)
		return 0.0f;
	float3 offset = position - impulse.Position;
	float distanceToOrigin = length(offset);
	if (!isfinite(distanceToOrigin) || distanceToOrigin >= impulse.Radius)
		return 0.0f;
	float life = saturate(impulse.Age / impulse.Duration);
	float decay = (1.0f - life) * (1.0f - life);
	if (impulse.Type == IMPULSE_TRAVELLING_WAVE)
	{
		float frontCurrent = impulse.Age * impulse.WaveSpeed;
		float frontPrevious = impulse.PreviousAge * impulse.WaveSpeed;
		float width = max(impulse.Radius * 0.12f, 48.0f);
		float intervalDistance = max(max(frontPrevious - distanceToOrigin, 0.0f),
			distanceToOrigin - frontCurrent);
		float wave = exp2(-4.0f * intervalDistance / width);
		float3 radial = SafeDirection(offset, SafeDirection(impulse.Direction, float3(1, 0, 0)));
		forceDirection = SafeDirection(lerp(radial, impulse.Direction, 0.72f) +
			float3(0, 0, impulse.VerticalInfluence * 0.65f), radial);
		return wave * decay * impulse.Strength;
	}
	float radialFalloff = pow(saturate(1.0f - distanceToOrigin / impulse.Radius), max(impulse.Falloff, 0.25f));
	float3 radialDirection = SafeDirection(offset, float3(0, 0, 1));
	forceDirection = impulse.Type == IMPULSE_DIRECTIONAL
		? SafeDirection(lerp(radialDirection, impulse.Direction, 0.78f) +
			float3(0, 0, impulse.VerticalInfluence * 0.35f), radialDirection)
		: SafeDirection(radialDirection + float3(0, 0, impulse.VerticalInfluence), radialDirection);
	return radialFalloff * decay * impulse.Strength;
}

[numthreads(64, 1, 1)]
void SimulateCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= ParticleCapacity)
		return;
	Particle particle = ParticleState[dispatchID.x];
	if (!all(isfinite(float4(particle.Position, particle.Age))) ||
		!all(isfinite(float4(particle.Velocity, particle.Lifetime))) ||
		particle.Lifetime <= 0.0f || particle.Age >= particle.Lifetime)
	{
		particle.Age = max(particle.Lifetime, 0.0f);
		ParticleState[dispatchID.x] = particle;
		return;
	}

	float dt = clamp(DeltaTime, 0.0f, 1.0f / 15.0f);
	particle.Age += dt;
	if (particle.Age >= particle.Lifetime)
	{
		particle.Age = particle.Lifetime;
		ParticleState[dispatchID.x] = particle;
		return;
	}

	[loop]
	for (uint i = 0u; i < ActiveImpulseCount; ++i)
	{
		float3 forceDirection;
		float envelope = ImpulseEnvelope(ParticleImpulses[i], particle.Position, forceDirection);
		if (particle.Type == PARTICLE_FROST_CRYSTAL && particle.BounceCount > 0u &&
			ParticleImpulses[i].Type == IMPULSE_DIRECTIONAL && ParticleImpulses[i].Duration <= 0.19f)
			envelope = 0.0f;
		particle.Velocity += forceDirection * envelope * (420.0f * dt);
	}
	// Gentle, phase-varied lateral air motion gives low-energy hearth and torch
	// embers a flutter without perturbing fast combat sparks or their trajectories.
	float speedSqBeforeForces = dot(particle.Velocity, particle.Velocity);
	if ((particle.Type == PARTICLE_SPARK || particle.Type == PARTICLE_HOT_STREAK) && speedSqBeforeForces < 62500.0f)
	{
		float phase = dot(particle.Position, float3(0.017f, 0.023f, 0.011f)) + particle.Rotation;
		float flutter = 22.0f + 18.0f * sin(particle.Age * 5.1f + phase * 1.7f);
		particle.Velocity.xy += float2(cos(phase + particle.Age * 2.8f), sin(phase * 1.31f - particle.Age * 2.2f)) * flutter * dt;
	}

	bool initialStaffJet = (particle.Flags & 256u) != 0u && particle.BounceCount == 0u && particle.Age < 0.5f;
	particle.Velocity += particle.Acceleration * dt * (initialStaffJet ? 0.15f : 1.0f);
	particle.Velocity *= exp2(-max(particle.Drag, 0.0f) * dt * 1.442695f);
	float3 previousPosition = particle.Position;
	particle.Position += particle.Velocity * dt;
	particle.Rotation += particle.AngularVelocity * dt;

	float3 cameraRelative = particle.Position - FrameBuffer::CameraPosAdjust.xyz;
	float cameraDistance = length(cameraRelative);
	if (!isfinite(cameraDistance) || cameraDistance > MaximumDistance)
	{
		particle.Age = particle.Lifetime;
		ParticleState[dispatchID.x] = particle;
		return;
	}

	uint collisionStride = max(
		(ParticleCapacity + max(CollisionBudget, 1u) - 1u) / max(CollisionBudget, 1u),
		1u);
	bool frostSettleCandidate = particle.Type == PARTICLE_FROST_CRYSTAL &&
		(particle.Flags & PARTICLE_SETTLE_CANDIDATE) != 0u;
	bool collisionParticle = (particle.Flags & 2u) != 0u &&
		CollisionEnabled > 0.5f && CollisionBudget > 0u &&
		(((dispatchID.x + CollisionPhase) % collisionStride) == 0u || particle.Type == PARTICLE_FROST_CRYSTAL) &&
		(particle.BounceCount < particle.MaxBounces || particle.Type == PARTICLE_FROST_CRYSTAL);
	if (collisionParticle)
	{
		// Test four points along the swept segment. A single end-point
		// depth test can tunnel through thin fences, stairs and weapon-height
		// ledges when a spark receives a large impulse in a long frame.
		[unroll]
		for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
		{
			float sampleT = (sampleIndex + 1u) * 0.25f;
			float3 samplePosition = lerp(previousPosition, particle.Position, sampleT);
			float3 sampleRelative = samplePosition - FrameBuffer::CameraPosAdjust.xyz;
			float4 clip = mul(FrameBuffer::CameraViewProj, float4(sampleRelative, 1.0f));
			if (all(isfinite(clip)) && clip.w > 1.0e-5f)
			{
				float3 ndc = clip.xyz / clip.w;
				float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
				if (ndc.z >= 0.0f && ndc.z <= 1.0f && !FrameBuffer::IsOutsideFrame(uv))
				{
					int2 pixel = clamp(int2(uv * RenderSize), 0, int2(RenderSize) - 1);
					float rawDepth = CollisionDepth.Load(int3(pixel, 0));
					if (rawDepth < 0.999999f)
					{
						float sceneDepth = SharedData::GetScreenDepth(rawDepth);
						float particleDepth = SharedData::GetScreenDepth(ndc.z);
						float displacement = length(particle.Position - previousPosition);
						float thickness = max(particle.CollisionThickness, 1.0f);
						float depthGap = particleDepth - sceneDepth;
						float allowedGap = thickness + min(displacement * 1.25f, 96.0f);
						if (isfinite(sceneDepth) && isfinite(particleDepth) &&
							depthGap >= -thickness && depthGap <= allowedGap)
						{
							float4 normalRoughness = CollisionNormalRoughness.Load(int3(pixel, 0));
							float roughness = saturate(1.0f - normalRoughness.z);
							float3 normalVS = GBuffer::DecodeNormal(normalRoughness.xy);
							float3 normalWS = SafeDirection(
								mul((float3x3)FrameBuffer::CameraViewInverse, normalVS),
								float3(0, 0, 1));
							float incoming = dot(particle.Velocity, normalWS);
							if (incoming < 0.0f)
							{
								// Recover the surface itself; offsetting an already penetrated
								// sample can leave the shard underneath the floor.
								float2 surfaceUV = (float2(pixel) + 0.5f) / RenderSize;
								float4 surface = mul(FrameBuffer::CameraViewProjInverse,
									float4(surfaceUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), rawDepth, 1.0f));
								if (!all(isfinite(surface)) || abs(surface.w) < 1.0e-6f) continue;
								particle.Position = surface.xyz / surface.w + FrameBuffer::CameraPosAdjust.xyz +
									normalWS * max(particle.CollisionThickness, 1.0f);
								float3 normalVelocity = normalWS * incoming;
								float3 tangentVelocity = particle.Velocity - normalVelocity;
							float friction = saturate(particle.Friction + (roughness - 0.5f) * 0.18f);
							float restitution = saturate(particle.Restitution + (0.5f - roughness) * 0.10f);
							particle.Velocity = tangentVelocity * (1.0f - friction) - normalVelocity * restitution;
							float tangentEnergy = length(tangentVelocity);
							if (particle.Type == PARTICLE_FRAGMENT || particle.Type == PARTICLE_LEAF_CARD || particle.Type == PARTICLE_FROST_CRYSTAL)
								particle.AngularVelocity += clamp(tangentEnergy * 0.035f, -14.0f, 14.0f);
							particle.AngularVelocity *= lerp(0.78f, 0.66f, roughness);
							particle.BounceCount++;
			if (particle.Type == PARTICLE_FROST_CRYSTAL)
			{
				// Ice rebounds freely from walls. Only floor-like contacts can
				// begin the settle timer for the temporary screen-space shard.
				bool floorContact = normalWS.z > 0.62f;
				float postImpactSpeedSq = dot(particle.Velocity, particle.Velocity);
				if (floorContact && postImpactSpeedSq < 40000.0f)
					particle.Flags |= PARTICLE_SETTLE_CANDIDATE;
				else
					particle.Flags &= ~(PARTICLE_SETTLE_CANDIDATE | PARTICLE_QUIET_HIT_MASK);
			}
							if (dot(particle.Velocity, particle.Velocity) < 144.0f) {
								particle.Drag = max(particle.Drag, 5.0f);
								particle.AngularVelocity *= 0.72f;
							}
								break;
							}
						}
					}
				}
			}
		}
	}
	if (particle.Type == PARTICLE_FROST_CRYSTAL)
	{
		uint quietFrames = (particle.Flags & PARTICLE_QUIET_HIT_MASK) >> 3u;
		float speedSq = dot(particle.Velocity, particle.Velocity);
		if ((particle.Flags & PARTICLE_SETTLE_CANDIDATE) != 0u && speedSq < 10000.0f)
			quietFrames = min(quietFrames + 1u, 31u);
		else
			quietFrames = 0u;

		particle.Flags &= ~PARTICLE_QUIET_HIT_MASK;
		particle.Flags |= quietFrames << 3u;
		if (quietFrames >= 4u)
		{
			particle.Velocity = 0.0f;
			particle.Acceleration = 0.0f;
			particle.Flags &= ~(2u | PARTICLE_SETTLE_CANDIDATE | PARTICLE_QUIET_HIT_MASK);
			particle.Lifetime = min(max(particle.Lifetime, particle.Age + 8.0f), 24.0f);
			particle.FadeOut = 2.4f;
			particle.AngularVelocity = 0.0f;
		}
	}
	ParticleState[dispatchID.x] = particle;
}

StructuredBuffer<Particle> RenderParticles : register(t0);
Texture2D<float> RenderDepth : register(t1);
RWTexture2D<uint> ParticleMask : register(u0);

uint EncodeParticle(float intensity, float3 color, uint type, bool additive, bool dynamic)
{
	uint encodedIntensity = (uint)round(saturate(sqrt(max(intensity, 0.0f) / 4.0f)) * 511.0f);
	uint3 encodedColor = (uint3)round(saturate(color) * 63.0f);
	return (encodedIntensity << 23u) | (encodedColor.r << 17u) |
		(encodedColor.g << 11u) | (encodedColor.b << 5u) |
		((type & 7u) << 2u) | (additive ? 2u : 0u) | (dynamic ? 1u : 0u);
}

[numthreads(64, 1, 1)]
void BuildMaskCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= ParticleCapacity) return;
	Particle particle = RenderParticles[dispatchID.x];
	if (particle.Lifetime <= 0.0f || particle.Age >= particle.Lifetime ||
		!all(isfinite(float4(particle.Position, particle.Age)))) return;
	float3 cameraRelative = particle.Position - FrameBuffer::CameraPosAdjust.xyz;
	float4 clip = mul(FrameBuffer::CameraViewProj, float4(cameraRelative, 1.0f));
	if (!all(isfinite(clip)) || clip.w <= 1.0e-5f) return;
	float3 ndc = clip.xyz / clip.w;
	if (ndc.z < 0.0f || ndc.z > 1.0f) return;
	float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
	if (FrameBuffer::IsOutsideFrame(uv)) return;
	float particleDepth = SharedData::GetScreenDepth(ndc.z);
	if (!isfinite(particleDepth) || particleDepth <= 0.0f || particleDepth > MaximumDistance) return;

	float typeRadiusScale = particle.Type == PARTICLE_SMOKE_DUST ? 1.65f :
		(particle.Type == PARTICLE_SOFT_PUFF ? 1.8f :
		(particle.Type == PARTICLE_FROST_CRYSTAL ? 1.25f :
		(particle.Type == PARTICLE_FRAGMENT || particle.Type == PARTICLE_LEAF_CARD ? 1.12f : 1.0f)));
	float radiusLimit = particle.Type == PARTICLE_SMOKE_DUST || particle.Type == PARTICLE_SOFT_PUFF ? 7.0f :
		(particle.Type == PARTICLE_FROST_CRYSTAL ? 6.0f : 4.5f);
	float renderPixelScale = rcp(clamp(DisplayScale, 0.5f, 4.0f));
	// Project the physical size at render resolution, then apply only display-space
	// bounds. Dividing the projection itself by DisplayScale made upscaled shards
	// shrink twice; the bounds below keep their minimum and maximum output footprint
	// stable as internal resolution changes.
	float projectedRadius = particle.Size / max(particleDepth, 1.0f) * RenderSize.y * 0.72f * typeRadiusScale;
	float frostSizeVariation = saturate((particle.Size - 1.6f) / 3.0f);
	float minimumDisplayRadius = particle.Type == PARTICLE_FROST_CRYSTAL
		? lerp(0.72f, 1.05f, frostSizeVariation) : 0.75f;
	float radiusPixels = clamp(
		max(projectedRadius, minimumDisplayRadius * renderPixelScale),
		0.0f,
		radiusLimit * renderPixelScale);
	float subpixelCoverage = min(radiusPixels / 0.75f, 1.0f);
	float2 centre = uv * RenderSize;
	float2 streak = 0.0f;
	float streakLengthSq = 0.0f;
	bool streakParticle = particle.Type == PARTICLE_SPARK || particle.Type == PARTICLE_HOT_STREAK;
	if (streakParticle && dot(particle.Velocity, particle.Velocity) > (particle.Type == PARTICLE_HOT_STREAK ? 9000.0f : 40000.0f)) {
		float trailTime = particle.Type == PARTICLE_HOT_STREAK ? 0.034f : 0.018f;
		float4 trailClip = mul(FrameBuffer::CameraViewProj,
			float4(particle.Position - particle.Velocity * trailTime - FrameBuffer::CameraPosAdjust.xyz, 1.0f));
		if (all(isfinite(trailClip)) && trailClip.w > 1.0e-5f) {
			float2 trailUV = (trailClip.xy / trailClip.w) * float2(0.5f, -0.5f) + 0.5f;
			streak = clamp((trailUV - uv) * RenderSize, -12.0f * renderPixelScale, 12.0f * renderPixelScale);
			streakLengthSq = dot(streak, streak);
			radiusPixels = min(radiusPixels, (particle.Type == PARTICLE_HOT_STREAK ? 3.1f : 2.2f) * renderPixelScale);
			subpixelCoverage = min(radiusPixels / 0.75f, 1.0f);
		}
	}

	float age01 = saturate(particle.Age / max(particle.Lifetime, 1.0e-4f));
	float fadeIn = particle.FadeIn > 1.0e-4f ? saturate(particle.Age / particle.FadeIn) : 1.0f;
	float remaining = particle.Lifetime - particle.Age;
	float phaseSeed = dot(particle.Position, float3(0.013f, 0.019f, 0.023f)) + particle.Rotation;
	float fadeOut = particle.FadeOut > 1.0e-4f ? saturate(remaining / particle.FadeOut) : 1.0f;
	float emissiveCurve = streakParticle ? 0.88f + 0.22f * (0.5f + 0.5f *
		sin(particle.Rotation * 1.7f + particle.Age * 31.0f)) : 1.0f;
	if (particle.Type == PARTICLE_MAGIC_MOTE)
		emissiveCurve *= 0.72f + 0.28f * (0.5f + 0.5f * sin(particle.Age * 5.5f + phaseSeed));
	bool isFire = particle.Type == PARTICLE_SPARK || particle.Type == PARTICLE_HOT_STREAK;
	if (isFire) emissiveCurve *= smoothstep(0.0f, 0.08f, age01) *
		(1.0f - 0.62f * smoothstep(0.48f, 1.0f, age01));
	float typeEnergy = particle.Type == PARTICLE_SPARK ? 1.18f :
		(particle.Type == PARTICLE_HOT_STREAK ? 1.12f :
		(particle.Type == PARTICLE_FROST_CRYSTAL || particle.Type == PARTICLE_MAGIC_MOTE ? 1.08f :
		(particle.Type == PARTICLE_SMOKE_DUST || particle.Type == PARTICLE_SOFT_PUFF ? 0.88f : 1.0f)));
	float3 color = max(particle.ColorEmission.rgb, 0.0f);
	if (particle.Type == PARTICLE_FROST_CRYSTAL) {
		// A cool clear-to-milky shift as the settled shard approaches its melt fade.
		float melt = smoothstep(0.58f, 1.0f, age01);
		color = lerp(color, float3(0.60f, 0.78f, 0.88f), melt * 0.42f);
	}
	if (isFire) color = lerp(color, lerp(float3(1.0f, 0.08f, 0.005f), float3(1.0f, 0.72f, 0.12f),
		1.0f - smoothstep(0.16f, 0.78f, age01)), 0.72f);
	if (DebugMode == 1u) color = float3(0.1f, 0.8f, 1.0f);
	else if (DebugMode == 2u && particle.BounceCount > 0u) color = float3(1.0f, 0.12f, 0.05f);
	else if (DebugMode == 4u) {
		const float3 classColors[8] = { float3(1.0f,0.28f,0.04f),float3(0.32f,0.76f,1.0f),
			float3(0.72f,0.68f,0.55f),float3(0.34f,0.34f,0.38f),float3(0.88f,0.96f,1.0f),
			float3(0.72f,0.30f,0.90f),float3(1.0f,0.20f,0.025f),float3(0.48f,0.30f,0.12f) };
		color = classColors[min(particle.Type, 7u)];
	}

	int2 basePixel = int2(floor(centre));
	int footprintScalar = (int)ceil(radiusPixels + min(length(streak), 12.0f) + 1.0f);
	int2 footprint = int2(footprintScalar, footprintScalar);
	footprint = min(footprint, int2(14, 14));
	[loop] for (int y = -footprint.y; y <= footprint.y; ++y) {
		[loop] for (int x = -footprint.x; x <= footprint.x; ++x) {
			int2 pixel = basePixel + int2(x, y);
			if (any(pixel < 0) || any(pixel >= int2(RenderSize))) continue;
			float2 offset = float2(pixel) + 0.5f - centre;
			float distanceSq = dot(offset, offset);
			if (streakLengthSq > 1.0e-4f) {
				float along = saturate(dot(offset, streak) / streakLengthSq);
				float2 nearest = streak * along;
				distanceSq = dot(offset - nearest, offset - nearest);
			}
			bool subpixelCenter = radiusPixels < 0.75f && x == 0 && y == 0;
			if (distanceSq > radiusPixels * radiusPixels && !subpixelCenter) continue;
			float angle = particle.Rotation;
			float2 axis = float2(cos(angle), sin(angle));
			float2 side = float2(-axis.y, axis.x);
			float facetResponse = 1.0f;
			float frostCoverage = 1.0f;
			float frostEdge = 0.5f;
			float2 frostNormal = axis * 0.45f;
			if (!subpixelCenter && particle.Type == PARTICLE_FRAGMENT) {
				float along = dot(offset, axis) / max(radiusPixels * 1.22f, 0.8f);
				float across = dot(offset, side) / max(radiusPixels * 0.48f, 0.42f);
				if (along * along + across * across > 1.0f) continue;
			} else if (!subpixelCenter && particle.Type == PARTICLE_LEAF_CARD) {
				float along = dot(offset, axis) / max(radiusPixels, 0.75f);
				float across = dot(offset, side) / max(radiusPixels * 0.42f, 0.45f);
				if (along * along + across * across > 1.0f) continue;
			} else if (!subpixelCenter && particle.Type == PARTICLE_FROST_CRYSTAL) {
				float aspect = lerp(0.42f, 0.82f, frac(particle.Size * 17.7f));
				float along = dot(offset, axis) / max(radiusPixels, 0.5f);
				float across = dot(offset, side) / max(radiusPixels * aspect, 0.4f);
				float facet = max(abs(along) * 0.88f + abs(across) * 0.5f, abs(across));
				frostCoverage = saturate((1.0f - facet) * max(radiusPixels, 0.75f) + 0.5f);
				if (frostCoverage <= 0.0f) continue;
				frostEdge = saturate(facet);
				frostNormal = abs(across) > 0.35f
					? side * (across > 0.0f ? 0.72f : -0.72f) + axis * 0.20f
					: axis * (along > 0.0f ? 0.42f : -0.42f) + side * 0.12f;
			}
			float rawDepth = RenderDepth.Load(int3(pixel, 0));
			float depthFade = 1.0f;
			if (rawDepth < 0.999999f) {
				float sceneDepth = SharedData::GetScreenDepth(rawDepth);
				float tolerance = max(particle.CollisionThickness, 6.0f);
				if (isfinite(sceneDepth) && sceneDepth + tolerance < particleDepth) continue;
				if (isfinite(sceneDepth)) depthFade = saturate((sceneDepth - particleDepth + tolerance) / tolerance);
			}
			float radialExponent = particle.Type == PARTICLE_SMOKE_DUST ? 1.05f :
				(particle.Type == PARTICLE_SOFT_PUFF ? 0.82f :
				(particle.Type == PARTICLE_MAGIC_MOTE ? 3.5f :
				(particle.Type == PARTICLE_FROST_CRYSTAL ? 1.85f :
				(particle.Type == PARTICLE_LEAF_CARD ? 1.8f : (streakParticle ? 3.0f : 2.4f)))));
			float radial = exp2(-radialExponent * distanceSq / max(radiusPixels * radiusPixels, 1.0f));
			if (particle.Type == PARTICLE_FROST_CRYSTAL)
				radial = frostCoverage;
			radial *= facetResponse;
			if (particle.Type == PARTICLE_SMOKE_DUST) {
				// Stable particle-space breakup avoids a uniform disc without flickering
				// animated screen-space noise into the temporal upscaler.
				float densityVariation = 0.88f + 0.12f * sin(phaseSeed * 2.7f + particle.Age * 1.3f);
				radial *= densityVariation;
			}
			if (particle.BounceCount > 0u && streakParticle) radial = max(radial, 0.58f * radial);
			if (particle.Type == PARTICLE_HOT_STREAK && streakLengthSq > 1.0e-4f)
				radial = max(radial * 0.82f, exp2(-3.2f * dot(offset, offset) / max(radiusPixels * radiusPixels * 0.18f, 0.7f)) * 1.18f);
			float intensity = radial * subpixelCoverage * fadeIn * fadeOut * depthFade * max(particle.ColorEmission.a, 0.0f) *
				typeEnergy * emissiveCurve * ParticleIntensity;
			bool settledFrost = particle.Type == PARTICLE_FROST_CRYSTAL && (particle.Flags & 2u) == 0u;
			bool dynamic = dot(particle.Velocity, particle.Velocity) > 40000.0f ||
				particle.Type == PARTICLE_SPARK || particle.Type == PARTICLE_HOT_STREAK ||
				(particle.Type == PARTICLE_FROST_CRYSTAL && !settledFrost);
			uint previous;
			// Frost stores a particle-local facet direction in the existing color
			// bits. CompositeCS uses it for refraction without background-locked noise.
			float3 encodedColor = color;
			if (particle.Type == PARTICLE_FROST_CRYSTAL && DebugMode == 0u) {
				encodedColor = float3(frostNormal * 0.5f + 0.5f, frostEdge);
			}
			InterlockedMax(ParticleMask[pixel], EncodeParticle(intensity, encodedColor, particle.Type,
				(particle.Flags & 1u) != 0u, dynamic), previous);
		}
	}
}

Texture2D<uint> CompositeMask : register(t0);
Texture2D<float> CompositeDepth : register(t1);
Texture2D<float4> SceneColorSource : register(t3);
RWTexture2D<float4> SceneColor : register(u0);

[numthreads(8, 8, 1)]
void CompositeCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (any(dispatchID.xy >= uint2(RenderSize)))
		return;
	uint packed = CompositeMask[dispatchID.xy];
	if (packed == 0u && DebugMode != 3u)
		return;

	float4 scene = SceneColor[dispatchID.xy];
	if (DebugMode == 3u)
	{
		float rawDepth = CompositeDepth.Load(int3(dispatchID.xy, 0));
		if (rawDepth < 0.999999f)
		{
			float2 uv = (float2(dispatchID.xy) + 0.5f) * InvRenderSize;
			float4 clip = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), rawDepth, 1.0f);
			float4 reconstructed = mul(FrameBuffer::CameraViewProjInverse, clip);
			if (abs(reconstructed.w) > 1.0e-6f && all(isfinite(reconstructed)))
			{
				float3 absolutePosition = reconstructed.xyz / reconstructed.w + FrameBuffer::CameraPosAdjust.xyz;
				float3 accumulatedForce = 0.0f;
				[loop]
				for (uint i = 0u; i < ActiveImpulseCount; ++i)
				{
					float3 forceDirection;
					float envelope = ImpulseEnvelope(ParticleImpulses[i], absolutePosition, forceDirection);
					accumulatedForce += forceDirection * envelope;
				}
				float magnitude = length(accumulatedForce);
				if (isfinite(magnitude) && magnitude > 1.0e-4f)
				{
					float3 direction = SafeDirection(accumulatedForce, float3(0, 0, 1));
					float3 debugColor = direction * 0.5f + 0.5f;
					scene.rgb = lerp(scene.rgb, debugColor, saturate(magnitude * 0.35f) * 0.72f);
				}
			}
		}
	}
	if (packed == 0u)
	{
		SceneColor[dispatchID.xy] = scene;
		return;
	}
	float intensity = pow(float((packed >> 23u) & 511u) / 511.0f, 2.0f) * 4.0f;
	float3 color = float3(
		(packed >> 17u) & 63u,
		(packed >> 11u) & 63u,
		(packed >> 5u) & 63u) / 63.0f;
	uint particleType = (packed >> 2u) & 7u;
	bool additive = (packed & 2u) != 0u;
	bool dynamic = (packed & 1u) != 0u;
	float renderPixelScale = rcp(clamp(DisplayScale, 0.5f, 4.0f));
	int2 scenePixel = int2(dispatchID.xy);
	if (particleType == PARTICLE_FROST_CRYSTAL && DebugMode == 0u)
	{
		float coverage = saturate(intensity * 1.35f);
		float2 facetNormal = color.xy * 2.0f - 1.0f;
		float edge = smoothstep(0.35f, 1.0f, color.z);
		float3 transmitted = scene.rgb;
		if (OpticalActive > 0.5f) {
			int2 offset = int2(round(facetNormal * (2.5f + edge * 2.0f) * renderPixelScale));
			int2 samplePixel = clamp(scenePixel + offset, int2(0, 0), int2(RenderSize) - 1);
			float centerDepth = CompositeDepth.Load(int3(scenePixel, 0));
			float offsetDepth = CompositeDepth.Load(int3(samplePixel, 0));
			// Avoid pulling foreground silhouettes into a refracted background shard.
			if (offsetDepth >= centerDepth - 0.0001f)
				transmitted = SceneColorSource.Load(int3(samplePixel, 0)).rgb;
		}
		float sceneLuma = dot(max(scene.rgb, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		float3 lightColor = max(SharedData::DirLightColor.xyz, 0.0f);
		float fresnel = 0.035f + 0.32f * pow(edge, 3.0f);
		float facet = 0.25f + 0.75f * saturate(-facetNormal.y * 0.7f + facetNormal.x * 0.3f);
		float3 normalVS = normalize(float3(facetNormal, -0.75f));
		float3 normalWS = mul((float3x3)FrameBuffer::CameraViewInverse, normalVS);
		float3 ambient = max(SharedData::GetAmbient(normalWS), 0.0f);
		float3 lightDirection = SafeDirection(SharedData::DirLightDirection.xyz, float3(0, 0, 1));
		float faceLighting = saturate(dot(normalWS, lightDirection));
		// Ambient SH and directional irradiance light the material. Scene color
		// supplies a local colored-light approximation (including warm interiors),
		// not a fixed brightness floor or emission term.
		float3 bodyLight = ambient * 0.65f + lightColor * (faceLighting * 0.25f) + max(scene.rgb, 0.0f) * 0.45f;
		float3 frostBody = bodyLight * float3(0.80f, 0.91f, 1.10f);
		float3 halfVector = SafeDirection(lightDirection + mul((float3x3)FrameBuffer::CameraViewInverse, float3(0, 0, -1)), normalWS);
		float glint = pow(saturate(dot(normalWS, halfVector)), 20.0f);
		float3 reflection = (scene.rgb * 1.15f + bodyLight * 1.8f) * facet + lightColor * glint * 0.35f;
		float3 ice = transmitted * float3(0.84f, 0.94f, 1.08f) * (1.0f - edge * 0.10f);
		ice = lerp(ice, frostBody, 0.24f + edge * 0.24f);
		ice = lerp(ice, reflection, fresnel) + bodyLight * (0.08f * facet) + lightColor * glint * 0.18f;
		SceneColor[dispatchID.xy] = float4(lerp(scene.rgb, ice, coverage), scene.a);
		return;
	}
	if (particleType == PARTICLE_HOT_STREAK && additive && intensity > 0.035f && OpticalActive > 0.5f)
	{
		// Anchor the small heat shimmer to reconstructed scene position so camera
		// panning does not drag a screen-space distortion pattern across the world.
		float phase = 0.0f;
		float rawDepth = CompositeDepth.Load(int3(scenePixel, 0));
		if (rawDepth < 0.999999f) {
			float2 uv = (float2(scenePixel) + 0.5f) * InvRenderSize;
			float4 clip = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), rawDepth, 1.0f);
			float4 reconstructed = mul(FrameBuffer::CameraViewProjInverse, clip);
			if (abs(reconstructed.w) > 1.0e-6f && all(isfinite(reconstructed))) {
				float3 worldPoint = reconstructed.xyz / reconstructed.w + FrameBuffer::CameraPosAdjust.xyz;
				phase = dot(worldPoint, float3(0.013f, 0.021f, 0.017f));
			}
		}
		float2 flow = float2(
			sin(phase * 1.7f + SharedData::Timer * 1.2f),
			cos(phase * 1.3f - SharedData::Timer * 0.9f));
		int2 heatOffset = int2(round(flow * (1.0f + 2.0f * saturate(intensity)) * renderPixelScale));
		int2 heatPixel = clamp(scenePixel + heatOffset, int2(0, 0), int2(RenderSize) - 1);
		float3 displaced = SceneColorSource.Load(int3(heatPixel, 0)).rgb;
		scene.rgb = lerp(scene.rgb, displaced, saturate(intensity * 0.42f));
	}
	if (particleType == PARTICLE_SMOKE_DUST)
	{
		float density = saturate(intensity * 0.32f);
		float sceneLuma = dot(max(scene.rgb, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		float3 smokeLighting = max(SharedData::GetAmbient(float3(0, 0, 1)), 0.0f) * 0.35f + max(scene.rgb, 0.0f) * 0.65f;
		float3 inScatter = color * smokeLighting * (0.12f + 0.28f * density);
		scene.rgb = scene.rgb * (1.0f - density * 0.58f) + inScatter;
	}
	else if (particleType == PARTICLE_SOFT_PUFF)
	{
		float opacity = saturate(intensity * 0.26f);
		scene.rgb = lerp(scene.rgb, scene.rgb * (0.72f + color * 0.28f), opacity);
	}
	else if (additive)
	{
		// Keep HDR energy bounded per layer. PIXL bloom/tonemap remains responsible
		// for the final highlight footprint rather than Reactive FX acting as bloom.
		scene.rgb += color * intensity * 1.35f;
	}
	else
	{
		float opacity = saturate(intensity * 0.46f);
		float sceneLuma = dot(max(scene.rgb, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		float colorLuma = max(dot(color, float3(0.2126f, 0.7152f, 0.0722f)), 0.08f);
		float3 materialLighting = max(SharedData::GetAmbient(float3(0, 0, 1)), 0.0f) * 0.35f + max(scene.rgb, 0.0f) * 0.65f;
		float3 energyMatchedColor = color * materialLighting / colorLuma;
		float materialResponse = particleType == PARTICLE_FRAGMENT || particleType == PARTICLE_LEAF_CARD ? 0.72f : 1.0f;
		scene.rgb = lerp(scene.rgb, energyMatchedColor, opacity * materialResponse);
	}
	SceneColor[dispatchID.xy] = scene;
}

Texture2D<uint> ReconstructionParticleMask : register(t0);
RWTexture2D<float> ReconstructionReactiveMask : register(u0);

[numthreads(8, 8, 1)]
void WriteReactiveMaskCS(uint3 dispatchID : SV_DispatchThreadID)
{
	uint sourceWidth, sourceHeight;
	uint targetWidth, targetHeight;
	ReconstructionParticleMask.GetDimensions(sourceWidth, sourceHeight);
	ReconstructionReactiveMask.GetDimensions(targetWidth, targetHeight);
	if (any(dispatchID.xy >= uint2(targetWidth, targetHeight)) || sourceWidth == 0u || sourceHeight == 0u)
		return;

	float2 sourceScale = float2(sourceWidth, sourceHeight) / float2(targetWidth, targetHeight);
	int2 sourcePixel = clamp(
		int2((float2(dispatchID.xy) + 0.5f) * sourceScale),
		int2(0, 0),
		int2(sourceWidth, sourceHeight) - 1);
	uint strongest = ReconstructionParticleMask.Load(int3(sourcePixel, 0));
	int2 sourceMax = int2(sourceWidth, sourceHeight) - 1;
	strongest = max(strongest, ReconstructionParticleMask.Load(int3(clamp(sourcePixel + int2(-1, 0), int2(0, 0), sourceMax), 0)));
	strongest = max(strongest, ReconstructionParticleMask.Load(int3(clamp(sourcePixel + int2(1, 0), int2(0, 0), sourceMax), 0)));
	strongest = max(strongest, ReconstructionParticleMask.Load(int3(clamp(sourcePixel + int2(0, -1), int2(0, 0), sourceMax), 0)));
	strongest = max(strongest, ReconstructionParticleMask.Load(int3(clamp(sourcePixel + int2(0, 1), int2(0, 0), sourceMax), 0)));
	if (strongest != 0u)
	{
		float coverage = pow(float((strongest >> 23u) & 511u) / 511.0f, 2.0f) * 4.0f;
		uint particleType = (strongest >> 2u) & 7u;
		bool dynamic = (strongest & 1u) != 0u;
		float response = particleType == PARTICLE_HOT_STREAK || particleType == PARTICLE_SPARK ? 1.0f :
			(particleType == PARTICLE_FROST_CRYSTAL || particleType == PARTICLE_MAGIC_MOTE ? 0.88f :
			(particleType == PARTICLE_SMOKE_DUST ? 0.58f : (particleType == PARTICLE_SOFT_PUFF ? 0.48f : 0.68f)));
		if (dynamic) response = max(response, 0.78f);
		float existing = ReconstructionReactiveMask[dispatchID.xy];
		ReconstructionReactiveMask[dispatchID.xy] = max(existing, saturate(coverage * response * 0.72f + 0.05f));
	}
}
