// Offline snapshot-journal experiment. Same formats and seven payloads as the
// current cache including optional intelligence and update-time resources.
cbuffer Parameters : register(b0) { uint Epoch; uint Capacity; uint Stride; uint Reserved; };
#ifdef SNAPSHOT_MUTATE
RWTexture2D<uint> metadata : register(u0);
RWTexture2D<uint> normal : register(u1);
RWTexture2D<uint> intelligence : register(u2);
RWTexture2D<uint> updateTime : register(u3);
RWTexture2D<float4> sh0 : register(u4);
RWTexture2D<float4> sh1 : register(u5);
RWTexture2D<float4> sh2 : register(u6);
RWTexture2D<uint> dirty : register(u7);
[numthreads(8,8,1)] void main(uint2 cell : SV_DispatchThreadID)
{
    if(any(cell >= uint2(1024,64))) return;
    uint index=cell.x+cell.y*1024u;
    bool touched=(index+Epoch)%Stride==0u;
    dirty[cell]=touched?1u:0u;
    if(!touched) return;
    // Include tombstones: dirty tracking must not depend on a live hash.
    metadata[cell]=(index%5u==0u)?0u:((Epoch&255u)<<24)|(index+1u);
    normal[cell]=index*1664525u+Epoch;
    intelligence[cell]=(index<<16)^Epoch;
    updateTime[cell]=Epoch*33u;
    float value=float((index+Epoch)&1023u)/1024.0f;
    sh0[cell]=float4(value,-value,2.0f*value,0.0f);
    sh1[cell]=float4(4.0f*value,0.5f*value,-2.0f*value,value);
    sh2[cell]=float4(-value,0.25f*value,0.75f*value,2.0f*value);
}
#elif defined(SNAPSHOT_JOURNAL)
Texture2D<uint> dirty : register(t0);
RWStructuredBuffer<uint2> journal : register(u0);
RWStructuredBuffer<uint> control : register(u1);
[numthreads(8,8,1)] void main(uint2 cell : SV_DispatchThreadID)
{
    if(any(cell >= uint2(1024,64)) || dirty[cell]==0u) return;
    uint slot; InterlockedAdd(control[0],1u,slot);
    if(slot<Capacity) journal[slot]=uint2(cell.x+cell.y*1024u,Epoch);
    else InterlockedOr(control[1],1u);
}
#else
Texture2D<uint> sourceMetadata : register(t0);
Texture2D<uint> sourceNormal : register(t1);
Texture2D<uint> sourceIntelligence : register(t2);
Texture2D<uint> sourceUpdateTime : register(t3);
Texture2D<float4> sourceSH0 : register(t4);
Texture2D<float4> sourceSH1 : register(t5);
Texture2D<float4> sourceSH2 : register(t6);
StructuredBuffer<uint2> journal : register(t7);
StructuredBuffer<uint> control : register(t8);
RWTexture2D<uint> metadata : register(u0);
RWTexture2D<uint> normal : register(u1);
RWTexture2D<uint> intelligence : register(u2);
RWTexture2D<uint> updateTime : register(u3);
RWTexture2D<float4> sh0 : register(u4);
RWTexture2D<float4> sh1 : register(u5);
RWTexture2D<float4> sh2 : register(u6);
[numthreads(64,1,1)] void main(uint index : SV_DispatchThreadID)
{
    // Epoch is published by the host/journal owner only after collection.
    // A missing record or reset sets recovery before any snapshot read begins.
    bool full=control[1]!=0u || control[0]>Capacity || control[2]!=Epoch;
    if(index >= (full?65536u:control[0])) return;
    uint2 entry=full?uint2(index,Epoch):journal[index];
    if(entry.x>=65536u || entry.y!=Epoch) return;
    uint2 cell=uint2(entry.x&1023u,entry.x>>10u);
    metadata[cell]=sourceMetadata[cell]; normal[cell]=sourceNormal[cell];
    intelligence[cell]=sourceIntelligence[cell]; updateTime[cell]=sourceUpdateTime[cell];
    sh0[cell]=sourceSH0[cell]; sh1[cell]=sourceSH1[cell]; sh2[cell]=sourceSH2[cell];
}
#endif
