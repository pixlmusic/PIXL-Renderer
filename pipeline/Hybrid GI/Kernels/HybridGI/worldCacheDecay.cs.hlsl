#include "HybridGI/common.hlsli"
#include "HybridGI/worldCache.hlsli"

RWTexture2D<uint> outWorldMetadata : register(u0);

// Why this pass exists
// ---------------------
// worldCacheInject_cs only ever refreshes a voxel that is currently visible
// on screen. A voxel that scrolls out of view (behind the camera, occluded,
// or pushed out of the toroidal window as the camera moves) is simply never
// touched again -- its metadata just sits in the atlas.
//
// Age is reconstructed every frame from an 8-bit "frame written" stamp via
// modular subtraction: ((now & 255) - stored) & 255. That arithmetic is only
// correct while the true gap since the last write is below 256 frames. Once
// an abandoned voxel's real age passes 256, the subtraction aliases back
// down through small values once every 256 frames (~4-8 seconds depending on
// framerate) -- which made the old, stale voxel falsely evaluate as "fresh"
// again for a few frames on a fixed rhythm, then age back out. That produced
// GI and reflections that pop in and out on a beat with no relation to
// anything the camera or lighting was actually doing.
//
// Fix: explicitly stamp a voxel's hash to WORLD_CACHE_INVALID_HASH (0, which
// WorldCacheHash() never produces) the moment it first crosses
// WorldCacheMaxAge. That happens well inside the 256-frame window every
// time, so the wraparound case above can no longer occur -- a hash of 0
// will not match any real cell again until the voxel is actually reinjected
// from scratch.
//
// Dispatch once per frame over the full atlas (WORLD_CACHE_DIM * WORLD_CACHE_DIM
// x WORLD_CACHE_DIM * WORLD_CACHE_CASCADES, i.e. 1024x64), after
// worldCacheInject_cs and before gi_cs reads the cache. The whole atlas is
// only 65536 texels, so this is a negligible full pass -- no stride/jitter
// amortization needed, and none should be added: any amortization reopens
// the wraparound window for cells that get revisited late.
//
// Extension note: if WORLD_CACHE_DIM or WORLD_CACHE_CASCADES is increased
// later (bigger world, more cascades), keep this a full unstrided pass as
// long as the atlas stays comfortably sub-megapixel; only reach for
// injection-style striding here if the atlas grows enough that this pass
// shows up in a frame-time budget, and if so keep the stride small enough
// that no cell can go unswept for more than a fraction of WorldCacheMaxAge.
[numthreads(8, 8, 1)]
void main(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
	const uint2 atlasDim = uint2(WORLD_CACHE_DIM * WORLD_CACHE_DIM, WORLD_CACHE_DIM * WORLD_CACHE_CASCADES);
	if (any(dispatchThreadID >= atlasDim))
		return;

	uint metadata = outWorldMetadata[dispatchThreadID];
	uint hash = metadata & 0x00ffffffu;
	if (hash == WORLD_CACHE_INVALID_HASH)
		return;

	uint age = ((FrameIndex & 255u) - (metadata >> 24)) & 255u;
	if (age > WorldCacheMaxAge) {
		// Keep the timestamp byte (harmless, and cheaper than a full zero
		// write to reason about), just clear the hash so no future read or
		// injection can mistake this cell for the voxel it used to hold.
		outWorldMetadata[dispatchThreadID] = metadata & 0xff000000u;
	}
}


