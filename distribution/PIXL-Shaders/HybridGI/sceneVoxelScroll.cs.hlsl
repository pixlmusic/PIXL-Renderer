Texture3D<uint> SourceOccupancy : register(t0);
RWTexture3D<uint> DestinationOccupancy : register(u0);
Texture3D<uint> SourceSurfaceNormal : register(t1);
RWTexture3D<uint> DestinationSurfaceNormal : register(u1);
Texture3D<uint> SourceMaterial : register(t2);
RWTexture3D<uint> DestinationMaterial : register(u2);

cbuffer SceneVoxelScrollCB : register(b4)
{
	int3 SourceOffset;
	uint SceneDimension;
};

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (any(dispatchThreadID >= SceneDimension))
		return;

	int3 sourceCell = int3(dispatchThreadID) + SourceOffset;
	uint value = 0u;
	uint normal = 0xffffffffu;
	uint material = 0u;
	if (all(sourceCell >= 0) && all(sourceCell < int(SceneDimension)))
	{
		value = SourceOccupancy.Load(int4(sourceCell, 0));
		normal = SourceSurfaceNormal.Load(int4(sourceCell, 0));
		material = SourceMaterial.Load(int4(sourceCell, 0));
	}
	DestinationOccupancy[dispatchThreadID] = value;
	DestinationSurfaceNormal[dispatchThreadID] = normal;
	DestinationMaterial[dispatchThreadID] = material;
}
