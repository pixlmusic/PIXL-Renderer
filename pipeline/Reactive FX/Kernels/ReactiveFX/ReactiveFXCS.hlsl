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
	float Gravity;
	float ParticleIntensity;
	float MaximumDistance;
	uint ParticleCapacity;
	uint SpawnCount;
	uint CollisionBudget;
	uint DebugMode;
	float CollisionEnabled;
	float Reserved0;
	float2 ReactivePadding;
};

StructuredBuffer<SpawnCommand> SpawnCommands : register(t0);
RWStructuredBuffer<Particle> ParticleState : register(u0);

[numthreads(64, 1, 1)]
void SpawnCS(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= SpawnCount)
		return;
	SpawnCommand command = SpawnCommands[dispatchID.x];
	if (command.Slot < ParticleCapacity)
		ParticleState[command.Slot] = command.Value;
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
	if (impulse.Type == 2u)
	{
		float front = impulse.Age * impulse.WaveSpeed;
		float width = max(impulse.Radius * 0.12f, 48.0f);
		float wave = exp2(-4.0f * abs(distanceToOrigin - front) / width);
		float3 radial = SafeDirection(offset, SafeDirection(impulse.Direction, float3(1, 0, 0)));
		forceDirection = SafeDirection(lerp(radial, impulse.Direction, 0.72f), radial);
		return wave * decay * impulse.Strength;
	}
	float radialFalloff = pow(saturate(1.0f - distanceToOrigin / impulse.Radius), max(impulse.Falloff, 0.25f));
	float3 radialDirection = SafeDirection(offset, float3(0, 0, 1));
	forceDirection = impulse.Type == 1u
		? SafeDirection(lerp(radialDirection, impulse.Direction, 0.78f), radialDirection)
		: radialDirection;
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

	[unroll]
	for (uint i = 0u; i < 16u; ++i)
	{
		float3 forceDirection;
		float envelope = ImpulseEnvelope(ParticleImpulses[i], particle.Position, forceDirection);
		particle.Velocity += forceDirection * envelope * (420.0f * dt);
	}

	particle.Velocity += particle.Acceleration * dt;
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
	bool collisionParticle = (particle.Flags & 2u) != 0u &&
		CollisionEnabled > 0.5f && CollisionBudget > 0u &&
		(dispatchID.x % collisionStride) == 0u &&
		particle.BounceCount < particle.MaxBounces;
	if (collisionParticle)
	{
		// Test the current point and one swept midpoint. A single end-point
		// depth test can tunnel through thin fences, stairs and weapon-height
		// ledges when a spark receives a large impulse in a long frame.
		[unroll]
		for (uint sampleIndex = 0u; sampleIndex < 2u; ++sampleIndex)
		{
			float sampleT = sampleIndex == 0u ? 0.5f : 1.0f;
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
						if (isfinite(sceneDepth) && isfinite(particleDepth) &&
							particleDepth >= sceneDepth - max(particle.CollisionThickness, 1.0f))
						{
							float3 normalVS = GBuffer::DecodeNormal(CollisionNormalRoughness.Load(int3(pixel, 0)).xy);
							float3 normalWS = SafeDirection(
								mul((float3x3)FrameBuffer::CameraViewInverse, normalVS),
								float3(0, 0, 1));
							float incoming = dot(particle.Velocity, normalWS);
							if (incoming < 0.0f)
							{
								particle.Position = samplePosition + normalWS * max(particle.CollisionThickness, 1.0f);
								float3 normalVelocity = normalWS * incoming;
								float3 tangentVelocity = particle.Velocity - normalVelocity;
								particle.Velocity =
									tangentVelocity * saturate(1.0f - particle.Friction) -
									normalVelocity * saturate(particle.Restitution);
								particle.AngularVelocity *= 0.65f;
								particle.BounceCount++;
								if (dot(particle.Velocity, particle.Velocity) < 144.0f)
									particle.Drag = max(particle.Drag, 5.0f);
								break;
							}
						}
					}
				}
			}
		}
	}
	ParticleState[dispatchID.x] = particle;
}

StructuredBuffer<Particle> RenderParticles : register(t0);
Texture2D<float> RenderDepth : register(t1);
RWTexture2D<uint> ParticleMask : register(u0);

uint EncodeParticle(float intensity, float3 color, bool additive)
{
	uint encodedIntensity = (uint)round(saturate(intensity) * 1023.0f);
	uint3 encodedColor = (uint3)round(saturate(color) * 127.0f);
	return (additive ? 0x80000000u : 0u) |
		(encodedIntensity << 21u) | (encodedColor.r << 14u) |
		(encodedColor.g << 7u) | encodedColor.b;
}

[numthreads(8, 8, 1)]
void BuildMaskCS(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID)
{
	if (groupID.x >= ParticleCapacity)
		return;
	Particle particle = RenderParticles[groupID.x];
	if (particle.Lifetime <= 0.0f || particle.Age >= particle.Lifetime ||
		!all(isfinite(float4(particle.Position, particle.Age))))
		return;

	float3 cameraRelative = particle.Position - FrameBuffer::CameraPosAdjust.xyz;
	float4 clip = mul(FrameBuffer::CameraViewProj, float4(cameraRelative, 1.0f));
	if (!all(isfinite(clip)) || clip.w <= 1.0e-5f)
		return;
	float3 ndc = clip.xyz / clip.w;
	if (ndc.z < 0.0f || ndc.z > 1.0f)
		return;
	float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
	if (FrameBuffer::IsOutsideFrame(uv))
		return;
	float particleDepth = SharedData::GetScreenDepth(ndc.z);
	if (!isfinite(particleDepth) || particleDepth <= 0.0f || particleDepth > MaximumDistance)
		return;

	float projectedRadius = particle.Size / max(particleDepth, 1.0f) * RenderSize.y * 0.72f;
	// Keep the per-particle raster footprint bounded. Continuous concentration
	// spells can keep this pass active for minutes, so empty-slot work must not
	// become a multi-million-thread dispatch every frame.
	float radiusPixels = clamp(projectedRadius, 0.75f, 4.0f);
	float2 centre = uv * RenderSize;
	int2 pixel = int2(floor(centre)) + int2(threadID.xy) - 4;
	float2 offset = float2(pixel) + 0.5f - centre;
	float distanceSq = dot(offset, offset);
	float2 streak = float2(0.0f, 0.0f);
	float streakLengthSq = 0.0f;
	if ((particle.Type == 0u || particle.Type == 6u) &&
		dot(particle.Velocity, particle.Velocity) > (particle.Type == 6u ? 9000.0f : 40000.0f))
	{
		// Fire gets a longer, sharper screen-space streak than metallic sparks.
		// The segment is projected from the current particle position rather than
		// faking a second circular sprite, which removes the bubbly appearance.
		float trailTime = particle.Type == 6u ? 0.034f : 0.018f;
		float3 trailPosition = particle.Position - particle.Velocity * trailTime;
		float3 trailRelative = trailPosition - FrameBuffer::CameraPosAdjust.xyz;
		float4 trailClip = mul(FrameBuffer::CameraViewProj, float4(trailRelative, 1.0f));
		if (all(isfinite(trailClip)) && trailClip.w > 1.0e-5f)
		{
			float2 trailNdc = trailClip.xy / trailClip.w;
			float2 trailUV = trailNdc * float2(0.5f, -0.5f) + 0.5f;
			streak = clamp((trailUV - uv) * RenderSize, -12.0f, 12.0f);
			streakLengthSq = dot(streak, streak);
			float along = streakLengthSq > 1.0e-4f
				? saturate(dot(offset, streak) / streakLengthSq)
				: 0.0f;
			float2 nearest = streak * along;
			distanceSq = dot(offset - nearest, offset - nearest);
			radiusPixels = min(radiusPixels, particle.Type == 6u ? 3.1f : 2.2f);
		}
	}
	if (distanceSq > radiusPixels * radiusPixels)
		return;
	if (particle.Type == 2u)
	{
		// Chips and metal fragments use a narrow oriented shard footprint rather
		// than a circular point. Rotation is integrated in the particle pool, so
		// the fragment tumbles while retaining a readable silhouette.
		float angle = particle.Rotation;
		float2 axis = float2(cos(angle), sin(angle));
		float2 side = float2(-axis.y, axis.x);
		float along = dot(offset, axis) / max(radiusPixels * 1.22f, 0.8f);
		float across = dot(offset, side) / max(radiusPixels * 0.48f, 0.42f);
		if (along * along + across * across > 1.0f)
			return;
	}
	if (particle.Type == 7u)
	{
		// Leaf cards are elongated and rotated by their particle state. This keeps
		// the settled population from reading as a second layer of round sprites.
		float angle = particle.Rotation;
		float2 axis = float2(cos(angle), sin(angle));
		float2 side = float2(-axis.y, axis.x);
		float along = dot(offset, axis) / max(radiusPixels, 0.75f);
		float across = dot(offset, side) / max(radiusPixels * 0.42f, 0.45f);
		if (along * along + across * across > 1.0f)
			return;
	}
	if (particle.Type == 1u)
	{
		// Frost is crystalline rather than a round billboard.  A rotated,
		// lightly bevelled diamond/cube footprint gives the small shard a readable
		// faceted silhouette while remaining compatible with the packed mask path.
		float angle = particle.Rotation;
		float2 axis = float2(cos(angle), sin(angle));
		float2 side = float2(-axis.y, axis.x);
		float along = abs(dot(offset, axis)) / max(radiusPixels * 0.92f, 0.7f);
		float across = abs(dot(offset, side)) / max(radiusPixels * 0.72f, 0.6f);
		float bevel = smoothstep(0.72f, 1.0f, max(along, across));
		if (along + across > 1.24f || (bevel > 0.0f && along + across > 1.08f))
			return;
	}
	if (any(pixel < 0) || any(pixel >= int2(RenderSize)))
		return;

	float rawDepth = RenderDepth.Load(int3(pixel, 0));
	float depthFade = 1.0f;
	if (rawDepth < 0.999999f)
	{
		float sceneDepth = SharedData::GetScreenDepth(rawDepth);
		float tolerance = max(particle.CollisionThickness, 6.0f);
		if (isfinite(sceneDepth) && sceneDepth + tolerance < particleDepth)
			return;
		if (isfinite(sceneDepth))
			depthFade = saturate((sceneDepth - particleDepth + tolerance) / tolerance);
	}

	float age01 = saturate(particle.Age / max(particle.Lifetime, 1.0e-4f));
	float fadeIn = particle.FadeIn > 1.0e-4f ? saturate(particle.Age / particle.FadeIn) : 1.0f;
	float remaining = particle.Lifetime - particle.Age;
	float fadeOut = particle.FadeOut > 1.0e-4f ? saturate(remaining / particle.FadeOut) : 1.0f;
	float emissiveCurve = 1.0f;
	if (particle.Type == 0u || particle.Type == 6u)
	{
		// Stable per-particle glint: the phase comes from integrated rotation, not
		// frame noise, so sparks shimmer without temporal crawling under TAA/DLSS.
		emissiveCurve = 0.88f + 0.22f * (0.5f + 0.5f * sin(particle.Rotation * 1.7f + particle.Age * 31.0f));
	}
	// Warm low-green particles are fire/embers; metal sparks retain their authored
	// orange-white colour and do not receive the ember lifecycle.  The bell curve
	// removes the hard billboard pop, grows the hot core naturally, and lets the
	// existing fade-out handle the final transparent ember stage.
	bool isFire = (particle.Type == 0u || particle.Type == 6u) &&
		particle.ColorEmission.g < 0.45f && particle.ColorEmission.b < 0.18f;
	float fireBell = 4.0f * age01 * (1.0f - age01);
	if (isFire)
	{
		emissiveCurve *= smoothstep(0.0f, 0.12f, age01) * (0.72f + 0.78f * fireBell);
	}
	float radialExponent = particle.Type == 3u ? 1.35f : (particle.Type == 7u ? 1.8f : 2.4f);
	float radial = exp2(-radialExponent * distanceSq / max(radiusPixels * radiusPixels, 1.0f));
	if (particle.BounceCount > 0u && (particle.Type == 0u || particle.Type == 6u))
	{
		// A bounded forked lobe sells a spark splitting on impact without a second
		// allocation or UAV race in the fixed-size particle pool.  It is deliberately
		// short-lived and weaker than the parent spark, so it cannot become a hidden
		// particle multiplier during sustained combat.
		float2 splitAxis = streakLengthSq > 1.0e-4f
			? normalize(streak)
			: float2(cos(particle.Rotation), sin(particle.Rotation));
		float2 splitCentre = float2(-splitAxis.y, splitAxis.x) * min(radiusPixels * 0.72f, 1.6f);
		float splitRadius = max(radiusPixels * 0.58f, 0.65f);
		float splitDistanceSq = dot(offset - splitCentre, offset - splitCentre);
		float splitLobe = exp2(-2.6f * splitDistanceSq / (splitRadius * splitRadius));
		radial = max(radial, splitLobe * 0.58f);
	}
	if (particle.Type == 6u && streakLengthSq > 1.0e-4f)
	{
		// Add a compact incandescent core without returning to a large circular
		// blob. The core is deliberately bounded so bloom receives a highlight,
		// not a full opaque fireball.
		float coreRadius = max(radiusPixels * 0.42f, 0.85f);
		float core = exp2(-3.2f * dot(offset, offset) / (coreRadius * coreRadius));
		radial = max(radial * 0.82f, core * 1.18f);
	}
	float typeEnergy = particle.Type == 0u ? 1.18f : (particle.Type == 6u ? 1.12f : 1.0f);
	float intensity = radial * fadeIn * fadeOut * depthFade * max(particle.ColorEmission.a, 0.0f) *
		typeEnergy * emissiveCurve * ParticleIntensity;
	float3 color = max(particle.ColorEmission.rgb, 0.0f);
	if (isFire)
	{
		float hot = smoothstep(0.16f, 0.58f, age01);
		float3 emberColor = lerp(float3(0.035f, 0.004f, 0.001f),
			lerp(float3(1.0f, 0.08f, 0.005f), float3(1.0f, 0.72f, 0.12f), hot),
			saturate(fireBell * 1.6f));
		color = lerp(color, emberColor, 0.72f);
	}
	if (DebugMode == 1u)
	{
		color = float3(0.1f, 0.8f, 1.0f);
		intensity = radial;
	}
	else if (DebugMode == 2u && particle.BounceCount > 0u)
	{
		color = float3(1.0f, 0.12f, 0.05f);
		intensity = radial;
	}
	else if (DebugMode == 4u)
	{
		const float3 classColors[8] = {
			float3(1.0f, 0.28f, 0.04f),
			float3(0.32f, 0.76f, 1.0f),
			float3(0.72f, 0.68f, 0.55f),
			float3(0.34f, 0.34f, 0.38f),
			float3(0.88f, 0.96f, 1.0f),
			float3(0.72f, 0.30f, 0.90f),
			float3(1.0f, 0.20f, 0.025f),
			float3(0.48f, 0.30f, 0.12f)
		};
		color = classColors[min(particle.Type, 7u)];
		intensity = radial;
	}
	uint encoded = EncodeParticle(intensity, color, (particle.Flags & 1u) != 0u);
	uint previous;
	InterlockedMax(ParticleMask[pixel], encoded, previous);
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
				[unroll]
				for (uint i = 0u; i < 16u; ++i)
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
	bool additive = (packed & 0x80000000u) != 0u;
	float intensity = float((packed >> 21u) & 1023u) / 1023.0f;
	float3 color = float3(
		(packed >> 14u) & 127u,
		(packed >> 7u) & 127u,
		packed & 127u) / 127.0f;
	int2 scenePixel = int2(dispatchID.xy);
	if (additive && color.r > color.g * 1.22f && intensity > 0.035f)
	{
		// Fire/heat distortion is sampled from a pre-FX scene copy. The offset is
		// stable in screen space and kept deliberately small so it reads as hot air,
		// not as a refractive post-process applied to the whole image.
		float2 flow = float2(
			sin(float(dispatchID.x) * 0.071f + SharedData::Timer * 2.1f),
			cos(float(dispatchID.y) * 0.053f + SharedData::Timer * 1.7f));
		int2 heatOffset = int2(round(flow * (1.0f + 2.0f * saturate(intensity))));
		int2 heatPixel = clamp(scenePixel + heatOffset, int2(0, 0), int2(RenderSize) - 1);
		float3 displaced = SceneColorSource.Load(int3(heatPixel, 0)).rgb;
		scene.rgb = lerp(scene.rgb, displaced, saturate(intensity * 0.42f));
	}
	else if (!additive && color.b > color.r * 1.18f && intensity > 0.025f)
	{
		// Frost shards receive a restrained screen-space refractive glint. This is
		// evaluated only where the packed particle is blue-dominant, so ordinary
		// debris and dust remain unaffected.
		float2 crystalAxis = float2(
			sin(float(dispatchID.x) * 0.113f + SharedData::Timer),
			cos(float(dispatchID.y) * 0.097f + SharedData::Timer * 0.73f));
		int2 crystalOffset = int2(round(crystalAxis * (1.0f + 3.0f * intensity)));
		int2 refractedPixelA = clamp(scenePixel + crystalOffset, int2(0, 0), int2(RenderSize) - 1);
		int2 refractedPixelB = clamp(scenePixel - crystalOffset, int2(0, 0), int2(RenderSize) - 1);
		float3 refractedA = SceneColorSource.Load(int3(refractedPixelA, 0)).rgb;
		float3 refractedB = SceneColorSource.Load(int3(refractedPixelB, 0)).rgb;
		scene.rgb = lerp(scene.rgb, (refractedA + refractedB) * 0.5f, saturate(intensity * 0.28f));
	}
	if (additive)
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
		float3 energyMatchedColor = color * (sceneLuma / colorLuma);
		scene.rgb = lerp(scene.rgb, energyMatchedColor, opacity);
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
	float coverage = 0.0f;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			int2 samplePixel = clamp(
				sourcePixel + int2(x, y),
				int2(0, 0),
				int2(sourceWidth, sourceHeight) - 1);
			uint packed = ReconstructionParticleMask.Load(int3(samplePixel, 0));
			float intensity = float((packed >> 21u) & 1023u) / 1023.0f;
			coverage = max(coverage, intensity);
		}
	}
	if (coverage > 0.0f)
	{
		float existing = ReconstructionReactiveMask[dispatchID.xy];
		ReconstructionReactiveMask[dispatchID.xy] = max(existing, saturate(coverage * 1.2f + 0.08f));
	}
}
