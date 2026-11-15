#include "Common/FrameBuffer.hlsli"

#include "RadiantGrid/Common.hlsli"

cbuffer PerFrame : register(b0)
{
	float LightsNear;
	float LightsFar;
	uint2 pad0;  // Padding for 16-byte alignment: 8 -> 16 bytes
	uint4 ClusterSize;
}

bool TryGetPositionVS(float2 texcoord, float depth, out float3 positionVS)
{
	float4 clipSpaceLocation;
	clipSpaceLocation.xy = texcoord * 2.0f - 1.0f;  // convert from [0,1] to [-1,1]
	clipSpaceLocation.y *= -1;
	clipSpaceLocation.z = depth;
	clipSpaceLocation.w = 1.0f;
	// Build clusters from the same current-frame projection contract used by the
	// engine's screen-space cluster lookup. Substituting the separately cached
	// unjittered inverse can put cluster bounds and querying pixels in different
	// frusta while the camera rotates, causing whole 64-pixel lighting cells to
	// pop on and off.
	float4 homogenousLocation = mul(FrameBuffer::CameraProjInverse, clipSpaceLocation);
	if (!all(isfinite(homogenousLocation)) || abs(homogenousLocation.w) < 1.0e-6f) {
		positionVS = 0.0f.xxx;
		return false;
	}
	positionVS = homogenousLocation.xyz / homogenousLocation.w;
	return all(isfinite(positionVS));
}

//reference
//https://github.com/Angelo1211/HybridRenderingEngine/

RWStructuredBuffer<ClusterAABB> clusters : register(u0);

float3 IntersectionZPlane(float3 B, float z_dist)
{
	// Near/far are distance magnitudes. Preserve the inverse projection's
	// forward direction: forcing a signed plane can put cells behind the
	// camera and mirror XY, dropping lights at whole-cell boundaries.
	return B * (z_dist / max(abs(B.z), 1.0e-6f));
}

[numthreads(1, 1, 1)] void main(uint3 groupId : SV_GroupID,
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex) {
	uint clusterIndex = groupId.x +
	                    groupId.y * ClusterSize.x +
	                    groupId.z * (ClusterSize.x * ClusterSize.y);
	if (any(ClusterSize.xyz == 0u) || groupId.x >= ClusterSize.x || groupId.y >= ClusterSize.y || groupId.z >= ClusterSize.z)
		return;

	float2 clusterSize = rcp(float2(ClusterSize.x, ClusterSize.y));

	float2 texcoordMax = (groupId.xy + 1) * clusterSize;
	float2 texcoordMin = groupId.xy * clusterSize;
	// A jittered or asymmetric projection does not guarantee that the two
	// diagonal corners bound the other two corners. Build the tile AABB from
	// all four rays so lights cannot pop at 64-pixel cluster boundaries.
	// Reconstruct at an interior D3D depth. Depth 1 can map to infinity for
	// infinite-far projections and collapse the cluster bounds to the origin.
	float3 corner00VS, corner10VS, corner01VS, corner11VS;
	bool validCorners = TryGetPositionVS(float2(texcoordMin.x, texcoordMin.y), 0.5f, corner00VS);
	validCorners = TryGetPositionVS(float2(texcoordMax.x, texcoordMin.y), 0.5f, corner10VS) && validCorners;
	validCorners = TryGetPositionVS(float2(texcoordMin.x, texcoordMax.y), 0.5f, corner01VS) && validCorners;
	validCorners = TryGetPositionVS(float2(texcoordMax.x, texcoordMax.y), 0.5f, corner11VS) && validCorners;

	float safeNear = max(abs(LightsNear), 1.0e-4f);
	float safeFar = max(abs(LightsFar), safeNear + 1.0e-3f);
	if (!validCorners) {
		// A failed projection must over-include lights rather than silently
		// culling every light from a collapsed cluster.
		clusters[clusterIndex].minPoint = float4(-safeFar, -safeFar, -safeFar, 0.0f);
		clusters[clusterIndex].maxPoint = float4(safeFar, safeFar, safeFar, 0.0f);
		return;
	}
	float layerExponent = log2(safeFar / safeNear) / float(ClusterSize.z);
	float clusterNear = safeNear * exp2(layerExponent * groupId.z);
	float clusterFar = safeNear * exp2(layerExponent * (groupId.z + 1u));

	float3 near00 = IntersectionZPlane(corner00VS, clusterNear);
	float3 near10 = IntersectionZPlane(corner10VS, clusterNear);
	float3 near01 = IntersectionZPlane(corner01VS, clusterNear);
	float3 near11 = IntersectionZPlane(corner11VS, clusterNear);
	float3 far00 = IntersectionZPlane(corner00VS, clusterFar);
	float3 far10 = IntersectionZPlane(corner10VS, clusterFar);
	float3 far01 = IntersectionZPlane(corner01VS, clusterFar);
	float3 far11 = IntersectionZPlane(corner11VS, clusterFar);

	float3 minPointAABB = min(min(min(near00, near10), min(near01, near11)),
		min(min(far00, far10), min(far01, far11)));
	float3 maxPointAABB = max(max(max(near00, near10), max(near01, near11)),
		max(max(far00, far10), max(far01, far11)));
	// Cluster coordinates are reconstructed independently in the lighting pass.
	// Give adjacent cells a small shared overlap so FP rounding and projection
	// jitter cannot make a local light vanish for a pixel exactly on a cell edge.
	// The extra candidate is harmless: the light's own attenuation still limits
	// its contribution, while a tiny overlap prevents visible 64-pixel popping.
	float clusterGuard = max(0.5f, clusterFar * 1.0e-4f);
	minPointAABB -= clusterGuard.xxx;
	maxPointAABB += clusterGuard.xxx;

	clusters[clusterIndex].minPoint = float4(minPointAABB, 0.0);
	clusters[clusterIndex].maxPoint = float4(maxPointAABB, 0.0);
}
