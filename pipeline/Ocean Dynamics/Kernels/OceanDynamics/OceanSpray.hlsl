// PIXL Ocean Dynamics GPU crest spray. Structured particle state is bounded and
// advanced entirely on the GPU; particles use the same fitted shoreline cage
// and broad FFT crest field as rendered water and gameplay surface queries.
#include "Common/FrameBuffer.hlsli"
#include "OceanDynamics/OceanWaveBands.hlsli"

#define SPRAY_CAPACITY 4096

struct OceanSprayParticle
{
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float size;
    float seed;
    float2 padding;
};

Texture2DArray<float4> WaveCurrent : register(t0);
Texture2DArray<float4> WavePrevious : register(t1);
Texture2D<float> SeaFloor : register(t2);
Texture2D<float> LocalSeaFloor : register(t3);
Texture2D<float4> WaveCageTexture : register(t4);
#if !defined(OCEAN_BREAKER_SHEETS)
#if defined(COMPUTESHADER)
RWStructuredBuffer<OceanSprayParticle> SprayState : register(u0);
#else
// D3D11 vertex stages consume spray state through an SRV. Declaring this as
// RWStructuredBuffer forces an unsupported vertex UAV binding and leaves the
// billboard draw with no valid particle data even though compute emitted it.
StructuredBuffer<OceanSprayParticle> SprayState : register(t0);
#endif
#endif
struct OceanBreakerNode
{
    float4 root; // absolute XYZ, smoothed activation
    float4 heading; // forward XY, curl radius, age
    float4 grid; // stable along/across coordinates, floor height, reserved
};
#if defined(COMPUTESHADER) && !defined(OCEAN_BREAKER_SHEETS)
StructuredBuffer<OceanBreakerNode> BreakerSpraySource : register(t6);
#endif
SamplerState SpraySampler : register(s0);

cbuffer SprayConstants : register(b0)
{
    float DeltaSeconds;
    float TimeSeconds;
    float EmissionScale;
    float WindSpeed;
    float4 CameraAndSea; // absolute camera XY, sea level, camera Z
    float4 WindDirectionAndRadius; // XY direction, spawn radius, reserved
};

cbuffer OceanSurfaceConstants : register(b1)
{
    float4 OceanMapBounds;
    float4 OceanHeightEncoding;
    float4 OceanSurfaceSettings;
    float4 OceanGeometrySettings;
    float4 OceanPhaseSettings;
    float4 OceanTimingSettings;
    float4 OceanSurfBoardPosition;
    float4 OceanSurfBoardVelocity;
    float4 OceanLocalTerrainPatch;
};

cbuffer OceanWaveCageConstants : register(b2)
{
    float4 OceanWaveCageSettings; // texture resolution, valid, blend, maximum radius
};

bool SampleGlobalSeaFloor(float2 world, out float height)
{
	height = -32768.0f;
	float2 extent = OceanMapBounds.zw - OceanMapBounds.xy;
	if (OceanSurfaceSettings.x < 0.5f || !all(isfinite(world)) ||
		!all(isfinite(extent)) || any(abs(extent) <= 1.0f))
		return false;
	float2 uv = (world - OceanMapBounds.xy) / extent;
	if (any(uv < 0.0f) || any(uv > 1.0f))
		return false;
	uint width, heightPixels;
	SeaFloor.GetDimensions(width, heightPixels);
	float2 texel = rcp(max(float2(width, heightPixels), 1.0f.xx));
	uv = clamp(uv, 0.5f * texel, 1.0f - 0.5f * texel);
	float encoded = SeaFloor.SampleLevel(SpraySampler, uv, 0.0f);
	if (!isfinite(encoded))
		return false;
	height = clamp(lerp(OceanHeightEncoding.x, OceanHeightEncoding.y, encoded),
		OceanHeightEncoding.z, OceanHeightEncoding.w);
	return isfinite(height);
}

bool SampleLocalSeaFloor(float2 world, out float height, out float edgeDistance)
{
	height = -32768.0f;
	edgeDistance = 0.0f;
	if (OceanLocalTerrainPatch.w < 0.5f || OceanLocalTerrainPatch.z <= 1.0f ||
		!all(isfinite(world)))
		return false;
	float2 grid = (world - OceanLocalTerrainPatch.xy) / OceanLocalTerrainPatch.z;
	uint width,heightPixels;
	LocalSeaFloor.GetDimensions(width,heightPixels);
	float2 lastSample=max(float2(width,heightPixels)-1.0f,1.0f.xx);
	if (any(grid < 0.0f) || any(grid > lastSample))
		return false;
	uint2 cell = min((uint2)floor(grid), (uint2)(lastSample-1.0f));
	float2 f = saturate(grid - cell);
	float h00 = LocalSeaFloor.Load(int3(cell, 0));
	float h10 = LocalSeaFloor.Load(int3(cell + uint2(1, 0), 0));
	float h01 = LocalSeaFloor.Load(int3(cell + uint2(0, 1), 0));
	float h11 = LocalSeaFloor.Load(int3(cell + uint2(1, 1), 0));
	if (!all(isfinite(float4(h00, h10, h01, h11))) ||
		min(min(h00, h10), min(h01, h11)) <= -30000.0f)
		return false;
	height = lerp(lerp(h00, h10, f.x), lerp(h01, h11, f.x), f.y);
	edgeDistance = min(min(grid.x, grid.y), min(lastSample.x - grid.x, lastSample.y - grid.y));
	return true;
}

// Feather the 4-cell high-resolution patch boundary into xLODGen terrain. The
// same terrain source drives shore phase, depth-limited crests, foam and spray.
bool SampleSeaFloor(float2 world, out float height, out float2 sampleStep)
{
	height = -32768.0f;
	sampleStep = 1.0f.xx;
	float localHeight, edgeDistance;
	bool hasLocal = SampleLocalSeaFloor(world, localHeight, edgeDistance);
	if (hasLocal && edgeDistance >= 4.0f) {
		height = localHeight;
		sampleStep = OceanLocalTerrainPatch.z.xx;
		return true;
	}
	float globalHeight;
	bool hasGlobal = SampleGlobalSeaFloor(world, globalHeight);
	if (hasLocal) {
		height = hasGlobal ? lerp(globalHeight, localHeight, smoothstep(0.0f, 4.0f, edgeDistance))
			: localHeight;
		sampleStep = OceanLocalTerrainPatch.z.xx;
		return true;
	}
	if (hasGlobal) {
		height = globalHeight;
		float2 extent = OceanMapBounds.zw - OceanMapBounds.xy;
		uint width, heightPixels;
		SeaFloor.GetDimensions(width, heightPixels);
		sampleStep = abs(extent / max(float2(width, heightPixels), 1.0f.xx));
		return true;
	}
	return false;
}

float3 TransformOceanWave(float3 wave, float2 world, float seaLevel, out float depth)
{
    float terrain;
    float2 sampleStep;
    if (!SampleSeaFloor(world, terrain, sampleStep)) {
        depth = 50000.0f;
        return float3(0.0f, 0.0f, clamp(wave.z, -100.0f, 100.0f));
    }
    depth = seaLevel - terrain;
    float period = clamp(OceanGeometrySettings.z, 2.0f, 24.0f);
    float omega = 6.28318530718f / period;
    float ratio = sqrt(clamp(tanh(omega * omega * max(depth, 0.0f) /
        (9.81f * 70.0f)), 0.001225f, 1.0f));
    float shallow = saturate((1.0f - smoothstep(200.0f, 1600.0f,
        max(depth, 0.0f))) * clamp(OceanPhaseSettings.z, 0.0f, 1.5f));
    float shoaling = lerp(1.0f,
        clamp(rsqrt(max(ratio, 0.05f)), 1.0f, 1.25f), shallow);
    float3 shoaledWave = wave * shoaling;
    float wet = smoothstep(-24.0f, 48.0f, depth);
    float verticalFallback = OceanThreeBandHeight(shoaledWave.z, shallow, depth, wet);
    if (OceanWaveCageSettings.y < 0.5f || OceanWaveCageSettings.z <= 0.0f)
        return float3(0.0f, 0.0f, verticalFallback);

    float2 extent = OceanMapBounds.zw - OceanMapBounds.xy;
    if (any(abs(extent) <= 1.0f))
        return float3(0.0f, 0.0f, verticalFallback);
    float2 cageUV = (world - OceanMapBounds.xy) / extent;
    if (any(cageUV < 0.0f) || any(cageUV > 1.0f))
        return float3(0.0f, 0.0f, verticalFallback);
    float2 cageTexel = rcp(max(OceanWaveCageSettings.xx, 1.0f.xx));
    cageUV = clamp(cageUV, 0.5f * cageTexel, 1.0f - 0.5f * cageTexel);
    float4 cage = WaveCageTexture.SampleLevel(SpraySampler, cageUV, 0.0f);
    if (cage.w < 0.5f)
        return float3(0.0f, 0.0f, verticalFallback);

    // Keep the terrain frame identical to the CPU cage fit and rendered-water
    // transform: two heightmap texels, with a 64-unit floor for stable normals.
    float2 frameStep = max(2.0f * sampleStep, 64.0f.xx);
    float left, right, down, up;
    float2 ignoredStep;
    bool hasGradient = SampleSeaFloor(world - float2(frameStep.x, 0.0f), left, ignoredStep) &&
        SampleSeaFloor(world + float2(frameStep.x, 0.0f), right, ignoredStep) &&
        SampleSeaFloor(world - float2(0.0f, frameStep.y), down, ignoredStep) &&
        SampleSeaFloor(world + float2(0.0f, frameStep.y), up, ignoredStep);
    float2 terrainSlope = hasGradient
        ? float2(right - left, up - down) / (2.0f * frameStep)
        : 0.0f.xx;
    float3 terrainNormal = normalize(float3(-terrainSlope, 1.0f));
    float shoreSign = depth >= 0.0f ? 1.0f : -1.0f;
    float3 shoreCandidate = float3(terrainSlope * shoreSign,
        dot(terrainSlope, terrainSlope) * shoreSign);
    float shoreLengthSq = dot(shoreCandidate, shoreCandidate);
    float3 shoreAxis = shoreLengthSq > 1.0e-8f
        ? shoreCandidate * rsqrt(shoreLengthSq) : float3(1.0f, 0.0f, 0.0f);
    float3 alongAxis = normalize(cross(terrainNormal, shoreAxis));
    shoreAxis = normalize(cross(alongAxis, terrainNormal));
    float referenceRadius = max(cage.x, max(cage.y, cage.z));
    float waveLength = length(shoaledWave);
    if (referenceRadius <= 1.0e-3f || waveLength <= 1.0e-6f)
        return float3(0.0f, 0.0f, verticalFallback);
    float3 radialClip = shoaledWave * (tanh(waveLength / referenceRadius) / waveLength);
    float3 cagedWave = shoreAxis * (cage.x * dot(shoreAxis, radialClip)) +
        alongAxis * (cage.y * dot(alongAxis, radialClip)) +
        terrainNormal * (cage.z * dot(terrainNormal, radialClip));
    return OceanBeachCoverage(lerp(float3(0.0f, 0.0f, verticalFallback), cagedWave * wet,
        saturate(OceanWaveCageSettings.z)), depth);
}
float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float4 SampleDisplacement(float2 world, uint cascade)
{
    const float extents[4] = { 256.0f, 1024.0f, 4096.0f, 32768.0f };
    float extent = extents[cascade];
    return WaveCurrent.SampleLevel(SpraySampler, float3(frac(world / extent + 0.5f), cascade), 0.0f);
}

float4 SamplePreviousDisplacement(float2 world, uint cascade)
{
    const float extents[4] = { 256.0f, 1024.0f, 4096.0f, 32768.0f };
    float extent = extents[cascade];
    return WavePrevious.SampleLevel(SpraySampler, float3(frac(world / extent + 0.5f), cascade), 0.0f);
}

#if defined(COMPUTESHADER) && !defined(OCEAN_BREAKER_SHEETS)
[numthreads(64, 1, 1)]
void UpdateSpray(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= SPRAY_CAPACITY) return;
    OceanSprayParticle p = SprayState[id.x];
    float dt = clamp(DeltaSeconds, 0.0f, 0.05f);
    if (EmissionScale <= 0.0f) {
        p.age = p.lifetime = 0.0f;
        SprayState[id.x] = p;
        return;
    }
    if (p.lifetime > 0.0f && p.age < p.lifetime)
    {
        p.age += dt;
        p.velocity += float3(WindDirectionAndRadius.xy * WindSpeed * 0.10f, -115.0f) * dt;
        p.position += p.velocity * dt;
        if (p.position.z < CameraAndSea.z - 10.0f) p.age = p.lifetime;
        SprayState[id.x] = p;
        return;
    }

    // Spread candidates across a world-space patch around the camera. Emission
    // is probabilistic and gated by both crest height and displacement slope.
    uint frameSeed = (uint)floor(TimeSeconds * 30.0f);
    float seed = Hash12(float2(id.x + 19u, frameSeed + 7u));
    float angle = Hash12(float2(id.x + 101u, frameSeed + 31u)) * 6.2831853f;
    float radius = sqrt(Hash12(float2(id.x + 337u, frameSeed + 61u))) * WindDirectionAndRadius.z;
    float2 world = CameraAndSea.xy + float2(cos(angle), sin(angle)) * radius;
    uint lipCount, lipStride;
    BreakerSpraySource.GetDimensions(lipCount, lipStride);
    OceanBreakerNode lipNode = (OceanBreakerNode)0;
    if (lipCount > 0 && (id.x & 1u) == 0u)
        lipNode = BreakerSpraySource[id.x % lipCount];
    bool fromLip = lipNode.root.w > 0.06f && lipNode.heading.z > 4.0f;
    float3 lipPoint = 0.0f;
    if (fromLip) {
        float maturity = smoothstep(0.0f,0.65f,lipNode.heading.w) *
            smoothstep(0.02f,0.45f,lipNode.root.w);
        float theta = 3.14159265f - lerp(1.8f,3.25f,maturity);
        float2 along = float2(-lipNode.heading.y,lipNode.heading.x);
        lipPoint = lipNode.root.xyz + float3(lipNode.heading.xy * lipNode.heading.z *
            (1.0f+cos(theta)) + along*(seed-0.5f)*96.0f,
            lipNode.heading.z*(sin(theta)-0.22f));
        world = lipPoint.xy;
    }
    float2 phaseWorld = world;
    float depth = 50000.0f;
    float coastSlope = 0.0f;
    float coastContext = 0.0f;
    float2 terrainStep = 1.0f.xx;
    float terrainHeight;
    if (EmissionScale > 0.0f &&
        SampleSeaFloor(world, terrainHeight, terrainStep))
    {
        depth = CameraAndSea.z - terrainHeight;
        float left, right, down, up;
        const float2 gradientStep = terrainStep;
        float2 ignoredStep;
        bool hasGradient =
            SampleSeaFloor(world - float2(gradientStep.x, 0.0f), left, ignoredStep) &&
            SampleSeaFloor(world + float2(gradientStep.x, 0.0f), right, ignoredStep) &&
            SampleSeaFloor(world - float2(0.0f, gradientStep.y), down, ignoredStep) &&
            SampleSeaFloor(world + float2(0.0f, gradientStep.y), up, ignoredStep);
        float2 groundSlope = hasGradient
            ? float2(right - left, up - down) / max(2.0f * gradientStep, 1.0f.xx)
            : 0.0f.xx;
        coastSlope = length(groundSlope);
        coastContext = smoothstep(8.0f, 40.0f, depth) *
            (1.0f - smoothstep(1000.0f, 1900.0f, depth)) *
            smoothstep(0.00035f, 0.018f, coastSlope);
    }
    float currentDepth;
    float previousDepth;
    float3 currentWave = TransformOceanWave(SampleDisplacement(phaseWorld, 3u).xyz,
        phaseWorld, CameraAndSea.z, currentDepth);
    float3 previousWave = TransformOceanWave(SamplePreviousDisplacement(phaseWorld, 3u).xyz,
        phaseWorld, CameraAndSea.z, previousDepth);
    depth = currentDepth;
    float surfaceHeight = currentWave.z;
    const float sampleDistance = 48.0f;
    float ignoredDepth;
    float heightLeft = TransformOceanWave(SampleDisplacement(
        phaseWorld - float2(sampleDistance, 0.0f), 3u).xyz,
        phaseWorld - float2(sampleDistance, 0.0f), CameraAndSea.z, ignoredDepth).z;
    float heightRight = TransformOceanWave(SampleDisplacement(
        phaseWorld + float2(sampleDistance, 0.0f), 3u).xyz,
        phaseWorld + float2(sampleDistance, 0.0f), CameraAndSea.z, ignoredDepth).z;
    float heightDown = TransformOceanWave(SampleDisplacement(
        phaseWorld - float2(0.0f, sampleDistance), 3u).xyz,
        phaseWorld - float2(0.0f, sampleDistance), CameraAndSea.z, ignoredDepth).z;
    float heightUp = TransformOceanWave(SampleDisplacement(
        phaseWorld + float2(0.0f, sampleDistance), 3u).xyz,
        phaseWorld + float2(0.0f, sampleDistance), CameraAndSea.z, ignoredDepth).z;
    float2 surfaceGradient = float2(heightRight - heightLeft, heightUp - heightDown) /
        (2.0f * sampleDistance);
    float surfaceSlope = length(surfaceGradient);
    float deepFactor = smoothstep(900.0f, 2400.0f, depth);
    float crest = lerp(smoothstep(1.5f, 12.0f, surfaceHeight),
        smoothstep(10.0f, 42.0f, surfaceHeight), deepFactor);
    float2 fluidVelocity = (currentWave.xy - previousWave.xy) / max(dt, 1.0f / 120.0f);
    float fluidSpeed = length(fluidVelocity);
    float frontFacing = fluidSpeed > 1.0e-3f && surfaceSlope > 1.0e-5f
        ? saturate(-dot(surfaceGradient, fluidVelocity) / (surfaceSlope * fluidSpeed))
        : 0.0f;
    // Thurey et al. detect an overturning front where the height gradient is
    // steep and opposes the surface flow. Use the same criterion on the FFT
    // surface; no separate wave phase or shallow-water simulation is introduced.
    // The long FFT cascade has a 256-unit spatial cell (32,768 / 128).
    // Use that discretization in Thurey's pH*g*dt/dx threshold instead of the
    // closer finite-difference probe spacing, which made shallow breakers too
    // difficult to trigger on this coarser wave field.
    const float longCascadeCell = 32768.0f / 128.0f;
    float paperThreshold = clamp(0.25f * (9.81f * 70.0f) *
        max(dt, 1.0f / 120.0f) / longCascadeCell, 0.005f, 0.025f);
    float shallowSteepness = smoothstep(paperThreshold * 0.30f,
        paperThreshold, surfaceSlope);
    float deepSteepness = smoothstep(0.006f, 0.048f, surfaceSlope);
    // FFT orbital displacement is not a shallow-water particle velocity: it
    // can have a weak/ambiguous dot product with the crest slope even when the
    // rendered face is visibly steep. Retain the paper's opposing-flow signal,
    // but give a genuine coastal steep face a bounded emission floor.
    float paperBreak = crest * shallowSteepness *
        lerp(0.28f, 1.0f, frontFacing) * coastContext;
    // The coarse long-swell cascade does not resolve the narrowest overturning
    // tip. Add a restrained local visual cue just below the paper threshold so
    // beach spray remains visible without turning every shallow ripple into foam.
    float nearshoreBreak = crest * smoothstep(paperThreshold * 0.45f,
        paperThreshold * 1.10f, surfaceSlope) *
        lerp(0.16f, 0.68f, frontFacing) * coastContext;
    float shallowBreak = max(paperBreak, nearshoreBreak);
    float deepBreak = crest * deepSteepness * deepFactor;
    float eventStrength = saturate(max(shallowBreak, deepBreak) *
        clamp(OceanPhaseSettings.w, 0.0f, 2.0f));
    // Board spray uses the same camera-independent candidate field as breaker
    // spray. The wake mask follows the actual physics board position/velocity
    // published in the shared bathymetry constants, and is inactive at low speed.
    float2 boardDelta = world - OceanSurfBoardPosition.xy;
    float boardSpeed = OceanSurfBoardVelocity.z;
    float wakeDistance = length(boardDelta);
    float wakeMask = OceanSurfBoardPosition.w > 0.5f && boardSpeed > 45.0f
        ? (1.0f - smoothstep(72.0f, 240.0f, wakeDistance))
        : 0.0f;
    float boardSpray = wakeMask * smoothstep(45.0f, 190.0f, boardSpeed) *
        saturate(0.35f + max(shallowSteepness, deepSteepness) * 0.45f);
    eventStrength = max(eventStrength, boardSpray * 0.82f);
    if (fromLip) eventStrength = max(eventStrength,lipNode.root.w);
    // Concentrate a bounded particle budget around meaningful moving crests.
    // The former wide distribution dispatched particles but made the local
    // shoreline spray effectively invisible.
    float spawnProbability = saturate(eventStrength * max(EmissionScale, 0.0f) * dt * 140.0f);
    if (seed < spawnProbability)
    {
        float3 velocity = float3(currentWave.xy - previousWave.xy, 0.0f) / max(dt, 1.0f / 120.0f);
        velocity.xy = clamp(velocity.xy * 0.15f + WindDirectionAndRadius.xy * WindSpeed * 7.0f,
            -90.0f.xx, 90.0f.xx);
        if (boardSpray > eventStrength * 0.5f && boardSpeed > 45.0f) {
            float2 boardDirection = OceanSurfBoardVelocity.xy / max(boardSpeed, 1.0e-3f);
            float2 wakeSide = float2(-boardDirection.y, boardDirection.x);
            float lateral = (Hash12(float2(seed, id.x + 211u)) - 0.5f) * 30.0f;
            velocity.xy = clamp(-boardDirection * lerp(24.0f, 68.0f, saturate(boardSpeed / 220.0f)) +
                wakeSide * lateral + WindDirectionAndRadius.xy * WindSpeed * 2.0f,
                -100.0f.xx, 100.0f.xx);
        }
        velocity.z = lerp(65.0f, 205.0f, eventStrength) * (0.75f + seed * 0.5f);
        float2 waveDirection = normalize(OceanPhaseSettings.xy + float2(1.0e-5f, 0.0f));
        float2 crestOffset = surfaceSlope > 1.0e-5f
            ? normalize(surfaceGradient) * 12.0f : waveDirection * 12.0f;
        p.size = lerp(30.0f, 62.0f, Hash12(float2(seed, id.x + 89u)));
        // The rendered crest is at the displaced XY as well as Z. Previously
        // particles started at the undisplaced grid point and were often hidden
        // below the choppy water surface even though the compute pass emitted them.
        p.position = float3(world + currentWave.xy + crestOffset,
            CameraAndSea.z + surfaceHeight + max(20.0f, p.size * 0.70f));
        if (fromLip) {
            p.position = lipPoint + float3(0,0,10);
            p.size = lerp(7.0f,24.0f,seed);
            velocity = float3(lipNode.heading.xy * lerp(60.0f,155.0f,seed),
                lerp(35.0f,135.0f,seed));
        }
        p.velocity = velocity;
        p.age = 0.0f;
        p.lifetime = lerp(0.60f, 1.45f, Hash12(float2(seed, id.x + 3u)));
        p.seed = seed;
    }
    else
    {
        p.age = p.lifetime = 0.0f;
        p.seed = seed;
    }
    SprayState[id.x] = p;
}
#endif

#if !defined(OCEAN_BREAKER_SHEETS)
struct SprayVertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float alpha : TEXCOORD1;
};

SprayVertexOutput SprayVS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    SprayVertexOutput output = (SprayVertexOutput)0;
    OceanSprayParticle p = SprayState[instanceID];
    if (p.lifetime <= 0.0f || p.age >= p.lifetime) {
        output.position = float4(0.0f, 0.0f, 2.0f, 1.0f);
        return output;
    }
    static const float2 corners[6] = {
        float2(-1,-1), float2(-1,1), float2(1,1),
        float2(-1,-1), float2(1,1), float2(1,-1)
    };
    float2 corner = corners[vertexID % 6u];
    float3 right = normalize(mul((float3x3)FrameBuffer::CameraViewInverse,float3(1,0,0)));
    float3 up = normalize(mul((float3x3)FrameBuffer::CameraViewInverse,float3(0,1,0)));
    float3 relative = p.position - FrameBuffer::CameraPosAdjust.xyz;
    float life = saturate(1.0f - p.age / max(p.lifetime, 1.0e-4f));
    float3 billboard = relative + (right * corner.x + up * corner.y) * p.size * (0.55f + (1.0f - life) * 0.45f);
    output.position = mul(FrameBuffer::CameraViewProj, float4(billboard, 1.0f));
    output.uv = corner;
    output.alpha = life * life * saturate(p.seed * 2.0f + 0.25f);
    return output;
}

float4 SprayPS(SprayVertexOutput input) : SV_TARGET
{
    float radius = dot(input.uv, input.uv);
    float alpha = (1.0f - smoothstep(0.30f, 1.0f, radius)) * input.alpha * 0.82f;
    clip(alpha - 0.005f);
    return float4(0.88f, 0.95f, 0.99f, alpha);
}
#endif
