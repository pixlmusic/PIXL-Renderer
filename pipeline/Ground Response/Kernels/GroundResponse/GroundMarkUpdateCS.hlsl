// Phase 4 visual-only ground marks. t101 remains the physical deformation field.
cbuffer GroundMarkCB : register(b0)
{
    float2 SurfaceOriginAbsolute; uint2 SurfaceArrayOrigin;
    int2 SurfaceValidMargin; float SurfaceTimeDelta; uint GroundMarkCount;
    float SurfaceTrackHoldSeconds; float SurfaceRecoveryRate; float SurfaceStampStrength; float SurfaceElementalRecoveryRate;
    uint TileDispatchCount; uint TileMode; uint ElementalEnabled; uint Padding;
}
struct GroundMarkPacked { float2 CurrentPosition; float2 PreviousPosition; float2 HalfExtent; float Orientation; float Strength; uint Type; uint Receiver; float ReceiverZ; float AgeFade; };
struct TileHeader { uint Offset; uint Count; uint pad0; uint pad1; };
struct TileDispatch { uint2 LogicalTile; uint pad0; uint pad1; };
StructuredBuffer<GroundMarkPacked> GroundMarks : register(t0);
StructuredBuffer<TileHeader> TileHeaders : register(t1);
StructuredBuffer<uint> TileMarkIndices : register(t2);
StructuredBuffer<TileDispatch> TileDispatches : register(t3);
RWTexture2D<float4> GroundMarkField : register(u0);
static const uint SIZE=1024u, MASK=1023u, TILE_SIZE=32u, TILE_GROUPS=4u;
static const float CELL_SIZE=4.0f;
static const uint MARK_TYPE_MASK=0x0Fu, MARK_SHAPE_MASK=0x30u;
static const uint MARK_CIRCLE=0x00u, MARK_ELLIPSE=0x10u, MARK_CAPSULE=0x20u, MARK_BOX=0x30u;
float2 World(uint2 cell) { return SurfaceOriginAbsolute + (float2(cell)+0.5f-512.0f)*CELL_SIZE; }
uint2 Wrap(uint2 cell) { return (cell+SurfaceArrayOrigin)&uint2(MASK,MASK); }
float SmoothMark(float x) { x=saturate(1.0f-x); return x*x*(3.0f-2.0f*x); }
float MarkCoverage(float2 p, GroundMarkPacked mark)
{
    float2 motion=mark.CurrentPosition-mark.PreviousPosition;
    float len=max(length(motion), 1e-3f); float2 forward=motion/len;
    if (length(motion)<0.5f) forward=float2(cos(mark.Orientation),sin(mark.Orientation));
    float2 side=float2(-forward.y,forward.x);
    uint shape=mark.Type&MARK_SHAPE_MASK;
    float2 center=0.5f*(mark.CurrentPosition+mark.PreviousPosition);
    float2 rel=p-center;
    float2 ext=max(mark.HalfExtent, 1.0f.xx);
    if(shape==MARK_CIRCLE) ext=ext.xx;
    float2 local=float2(dot(rel,side),dot(rel,forward));
    float d=0.0f;
    if(shape==MARK_CAPSULE) {
        float longitudinal=clamp(dot(p-mark.PreviousPosition,forward),0.0f,len);
        local=float2(dot(p-mark.PreviousPosition-forward*longitudinal,side),dot(p-mark.PreviousPosition-forward*longitudinal,forward));
        d=length(local/ext);
    } else if(shape==MARK_BOX) {
        d=max(abs(local.x)/ext.x,abs(local.y)/ext.y);
    } else {
        d=length(local/ext);
    }
    return SmoothMark(d) * saturate(mark.Strength) * saturate(mark.AgeFade);
}
[numthreads(8,8,1)] void main(uint3 groupId:SV_GroupID,uint3 id:SV_DispatchThreadID,uint3 tid:SV_GroupThreadID)
{
    uint2 logical=id.xy, output=id.xy; uint count=GroundMarkCount; TileHeader header = (TileHeader)0;
    if(TileMode!=0u) { uint di=groupId.x/TILE_GROUPS; if(di>=TileDispatchCount||groupId.y>=TILE_GROUPS)return; TileDispatch tile=TileDispatches[di]; logical=tile.LogicalTile*TILE_SIZE+uint2(groupId.x%TILE_GROUPS,groupId.y)*8u+tid.xy; output=Wrap(logical); header=TileHeaders[tile.LogicalTile.y*32u+tile.LogicalTile.x]; count=header.Count; }
    else logical=(uint2(int2(id.xy)-int2(SurfaceArrayOrigin)))&uint2(MASK,MASK);
    float4 value=GroundMarkField[output];
    // Footprints settle into the snow; blood fades slowly and is washed faster by
    // future weather integration. Elemental marks decay independently.
    float dt=clamp(SurfaceTimeDelta,0.0f,0.25f);
    value.x=max(value.x-dt*0.020f,0.0f); value.y=max(value.y-dt*0.012f,0.0f); value.z*=max(1.0f-dt*0.045f,0.0f); value.w=max(value.w-dt*0.04f,0.0f);
    float2 p=World(logical);
    [loop] for(uint i=0u;i<count;++i) {
        uint index=TileMode!=0u ? TileMarkIndices[header.Offset+i] : i;
        if(index>=GroundMarkCount) continue;
        GroundMarkPacked mark=GroundMarks[index];
        if(mark.Receiver!=0u) continue; // terrain-only Phase 4 receiver contract
        float coverage=MarkCoverage(p,mark); if(coverage<=1e-4f) continue;
        uint type=mark.Type&MARK_TYPE_MASK;
        if(type==0u) value.x=max(value.x,coverage);
        else if(type==1u) value.y=max(value.y,coverage);
        else if(type==2u) value.z=min(value.z,-coverage);
        else if(type==3u) value.z=max(value.z,coverage);
        else value.x=max(value.x,coverage*0.55f);
        value.w=max(value.w,coverage);
    }
    GroundMarkField[output]=value;
}
