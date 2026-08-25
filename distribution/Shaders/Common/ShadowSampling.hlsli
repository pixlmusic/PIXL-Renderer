#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#if defined(TERRAIN_OCCLUSION)
#	include "TerrainOcclusion/TerrainOcclusion.hlsli"
#endif

#if defined(SKY_VEIL)
#	include "SkyVeil/SkyVeil.hlsli"
#endif

#if defined(AMBIENT_PROBE)
#	include "AmbientProbe/AmbientProbe.hlsli"
#elif defined(SKY_BOUNCE)
#	include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#endif

// Populated once per frame by Deferred::CopyShadowLightData from BSShadowDirectionalLight.
// Column-major float4x4 projections so HLSL `mul(proj, float4(pos, 1))` matches the
// XMMATRIX layout written by XMStoreFloat4x4 on the C++ side.
struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

#if defined(VOLUME_OCCLUSION)
#	include "VolumeOcclusion/VolumeOcclusion.hlsli"
#endif

// PIXL_GR_13BG_EXACT_UTILITY_SHADOW_RECEIVER_V1
// These bindings exist only during GroundResponse's tessellated landscape replay.
// State.cpp captures Utility/RenderShadowmask's fullscreen depth permutation, and
// GroundResponse mirrors the atlas + exact b0/b2 constants + s4 comparison sampler
// here. Normal meshes never see or consume these slots.
#if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
Texture2DArray<float4> GroundResponseDirectionalShadowAtlas : register(t104);
ByteAddressBuffer GroundResponseUtilityShadowTechnique : register(t106);
ByteAddressBuffer GroundResponseUtilityShadowGeometry : register(t107);
Texture2D<uint4> GroundResponseFocusStencil : register(t108);
Texture2DArray<float4> GroundResponseFocusShadowAtlas : register(t109);
SamplerComparisonState GroundResponseDirectionalShadowSampler : register(s7);
SamplerComparisonState GroundResponseFocusShadowSampler : register(s8);
#endif

namespace ShadowSampling
{
	static const float MinDirectionalLightMultiplier = 1e-5;
	static const float3 LightingSampleNormal = float3(0, 0, 1);
	static const float3 ImageBasedLightingNormal = float3(0, 0, -1);

	bool HasDirectionalShadows()
	{
		return SharedData::HasDirectionalShadows;
	}

	float GetWorldShadow(float3 positionWS, float3 offset)
	{
		if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_OCCLUSION)
		worldShadow = TerrainOcclusion::GetTerrainShadow(positionWS + offset, LinearSampler);
#endif

#if defined(SKY_VEIL)
		worldShadow *= SkyVeil::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, out float surfaceShadow)
	{
#if defined(EFFECT)
		float viewRayLength = min(Permutation::EffectRadius * 0.2, 256);
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#elif defined(UNDERWATER)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
#else
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#endif

		float totalRayLength = distance(endPosition, startPosition);

		const float stepSize = 32.0;  // Fixed step size in world units

		uint sampleCount = clamp(uint(totalRayLength / stepSize + 0.5), 1, 4);
		float rcpSampleCount = rcp(sampleCount);

		float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);

		float worldShadow = 0.0;
		for (uint i = 0; i < sampleCount; i++) {
			float t = (float(i) + noise) * rcpSampleCount;
			float3 sampledPositionWS = lerp(endPosition, startPosition, t);
			float worldShadowSample = ShadowSampling::GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust.xyz);
			surfaceShadow = worldShadowSample;
			worldShadow += worldShadowSample;
		}

		if (worldShadow == 0.0 && surfaceShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

#if defined(VOLUME_OCCLUSION)
		if (HasDirectionalShadows()) {
			float vsmSurfaceShadow;
			float shadow = VolumeOcclusion::GetVSMShadow3D(startPosition, endPosition, noise, sampleCount, vsmSurfaceShadow);
			surfaceShadow *= vsmSurfaceShadow;
			return worldShadow * shadow;
		}
#else
		return worldShadow;
#endif

		return worldShadow;
	}


#if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	static const uint GroundResponseRawDirectionalShadowBit = 1u << 2;
	static const uint GroundResponseFocusDirectionalShadowBit = 1u << 3;

	float4 GroundResponseLoadUtilityTechnique(uint registerIndex)
	{
		return asfloat(GroundResponseUtilityShadowTechnique.Load4(registerIndex * 16u));
	}

	float4 GroundResponseLoadUtilityGeometry(uint registerIndex)
	{
		return asfloat(GroundResponseUtilityShadowGeometry.Load4(registerIndex * 16u));
	}

	float3 GroundResponseProjectDirectionalCascade(float3 positionMS, uint cascadeIndex)
	{
		// Utility.hlsl declares float4x3 ShadowMapProj[3] at b2 c16/c19/c22 and
		// evaluates mul(transpose(matrix), float4(positionMS,1)). Each column of a
		// default column-major float4x3 occupies one float4 c-register, so three
		// raw float4 loads + dots reproduce that exact operation.
		uint baseRegister = 16u + cascadeIndex * 3u;
		float4 p = float4(positionMS, 1.0f);
		return float3(
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 0u), p),
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 1u), p),
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 2u), p));
	}

	float GroundResponseSampleDirectionalPCF(
		float2 baseUV,
		uint cascadeIndex,
		float compareValue,
		float2x2 rotationMatrix,
		float radius)
	{
		float visibility = 0.0f;
		[unroll] for (uint i = 0u; i < 8u; ++i) {
			float2 offset =
				mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * radius;
			visibility += GroundResponseDirectionalShadowAtlas.SampleCmpLevelZero(
				GroundResponseDirectionalShadowSampler,
				float3(baseUV + offset, (float)cascadeIndex),
				compareValue).x;
		}
		return visibility * 0.125f;
	}


	float3 GroundResponseProjectFocusShadow(float3 positionMS, uint focusIndex)
	{
		uint baseRegister = 4u + focusIndex * 3u;
		float4 p = float4(positionMS, 1.0f);
		return float3(
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 0u), p),
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 1u), p),
			dot(GroundResponseLoadUtilityGeometry(baseRegister + 2u), p));
	}

	float GroundResponseSampleFocusPCF(
		float2 baseUV,
		float layerIndex,
		float compareValue,
		float2x2 rotationMatrix,
		float radius)
	{
		float visibility = 0.0f;
		[unroll] for (uint i = 0u; i < 8u; ++i) {
			float2 offset =
				mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * radius;
			visibility += GroundResponseFocusShadowAtlas.SampleCmpLevelZero(
				GroundResponseFocusShadowSampler,
				float3(baseUV + offset, layerIndex),
				compareValue).x;
		}
		return visibility * 0.125f;
	}

	bool HasGroundResponseFocusDirectionalShadow()
	{
		return GroundResponseRuntime::IsRuntimeValid() &&
			(GroundRuntimeTerrainDebug & GroundResponseFocusDirectionalShadowBit) != 0u;
	}

	bool HasGroundResponseRawDirectionalShadow()
	{
		return GroundResponseRuntime::IsRuntimeValid() &&
			(GroundRuntimeTerrainDebug & GroundResponseRawDirectionalShadowBit) != 0u;
	}

	float GetGroundResponseRawDirectionalShadow(
		float3 worldPosition,
		float2 screenPixel)
	{
		// Exact registers from the uploaded PIXL Utility.hlsl:
		// b0 c1 = ShadowSampleParam, c2 = EndSplitDistances,
		// b0 c3 = StartSplitDistances; b2 c3 = ShadowLightParam.
		float4 shadowSampleParam = GroundResponseLoadUtilityTechnique(1u);
		float4 endSplitDistances = GroundResponseLoadUtilityTechnique(2u);
		float4 startSplitDistances = GroundResponseLoadUtilityTechnique(3u);
		float4 focusShadowFadeParam = GroundResponseLoadUtilityTechnique(4u);
		float4 alphaTestRef = GroundResponseLoadUtilityGeometry(2u);
		float4 shadowLightParam = GroundResponseLoadUtilityGeometry(3u);

		// State.cpp deliberately captures only Utility's RENDER_DEPTH shadowmask
		// permutation. Utility samples raw scene depth there; projecting the actual
		// displaced receiver gives the equivalent depth for the raised hull.
		float4 receiverCS = mul(
			FrameBuffer::CameraViewProj,
			float4(worldPosition, 1.0f));
		if (abs(receiverCS.w) <= 1.0e-6f)
			return 1.0f;
		float shadowMapDepth = receiverCS.z / receiverCS.w;
		if (shadowMapDepth < 0.0f || shadowMapDepth > 1.0f ||
			endSplitDistances.z < shadowMapDepth)
			return 1.0f;

		float noise =
			Random::InterleavedGradientNoise(screenPixel, SharedData::FrameCount);
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix =
			float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		uint cascadeIndex = 0u;
		float shadowMapThreshold = alphaTestRef.y;
		if (2.5f < endSplitDistances.w && endSplitDistances.y < shadowMapDepth) {
			cascadeIndex = 2u;
			shadowMapThreshold = alphaTestRef.z;
		} else if (endSplitDistances.x < shadowMapDepth) {
			cascadeIndex = 1u;
			shadowMapThreshold = alphaTestRef.z;
		}

		float3 positionLS =
			GroundResponseProjectDirectionalCascade(worldPosition, cascadeIndex);
		float shadowVisibility = GroundResponseSampleDirectionalPCF(
			positionLS.xy,
			cascadeIndex,
			positionLS.z,
			rotationMatrix,
			shadowSampleParam.z);

		// Match Utility's c0 -> c1 overlap exactly. This is the transition most
		// visible on nearby snow and is one reason the old two-cascade/VSM result
		// read as a low-resolution blob next to normal terrain.
		if (cascadeIndex < 1u && startSplitDistances.y < shadowMapDepth) {
			float3 cascade1PositionLS =
				GroundResponseProjectDirectionalCascade(worldPosition, 1u);
			float cascade1Visibility = GroundResponseSampleDirectionalPCF(
				cascade1PositionLS.xy,
				1u,
				cascade1PositionLS.z,
				rotationMatrix,
				shadowSampleParam.z);
			float blendDenominator =
				max(endSplitDistances.x - startSplitDistances.y, 1.0e-6f);
			float cascade1Blend = smoothstep(
				0.0f,
				1.0f,
				(shadowMapDepth - startSplitDistances.y) / blendDenominator);
			shadowVisibility =
				lerp(shadowVisibility, cascade1Visibility, cascade1Blend);
			shadowMapThreshold = alphaTestRef.z;
		}

		// Utility's optional focus shadow path uses screen stencil only to select
		// the focus-map index. The shadow comparison itself is still evaluated from
		// the displaced world position, so this does not restore 13BE's stale base-
		// terrain receiver problem. If no focus resources were bound, the bit is off.
		[branch] if (HasGroundResponseFocusDirectionalShadow()) {
			float4 vposOffset = GroundResponseLoadUtilityTechnique(0u);
			float2 depthUv = screenPixel * vposOffset.xy + vposOffset.zw;
			uint stencilWidth, stencilHeight, stencilLevels;
			GroundResponseFocusStencil.GetDimensions(
				0u, stencilWidth, stencilHeight, stencilLevels);
			if (stencilWidth > 0u && stencilHeight > 0u) {
				uint2 stencilCoord = min(
					uint2(saturate(depthUv) * float2(stencilWidth, stencilHeight)),
					uint2(stencilWidth - 1u, stencilHeight - 1u));
				uint stencilValue =
					GroundResponseFocusStencil.Load(int3(stencilCoord, 0)).x;
				if (stencilValue > 0u && stencilValue <= 4u) {
					uint focusIndex = stencilValue - 1u;
					float3 focusPosition =
						GroundResponseProjectFocusShadow(worldPosition, focusIndex);
					float focusLayer = startSplitDistances.w + (float)focusIndex;
					float focusCompare =
						focusPosition.z - 3.0f * shadowMapThreshold;
					float focusVisibility = GroundResponseSampleFocusPCF(
						focusPosition.xy,
						focusLayer,
						focusCompare,
						rotationMatrix,
						shadowSampleParam.z);
					shadowVisibility = min(
						shadowVisibility,
						lerp(1.0f, focusVisibility, focusShadowFadeParam[focusIndex]));
				}
			}
		}

		float shadowDistanceSq = max(shadowLightParam.z, 1.0e-6f);
		float fadeFactor =
			1.0f - pow(saturate(dot(worldPosition, worldPosition) / shadowDistanceSq), 8.0f);
		return saturate(lerp(
			1.0f * !SharedData::InInterior,
			shadowVisibility,
			fadeFactor));
	}
#endif

	float GetLightingShadow(float3 worldPosition, out float detailedShadow)
	{
		if (!HasDirectionalShadows()) {
			detailedShadow = 1.0;
			return 1.0;
		}

#if defined(VOLUME_OCCLUSION)
		float shadow = VolumeOcclusion::GetVSMShadow2D(worldPosition, detailedShadow);
		return shadow;
#else
		detailedShadow = 1.0;
		return 1.0;
#endif
	}


	float GetGroundResponseLightingShadow(
		float3 worldPosition,
		float2 screenPixel,
		out float detailedShadow)
	{
		// Keep VSM only as the broad/soft/far shadow term. 13BE's displaced world
		// receiver remains authoritative, while 13BG replaces the *detailed* term
		// with Utility's actual full-resolution 3-cascade 8-tap PCF whenever the
		// exact capture is available this frame.
		float vsmDetailedShadow = 1.0f;
		float softShadow = GetLightingShadow(worldPosition, vsmDetailedShadow);
		detailedShadow = vsmDetailedShadow;

#if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
		[branch] if (HasGroundResponseRawDirectionalShadow())
			detailedShadow =
				GetGroundResponseRawDirectionalShadow(worldPosition, screenPixel);
#endif
		return softShadow;
	}

	float3 GetRawAmbientLighting()
	{
		return max(0, SharedData::GetAmbient(LightingSampleNormal));
	}

	float3 GetAmbientLighting()
	{
		float3 ambientColor = GetRawAmbientLighting();

#if defined(AMBIENT_PROBE)
		if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
			ambientColor = AmbientProbe::GetDiffuseAmbient(ambientColor, ImageBasedLightingNormal);
		}
#endif

		return ambientColor;
	}

	float3 GetDirectionalLighting()
	{
		float llDirLightMult = (SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear) ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
		return Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, MinDirectionalLightMultiplier), SharedData::linearLightCoreSettings.isDirLightLinear) * llDirLightMult;
	}

	float3 GetSceneLightingColor()
	{
		return GetAmbientLighting() + GetDirectionalLighting();
	}

	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor)
	{
		float3 ambientColorAmb = GetAmbientLighting();
		float3 dirLightColorDir = GetDirectionalLighting();

		float inputLuma = Color::RGBToLuminance(inputColor);
		float ambientLuma = Color::RGBToLuminance(ambientColorAmb);
		float dirLightLuma = Color::RGBToLuminance(dirLightColorDir);

		float totalLuma = ambientLuma + dirLightLuma;

		if (totalLuma > 0.0 && ambientLuma > 0.0)
			ambientColorAmb *= inputLuma / totalLuma;

		float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);

		dirColor = dirLightColorAmb;
		ambientColor = ambientColorAmb;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__
