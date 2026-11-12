// Copies one freshly rendered Skyrim point-light depth slice into the RadiantGrid
// cache. The minimum reduction preserves thin occluders when the cache resolution
// is lower than the engine's shadow-map resolution.

Texture2DArray<float> SourceShadowMaps : register(t0);
RWTexture2DArray<float> CacheShadowMaps : register(u0);

cbuffer CopyCB : register(b0)
{
	uint SourceSlice;
	uint TargetSlice;
	uint Scale;
	uint TargetSize;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID)
{
	if (any(dispatchThreadId >= TargetSize.xx) || Scale == 0u)
		return;
	const uint2 sourceBase = dispatchThreadId * Scale;
	float depth = 1.0;
	[loop] for (uint y = 0; y < Scale; y++) {
		[loop] for (uint x = 0; x < Scale; x++)
			depth = min(depth, SourceShadowMaps.Load(int4(sourceBase + uint2(x, y), SourceSlice, 0)));
	}
	CacheShadowMaps[uint3(dispatchThreadId, TargetSlice)] = depth;
}
