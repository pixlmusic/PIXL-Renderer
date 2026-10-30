// Offline hard-budget candidate queue. Coordinates are stable absolute-world
// toroidal IDs supplied after window/hash validation and representative election.
cbuffer Parameters : register(b0) { uint Clock; uint Budget; uint Capacity; uint Reserved; };
StructuredBuffer<uint4> candidates : register(t0); // identity, age, intelligence, confidence/255
uint Priority(uint4 candidate)
{
    if(candidate.x==0u) return 8u;
    // A fixed reservation provides a provable service bound even if new lights
    // continually occupy every other priority bucket. IDs are toroidal cells.
    uint reservation=min(65536u,min(Budget,Capacity))/2u;
    if(reservation>0u && ((candidate.x-1u-Clock*reservation)&65535u)<reservation) return 0u;
    if(candidate.y>=8u) return 1u;
    uint state=candidate.z>>28u;
    if(state==5u || ((candidate.z>>16u)&255u)!=0u) return 2u;
    if(state<2u || state>5u) return 3u;
    if(state==3u || candidate.w<204u || (candidate.z&65535u)>1319u) return 4u;
    if(candidate.y>=2u) return 5u;
    if(((candidate.z>>24u)&15u)!=0u) return 6u;
    return 7u;
}
#if defined(QUEUE_CLASSIFY)
RWStructuredBuffer<uint> counts : register(u0);
RWStructuredBuffer<uint2> keys : register(u1);
groupshared uint groupCounts[8];
groupshared uint priorities[256];
[numthreads(256,1,1)] void main(uint index:SV_DispatchThreadID,uint local:SV_GroupIndex,uint group:SV_GroupID)
{
    if(local<8u) groupCounts[local]=0u;
    uint physical=(index+Clock*1021u)&65535u;
    uint priority=Priority(candidates[physical]); priorities[local]=priority;
    GroupMemoryBarrierWithGroupSync();
    if(priority<8u) InterlockedAdd(groupCounts[priority],1u);
    uint rank=0u;
    [loop] for(uint i=0u;i<local;++i) rank+=priorities[i]==priority?1u:0u;
    keys[index]=uint2(priority,rank);
    GroupMemoryBarrierWithGroupSync();
    if(local<8u) counts[local*256u+group]=groupCounts[local];
}
#elif defined(QUEUE_PREFIX)
StructuredBuffer<uint> counts : register(t1);
RWStructuredBuffer<uint> offsets : register(u0);
RWStructuredBuffer<uint> control : register(u1);
[numthreads(8,1,1)] void main(uint bin:SV_DispatchThreadID)
{
    uint base=0u;
    [loop] for(uint i=0u;i<bin*256u;++i) base+=counts[i];
    [loop] for(uint group=0u;group<256u;++group) {
        offsets[bin*256u+group]=base;
        base+=counts[bin*256u+group];
    }
    if(bin==7u) {
        bool invalid=Capacity==0u || Capacity>65536u;
        control[0]=invalid?0u:min(base,min(Budget,Capacity));
        control[1]=invalid?1u:0u;
    }
}
#else
StructuredBuffer<uint2> keys : register(t1);
StructuredBuffer<uint> offsets : register(t2);
StructuredBuffer<uint> control : register(t3);
RWStructuredBuffer<uint> queue : register(u0);
[numthreads(256,1,1)] void main(uint index:SV_DispatchThreadID)
{
    uint2 key=keys[index];
    if(key.x>=8u || control[1]!=0u) return;
    uint destination=offsets[key.x*256u+index/256u]+key.y;
    if(destination<control[0]) queue[destination]=candidates[(index+Clock*1021u)&65535u].x;
}
#endif
