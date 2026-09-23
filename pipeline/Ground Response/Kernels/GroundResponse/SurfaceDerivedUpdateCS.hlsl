// Phase 3: dirty-tile derived deformation field.  This mirrors the expensive
// t101/t102/t103 reconstruction formerly performed for every terrain DS vertex.
// Logical coordinates remain finite even though physical storage is toroidal.
cbuffer SurfaceFieldCB : register(b0)
{
    float2 SurfaceOriginAbsolute; uint2 SurfaceArrayOrigin;
    int2 SurfaceValidMargin; float SurfaceTimeDelta; uint SurfaceStampBoxCount;
    float SurfaceTrackHoldSeconds; float SurfaceRecoveryRate; float SurfaceStampStrength; float SurfaceElementalRecoveryRate;
    uint SurfaceTileDispatchCount; uint SurfaceTileMode; uint SurfaceElementalEnabled; uint SurfaceTilePadding;
}
Texture2D<float4> SurfaceField : register(t0);
Texture2D<float4> SurfacePushField : register(t1);
Texture2D<float4> SurfaceElementalField : register(t2);
struct SurfaceTileDispatchPacked { uint2 LogicalTile; uint pad0; uint pad1; };
StructuredBuffer<SurfaceTileDispatchPacked> TileDispatches : register(t3);
RWTexture2D<float4> DerivedResponse : register(u0); // current/previous compaction, current/previous freshness
RWTexture2D<float4> DerivedSlump : register(u1);    // current/previous berm, current/previous wall collapse
RWTexture2D<float4> DerivedGradient : register(u2); // current dx/dy, previous dx/dy normalized height signal
static const uint SIZE = 1024u, MASK = 1023u, TILE_SIZE = 32u, TILE_GROUPS = 4u;
static const float WORLD_SIZE = 4096.0f, CELL_SIZE = 4.0f;

uint2 Wrap(uint2 logicalCell) { return (logicalCell + SurfaceArrayOrigin) & uint2(MASK, MASK); }
float2 World(uint2 logicalCell) { return SurfaceOriginAbsolute + (float2(logicalCell) + 0.5f - 512.0f) * CELL_SIZE; }
bool Valid(float2 absoluteXY) { return all(abs(absoluteXY - SurfaceOriginAbsolute) < (WORLD_SIZE * 0.5f - CELL_SIZE).xx); }
float4 SampleTexture(Texture2D<float4> textureField, float2 absoluteXY)
{
    if (!Valid(absoluteXY)) return 0.0f.xxxx;
    float2 p = clamp((absoluteXY - SurfaceOriginAbsolute) / WORLD_SIZE * float(SIZE) + 511.5f, 0.0f.xx, 1022.9999f.xx);
    uint2 b = uint2(floor(p)); float2 f = frac(p);
    float4 a = textureField.Load(int3(Wrap(b), 0));
    float4 x = textureField.Load(int3(Wrap(b + uint2(1, 0)), 0));
    float4 y = textureField.Load(int3(Wrap(b + uint2(0, 1)), 0));
    float4 z = textureField.Load(int3(Wrap(b + uint2(1, 1)), 0));
    return lerp(lerp(a, x, f.x), lerp(y, z, f.x), f.y);
}
float4 Raw(float2 p) { return saturate(SampleTexture(SurfaceField, p)); }
float4 Push(float2 p) { return saturate(SampleTexture(SurfacePushField, p)); }
float4 Element(float2 p) { float4 v = SampleTexture(SurfaceElementalField, p); v.yw = saturate(v.yw); return v; }
float4 Filtered(float2 p)
{
    const float r = 4.0f;
    float4 c = Raw(p);
    float4 f = c * .36f + (Raw(p+float2(r,0))+Raw(p-float2(r,0))+Raw(p+float2(0,r))+Raw(p-float2(0,r)))*.12f +
        (Raw(p+float2(r,r))+Raw(p+float2(r,-r))+Raw(p+float2(-r,r))+Raw(p+float2(-r,-r)))*.04f;
    float2 heat = Element(p).yw;
    if (max(heat.x, heat.y) > 1e-4f) {
        float4 wide = (c + Raw(p+float2(10,0))+Raw(p-float2(10,0))+Raw(p+float2(0,10))+Raw(p-float2(0,10))) * .20f;
        f.x = lerp(f.x, wide.x, heat.x * .72f); f.z = lerp(f.z, wide.z, heat.y * .72f);
    }
    return saturate(f);
}
float Profile(float c) { float t=saturate((saturate(c)-0.055f)/(0.62f-0.055f)); return t*t*t*(t*(t*6.0f-15.0f)+10.0f); }
float2 SearchOutward(float2 p)
{
    static const float2 dirs[8] = { float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),float2(.70710678,.70710678),float2(-.70710678,.70710678),float2(.70710678,-.70710678),float2(-.70710678,-.70710678) };
    float2 inward=0.0f.xx; float sum=0.0f;
    [unroll] for(uint i=0;i<8;++i) { float c=Raw(p+dirs[i]*12.0f).x; float w=c*c; inward+=dirs[i]*w; sum+=w; }
    [unroll] for(uint j=0;j<8;++j) { float c=Raw(p+dirs[j]*30.0f).x; float w=c*c*.62f; inward+=dirs[j]*w; sum+=w; }
    float l=dot(inward,inward); return (sum>1e-5f && l>1e-6f) ? -inward*rsqrt(l) : 0.0f.xx;
}
void Slump(float2 p, out float2 berm, out float2 wall)
{
    berm=0.0f.xx; wall=0.0f.xx; float2 outward=SearchOutward(p); if(dot(outward,outward)<=1e-5f) return;
    float4 centre=Push(p); float2 localProgress=1.0f-centre.yw;
    float2 bulkT=saturate((localProgress-.01f)/(.62f-.01f)); float2 bulk=bulkT*bulkT*bulkT*(bulkT*(bulkT*6.0f-15.0f)+10.0f);
    float2 edgeT=saturate((localProgress-.50f)/(.95f-.50f)); float2 edge=edgeT*edgeT*edgeT*(edgeT*(edgeT*6.0f-15.0f)+10.0f);
    float4 s0=Push(p-outward*6), s1=Push(p-outward*14), s2=Push(p-outward*28), s3=Push(p-outward*44);
    float4 progress = 1.0f.xxxx-float4(s0.y,s1.y,s2.y,s3.y);
    float4 previousProgress = 1.0f.xxxx-float4(s0.w,s1.w,s2.w,s3.w);
    float4 target=lerp(2.0f.xxxx,50.0f.xxxx,smoothstep(.02f,.68f,progress));
    float4 previousTarget=lerp(2.0f.xxxx,50.0f.xxxx,smoothstep(.02f,.68f,previousProgress));
    float4 d=float4(6,14,28,44); float4 width=float4(6,7,8,9);
    float4 weights=1.0f.xxxx-smoothstep(d-width,d+width,abs(target-d));
    float4 pweights=1.0f.xxxx-smoothstep(d-width,d+width,abs(previousTarget-d));
    float ws=max(dot(weights,1.0f.xxxx),1e-4f), pws=max(dot(pweights,1.0f.xxxx),1e-4f);
    float moved=dot(float4(s0.x,s1.x,s2.x,s3.x),weights)/ws;
    float previousMoved=dot(float4(s0.z,s1.z,s2.z,s3.z),pweights)/pws;
    float pile=max(centre.x*(1.0f-bulk.x),moved), previousPile=max(centre.z*(1.0f-bulk.y),previousMoved);
    float progressMax=max(localProgress.x,max(max(progress.x*weights.x,progress.y*weights.y),max(progress.z*weights.z,progress.w*weights.w)));
    float previousProgressMax=max(localProgress.y,max(max(previousProgress.x*pweights.x,previousProgress.y*pweights.y),max(previousProgress.z*pweights.z,previousProgress.w*pweights.w)));
    float2 bermHeight=lerp(1.0f.xx,.22f.xx,smoothstep(.04f,.86f,float2(progressMax,previousProgressMax)));
    berm=float2(pile,previousPile)*bermHeight;
    float local=Raw(p).x, previousLocal=Raw(p).z;
    float signal=saturate(Raw(p-outward*6).x*.56f+Raw(p-outward*14).x*.30f+Raw(p-outward*28).x*.14f-local*.58f);
    float previousSignal=saturate(Raw(p-outward*6).z*.56f+Raw(p-outward*14).z*.30f+Raw(p-outward*28).z*.14f-previousLocal*.58f);
    wall=float2(signal*saturate(bulk.x*.90f+edge.x*.10f), previousSignal*saturate(bulk.y*.90f+edge.y*.10f));
}
void Evaluate(float2 p, out float4 response, out float4 slump)
{
    response=Filtered(p); float2 berm, wall; Slump(p,berm,wall); slump=float4(berm,wall);
}
[numthreads(8,8,1)] void main(uint3 groupId:SV_GroupID,uint3 id:SV_DispatchThreadID,uint3 tid:SV_GroupThreadID)
{
    uint2 logical=id.xy, outputCell=id.xy;
    if(SurfaceTileMode!=0u) { uint di=groupId.x/TILE_GROUPS; if(di>=SurfaceTileDispatchCount||groupId.y>=TILE_GROUPS)return; SurfaceTileDispatchPacked tile=TileDispatches[di]; logical=tile.LogicalTile*TILE_SIZE+uint2(groupId.x%TILE_GROUPS,groupId.y)*8u+tid.xy; outputCell=Wrap(logical); }
    else { logical=(uint2(int2(id.xy)-int2(SurfaceArrayOrigin)))&uint2(MASK,MASK); }
    float2 p=World(logical); float4 response,slump; Evaluate(p,response,slump);
    // This gradient is the normalized interaction-height signal. The DS scales
    // it by its exact per-layer physical capacity before rebuilding its TBN.
    float4 l=Filtered(p-float2(CELL_SIZE,0)), r=Filtered(p+float2(CELL_SIZE,0)), d=Filtered(p-float2(0,CELL_SIZE)), u=Filtered(p+float2(0,CELL_SIZE));
    float4 gradient=float4((Profile(l.x)-Profile(r.x))/(2*CELL_SIZE),(Profile(d.x)-Profile(u.x))/(2*CELL_SIZE),(Profile(l.z)-Profile(r.z))/(2*CELL_SIZE),(Profile(d.z)-Profile(u.z))/(2*CELL_SIZE));
    DerivedResponse[outputCell]=response; DerivedSlump[outputCell]=slump; DerivedGradient[outputCell]=gradient;
}