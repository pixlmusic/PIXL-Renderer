// Connected crest sheets extend the heightfield into a genuine overhang.
// A bounded world-aligned front lattice tracks the existing FFT; it introduces
// no independent shore oscillator. The base overlaps the rendered water.
#define OCEAN_BREAKER_SHEETS 1
#include "OceanDynamics/OceanSpray.hlsl"
#include "Common/SharedData.hlsli"

#define BREAKER_ALONG 32
#define BREAKER_CROSS 24
#define BREAKER_STEP 96.0f
#define CURL_SEGMENTS 16

#if defined(COMPUTESHADER)
RWStructuredBuffer<OceanBreakerNode> BreakerNodes : register(u0);
#else
StructuredBuffer<OceanBreakerNode> BreakerNodes : register(t6);
#endif

float3 BreakerSurface(float2 world, out float depth)
{
    float3 raw = clamp(SampleDisplacement(world, 3u).xyz,
        float3(-35,-35,-100), float3(35,35,100));
    return TransformOceanWave(raw, world, CameraAndSea.z, depth);
}

#if defined(COMPUTESHADER)
[numthreads(64,1,1)]
void UpdateBreakers(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= BREAKER_ALONG * BREAKER_CROSS) return;
    float2 travel = normalize(OceanPhaseSettings.xy + float2(1.0e-5f,0));
    float2 along = float2(-travel.y, travel.x);
    float2 cameraGrid = float2(dot(CameraAndSea.xy, along), dot(CameraAndSea.xy, travel));
    float2 origin = floor(cameraGrid / BREAKER_STEP) * BREAKER_STEP -
        float2(BREAKER_ALONG / 2, BREAKER_CROSS / 2) * BREAKER_STEP;
    uint u = id.x % BREAKER_ALONG;
    uint v = id.x / BREAKER_ALONG;
    float2 lattice = origin + float2(u,v) * BREAKER_STEP;
    float2 baseWorld = along * lattice.x + travel * lattice.y;
    float bestScore = 0.0f;
    float2 bestWorld = baseWorld;
    float bestDepth = 0.0f;
    float3 bestSurface = 0.0f;
    // Search along propagation for a front, then connect neighboring alongshore
    // samples. Quantization assigns each front to one cross-shore band.
    [unroll]
    for (int offset = -2; offset <= 2; ++offset) {
        float2 world = baseWorld + travel * (offset * 48.0f);
        float depth, unusedDepth;
        float3 surface = BreakerSurface(world, depth);
        float behind = BreakerSurface(world - travel * 48.0f, unusedDepth).z;
        float ahead = BreakerSurface(world + travel * 48.0f, unusedDepth).z;
        float front = (behind - ahead) / 96.0f;
        float window = smoothstep(48.0f, 140.0f, depth) *
            (1.0f - smoothstep(1100.0f, 2000.0f, depth));
        float score = smoothstep(0.006f, 0.045f, front) *
            smoothstep(2.0f, 20.0f, surface.z) * window * saturate(OceanPhaseSettings.w);
        if (score > bestScore) {
            bestScore = score;
            bestWorld = world;
            bestDepth = depth;
            bestSurface = surface;
        }
    }
    int2 absoluteCell = (int2)round(lattice / BREAKER_STEP);
    // This unsigned bias is divisible by both dimensions, so negative world
    // cells wrap correctly without SM5's expensive signed integer remainder.
    uint2 wrapped = ((uint2)absoluteCell + 0x60000000u) % uint2(BREAKER_ALONG,BREAKER_CROSS);
    uint storage = wrapped.y * BREAKER_ALONG + wrapped.x;
    OceanBreakerNode old = BreakerNodes[storage];
    OceanBreakerNode node = (OceanBreakerNode)0;
    float3 target = float3(bestWorld + bestSurface.xy, CameraAndSea.z + bestSurface.z - 3.0f);
    float identity = step(length(old.grid.xy - lattice), 1.0f) *
        step(abs(TimeSeconds-old.grid.w),0.25f) *
        step(abs(old.root.z-target.z),160.0f);
    // Reject duplicate bands, rather than stacking disconnected opaque sheets.
    float owner = floor((dot(bestWorld,travel) - origin.y) / BREAKER_STEP + 0.5f);
    bestScore *= abs(owner - v) < 0.1f ? 1.0f : 0.0f;
    bestScore *= smoothstep(0.0f,2.0f,min(min((float)u,(float)(BREAKER_ALONG-1-u)),
        min((float)v,(float)(BREAKER_CROSS-1-v))));
    float dt = clamp(DeltaSeconds, 0.0f, 0.05f);
    float follow = 1.0f - exp(-dt * 10.0f);
    float activation = lerp(old.root.w * identity, bestScore, follow);
    bool sameFront = identity > 0.5f && length(target.xy - old.root.xy) < 180.0f;
    node.root.xyz = sameFront ? lerp(old.root.xyz, target, follow) : target;
    node.root.w = activation;
    node.heading.xy = travel;
    // The terrain cage bounds the base surface. Use the incoming crest's
    // uncompressed energy for the overturning sheet, then limit its radius by
    // local water depth so the lip can develop offshore and collapse at shore.
    float incomingHeight = clamp(SampleDisplacement(bestWorld,3u).z,0.0f,100.0f);
    float radius = clamp(20.0f + incomingHeight * 2.0f, 18.0f, 160.0f);
    radius = min(radius, max(bestDepth,0.0f) * 0.30f);
    node.heading.z = sameFront ? lerp(old.heading.z, radius, follow) : radius;
    node.heading.w = sameFront && bestScore > 0.03f ? old.heading.w + dt : 0.0f;
    node.grid = float4(lattice, CameraAndSea.z - bestDepth, TimeSeconds);
    BreakerNodes[storage] = node;
}
#endif

struct BreakerOutput
{
    float4 position : SV_POSITION;
    float3 relative : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 strength : TEXCOORD3;
};

BreakerOutput BreakerVS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    BreakerOutput o = (BreakerOutput)0;
    uint alongID = instanceID % BREAKER_ALONG;
    uint crossID = instanceID / BREAKER_ALONG;
    uint index = crossID * BREAKER_ALONG + alongID;
    OceanBreakerNode a = BreakerNodes[index];
    uint nextAlong = (alongID+1) % BREAKER_ALONG;
    OceanBreakerNode b = BreakerNodes[crossID * BREAKER_ALONG + nextAlong];
    float distanceToNext = b.root.w > 0.015f && abs(a.grid.x+BREAKER_STEP-b.grid.x)<1.0f
        ? length(a.root.xyz-b.root.xyz) : 1.0e6f;
    [unroll]
    for (int row=-1;row<=1;row+=2) {
        uint otherRow = row<0 ? (crossID+BREAKER_CROSS-1u)%BREAKER_CROSS : (crossID+1u)%BREAKER_CROSS;
        OceanBreakerNode candidate = BreakerNodes[otherRow*BREAKER_ALONG+nextAlong];
        float separation = length(candidate.root.xyz-a.root.xyz);
        if (candidate.root.w > 0.015f && abs(a.grid.x+BREAKER_STEP-candidate.grid.x)<1.0f && separation<distanceToNext) {
            b=candidate;
            distanceToNext=separation;
        }
    }
    if (a.root.w < 0.015f) {
        o.position = float4(0,0,2,1);
        return o;
    }
    if (distanceToNext > BREAKER_STEP*2.4f) {
        b=a;
        b.root.xyz += float3(-a.heading.y,a.heading.x,0)*BREAKER_STEP;
        b.root.w=0;
        b.grid.x+=BREAKER_STEP;
    }
    static const float2 corners[6] = {
        float2(0,0),float2(1,0),float2(1,1),
        float2(0,0),float2(1,1),float2(0,1)
    };
    float2 corner = corners[vertexID % 6];
    float t = (vertexID / 6 + corner.y) / CURL_SEGMENTS;
    float side = corner.x;
    float3 root = lerp(a.root.xyz, b.root.xyz, side);
    float2 direction = normalize(lerp(a.heading.xy,b.heading.xy,side));
    float radius = lerp(a.heading.z,b.heading.z,side);
    float strength = lerp(a.root.w,b.root.w,side);
    float age = lerp(a.heading.w,b.heading.w,side);
    float maturity = smoothstep(0.0f,0.65f,age) * smoothstep(0.02f,0.45f,strength);
    float arc = lerp(1.8f,3.25f,maturity);
    float theta = 3.14159265f - t * arc;
    float horizontal = radius * (1.0f + cos(theta));
    float vertical = radius * sin(theta) - t*t*radius*0.22f;
    float3 world = root + float3(direction * horizontal, vertical);
    float3 tangentAlong = normalize(b.root.xyz-a.root.xyz);
    float3 tangentCurl = float3(direction * (sin(theta)*arc),
        -cos(theta)*arc - 0.44f*t);
    o.normal = normalize(cross(tangentAlong,tangentCurl) + float3(0,0,1.0e-6f));
    o.relative = world - FrameBuffer::CameraPosAdjust.xyz;
    o.position = mul(FrameBuffer::CameraViewProj,float4(o.relative,1));
    o.uv = float2(lerp(a.grid.x,b.grid.x,side)/100.0f,t);
    o.strength = float2(strength,maturity);
    return o;
}

Texture2D<float4> BreakerReflection : register(t0);
Texture2D<float4> BreakerRefraction : register(t1);
SamplerState BreakerReflectionSampler : register(s0);
SamplerState BreakerRefractionSampler : register(s1);

float4 BreakerPS(BreakerOutput i, bool front : SV_IsFrontFace) : SV_TARGET
{
    float3 n = normalize(i.normal) * (front ? 1.0f : -1.0f);
    float3 view = normalize(-i.relative + float3(0,0,1.0e-6f));
    if (dot(n,view)<0) n=-n;
    float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(i.position.xy * SharedData::BufferDim.zw);
    float2 bend = n.xy * 0.006f * sin(i.uv.y * 3.14159265f);
    float3 transmitted = BreakerRefraction.Sample(BreakerRefractionSampler,saturate(uv+bend)).rgb;
    float3 reflected = BreakerReflection.Sample(BreakerReflectionSampler,saturate(uv-bend)).rgb;
    float3 ambient = max(SharedData::GetAmbient(n),0.0f.xxx);
    float3 light = normalize(-SharedData::DirLightDirection.xyz + float3(0,0,1.0e-5f));
    float3 key = max(SharedData::DirLightColor.xyz,0.0f.xxx);
    float facing = saturate(dot(n,view));
    float fresnel = 0.0204f + 0.9796f*pow(1.0f-facing,5.0f);
    float backlight = pow(saturate(dot(-light,view)),4.0f) *
        (1.0f-facing) * saturate(OceanGeometrySettings.y);
    float thickness = lerp(0.7f,0.08f,i.uv.y);
    float3 attenuation = exp(-float3(1.8f,0.48f,0.25f)*thickness);
    float3 water = transmitted*attenuation + float3(0.025f,0.12f,0.14f) *
        (ambient + key*(0.25f + backlight*1.8f))*(1.0f-attenuation);
    water = lerp(water,reflected,fresnel);
    float streak = sin(i.uv.x*17.0f + sin(i.uv.x*3.1f)*2.0f + i.uv.y*33.0f);
    float lip = smoothstep(0.60f,0.93f,i.uv.y);
    float foam = saturate(lip*(0.70f+0.30f*streak) * lerp(0.45f,1.0f,i.strength.y));
    foam *= saturate(OceanSurfaceSettings.z * 1.5f);
    float3 whitewater = (ambient + key*(0.22f + 0.78f*saturate(dot(n,light)))) * float3(0.75f,0.84f,0.84f);
    float3 color = lerp(water,whitewater,foam);
    float rootBlend = smoothstep(0.0f,0.12f,i.uv.y);
    // A breaker sheet is a thin, water-filled overhang. Nearly opaque alpha
    // made the 96-unit front lattice read as separate rectangular water tiles
    // whenever neighboring crest nodes had different activation. Keep the
    // body optically integrated with the ocean and reserve stronger coverage
    // for the actual aerated lip.
    float sheetConfidence = smoothstep(0.045f,0.24f,i.strength.x);
    float alpha = rootBlend * sheetConfidence * lerp(0.22f,0.70f,foam);
    clip(alpha-0.006f);
    return float4(color,alpha);
}
