
namespace RadiantGrid
{

#include "RadiantGrid/Common.hlsli"

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
		Light StrictLights[15];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3

	bool GetClusterIndex(in float2 uv, in float z, inout uint clusterIndex)
	{
		const uint3 clusterSize = max(SharedData::radiantGridSettings.ClusterSize.xyz, 1u);

		if (!FrameBuffer::FrameParams.y)  // Fix first person lights
			uv = 0.5;

		const float nearPlane = max(SharedData::CameraData.y, 1.0e-3f);
		const float farPlane = max(SharedData::CameraData.x, nearPlane + 1.0f);
		z = clamp(z, nearPlane, farPlane);
		uv = clamp(uv, 0.0f, 0.999999f);

		uint clusterZ = min((uint)(log(z / nearPlane) * clusterSize.z / max(log(farPlane / nearPlane), 1.0e-5f)), clusterSize.z - 1u);
		uint3 cluster = uint3(uint2(uv * clusterSize.xy), clusterZ);

		// Bounds validation to prevent out-of-range cluster indices
		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

	bool IsLightIgnored(Light light)
	{
		if (light.lightFlags & RadiantGrid::LightFlags::Shadow) {
			return light.shadowLightIndex >= 32u || !(ShadowBitMask & (1u << light.shadowLightIndex));
		}

		bool lightIgnored = false;
		if ((light.lightFlags & LightFlags::PortalStrict) && RoomIndex >= 0 && RoomIndex < 128) {
			lightIgnored = true;
			int roomIndex = RoomIndex;
			[unroll] for (int flagsIndex = 0; flagsIndex < 4; ++flagsIndex)
			{
				if (roomIndex < 32) {
					if (((light.roomFlags[flagsIndex] >> roomIndex) & 1) == 1) {
						lightIgnored = false;
					}
					break;
				}
				roomIndex -= 32;
			}
		}
		return lightIgnored;
	}
}
