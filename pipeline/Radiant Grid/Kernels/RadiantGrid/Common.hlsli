#ifndef __LLF_COMMON_DEPENDENCY_HLSL__
#define __LLF_COMMON_DEPENDENCY_HLSL__

#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)
// Must match RadiantGrid.cpp::CLUSTER_MAX_LIGHTS. Dense interiors may exceed
// 128 overlapping lights, so retain the original 256-light visual capacity and
// allocate the matching physical storage on the CPU.
#define MAX_CLUSTER_LIGHTS 256

namespace LightFlags
{
	static const uint PortalStrict = (1 << 0);
	static const uint Shadow = (1 << 1);
	static const uint Simple = (1 << 2);

	static const uint Initialised = (1 << 8);
	static const uint Disabled = (1 << 9);
	static const uint InverseSquare = (1 << 10);
	static const uint Linear = (1 << 11);
}

struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

struct LightGrid
{
	uint offset;
	uint lightCount;
	// Two cluster-wide influence candidates used by PIXL's bounded local
	// contact-shadow selection. These occupy the original padding, so the ABI
	// and buffer stride remain unchanged.
	uint contactLight0;
	uint contactLight1;
};

struct Light
{
	float3 color;
	float fade;
	float radius;
	float invRadius;
	float fadeZone;
	float sizeBias;
	float4 positionWS;
	uint4 roomFlags;
	uint lightFlags;
	uint shadowLightIndex;
	uint pad0;
	uint pad1;
};

#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
