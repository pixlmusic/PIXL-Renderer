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
	static const uint ShadowCaster = (1 << 3);
	static const uint LocalShadow = (1 << 4);

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
	// Four cluster-wide influence candidates used by PIXL's bounded local
	// contact-shadow selection.
	uint contactLight0;
	uint contactLight1;
	uint contactLight2;
	uint contactLight3;
	// Independent bounded candidates for geometric SDF visibility, including
	// lights whose native maps can be conservatively refined by SDFs.
	uint sdfLight0;
	uint sdfLight1;
	uint sdfLight2;
	uint sdfLight3;
	uint2 pad;
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
	uint localShadowIndex;
	uint pad1;
};

#endif  //__LLF_COMMON_DEPENDENCY_HLSL__
