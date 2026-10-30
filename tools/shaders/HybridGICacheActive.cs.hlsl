// Offline/default-off sparse-decay prototype. The GPU regression harness
// compiles this after the authoritative worldCache helpers and test constants.
// No renderer allocations or shader permutations are added by this experiment.
#if defined(ACTIVE_BUILD)
Texture2D<uint> metadata : register(t0);
Texture2D<uint> generations : register(t1);
RWStructuredBuffer<uint2> active : register(u0);
RWStructuredBuffer<uint> control : register(u1);
[numthreads(8,8,1)] void main(uint2 cell : SV_DispatchThreadID)
{
    if (any(cell >= uint2(1024,64))) return;
    if ((metadata[cell] & 0xffffffu) == 0u) return;
    uint index;
    InterlockedAdd(control[0], 1u, index);
    if (index < ActiveCapacity)
        active[index] = uint2(cell.y * 1024u + cell.x, generations[cell]);
    else
        InterlockedOr(control[1], 1u);
}
#elif defined(ACTIVE_ARGUMENTS)
StructuredBuffer<uint> control : register(t0);
RWByteAddressBuffer arguments : register(u0);
[numthreads(1,1,1)] void main(uint index : SV_DispatchThreadID)
{
    bool fallback = control[1] != 0u || control[0] > ActiveCapacity;
    uint count = fallback ? 65536u : control[0];
    arguments.Store3(0, uint3((count + 63u) / 64u, 1u, 1u));
}
#else
StructuredBuffer<uint2> active : register(t0);
StructuredBuffer<uint> control : register(t1);
Texture2D<uint> generations : register(t2);
RWTexture2D<uint> metadata : register(u0);
RWTexture2D<uint> intelligence : register(u1);
[numthreads(64,1,1)] void main(uint index : SV_DispatchThreadID)
{
    bool fallback = control[1] != 0u || control[0] > ActiveCapacity;
    uint count = fallback ? 65536u : control[0];
    if (index >= count) return;
    uint2 entry = fallback ? uint2(index, 0u) : active[index];
    if (entry.x >= 65536u) return;
    uint2 cell = uint2(entry.x & 1023u, entry.x >> 10u);
    // Publication after list construction owns the newer generation. Its new
    // timestamp is evaluated next tick; an old queue entry cannot erase it.
    if (!fallback && generations[cell] != entry.y) return;
    uint meta = metadata[cell];
    if (WorldCacheExpired(meta, intelligence[cell], WorldCacheClock, WorldCacheMaxAge)) {
        metadata[cell] = meta & 0xff000000u;
        intelligence[cell] = 0u;
    }
}
#endif
