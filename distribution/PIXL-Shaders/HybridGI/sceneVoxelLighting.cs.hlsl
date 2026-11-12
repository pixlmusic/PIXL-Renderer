#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"
#include "RadiantGrid/RadiantGrid.hlsli"
#include "NaturalLighting/NaturalLighting.hlsli"

Texture3D<uint> srcSceneVoxelOccupancy : register(t0);
Texture3D<uint> srcSceneVoxelSurface : register(t1);
Texture3D<uint> srcSceneVoxelMaterial : register(t2);
RWTexture3D<uint2> outSceneVoxelRadiance : register(u0);
SamplerState samplerLinearClamp : register(s0);

// SceneVoxelCB is also consumed by the screen-radiance resolve and GI trace.
// This pass uses b4 to leave RadiantGrid's real interior-room light contract at b3.
cbuffer SceneVoxelLightingCB : register(b4)
{
	float3 SceneVoxelWorldMin;
	float SceneVoxelSize;
	uint SceneVoxelDimension;
	uint SceneVoxelValid;
	uint SceneVoxelRadianceStride;
	uint SceneVoxelPadding;
};

bool IsFiniteScalar(float value)
{
	return (asuint(value) & 0x7f800000u) != 0x7f800000u;
}

bool IsFiniteVector(float3 value)
{
	return IsFiniteScalar(value.x) && IsFiniteScalar(value.y) && IsFiniteScalar(value.z);
}

float3 DecodeSceneNormal(uint packed)
{
	float2 oct = float2(packed & 0x7fffu, (packed >> 15u) & 0x7fffu) / 32767.0f * 2.0f - 1.0f;
	float3 normal = float3(oct, 1.0f - abs(oct.x) - abs(oct.y));
	if (normal.z < 0.0f)
		normal.xy = (1.0f - abs(normal.yx)) * float2(normal.x < 0.0f ? -1.0f : 1.0f, normal.y < 0.0f ? -1.0f : 1.0f);
	float lengthSq = dot(normal, normal);
	return lengthSq > 1.0e-8f && IsFiniteScalar(lengthSq) ? normal * rsqrt(lengthSq) : float3(0.0f, 0.0f, 1.0f);
}

float3 DecodeMaterialColor(uint packed)
{
	return float3((packed >> 15u) & 0xffu, (packed >> 7u) & 0xffu, packed & 0x7fu) /
		float3(255.0f, 255.0f, 127.0f);
}

float3 DecodeEmissive(uint packed)
{
	float3 encoded = DecodeMaterialColor(packed);
	float denominator = max(1.0f / 256.0f, 1.0f - max(encoded.x, max(encoded.y, encoded.z)));
	return encoded / denominator;
}

float3 EvaluateVoxelLight(RadiantGrid::Light light, float3 renderPosition, float3 normal, float3 albedo)
{
	if ((light.lightFlags & RadiantGrid::LightFlags::Disabled) != 0u ||
		((light.lightFlags & RadiantGrid::LightFlags::PortalStrict) != 0u && RadiantGrid::IsLightIgnored(light)))
		return 0.0f;
	float3 toLight = light.positionWS.xyz - renderPosition;
	float distanceSq = dot(toLight, toLight);
	if (!(distanceSq > 1.0e-6f) || !IsFiniteScalar(distanceSq))
		return 0.0f;
	float distance = sqrt(distanceSq);
	float3 direction = toLight / distance;
	float nDotL = saturate(dot(normal, direction));
	if (nDotL <= 0.0f)
		return 0.0f;
	float attenuation = NaturalLighting::GetAttenuation(distance, light);
	if (!(attenuation > 1.0e-6f) || !IsFiniteScalar(attenuation))
		return 0.0f;
	float visibility = 1.0f;
	if ((light.lightFlags & RadiantGrid::LightFlags::LocalShadow) != 0u)
		visibility = saturate(RadiantGrid::GetLocalShadow(samplerLinearClamp, light.localShadowIndex,
			renderPosition, FrameBuffer::CameraPosAdjust.xyz, direction, false, 0.0f, 1u));
	bool linearLight = (light.lightFlags & RadiantGrid::LightFlags::Linear) != 0u;
	float3 energy = Color::PointLight(light.color, linearLight) * attenuation * max(light.fade, 0.0f);
	return energy * albedo * (nDotL * visibility * 0.31830988618f);
}

void StoreRadiance(uint3 cell, float3 radiance)
{
	radiance = clamp(IsFiniteVector(radiance) ? max(radiance, 0.0f) : 0.0f, 0.0f, 65504.0f);
	uint packedRG = f32tof16(radiance.r) | (f32tof16(radiance.g) << 16u);
	uint packedBValid = f32tof16(radiance.b) | (0x3c00u << 16u);
	outSceneVoxelRadiance[cell] = uint2(packedRG, packedBValid);
}

[numthreads(4, 4, 4)]
void main(uint3 cell : SV_DispatchThreadID)
{
	if (any(cell >= SceneVoxelDimension))
		return;
	if (SceneVoxelValid == 0u || !(SceneVoxelSize > 0.0f) || srcSceneVoxelOccupancy.Load(int4(cell, 0)) == 0u) {
		StoreRadiance(cell, 0.0f);
		return;
	}

	uint packedNormal = srcSceneVoxelSurface.Load(int4(cell, 0));
	uint packedMaterial = srcSceneVoxelMaterial.Load(int4(cell, 0));
	float3 normal = DecodeSceneNormal(packedNormal);
	if ((packedMaterial & 0x80000000u) != 0u) {
		StoreRadiance(cell, DecodeEmissive(packedMaterial));
		return;
	}

	float3 albedo = saturate(DecodeMaterialColor(packedMaterial));
	float3 worldPosition = SceneVoxelWorldMin + (float3(cell) + 0.5f) * SceneVoxelSize;
	float3 renderPosition = worldPosition - FrameBuffer::CameraPosAdjust.xyz;
	float3 radiance = 0.0f;
	// Voxels describe world space, not the current camera frustum. Cluster
	// lookup here made off-screen lights disappear as the player turned, and
	// left visible, coarse patches as screen winners repopulated nearby cells.
	// Evaluate the bounded active-light list by distance instead. Portal-strict
	// lights remain in the room-aware list below to avoid illuminating other rooms.
	if (IsFiniteVector(renderPosition)) {
		[loop] for (uint i = 0u; i < min(SceneVoxelPadding, 1024u); ++i) {
			RadiantGrid::Light light = RadiantGrid::lights[i];
			if ((light.lightFlags & RadiantGrid::LightFlags::PortalStrict) != 0u)
				continue;
			float3 toLight = light.positionWS.xyz - renderPosition;
			float radius = max(light.radius, 0.0f);
			if (dot(toLight, toLight) > radius * radius)
				continue;
			radiance += EvaluateVoxelLight(light, renderPosition, normal, albedo);
		}
	}

	// Skyrim interior lights are supplied through the room-aware strict-light
	// buffer instead of the exterior cluster list. Keep that native room filter.
	[loop] for (uint i = 0u; i < min(RadiantGrid::NumStrictLights, 15u); ++i)
		radiance += EvaluateVoxelLight(RadiantGrid::StrictLights[i], renderPosition, normal, albedo);
	StoreRadiance(cell, radiance);
}
