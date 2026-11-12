struct SceneTriangle
{
	float3 A;
	float3 B;
	float3 C;
	uint ProjectedCellCount;
	uint PackedMaterial;
	float2 UVA;
	float2 UVB;
	float2 UVC;
	uint TextureIndex;
};

StructuredBuffer<SceneTriangle> SourceTriangles : register(t0);
Texture2D<float4> SceneMaterialTextures[16] : register(t1);
SamplerState SceneMaterialSampler : register(s0);
RWTexture3D<uint> SceneOccupancy : register(u0);
RWTexture3D<uint> SceneSurfaceNormal : register(u1);
RWTexture3D<uint> SceneMaterial : register(u2);

cbuffer SceneVoxelizeCB : register(b4)
{
	float3 SceneWorldMin;
	float SceneVoxelSize;
	uint SceneDimension;
	uint SceneTriangleCount;
	uint2 ScenePadding;
	int3 SceneVoxelScrollOffset;
	uint SceneVoxelUpdateMode;
};

bool IsNewlyExposedVoxel(int3 cell)
{
	int3 sourceCell = cell + SceneVoxelScrollOffset;
	return any(sourceCell < 0) || any(sourceCell >= int(SceneDimension));
}

bool Separated2D(float2 axis, float2 a, float2 b, float2 c)
{
	float p0 = dot(a, axis), p1 = dot(b, axis), p2 = dot(c, axis);
	float radius = 0.5f * (abs(axis.x) + abs(axis.y));
	return min(p0, min(p1, p2)) > radius || max(p0, max(p1, p2)) < -radius;
}

bool TriangleIntersectsVoxelRect(float2 a, float2 b, float2 c, float2 center)
{
	a -= center;
	b -= center;
	c -= center;
	float2 e0 = b - a, e1 = c - b, e2 = a - c;
	return !Separated2D(float2(-e0.y, e0.x), a, b, c) &&
		!Separated2D(float2(-e1.y, e1.x), a, b, c) &&
		!Separated2D(float2(-e2.y, e2.x), a, b, c);
}

float AxisValue(float3 value, uint axis)
{
	return axis == 0u ? value.x : (axis == 1u ? value.y : value.z);
}

uint EncodeSceneNormal(float3 normal)
{
	normal *= rcp(max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1.0e-8f));
	float2 oct = normal.xy;
	if (normal.z < 0.0f)
		oct = (1.0f - abs(oct.yx)) * float2(oct.x < 0.0f ? -1.0f : 1.0f, oct.y < 0.0f ? -1.0f : 1.0f);
	uint2 quantized = (uint2)round(saturate(oct * 0.5f + 0.5f) * 32767.0f);
	return 0x80000000u | (quantized.x & 0x7fffu) | ((quantized.y & 0x7fffu) << 15u);
}

void MarkSceneVoxel(int3 voxel, float3 normal, uint packedMaterial)
{
	uint ignored;
	InterlockedOr(SceneOccupancy[uint3(voxel)], 1u, ignored);
	InterlockedMin(SceneSurfaceNormal[uint3(voxel)], EncodeSceneNormal(normal), ignored);
	InterlockedMax(SceneMaterial[uint3(voxel)], packedMaterial, ignored);
}

uint PackDiffuseMaterial(float3 color)
{
	color = saturate(all(isfinite(color)) ? color : 0.0f);
	uint3 q = (uint3)round(color * float3(255.0f, 255.0f, 127.0f));
	return ((q.x & 0xffu) << 15u) | ((q.y & 0xffu) << 7u) | (q.z & 0x7fu);
}

float3 SampleSceneMaterial(uint index, float2 uv)
{
	// Keep resource selection explicit for SM5 drivers with limited dynamic
	// resource-array indexing. Every branch samples the same material slot.
	switch (index) {
	case 0u: return SceneMaterialTextures[0].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 1u: return SceneMaterialTextures[1].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 2u: return SceneMaterialTextures[2].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 3u: return SceneMaterialTextures[3].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 4u: return SceneMaterialTextures[4].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 5u: return SceneMaterialTextures[5].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 6u: return SceneMaterialTextures[6].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 7u: return SceneMaterialTextures[7].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 8u: return SceneMaterialTextures[8].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 9u: return SceneMaterialTextures[9].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 10u: return SceneMaterialTextures[10].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 11u: return SceneMaterialTextures[11].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 12u: return SceneMaterialTextures[12].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 13u: return SceneMaterialTextures[13].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 14u: return SceneMaterialTextures[14].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	case 15u: return SceneMaterialTextures[15].SampleLevel(SceneMaterialSampler, uv, 0).rgb;
	default: return 1.0f;
	}
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint triangleIndex = dispatchThreadID.x;
	if (triangleIndex >= SceneTriangleCount || SceneDimension == 0u || !(SceneVoxelSize > 0.0f))
		return;

	SceneTriangle triData = SourceTriangles[triangleIndex];
	if (!all(isfinite(triData.A)) || !all(isfinite(triData.B)) || !all(isfinite(triData.C)))
		return;

	float3 edge0 = triData.B - triData.A;
	float3 edge1 = triData.C - triData.A;
	float3 normal = cross(edge0, edge1);
	if (dot(normal, normal) <= 1.0e-8f)
		return;

	// HTrace's geometry stage projects a triangle onto the plane selected by its
	// dominant normal component. Scanning this 2D footprint preserves broad,
	// thin floors and walls without testing their empty 3D bounding-box volume.
	uint depthAxis = 2u;
	if (abs(normal.x) > abs(normal.y) && abs(normal.x) > abs(normal.z)) depthAxis = 0u;
	else if (abs(normal.y) > abs(normal.x) && abs(normal.y) > abs(normal.z)) depthAxis = 1u;
	uint uAxis = (depthAxis + 1u) % 3u;
	uint vAxis = (depthAxis + 2u) % 3u;
	float3 localA = (triData.A - SceneWorldMin) / SceneVoxelSize;
	float3 localB = (triData.B - SceneWorldMin) / SceneVoxelSize;
	float3 localC = (triData.C - SceneWorldMin) / SceneVoxelSize;
	if (!all(isfinite(localA)) || !all(isfinite(localB)) || !all(isfinite(localC)))
		return;
	float2 a = float2(AxisValue(localA, uAxis), AxisValue(localA, vAxis));
	float2 b = float2(AxisValue(localB, uAxis), AxisValue(localB, vAxis));
	float2 c = float2(AxisValue(localC, uAxis), AxisValue(localC, vAxis));
	float2 projectedMin = min(a, min(b, c));
	float2 projectedMax = max(a, max(b, c));
	if (any(projectedMax < 0.0f) || any(projectedMin >= float(SceneDimension)))
		return;
	int2 lo = max(int2(floor(projectedMin)), 0);
	int2 hi = min(int2(floor(projectedMax)), int(SceneDimension) - 1);
	if (any(hi < lo)) return;

	int scanU = lo.x;
	int scanV = lo.y;
	// The uploaded count is the maximum projected footprint for this grid.
	// Keep the limit tied to the texture dimension: the prior 16,384-cell cap
	// truncated wide triangles after the grid grew to 256^3, leaving large
	// floors and walls only partially voxelized. At 256^3 a triangle scans at
	// most 256 * 256 candidates, then exits as soon as its clipped rows finish.
	uint maxProjectedCells = min(SceneDimension, 256u);
	maxProjectedCells *= maxProjectedCells;
	uint projectedCellLimit = min(triData.ProjectedCellCount, maxProjectedCells);
	[loop] for (uint candidate = 0u; candidate < projectedCellLimit; ++candidate) {
		if (scanV > hi.y) break;
		int u = scanU;
	int v = scanV;
		float2 center = float2(u, v) + 0.5f;
		if (TriangleIntersectsVoxelRect(a, b, c, center)) {
			uint material = triData.PackedMaterial;
			if ((triData.PackedMaterial & 0x80000000u) == 0u && triData.TextureIndex < 16u) {
				float3 tint = float3((material >> 15u) & 0xffu, (material >> 7u) & 0xffu, material & 0x7fu) / float3(255.0f, 255.0f, 127.0f);
				float2 ab = b - a, ac = c - a, ap = center - a;
				float denominator = ab.x * ac.y - ab.y * ac.x;
				if (abs(denominator) > 1.0e-8f) {
					float wb = (ap.x * ac.y - ap.y * ac.x) / denominator;
					float wc = (ab.x * ap.y - ab.y * ap.x) / denominator;
					float wa = 1.0f - wb - wc;
					float2 uv = triData.UVA * wa + triData.UVB * wb + triData.UVC * wc;
					float3 diffuse = SampleSceneMaterial(triData.TextureIndex, uv) * tint;
					material = PackDiffuseMaterial(diffuse);
				}
			}
			float depth = AxisValue(localA, depthAxis) -
				(AxisValue(normal, uAxis) * (center.x - AxisValue(localA, uAxis)) + AxisValue(normal, vAxis) * (center.y - AxisValue(localA, vAxis))) / AxisValue(normal, depthAxis);
			if (isfinite(depth) && depth >= -1.0f && depth < float(SceneDimension) + 1.0f) {
				int depthCell = int(floor(depth));
				int3 voxel = depthAxis == 0u ? int3(0, u, v) : (depthAxis == 1u ? int3(v, 0, u) : int3(u, v, 0));
				if (depthCell >= 0 && depthCell < int(SceneDimension)) {
					if (depthAxis == 0u) voxel.x = depthCell; else if (depthAxis == 1u) voxel.y = depthCell; else voxel.z = depthCell;
					if (SceneVoxelUpdateMode == 0u || IsNewlyExposedVoxel(voxel))
						MarkSceneVoxel(voxel, normal, material);
				}
				int adjacentDepth = depthCell + (AxisValue(normal, depthAxis) >= 0.0f ? 1 : -1);
				if (adjacentDepth >= 0 && adjacentDepth < int(SceneDimension)) {
					if (depthAxis == 0u) voxel.x = adjacentDepth; else if (depthAxis == 1u) voxel.y = adjacentDepth; else voxel.z = adjacentDepth;
					if (SceneVoxelUpdateMode == 0u || IsNewlyExposedVoxel(voxel))
						MarkSceneVoxel(voxel, normal, material);
				}
			}
		}
		scanU++;
		if (scanU > hi.x) { scanU = lo.x; scanV++; }
	}
}
