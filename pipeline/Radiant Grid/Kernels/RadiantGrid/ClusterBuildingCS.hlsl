#include "Common/FrameBuffer.hlsli"

#include "RadiantGrid/Common.hlsli"

cbuffer PerFrame : register(b0)
{
	float LightsNear;
	float LightsFar;
	uint2 pad0;  // Padding for 16-byte alignment: 8 -> 16 bytes
	uint4 ClusterSize;
}

float3 GetPositionVS(float2 texcoord, float depth)
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
	if (!isfinite(homogenousLocation.w) || abs(homogenousLocation.w) < 1.0e-6f)
		return 0.0f.xxx;
	return homogenousLocation.xyz / homogenousLocation.w;
}

//reference
//https://github.com/Angelo1211/HybridRenderingEngine/

RWStructuredBuffer<ClusterAABB> clusters : register(u0);

float3 IntersectionZPlane(float3 B, float z_dist)
{
	//Because this is a Z based normal this is fixed
	float3 normal = float3(0.0, 0.0, -1.0);
	float3 d = B;
	//Computing the intersection length for the line and the plane
	float safeZ = abs(d.z) > 1.0e-6f ? d.z : (d.z < 0.0f ? -1.0e-6f : 1.0e-6f);
	float t = z_dist / safeZ;  //dot(normal, d);

	//Computing the actual xyz position of the point along the line
	float3 result = t * d;

	return result;
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
	float3 maxPointVS = GetPositionVS(texcoordMax, 1.0f);
	float3 minPointVS = GetPositionVS(texcoordMin, 1.0f);

	float safeNear = max(abs(LightsNear), 1.0e-4f);
	float safeFar = max(abs(LightsFar), safeNear + 1.0e-3f);
	float layerExponent = log2(safeFar / safeNear) / float(ClusterSize.z);
	float clusterNear = safeNear * exp2(layerExponent * groupId.z);
	float clusterFar = safeNear * exp2(layerExponent * (groupId.z + 1u));

	float3 minPointNear = IntersectionZPlane(minPointVS, clusterNear);
	float3 minPointFar = IntersectionZPlane(minPointVS, clusterFar);
	float3 maxPointNear = IntersectionZPlane(maxPointVS, clusterNear);
	float3 maxPointFar = IntersectionZPlane(maxPointVS, clusterFar);

	float3 minPointAABB = min(min(minPointNear, minPointFar), min(maxPointNear, maxPointFar));
	float3 maxPointAABB = max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar));

	clusters[clusterIndex].minPoint = float4(minPointAABB, 0.0);
	clusters[clusterIndex].maxPoint = float4(maxPointAABB, 0.0);
}
