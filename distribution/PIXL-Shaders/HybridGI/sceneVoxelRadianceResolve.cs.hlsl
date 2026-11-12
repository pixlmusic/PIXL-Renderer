#include "Common/FrameBuffer.hlsli"
#include "HybridGI/common.hlsli"

Texture3D<uint> srcSceneVoxelWinner : register(t0);
Texture2D<float> srcSceneVoxelDepth : register(t1);
Texture2D<float3> srcSceneRadiance : register(t2);
RWTexture3D<uint2> outSceneVoxelRadiance : register(u0);

cbuffer SceneVoxelCB : register(b3)
{
	float3 SceneVoxelWorldMin;
	float SceneVoxelSize;
	uint SceneVoxelDimension;
	uint SceneVoxelValid;
	uint SceneVoxelRadianceStride;
	uint SceneVoxelPadding;
};

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
	if (any(cell >= SceneVoxelDimension))
		return;
	// This volume is world-space data. Replacing its direct-light solution with
	// one camera's raster winner made radiance appear in moving voxel-sized
	// patches and made lights vanish when their surfaces left the view. Keep the
	// result written by sceneVoxelLighting; that pass updates the full field.
	uint2 worldSpaceRadiance = outSceneVoxelRadiance[cell];
	outSceneVoxelRadiance[cell] = worldSpaceRadiance;
}
