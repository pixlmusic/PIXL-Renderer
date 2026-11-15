
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

namespace RadiantGrid
{

#include "RadiantGrid/Common.hlsli"

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint FirstPerson;
		float4 WorldEyePosition;
		Light StrictLights[15];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);       //MAX_CLUSTER_LIGHTS * 16^3
	StructuredBuffer<LightGrid> lightGrid : register(t37);  //16^3

	#include "RadiantGrid/LocalShadowSampling.hlsli"

	bool GetClusterIndex(in float2 uv, in float z, inout uint clusterIndex)
	{
		const uint3 clusterSize = max(SharedData::radiantGridSettings.ClusterSize.xyz, 1u);

		const float nearPlane = max(SharedData::CameraData.y, 1.0e-3f);
		const float farPlane = max(SharedData::CameraData.x, nearPlane + 1.0f);
		// Callers pass signed view-space Z; visible geometry is negative Z.
		// Cluster construction uses positive near/far distance magnitudes.
		z = clamp(abs(z), nearPlane, farPlane);
		uv = clamp(uv, 0.0f, 0.999999f);

		uint clusterZ = min((uint)(log(z / nearPlane) * clusterSize.z / max(log(farPlane / nearPlane), 1.0e-5f)), clusterSize.z - 1u);
		uint3 cluster = uint3(uint2(uv * clusterSize.xy), clusterZ);

		// Bounds validation to prevent out-of-range cluster indices
		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (cluster.y * clusterSize.x) + (cluster.z * clusterSize.x * clusterSize.y);
		return true;
	}

	bool IsLightIgnored(Light light)
	{
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

	// Clustered lights carry their own CPU-validated Shadow flag and mask channel.
	// ShadowBitMask belongs to the separate strict-light geometry path; consulting
	// it here can reject a valid clustered light because the two light lists need
	// not contain the same lights. Keep the channel bound check before indexing
	// shadowColor[4]. Missing/stale maps have Shadow cleared on the CPU, preserving
	// neutral visibility without removing the light itself.
	bool HasNativeShadowMap(Light light)
	{
		return (light.lightFlags & LightFlags::Shadow) != 0u && light.shadowLightIndex < 4u;
	}

	// Screen-space visibility supplement for local lights. This follows the
	// reference's receiver-to-light depth ray, but marches in view space so
	// perspective and nonlinear depth are handled by the engine's projection.
	// It only sees the current depth buffer; native maps remain responsible for
	// off-screen blockers and the full light volume.
	float GetScreenSpaceLocalShadow(SamplerState depthSampler, float2 receiverUV, float3 receiverView, float3 lightView, float lightRadius)
	{
		// The receiver UV is supplied by the active deferred pixel. Reject invalid
		// projections before issuing depth fetches; these can occur during camera
		// transitions or for malformed geometry and must leave native visibility
		// neutral.
		if (!all(isfinite(receiverView)) || !all(isfinite(lightView)) ||
			!isfinite(receiverUV.x) || !isfinite(receiverUV.y) ||
			any(receiverUV < 0.0f) || any(receiverUV > 1.0f))
			return 1.0f;
		// Spend depth rays only on nearby receivers. The camera-depth estimate
		// loses precision and coverage at range, especially outdoors; fade it
		// out before it can create distant silhouettes or cost ten taps per light.
		const float receiverDistance = -receiverView.z;
		if (receiverDistance <= 0.0f || receiverDistance >= 4096.0f)
			return 1.0f;
		const float distanceWeight = 1.0f - smoothstep(1536.0f, 4096.0f, receiverDistance);

		const float3 receiverToLight = lightView - receiverView;
		const float receiverToLightLength = length(receiverToLight);
		if (!isfinite(receiverToLightLength) || receiverToLightLength < 12.0f)
			return 1.0f;
		// Offset a few world units out of the emitter housing while retaining
		// nearby lantern bars and candle surroundings in the tested segment.
		const float sourceClearance = clamp(receiverToLightLength * 0.01f, 2.0f, 6.0f);
		const float3 lightEndpoint = lightView - receiverToLight * (sourceClearance / receiverToLightLength);
		const float3 segment = lightEndpoint - receiverView;

		const uint steps = 10u;
		const float bias = max(1.5f, -receiverView.z * 0.0015f);
		float visibility = 1.0f;
		[unroll] for (uint rayStep = 1u; rayStep <= steps; ++rayStep) {
			const float t = float(rayStep) / float(steps + 1u);
			const float3 sampleView = receiverView + segment * t;
			if (sampleView.z >= -1.0f)
				continue;
			const float2 sampleUV = FrameBuffer::ViewToUV(sampleView);
			if (!all(isfinite(sampleUV)) || any(sampleUV <= 0.001f) || any(sampleUV >= 0.999f))
				break;

			const float rawDepth = SharedData::GetDepth(sampleUV);
			if (!isfinite(rawDepth) || rawDepth >= 0.999999f)
				continue;
			const float sceneViewDepth = SharedData::GetScreenDepth(rawDepth);
			if (!isfinite(sceneViewDepth) || sceneViewDepth <= 0.0f)
				continue;
			const float rayViewDepth = -sampleView.z;
			const float blockerDistance = rayViewDepth - sceneViewDepth;
			// Positive depth separation means this visible surface lies in front
			// of the receiver-to-light segment and blocks it. Do not fade the
			// blocker back to lit as the ray travels deeper behind the surface:
			// that made ordinary wall-sized blockers disappear between the fixed
			// march samples, leaving only a narrow depth band that cast shadows.
			if (blockerDistance > bias) {
				float edge = saturate(min(min(sampleUV.x, sampleUV.y), min(1.0f - sampleUV.x, 1.0f - sampleUV.y)) * 20.0f);
				// The nearest blocker controls this ray. Continuing past it makes a
				// later, deeper sample drive every partial result to fully black. Use
				// a bounded source-size proxy (Skyrim exposes light range, not emitter
				// radius) and grow the penumbra as the blocker approaches the light.
				float safeLightRadius = isfinite(lightRadius) ? max(lightRadius, 1.0f) : 1.0f;
				float sourceRadius = clamp(sqrt(safeLightRadius) * 0.90f, 1.5f, 24.0f);
				float penumbra = clamp(sourceRadius * t / max(1.0f - t, 0.08f), 1.5f, 64.0f);
				float blocker = smoothstep(bias, bias + penumbra, blockerDistance);
				// Resolve a two-by-two depth footprint only at the first blocker.
				// This costs one gather on shadowed rays and softens subpixel
				// silhouettes without increasing the ten-step ray march.
				float2 depthUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sampleUV);
				float4 nearbyDepth = SharedData::DepthTexture.GatherRed(depthSampler, depthUV);
				if (all(isfinite(nearbyDepth))) {
					float4 nearbyViewDepth = SharedData::GetScreenDepths(min(nearbyDepth, 0.9999f));
					float4 nearbyBlockers = smoothstep(bias, bias + penumbra,
						rayViewDepth - nearbyViewDepth);
					nearbyBlockers *= 1.0f - step(0.999999f, nearbyDepth);
					if (all(isfinite(nearbyViewDepth)))
						blocker = lerp(blocker, dot(nearbyBlockers, 0.25f.xxxx), 0.65f);
				}
				visibility = min(visibility, 1.0f - blocker * edge);
				break;
			}
		}
		// This is a depth-buffer-only supplement to the native shadow map. Blend
		// its occlusion back toward neutral so a single screen-space depth sample
		// cannot add a fully hard edge or make a light appear to switch abruptly
		// as the camera crosses a depth discontinuity.
		return lerp(1.0f, visibility, 0.75f * distanceWeight);
	}

}
