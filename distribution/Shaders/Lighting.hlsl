#define LIGHTING

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/LodLandscape.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#if defined(VSHADER) && defined(FOLIAGE_DYNAMICS) && defined(TREE_ANIM)
#	include "FoliageDynamics/FoliageWind.hlsli"
#endif
#if defined(EYE)
#	include "EyeRendering/EyeRendering.hlsli"
#endif
#include "Common/Skinned.hlsli"
#include "Common/Triplanar.hlsli"

#if defined(FACEGEN) || defined(FACEGEN_RGB_TINT)
#	define SKIN
#endif

#if !defined(WORLD_PROBES) && defined(AMBIENT_PROBE)
#	undef AMBIENT_PROBE
#endif

#if (defined(TREE_ANIM) || defined(LANDSCAPE)) && !defined(VC)
#	define VC
#endif  // TREE_ANIM || LANDSCAPE || !VC

#if defined(LODOBJECTS) || defined(LODOBJECTSHD) || defined(LODLANDNOISE) || defined(WORLD_MAP)
#	define LOD
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord0: TEXCOORD0;
#if !defined(MODELSPACENORMALS)
	float4 Normal: NORMAL0;
	float4 Bitangent: BINORMAL0;
#endif  // !MODELSPACENORMALS

#if defined(VC)
	float4 Color: COLOR0;
#	if defined(LANDSCAPE)
	float4 LandBlendWeights1: TEXCOORD2;
	float4 LandBlendWeights2: TEXCOORD3;
#	endif  // LANDSCAPE
#endif      // VC
#if defined(SKINNED)
	float4 BoneWeights: BLENDWEIGHT0;
	float4 BoneIndices: BLENDINDICES0;
#endif  // SKINNED
#if defined(EYE)
	float EyeParameter: TEXCOORD2;
#endif  // EYE
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
#if (defined(PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
	float4
#else
	float2
#endif  // (defined (PROJECTED_UV) && !defined(SKINNED)) || defined(LANDSCAPE)
		TexCoord0: TEXCOORD0;

#if defined(WORLD_MAP)
	float3 InputPosition: TEXCOORD4;
#endif

#if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3 TBN0: TEXCOORD1;
	float3 TBN1: TEXCOORD2;
	float3 TBN2: TEXCOORD3;
#endif  // defined(SKINNED) || !defined(MODELSPACENORMALS)
#if defined(EYE)
	float3 EyeNormal: TEXCOORD6;
#elif defined(LANDSCAPE)
	float4 LandBlendWeights1: TEXCOORD6;
	float4 LandBlendWeights2: TEXCOORD7;
#elif defined(PROJECTED_UV) && !defined(SKINNED)
	float3 TexProj: TEXCOORD7;
#endif  // EYE

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 Color: COLOR0;
	float4 FogParam: COLOR1;

	float3 ModelPosition: TEXCOORD12;
#if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	// Base (undisplaced) terrain position survives the GroundResponse HS/DS path.
	// Lighting still receives WorldPosition at the unified geometric snow/mud surface.
	float3 GroundBaseWorldPosition: TEXCOORD13;
#endif
};
#ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
	float4 HighDetailRange : packoffset(c0);  // loaded cells center in xy, size in zw
	float4 FogParam : packoffset(c1);
	float4 FogNearColor : packoffset(c2);
	float4 FogFarColor : packoffset(c3);
};

cbuffer PerMaterial : register(b1)
{
	float4 LeftEyeCenter : packoffset(c0);
	float4 RightEyeCenter : packoffset(c1);
	float4 TexcoordOffset : packoffset(c2);
};

cbuffer PerGeometry : register(b2)
{
	row_major float3x4 World : packoffset(c0);
	row_major float3x4 PreviousWorld : packoffset(c3);
	float4 EyePosition : packoffset(c6);
	float4 LandBlendParams : packoffset(c7);  // offset in xy, gridPosition in yw
	float4 TreeParams : packoffset(c8);       // wind magnitude in y, amplitude in z, leaf frequency in w
	float2 WindTimers : packoffset(c9);
	row_major float3x4 TextureProj : packoffset(c10);
	float IndexScale : packoffset(c13);
	float4 WorldMapOverlayParameters : packoffset(c14);
};

cbuffer VS_PerFrame : register(b12)
{
	row_major float3x3 ScreenProj : packoffset(c0);
	row_major float4x4 ViewProj : packoffset(c8);
#	if defined(SKINNED)
	float3 BonesPivot : packoffset(c40);
	float3 PreviousBonesPivot : packoffset(c41);
#	endif  // SKINNED
};

#	if defined(TREE_ANIM)
#		ifndef USE_PIXL_MULTI_FREQUENCY_TREE_WIND
#			define USE_PIXL_MULTI_FREQUENCY_TREE_WIND 1
#		endif
#		ifndef USE_PIXL_FLOW_WIND
#			define USE_PIXL_FLOW_WIND 1
#		endif

float2 GetTreeShiftVector(float4 position, float4 color)
{
	precise float4 tmp1 = (TreeParams.w * TreeParams.y).xxxx * WindTimers.xxyy;
	precise float4 tmp2 = float4(0.1, 0.25, 0.1, 0.25) * tmp1 + dot(position.xyz, 1.0.xxx).xxxx;
	precise float4 tmp3 = abs(-1.0.xxxx + 2.0.xxxx * frac(0.5.xxxx + tmp2.xyzw));
	precise float4 tmp4 = (tmp3 * tmp3) * (3.0.xxxx - 2.0.xxxx * tmp3);
	float2 legacyShift = (tmp4.xz + 0.1.xx * tmp4.yw) * (TreeParams.z * color.w).xx;
	float2 result = legacyShift;

#		if USE_PIXL_MULTI_FREQUENCY_TREE_WIND && USE_PIXL_FLOW_WIND
	if (SharedData::foliageDynamicsSettings.EnableEnhancedWind != 0) {
		float spatialScale = max(SharedData::foliageDynamicsSettings.WindSpatialScale, 0.05f);
		float gustStrength = max(SharedData::foliageDynamicsSettings.GustStrength, 0.0f);
		float flutterStrength = max(SharedData::foliageDynamicsSettings.FlutterStrength, 0.0f);

		// Keep TREE_ANIM vertex permutations off FrameBuffer b12. Shared b5 already
		// carries PIXL's restored world origin, so gust cells remain stationary when
		// Skyrim recentres its camera-relative object matrices.
		float3 treeAnchorWS =
			float3(World[0].w, World[1].w, World[2].w) +
			SharedData::CameraPosAdjust;
		float2 fieldPosition = treeAnchorWS.xy + position.xy * 0.20f;
		const float2 flowAxis = float2(0.8192319f, 0.5734624f);
		float gustSpeed = max(SharedData::foliageDynamicsSettings.GustSpeed, 0.0f);
		float flutterSpeed = max(SharedData::foliageDynamicsSettings.FlutterSpeed, 0.0f);

		float2 broadNoise = float2(
			FoliageWind::AdvectedNoise(fieldPosition, WindTimers.x, flowAxis, spatialScale, gustSpeed, 0.00042f, 7.1f),
			FoliageWind::AdvectedNoise(fieldPosition, WindTimers.y, flowAxis, spatialScale, gustSpeed, 0.00042f, 7.1f));
		float2 branchNoise = float2(
			FoliageWind::AdvectedNoise(fieldPosition + position.xy * 0.31f, WindTimers.x, flowAxis, spatialScale * 1.45f, gustSpeed * 0.87f, 0.00135f, 23.7f),
			FoliageWind::AdvectedNoise(fieldPosition + position.xy * 0.31f, WindTimers.y, flowAxis, spatialScale * 1.45f, gustSpeed * 0.87f, 0.00135f, 23.7f));
		float2 gust = float2(
			FoliageWind::SmoothGust(broadNoise.x),
			FoliageWind::SmoothGust(broadNoise.y));
		float2 branchWave = branchNoise * 2.0f - 1.0f;
		float localPhase = dot(position.xyz, float3(0.09f, 0.13f, 0.17f));
		float2 flutterPhase =
			localPhase.xx + WindTimers * (2.15f * flutterSpeed) + branchWave * 1.7f;
		float2 tinyWave = sin(flutterPhase) * sin(flutterPhase * 0.61f + 1.93f);

		float tip = saturate(color.w);
		float secondaryMask = tip * tip;
		float tinyMask = secondaryMask * secondaryMask;

		// Preserve Skyrim's authored tree animation as the structural motion.
		// PIXL modulates its energy instead of replacing it with a second sway model.
		float gustAmount = min(gustStrength, 2.0f);
		float flutterAmount = min(flutterStrength, 2.0f);
		float2 primary =
			legacyShift * (1.0f + (gust - 0.5f.xx) * (0.32f * gustAmount));

		// TREE_ANIM sometimes supplies a zero weather amplitude for smaller plants.
		// Give those weighted branch tips a restrained ambient motion floor while
		// retaining TreeParams.z as the authority during real wind.
		float treeEnergy = max(abs(TreeParams.z), 1.25f);
		float2 secondary =
			treeEnergy * secondaryMask * branchWave *
			(0.020f + 0.026f * gustAmount) * (0.38f.xx + 0.62f * gust);
		float2 tiny =
			treeEnergy * tinyMask * tinyWave * (0.022f * flutterAmount);

		// Keep authored tree motion authoritative. WindStrength controls only the
		// additional PIXL frequencies; it must not multiply the complete bend.
		float enhancement = saturate(
			max(SharedData::foliageDynamicsSettings.WindStrength, 0.0f) * 0.5f);
		float2 enhancedDelta = (primary - legacyShift) + secondary + tiny;
		float maxDelta = treeEnergy * tip * (0.12f + 0.08f * gustAmount);
		enhancedDelta = clamp(enhancedDelta, -maxDelta.xx, maxDelta.xx);
		result += enhancedDelta * enhancement;
	}
#		endif

	return result;
}
#	endif  // TREE_ANIM

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	precise float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	inputPosition = LodLandscape::AdjustLodLandscapeVertexPositionMS(inputPosition, float4x4(World, float4(0, 0, 0, 1)), HighDetailRange);
#	endif  // defined(LODLANDNOISE) || defined(LODLANDSCAPE)                                                                   \

	precise float4 previousInputPosition = inputPosition;

#	if defined(TREE_ANIM)
	precise float2 treeShiftVector = GetTreeShiftVector(input.Position, input.Color);
	float3 normal = -1.0.xxx + 2.0.xxx * input.Normal.xyz;

	inputPosition.xyz += normal.xyz * treeShiftVector.x;
	previousInputPosition.xyz += normal.xyz * treeShiftVector.y;
#	endif

#	if defined(SKINNED)
	precise int4 actualIndices = 765.01.xxxx * input.BoneIndices.xyzw;

	float3x4 previousWorldMatrix =
		Skinned::GetBoneTransformMatrix(PreviousBones, actualIndices, PreviousBonesPivot, input.BoneWeights);
	precise float4 previousWorldPosition =
		float4(mul(inputPosition, transpose(previousWorldMatrix)), 1);

	float3x4 worldMatrix = Skinned::GetBoneTransformMatrix(Bones, actualIndices, BonesPivot, input.BoneWeights);
	precise float4 worldPosition = float4(mul(inputPosition, transpose(worldMatrix)), 1);

	float4 viewPos = mul(ViewProj, worldPosition);
#	else   // !SKINNED
	precise float4 previousWorldPosition = float4(mul(PreviousWorld, inputPosition), 1);
	precise float4 worldPosition = float4(mul(World, inputPosition), 1);
	precise float4x4 world4x4 = float4x4(World[0], World[1], World[2], float4(0, 0, 0, 1));
	precise float4x4 modelView = mul(ViewProj, world4x4);
	float4 viewPos = mul(modelView, inputPosition);
#	endif  // SKINNED

	vsout.Position = viewPos;

#	if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	vsout.Position.z += min(1, 1e-4 * max(0, viewPos.z - 70000)) * 0.5;
#	endif

	float2 uv = input.TexCoord0.xy * TexcoordOffset.zw + TexcoordOffset.xy;
#	if defined(LANDSCAPE)
	vsout.TexCoord0.zw = (uv * 0.010416667.xx + LandBlendParams.xy) * float2(1, -1) + float2(0, 1);
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	vsout.TexCoord0.z = mul(TextureProj[0], inputPosition);
	vsout.TexCoord0.w = mul(TextureProj[1], inputPosition);
#	endif
	vsout.TexCoord0.xy = uv;

#	if defined(WORLD_MAP)
	vsout.InputPosition.xyz = WorldMapOverlayParameters.xyz + worldPosition.xyz;
#	endif

#	if defined(SKINNED)
	float3x3 boneRSMatrix = Skinned::GetBoneRSMatrix(Bones, actualIndices, input.BoneWeights);
#	endif

#	if !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(
		float3(input.Position.w, input.Normal.w * 2 - 1, input.Bitangent.w * 2 - 1),
		input.Bitangent.xyz * 2.0.xxx + -1.0.xxx,
		input.Normal.xyz * 2.0.xxx + -1.0.xxx);
	float3x3 tbnTr = transpose(tbn);

#		if defined(SKINNED)
	float3x3 worldTbnTr = transpose(mul(transpose(tbnTr), transpose(boneRSMatrix)));
	float3x3 worldTbnTrTr = transpose(worldTbnTr);
	worldTbnTrTr[0] = normalize(worldTbnTrTr[0]);
	worldTbnTrTr[1] = normalize(worldTbnTrTr[1]);
	worldTbnTrTr[2] = normalize(worldTbnTrTr[2]);
	worldTbnTr = transpose(worldTbnTrTr);
	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#		else
	vsout.TBN0.xyz = mul(tbn, World[0].xyz);
	vsout.TBN1.xyz = mul(tbn, World[1].xyz);
	vsout.TBN2.xyz = mul(tbn, World[2].xyz);
	float3x3 tempTbnTr = transpose(float3x3(vsout.TBN0.xyz, vsout.TBN1.xyz, vsout.TBN2.xyz));
	tempTbnTr[0] = normalize(tempTbnTr[0]);
	tempTbnTr[1] = normalize(tempTbnTr[1]);
	tempTbnTr[2] = normalize(tempTbnTr[2]);
	tempTbnTr = transpose(tempTbnTr);
	vsout.TBN0.xyz = tempTbnTr[0];
	vsout.TBN1.xyz = tempTbnTr[1];
	vsout.TBN2.xyz = tempTbnTr[2];
#		endif
#	elif defined(SKINNED)
	float3x3 boneRSMatrixTr = transpose(boneRSMatrix);
	float3x3 worldTbnTr = transpose(float3x3(normalize(boneRSMatrixTr[0]),
		normalize(boneRSMatrixTr[1]), normalize(boneRSMatrixTr[2])));

	vsout.TBN0.xyz = worldTbnTr[0];
	vsout.TBN1.xyz = worldTbnTr[1];
	vsout.TBN2.xyz = worldTbnTr[2];
#	endif

#	if defined(LANDSCAPE)
	vsout.LandBlendWeights1 = input.LandBlendWeights1;

	float2 gridOffset = LandBlendParams.zw - input.Position.xy;
	vsout.LandBlendWeights2.w = 1 - saturate(0.000375600968 * (9625.59961 - length(gridOffset)));
	vsout.LandBlendWeights2.xyz = input.LandBlendWeights2.xyz;
#	elif defined(PROJECTED_UV) && !defined(SKINNED)
	float3x3 texProjWorld3x3 = float3x3(World[0].xyz, World[1].xyz, World[2].xyz);
	vsout.TexProj = mul(texProjWorld3x3, TextureProj[2].xyz);
#	endif

#	if defined(EYE)
	precise float4 modelEyeCenter = float4(LeftEyeCenter.xyz + input.EyeParameter.xxx * (RightEyeCenter.xyz - LeftEyeCenter.xyz), 1);
	vsout.EyeNormal.xyz = normalize(worldPosition.xyz - mul(modelEyeCenter, transpose(worldMatrix)));
#	endif  // EYE

	vsout.WorldPosition = worldPosition;
	vsout.PreviousWorldPosition = previousWorldPosition;
#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	vsout.GroundBaseWorldPosition = worldPosition.xyz;
#	endif

#	if defined(VC)
	vsout.Color = input.Color;
#	else
	vsout.Color = 1.0.xxxx;
#	endif  // VC

	float fogColorParam = min(FogParam.w,
		exp2(FogParam.z * log2(saturate(length(viewPos.xyz) * FogParam.y - FogParam.x))));

	vsout.FogParam.xyz = lerp(FogNearColor.xyz, FogFarColor.xyz, fogColorParam);
	vsout.FogParam.w = fogColorParam;

	vsout.ModelPosition = input.Position.xyz;

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#if !defined(LANDSCAPE)
#	undef TERRAIN_SEAM
#endif

// -------------------------------------------------------------------------------------------------
// PIXL Vanilla Auto-POM + depth-correct POM
// -------------------------------------------------------------------------------------------------
// Keep these compile-time so no SharedData ABI or CPU-side settings change is required.
#ifndef USE_PIXL_AUTO_PARALLAX
#	define USE_PIXL_AUTO_PARALLAX 1
#endif
#ifndef PIXL_AUTO_PARALLAX_SELF_SHADOWS
#	define PIXL_AUTO_PARALLAX_SELF_SHADOWS 0
#endif
#if USE_PIXL_AUTO_PARALLAX && !defined(LANDSCAPE) && !defined(LOD) && !defined(LODLANDSCAPE) && !defined(LOD_LAND_BLEND) && \
	!defined(PARALLAX) && !defined(MATERIAL_FORGE) && !defined(MULTI_LAYER_PARALLAX) && \
	!defined(SKIN) && !defined(HAIR) && !defined(EYE) && !defined(TREE_ANIM) && \
	!defined(SPARKLE) && !defined(PROJECTED_UV) && !defined(DO_ALPHA_TEST) && !defined(DEPTH_WRITE_DECALS)
#	define PIXL_AUTO_PARALLAX
#endif

#ifndef USE_PIXL_PARALLAX_DEPTH_CORRECTION
#	define USE_PIXL_PARALLAX_DEPTH_CORRECTION 1
#endif
#ifndef PIXL_PARALLAX_DEPTH_SCALE
#	define PIXL_PARALLAX_DEPTH_SCALE 1.0f
#endif
#ifndef PIXL_PARALLAX_DEPTH_MAX_WORLD
#	define PIXL_PARALLAX_DEPTH_MAX_WORLD 16.0f
#endif
#ifndef PIXL_PARALLAX_DEPTH_MAX_UV
#	define PIXL_PARALLAX_DEPTH_MAX_UV 0.12f
#endif
#ifndef PIXL_PARALLAX_DEPTH_MIN_GRAZING
#	define PIXL_PARALLAX_DEPTH_MIN_GRAZING 0.06f
#endif
#ifndef PIXL_PARALLAX_DEPTH_ALLOW_PROTRUSION
#	define PIXL_PARALLAX_DEPTH_ALLOW_PROTRUSION 0
#endif
#ifndef PIXL_PARALLAX_DEPTH_AUTO
// Auto-POM reconstructs relief from vanilla colour/normal data. That is excellent
// for shading/UV relief, but not reliable enough to become global scene geometry:
// screen-space contact rays can interpret tiny reconstructed recesses as blockers.
// Keep authored displacement depth-correct, but leave synthetic Auto-POM depth opt-in.
#	define PIXL_PARALLAX_DEPTH_AUTO 0
#endif
#ifndef PIXL_PARALLAX_DEPTH_MATERIAL_FORGE
// Keep Material Forge POM shading, but disable its experimental hardware-depth
// rewrite. On some static/rock UV frames that reconstruction can form triangular
// depth shards. Authored PARALLAX permutations keep their dedicated depth path.
#	define PIXL_PARALLAX_DEPTH_MATERIAL_FORGE 0
#endif
#ifndef PIXL_PARALLAX_DEPTH_LANDSCAPE
// Generic terrain-POM hardware depth is deliberately opt-in. Ground deformation
// has its own bounded world-space depth path below and must not depend on UV POM.
#	define PIXL_PARALLAX_DEPTH_LANDSCAPE 0
#endif

// Dedicated snow/mud depth output. This remains available even when generic
// landscape POM depth is disabled, preventing the two unrelated systems from
// corrupting each other's SV_Depth result.
// PIXL_GR_13AW_RASTER_DEPTH_ONLY_DIAGNOSTIC_V1
#if 0 && defined(DEFERRED) && defined(GROUND_RESPONSE) && defined(LANDSCAPE) && \
	!defined(DO_ALPHA_TEST) && !defined(DEPTH_WRITE_DECALS) && !defined(WORLD_MAP)
#	define PIXL_GROUND_DEFORMATION_DEPTH
#endif

#if USE_PIXL_PARALLAX_DEPTH_CORRECTION && defined(DEFERRED) && !defined(DO_ALPHA_TEST) && !defined(DEPTH_WRITE_DECALS) && \
	!defined(SKIN) && !defined(HAIR) && !defined(EYE) && !defined(TREE_ANIM) && !defined(WORLD_MAP) && \
	((!defined(LANDSCAPE) && \
	  ((PIXL_PARALLAX_DEPTH_AUTO && defined(PIXL_AUTO_PARALLAX)) || defined(PARALLAX) || \
	   (PIXL_PARALLAX_DEPTH_MATERIAL_FORGE && defined(MATERIAL_FORGE)))) || \
	 (defined(LANDSCAPE) && defined(MATERIAL_LAYERS) && PIXL_PARALLAX_DEPTH_LANDSCAPE))
#	define PIXL_PARALLAX_DEPTH
#endif

#if defined(PIXL_PARALLAX_DEPTH) || defined(PIXL_GROUND_DEFORMATION_DEPTH)
#	define PIXL_VIRTUAL_DEPTH_OUTPUT
#endif

#if defined(DEFERRED)
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
	float4 Reflectance: SV_Target5;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	if defined(PIXL_VIRTUAL_DEPTH_OUTPUT)
	float Depth: SV_Depth;
#	endif
};
#else
struct PS_OUTPUT
{
	float4 Diffuse: SV_Target0;
	float4 MotionVectors: SV_Target1;
};
#endif

#ifdef PSHADER

SamplerState SampTerrainParallaxSampler : register(s1);

#	if defined(LANDSCAPE)

SamplerState SampColorSampler : register(s0);

#		define SampLandColor2Sampler SampColorSampler
#		define SampLandColor3Sampler SampColorSampler
#		define SampLandColor4Sampler SampColorSampler
#		define SampLandColor5Sampler SampColorSampler
#		define SampLandColor6Sampler SampColorSampler
#		define SampNormalSampler SampColorSampler
#		define SampLandNormal2Sampler SampColorSampler
#		define SampLandNormal3Sampler SampColorSampler
#		define SampLandNormal4Sampler SampColorSampler
#		define SampLandNormal5Sampler SampColorSampler
#		define SampLandNormal6Sampler SampColorSampler
#		define SampRMAOSSampler SampColorSampler
#		define SampLandRMAOS2Sampler SampColorSampler
#		define SampLandRMAOS3Sampler SampColorSampler
#		define SampLandRMAOS4Sampler SampColorSampler
#		define SampLandRMAOS5Sampler SampColorSampler
#		define SampLandRMAOS6Sampler SampColorSampler

#	else

SamplerState SampColorSampler : register(s0);

#		define SampNormalSampler SampColorSampler

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
SamplerState SampSpecularSampler : register(s2);
#		endif
#		if defined(FACEGEN)
SamplerState SampTintSampler : register(s3);
SamplerState SampDetailSampler : register(s4);
#		elif defined(PARALLAX)
SamplerState SampParallaxSampler : register(s3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
SamplerState SampProjDiffuseSampler : register(s3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
SamplerState SampEnvSampler : register(s4);
SamplerState SampEnvMaskSampler : register(s5);
#		endif

#		if defined(MATERIAL_FORGE) && !defined(FACEGEN)
SamplerState SampParallaxSampler : register(s4);
#		endif
#		if defined(MATERIAL_FORGE)
SamplerState SampRMAOSSampler : register(s5);
#		endif

SamplerState SampGlowSampler : register(s6);

#		if defined(MULTI_LAYER_PARALLAX)
SamplerState SampLayerSampler : register(s8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
SamplerState SampProjNormalSampler : register(s8);
#		endif

SamplerState SampBackLightSampler : register(s9);

#		if defined(PROJECTED_UV)
SamplerState SampProjDetailSampler : register(s10);
#		endif

SamplerState SampCharacterLightProjNoiseSampler : register(s11);
SamplerState SampRimSoftLightWorldMapOverlaySampler : register(s12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
SamplerState SampWorldMapOverlaySnowSampler : register(s13);
#		endif

#	endif

#	if defined(LOD_LAND_BLEND)
SamplerState SampLandLodBlend1Sampler : register(s13);
SamplerState SampLandLodBlend2Sampler : register(s15);
#	elif defined(LODLANDNOISE)
SamplerState SampLandLodNoiseSampler : register(s15);
#	endif

SamplerState SampShadowMaskSampler : register(s14);

#	if defined(LANDSCAPE)

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexLandColor2Sampler : register(t1);
Texture2D<float4> TexLandColor3Sampler : register(t2);
Texture2D<float4> TexLandColor4Sampler : register(t3);
Texture2D<float4> TexLandColor5Sampler : register(t4);
Texture2D<float4> TexLandColor6Sampler : register(t5);
Texture2D<float4> TexNormalSampler : register(t7);
Texture2D<float4> TexLandNormal2Sampler : register(t8);
Texture2D<float4> TexLandNormal3Sampler : register(t9);
Texture2D<float4> TexLandNormal4Sampler : register(t10);
Texture2D<float4> TexLandNormal5Sampler : register(t11);
Texture2D<float4> TexLandNormal6Sampler : register(t12);

Texture2D<float4> TexLandTHDisp0Sampler : register(t92);
Texture2D<float4> TexLandTHDisp1Sampler : register(t93);
Texture2D<float4> TexLandTHDisp2Sampler : register(t94);
Texture2D<float4> TexLandTHDisp3Sampler : register(t95);
Texture2D<float4> TexLandTHDisp4Sampler : register(t96);
Texture2D<float4> TexLandTHDisp5Sampler : register(t97);

#		if defined(MATERIAL_FORGE)

Texture2D<float4> TexLandDisplacement0Sampler : register(t80);
Texture2D<float4> TexLandDisplacement1Sampler : register(t81);
Texture2D<float4> TexLandDisplacement2Sampler : register(t82);
Texture2D<float4> TexLandDisplacement3Sampler : register(t83);
Texture2D<float4> TexLandDisplacement4Sampler : register(t84);
Texture2D<float4> TexLandDisplacement5Sampler : register(t85);

Texture2D<float4> TexRMAOSSampler : register(t86);
Texture2D<float4> TexLandRMAOS2Sampler : register(t87);
Texture2D<float4> TexLandRMAOS3Sampler : register(t88);
Texture2D<float4> TexLandRMAOS4Sampler : register(t89);
Texture2D<float4> TexLandRMAOS5Sampler : register(t90);
Texture2D<float4> TexLandRMAOS6Sampler : register(t91);

#		endif

#	else

Texture2D<float4> TexColorSampler : register(t0);
Texture2D<float4> TexNormalSampler : register(t1);  // normal in xyz, glossiness in w if not modelspacenormal

#		if defined(MODELSPACENORMALS) && !defined(LODLANDNOISE)
Texture2D<float4> TexSpecularSampler : register(t2);
#		endif
#		if defined(FACEGEN)
Texture2D<float4> TexTintSampler : register(t3);
Texture2D<float4> TexDetailSampler : register(t4);
#		elif defined(PARALLAX)
Texture2D<float4> TexParallaxSampler : register(t3);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
Texture2D<float4> TexProjDiffuseSampler : register(t3);
#		endif

#		if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(SNOW_FLAG) || defined(EYE)) && !defined(FACEGEN)
TextureCube<float4> TexEnvSampler : register(t4);
Texture2D<float4> TexEnvMaskSampler : register(t5);
#		endif

#		if defined(MATERIAL_FORGE) && !defined(FACEGEN)
Texture2D<float4> TexParallaxSampler : register(t4);
#		endif
#		if defined(MATERIAL_FORGE)
Texture2D<float4> TexRMAOSSampler : register(t5);
#		endif

Texture2D<float4> TexGlowSampler : register(t6);

#		if defined(MULTI_LAYER_PARALLAX)
Texture2D<float4> TexLayerSampler : register(t8);
#		elif defined(PROJECTED_UV) && !defined(SPARKLE)
Texture2D<float4> TexProjNormalSampler : register(t8);
#		endif

Texture2D<float4> TexBackLightSampler : register(t9);

#		if defined(PROJECTED_UV)
Texture2D<float4> TexProjDetail : register(t10);
#		endif

Texture2D<float4> TexCharacterLightProjNoiseSampler : register(t11);
Texture2D<float4> TexRimSoftLightWorldMapOverlaySampler : register(t12);

#		if defined(WORLD_MAP) && (defined(LODLANDSCAPE) || defined(LODLANDNOISE))
Texture2D<float4> TexWorldMapOverlaySnowSampler : register(t13);
#		endif

#	endif

#	if defined(LOD_LAND_BLEND)
Texture2D<float4> TexLandLodBlend1Sampler : register(t13);
Texture2D<float4> TexLandLodBlend2Sampler : register(t15);
#	elif defined(LODLANDNOISE)
Texture2D<float4> TexLandLodNoiseSampler : register(t15);
#	endif

Texture2D<float4> TexShadowMaskSampler : register(t14);

#	if defined(SKIN) && defined(PIXL_SKIN)
Texture2D<float4> TexSkinExtraSampler : register(t71);
Texture2D<float4> TexSkinWetnessSampler : register(t74);
Texture2D<float4> TexSkinWetnessNormalSampler : register(t75);
#	endif

cbuffer PerTechnique : register(b0)
{
	float4 FogColor : packoffset(c0);           // Color in xyz, invFrameBufferRange in w
	float4 ColourOutputClamp : packoffset(c1);  // fLightingOutputColourClampPostLit in x, fLightingOutputColourClampPostEnv in y, fLightingOutputColourClampPostSpec in z
	float4 VPOSOffset : packoffset(c2);         // ???
};

cbuffer PerMaterial : register(b1)
{
	float4 LODTexParams : packoffset(c0);  // TerrainTexOffset in xy, LodBlendingEnabled in z
#	if !(defined(LANDSCAPE) && defined(MATERIAL_FORGE))
	float4 TintColor : packoffset(c1);
	float4 EnvmapData : packoffset(c2);  // fEnvmapScale in x, 1 or 0 in y depending of if has envmask
	float4 ParallaxOccData : packoffset(c3);
	float4 SpecularColor : packoffset(c4);  // Shininess in w, color in xyz
	float4 SparkleParams : packoffset(c5);
	float4 MultiLayerParallaxData : packoffset(c6);  // Layer thickness in x, refraction scale in y, uv scale in zw
#	else
	float4 LandscapeTexture1GlintParameters : packoffset(c1);
	float4 LandscapeTexture2GlintParameters : packoffset(c2);
	float4 LandscapeTexture3GlintParameters : packoffset(c3);
	float4 LandscapeTexture4GlintParameters : packoffset(c4);
	float4 LandscapeTexture5GlintParameters : packoffset(c5);
	float4 LandscapeTexture6GlintParameters : packoffset(c6);
#	endif
	float4 LightingEffectParams : packoffset(c7);  // fSubSurfaceLightRolloff in x, fRimLightPower in y
	float4 IBLParams : packoffset(c8);

#	if !defined(MATERIAL_FORGE)
	float4 LandscapeTexture1to4IsSnow : packoffset(c9);
	float4 LandscapeTexture5to6IsSnow : packoffset(c10);  // bEnableSnowMask in z, inverse iLandscapeMultiNormalTilingFactor in w
	float4 LandscapeTexture1to4IsSpecPower : packoffset(c11);
	float4 LandscapeTexture5to6IsSpecPower : packoffset(c12);
	float4 SnowRimLightParameters : packoffset(c13);  // fSnowRimLightIntensity in x, fSnowGeometrySpecPower in y, fSnowNormalSpecPower in z, bEnableSnowRimLighting in w
#	endif

#	if defined(MATERIAL_FORGE) && defined(LANDSCAPE)
	float3 LandscapeTexture2PBRParams : packoffset(c9);
	float3 LandscapeTexture3PBRParams : packoffset(c10);
	float3 LandscapeTexture4PBRParams : packoffset(c11);
	float3 LandscapeTexture5PBRParams : packoffset(c12);
	float3 LandscapeTexture6PBRParams : packoffset(c13);
#	endif

	float4 CharacterLightParams : packoffset(c14);

	uint PBRFlags : packoffset(c15.x);
	float3 PBRParams1 : packoffset(c15.y);  // roughness scale, displacement scale, specular level
	float4 PBRParams2 : packoffset(c16);    // subsurface color, subsurface opacity

	float3 MaterialObjectRGBScale : packoffset(c17);  // RGB multipliers for material objects
};

cbuffer PerGeometry : register(b2)
{
	float3 DirLightDirection : packoffset(c0);
	float3 DirLightColor : packoffset(c1);
	float4 ShadowLightMaskSelect : packoffset(c2);
	float4 MaterialData : packoffset(c3);  // envmapLODFade in x, specularLODFade in y, alpha in z
	float AlphaTestRef : packoffset(c4);
	float3 EmitColor : packoffset(c4.y);
	float4 ProjectedUVParams : packoffset(c6);
	float4 SSRParams : packoffset(c7);
	float4 WorldMapOverlayParametersPS : packoffset(c8);
	float4 ProjectedUVParams2 : packoffset(c9);
	float4 ProjectedUVParams3 : packoffset(c10);  // fProjectedUVDiffuseNormalTilingScale in x, fProjectedUVNormalDetailTilingScale in y, EnableProjectedNormals in w
	row_major float3x4 DirectionalAmbient : packoffset(c11);
	float4 AmbientSpecularTintAndFresnelPower : packoffset(c14);  // Fresnel power in z, color in xyz
	float4 PointLightPosition[7] : packoffset(c15);               // point light radius in w
	float4 PointLightColor[7] : packoffset(c22);
	float2 NumLightNumShadowLight : packoffset(c29);
};

cbuffer AlphaTestRefBuffer : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

float GetSoftLightMultiplier(float angle)
{
	float softLightParam = saturate((LightingEffectParams.x + angle) / (1 + LightingEffectParams.x));
	float arg1 = (softLightParam * softLightParam) * (3 - 2 * softLightParam);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	float softLigtMul = saturate(arg1 - arg2);
	return softLigtMul;
}

float GetRimLightMultiplier(float3 L, float3 V, float3 N)
{
	float NdotV = saturate(dot(N, V));
	return exp2(LightingEffectParams.y * log2(1 - NdotV)) * saturate(dot(V, -L));
}

#	if !defined(MATERIAL_FORGE)
float ProcessSparkleColor(float color)
{
	return exp2(SparkleParams.y * log2(min(1, abs(color))));
}
#	endif

float3 TransformNormal(float3 normal)
{
	return normal * 2 + -1.0.xxx;
}

float GetLodLandBlendParameter(float3 color)
{
	float result = saturate(1.6666666 * (dot(color, 0.55.xxx) - 0.4));
	result = ((result * result) * (3 - result * 2));
#	if !defined(WORLD_MAP)
	result *= 0.8;
#	endif
	return result;
}

float GetLodLandBlendMultiplier(float parameter, float mask)
{
	return 0.8333333 * (parameter * (0.37 - mask) + mask) + 0.37;
}

float GetLandSnowMaskValue(float alpha)
{
#	if !defined(MATERIAL_FORGE)
	return alpha * LandscapeTexture5to6IsSnow.z + (1 + -LandscapeTexture5to6IsSnow.z);
#	else
	return 0;
#	endif
}

float3 GetLandNormal(float landSnowMask, float3 normal, float2 uv, SamplerState sampNormal, Texture2D<float4> texNormal)
{
	float3 landNormal = TransformNormal(normal);
#	if defined(SNOW) && !defined(MATERIAL_FORGE)
	if (landSnowMask > 1e-5 && LandscapeTexture5to6IsSnow.w != 1.0) {
		float3 snowNormal =
			float3(-1, -1, 1) *
			TransformNormal(texNormal.Sample(sampNormal, LandscapeTexture5to6IsSnow.ww * uv).xyz);
		landNormal.z += 1;
		float normalProjection = dot(landNormal, snowNormal);
		snowNormal = landNormal * normalProjection.xxx - snowNormal * landNormal.z;
		return normalize(snowNormal);
	} else {
		return landNormal;
	}
#	else
	return landNormal;
#	endif
}

#	if defined(SNOW) && !defined(MATERIAL_FORGE)
float3 GetSnowSpecularColor(PS_INPUT input, float3 worldNormal, float3 viewDirection)
{
	if (SnowRimLightParameters.w > 1e-5) {
#		if defined(MODELSPACENORMALS) && !defined(SKINNED)
		float3 modelGeometryNormal = float3(0, 0, 1);
#		else
		float3 modelGeometryNormal = normalize(float3(input.TBN0.z, input.TBN1.z, input.TBN2.z));
#		endif
		float normalFactor = 1 - saturate(dot(worldNormal, viewDirection));
		float geometryNormalFactor = 1 - saturate(dot(modelGeometryNormal, viewDirection));
		return (SnowRimLightParameters.x * (exp2(SnowRimLightParameters.y * log2(geometryNormalFactor)) * exp2(SnowRimLightParameters.z * log2(normalFactor)))).xxx;
	} else {
		return 0.0.xxx;
	}
}
#	endif

#	if defined(FACEGEN)
float3 GetFacegenBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 detailColor = TexDetailSampler.Sample(SampDetailSampler, uv).xyz;
	detailColor = float3(3.984375, 3.984375, 3.984375) * (float3(0.00392156886, 0, 0.00392156886) + detailColor);
	float3 tintColor = TexTintSampler.Sample(SampTintSampler, uv).xyz;
	tintColor = tintColor * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return (rawBaseColor * rawBaseColor + tintColor) * detailColor;
}
#	endif

#	if defined(FACEGEN_RGB_TINT)
float3 GetFacegenRGBTintBaseColor(float3 rawBaseColor, float2 uv)
{
	float3 tintColor = TintColor.xyz * rawBaseColor * 2.0.xxx;
	tintColor = tintColor - tintColor * rawBaseColor;
	return float3(1.01171875, 0.99609375, 1.01171875) * (rawBaseColor * rawBaseColor + tintColor);
}
#	endif

#	if defined(WORLD_MAP)
float3 GetWorldMapNormal(PS_INPUT input, float3 rawNormal, float3 baseColor)
{
	float3 normal = normalize(rawNormal);
#		if defined(MODELSPACENORMALS)
	float3 worldMapNormalSrc = normal.xyz;
#		else
	float3 worldMapNormalSrc = float3(input.TBN0.z, input.TBN1.z, input.TBN2.z);
#		endif
	float3 worldMapNormal = 7.0.xxx * (-0.2.xxx + abs(normalize(worldMapNormalSrc)));
	worldMapNormal = max(0.01.xxx, worldMapNormal * worldMapNormal * worldMapNormal);
	worldMapNormal /= dot(worldMapNormal, 1.0.xxx);
	float3 worldMapColor1 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.yz).xyz;
	float3 worldMapColor2 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xz).xyz;
	float3 worldMapColor3 = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, WorldMapOverlayParametersPS.xx * input.InputPosition.xy).xyz;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor1 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.yz).xyz;
	float3 worldMapSnowColor2 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xz).xyz;
	float3 worldMapSnowColor3 = TexWorldMapOverlaySnowSampler.Sample(SampWorldMapOverlaySnowSampler, WorldMapOverlayParametersPS.ww * input.InputPosition.xy).xyz;
#		endif
	float3 worldMapColor = worldMapNormal.xxx * worldMapColor1 + worldMapNormal.yyy * worldMapColor2 + worldMapNormal.zzz * worldMapColor3;
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float3 worldMapSnowColor = worldMapSnowColor1 * worldMapNormal.xxx + worldMapSnowColor2 * worldMapNormal.yyy + worldMapSnowColor3 * worldMapNormal.zzz;
	float snowMultiplier = GetLodLandBlendParameter(baseColor);
	worldMapColor = snowMultiplier * (worldMapSnowColor - worldMapColor) + worldMapColor;
#		endif
	worldMapColor = normalize(2.0.xxx * (-0.5.xxx + (worldMapColor)));
#		if defined(LODLANDNOISE) || defined(LODLANDSCAPE)
	float worldMapLandTmp = saturate(19.9999962 * (rawNormal.z - 0.95));
	worldMapLandTmp = saturate(-(worldMapLandTmp * worldMapLandTmp) * (worldMapLandTmp * -2 + 3) + 1.5);
	float3 worldMapLandTmp1 = normalize(normal.zxy * float3(1, 0, 0) - normal.yzx * float3(0, 0, 1));
	float3 worldMapLandTmp2 = normalize(worldMapLandTmp1.yzx * normal.zxy - worldMapLandTmp1.zxy * normal.yzx);
	float3 worldMapLandTmp3 = normalize(worldMapColor.xxx * worldMapLandTmp1 + worldMapColor.yyy * worldMapLandTmp2 + worldMapColor.zzz * normal.xyz);
	float worldMapLandTmp4 = dot(worldMapLandTmp3, worldMapLandTmp3);
	if (worldMapLandTmp4 > 0.999 && worldMapLandTmp4 < 1.001) {
		normal.xyz = worldMapLandTmp * (worldMapLandTmp3 - normal.xyz) + normal.xyz;
	}
#		else
	normal.xyz = normalize(
		WorldMapOverlayParametersPS.zzz * (rawNormal.xyz - worldMapColor.xyz) + worldMapColor.xyz);
#		endif
	return normal;
}

float3 GetWorldMapBaseColor(float3 originalBaseColor, float3 rawBaseColor, float texProjTmp)
{
#		if defined(LODOBJECTS) && !defined(PROJECTED_UV)
	return rawBaseColor;
#		endif
#		if defined(LODLANDSCAPE) || defined(LODOBJECTSHD) || defined(LODLANDNOISE)
	float lodMultiplier = GetLodLandBlendParameter(originalBaseColor.xyz);
#		elif defined(LODOBJECTS) && defined(PROJECTED_UV)
	float lodMultiplier = saturate(10 * texProjTmp);
#		else
	float lodMultiplier = 1;
#		endif
#		if defined(LODOBJECTS)
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.269999981, 0.281000018, 0.441000015, 0.441000015) + float4(0.0780000091, 0.09799999, -0.0349999964, 0.465000004);
	float4 lodColor = lodColorMul.xyzw * 2.0.xxxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	bool useLodColorZ = lodColorMul.w > 0.5;
	lodColor.xyz = max(lodColor.xyz, rawBaseColor.xyz);
	lodColor.w = useLodColorZ ? lodColor.z : min(lodColor.w, rawBaseColor.z);
	return (0.5 * lodMultiplier).xxx * (lodColor.xyw - rawBaseColor.xyz) + rawBaseColor;
#		else
	float4 lodColorMul = lodMultiplier.xxxx * float4(0.199999988, 0.441000015, 0.269999981, 0.281000018) + float4(0.300000012, 0.465000004, 0.0780000091, 0.09799999);
	float3 lodColor = lodColorMul.zwy * 2.0.xxx;
	lodColor.xyz = Color::Diffuse(lodColor.xyz);
	lodColor.xy = max(lodColor.xy, rawBaseColor.xy);
	lodColor.z = lodColorMul.y > 0.5 ? max((lodMultiplier * 0.441 + -0.0349999964) * 2, rawBaseColor.z) : min(lodColor.z, rawBaseColor.z);
	return lodColorMul.xxx * (lodColor - rawBaseColor.xyz) + rawBaseColor;
#		endif
}
#	endif

float GetSnowParameterY(float texProjTmp, float alpha)
{
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::BaseObjectIsSnow) {
		return min(1, texProjTmp + alpha);
	}
	return texProjTmp;
}

#	if defined(LOD)
#		undef MATERIAL_LAYERS
#		undef WATER_BLENDING
#		undef RADIANT_GRID
#		undef RAIN_RESPONSE
#		undef WORLD_PROBES
#		undef WATER_OPTICS
#	endif

#	if defined(WORLD_MAP)
#		undef SKY_VEIL
#		undef SKY_BOUNCE
#	endif

#	if defined(SKY_BOUNCE)
#		if defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(BACK_LIGHTING)
#			define SKY_BOUNCE_SHADOW_VIS
#		endif
#	endif

#	include "Common/LightingCommon.hlsli"

#	if defined(MATERIAL_LAYERS)
#		include "MaterialLayers/MaterialLayersTuning.hlsli"
#		include "MaterialLayers/MaterialDetail.hlsli"
#	endif

#	if defined(FOLIAGE_DYNAMICS)
#		include "FoliageDynamics/FoliageDynamics.hlsli"
#	endif

#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
#		include "GroundResponse/Runtime.hlsli"
#		include "GroundResponse/DeformableGround.hlsli"
#	endif

// DialogueFocus owns PS b13 only on SKIN/EYE/HAIR permutations.
// LANDSCAPE is compile-time excluded inside the include so GroundResponse's b13 ABI is untouched.
#	include "DialogueFocus/DialogueFocus.hlsli"

#	if defined(WATER_OPTICS)
#		include "WaterOptics/WaterCaustics.hlsli"
#	endif

#	if defined(EYE)
#		undef RAIN_RESPONSE
#		undef SOFT_LIGHTING
#		undef BACK_LIGHTING
#		undef RIM_LIGHTING
#	endif

#	if defined(MATERIAL_LAYERS) && !defined(LOD) && (defined(PARALLAX) || defined(LANDSCAPE) || defined(ENVMAP) || defined(MATERIAL_FORGE))
#		define EMAT
#	endif

// PIXL Vanilla Auto-POM
// Eligibility is declared before PS_OUTPUT so depth-correct variants can expose SV_Depth only
// where it is actually needed. Existing authored displacement paths still take priority.

#	if defined(EMAT) && (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		define EMAT_ENVMAP
#	endif

#	if defined(WORLD_PROBES)
#		include "WorldProbes/WorldProbes.hlsli"
#	endif

#	if defined(MATERIAL_FORGE)
#		include "Common/PBR.hlsli"
#	endif

#	if defined(EMAT) || defined(PIXL_AUTO_PARALLAX)
#		include "MaterialLayers/MaterialLayers.hlsli"
#	endif

#	if defined(CONTACT_SHADOWS)
#		include "ContactShadows/ContactShadows.hlsli"
#	endif

#	if defined(RADIANT_GRID)
#		include "RadiantGrid/RadiantGrid.hlsli"
#	endif

#	if defined(NATURAL_LIGHTING) && defined(RADIANT_GRID)
#		include "NaturalLighting/NaturalLighting.hlsli"
#	endif

#	if defined(TREE_ANIM)
#		undef RAIN_RESPONSE
#	endif

#	if defined(RAIN_RESPONSE)
#		include "RainResponse/RainResponse.hlsli"
#	endif

#	if defined(TERRAIN_SEAM)
#		include "TerrainSeam/TerrainSeam.hlsli"
#	endif

#	if defined(TISSUE_DIFFUSION) && defined(SKIN) && defined(DEFERRED)
#		undef SOFT_LIGHTING
#	endif

#	if defined(SKY_BOUNCE)
#		include "SkyBounce/SkyBounce.hlsli"
#	endif

#	if defined(HAIR) && defined(STRAND_SHADING)
#		include "Hair/Hair.hlsli"
#	endif

#	if defined(LANDSCAPE)
#		include "Common/LightingLandscape.hlsli"
#	endif

#	if defined(TERRAIN_DETAIL) && (defined(LANDSCAPE) || defined(LOD_LAND_BLEND) || (defined(DISTANCE_BLEND) && defined(LODLANDSCAPE)))
#		include "TerrainDetail/TerrainDetail.hlsli"
#	endif

#	if defined(THIN_SURFACE) && !(defined(LOD) || defined(SKIN) || defined(HAIR) || defined(EYE) || defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS) || defined(DEPTH_WRITE_DECALS))
#		include "ThinSurface/ThinSurface.hlsli"
#		define ANISOTROPIC_ALPHA
#	endif

#	if defined(PIXL_SKIN)
#		include "SkinOptics/SkinOptics.hlsli"
#	endif

// WindowLife is intentionally limited to static/object Lighting permutations.
// Private PS structured SRV carries per-draw classification and optics; FeatureData b6 is untouched.
#	if defined(PIXL_WINDOW_LIFE) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE) && !defined(LOD) && \
	!defined(SKINNED) && !defined(SKIN) && !defined(HAIR) && !defined(EYE) && !defined(TREE_ANIM) && \
	!defined(SPARKLE) && !defined(WORLD_MAP)
#		define PIXL_WINDOW_LIFE_ACTIVE
#		include "WindowLife/WindowLife.hlsli"
#	endif

#	define LinearSampler SampColorSampler

#	include "Common/ShadowSampling.hlsli"

#	if defined(AMBIENT_PROBE)
#		include "AmbientProbe/AmbientProbe.hlsli"
#	endif

#	if defined(ATMOSPHERE_PIPELINE)
#		include "Atmosphere/Atmosphere.hlsli"
#	endif

#	include "Common/LightingEval.hlsli"

#	if defined(PIXL_PARALLAX_DEPTH)
// Convert a POM UV intersection into a bounded world-space point on the camera ray.
// The UV->world Jacobian supplies the metric ordinary POM lacks. Grazing views are
// explicitly suppressed because tiny UV errors become large world-space depth errors.
float3 PixlResolveParallaxDepthPosition(
	float3 baseWorldPosition,
	float3 viewDirection,
	float2 baseUV,
	float2 displacedUV,
	float3 dpdx,
	float3 dpdy,
	float2 duvdx,
	float2 duvdy,
	out float confidence)
{
	confidence = 0.0f;

	float2 deltaUV = displacedUV - baseUV;
	if (dot(deltaUV, deltaUV) <= 1e-12f)
		return baseWorldPosition;

	float determinant =
		duvdx.x * duvdy.y - duvdx.y * duvdy.x;
	if (abs(determinant) <= 1e-8f)
		return baseWorldPosition;

	float invDet = rcp(determinant);
	float3 dPdu =
		(dpdx * duvdy.y - dpdy * duvdx.y) * invDet;
	float3 dPdv =
		(dpdy * duvdx.x - dpdx * duvdy.x) * invDet;
	float3 tangentShift =
		dPdu * deltaUV.x + dPdv * deltaUV.y;

	float3 planeNormal = cross(dPdu, dPdv);
	float planeLenSq = dot(planeNormal, planeNormal);
	if (planeLenSq <= 1e-10f)
		return baseWorldPosition;
	planeNormal *= rsqrt(planeLenSq);

	// viewDirection points surface -> camera; rayIntoSurface is camera -> surface.
	float3 rayIntoSurface = -viewDirection;
	float normalIncidence =
		abs(dot(rayIntoSurface, planeNormal));

	// Protect the horizon/grazing case. The previous implementation used tangent
	// magnitude for this fade, which did the opposite and enabled depth most
	// aggressively exactly where the reconstruction is least stable.
	float grazingFade =
		smoothstep(
			PIXL_PARALLAX_DEPTH_MIN_GRAZING,
			PIXL_PARALLAX_DEPTH_MIN_GRAZING + 0.14f,
			normalIncidence);
	if (grazingFade <= 1e-4f)
		return baseWorldPosition;

	float3 rayTangent =
		rayIntoSurface -
		planeNormal * dot(rayIntoSurface, planeNormal);
	float rayTangentLenSq =
		dot(rayTangent, rayTangent);
	if (rayTangentLenSq <= 1e-8f)
		return baseWorldPosition;

	float rayTravel =
		dot(tangentShift, rayTangent) *
		rcp(rayTangentLenSq);
	float uvWorldScale =
		max(length(dPdu), length(dPdv));

#if defined(LANDSCAPE)
	float maximumTravel =
		min(
			MaterialLayersTuning::TerrainVirtualDepthMaxWorld(),
			max(
				uvWorldScale *
					MaterialLayersTuning::TerrainVirtualDepthMaxUV(),
				1e-3f));
	rayTravel = clamp(
		rayTravel *
			MaterialLayersTuning::TerrainVirtualDepthStrength(),
		-maximumTravel,
		maximumTravel);
	float protrusionBudget =
		maximumTravel *
		MaterialLayersTuning::TerrainVirtualDepthProtrusion();
	rayTravel =
		max(rayTravel, -protrusionBudget);
#else
	float maximumTravel =
		min(
			PIXL_PARALLAX_DEPTH_MAX_WORLD,
			max(
				uvWorldScale * PIXL_PARALLAX_DEPTH_MAX_UV,
				1e-3f));
	rayTravel = clamp(
		rayTravel * PIXL_PARALLAX_DEPTH_SCALE,
		-maximumTravel,
		maximumTravel);
#	if !PIXL_PARALLAX_DEPTH_ALLOW_PROTRUSION
	rayTravel = max(rayTravel, 0.0f);
#	endif
#endif

	rayTravel *= grazingFade;
	confidence =
		grazingFade *
		saturate(abs(rayTravel) /
			max(maximumTravel, 1e-3f));

	return
		baseWorldPosition +
		rayIntoSurface * rayTravel;
}
#	endif

#	if defined(PIXL_VIRTUAL_DEPTH_OUTPUT)
float PixlProjectVirtualDepth(
	float3 worldPosition,
	float fallbackDepth)
{
	float4 clipPosition =
		mul(
			FrameBuffer::CameraViewProj,
			float4(worldPosition, 1.0f));

	if (abs(clipPosition.w) <= 1e-6f)
		return fallbackDepth;

	float projectedDepth =
		clipPosition.z * rcp(clipPosition.w);

	// Reject impossible projections rather than letting saturate turn them into a
	// full-screen near/far plane. Valid D3D11 hardware depth is [0,1].
	if (projectedDepth < 0.0f ||
		projectedDepth > 1.0f)
		return fallbackDepth;

	return projectedDepth;
}
#	endif

#	if defined(PIXL_GROUND_DEFORMATION_DEPTH)
// Convert a vertical snow/mud compression into a depth value at the *same raster
// pixel*. The virtual point is moved along the camera ray until that ray reaches
// the lowered world-Z plane. This avoids the off-ray projection that produced
// large triangular depth wedges at oblique camera angles.
float PixlResolveGroundDeformationDepth(
	float3 baseWorldPosition,
	float compressionDepth,
	float deformationAmount,
	float fallbackDepth)
{
	float safeCompression =
		clamp(compressionDepth, 0.0f, 40.0f);
	if (safeCompression <= 1e-4f ||
		deformationAmount <= 1e-4f)
		return fallbackDepth;

	float distanceToCamera =
		length(baseWorldPosition);
	if (distanceToCamera <= 1e-3f)
		return fallbackDepth;

	float3 rayIntoSurface =
		baseWorldPosition / distanceToCamera;

	// Landscape receiving the camera ray from above has a negative ray Z.
	// Near-horizontal rays require enormous travel for a tiny vertical drop, so
	// smoothly remove hardware-depth recession there while retaining normal and
	// material relief.
	float downwardIncidence =
		saturate(-rayIntoSurface.z);
	float incidenceFade =
		smoothstep(0.16f, 0.38f, downwardIncidence);
	float distanceFade =
		1.0f -
		smoothstep(768.0f, 1536.0f, distanceToCamera);
	float amountFade =
		smoothstep(0.025f, 0.22f, deformationAmount);

	float visibleCompression =
		safeCompression *
		incidenceFade *
		distanceFade *
		amountFade;
	if (visibleCompression <= 1e-4f)
		return fallbackDepth;

	float rayTravel =
		visibleCompression /
		max(downwardIncidence, 0.20f);

	// Extra protection against pathological views/data. Real snow/mud depth is
	// small; there is no visual benefit in moving the virtual hit tens of metres.
	rayTravel =
		min(
			rayTravel,
			min(64.0f, visibleCompression * 4.0f));

	float3 virtualWorldPosition =
		baseWorldPosition +
		rayIntoSurface * rayTravel;
	float resolvedDepth =
		PixlProjectVirtualDepth(
			virtualWorldPosition,
			fallbackDepth);

	// rayTravel is strictly positive, so virtualWorldPosition is farther along
	// the same camera ray regardless of the projection's depth convention.
	return resolvedDepth;
}
#	endif

#if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
// PIXL_GR_13Z_EMBEDDED_MICROSURFACE_V1
//
// Stable absolute-world procedural detail acts like an embedded detail texture
// without consuming another terrain SRV slot.
//
// Snow: packed crystalline grain.
// Mud : clump breakup.
// Wet deep mud: broad basin field for thin water accumulation.
uint PixlGroundMicroHash(int2 p)
{
    uint x = asuint(p.x);
    uint y = asuint(p.y);
    uint h =
        x * 0x8da6b343u ^
        y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}

float PixlGroundMicroHash01(int2 p)
{
    return
        float(
            PixlGroundMicroHash(p) &
            0x00FFFFFFu) *
        (1.0f / 16777215.0f);
}

float PixlGroundMicroNoise(
    float2 absoluteXY,
    float worldScale)
{
    float scale =
        max(worldScale, 0.75f);
    float2 grid =
        absoluteXY / scale;
    int2 cell =
        int2(floor(grid));
    float2 f =
        frac(grid);

    f =
        f * f * f *
        (f * (f * 6.0f - 15.0f) + 10.0f);

    float n00 =
        PixlGroundMicroHash01(cell);
    float n10 =
        PixlGroundMicroHash01(
            cell + int2(1, 0));
    float n01 =
        PixlGroundMicroHash01(
            cell + int2(0, 1));
    float n11 =
        PixlGroundMicroHash01(
            cell + int2(1, 1));

    return
        lerp(
            lerp(n00, n10, f.x),
            lerp(n01, n11, f.x),
            f.y);
}

float PixlGroundSnowMicro(float2 absoluteXY)
{
    float coarse =
        PixlGroundMicroNoise(
            absoluteXY,
            7.5f);
    float grain =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(31.0f, -47.0f),
            2.35f);

    return
        saturate(
            coarse * 0.58f +
            grain * 0.42f);
}

float PixlGroundMudMicro(float2 absoluteXY)
{
    float clump =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(17.0f, 23.0f),
            10.5f);
    float breakup =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(-39.0f, 61.0f),
            4.8f);

    return
        saturate(
            clump * 0.68f +
            breakup * 0.32f);
}

float PixlGroundMudBasin(float2 absoluteXY)
{
    float broad =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(211.0f, -97.0f),
            46.0f);
    float breakup =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(-131.0f, 173.0f),
            22.0f);

    return
        saturate(
            broad * 0.72f +
            breakup * 0.28f);
}

float2 PixlGroundMicroVector(float2 absoluteXY)
{
    float x =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(7.0f, 29.0f),
            3.2f) *
            2.0f -
        1.0f;
    float y =
        PixlGroundMicroNoise(
            absoluteXY +
                float2(-41.0f, 13.0f),
            3.2f) *
            2.0f -
        1.0f;

    return float2(x, y);
}
#endif


PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout;
#	if defined(PIXL_VIRTUAL_DEPTH_OUTPUT)
	psout.Depth = input.Position.z;
	float pixlVirtualDepth = input.Position.z;
#	endif

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float3 viewDirection = -normalize(input.WorldPosition.xyz);

	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.Position.xy, SharedData::FrameCount);

	// Per-geometry DialogueFocus is zero for every non-focused/non-character draw.
	// The spatial mask deliberately concentrates the quality budget on face/upper body.
	const float pixlDialogueFocus = DialogueFocus::GetFocus(input.WorldPosition.xyz);

#	if defined(DEFERRED)
	const bool inWorld = true;
#	else
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
#	endif
	const bool inReflection = Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection;

	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, viewPosition.z);

#	if defined(SKINNED) || !defined(MODELSPACENORMALS)
	float3x3 tbn = float3x3(input.TBN0.xyz, input.TBN1.xyz, input.TBN2.xyz);

	// PIXL_GR_13BB_FRONTFACE_TBN_PROOF_V1
	//
	// 13AP makes the physical TOP of the GroundResponse shell SV_IsFrontFace.
	// TerrainSurface 13X still negates the interpolated landscape TBN before it
	// reaches this shader. The normal Lighting back-face correction therefore
	// repairs the underside, but never repairs the now-front-facing physical top.
	//
	// For this diagnostic only, undo 13X on the physical top before normal-map
	// and lighting evaluation. Geometry, winding, raster depth and opacity are
	// completely untouched.
#		if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	if (GroundResponseRuntime::IsGeometryPass() && frontFace) {
		tbn = -tbn;
	}
#		endif

#		if !defined(LOD)
	// Existing Skyrim/PIXL double-sided correction remains unchanged.
	if (!frontFace) {
#			if defined(TREE_ANIM)
		tbn = -tbn;
#			else
		tbn = lerp(tbn, -tbn, nearFactor);
#			endif
	}
#		endif

	float3x3 tbnTr = transpose(tbn);

	tbnTr[0] = normalize(tbnTr[0]);
	tbnTr[1] = normalize(tbnTr[1]);
	tbnTr[2] = normalize(tbnTr[2]);

	tbn = transpose(tbnTr);

#	endif  // defined (SKINNED) || !defined (MODELSPACENORMALS)

#	if !defined(MATERIAL_FORGE)
#		if defined(LANDSCAPE)
	float shininess = dot(input.LandBlendWeights1, LandscapeTexture1to4IsSpecPower) + input.LandBlendWeights2.x * LandscapeTexture5to6IsSpecPower.x + input.LandBlendWeights2.y * LandscapeTexture5to6IsSpecPower.y;
#		elif defined(SPECULAR)
	float shininess = SpecularColor.w;
#		else
	float shininess = 0.0;
#		endif  // defined (LANDSCAPE)
#	endif

#	if defined(TERRAIN_SEAM)
	float blendFactorTerrain = 0.0;
	[flatten] if (SharedData::terrainSeamSettings.Enabled)
	{
		float depthSampled = TerrainSeam::TerrainSeamMaskTexture[input.Position.xy].x;

		float depthSampledLinear = SharedData::GetScreenDepth(depthSampled);
		float depthPixelLinear = SharedData::GetScreenDepth(input.Position.z);

		blendFactorTerrain = saturate((depthSampledLinear - depthPixelLinear) / 10.0);

		if (input.Position.z == depthSampled)
			blendFactorTerrain = 1;
	}
#	endif

	float2 uv = input.TexCoord0.xy;
	float2 uvOriginal = uv;

#	if defined(PIXL_PARALLAX_DEPTH)
	// Derivatives must be evaluated in uniform control flow, before the material-specific POM branches.
	float3 pixlParallaxDPdx = ddx(input.WorldPosition.xyz);
	float3 pixlParallaxDPdy = ddy(input.WorldPosition.xyz);
	float2 pixlParallaxDUVdx = ddx(uvOriginal);
	float2 pixlParallaxDUVdy = ddy(uvOriginal);
	float3 pixlParallaxWorldPosition = input.WorldPosition.xyz;
	float pixlParallaxDepth = input.Position.z;
	float pixlParallaxDepthConfidence = 0.0f;
#	endif

#	if defined(EMAT)
	float parallaxShadowQuality = viewPosition.z < MaterialLayers::ParallaxCheapDistance ? MaterialLayers::ParallaxNearShadowQuality : MaterialLayers::ParallaxFarShadowQuality;
	float terrainDirectionalShadowQuality = parallaxShadowQuality;
#		define COMPUTE_TERRAIN_SHADOW_BASE(OUT_SH0) MaterialLayers::ComputeTerrainParallaxShadowBaseHeight(input, uv, terrainShadowMipLevels, terrainDirectionalShadowQuality, screenNoise, displacementParams, sharedOffset, OUT_SH0)
#		define EVAL_TERRAIN_DIR_SHADOW(BASE_SH0, DIR_TS) MaterialLayers::EvaluateTerrainDirectionalParallaxShadowMultiplier(input, uv, terrainShadowMipLevels, DIR_TS, terrainDirectionalShadowQuality, screenNoise, displacementParams, sharedOffset, BASE_SH0)
#		if defined(LANDSCAPE)
#			define LANDSCAPE_PARALLAX_ENABLED (MaterialLayers::TerrainParallaxEnabled())
#		endif
#	endif

#	if defined(LANDSCAPE)
#		if defined(EMAT)
	float mipLevels[6];
	float terrainShadowMipLevels[6];
#			if defined(TERRAIN_DETAIL)
	StochasticOffsets sharedOffset = ComputeStochasticOffsets(input.TexCoord0.zw);
#			else
	StochasticOffsets sharedOffset = (StochasticOffsets)0;
#			endif
	float cachedDirectionalTerrainParallaxShadow = 1.0;
	bool hasCachedDirectionalTerrainParallaxShadow = false;
	bool hasCachedTerrainShadowBaseHeight = false;
	bool hasTerrainParallaxShadow = false;
#		endif
#	else
	float mipLevel = 0;
#	endif  // LANDSCAPE
	float sh0 = 0;
	float pixelOffset = 0;

#	if defined(PIXL_AUTO_PARALLAX)
	float autoParallaxMipLevel = 0.0;
	float autoParallaxBaseHeight = 0.5;
	float autoParallaxStrength = 0.0;
	bool autoParallaxApplied = false;
#	endif

#	if defined(EMAT)
#		if defined(LANDSCAPE)
	DisplacementParams displacementParams[6];
	displacementParams[0].DisplacementScale = 1.f;
	displacementParams[0].DisplacementOffset = 0.f;
	displacementParams[0].HeightScale = 1;
	displacementParams[0].FlattenAmount = 0;
#		else
	DisplacementParams displacementParams;
	displacementParams.DisplacementScale = 1.f;
	displacementParams.DisplacementOffset = 0.f;
	displacementParams.HeightScale = 1;
	displacementParams.FlattenAmount = 0;
#		endif

#	endif

	float curvature = 0;
	float normalSmoothness = 0;

#	if !defined(MODELSPACENORMALS)
	float3 vertexNormal = tbnTr[2];
#		if defined(EMAT)

	if (SharedData::materialLayerSettings.EnableParallaxWarpingFix
#			if defined(LANDSCAPE)
		&& LANDSCAPE_PARALLAX_ENABLED
#			endif
	) {
		float3 ndx = ddx(vertexNormal);
		float3 ndy = ddy(vertexNormal);
		float3 fdx = ddx(input.WorldPosition.xyz);
		float3 fdy = ddy(input.WorldPosition.xyz);
		float fragSize = rcp(length(max(abs(fdx), abs(fdy))));
		float normalDelta = length(max(abs(ndx), abs(ndy))) * fragSize;
		float3 flatWorldNormal = normalize(-cross(fdx, fdy));
		normalSmoothness = saturate(1.0 - dot(vertexNormal, flatWorldNormal));
#			if defined(LANDSCAPE)
		curvature = sqrt(normalDelta);
		displacementParams[0].HeightScale = saturate(1.0 - curvature);
		displacementParams[0].FlattenAmount = (normalSmoothness + curvature);
#			else
		// Objects: do not crush HeightScale (that flattens convex faces you are looking at).
		// Only add FlattenAmount past the facing hemisphere (grazing > facing) and only if bent.
		// Flat walls stay at bend ≈ 0. Flatten tracks UV stretch (÷ facing).
		curvature = normalDelta;
		float facing = saturate(dot(vertexNormal, viewDirection));
		float grazing = 1.0 - facing;
		float bend = saturate(normalSmoothness + curvature);
		float silhouette = saturate(grazing - facing);
		// Cap FlattenAmount: ÷facing tracks UV stretch but can explode at grazing (~14× at the 0.0625 floor).
		displacementParams.FlattenAmount = saturate(silhouette * bend * rcp(max(facing, 0.0625)));
#			endif
	}
#		endif
#	endif

	float3 entryNormal = 0;
	float3 entryNormalTS = 0;
	float eta = 1;
	float3 refractedViewDirection = viewDirection;
	float4 sampledCoatColor = PBRParams2;
	float3 complexSpecular = 1.0;  // Declare complexSpecular at a higher scope so it's available throughout the shader (NEEDED FOR STOCH. FIX)

#	if defined(EMAT)
#		if defined(PARALLAX) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
	if (SharedData::materialLayerSettings.EnableParallax) {
		mipLevel = MaterialLayers::GetMipLevel(uv, TexParallaxSampler);
		uv = MaterialLayers::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::materialLayerSettings.EnableShadows && parallaxShadowQuality > 0.0)
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#		endif  // defined(PARALLAX) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

	bool complexMaterial = false;
	bool complexMaterialParallax = false;
	float4 complexMaterialColor = 1.0;

#		if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float4 envMaskSample = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv);
	float envMaskBase = envMaskSample.x;
	if (SharedData::materialLayerSettings.EnableComplexMaterial) {
		const float kMaskEpsilon = (4.0 / 255.0);

		const float4 mipSample = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, 15);
		complexMaterial = mipSample.w < (1.0 - kMaskEpsilon);

		const bool grayscaleMask = (abs(mipSample.x - mipSample.y) < kMaskEpsilon) &&
		                           (abs(mipSample.x - mipSample.z) < kMaskEpsilon) &&
		                           (abs(mipSample.y - mipSample.z) < kMaskEpsilon);
		// Preserve height-only masks while rejecting grayscale environment masks
		const bool solidBlackHeightMask = all(mipSample.xyz < kMaskEpsilon) &&
		                                  mipSample.w > kMaskEpsilon &&
		                                  mipSample.w < (1.0 - kMaskEpsilon);
		if (grayscaleMask && !solidBlackHeightMask)
			complexMaterial = false;

		if (complexMaterial) {
			if (envMaskSample.w > kMaskEpsilon && envMaskSample.w < (1.0 - kMaskEpsilon)) {
				complexMaterialParallax = true;
				mipLevel = MaterialLayers::GetMipLevel(uv, TexEnvMaskSampler);
				uv = MaterialLayers::GetParallaxCoords(viewPosition.z, uv, mipLevel, viewDirection, tbnTr, screenNoise, TexEnvMaskSampler, SampTerrainParallaxSampler, 3, displacementParams, pixelOffset);
				if (SharedData::materialLayerSettings.EnableShadows && parallaxShadowQuality > 0.0)
					sh0 = TexEnvMaskSampler.SampleLevel(SampEnvMaskSampler, uv, mipLevel).w;
				complexMaterialColor = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv);
			} else {
				complexMaterialColor = envMaskSample;
			}
			envMaskBase = complexMaterialColor.x;
		}
	}
#		endif  // ENVMAP

#		if defined(MATERIAL_FORGE) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool PBRParallax = false;
	[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
	{
		float4 sampledCoatProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
		sampledCoatColor.rgb *= Color::Diffuse(sampledCoatProperties.rgb);
		sampledCoatColor.a *= sampledCoatProperties.a;
	}
#			if !defined(FACEGEN)
	[branch] if (SharedData::materialLayerSettings.EnableParallax && (PBRFlags & PBR::Flags::HasDisplacement) != 0)
	{
		PBRParallax = true;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			displacementParams.HeightScale *= PBRParams1.y;
			displacementParams.DisplacementScale = 0.5;
			displacementParams.DisplacementOffset = -0.25;

			eta = lerp(1.0, (1 - sqrt(MultiLayerParallaxData.y)) / (1 + sqrt(MultiLayerParallaxData.y)), sampledCoatColor.w);
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				entryNormalTS = normalize(TransformNormal(TexBackLightSampler.Sample(SampBackLightSampler, uvOriginal).xyz));
			}
			else
			{
				entryNormalTS = normalize(TransformNormal(TexNormalSampler.Sample(SampNormalSampler, uvOriginal).xyz));
			}
			entryNormal = normalize(mul(tbn, entryNormalTS));
			refractedViewDirection = -refract(-viewDirection, entryNormal, eta);
		}
		else
		{
			displacementParams.HeightScale *= PBRParams1.y;
		}
		mipLevel = MaterialLayers::GetMipLevel(uv, TexParallaxSampler);
		uv = MaterialLayers::GetParallaxCoords(viewPosition.z, uv, mipLevel, refractedViewDirection, tbnTr, screenNoise, TexParallaxSampler, SampParallaxSampler, 0, displacementParams, pixelOffset);
		if (SharedData::materialLayerSettings.EnableShadows && parallaxShadowQuality > 0.0)
			sh0 = TexParallaxSampler.SampleLevel(SampParallaxSampler, uv, mipLevel).x;
	}
#			endif  // !FACEGEN
#		endif      // MATERIAL_FORGE

#	elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float envMaskBase = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv).x;
#	endif  // EMAT

#	if defined(PIXL_AUTO_PARALLAX)
	// Vanilla textures do not carry a dedicated height map. Build a stable local
	// relief field from the existing diffuse texture, referenced against a coarse
	// mip so broad albedo colour does not become geometry. The normal map controls
	// only amplitude; its alpha/gloss channel is never repurposed.
	bool autoParallaxAllowed = inWorld && !inReflection && !SharedData::InMapMenu &&
		SharedData::materialLayerSettings.EnableParallax &&
		MaterialLayersTuning::ObjectAutoPOMEnabled();
#		if defined(EMAT_ENVMAP)
	// Complex Material masks may already describe an authored layered surface.
	// Never stack synthesized POM on top of that path.
	autoParallaxAllowed = autoParallaxAllowed && !complexMaterial;
#		endif

#	if defined(PIXL_WINDOW_LIFE_ACTIVE)
	// PIXL WL2A AUTO-POM GLASS SUPPRESSION
	// Window atlases may contain wood/stone and glass in one material. Suppress
	// synthetic relief on pane pixels only, never on the complete draw/material.
	[branch] if (autoParallaxAllowed && WindowLife::IsCandidate() && WindowLife::SuppressAutoPOM())
	{
		float4 pixlWindowPreColor = TexColorSampler.SampleBias(SampColorSampler, uv, SharedData::MipBias);
		float4 pixlWindowPreNormal = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
		float pixlWindowPreGlowLuma = 0.0f;
		[branch] if (WindowLife::HasGameGlowTexture())
		{
			float3 pixlWindowPreGlow = Color::Glowmap(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);
			pixlWindowPreGlowLuma = dot(max(pixlWindowPreGlow, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
		}
		float pixlWindowPrePane = WindowLife::PaneMask(
			pixlWindowPreColor.rgb, pixlWindowPreNormal, pixlWindowPreGlowLuma, uv);
		autoParallaxAllowed = pixlWindowPrePane < 0.34f;
	}
#	endif
	[branch] if (autoParallaxAllowed)
	{
		float autoDistanceFade = MaterialLayers::AutoParallaxDistanceFade(viewPosition.z);
		if (autoDistanceFade > 0.0f)
		{
			float2 autoParallaxTextureDims;
			autoParallaxMipLevel = MaterialLayers::GetAutoParallaxMipLevel(uv, TexColorSampler, autoParallaxTextureDims);
#			if defined(MODELSPACENORMALS)
			// Model-space normal variants do not carry a vertex TBN. Reconstruct the UV
			// frame from the original normal sample so they can participate as well.
			float3 autoModelNormal = TexNormalSampler.SampleLevel(
				SampNormalSampler, uv, min(autoParallaxMipLevel, 4.0f)).xzy * 2.0f - 1.0f;
			autoModelNormal = normalize(autoModelNormal);
			float3 autoModelNormalCoarse = TexNormalSampler.SampleLevel(
				SampNormalSampler, uv, min(autoParallaxMipLevel + 1.5f, 6.0f)).xzy * 2.0f - 1.0f;
			autoModelNormalCoarse = normalize(autoModelNormalCoarse);
			float3x3 autoParallaxTbn = ReconstructTBN(input.WorldPosition.xyz, autoModelNormal, screenUV);
			// Model-space normals have no meaningful tangent XY slope here. Cross-mip
			// variation is safer evidence than the old constant 0.60 confidence.
			float autoNormalActivity = lerp(
				0.10f,
				1.0f,
				saturate(length(autoModelNormal - autoModelNormalCoarse) * 1.8f));
			float2 autoNormalDirectionTS = 0.0f.xx;
#			else
			float3 autoNormalTS = TexNormalSampler.SampleLevel(
				SampNormalSampler, uv, min(autoParallaxMipLevel, 4.0f)).xyz * 2.0f - 1.0f;
			float autoNormalSlope = saturate(length(autoNormalTS.xy));
			// The shape classifier keeps flat plateaus continuous, so the amplitude floor
			// can stay conservative on painted/flat materials.
			float autoNormalActivity = lerp(0.12f, 1.0f, saturate(autoNormalSlope * 1.65f));
			float2 autoNormalDirectionTS = autoNormalTS.xy;
			float3x3 autoParallaxTbn = tbnTr;
#			endif
			uv = MaterialLayers::GetAutoParallaxCoords(
				viewPosition.z, uv, autoParallaxMipLevel, viewDirection, autoParallaxTbn, autoParallaxTextureDims,
				autoNormalActivity, autoNormalDirectionTS, TexColorSampler, SampColorSampler,
				pixelOffset, autoParallaxBaseHeight, autoParallaxStrength);
			autoParallaxApplied = autoParallaxStrength > 1e-5f;
		}
	}
#	endif

#	if defined(PIXL_AUTO_PARALLAX) && defined(ENVMAP)
	// Keep the authored environment mask registered to the displaced surface.
	[branch] if (autoParallaxApplied)
		envMaskBase = TexEnvMaskSampler.Sample(SampEnvMaskSampler, uv).x;
#	endif

#	if defined(PIXL_PARALLAX_DEPTH) && !defined(LANDSCAPE)
	bool pixlAllowDepthResolve = inWorld && !inReflection && !SharedData::InMapMenu;
#		if defined(MATERIAL_FORGE) && !defined(LODLANDSCAPE)
	// Interlayer parallax describes refraction beneath a coat, not the outer geometric surface.
	// It must never move the hardware depth buffer.
	pixlAllowDepthResolve = pixlAllowDepthResolve && ((PBRFlags & PBR::Flags::InterlayerParallax) == 0);
#		endif
	float2 pixlDepthUvDelta = uv - uvOriginal;
	[branch] if (pixlAllowDepthResolve && dot(pixlDepthUvDelta, pixlDepthUvDelta) > 1e-12f)
	{
		pixlParallaxWorldPosition = PixlResolveParallaxDepthPosition(
			input.WorldPosition.xyz, viewDirection, uvOriginal, uv,
			pixlParallaxDPdx, pixlParallaxDPdy, pixlParallaxDUVdx, pixlParallaxDUVdy,
			pixlParallaxDepthConfidence);

		if (pixlParallaxDepthConfidence > 1e-5f)
			pixlParallaxDepth = PixlProjectVirtualDepth(pixlParallaxWorldPosition, input.Position.z);
	}
#	endif

#	if defined(SNOW)
	bool useSnowSpecular = true;
#	else
	bool useSnowSpecular = false;
#	endif  // SNOW

#	if defined(SPARKLE) || !defined(PROJECTED_UV)
	bool useSnowDecalSpecular = true;
#	else
	bool useSnowDecalSpecular = false;
#	endif  // defined(SPARKLE) || !defined(PROJECTED_UV)

	float2 diffuseUv = uv;

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * input.TexCoord0.zw;
#	endif  // SPARKLE

#	if defined(EYE) && USE_PIXL_DYNAMIC_PUPILS
	// PIXL_EYES_PHASE1_BEGIN
	const float3 pixlEyeOpticalNormal = normalize(input.EyeNormal);
	const float3 pixlEyeAmbientRGB =
		max(0.0f.xxx, mul(DirectionalAmbient, float4(pixlEyeOpticalNormal, 1.0f)));

	float pixlEyeStimulus =
		EyeRendering::Luminance(max(DirLightColor, 0.0f.xxx)) * PIXL_EYE_DIRECTIONAL_WEIGHT +
		EyeRendering::Luminance(pixlEyeAmbientRGB) * PIXL_EYE_AMBIENT_WEIGHT;

	const uint pixlEyeLightCount = min(7u, (uint)max(NumLightNumShadowLight.x, 0.0f));
	[unroll] for (uint pixlEyeLightIndex = 0u; pixlEyeLightIndex < 7u; ++pixlEyeLightIndex)
	{
		if (pixlEyeLightIndex < pixlEyeLightCount)
		{
			pixlEyeStimulus += EyeRendering::PointLightStimulus(
				input.WorldPosition.xyz,
				PointLightPosition[pixlEyeLightIndex],
				PointLightColor[pixlEyeLightIndex].xyz);
		}
	}

	const float pixlEyePupilDilation = EyeRendering::PupilDilationFromLight(
		pixlEyeStimulus, SharedData::InInterior != 0);
	diffuseUv = EyeRendering::WarpPupilUV(diffuseUv, pixlEyePupilDilation);
	// PIXL_EYES_PHASE1_END
#	endif

#	if defined(LANDSCAPE)
	// Normalise blend weights
	float totalWeight = input.LandBlendWeights1.x + input.LandBlendWeights1.y + input.LandBlendWeights1.z +
	                    input.LandBlendWeights1.w + input.LandBlendWeights2.x + input.LandBlendWeights2.y;
	if (totalWeight > 0.0) {
		input.LandBlendWeights1 /= totalWeight;
		input.LandBlendWeights2.xy /= totalWeight;
	}
	float3 blendedRGB = 0;
	float blendedAlpha = 0;
	float3 blendedNormalRGB = 0;
	float blendedNormalAlpha = 0;

#		if defined(MATERIAL_FORGE)
	float4 blendedRMAOS = 0;
#		endif

#		if defined(EMAT)
	if (LANDSCAPE_PARALLAX_ENABLED) {
		float terrainMaxTexDim = 0.0;
		MaterialLayers::InitializeTerrainMipLevels(uv, mipLevels, terrainMaxTexDim);
		[unroll] for (uint terrainMipIndex = 0; terrainMipIndex < 6; terrainMipIndex++)
		{
			terrainShadowMipLevels[terrainMipIndex] = min(mipLevels[terrainMipIndex], MaterialLayers::TerrainParallaxShadowMaxMipLevel);
		}

		displacementParams[1] = displacementParams[0];
		displacementParams[2] = displacementParams[0];
		displacementParams[3] = displacementParams[0];
		displacementParams[4] = displacementParams[0];
		displacementParams[5] = displacementParams[0];
#			if defined(MATERIAL_FORGE)
		displacementParams[0].HeightScale *= PBRParams1.y;
		displacementParams[1].HeightScale *= LandscapeTexture2PBRParams.y;
		displacementParams[2].HeightScale *= LandscapeTexture3PBRParams.y;
		displacementParams[3].HeightScale *= LandscapeTexture4PBRParams.y;
		displacementParams[4].HeightScale *= LandscapeTexture5PBRParams.y;
		displacementParams[5].HeightScale *= LandscapeTexture6PBRParams.y;
#			endif

		float weights[6];
		weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0.0;

		const float terrainEffectivePomScale =
			MaterialLayers::TerrainEffectivePomScale(
				input.LandBlendWeights1, input.LandBlendWeights2.xy, displacementParams);
		const bool doTerrainPom =
			MaterialLayers::TerrainHasAnyDisplacement() && terrainEffectivePomScale > 0.01f;
		[branch] if (doTerrainPom)
		{
			uv = MaterialLayers::GetParallaxCoords(input, viewPosition.z, uv, mipLevels, terrainMaxTexDim, viewDirection, tbnTr, screenNoise, displacementParams, sharedOffset, pixelOffset, weights);
		}
		else if (SharedData::materialLayerSettings.EnableHeightBlending)
		{
			float unusedHeight;
			unusedHeight = MaterialLayers::GetTerrainHeight(screenNoise, input, uv, mipLevels, displacementParams, 1.0, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, weights);
		}

		if (SharedData::materialLayerSettings.EnableHeightBlending) {
			input.LandBlendWeights1.x = weights[0];
			input.LandBlendWeights1.y = weights[1];
			input.LandBlendWeights1.z = weights[2];
			input.LandBlendWeights1.w = weights[3];
			input.LandBlendWeights2.x = weights[4];
			input.LandBlendWeights2.y = weights[5];
		}
		hasTerrainParallaxShadow =
			viewPosition.z < MaterialLayers::ParallaxCheapDistance &&
			MaterialLayers::TerrainHasAnyDisplacement() &&
			terrainEffectivePomScale > 0.01f;
		// sh0 feeds point-light terrain shadows (hasTerrainParallaxShadow), not only POM.
		if ((doTerrainPom || hasTerrainParallaxShadow) && SharedData::materialLayerSettings.EnableShadows && terrainDirectionalShadowQuality > 0.0) {
			hasCachedTerrainShadowBaseHeight = COMPUTE_TERRAIN_SHADOW_BASE(sh0);
			if (doTerrainPom && hasCachedTerrainShadowBaseHeight) {
				float3 dirLightDirectionTS = mul(DirLightDirection, tbn).xyz;
				cachedDirectionalTerrainParallaxShadow = EVAL_TERRAIN_DIR_SHADOW(sh0, dirLightDirectionTS);
				hasCachedDirectionalTerrainParallaxShadow = true;
			}
		}
	}
#		endif  // EMAT
#	endif      // LANDSCAPE

#	if defined(PIXL_PARALLAX_DEPTH) && defined(LANDSCAPE)
	// Landscape POM happens after the generic object/material POM block. Resolve
	// virtual hardware depth from the final displaced terrain UV here.
	bool pixlAllowTerrainDepthResolve =
		inWorld && !inReflection && !SharedData::InMapMenu &&
		MaterialLayersTuning::TerrainVirtualDepthEnabled();
	float2 pixlTerrainDepthUvDelta = uv - uvOriginal;
	[branch] if (pixlAllowTerrainDepthResolve &&
		dot(pixlTerrainDepthUvDelta, pixlTerrainDepthUvDelta) > 1e-12f)
	{
		pixlParallaxWorldPosition = PixlResolveParallaxDepthPosition(
			input.WorldPosition.xyz, viewDirection, uvOriginal, uv,
			pixlParallaxDPdx, pixlParallaxDPdy, pixlParallaxDUVdx, pixlParallaxDUVdy,
			pixlParallaxDepthConfidence);

		if (pixlParallaxDepthConfidence > 1e-5f)
			pixlParallaxDepth = PixlProjectVirtualDepth(
				pixlParallaxWorldPosition, input.Position.z);
	}
#	endif

#	if defined(PIXL_PARALLAX_DEPTH)
	pixlVirtualDepth = pixlParallaxDepth;
#	endif

#	if defined(SPARKLE)
	diffuseUv = ProjectedUVParams2.yy * (input.TexCoord0.zw + (uv - uvOriginal));
#	else
	diffuseUv = uv;
#	endif  // SPARKLE

	float4 baseColor = 0;
	float4 normal = 0;
	float glossiness = 0;
#	if defined(PIXL_SKIN)
	const bool skinEnabled = SharedData::skinOpticsData.skinParams.w > 0.0f;
#		if defined(SKIN)
	float skinRoughness = 0;
	float skinSpecular = 0;
	float skinFuzzMask = 1;
	float skinWetMask = 1;
	float skinAO = 1;
	bool skinRoughnessSet = false;
#		endif
#	endif

	float4 rawRMAOS = 0;
	float pixlDetailRoughnessDelta = 0.0f;

	float4 glintParameters = 0;

#	if defined(SNOW)
#		if !defined(MATERIAL_FORGE)
	float landSnowMask = 0.0;
#			if defined(LANDSCAPE)
	landSnowMask = GetLandSnowMaskValue(baseColor.w);
#			endif
#		endif
#	endif

#	if defined(LANDSCAPE)
#		if defined(TERRAIN_DETAIL)
	g_terrainStochasticLodBase = ComputeTerrainStochasticLodBase(uv);
#			define SampleTerrain(TEX, SAMP, UV, OFFSET) StochasticEffect(TEX, SAMP, UV, OFFSET)
#		else
#			define SampleTerrain(TEX, SAMP, UV, OFFSET) TEX.SampleBias(SAMP, UV, SharedData::MipBias)
#		endif
#		if defined(MATERIAL_FORGE)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(0, TexColorSampler, SampColorSampler, TexNormalSampler, SampNormalSampler, TexRMAOSSampler, SampRMAOSSampler, PBRParams1, LandscapeTexture1GlintParameters, input.LandBlendWeights1.x)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(1, TexLandColor2Sampler, SampLandColor2Sampler, TexLandNormal2Sampler, SampLandNormal2Sampler, TexLandRMAOS2Sampler, SampLandRMAOS2Sampler, LandscapeTexture2PBRParams, LandscapeTexture2GlintParameters, input.LandBlendWeights1.y)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(2, TexLandColor3Sampler, SampLandColor3Sampler, TexLandNormal3Sampler, SampLandNormal3Sampler, TexLandRMAOS3Sampler, SampLandRMAOS3Sampler, LandscapeTexture3PBRParams, LandscapeTexture3GlintParameters, input.LandBlendWeights1.z)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(3, TexLandColor4Sampler, SampLandColor4Sampler, TexLandNormal4Sampler, SampLandNormal4Sampler, TexLandRMAOS4Sampler, SampLandRMAOS4Sampler, LandscapeTexture4PBRParams, LandscapeTexture4GlintParameters, input.LandBlendWeights1.w)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(4, TexLandColor5Sampler, SampLandColor5Sampler, TexLandNormal5Sampler, SampLandNormal5Sampler, TexLandRMAOS5Sampler, SampLandRMAOS5Sampler, LandscapeTexture5PBRParams, LandscapeTexture5GlintParameters, input.LandBlendWeights2.x)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(5, TexLandColor6Sampler, SampLandColor6Sampler, TexLandNormal6Sampler, SampLandNormal6Sampler, TexLandRMAOS6Sampler, SampLandRMAOS6Sampler, LandscapeTexture6PBRParams, LandscapeTexture6GlintParameters, input.LandBlendWeights2.y)
#		else
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(0, TexColorSampler, SampColorSampler, TexNormalSampler, SampNormalSampler, input.LandBlendWeights1.x, LandscapeTexture1to4IsSnow.x)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(1, TexLandColor2Sampler, SampLandColor2Sampler, TexLandNormal2Sampler, SampLandNormal2Sampler, input.LandBlendWeights1.y, LandscapeTexture1to4IsSnow.y)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(2, TexLandColor3Sampler, SampLandColor3Sampler, TexLandNormal3Sampler, SampLandNormal3Sampler, input.LandBlendWeights1.z, LandscapeTexture1to4IsSnow.z)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(3, TexLandColor4Sampler, SampLandColor4Sampler, TexLandNormal4Sampler, SampLandNormal4Sampler, input.LandBlendWeights1.w, LandscapeTexture1to4IsSnow.w)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(4, TexLandColor5Sampler, SampLandColor5Sampler, TexLandNormal5Sampler, SampLandNormal5Sampler, input.LandBlendWeights2.x, LandscapeTexture5to6IsSnow.x)
	LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(5, TexLandColor6Sampler, SampLandColor6Sampler, TexLandNormal6Sampler, SampLandNormal6Sampler, input.LandBlendWeights2.y, LandscapeTexture5to6IsSnow.y)
#		endif

#		if defined(MATERIAL_LAYERS) && !defined(MATERIAL_FORGE)
	// PIXL material-space detail reconstruction. Only legacy terrain is touched;
	// authored Material Forge PBR remains authoritative.
	if (MaterialLayersTuning::DetailReconstructionEnabled() && inWorld && !inReflection && !SharedData::InMapMenu)
	{
		float terrainWeights[6] = {
			input.LandBlendWeights1.x,
			input.LandBlendWeights1.y,
			input.LandBlendWeights1.z,
			input.LandBlendWeights1.w,
			input.LandBlendWeights2.x,
			input.LandBlendWeights2.y
		};

		uint dominantLayer = 0u;
		float dominantWeight = terrainWeights[0];
		[unroll] for (uint detailLayer = 1u; detailLayer < 6u; ++detailLayer)
		{
			if (terrainWeights[detailLayer] > dominantWeight)
			{
				dominantWeight = terrainWeights[detailLayer];
				dominantLayer = detailLayer;
			}
		}

		[branch] if (dominantWeight >= MaterialLayersTuning::DetailDominantTerrainMinWeight())
		{
			float detailMip = MaterialDetail::GetMipLevel(uv, TexColorSampler);
			float detailVisibility = MaterialDetail::Visibility(
				viewPosition.z,
				detailMip,
				MaterialLayersTuning::DetailTerrainStrength() * dominantWeight);

			if (detailVisibility > 1e-4f)
			{
				float4 detailFineColor = 0.0f;
				float4 detailCoarseColor = 0.0f;
				float4 detailFineNormal = float4(0.5f, 0.5f, 1.0f, 1.0f);
				float4 detailCoarseNormal = detailFineNormal;
				float coarseMip = detailMip + MaterialLayersTuning::DetailMipSeparation();

				[branch] if (dominantLayer == 0u) {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexColorSampler, uv, detailMip, sharedOffset, 0u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexColorSampler, uv, coarseMip, sharedOffset, 0u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexNormalSampler, uv, detailMip, sharedOffset, 0u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexNormalSampler, uv, coarseMip, sharedOffset, 0u);
					}
				}
				else if (dominantLayer == 1u) {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor2Sampler, uv, detailMip, sharedOffset, 1u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor2Sampler, uv, coarseMip, sharedOffset, 1u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal2Sampler, uv, detailMip, sharedOffset, 1u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal2Sampler, uv, coarseMip, sharedOffset, 1u);
					}
				}
				else if (dominantLayer == 2u) {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor3Sampler, uv, detailMip, sharedOffset, 2u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor3Sampler, uv, coarseMip, sharedOffset, 2u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal3Sampler, uv, detailMip, sharedOffset, 2u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal3Sampler, uv, coarseMip, sharedOffset, 2u);
					}
				}
				else if (dominantLayer == 3u) {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor4Sampler, uv, detailMip, sharedOffset, 3u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor4Sampler, uv, coarseMip, sharedOffset, 3u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal4Sampler, uv, detailMip, sharedOffset, 3u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal4Sampler, uv, coarseMip, sharedOffset, 3u);
					}
				}
				else if (dominantLayer == 4u) {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor5Sampler, uv, detailMip, sharedOffset, 4u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor5Sampler, uv, coarseMip, sharedOffset, 4u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal5Sampler, uv, detailMip, sharedOffset, 4u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal5Sampler, uv, coarseMip, sharedOffset, 4u);
					}
				}
				else {
					detailFineColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor6Sampler, uv, detailMip, sharedOffset, 5u);
					detailCoarseColor = MaterialLayers::TerrainParallaxTexSample(TexLandColor6Sampler, uv, coarseMip, sharedOffset, 5u);
					if (MaterialLayersTuning::DetailQuality() >= 2u) {
						detailFineNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal6Sampler, uv, detailMip, sharedOffset, 5u);
						detailCoarseNormal = MaterialLayers::TerrainParallaxTexSample(TexLandNormal6Sampler, uv, coarseMip, sharedOffset, 5u);
					}
				}

				float3 enhancedTerrainColor = MaterialDetail::ReconstructAlbedo(
					detailFineColor.rgb, detailCoarseColor.rgb, detailVisibility);
				blendedRGB +=
					(enhancedTerrainColor - detailFineColor.rgb) * dominantWeight;

				if (MaterialLayersTuning::DetailQuality() >= 2u)
				{
					float3 enhancedTerrainNormal = MaterialDetail::ReconstructNormalEncoded(
						detailFineNormal.rgb, detailCoarseNormal.rgb, detailVisibility);
					blendedNormalRGB +=
						(enhancedTerrainNormal - detailFineNormal.rgb) * dominantWeight;
					pixlDetailRoughnessDelta = MaterialDetail::RoughnessDelta(
						detailFineColor.rgb, detailCoarseColor.rgb, detailVisibility);
				}
			}
		}
	}
#		endif
#		undef SampleTerrain

	float4 rawBaseColor = float4(blendedRGB, blendedAlpha);
	baseColor = float4(Color::Diffuse(blendedRGB), blendedAlpha);
	normal = float4(blendedNormalRGB, blendedNormalAlpha);
#		if defined(MATERIAL_FORGE)
	rawRMAOS = blendedRMAOS;
#		endif
#	else  // Non-landscape code
	float4 rawBaseColor = TexColorSampler.SampleBias(SampColorSampler, diffuseUv, SharedData::MipBias);
	baseColor = float4(Color::Diffuse(rawBaseColor.rgb), rawBaseColor.a);
	float4 normalColor = TexNormalSampler.SampleBias(SampNormalSampler, uv, SharedData::MipBias);
	normal = normalColor;
#		if defined(MATERIAL_LAYERS) && !defined(MATERIAL_FORGE) && !defined(SKIN) && !defined(HAIR) && !defined(EYE) && !defined(TREE_ANIM) && !defined(SPARKLE) && !defined(PROJECTED_UV) && !defined(DO_ALPHA_TEST) && !defined(DEPTH_WRITE_DECALS) && !defined(WORLD_MAP)
	if (MaterialLayersTuning::DetailReconstructionEnabled() &&
		inWorld && !inReflection && !SharedData::InMapMenu)
	{
		float detailMip = MaterialDetail::GetMipLevel(diffuseUv, TexColorSampler);
		float detailVisibility = MaterialDetail::Visibility(
			viewPosition.z, detailMip, MaterialLayersTuning::DetailObjectStrength());

		if (detailVisibility > 1e-4f)
		{
			float coarseMip = detailMip + MaterialLayersTuning::DetailMipSeparation();
			float3 coarseColor = TexColorSampler.SampleLevel(
				SampColorSampler, diffuseUv, coarseMip).rgb;
			float3 fineColorBeforeDetail = rawBaseColor.rgb;

			rawBaseColor.rgb = MaterialDetail::ReconstructAlbedo(
				rawBaseColor.rgb, coarseColor, detailVisibility);
			baseColor.xyz = Color::Diffuse(rawBaseColor.rgb);

			if (MaterialLayersTuning::DetailQuality() >= 2u)
			{
				float3 coarseNormal = TexNormalSampler.SampleLevel(
					SampNormalSampler, uv, coarseMip).rgb;
				normalColor.rgb = MaterialDetail::ReconstructNormalEncoded(
					normalColor.rgb, coarseNormal, detailVisibility);
				normal.rgb = normalColor.rgb;
				pixlDetailRoughnessDelta = MaterialDetail::RoughnessDelta(
					fineColorBeforeDetail, coarseColor, detailVisibility);
			}
		}
	}
#		endif
#		if defined(MATERIAL_FORGE)
	rawRMAOS = TexRMAOSSampler.SampleBias(SampRMAOSSampler, diffuseUv, SharedData::MipBias) * float4(PBRParams1.x, 1, 1, PBRParams1.z);
	if ((PBRFlags & PBR::Flags::Glint) != 0) {
		glintParameters = MultiLayerParallaxData;
	}
#		endif
#	endif

#	if defined(DISTANCE_BLEND)
#		if defined(LODOBJECTS) || defined(LODOBJECTSHD)
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::distanceBlendSettings.LODObjectGamma) * SharedData::distanceBlendSettings.LODObjectBrightness;
#		elif defined(LODLANDSCAPE)
#			if defined(TERRAIN_DETAIL)
	[branch] if (SharedData::terrainDetailSettings.enableLODTerrainTilingFix)
	{
		float4 lodStochasticColor = StochasticSampleLOD(screenNoise, TexColorSampler, SampColorSampler, uv);
		baseColor.xyz = Color::Diffuse(lodStochasticColor.rgb);
	}
#			endif
	baseColor.xyz = pow(abs(baseColor.xyz), SharedData::distanceBlendSettings.LODTerrainGamma) * SharedData::distanceBlendSettings.LODTerrainBrightness;
#		endif
#	endif  // DISTANCE_BLEND

#	if defined(SKIN) && defined(PIXL_SKIN)
	float4 skinsk = 0;
	float4 skinExtra = 0;
	float4 skinWetnessSample = 0;
	uint2 skinExtraDimensions = uint2(0, 0);
	uint2 wetnessDimensions = uint2(0, 0);
	bool hasSkinExtra = false;
	bool hasSkinWetness = false;
	if (skinEnabled) {
		skinsk = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
		TexSkinExtraSampler.GetDimensions(skinExtraDimensions.x, skinExtraDimensions.y);
		TexSkinWetnessSampler.GetDimensions(wetnessDimensions.x, wetnessDimensions.y);
		hasSkinExtra = skinExtraDimensions.x > 32 && skinExtraDimensions.y > 32;
		hasSkinWetness = wetnessDimensions.x > 32 && wetnessDimensions.y > 32;
	}
	float4 skinWetnessNormal = float4(0.f, 0.f, 0.f, 1.f);

	if (hasSkinExtra && SharedData::skinOpticsData.skinParams.x > 0.0f) {
		skinExtra = TexSkinExtraSampler.Sample(SampColorSampler, uv);
		skinRoughness = skinExtra.x;
		skinFuzzMask = skinExtra.y;
		skinAO = skinExtra.z;
		skinSpecular = skinExtra.w;
		skinRoughnessSet = true;
	} else {
		skinRoughnessSet = false;
	}
	if (hasSkinWetness && skinEnabled) {
		skinWetnessSample = TexSkinWetnessSampler.Sample(SampColorSampler, uv);
		if ((skinWetnessSample.y == 0 && skinWetnessSample.z == 0) || (skinWetnessSample.x == skinWetnessSample.y && skinWetnessSample.y == skinWetnessSample.z && skinWetnessSample.w >= 0.99f)) {
			skinWetMask = skinWetnessSample.x;
			skinWetnessNormal.xyz = CalculateNormalFromHeight(skinWetMask, SharedData::skinOpticsData.wetParams.w * 0.0001, uv) * 0.5 + 0.5;
		} else {
			skinWetnessNormal.xyz = skinWetnessSample.xyz;
			skinWetMask = skinWetnessSample.w;
		}
	} else {
		skinWetMask = 1.0;
	}
#	endif

	float landSnowMask1 = GetLandSnowMaskValue(baseColor.w);

#	if defined(MODELSPACENORMALS)
#		if defined(LODLANDNOISE)
	normal.xyz = normal.xzy - 0.5.xxx;
	float lodLandNoiseParameter = GetLodLandBlendParameter(baseColor.xyz);
	float noise = TexLandLodNoiseSampler.Sample(SampLandLodNoiseSampler, uv * 3.0.xx).x;
	float lodLandNoiseMultiplier = GetLodLandBlendMultiplier(lodLandNoiseParameter, noise);
	baseColor.xyz *= lodLandNoiseMultiplier;
	normal.xyz *= 2;
	normal.w = 1;
	glossiness = 0;
#		elif defined(LODLANDSCAPE)
	normal.xyz = 2.0.xxx * (-0.5.xxx + normal.xzy);
	normal.w = 1;
	glossiness = 0;
#		else
	normal.xyz = normal.xzy * 2.0.xxx + -1.0.xxx;
	normal.w = 1;
	glossiness = TexSpecularSampler.Sample(SampSpecularSampler, uv).x;
#		endif  // LODLANDNOISE
#	elif (defined(SNOW) && defined(LANDSCAPE))
	normal.xyz = GetLandNormal(landSnowMask1, normal.xyz, uv, SampNormalSampler, TexNormalSampler);
	glossiness = normal.w;
#	else
	normal.xyz = TransformNormal(normal.xyz);
#		if defined(TREE_ANIM) && defined(FOLIAGE_DYNAMICS)
	// TreeFlipNormalY only affects animated tree/leaf tangent-space normals -
	// grass has its own independent flip controls in FoliageTuning (b13).
	[flatten] if (SharedData::foliageDynamicsSettings.TreeFlipNormalY != 0)
		normal.y = -normal.y;
#		endif
	glossiness = normal.w;
#	endif  // MODELSPACENORMALS

#	if defined(WORLD_MAP)
	normal.xyz = GetWorldMapNormal(input, normal.xyz, rawBaseColor.xyz);
#	endif  // WORLD_MAP

#	if defined(LANDSCAPE)
#		if defined(SNOW) && !defined(MATERIAL_FORGE)
	landSnowMask = LandscapeTexture1to4IsSnow.x * input.LandBlendWeights1.x;
#		endif  // SNOW
#	endif      // LANDSCAPE

#	if defined(EMAT_ENVMAP)
	complexMaterial = complexMaterial && complexMaterialColor.y > (4.0 / 255.0);
	shininess = lerp(shininess, shininess * complexMaterialColor.y, complexMaterial);
	if (complexMaterial) {
		complexSpecular = lerp(1.0, baseColor.xyz, complexMaterialColor.z);
		baseColor.xyz = lerp(baseColor.xyz, 0.0, complexMaterialColor.z);
	}
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if defined(FACEGEN)
	if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
		baseColor.xyz = GetFacegenBaseColor(baseColor.xyz, uv);
	} else {
		baseColor.xyz = Color::SkyrimGammaToLinear(GetFacegenBaseColor(Color::LinearToSkyrimGamma(baseColor.xyz), uv));
	}
#	elif defined(FACEGEN_RGB_TINT)
	if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
		baseColor.xyz = GetFacegenRGBTintBaseColor(baseColor.xyz, uv);
	} else {
		baseColor.xyz = Color::SkyrimGammaToLinear(GetFacegenRGBTintBaseColor(Color::LinearToSkyrimGamma(baseColor.xyz), uv));
	}
#	endif  // FACEGEN

#	if defined(SKIN) && defined(PIXL_SKIN)
	if (skinEnabled) {
		baseColor.xyz = baseColor.xyz * SharedData::skinOpticsData.skinParams2.w;
	}
#	endif  // PIXL_SKIN

#	if defined(HAIR) && defined(STRAND_SHADING)
	float3 hairTint = 0;

	if (SharedData::strandShadingSettings.Enabled) {
		hairTint = lerp(1, Color::Diffuse(TintColor.xyz), Color::ColorToLinear(input.Color.y));
		baseColor.xyz *= hairTint;
		baseColor.xyz = Hair::Saturation(baseColor.xyz, SharedData::strandShadingSettings.HairSaturation);
		baseColor.xyz *= SharedData::strandShadingSettings.BaseColorMult;
		baseColor.xyz = SharedData::strandShadingSettings.HairMode == 1 ? baseColor.xyz * baseColor.xyz : baseColor.xyz;  // To match color for Marschner
	}

	float3 sampledHairFlow = 0;
	bool useHairFlowMap = false;
#		if defined(BACK_LIGHTING)
	if (SharedData::strandShadingSettings.Enabled) {
		uint2 hairFlowDimensions = uint2(0, 0);
		sampledHairFlow = float3(TexBackLightSampler.Sample(SampBackLightSampler, uv).xy, 0.5f);
		TexBackLightSampler.GetDimensions(hairFlowDimensions.x, hairFlowDimensions.y);
		useHairFlowMap = (sampledHairFlow.x > 0.0 || sampledHairFlow.y > 0.0) && hairFlowDimensions.x > 32 && hairFlowDimensions.y > 32;
		sampledHairFlow = useHairFlowMap ? sampledHairFlow * 2.0f - 1.0f : float3(0.5f, 0.5f, 0.5f);
	}
#		endif
#	endif

#	if defined(LOD_LAND_BLEND)
	float4 lodLandColor;

#		if defined(TERRAIN_DETAIL)
	float2 blendColorUV = input.TexCoord0.zw;
	[branch] if (SharedData::terrainDetailSettings.enableLODTerrainTilingFix)
		lodLandColor = StochasticSampleLOD(screenNoise, TexLandLodBlend1Sampler, SampLandLodBlend1Sampler, blendColorUV);
	else
		lodLandColor = TexLandLodBlend1Sampler.SampleBias(SampLandLodBlend1Sampler, blendColorUV, SharedData::MipBias);
#		else
	lodLandColor = TexLandLodBlend1Sampler.Sample(SampLandLodBlend1Sampler, input.TexCoord0.zw);
#		endif

	lodLandColor.xyz = Color::ColorToLinear(lodLandColor.xyz) * Color::VanillaDiffuseColorMult();
#		if defined(DISTANCE_BLEND)
	lodLandColor.xyz = pow(abs(lodLandColor.xyz), SharedData::distanceBlendSettings.LODTerrainGamma) * SharedData::distanceBlendSettings.LODTerrainBrightness;
#		endif  // DISTANCE_BLEND
	float lodBlendParameter = GetLodLandBlendParameter(lodLandColor.xyz);
	float lodBlendMask = TexLandLodBlend2Sampler.Sample(SampLandLodBlend2Sampler, 3.0.xx * input.TexCoord0.zw).x;
	float lodLandFadeFactor = GetLodLandBlendMultiplier(lodBlendParameter, lodBlendMask);
	float lodLandBlendFactor = LODTexParams.z * input.LandBlendWeights2.w;
	normal.xyz = lerp(normal.xyz, float3(0, 0, 1), lodLandBlendFactor);

#		if !defined(MATERIAL_FORGE)
	baseColor.w = 0;
	baseColor = lerp(baseColor, lodLandColor * lodLandFadeFactor, lodLandBlendFactor);
	glossiness = lerp(glossiness, 0, lodLandBlendFactor);
#		endif
#	endif  // LOD_LAND_BLEND

#	if defined(SNOW) && !defined(MATERIAL_FORGE)
	useSnowSpecular = landSnowMask != 0.0;
#	endif  // SNOW

#	if defined(BACK_LIGHTING)
	float4 backLightColor = TexBackLightSampler.Sample(SampBackLightSampler, uv);
#		if defined(HAIR) && defined(STRAND_SHADING)
	if (useHairFlowMap) {
		backLightColor = 0.0f;
	}
#		endif
#	endif  // BACK_LIGHTING

#	if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING))
	float4 rimSoftLightColor = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);
#	endif  // RIM_LIGHTING || SOFT_LIGHTING

	uint numLights = min(7, uint(NumLightNumShadowLight.x));
	uint numShadowLights = min(4, uint(NumLightNumShadowLight.y));

#	if defined(MODELSPACENORMALS) && !defined(SKINNED)
	float3 worldNormal = normal.xyz;
	float3x3 tbnTr = ReconstructTBN(input.WorldPosition.xyz, worldNormal, screenUV);
#	else
	float3 worldNormal = normalize(mul(tbn, normal.xyz));
#		if defined(TREE_ANIM)
	// Preserve the authored normal; TBN orientation already follows front/back face.
	worldNormal = normalize(worldNormal);
#		endif

#		if defined(SPARKLE)
	float3 projectedNormal = normalize(mul(tbn, float3(ProjectedUVParams2.xx * normal.xy, normal.z)));
#		endif  // SPARKLE
#	endif      // defined (MODELSPACENORMALS) && !defined (SKINNED)

#	if defined(SKIN) && defined(PIXL_SKIN)
#		if defined(RAIN_RESPONSE)
	float3 skinWetNormal = worldNormal.xyz;
#			if defined(FACEGEN)
	float2 wetUV = uv;
#			else
	float2 wetUV = uv * SharedData::skinOpticsData.skinDetailParams.y;
#			endif
	float2 dynamicWet = SkinOptics::GetWetness(input.WorldPosition.z + FrameBuffer::CameraPosAdjust.z, worldNormal.xyz);
	float skinWetness = SkinOptics::PerlinNoise(wetUV, SharedData::skinOpticsData.wetParams.x, SharedData::skinOpticsData.wetParams.y, SharedData::skinOpticsData.wetParams.z, clamp(dynamicWet.x + dynamicWet.y + SharedData::skinOpticsData.skinParams2.y, 0.f, 2.f) * (hasSkinWetness ? 1.0 : 0.5));
	if ((SharedData::skinOpticsData.skinDetailParams.w > 0.0f || skinWetness > 0.0f) && skinEnabled)
#		else
	if (SharedData::skinOpticsData.skinDetailParams.w > 0.0f && skinEnabled)
#		endif
	{
#		if defined(FACEGEN)
		float2 detailUV = input.TexCoord0.xy * SharedData::skinOpticsData.skinDetailParams.x;
#		else
		float2 detailUV = input.TexCoord0.xy * SharedData::skinOpticsData.skinDetailParams.x * SharedData::skinOpticsData.skinDetailParams.y;
#		endif  // FACEGEN
#		if defined(MODELSPACENORMALS)
		const float3x3 tbnTr = ReconstructTBN(input.WorldPosition.xyz, worldNormal, screenUV);
		const float3x3 tbn = transpose(tbnTr);
		const float3 tangentNormal = mul(tbnTr, worldNormal.xyz);
#		else
		const float3 tangentNormal = normal.xyz;
#		endif  // MODELSPACENORMALS
		const float4 skinDetailSample = SkinOptics::TexSkinDetailNormal.SampleBias(
			SampNormalSampler, detailUV, SharedData::MipBias - 1.0f);
		skinAO *= skinDetailSample.w;
		// Dialogue close-ups get slightly more resolved skin micro-normal energy.
		// This does not change texture tiling or authored skin colour.
		const float pixlDialogueMicroDetail =
			1.0f +
			0.18f * pixlDialogueFocus *
			DialogueFocus::SkinQuality() *
			DialogueFocus::MicroDetailQuality();
		float2 detailNormalXY =
			(skinDetailSample.xy * 2.0f - 1.0f) *
			SharedData::skinOpticsData.skinDetailParams.z *
			pixlDialogueMicroDetail;
		// Reconstruct a unit tangent-space normal before RNM composition. The old
		// z=0 detail vector distorted pore slopes and then restored the base z by hand,
		// producing brittle highlights instead of small coherent skin relief.
		const float detailLengthSq = dot(detailNormalXY, detailNormalXY);
		if (detailLengthSq > 0.9604f)
			detailNormalXY *= sqrt(0.9604f / detailLengthSq);
		const float3 detailNormal = float3(
			detailNormalXY,
			sqrt(max(1.0f - dot(detailNormalXY, detailNormalXY), 1e-4f)));
		float3 combinedTangentNormal = normalize(ReorientNormal(detailNormal, normalize(tangentNormal)));
		float3 combinedNormal = normalize(mul(tbn, combinedTangentNormal));
		if (SharedData::skinOpticsData.skinDetailParams.w > 0.0f)
			worldNormal.xyz = combinedNormal;
#		if defined(RAIN_RESPONSE)
		if (skinWetness > 0.0f) {
			float3 wetNormal = CalculateNormalFromHeight(skinWetness, SharedData::skinOpticsData.wetParams.w * 0.0005, uv);
			if (hasSkinWetness) {
				float3 wetMaskNormal = (skinWetnessNormal.xyz * 2.0 - 1.0);
				wetNormal = ReorientNormal(wetMaskNormal, wetNormal);
			}
			if (SharedData::skinOpticsData.skinParams2.y > 1.0f) {
				wetNormal = lerp(wetNormal, tangentNormal, saturate(SharedData::skinOpticsData.skinParams2.y - 1.0f));
			}
			float3 combinedWetNormal = skinWetMask ? wetNormal : combinedTangentNormal;
			skinWetNormal = normalize(mul(tbn, combinedWetNormal));
			skinWetNormal = lerp(worldNormal.xyz, skinWetNormal, skinWetness > 0 ? 1 : 0);
		}
#		endif
	}
#	endif  // PIXL_SKIN

	float2 baseShadowUV = 1.0.xx;
	float4 shadowColor = 1.0;
	if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir) || inWorld) || numShadowLights > 0) {
		baseShadowUV = input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy;
		float2 adjustedShadowUV = baseShadowUV * VPOSOffset.xy + VPOSOffset.zw;
		float2 shadowUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(adjustedShadowUV);
		shadowColor = TexShadowMaskSampler.Sample(SampShadowMaskSampler, shadowUV);
	}

	// PIXL_GR_13BE_WORLDSPACE_SHADOW_RECEIVER_V1
	//
	// TerrainSeam replays the GroundResponse shell after Skyrim's screen-space
	// shadow mask was generated for the undisplaced/base landscape receiver.
	// Keep that mask intact for every normal draw, but do NOT consume it for the
	// raised shell. Directional shell shadowing is reconstructed later from the
	// displaced WorldPosition instead, so buried actors cannot project their old
	// base-terrain receiver shadow through the new snow/mud top.
	bool pixlGroundRaisedShell = false;
#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	pixlGroundRaisedShell = GroundResponseRuntime::IsGeometryPass();
#	endif

	float projectedMaterialWeight = 0;

	float projWeight = 0;

#	if defined(PROJECTED_UV)
	float3 projWorldPos = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
	float3 triFaceNormal = normalize(-cross(ddx(input.WorldPosition.xyz), ddy(input.WorldPosition.xyz)));
	float3 triWeights = Triplanar::GetWeights(tbnTr[2], triFaceNormal);
	float projNoise = Triplanar::Sample(TexCharacterLightProjNoiseSampler, SampCharacterLightProjNoiseSampler, projWorldPos, triWeights, ProjectedUVParams.z).x;
	float3 texProj = normalize(input.TexProj);
#		if defined(TREE_ANIM) || defined(LODOBJECTSHD)
	float vertexAlpha = 1;
#		else
	float vertexAlpha = input.Color.w;
#		endif  // defined (TREE_ANIM) || defined (LODOBJECTSHD)
	projWeight = -ProjectedUVParams.x * projNoise + (dot(worldNormal.xyz, texProj) * vertexAlpha - ProjectedUVParams.w);
#		if defined(LODOBJECTSHD)
	projWeight += (-0.5 + input.Color.w) * 2.5;
#		endif  // LODOBJECTSHD
#		if defined(SPARKLE)
	if (projWeight < 0)
		discard;

	rawBaseColor = Triplanar::SampleStochasticBias(TexColorSampler, SampColorSampler, projWorldPos, triWeights, ProjectedUVParams2.y, SharedData::MipBias, screenNoise);
	baseColor = float4(Color::Diffuse(rawBaseColor.rgb), rawBaseColor.a);
	worldNormal.xyz = projectedNormal;
#		elif !defined(FACEGEN) && !defined(MULTI_LAYER_PARALLAX) && !defined(PARALLAX) && !defined(SPARKLE)
	if (ProjectedUVParams3.w > 0.5) {
		float diffuseNormalScale = ProjectedUVParams3.x * ProjectedUVParams.z;
		float3 projNormal = TransformNormal(Triplanar::SampleStochastic(TexProjNormalSampler, SampProjNormalSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz);
		float detailNormalScale = ProjectedUVParams3.y * ProjectedUVParams.z;
		float3 projDetailNormal = Triplanar::SampleStochastic(TexProjDetail, SampProjDetailSampler, projWorldPos, triWeights, detailNormalScale, screenNoise).xyz;
		float3 finalProjNormal = normalize(TransformNormal(projDetailNormal) * float3(1, 1, projNormal.z) + float3(projNormal.xy, 0));
		float3 projBaseColor = Color::ColorToLinear(Triplanar::SampleStochastic(TexProjDiffuseSampler, SampProjDiffuseSampler, projWorldPos, triWeights, diffuseNormalScale, screenNoise).xyz) * Color::ColorToLinear(ProjectedUVParams2.xyz);
		projectedMaterialWeight = smoothstep(0, 1, 5 * (0.1 + projWeight));
#			if defined(MATERIAL_FORGE)
		projBaseColor = max(0, projBaseColor.xyz * MaterialObjectRGBScale);
		rawRMAOS.xyw = lerp(rawRMAOS.xyw, float3(ParallaxOccData.x, 0, ParallaxOccData.y), projectedMaterialWeight);
		float4 projectedGlintParameters = 0;
		if ((PBRFlags & PBR::Flags::ProjectedGlint) != 0) {
			projectedGlintParameters = SparkleParams;
		}
		glintParameters = lerp(glintParameters, projectedGlintParameters, projectedMaterialWeight);
#			else
		projBaseColor *= Color::VanillaDiffuseColorMult();
#			endif  // MATERIAL_FORGE
#			if defined(DISTANCE_BLEND) && (defined(LODOBJECTS) || defined(LODOBJECTSHD))
		projBaseColor.xyz = pow(abs(projBaseColor.xyz), SharedData::distanceBlendSettings.LODObjectSnowGamma) * SharedData::distanceBlendSettings.LODObjectSnowBrightness;
#			endif  // DISTANCE_BLEND
		normal.xyz = lerp(normal.xyz, finalProjNormal, projectedMaterialWeight);
		baseColor.xyz = lerp(baseColor.xyz, projBaseColor, projectedMaterialWeight);

#			if defined(SNOW)
		useSnowDecalSpecular = true;
#			endif  // SNOW
	} else {
		if (projWeight > 0) {
			baseColor.xyz = Color::Diffuse(ProjectedUVParams2.xyz);
#			if defined(SNOW)
			useSnowDecalSpecular = true;
#			endif  // SNOW
		}
	}

#			if defined(SPECULAR)
	useSnowSpecular = useSnowDecalSpecular;
#			endif  // SPECULAR
#		endif      // SPARKLE

#	endif  // SNOW

#	if defined(WORLD_MAP)
	baseColor.xyz = GetWorldMapBaseColor(rawBaseColor.xyz, baseColor.xyz, projWeight);
#	endif  // WORLD_MAP

#	if defined(MODELSPACENORMALS)
	float3 vertexNormal = worldNormal;
#	endif

#	if defined(PIXL_WINDOW_LIFE_ACTIVE)
	// PIXL WL2A ARCHITECTURAL GLASS SURFACE
	// Read the real glow map directly when the CPU classifier says one exists. PIXL's
	// shader lookup intentionally strips Skyrim's Glowmap technique bit, so relying on
	// that descriptor alone makes inhabited windows invisible even when the texture is bound.
	float pixlWindowGlowLuma = 0.0f;
	[branch] if (WindowLife::HasGameGlowTexture())
	{
		float3 pixlWindowGlowEvidence = Color::Glowmap(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);
		pixlWindowGlowLuma = dot(max(pixlWindowGlowEvidence, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
	}

	WindowLife::SurfaceResult pixlWindowSurface = WindowLife::EvaluateSurface(
		input.WorldPosition.xyz,
		viewDirection,
		vertexNormal,
		rawBaseColor.rgb,
		normalColor,
		pixlWindowGlowLuma,
		uv,
		viewPosition.z);

	[branch] if (pixlWindowSurface.glassWeight > 1.0e-4f)
	{
		// Old architectural glass is much flatter than Auto-POM stone/wood. Retain a
		// controlled fraction of authored normal detail, then add low-frequency waviness
		// in the actual pane plane instead of embossing the diffuse texture.
		worldNormal = normalize(lerp(vertexNormal, worldNormal, pixlWindowSurface.normalRetention));
		float3 pixlGlassN = normalize(vertexNormal);
		float3 pixlGlassH = cross(float3(0.0f, 0.0f, 1.0f), pixlGlassN);
		float pixlGlassHLen2 = dot(pixlGlassH, pixlGlassH);
		pixlGlassH = pixlGlassHLen2 > 1.0e-5f
			? pixlGlassH * rsqrt(pixlGlassHLen2)
			: float3(1.0f, 0.0f, 0.0f);
		float3 pixlGlassV = normalize(cross(pixlGlassN, pixlGlassH));
		worldNormal = normalize(
			worldNormal +
			pixlGlassH * pixlWindowSurface.normalWarp.x +
			pixlGlassV * pixlWindowSurface.normalWarp.y);
	}
#	endif
	float groundDeformationAmount = 0.0f;
	float groundDeformationDepth = 0.0f;
	float groundDeformationFreshness = 0.0f;
	float groundDeformationEdge = 0.0f;
	float groundSnowCoverage = 0.0f;
	float groundSnowMix = 0.0f;
	float groundMaterialActivation = 0.0f;
	float2 groundAbsoluteXY = 0.0f.xx;
	float groundSnowMicroDetail = 0.5f;
	float groundMudMicroDetail = 0.5f;
	float groundMudWaterAccumulation = 0.0f;
	float groundSnowBackscatterWeight = 0.0f;
	bool groundSnowClassificationValid = false;
	bool groundPixelUsesGeometricSurface = false;
#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	if (SharedData::deformableGroundSettings.EnableDeformableGround != 0) {
		groundSnowClassificationValid =
			GroundResponseRuntime::HasExactTerrainClassification();

		float snowActivation = 0.0f;
		float mudActivation = 0.0f;
		float groundMaximumDepth = 0.0f;

		if (groundSnowClassificationValid) {
			// Authoritative six-layer path: Skyrim's per-layer textureIsSnow flags are
			// multiplied by the final terrain blend weights after any PIXL height blend.
			groundSnowCoverage =
				GroundResponseRuntime::GetTerrainSnowCoverage(
					input.LandBlendWeights1,
					input.LandBlendWeights2.xy);
			float snowSurfaceMask =
				GroundResponseRuntime::GetSnowSurfaceMask(groundSnowCoverage);

			if (SharedData::deformableGroundSettings.EnableSnowDeformation != 0)
				snowActivation = snowSurfaceMask;

			if (SharedData::deformableGroundSettings.EnableMudDeformation != 0) {
				float mudWetness = 1.0f;
				if (SharedData::deformableGroundSettings.MudRequiresWetness != 0) {
					float threshold =
						saturate(
							SharedData::deformableGroundSettings.MudWetnessThreshold);
					float mudWeatherSignal =
						saturate(max(
							SharedData::rainResponseSettings.Raining,
							SharedData::rainResponseSettings.Wetness));
					mudWetness =
						smoothstep(
							max(threshold - 0.22f, 0.0f),
							min(threshold + 0.18f, 1.0f),
							mudWeatherSignal);
				}
				mudActivation =
					(1.0f - snowSurfaceMask) * mudWetness;
			}
		} else {
#			if defined(MATERIAL_FORGE)
			// Material Forge repurposes the vanilla b1 snow registers. Without the
			// preserved CPU metadata we cannot distinguish snow from rock/soil safely.
			// Fail closed instead of reverting to the old whole-pass guess.
			snowActivation = 0.0f;
			mudActivation = 0.0f;
#			else
			// Vanilla Lighting still exposes the exact six layer flags in PS b1, so
			// legacy terrain remains correctly classified even if TerrainSeam is off.
			float4 legacyW1 = max(input.LandBlendWeights1, 0.0f.xxxx);
			float2 legacyW2 = max(input.LandBlendWeights2.xy, 0.0f.xx);
			float legacyWeightSum =
				dot(legacyW1, 1.0f.xxxx) + dot(legacyW2, 1.0f.xx);
			if (legacyWeightSum > 1e-5f) {
				float invLegacyWeight = rcp(legacyWeightSum);
				legacyW1 *= invLegacyWeight;
				legacyW2 *= invLegacyWeight;
			}
			groundSnowCoverage =
				saturate(
					dot(legacyW1, saturate(LandscapeTexture1to4IsSnow)) +
					dot(legacyW2, saturate(LandscapeTexture5to6IsSnow.xy)));
			groundSnowClassificationValid = true;
			float snowSurfaceMask =
				GroundResponseRuntime::GetSnowSurfaceMask(groundSnowCoverage);

			if (SharedData::deformableGroundSettings.EnableSnowDeformation != 0)
				snowActivation = snowSurfaceMask;

			if (SharedData::deformableGroundSettings.EnableMudDeformation != 0) {
				float mudWetness = 1.0f;
				if (SharedData::deformableGroundSettings.MudRequiresWetness != 0) {
					float threshold =
						saturate(
							SharedData::deformableGroundSettings.MudWetnessThreshold);
					float mudWeatherSignal =
						saturate(max(
							SharedData::rainResponseSettings.Raining,
							SharedData::rainResponseSettings.Wetness));
					mudWetness =
						smoothstep(
							max(threshold - 0.22f, 0.0f),
							min(threshold + 0.18f, 1.0f),
							mudWeatherSignal);
				}
				mudActivation =
					(1.0f - snowSurfaceMask) * mudWetness;
			}
#			endif
		}

		float activationSum = snowActivation + mudActivation;
		groundMaterialActivation = saturate(activationSum);
		groundPixelUsesGeometricSurface =
			GroundResponseRuntime::IsGeometryPass() &&
			groundSnowClassificationValid &&
			groundMaterialActivation > 1e-4f;
		if (activationSum > 1e-5f) {
			groundSnowMix = saturate(snowActivation / activationSum);
			groundMaximumDepth =
				lerp(
					SharedData::deformableGroundSettings.MudMaximumDepth,
					SharedData::deformableGroundSettings.SnowMaximumDepth,
					groundSnowMix);
		}

		// On the unified tessellated snow/mud path WorldPosition is the raised/compacted surface.
		// Sample the shared actor field at the base terrain receiver to avoid counting
		// the snow shell thickness itself as collider penetration.
		float3 groundReceiverPosition =
			groundPixelUsesGeometricSurface
				? input.GroundBaseWorldPosition
				: input.WorldPosition.xyz;

		DeformableGround::SampleData groundSample =
			DeformableGround::Evaluate(
				groundReceiverPosition,
				worldNormal.xyz,
				groundMaximumDepth);
		groundDeformationAmount =
			saturate(groundSample.amount * groundMaterialActivation);
		groundDeformationDepth =
			groundSample.depth * groundMaterialActivation;
		groundDeformationFreshness =
			saturate(groundSample.freshness * groundMaterialActivation);
		groundDeformationEdge =
			saturate(groundSample.edge * groundMaterialActivation);
		worldNormal =
			normalize(
				lerp(
					worldNormal,
					groundSample.normal,
					groundMaterialActivation));

		// PIXL_GR_13Z_MICROSURFACE_EVALUATION_V1
		groundAbsoluteXY =
			groundReceiverPosition.xy +
			FrameBuffer::CameraPosAdjust.xy;

		float groundMicroDistanceFade =
			1.0f -
			smoothstep(
				720.0f,
				1700.0f,
				length(groundReceiverPosition));

		groundSnowMicroDetail =
			PixlGroundSnowMicro(
				groundAbsoluteXY);
		groundMudMicroDetail =
			PixlGroundMudMicro(
				groundAbsoluteXY);

		float localSnowWeight =
			saturate(groundSnowMix);
		float localMudWeight =
			1.0f - localSnowWeight;

		// PIXL_GR_13BI_MUD_RESERVOIR_V1
		float mudRain =
			saturate(
				SharedData::rainResponseSettings.Raining);
		float mudWetness =
			saturate(
				SharedData::rainResponseSettings.Wetness);

		float mudBasin =
			PixlGroundMudBasin(
				groundAbsoluteXY);

		// Deep rut centres collect first; shoulders and steep surfaces drain.
		float waterDepthSignal =
			saturate(
				groundDeformationAmount *
					(1.0f - groundDeformationEdge * 0.42f) +
				(mudBasin - 0.5f) * 0.20f);

		float mudFlatness =
			smoothstep(
				0.84f,
				0.985f,
				saturate(abs(worldNormal.z)));

		// t101 freshness/hold already counts down BEFORE compaction recovery starts.
		// Reusing that clock gives us the material order we want with no new texture:
		// wet rut -> pooled centre -> retreating water -> mud finally rises.
		float mudDrainageClock =
			smoothstep(
				0.04f,
				0.82f,
				groundDeformationFreshness);

		// Rain recharges the reservoir immediately. Residual world wetness can keep
		// a shallow film alive, but increasingly only in the deepest basin core.
		float mudReservoir =
			saturate(
				mudRain +
				mudWetness *
					(0.28f + 0.72f * mudDrainageClock));

		float mudBasinFill =
			smoothstep(
				0.34f,
				0.78f,
				waterDepthSignal);
		float mudDeepCore =
			smoothstep(
				0.56f,
				0.90f,
				waterDepthSignal);

		float mudRetreatingFill =
			saturate(
				mudBasinFill *
				lerp(
					mudDeepCore,
					1.0f,
					mudReservoir));

		groundMudWaterAccumulation =
			localMudWeight *
			mudFlatness *
			mudReservoir *
			mudRetreatingFill;

		// PIXL_GR_13BI_SNOW_THICKNESS_BACKSCATTER_INPUT_V1
		// TerrainSurface raises the hull vertically, therefore raised Z above the
		// preserved base receiver is an inexpensive real thickness proxy. Thin or
		// heavily compacted snow loses porous scattering.
		float groundSnowVisibleThickness =
			groundPixelUsesGeometricSurface
				? max(
					input.WorldPosition.z -
						input.GroundBaseWorldPosition.z,
					0.0f)
				: max(
					GroundRuntimeSnowSurfaceThickness,
					0.0f);

		float groundSnowThicknessScatter =
			smoothstep(
				1.5f,
				18.0f,
				groundSnowVisibleThickness);
		float groundSnowPorosity =
			1.0f -
			saturate(
				groundDeformationAmount *
					0.85f);

		groundSnowBackscatterWeight =
			saturate(
				localSnowWeight *
				groundSnowThicknessScatter *
				lerp(
					0.22f,
					1.0f,
					groundSnowPorosity));

		// Water flattens the mud's micro-normal as the rut fills.
		float2 groundMicroVector =
			PixlGroundMicroVector(
				groundAbsoluteXY);

		float snowMicroNormalStrength =
			0.080f *
			localSnowWeight;
		float mudMicroNormalStrength =
			0.105f *
			localMudWeight *
			(1.0f -
				groundMudWaterAccumulation * 0.88f);
		float groundMicroNormalStrength =
			groundDeformationAmount *
			groundMicroDistanceFade *
			(snowMicroNormalStrength +
			 mudMicroNormalStrength);

		worldNormal =
			normalize(
				worldNormal +
				float3(
					groundMicroVector *
						groundMicroNormalStrength,
					0.0f));

#		if defined(PIXL_GROUND_DEFORMATION_DEPTH)
		// The unified geometric snow/mud pass already rasterizes the displaced surface.
		// Virtual recession is retained only when the geometric path is unavailable.
		[branch] if (!groundPixelUsesGeometricSurface &&
			groundDeformationDepth > 0.001f &&
			groundDeformationAmount > 0.001f &&
			inWorld && !inReflection && !SharedData::InMapMenu)
		{
			pixlVirtualDepth =
				PixlResolveGroundDeformationDepth(
					groundReceiverPosition,
					groundDeformationDepth,
					groundDeformationAmount,
					pixlVirtualDepth);
		}
#		endif
	}
#	endif

	float3 screenSpaceNormal = normalize(FrameBuffer::WorldToView(worldNormal, false));

#	if defined(HAIR) && defined(STRAND_SHADING)
	float3 Bitangent = normalize(float3(input.TBN0.y, input.TBN1.y, input.TBN2.y));
	float3 hairT = 0;
#		if defined(BACK_LIGHTING)
	hairT = useHairFlowMap ? normalize(mul(tbn, sampledHairFlow)) : Bitangent;
#		else
	hairT = Bitangent;
#		endif
	hairT = Hair::ReorientTangent(hairT, worldNormal);

	if (SharedData::strandShadingSettings.Enabled) {
		if (SharedData::strandShadingSettings.EnableTangentShift && SharedData::strandShadingSettings.HairMode != 1) {
			float3 shiftedNormal = Hair::ShiftWorldNormal(hairT, worldNormal, 0, uv);
			screenSpaceNormal = normalize(FrameBuffer::WorldToView(shiftedNormal, false));
		}
	}
#	endif

	MaterialProperties material = (MaterialProperties)0;

	material.F0 = 0;
	material.Roughness = 1;
	material.Metallic = 0;

#	if defined(MATERIAL_FORGE)
	material.Noise = screenNoise;

	material.Roughness = clamp(rawRMAOS.x, PBR::Constants::MinRoughness, PBR::Constants::MaxRoughness);
	material.Metallic = saturate(rawRMAOS.y);
	material.AO = rawRMAOS.z;

	// Apply vertex color to base color so PBR metals use it. On LANDSCAPE,
	// honor DisableTerrainVertexColors (as the non-PBR path does) by
	// neutralizing the source color so terrain vertex colors don't tint PBR;
	// a white source yields VertexAO == 1, i.e. no AO darkening either.
	float3 pbrVertexColorSrc = input.Color.xyz;
#		if defined(LANDSCAPE)
	if (SharedData::distanceBlendSettings.DisableTerrainVertexColors)
		pbrVertexColorSrc = 1;
#		endif
	float3 pbrVertexColor = Color::SrgbToLinear(pbrVertexColorSrc);
	float pbrVertexAO = max(max(pbrVertexColor.x, pbrVertexColor.y), pbrVertexColor.z);
	pbrVertexColor = pbrVertexAO == 0.0f ? 1.0f : pbrVertexColor * lerp(1 / max(pbrVertexAO, 0.001), 1, SharedData::materialForgeSettings.VertexAOStrength);

	if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
		baseColor.xyz = Color::SrgbToLinear(baseColor.xyz) * pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
		baseColor.xyz = Color::LinearToSrgb(baseColor.xyz);
	} else {
		baseColor.xyz *= pbrVertexColor;
		material.F0 = lerp(rawRMAOS.w, baseColor.xyz, material.Metallic);
	}

	material.GlintScreenSpaceScale = max(1, glintParameters.x);
	material.GlintLogMicrofacetDensity = clamp(PBR::Constants::MaxGlintDensity - glintParameters.y, PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity);
	material.GlintMicrofacetRoughness = clamp(glintParameters.z, PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
	material.GlintDensityRandomization = clamp(glintParameters.w, PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);

#		if defined(GLINT)
	float glintNoise = Random::R1Modified(float(SharedData::FrameCount), (Random::pcg2d(uint2(input.Position.xy)) / 4294967296.0).x);
	Glints::PrecomputeGlints(glintNoise, uvOriginal, ddx(uvOriginal), ddy(uvOriginal), material.GlintScreenSpaceScale, material.GlintCache);
#		endif

	baseColor.xyz *= 1 - material.Metallic;

	material.BaseColor = baseColor.xyz;

	float3 coatWorldNormal = worldNormal;

#		if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::Subsurface) != 0)
	{
		material.SubsurfaceColor = PBRParams2.xyz;
		material.Thickness = PBRParams2.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture0) != 0)
		{
			float4 sampledSubsurfaceProperties = TexRimSoftLightWorldMapOverlaySampler.Sample(SampRimSoftLightWorldMapOverlaySampler, uv);

			// If LL is off, Diffuse returns sRGB
			material.SubsurfaceColor *= Color::Diffuse(sampledSubsurfaceProperties.xyz);

			if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
				material.SubsurfaceColor = Color::LinearToSrgb(
					Color::SrgbToLinear(material.SubsurfaceColor) * pbrVertexColor);
			} else {
				material.SubsurfaceColor *= pbrVertexColor;
			}

			material.Thickness *= sampledSubsurfaceProperties.w;
		}
		material.Thickness = lerp(material.Thickness, 1, projectedMaterialWeight);
	}
	else if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		material.CoatColor = sampledCoatColor.xyz;
		material.CoatStrength = sampledCoatColor.w;
		material.CoatRoughness = MultiLayerParallaxData.x;
		material.CoatF0 = MultiLayerParallaxData.y;

		float2 coatUv = uv;
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			coatUv = uvOriginal;
		}
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledCoatProperties = TexBackLightSampler.Sample(SampBackLightSampler, coatUv);
			material.CoatRoughness *= sampledCoatProperties.w;
			[branch] if ((PBRFlags & PBR::Flags::CoatNormal) != 0)
			{
				coatWorldNormal = normalize(mul(tbn, TransformNormal(sampledCoatProperties.xyz)));
			}
		}
		material.CoatStrength = lerp(material.CoatStrength, 0, projectedMaterialWeight);
	}

	[branch] if ((PBRFlags & PBR::Flags::Fuzz) != 0)
	{
		material.FuzzColor = MultiLayerParallaxData.xyz;
		material.FuzzWeight = MultiLayerParallaxData.w;
		[branch] if ((PBRFlags & PBR::Flags::HasFeatureTexture1) != 0)
		{
			float4 sampledFuzzProperties = TexBackLightSampler.Sample(SampBackLightSampler, uv);
			material.FuzzColor *= Color::Diffuse(sampledFuzzProperties.xyz);
			material.FuzzWeight *= sampledFuzzProperties.w;
		}
		material.FuzzWeight = lerp(material.FuzzWeight, 0, projectedMaterialWeight);
	}
#		endif
#	else
#		if defined(SPECULAR)
	material.Shininess = shininess;
	material.Glossiness = glossiness;
	material.SpecularColor = SpecularColor.xyz;
	PhysicalMaterial::Surface physicalSurface = PhysicalMaterial::FromLegacy(baseColor.xyz, material.Shininess, material.SpecularColor, material.Glossiness);
#		else
	material.Shininess = 0;
	material.Glossiness = 0;
	material.SpecularColor = 0;
	PhysicalMaterial::Surface physicalSurface = PhysicalMaterial::FromLegacy(baseColor.xyz, 0, 0, 0);
#		endif
#		if defined(MATERIAL_LAYERS)
	if (abs(pixlDetailRoughnessDelta) > 1e-5f)
		physicalSurface.Roughness = MaterialDetail::ApplyRoughnessDetail(
			physicalSurface.Roughness, pixlDetailRoughnessDelta);
#		endif
	material.BaseColor = physicalSurface.BaseColor;
	material.Roughness = physicalSurface.Roughness;
	material.F0 = physicalSurface.F0;
#		if defined(EMAT_ENVMAP)
	PhysicalMaterial::ApplySpecularOverride(physicalSurface, complexSpecular, 1.0f - complexMaterialColor.y, complexMaterial);
#		endif
	[branch] if (SharedData::materialForgeSettings.EnableLegacyMetalInference != 0)
	{
		const float metalStrength = saturate(SharedData::materialForgeSettings.LegacyMetalInferenceStrength);
#		if defined(EMAT_ENVMAP)
		// Material Layers already removes the blue-channel conductor fraction
		// from baseColor above. Restore that authored conductor fraction to F0
		// without attenuating diffuse a second time.
		const float complexMetallic = saturate(complexMaterialColor.z) * (complexMaterial ? 1.0f : 0.0f) * metalStrength;
		PhysicalMaterial::ApplyMetallic(physicalSurface, complexSpecular, complexMetallic, 0.0f);
#		endif
#		if (defined(SPECULAR) || defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)) && !defined(EYE) && !defined(SKIN) && !defined(HAIR) && !defined(FACEGEN) && !defined(FACEGEN_RGB_TINT) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE) && !defined(TREE_ANIM)
		float legacyEnvironmentResponse = 0.0f;
#			if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)
		legacyEnvironmentResponse = envMaskBase;
#			endif
		float inferredMetallic = PhysicalMaterial::InferLegacyMetallic(
			physicalSurface.BaseColor,
			material.SpecularColor,
			material.Glossiness,
			physicalSurface.Roughness,
			legacyEnvironmentResponse,
			baseColor.w,
			SharedData::materialForgeSettings.LegacyMetalInferenceThreshold,
			SharedData::materialForgeSettings.LegacyMetalInferenceMaximum) * metalStrength;
#			if defined(EMAT_ENVMAP)
		inferredMetallic *= complexMaterial ? 0.0f : 1.0f;
#			endif
		PhysicalMaterial::ApplyMetallic(
			physicalSurface,
			physicalSurface.BaseColor,
			inferredMetallic,
			MaterialForgeTuning::MetallicDiffuseSuppression());
#		endif
	}
	material.BaseColor = physicalSurface.BaseColor;
	material.Roughness = physicalSurface.Roughness;
	material.F0 = physicalSurface.F0;
	material.Metallic = physicalSurface.Metallic;
#		if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING))
	material.rimSoftLightColor = rimSoftLightColor.xyz;
#		endif
#		if defined(BACK_LIGHTING)
	material.backLightColor = backLightColor.xyz;
#		endif
#	endif  // MATERIAL_FORGE

#	if defined(PIXL_WINDOW_LIFE_ACTIVE)
	// PIXL WL2A GLASS MATERIAL RESPONSE
	[branch] if (pixlWindowSurface.glassWeight > 1.0e-4f)
	{
		float pixlGlassW = saturate(pixlWindowSurface.glassWeight);
		material.Roughness = lerp(material.Roughness, pixlWindowSurface.roughness, pixlGlassW);
		material.F0 = lerp(material.F0, max(material.F0, pixlWindowSurface.f0.xxx), pixlGlassW);
		material.Metallic *= 1.0f - pixlGlassW;
		material.BaseColor *= lerp(1.0f.xxx, pixlWindowSurface.transmission.xxx, pixlGlassW * 0.55f);
	}
#	endif
#	if defined(TREE_ANIM) && defined(FOLIAGE_DYNAMICS)
	if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0)
	{
		float foliageGloss =
			clamp(SharedData::foliageDynamicsSettings.Glossiness, 1.0f, 100.0f);
		float foliageRoughness =
			FoliageDynamics::GlossinessToPerceptualRoughness(foliageGloss);

		float foliageNormalVariance = max(
			dot(ddx_coarse(worldNormal), ddx_coarse(worldNormal)),
			dot(ddy_coarse(worldNormal), ddy_coarse(worldNormal)));
		foliageRoughness = clamp(
			sqrt(foliageRoughness * foliageRoughness +
				saturate(foliageNormalVariance *
					max(SharedData::foliageDynamicsSettings.SpecularAA, 0.0f)) * 0.25f),
			0.08f, 1.0f);

		float foliageSpecularStrength =
			clamp(SharedData::foliageDynamicsSettings.SpecularStrength, 0.0f, 2.0f);

		// Enhanced vegetation must never make an authored leaf material shinier.
		// Only add roughness/specular AA and cap existing dielectric F0 downward.
		// Wind can therefore be enabled independently without changing leaf gloss.
		material.Roughness = max(material.Roughness, max(foliageRoughness, 0.50f));
		material.F0 = min(material.F0, (0.04f * foliageSpecularStrength).xxx);
		material.Metallic = 0.0f;
	}
#	endif

#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	[branch] if (groundDeformationAmount > 0.0f)
	{
		// Snow and wet non-snow terrain can share one painted landscape transition.
		// Blend the two compression material models continuously instead of flipping
		// at 50% coverage, which would expose a visible roughness/albedo seam.
		float snowWeight = saturate(groundSnowMix);
		float mudWeight = 1.0f - snowWeight;

		// Treat the artist sliders as direct maximum darkening fractions.
		// The previous exponential snow curve only produced a small fraction of
		// the requested value, which made the control appear ineffective.
		float snowDarkening =
			saturate(
				groundDeformationAmount *
				SharedData::deformableGroundSettings.SnowCompactionDarkening *
				lerp(0.82f, 1.0f, groundDeformationFreshness));
		float mudDarkening =
			saturate(
				groundDeformationAmount *
				SharedData::deformableGroundSettings.MudDarkening *
				lerp(0.82f, 1.12f, groundDeformationFreshness));
		material.BaseColor *=
			(1.0f - snowDarkening * snowWeight) *
			(1.0f - mudDarkening * mudWeight);
		material.BaseColor *=
			1.0f + groundDeformationEdge * 0.055f * snowWeight;

		// PIXL_GR_13Z_TRAIL_MICRO_ALBEDO_V1
		float snowMicroAlbedo =
			(groundSnowMicroDetail - 0.5f) *
			0.105f *
			groundDeformationAmount *
			snowWeight;
		float mudMicroAlbedo =
			(groundMudMicroDetail - 0.5f) *
			0.135f *
			groundDeformationAmount *
			mudWeight;

		material.BaseColor *=
			max(
				1.0f +
					snowMicroAlbedo +
					mudMicroAlbedo,
				0.72f);

		// Thin water darkens the mud underneath it.
		material.BaseColor *=
			lerp(
				1.0f,
				0.70f,
				groundMudWaterAccumulation);

		float packedSnowRoughness =
			lerp(0.64f, 0.48f, groundDeformationFreshness);
		float oldRutRoughness =
			max(
				SharedData::deformableGroundSettings.MudRoughness,
				0.42f);
		float rutRoughness =
			lerp(
				oldRutRoughness,
				SharedData::deformableGroundSettings.MudRoughness,
				groundDeformationFreshness);
		float compressionRoughness =
			lerp(rutRoughness, packedSnowRoughness, snowWeight);
		float roughnessWeight =
			saturate(
				groundDeformationAmount *
				lerp(1.0f, 0.85f, snowWeight));
		material.Roughness =
			lerp(material.Roughness, compressionRoughness, roughnessWeight);

		// PIXL_GR_13Z_MICRO_ROUGHNESS_AND_WATER_V1
		float snowMicroRoughness =
			(groundSnowMicroDetail - 0.5f) *
			0.11f *
			groundDeformationAmount *
			snowWeight;
		float mudMicroRoughness =
			(groundMudMicroDetail - 0.5f) *
			0.16f *
			groundDeformationAmount *
			mudWeight;

		material.Roughness =
			saturate(
				material.Roughness +
				snowMicroRoughness +
				mudMicroRoughness);

		material.Roughness =
			lerp(
				material.Roughness,
				0.055f,
				groundMudWaterAccumulation);

		float3 snowF0 =
			lerp(0.030f, 0.040f, groundDeformationFreshness).xxx;

		// Reuse MudRoughness as an ABI-safe gloss/specular control. Low
		// roughness means wet/glossy mud and now also raises dielectric F0
		// modestly, so mud can read wetter than packed snow.
		float mudGlossSpecular =
			1.0f -
			smoothstep(
				0.08f,
				0.80f,
				SharedData::deformableGroundSettings.MudRoughness);
		float3 mudF0 =
			lerp(0.035f, 0.075f, mudGlossSpecular).xxx;

		float3 compressionF0 = lerp(mudF0, snowF0, snowWeight);
		float mudF0Weight =
			groundDeformationAmount *
			lerp(0.30f, 0.90f, mudGlossSpecular) *
			lerp(0.65f, 1.0f, groundDeformationFreshness);
		float snowF0Weight =
			groundDeformationAmount * 0.55f;
		float f0Weight =
			lerp(mudF0Weight, snowF0Weight, snowWeight);
		material.F0 = lerp(material.F0, compressionF0, saturate(f0Weight));

		// Thin accumulated water remains dielectric. Its low roughness above
		// provides the strong sharp reflection.
		material.F0 =
			lerp(
				material.F0,
				0.0205f.xxx,
				groundMudWaterAccumulation * 0.94f);
		material.Metallic =
			lerp(
				material.Metallic,
				0.0f,
				saturate(groundDeformationAmount * mudWeight));
	}
#	endif

#	if defined(SKIN) && defined(PIXL_SKIN)
	const float ExtraRoughness = BRDF::F_Schlick(0.04, saturate(dot(worldNormal.xyz, viewDirection))).x * SharedData::skinOpticsData.fuzzParams.w;
	material.Roughness = SharedData::skinOpticsData.skinParams.x;
	material.Roughness = saturate(SharedData::skinOpticsData.skinParams.x - SharedData::skinOpticsData.skinParams.z * material.Glossiness);
	material.RoughnessSecondary = SharedData::skinOpticsData.skinParams.y;
	if (skinRoughnessSet) {
		material.Roughness = skinRoughness * SharedData::skinOpticsData.physicalParams.x;
		material.RoughnessSecondary = skinRoughness * SharedData::skinOpticsData.physicalParams.y;
	}
	material.Roughness = min(1.0, material.Roughness + ExtraRoughness);
	material.RoughnessSecondary = min(1.0, material.RoughnessSecondary + ExtraRoughness);
	material.SecondarySpecIntensity = SharedData::skinOpticsData.skinParams2.x;
	material.Thickness = 1 - skinsk.x;
	material.SubsurfaceColor = skinsk.xyz;
	material.F0 = SharedData::skinOpticsData.skinParams2.zzz;
	material.AO = skinAO;
	material.Curvature = SkinOptics::CalculateCurvature(vertexNormal.xyz, input.WorldPosition.xyz);

	material.FuzzWeight = SharedData::skinOpticsData.fuzzParams.x;
	material.FuzzRoughness = SharedData::skinOpticsData.fuzzParams.y;
	material.FuzzColor = SharedData::skinOpticsData.fuzzParams.zzz;

	if (skinRoughnessSet) {
		material.F0 = 0.08f * skinSpecular * SharedData::skinOpticsData.physicalParams.z;
		material.FuzzWeight *= skinFuzzMask;
	}

	// DialogueFocus preserves the physical model and only tightens close-up response.
	// No albedo tint/exposure boost is applied, so faces do not visibly "switch modes".
	const float pixlDialogueSkin =
		pixlDialogueFocus * DialogueFocus::SkinQuality();
	const float pixlDialogueTissue =
		pixlDialogueFocus * DialogueFocus::TissueQuality();
	material.Roughness =
		lerp(material.Roughness, max(material.Roughness * 0.94f, 0.055f), pixlDialogueSkin);
	material.RoughnessSecondary =
		lerp(material.RoughnessSecondary, max(material.RoughnessSecondary * 0.92f, 0.045f), pixlDialogueSkin);
	material.SecondarySpecIntensity *= 1.0f + 0.07f * pixlDialogueSkin;
	material.Thickness = saturate(material.Thickness * (1.0f + 0.08f * pixlDialogueTissue));
	material.SubsurfaceColor *= 1.0f + 0.025f * pixlDialogueTissue;
	material.FuzzWeight *= 1.0f + 0.05f * pixlDialogueSkin;
#	endif  // PIXL_SKIN

#	if defined(EYE)
#		ifndef USE_PIXL_EYE_OPTICS
#			define USE_PIXL_EYE_OPTICS 1
#		endif
#		if USE_PIXL_EYE_OPTICS
	// Cornea behaves as a smooth dielectric shell (IOR approximately 1.376).
	// Preserve the authored iris/sclera colour while constraining the optical
	// lobe to plausible values and retaining the vanilla eye-parallax geometry.
	material.F0 = 0.0253f.xxx;
	material.Roughness = clamp(
		material.Roughness * 0.35f *
		lerp(
			1.0f,
			0.90f,
			pixlDialogueFocus *
			DialogueFocus::EyeQuality() *
			DialogueFocus::EyeReflectionQuality()),
		0.038f,
		0.22f);
	material.Metallic = 0.0f;
#		endif
#	endif

#	if defined(SKIN)
	material.BaseColor = max(material.BaseColor, EPSILON_SKIN_ALBEDO);
#	endif

#	if defined(STRAND_SHADING) && defined(HAIR)
	if (SharedData::strandShadingSettings.Enabled) {
		material.Shininess =
			SharedData::strandShadingSettings.HairGlossiness *
			(1.0f + 0.06f * pixlDialogueFocus * DialogueFocus::HairQuality());
		material.F0 = Hair::HairF0();
		if (SharedData::strandShadingSettings.HairMode == 1) {
			material.Roughness = 1;
		} else {
			material.Roughness = PhysicalMaterial::LegacyShininessToPerceptualRoughness(material.Shininess * 0.75);
		}
	}
#	endif

	bool dynamicCubemap = false;

#	if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	float envMask = EnvmapData.x * MaterialData.x;

	float3 pixlEnvironmentNormal = worldNormal.xyz;
#	if defined(EYE) && USE_PIXL_EYE_OPTICS
	pixlEnvironmentNormal = normalize(input.EyeNormal);
#	endif
	float viewNormalAngle = dot(pixlEnvironmentNormal, viewDirection);
	float3 envSamplingPoint = (viewNormalAngle * 2) * pixlEnvironmentNormal - viewDirection;

	if (envMask > 0.0) {
		if (EnvmapData.y) {
			envMask *= envMaskBase;
		} else {
			envMask *= glossiness;
		}
	}

	float3 envColor = 0.0;

	if (envMask > 0.0) {
#		if defined(WORLD_PROBES)
		uint2 envSize;
		TexEnvSampler.GetDimensions(envSize.x, envSize.y);

#			if defined(EMAT)
		if (envSize.x == 1 && envSize.y == 1 || complexMaterial) {
#			else
		if (envSize.x == 1 && envSize.y == 1) {
#			endif

			dynamicCubemap = true;

#			if defined(EMAT)
			if (!complexMaterial)
#			endif
			{
				// Dynamic Cubemap Creator sets this value to black, if it is anything but black it is wrong
				float3 envColorTest = TexEnvSampler.SampleLevel(SampEnvSampler, float3(0.0, 1.0, 0.0), 15).xyz;
				dynamicCubemap = all(envColorTest == 0.0);
			}

#			if defined(CREATOR)
			if (SharedData::worldProbeCaptureSettings.Enabled) {
				dynamicCubemap = true;
			}
#			endif

			if (dynamicCubemap) {
				float4 envColorBase = TexEnvSampler.SampleLevel(SampEnvSampler, float3(1.0, 0.0, 0.0), 15);

				if (envColorBase.a < 1.0) {
					material.F0 = Color::SkyrimGammaToLinear(envColorBase.rgb);
					material.Roughness = envColorBase.a;
				} else {
					material.F0 = 1.0;
					material.Roughness = 1.0 / 7.0;
				}

#			if defined(CREATOR)
				if (SharedData::worldProbeCaptureSettings.Enabled) {
					material.F0 = SharedData::worldProbeCaptureSettings.CubemapColor.rgb;
					material.Roughness = SharedData::worldProbeCaptureSettings.CubemapColor.a;
				}
#			endif

#			if defined(EMAT)
				float complexMaterialRoughness = 1.0 - complexMaterialColor.y;
				material.Roughness = lerp(material.Roughness, complexMaterialRoughness, complexMaterial);
				material.F0 = lerp(material.F0, complexSpecular, complexMaterial);
#			endif
			}
		}
#		endif

		if (!dynamicCubemap) {
			float3 envColorBase = Color::SkyrimGammaToLinear(TexEnvSampler.Sample(SampEnvSampler, envSamplingPoint).xyz);
			envColor = envColorBase.xyz * envMask;
		}
	}

#	endif  // defined (ENVMAP) || defined (MULTI_LAYER_PARALLAX) || defined(EYE)

#	if defined(EYE) && USE_PIXL_EYE_OPTICS
	// Reassert after dynamic-cubemap metadata: the cornea remains a dielectric.
	material.F0 = EyeRendering::CorneaF0().xxx;
	material.Roughness = max(
		0.038f,
		EyeRendering::CorneaRoughness(uv) *
		lerp(
			1.0f,
			0.90f,
			pixlDialogueFocus *
			DialogueFocus::EyeQuality() *
			DialogueFocus::EyeReflectionQuality()));
	material.Metallic = 0.0f;

	// A tiny close-up reflection lift restores the wet corneal read without changing F0.
	envColor *=
		1.0f +
		0.035f * pixlDialogueFocus *
		DialogueFocus::EyeQuality() *
		DialogueFocus::EyeReflectionQuality();
#	endif

	// Geometric specular anti-aliasing: normal-map/parallax detail can remain high
	// frequency after the color and roughness textures have selected coarser mips.
	// Filtering alpha-squared by shading-normal variance suppresses distant shimmer
	// without blurring the normal used by diffuse lighting or the G-buffer.
	[branch] if (SharedData::materialForgeSettings.EnableSpecularAA != 0)
	{
		material.Roughness = BRDF::FilterRoughnessByNormalVariance(
			material.Roughness,
			worldNormal.xyz,
			SharedData::materialForgeSettings.SpecularAAStrength,
			SharedData::materialForgeSettings.SpecularAAVarianceClamp);
#	if defined(SKIN) && defined(PIXL_SKIN)
		material.RoughnessSecondary = BRDF::FilterRoughnessByNormalVariance(
			material.RoughnessSecondary,
			worldNormal.xyz,
			SharedData::materialForgeSettings.SpecularAAStrength,
			SharedData::materialForgeSettings.SpecularAAVarianceClamp);
#	endif
#	if defined(MATERIAL_FORGE) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
			material.CoatRoughness = BRDF::FilterRoughnessByNormalVariance(
				material.CoatRoughness,
				coatWorldNormal,
				SharedData::materialForgeSettings.SpecularAAStrength,
				SharedData::materialForgeSettings.SpecularAAVarianceClamp);
#	endif
	}

	float porosity = 1.0;

#	if defined(SKY_BOUNCE)
	float3 positionMSSkyBounce = input.WorldPosition.xyz;
#		if defined(SKY_BOUNCE_SHADOW_VIS)
	float skylightingShadowVisibility = 1.0;
#		endif
#		if defined(DEFERRED)
	sh2 skyBounceSH = SkyBounce::Sample(positionMSSkyBounce, worldNormal
#			if defined(SKY_BOUNCE_SHADOW_VIS)
		, skylightingShadowVisibility
#			endif
	);
#		else
	sh2 skyBounceSH = inWorld ? SkyBounce::Sample(positionMSSkyBounce, worldNormal
#			if defined(SKY_BOUNCE_SHADOW_VIS)
		, skylightingShadowVisibility
#			endif
	) : SkyBounce::UNIT_SH;
#		endif
#	endif

#	if defined(RAIN_RESPONSE) || defined(WATER_OPTICS)
	float4 waterData = SharedData::GetWaterData(input.WorldPosition.xyz);
#	endif
#	if defined(RAIN_RESPONSE)
	float waterHeight = waterData.w;
	float waterRoughnessSpecular = 1;

	// Initialize wetness parameters
	float wetness = 0.0;
	float3 wetnessNormal = vertexNormal.xyz;

	// Calculate shore wetness factors
	float wetnessDistToWater = abs(input.WorldPosition.z - waterHeight);
	float shoreFactor = saturate(1.0 - (wetnessDistToWater / max(SharedData::rainResponseSettings.ShoreRange, 1e-3f)));
	float shoreFactorAlbedo = (input.WorldPosition.z < waterHeight) ? 1.0 : shoreFactor;

	// Calculate wetness angle and occlusion
	float minWetnessValue = SharedData::rainResponseSettings.MinRainWetness;
	float minWetnessAngle = saturate(max(minWetnessValue, vertexNormal.z));
#		if defined(SKY_BOUNCE)
	float wetnessOcclusion = inWorld ? saturate(SphericalHarmonics::Unproject(skyBounceSH, float3(0, 0, 1))) : 0.0;
#		else
	float wetnessOcclusion = inWorld;
#		endif
	float flatnessAmount = smoothstep(SharedData::rainResponseSettings.PuddleMaxAngle, 1.0, minWetnessAngle);
	// Calculate raindrop effects
	float4 raindropInfo = float4(0, 0, 1, 0);
	bool shouldCalculateRaindrops = (worldNormal.z > 0.0) &&
	                                (SharedData::rainResponseSettings.Raining > 0.0) &&
	                                (SharedData::rainResponseSettings.EnableRaindropFx) &&
	                                (wetnessOcclusion > 0.05);

	if (shouldCalculateRaindrops) {
#		if defined(SKINNED)
		float3 ripplePosition = input.ModelPosition.xyz;
#		elif defined(DEFERRED)
		float3 ripplePosition = input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
#		else
		float3 ripplePosition = !FrameBuffer::FrameParams.y ? input.ModelPosition.xyz : input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
#		endif
		raindropInfo = RainResponse::GetRainDrops(ripplePosition, SharedData::rainResponseSettings.Time, wetnessNormal,
			flatnessAmount * smoothstep(0.05f, 0.75f, wetnessOcclusion));
	}

	// Calculate different wetness types
	float rainWetness = SharedData::rainResponseSettings.Wetness * minWetnessAngle * SharedData::rainResponseSettings.MaxRainWetness;
	rainWetness = max(rainWetness, raindropInfo.w);

#		if defined(SKIN) || defined(HAIR)
	rainWetness = SharedData::rainResponseSettings.SkinWetness * SharedData::rainResponseSettings.Wetness;
#		endif

#		if defined(PIXL_SKIN) && !defined(SKIN)
	if (skinEnabled) {
		float2 dynamicWetness = SkinOptics::GetWetness(input.WorldPosition.z + FrameBuffer::CameraPosAdjust.z, worldNormal.xyz);
#			if defined(MATERIAL_FORGE)
		dynamicWetness.x = lerp(dynamicWetness.x, 0.0f, material.Metallic);
#			endif
		float dynamicWetnessValue = clamp(dynamicWetness.x + dynamicWetness.y, 0.f, 2.f);
#			if defined(HAIR)
		dynamicWetnessValue = min(SharedData::skinOpticsData.skinParams2.y + dynamicWetnessValue, 2.0f);
#			endif
		rainWetness += min(dynamicWetnessValue, 1.f);
	}
#		endif
	float shoreWetness = shoreFactor * SharedData::rainResponseSettings.MaxShoreWetness;
	wetness = max(shoreWetness, rainWetness);

	// Calculate puddle effects
	float puddleWetness = SharedData::rainResponseSettings.PuddleWetness * minWetnessAngle;
	float puddle = wetness;

#		if !defined(SKINNED) && !(defined(SKIN) && defined(PIXL_SKIN))
	if (wetness > 0.0 || puddleWetness > 0.0) {
		float3 puddleCoords = ((input.WorldPosition.xyz + FrameBuffer::CameraPosAdjust.xyz) * 0.5 + 0.5) * 0.01 / max(SharedData::rainResponseSettings.PuddleRadius, 1e-3f);
		puddle = Random::perlinNoise(puddleCoords) * 0.5 + 0.5;
		puddle = puddle * ((minWetnessAngle / SharedData::rainResponseSettings.PuddleMaxAngle) * SharedData::rainResponseSettings.MaxPuddleWetness * 0.25) + 0.5;
		puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
	}
#		endif

	// Apply occlusion and distance factors
	puddle *= saturate(wetnessOcclusion * 2.0) * nearFactor;
	wetnessNormal = lerp(worldNormal.xyz, wetnessNormal, saturate(puddle));

	// Calculate wetness glossiness factors
	float wetnessGlossinessAlbedo = max(puddle, shoreFactorAlbedo * SharedData::rainResponseSettings.MaxShoreWetness);
	wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;

	float wetnessGlossinessSpecular = puddle;
	if (input.WorldPosition.z < waterHeight) {
		wetnessGlossinessSpecular *= shoreFactor;
	}

	// Update flatness and normal calculations
	flatnessAmount *= smoothstep(SharedData::rainResponseSettings.PuddleMinWetness, 1.0, wetnessGlossinessSpecular);

	// Apply ripple normal effects
	float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(flatnessAmount, 1.0, 0.5)));
	wetnessNormal = ReorientNormal(rippleNormal, wetnessNormal);

#		if defined(SKIN) && defined(PIXL_SKIN)
	if (skinEnabled && (skinWetness > 0.0f)) {
		wetnessNormal = skinWetNormal;
		wetnessGlossinessSpecular = saturate(max(wetnessGlossinessSpecular, skinWetness));
	}
#		endif

	// Minimum roughness prevents an extreme retroreflective peak (NdotH→1) for near-zero
	// roughness puddles. Real water has ripples and surface tension that keep it from being
	// optically perfect; the ripple normal map adds micro-variation but GGX still peaks
	// sharply without this floor.
	static const float wetnessMinPuddleRoughness = 0.05;
	waterRoughnessSpecular = max(saturate(1.0 - wetnessGlossinessSpecular), wetnessMinPuddleRoughness);
#		if USE_PIXL_WETNESS_RESPONSE
	waterRoughnessSpecular = BRDF::FilterRoughnessByNormalVariance(waterRoughnessSpecular, wetnessNormal, 0.75f, 0.18f);
#		endif
#	endif

	float llDirLightMult = SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear && (inWorld || inReflection) && !SharedData::InInterior ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightCoreSettings.isDirLightLinear) * llDirLightMult;

#	if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		dirLightColor *= Atmosphere::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#	endif

#	if defined(WATER_OPTICS)
	dirLightColor *= WaterOptics::ComputeCaustics(waterData, input.WorldPosition.xyz);
#	endif

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (inWorld || inReflection)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirLightAngle = dot(worldNormal.xyz, DirLightDirection.xyz);

	float3 refractedDirLightDirection = DirLightDirection;
#	if defined(MATERIAL_FORGE) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		if (dot(DirLightDirection, coatWorldNormal) > 0)
			refractedDirLightDirection = -refract(-DirLightDirection, coatWorldNormal, eta);
	}
#	endif

	float dirSoftShadow = 1.0;
	float dirVSMDetailedShadow = 1.0;

#	if defined(VOLUME_OCCLUSION) && !(defined(DEFERRED) && defined(SKY_BOUNCE_SHADOW_VIS))
	// 13BG: the raised shell evaluates its displaced receiver once below using
	// Utility's exact cascade data for the detailed term plus VSM for soft shadow.
	if (!pixlGroundRaisedShell && inWorld && !inReflection && ShadowSampling::HasDirectionalShadows())
#		if !defined(SKY_BOUNCE_SHADOW_VIS)
		dirSoftShadow =
#		endif
		ShadowSampling::GetLightingShadow(input.WorldPosition.xyz, dirVSMDetailedShadow);
#	endif

	float dirDetailedShadow = 1.0;

	// PIXL_GR_13BE_WORLDSPACE_DIRECTIONAL_SHADOW_V1 + 13BG_EXACT_UTILITY_DETAIL
	// The raised GroundResponse receiver did not exist when Skyrim produced the
	// screen-space shadow mask. Re-evaluate the directional shadow against the
	// actual displaced world-space receiver instead of sampling shadowColor.x.
	if (pixlGroundRaisedShell) {
		if (inWorld && !inReflection && ShadowSampling::HasDirectionalShadows()) {
			float groundWorldDetailedShadow = 1.0f;
			float groundWorldSoftShadow =
				ShadowSampling::GetGroundResponseLightingShadow(
					input.WorldPosition.xyz,
					input.Position.xy,
					groundWorldDetailedShadow);
			dirDetailedShadow = groundWorldDetailedShadow;
			dirSoftShadow = groundWorldSoftShadow;
		}
	} else if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow) && (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::ShadowDir)) {
		dirDetailedShadow *= shadowColor.x;

#	if !defined(VOLUME_OCCLUSION) && !defined(SKY_BOUNCE_SHADOW_VIS)
		dirSoftShadow = dirDetailedShadow;
#	endif
	}
#	if !defined(DEFERRED)
	else {
		dirDetailedShadow = dirVSMDetailedShadow;
	}
#	endif

#	if defined(CONTACT_SHADOWS) && defined(DEFERRED)
	if (!pixlGroundRaisedShell &&
		!SharedData::InInterior &&
		dirLightAngle >= 0.0)
	{
		float pixlContactShadow = ContactShadows::GetScreenSpaceShadow(
			input.Position.xyz, screenUV, screenNoise);

		// Do not recompile/rerun the contact-shadow ray marcher for dialogue.
		// Instead, use a small actor-local contrast refinement on the already-computed result.
		const float pixlDialogueContact =
			pixlDialogueFocus * DialogueFocus::ContactShadowQuality();
		pixlContactShadow =
			lerp(
				pixlContactShadow,
				pixlContactShadow * pixlContactShadow,
				0.16f * pixlDialogueContact);
		dirDetailedShadow *= pixlContactShadow;
	}
#	endif

#	if defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
	[branch] if (inWorld && SharedData::materialLayerSettings.EnableShadows)
	{
		float3 dirLightDirectionTS = mul(refractedDirLightDirection, tbn).xyz;
#		if defined(LANDSCAPE)
		if (LANDSCAPE_PARALLAX_ENABLED && dirLightAngle > 0.0) {
			if (hasCachedDirectionalTerrainParallaxShadow) {
				dirDetailedShadow *= cachedDirectionalTerrainParallaxShadow;
			} else {
				float sh0;
				bool hasShadowBase = COMPUTE_TERRAIN_SHADOW_BASE(sh0);
				if (hasShadowBase)
					dirDetailedShadow *= EVAL_TERRAIN_DIR_SHADOW(sh0, dirLightDirectionTS);
			}
		}
#		elif defined(PARALLAX)
		[branch] if (SharedData::materialLayerSettings.EnableParallax)
			dirDetailedShadow *= MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#		elif defined(EMAT_ENVMAP)
		[branch] if (complexMaterialParallax)
			dirDetailedShadow *= MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, parallaxShadowQuality, screenNoise, displacementParams);
#		elif defined(MATERIAL_FORGE) && !defined(LODLANDSCAPE) && !defined(FACEGEN)
		[branch] if (PBRParallax)
			dirDetailedShadow *= MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, dirLightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#		endif  // LANDSCAPE
	}
#	endif  // defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

#	if defined(PIXL_AUTO_PARALLAX) && !defined(MODELSPACENORMALS)
	[branch] if (MaterialLayersTuning::AutoPOMSelfShadowsEnabled() &&
		autoParallaxApplied && inWorld &&
		SharedData::materialLayerSettings.EnableShadows && dirLightAngle > 0.0f)
	{
		float3 autoDirLightTS = normalize(mul(refractedDirLightDirection, tbn).xyz);
		dirDetailedShadow *= MaterialLayers::GetAutoParallaxSoftShadowMultiplier(
			uv, autoParallaxMipLevel, autoDirLightTS, autoParallaxBaseHeight,
			autoParallaxStrength, TexColorSampler, SampColorSampler, screenNoise);
	}
#	endif

#	if defined(STRAND_SHADING) && defined(HAIR)
	if (SharedData::strandShadingSettings.Enabled) {
		vertexNormal.xyz = worldNormal.xyz;
		worldNormal.xyz = hairT;
	}
#	endif

#	if defined(SKY_BOUNCE_SHADOW_VIS)
	// Normal geometry keeps SkyBounce visibility. The raised GroundResponse shell
	// already has a receiver-correct world-space soft shadow from 13BE above.
	if (!pixlGroundRaisedShell)
		dirSoftShadow = skylightingShadowVisibility;
#	endif

	float3 diffuseColor = 0.0.xxx;
	float3 specularColor = 0.0.xxx;
	float3 transmissionColor = 0.0.xxx;

	float3 lightsDiffuseColor = 0.0.xxx;
	float3 coatLightsDiffuseColor = 0.0.xxx;
	float3 lightsSpecularColor = 0.0.xxx;
#	if !defined(MATERIAL_FORGE)
	float3 legacyPhysicalDelta = 0.0.xxx;
	float legacyPhysicalCoverage = 0.0;
#	endif

	float3 lodLandDiffuseColor = 0;

	// Directiontal Lighting
	DirectContext dirLightContext;
	DirectLightingOutput dirLightOutput;
#	if defined(MATERIAL_FORGE)
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedDirLightDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#	else
	dirLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, DirLightDirection, dirLightColor, dirDetailedShadow, dirSoftShadow);
#		if defined(HAIR) && defined(STRAND_SHADING)
	if (SharedData::strandShadingSettings.Enabled) {
		float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, DirLightDirection, screenNoise);
		// Close-up hair receives slightly better strand separation without extra ray samples.
		const float pixlDialogueHairShadow =
			0.12f * pixlDialogueFocus * DialogueFocus::HairQuality();
		hairShadow = lerp(hairShadow, hairShadow * hairShadow, pixlDialogueHairShadow);
		dirLightContext.hairShadow = hairShadow;
	}
#		endif
#	endif

	float2 uvOriginal_ddx = ddx(uvOriginal);
	float2 uvOriginal_ddy = ddy(uvOriginal);
	float3 dirLegacyPhysicalDelta;
	float dirLegacyPhysicalApplied;
	PhysicalLighting::EvaluateDirect(
		dirLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, inWorld || inReflection, dirLightOutput,
		dirLegacyPhysicalDelta, dirLegacyPhysicalApplied);
#	if !defined(MATERIAL_FORGE)
	legacyPhysicalDelta += dirLegacyPhysicalDelta;
	legacyPhysicalCoverage = max(legacyPhysicalCoverage, dirLegacyPhysicalApplied);
#	endif
#	if defined(RAIN_RESPONSE)
	if (waterRoughnessSpecular < 1)
		EvaluateWetnessLighting(wetnessNormal, dirLightContext, waterRoughnessSpecular, dirLightOutput);
#	endif

	lightsDiffuseColor += dirLightOutput.diffuse;
	lightsSpecularColor += dirLightOutput.specular;
#	if defined(MATERIAL_FORGE)
	coatLightsDiffuseColor += dirLightOutput.coatDiffuse;
#		if defined(LOD_LAND_BLEND)
	lodLandDiffuseColor += dirLightColor / Math::PI * saturate(dirLightAngle) * dirDetailedShadow;
#		endif
#	endif
	transmissionColor += dirLightOutput.transmission;
#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	// PIXL_GR_13BI_SHADOW_AWARE_SNOW_BACKSCATTER_V1
	// This is deliberately injected into transmission rather than emissive light.
	// The same corrected 13BE/13BH directional shadow terms used by the raised hull
	// gate the effect, so actors/objects/terrain can still occlude it correctly.
	[branch] if (groundSnowBackscatterWeight > 1.0e-4f) {
		float groundSnowShadow =
			min(
				dirDetailedShadow,
				dirSoftShadow);

		float groundSnowBackLight =
			smoothstep(
				0.08f,
				0.82f,
				saturate(-dirLightAngle));

		float groundSnowViewGrazing =
			1.0f -
			smoothstep(
				0.20f,
				0.88f,
				saturate(
					abs(
						dot(
							worldNormal.xyz,
							viewDirection.xyz))));

		float groundSnowScatter =
			groundSnowBackscatterWeight *
			groundSnowBackLight *
			lerp(
				0.72f,
				1.10f,
				groundSnowViewGrazing) *
			groundSnowShadow;

		const float3 groundSnowScatterTint =
			float3(0.74f, 0.86f, 1.00f);

		transmissionColor +=
			dirLightColor *
			groundSnowScatterTint *
			groundSnowScatter *
			0.095f;
	}
#	endif
#	if defined(TREE_ANIM) && defined(FOLIAGE_DYNAMICS)
	if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0) {
		float treeShadow = min(dirDetailedShadow, dirSoftShadow);
		transmissionColor += FoliageDynamics::GetTransmissionInput(
			DirLightDirection.xyz, viewDirection, worldNormal.xyz, dirLightColor, material.BaseColor,
			SharedData::foliageDynamicsSettings.TissueDiffusionAmount) * treeShadow;
		transmissionColor += FoliageDynamics::GetFoliageDiffuseWrap(
			DirLightDirection.xyz, worldNormal.xyz, dirLightColor, material.BaseColor, treeShadow);
	}
#	endif

#	if !defined(LOD)
#		if !defined(RADIANT_GRID)
	uint2 localContactLightIndices = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
#			if defined(CONTACT_SHADOWS) && defined(DEFERRED)
	const uint localContactLightCount = clamp(SharedData::materialForgeSettings.LocalContactShadowLightCount, 1u, 2u);
	const bool localContactShadowsActive =
		SharedData::materialForgeSettings.EnableLocalContactShadows != 0 &&
		SharedData::materialForgeSettings.LocalContactShadowStrength > 0.0f &&
		SharedData::materialForgeSettings.LocalContactShadowLength > 4.0f;
	if (localContactShadowsActive) {
		float2 bestContactScores = float2(-1.0f, -1.0f);
		[unroll] for (uint candidateIndex = 0; candidateIndex < 7; ++candidateIndex) {
			if (candidateIndex >= numLights)
				break;
			float3 candidateDirection = PointLightPosition[candidateIndex].xyz - input.WorldPosition.xyz;
			float candidateDistance = length(candidateDirection);
			float candidateAttenuation = GetLocalLightAttenuation(candidateDistance, PointLightPosition[candidateIndex].w, 0.0f, 0.0f);
			float candidateScore = Color::RGBToLuminance(Color::PointLight(PointLightColor[candidateIndex].xyz)) * candidateAttenuation;
			if (candidateScore > bestContactScores.x) {
				bestContactScores.y = bestContactScores.x;
				localContactLightIndices.y = localContactLightIndices.x;
				bestContactScores.x = candidateScore;
				localContactLightIndices.x = candidateIndex;
			} else if (candidateScore > bestContactScores.y) {
				bestContactScores.y = candidateScore;
				localContactLightIndices.y = candidateIndex;
			}
		}
	}
#			endif
	[loop] for (uint lightIndex = 0; lightIndex < numLights; lightIndex++)
	{
		float3 lightDirection = PointLightPosition[lightIndex].xyz - input.WorldPosition.xyz;
		float lightDistSq = dot(lightDirection, lightDirection);
		float lightDist = sqrt(max(lightDistSq, EPSILON_LENGTH_SQ));
		float intensityMultiplier;
		if (inWorld || inReflection) {
			intensityMultiplier = GetLocalLightAttenuation(lightDist, PointLightPosition[lightIndex].w, 0.0f, 0.0f);
		} else {
			float normalizedMenuLightDistance = saturate(lightDist / max(PointLightPosition[lightIndex].w, 1.0f));
			intensityMultiplier = 1.0f - normalizedMenuLightDistance * normalizedMenuLightDistance;
		}
		if (intensityMultiplier < 1e-5f)
			continue;
		float3 lightColor = Color::PointLight(PointLightColor[lightIndex].xyz) * intensityMultiplier;
		float lightShadow = 1.f;
		if (!pixlGroundRaisedShell &&
			(Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow)) {
			if (lightIndex < numShadowLights) {
				lightShadow *= shadowColor[ShadowLightMaskSelect[lightIndex]];
			}
		}

		float3 normalizedLightDirection = lightDirection * rcp(lightDist);
#			if defined(CONTACT_SHADOWS) && defined(DEFERRED)
		bool useLocalContactShadow = localContactShadowsActive && (
			lightIndex == localContactLightIndices.x ||
			(localContactLightCount > 1u && lightIndex == localContactLightIndices.y));
		[branch] if (!pixlGroundRaisedShell &&
			useLocalContactShadow &&
			dot(worldNormal.xyz, normalizedLightDirection) > 0.0f)
		{
			lightShadow *= ContactShadows::GetLocalContactShadow(
				input.WorldPosition.xyz, worldNormal.xyz, normalizedLightDirection, lightDist);
		}
#			endif

		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
		float3 pointLegacyPhysicalDelta;
		float pointLegacyPhysicalApplied;
#			if defined(MATERIAL_FORGE)
		{
			float3 refractedLightDirection = normalizedLightDirection;
#				if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
			{
				if (dot(normalizedLightDirection, coatWorldNormal) > 0)
					refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
			}
#				endif
			pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
		}
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, lightShadow, lightShadow);
#				if defined(HAIR) && defined(STRAND_SHADING)
		if (SharedData::strandShadingSettings.Enabled) {
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise);
			pointLightContext.hairShadow = hairShadow;
		}
#				endif
#			endif
		PhysicalLighting::EvaluateDirect(
			pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, inWorld || inReflection, pointLightOutput,
			pointLegacyPhysicalDelta, pointLegacyPhysicalApplied);
#			if !defined(MATERIAL_FORGE)
		legacyPhysicalDelta += pointLegacyPhysicalDelta;
		legacyPhysicalCoverage = max(legacyPhysicalCoverage, pointLegacyPhysicalApplied);
#			endif
#			if defined(RAIN_RESPONSE)
		if (waterRoughnessSpecular < 1)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif
		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(MATERIAL_FORGE)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}

#		else

	uint numClusteredLights = 0;
	uint totalLightCount = RadiantGrid::NumStrictLights;
	uint clusterIndex = 0;
	uint lightOffset = 0;
	uint2 clusteredContactCandidates = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
	if (inWorld && RadiantGrid::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		RadiantGrid::LightGrid pixelLightGrid = RadiantGrid::lightGrid[clusterIndex];
		numClusteredLights = pixelLightGrid.lightCount;
		totalLightCount += numClusteredLights;
		lightOffset = pixelLightGrid.offset;
		clusteredContactCandidates = uint2(pixelLightGrid.contactLight0, pixelLightGrid.contactLight1);
	}

	uint2 localContactLightIndices = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
#			if defined(CONTACT_SHADOWS) && defined(DEFERRED)
	const uint localContactLightCount = clamp(SharedData::materialForgeSettings.LocalContactShadowLightCount, 1u, 2u);
	const bool localContactShadowsActive =
		SharedData::materialForgeSettings.EnableLocalContactShadows != 0 &&
		SharedData::materialForgeSettings.LocalContactShadowStrength > 0.0f &&
		SharedData::materialForgeSettings.LocalContactShadowLength > 4.0f;
	if (localContactShadowsActive) {
		float2 bestContactScores = float2(-1.0f, -1.0f);
		uint contactCandidateCount = RadiantGrid::NumStrictLights + 2u;
		[loop] for (uint candidateIndex = 0; candidateIndex < contactCandidateCount; ++candidateIndex) {
			RadiantGrid::Light candidateLight;
			uint candidateKey;
			if (candidateIndex < RadiantGrid::NumStrictLights) {
				candidateLight = RadiantGrid::StrictLights[candidateIndex];
				candidateKey = candidateIndex;
			} else {
				uint clusteredCandidateIndex = clusteredContactCandidates[candidateIndex - RadiantGrid::NumStrictLights];
				if (clusteredCandidateIndex == 0xFFFFFFFFu)
					continue;
				candidateLight = RadiantGrid::lights[clusteredCandidateIndex];
				if (RadiantGrid::IsLightIgnored(candidateLight))
					continue;
				candidateKey = clusteredCandidateIndex | 0x80000000u;
			}
			float3 candidateDirection = candidateLight.positionWS.xyz - input.WorldPosition.xyz;
			float candidateDistance = length(candidateDirection);
			float candidateAttenuation = GetLocalLightAttenuation(
				candidateDistance, candidateLight.radius, candidateLight.fadeZone, candidateLight.sizeBias);
			float candidateScore = Color::RGBToLuminance(Color::PointLight(
				candidateLight.color.xyz, (candidateLight.lightFlags & RadiantGrid::LightFlags::Linear) != 0)) *
				candidateAttenuation * candidateLight.fade;
			if (candidateScore > bestContactScores.x) {
				bestContactScores.y = bestContactScores.x;
				localContactLightIndices.y = localContactLightIndices.x;
				bestContactScores.x = candidateScore;
				localContactLightIndices.x = candidateKey;
			} else if (candidateScore > bestContactScores.y) {
				bestContactScores.y = candidateScore;
				localContactLightIndices.y = candidateKey;
			}
		}
	}
#			endif

	[loop] for (uint lightIndex = 0; lightIndex < totalLightCount; lightIndex++)
	{
		RadiantGrid::Light light;
		uint lightContactKey;
		if (lightIndex < RadiantGrid::NumStrictLights) {
			light = RadiantGrid::StrictLights[lightIndex];
			lightContactKey = lightIndex;
		} else {
			uint clusteredLightIndex = RadiantGrid::lightList[lightOffset + (lightIndex - RadiantGrid::NumStrictLights)];
			light = RadiantGrid::lights[clusteredLightIndex];
			lightContactKey = clusteredLightIndex | 0x80000000u;

			if (RadiantGrid::IsLightIgnored(light))
				continue;
		}

		float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
		float lightDistSq = dot(lightDirection, lightDirection);
		float lightDist = sqrt(max(lightDistSq, EPSILON_LENGTH_SQ));

		float intensityMultiplier;
		if (!(inWorld || inReflection)) {
			float normalizedMenuLightDistance = saturate(lightDist / max(light.radius, 1.0f));
			intensityMultiplier = 1.0f - normalizedMenuLightDistance * normalizedMenuLightDistance;
		} else if (SharedData::materialForgeSettings.EnablePhysicalLocalLightFalloff != 0) {
			intensityMultiplier = GetLocalLightAttenuation(lightDist, light.radius, light.fadeZone, light.sizeBias);
		} else {
#			if defined(NATURAL_LIGHTING)
			intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
#			else
			intensityMultiplier = GetLocalLightAttenuation(lightDist, light.radius, 0.0f, light.sizeBias);
#			endif
		}
		if (intensityMultiplier < 1e-5f)
			continue;

		const bool isPointLightLinear = light.lightFlags & RadiantGrid::LightFlags::Linear;
		float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * light.fade;
		float lightShadow = 1.0;

		float shadowComponent = 1.0;
		if (!pixlGroundRaisedShell &&
			(Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow)) {
			if (light.lightFlags & RadiantGrid::LightFlags::Shadow) {
				shadowComponent = shadowColor[light.shadowLightIndex];
				lightShadow *= shadowComponent;
			}
		}

		float3 normalizedLightDirection = lightDirection * rcp(lightDist);
		float lightAngle = dot(worldNormal.xyz, normalizedLightDirection.xyz);
#			if defined(CONTACT_SHADOWS) && defined(DEFERRED)
		bool useLocalContactShadow = localContactShadowsActive && (
			lightContactKey == localContactLightIndices.x ||
			(localContactLightCount > 1u && lightContactKey == localContactLightIndices.y));
		[branch] if (!pixlGroundRaisedShell &&
			useLocalContactShadow &&
			lightAngle > 0.0f)
		{
			lightShadow *= ContactShadows::GetLocalContactShadow(
				input.WorldPosition.xyz, worldNormal.xyz, normalizedLightDirection, lightDist);
		}
#			endif

		float3 refractedLightDirection = normalizedLightDirection;
#			if defined(MATERIAL_FORGE) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
		{
			if (dot(normalizedLightDirection, coatWorldNormal) > 0)
				refractedLightDirection = -refract(-normalizedLightDirection, coatWorldNormal, eta);
		}
#			endif

		float parallaxShadow = 1;

#			if defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))
		[branch] if (
			SharedData::materialLayerSettings.EnableShadows &&
			!(light.lightFlags & RadiantGrid::LightFlags::Simple) &&
			lightAngle > 0.0 &&
			shadowComponent != 0.0)
		{
			float3 lightDirectionTS = normalize(mul(refractedLightDirection, tbn).xyz);
#				if defined(PARALLAX)
			[branch] if (SharedData::materialLayerSettings.EnableParallax)
				parallaxShadow = MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(LANDSCAPE)
			[branch] if (hasTerrainParallaxShadow)
				parallaxShadow = MaterialLayers::GetParallaxSoftShadowMultiplierTerrain(input, uv, terrainShadowMipLevels, lightDirectionTS, sh0, terrainDirectionalShadowQuality, screenNoise, displacementParams, sharedOffset);
#				elif defined(EMAT_ENVMAP)
			[branch] if (complexMaterialParallax)
				parallaxShadow = MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexEnvMaskSampler, SampEnvMaskSampler, 3, parallaxShadowQuality, screenNoise, displacementParams);
#				elif defined(MATERIAL_FORGE) && !defined(LODLANDSCAPE) && !defined(FACEGEN)
			[branch] if (PBRParallax)
				parallaxShadow = MaterialLayers::GetParallaxSoftShadowMultiplier(uv, mipLevel, lightDirectionTS, sh0, TexParallaxSampler, SampParallaxSampler, 0, parallaxShadowQuality, screenNoise, displacementParams);
#				endif
		}
#			endif  // defined(EMAT) && (defined(SKINNED) || !defined(MODELSPACENORMALS))

		// Local lights are numerous and spatially compact. Full-strength POM horizon
		// occlusion on every light can multiply into black, texture-shaped islands.
		// Keep the relief cue, but reserve the strongest POM self-shadow for the
		// directional light where the horizon is stable and coherent.
		parallaxShadow = lerp(1.0f, parallaxShadow, 0.72f);

		DirectContext pointLightContext;
		DirectLightingOutput pointLightOutput;
		float3 pointLegacyPhysicalDelta;
		float pointLegacyPhysicalApplied;
		float pointLightShadow = lightShadow * parallaxShadow;
#			if defined(MATERIAL_FORGE)
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, coatWorldNormal, vertexNormal.xyz, refractedViewDirection, viewDirection, refractedLightDirection, normalizedLightDirection, lightColor, pointLightShadow, pointLightShadow);
#			else
		pointLightContext = CreateDirectLightingContext(worldNormal.xyz, vertexNormal.xyz, viewDirection, normalizedLightDirection, lightColor, pointLightShadow, pointLightShadow);
#				if defined(HAIR) && defined(STRAND_SHADING)
		if (SharedData::strandShadingSettings.Enabled) {
			float hairShadow = Hair::HairSelfShadow(input.WorldPosition.xyz, normalizedLightDirection, screenNoise);
			pointLightContext.hairShadow = hairShadow;
		}
#				endif
#			endif
		PhysicalLighting::EvaluateDirect(
			pointLightContext, material, tbnTr, uvOriginal, uvOriginal_ddx, uvOriginal_ddy, inWorld || inReflection, pointLightOutput,
			pointLegacyPhysicalDelta, pointLegacyPhysicalApplied);
#			if !defined(MATERIAL_FORGE)
		legacyPhysicalDelta += pointLegacyPhysicalDelta;
		legacyPhysicalCoverage = max(legacyPhysicalCoverage, pointLegacyPhysicalApplied);
#			endif
#			if defined(RAIN_RESPONSE)
		if (waterRoughnessSpecular < 1)
			EvaluateWetnessLighting(wetnessNormal, pointLightContext, waterRoughnessSpecular, pointLightOutput);
#			endif

		lightsDiffuseColor += pointLightOutput.diffuse;
		lightsSpecularColor += pointLightOutput.specular;
#			if defined(MATERIAL_FORGE)
		coatLightsDiffuseColor += pointLightOutput.coatDiffuse;
#			endif
		transmissionColor += pointLightOutput.transmission;
	}
#		endif
#	endif

	diffuseColor += lightsDiffuseColor;
	specularColor += lightsSpecularColor;

#	if !defined(LANDSCAPE)
	if (Permutation::PixelShaderDescriptor & Permutation::LightingFlags::CharacterLight) {
		float charLightMul = saturate(dot(viewDirection, worldNormal.xyz)) * CharacterLightParams.x + CharacterLightParams.y * saturate(dot(float2(0.164398998, -0.986393988), worldNormal.yz));
		float charLightColor = min(CharacterLightParams.w, max(0, CharacterLightParams.z * TexCharacterLightProjNoiseSampler.Sample(SampCharacterLightProjNoiseSampler, baseShadowUV).x));
		diffuseColor += (charLightMul * charLightColor).xxx;
	}
#	endif

#	if defined(EYE) && USE_PIXL_EYE_OPTICS
	// Direct iris lighting has already run; reflections now use outer corneal geometry.
	worldNormal.xyz = normalize(input.EyeNormal);
#	elif defined(EYE) && defined(VANILLA_EYE_NORMAL)
	worldNormal.xyz = input.EyeNormal;
#	endif  // EYE

	// sRGB by default, linear if LL on
	float3 emitColor = Color::EmitColor(EmitColor);
#	if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	bool hasEmissive = (0x3F & (Permutation::PixelShaderDescriptor >> 24)) == Permutation::LightingTechnique::Glowmap;
#		if defined(MATERIAL_FORGE)
	hasEmissive = hasEmissive || (PBRFlags & PBR::Flags::HasEmissive != 0);
#		endif
	[branch] if (hasEmissive)
	{
		// Input TexGlowSampler = linear by default, but Color::Glowmap returns in sRGB if LL disabled
		float3 glowColor = Color::Glowmap(TexGlowSampler.Sample(SampGlowSampler, uv).xyz);

#		if defined(MATERIAL_FORGE)
		float3 emitVertexColor = Color::SrgbToLinear(input.Color.xyz);
		float emitVertexAO = max(max(emitVertexColor.r, emitVertexColor.g), emitVertexColor.b);
		emitVertexColor = emitVertexAO == 0.0f ? 1.0f : emitVertexColor * lerp(1 / max(emitVertexAO, 1e-4), 1, SharedData::materialForgeSettings.VertexAOStrength);

		if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
			emitColor = Color::SrgbToLinear(emitColor);
			glowColor = Color::SrgbToLinear(glowColor);
			emitColor *= glowColor;
			emitColor *= emitVertexColor;
			emitColor = Color::LinearToSrgb(emitColor);
		} else {
			emitColor *= glowColor;
			emitColor *= emitVertexColor;
		}
#		else
		if (!SharedData::linearLightCoreSettings.enableLinearLightCore) {
			emitColor = Color::LinearToSrgb(Color::SrgbToLinear(emitColor) * Color::SrgbToLinear(glowColor));
		} else {
			emitColor *= glowColor;
		}
#		endif  // MATERIAL_FORGE
	}
#	endif

#	if defined(PIXL_WINDOW_LIFE_ACTIVE)
	// PIXL WL2A OCCUPANT + TIER DEBUG
	WindowLife::Result pixlWindowLife = WindowLife::Evaluate(
		input.WorldPosition.xyz,
		viewDirection,
		vertexNormal,
		rawBaseColor.rgb,
		normalColor,
		pixlWindowGlowLuma,
		uv,
		viewPosition.z);

	// PIXL WL5 authored recessed room back plane. Replace the source window's flat
	// emissive fill with readable atlas detail while retaining a faint glass tint. The atlas
	// is stored as sRGB and sampled linearly; convert only for the legacy output
	// path so Linear Light Core and vanilla lighting remain colour-space correct.
	[branch] if (pixlWindowLife.roomColorWeight > 1.0e-4f)
	{
		float3 pixlRoomColor = max(pixlWindowLife.roomColor, 0.0f.xxx);
		if (!SharedData::linearLightCoreSettings.enableLinearLightCore)
			pixlRoomColor = Color::LinearToSrgb(pixlRoomColor);

		float pixlEmitLuma = dot(max(emitColor, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
		bool pixlLinearRoom = SharedData::linearLightCoreSettings.enableLinearLightCore;
		// RoomAtlas has a stable authored mean luminance. Exposure must be derived
		// from that mean, not independently from every sampled pixel; per-pixel
		// normalization erased the furniture/light contrast that conveys depth.
		float pixlRoomReferenceLuma = pixlLinearRoom ? 0.105f : 0.345f;
		float pixlNightWeight = saturate(
			(WindowLife::GetRuntime0().z - 0.12f) / 0.73f);
		float pixlVisibilityFloor = lerp(
			pixlLinearRoom ? 0.165f : 0.445f,
			pixlLinearRoom ? 0.390f : 0.655f,
			pixlNightWeight);
		float pixlTargetLuma = max(
			max(pixlEmitLuma * 0.92f, pixlWindowGlowLuma * 0.76f + 0.105f),
			pixlVisibilityFloor);
		float pixlRoomExposure = clamp(
			pixlTargetLuma / pixlRoomReferenceLuma,
			0.90f,
			8.00f);
		float3 pixlRoomExposed = pixlRoomColor * pixlRoomExposure;
		// A mild contrast expansion keeps firelight, furniture and recessed wall
		// structure readable after mip filtering instead of returning to flat paint.
		float3 pixlRoomTarget = max(
			pixlTargetLuma.xxx + (pixlRoomExposed - pixlTargetLuma.xxx) * 1.16f,
			0.0f.xxx);
		float pixlRoomCompositeWeight =
			pow(saturate(pixlWindowLife.roomColorWeight * 1.34f), 0.72f) * 0.995f;
		// The underlying diffuse texture is the source of the opaque yellow-paint
		// read. Retain only a restrained stained-glass/weathering trace once an
		// authored room is trustworthy; the glass optics still contribute their
		// independent reflection, roughness, dirt and refraction layers.
		diffuseColor *= lerp(1.0f, 0.08f, pixlRoomCompositeWeight);
		emitColor = lerp(
			emitColor,
			pixlRoomTarget,
			pixlRoomCompositeWeight);
	}

	float pixlWindowInteriorOcclusion = saturate(
		pixlWindowLife.occlusion +
		pixlWindowLife.curtainOcclusion +
		pixlWindowLife.roomDepthOcclusion);
	float pixlWindowTransmission = 1.0f - pixlWindowInteriorOcclusion;
	emitColor *= pixlWindowTransmission;
	diffuseColor *= 1.0f - pixlWindowInteriorOcclusion * 0.40f;

	// Glass dirt should influence transmission gently, never extinguish authored
	// window glow or repaint the room colour.
	[branch] if (pixlWindowSurface.glassWeight > 1.0e-4f)
		emitColor *= lerp(1.0f, pixlWindowSurface.transmission, pixlWindowSurface.glassWeight * 0.30f);

	if (WindowLife::GetRuntime1().x > 0.5f)
	{
		float3 pixlWindowTierColor = pixlWindowLife.tier < 1.5f
			? float3(0.10f, 0.38f, 1.00f)   // blue: glass only
			: (pixlWindowLife.tier < 2.5f
				? float3(1.00f, 0.63f, 0.08f) // amber: shallow interior
				: float3(0.10f, 0.90f, 0.32f)); // green: full occupancy
		float3 pixlWindowDebugColor = pixlWindowLife.authoredLayoutState < 0.5f
			? pixlWindowTierColor
			: (pixlWindowLife.authoredLayoutState < 1.5f
				? float3(1.00f, 0.08f, 0.08f) // red: authored fit failed
				: (pixlWindowLife.authoredLayoutState < 2.5f
					? float3(0.06f, 0.82f, 1.00f) // cyan: room background fit
					: float3(0.10f, 0.90f, 0.32f))); // green: occupant-safe fit
		float pixlWindowDebugWeight = saturate(pixlWindowLife.debugMask * 0.58f);
		diffuseColor = lerp(diffuseColor, pixlWindowDebugColor, pixlWindowDebugWeight);
	}
#	endif
#	if !defined(MATERIAL_FORGE)
	diffuseColor += emitColor.xyz;
#	endif

	IndirectContext indirectContext = (IndirectContext)0;
	IndirectLobeWeights indirectLobeWeights;

#	if defined(MULTI_LAYER_PARALLAX)
	float layerValue = MultiLayerParallaxData.x * TexLayerSampler.Sample(SampLayerSampler, uv).w;
	float3 tangentViewDirection = mul(viewDirection, tbn);
	float3 layerNormal = MultiLayerParallaxData.yyy * (normalColor.xyz * 2.0.xxx + float3(-1, -1, -2)) + float3(0, 0, 1);
	float layerViewAngle = dot(-tangentViewDirection.xyz, layerNormal.xyz) * 2;
	float3 layerViewProjection = -layerNormal.xyz * layerViewAngle.xxx - tangentViewDirection.xyz;
	float2 layerUv = uv * MultiLayerParallaxData.zw + (0.0009765625 * (layerValue / abs(layerViewProjection.z))).xx * layerViewProjection.xy;

	float3 layerColor = TexLayerSampler.Sample(SampLayerSampler, layerUv).xyz;

	float mlpBlendFactor = saturate(viewNormalAngle) * (1.0 - baseColor.w);

	material.BaseColor = lerp(material.BaseColor, layerColor, mlpBlendFactor);

	indirectLobeWeights.diffuse *= 1.0 - mlpBlendFactor;
#	endif  // MULTI_LAYER_PARALLAX

	float3 ambientNormal = worldNormal.xyz;
#	if defined(HAIR) && defined(STRAND_SHADING)
	if (SharedData::strandShadingSettings.Enabled) {
		if (SharedData::strandShadingSettings.HairMode == 1)
			ambientNormal = normalize(viewDirection - hairT * dot(viewDirection, hairT));
		else
			ambientNormal = vertexNormal.xyz;
		screenSpaceNormal = normalize(FrameBuffer::WorldToView(ambientNormal, false));
	}
#	endif

	float3 directionalAmbientColor = Color::Ambient(max(0, mul(DirectionalAmbient, float4(ambientNormal, 1.0))));

#	if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		if (SharedData::ambientProbeSettings.UseStaticAmbientProbe && !inWorld && !inReflection) {
			directionalAmbientColor = AmbientProbe::GetStaticDiffuseAmbient(ambientNormal, SampColorSampler);
		}
	}
#	endif

#	if defined(LANDSCAPE)
	if (SharedData::distanceBlendSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;
	else
		input.Color.xyz /= max(max(max(input.Color.x, input.Color.y), input.Color.z), EPSILON_DIVISION);
#	endif

#	if defined(HAIR)
	float3 vertexColor = lerp(1, Color::ColorToLinear(TintColor.xyz), Color::ColorToLinear(input.Color.y));
	float vertexAO = 1;
#		if defined(STRAND_SHADING)
	if (SharedData::strandShadingSettings.Enabled)
		vertexColor = 1;
#		endif
#		if defined(SKY_BOUNCE)
	float skyBounceDiffuse = SkyBounce::GetSkyBounceDiffuse(skyBounceSH, input.WorldPosition.xyz, ambientNormal);
#		endif
#	elif defined(SKY_BOUNCE)
	float3 vertexColor = input.Color.xyz;
#		if defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)
	float vertexAO = 1;
#		else
	float vertexAO = Color::ColorToLinear(max(max(vertexColor.r, vertexColor.g), vertexColor.b).xxx).x;
#		endif
#		if defined(MATERIAL_FORGE)
	vertexAO = lerp(1, vertexAO, SharedData::materialForgeSettings.VertexAOStrength);
	vertexColor = 1;
#		endif
	float skyBounceDiffuse = SkyBounce::GetSkyBounceDiffuse(skyBounceSH, input.WorldPosition.xyz, ambientNormal, vertexAO);
#	else
#		if defined(MATERIAL_FORGE)
	float3 vertexColor = 1;
#		else
	float3 vertexColor = input.Color.xyz;
#		endif
#		if defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)
	float vertexAO = 1;
#		else
	float vertexAO = Color::ColorToLinear(max(max(vertexColor.r, vertexColor.g), vertexColor.b).xxx).x;
#		endif
#	endif  // defined (HAIR)

#	if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		if (!(SharedData::ambientProbeSettings.UseStaticAmbientProbe && !inWorld && !inReflection)) {
			directionalAmbientColor = AmbientProbe::GetDiffuseAmbient(directionalAmbientColor, -ambientNormal);
		}
	}
#	endif

	float3 reflectionDiffuseColor = diffuseColor + directionalAmbientColor;

#	if defined(MATERIAL_FORGE) && defined(LOD_LAND_BLEND) && !defined(DEFERRED)
	lodLandDiffuseColor += directionalAmbientColor;
#	endif

#	if defined(PIXL_PARALLAX_DEPTH)
	float4 pixlMotionWorldPosition = input.WorldPosition;
	pixlMotionWorldPosition.xyz = pixlParallaxWorldPosition;
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(pixlMotionWorldPosition, input.PreviousWorldPosition);
#	else
	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
#	endif

#	if defined(RAIN_RESPONSE)
#		if !(defined(FACEGEN) || defined(FACEGEN_RGB_TINT) || defined(EYE)) || defined(TREE_ANIM)
#			if defined(MATERIAL_FORGE)
#				if !defined(LANDSCAPE)
	[branch] if ((PBRFlags & PBR::Flags::TwoLayer) != 0)
	{
		porosity = 0;
	}
	else
#				endif
	{
		porosity = lerp(porosity, 0.0, saturate(sqrt(material.Metallic)));
	}
#			elif defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX)
	porosity = lerp(porosity, 0.0, saturate(sqrt(envMask)));
#			endif
	float wetnessDarkeningAmount = porosity * wetnessGlossinessAlbedo;
#			if USE_PIXL_WETNESS_RESPONSE
	float3 wetInternalReflection = lerp(0.58f.xxx, 0.82f.xxx, sqrt(saturate(material.BaseColor)));
	material.BaseColor *= lerp(1.0f.xxx, wetInternalReflection, saturate(wetnessDarkeningAmount));
#			else
	material.BaseColor = lerp(material.BaseColor, pow(abs(material.BaseColor), 1.0 + wetnessDarkeningAmount), 0.5);
#			endif
#		endif
#	endif

	float4 color = 0;

	indirectContext = CreateIndirectLightingContext(ambientNormal, vertexNormal.xyz, viewDirection);

	PhysicalLighting::EvaluateIndirect(indirectLobeWeights, indirectContext, material, uvOriginal);

#	if defined(RAIN_RESPONSE)
#		if defined(WORLD_PROBES)
	float3 wetnessReflectance = GetWetnessIndirectLobeWeights(indirectLobeWeights, wetnessNormal, waterRoughnessSpecular, indirectContext);
#		else
	float3 wetnessReflectance = 0.0;
#		endif
#	endif
#	if defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE)
	indirectLobeWeights.specular *= envMask;
#	endif

#	if defined(SPECULAR) && !defined(MATERIAL_FORGE)
	indirectLobeWeights.specular *= MaterialData.yyy;
	specularColor *= MaterialData.yyy;
#	endif

#	if defined(MATERIAL_FORGE)
	{
		float3 directLightsDiffuseInput = diffuseColor * material.BaseColor;
		[branch] if ((PBRFlags & PBR::Flags::ColoredCoat) != 0)
		{
			directLightsDiffuseInput = lerp(directLightsDiffuseInput, material.CoatColor * coatLightsDiffuseColor, material.CoatStrength);
		}

		color.xyz += directLightsDiffuseInput;
	}

	[branch] if ((PBRFlags & PBR::Flags::HasEmissive) != 0)
	{
		color.xyz += emitColor.xyz;
	}
#	else
	color.xyz += diffuseColor * material.BaseColor;
#	endif

	color.xyz += indirectLobeWeights.diffuse * directionalAmbientColor;
	color.xyz += transmissionColor;

	color.xyz *= vertexColor;

#	if defined(SNOW)
	if (useSnowSpecular)
		specularColor = 0;
#	endif

	diffuseColor = reflectionDiffuseColor;

#	if (defined(ENVMAP) || defined(MULTI_LAYER_PARALLAX) || defined(EYE))
#		if defined(WORLD_PROBES)
	if (!dynamicCubemap)
#		endif
		specularColor += envColor * Color::IrradianceToLinear(diffuseColor);
	indirectLobeWeights.diffuse += envColor;
#	endif

#	if defined(EMAT_ENVMAP)
	specularColor *= complexSpecular;
#	endif  // defined (EMAT) && defined(ENVMAP)

#	if defined(LOD_LAND_BLEND) && defined(MATERIAL_FORGE)
	{
		lodLandDiffuseColor += directionalAmbientColor;
		float3 litLodLandColor = vertexColor * lodLandColor.xyz * lodLandFadeFactor * lodLandDiffuseColor;
		color.xyz = lerp(color.xyz * Color::PBRLightingScale, litLodLandColor, lodLandBlendFactor);

		specularColor = lerp(specularColor * Color::PBRLightingScale, 0, lodLandBlendFactor);
		indirectLobeWeights.diffuse = lerp(indirectLobeWeights.diffuse * Color::PBRLightingScale, vertexColor * lodLandColor.xyz * lodLandFadeFactor, lodLandBlendFactor);
		indirectLobeWeights.specular = lerp(indirectLobeWeights.specular, 0, lodLandBlendFactor);
		material.Roughness = lerp(material.Roughness, 1, lodLandBlendFactor);
	}
#	elif defined(MATERIAL_FORGE)
	color.xyz *= Color::PBRLightingScale;
	specularColor *= Color::PBRLightingScale;
	indirectLobeWeights.diffuse *= Color::PBRLightingScale;
#	endif

	float3 outputAlbedo = indirectLobeWeights.diffuse * vertexColor.xyz;

	directionalAmbientColor *= outputAlbedo;

#	if defined(SKY_BOUNCE)
	SkyBounce::ApplySkyBounce(color.xyz, directionalAmbientColor, outputAlbedo, skyBounceDiffuse);
#	endif

#	if !defined(DEFERRED)
	color.xyz = Color::IrradianceToLinear(color.xyz);
	color.xyz += specularColor;

	if (any(indirectLobeWeights.specular > 0)
#		if defined(RAIN_RESPONSE)
		|| any(wetnessReflectance > 0)
#		endif
	)
#		if defined(WORLD_PROBES)
#			if defined(SKY_BOUNCE)
		color.xyz += indirectLobeWeights.specular * WorldProbes::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness, skyBounceSH);
#				if defined(RAIN_RESPONSE)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * WorldProbes::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular, skyBounceSH);
#				endif
#			else
		color.xyz += indirectLobeWeights.specular * WorldProbes::GetDynamicCubemapSpecularIrradiance(worldNormal, viewDirection, material.Roughness);
#				if defined(RAIN_RESPONSE)
	if (waterRoughnessSpecular < 1)
		color.xyz += wetnessReflectance * WorldProbes::GetDynamicCubemapSpecularIrradiance(wetnessNormal, viewDirection, waterRoughnessSpecular);
#				endif
#			endif
#		else
		color.xyz += indirectLobeWeights.specular * directionalAmbientColor;
#		endif

	color.xyz = Color::IrradianceToGamma(color.xyz);
	float3 fogColor = Color::Fog(input.FogParam.xyz);
	float fogFactor = Color::FogAlpha(input.FogParam.w);
#		if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		fogColor = AmbientProbe::GetFogAmbientColor(fogColor);
	}
#		endif
#		if defined(ATMOSPHERE_PIPELINE)
	float3 vanillaFogColor = fogColor;
	float vanillaFogFactor = fogFactor;
	if (SharedData::atmosphereSettings.enabled) {
		float4 atmosphere;
		if (inReflection) {
			atmosphere = Atmosphere::GetAtmosphereWithoutVolumes(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		} else {
			atmosphere = Atmosphere::GetAtmosphere(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		}
		fogColor = atmosphere.xyz;
		fogFactor = atmosphere.w;
	}
#		endif
	if ((FrameBuffer::FrameParams.y && FrameBuffer::FrameParams.z) || inReflection) {
#		if defined(ATMOSPHERE_PIPELINE)
		if (SharedData::atmosphereSettings.enabled) {
			if (!Atmosphere::ShouldDisableVanillaFog()) {
				color.xyz = lerp(color.xyz, vanillaFogColor, vanillaFogFactor);
			}
			color.xyz = lerp(color.xyz, fogColor, fogFactor);
		} else {
			color.xyz = lerp(color.xyz, fogColor, fogFactor);
		}
#		else
		color.xyz = lerp(color.xyz, fogColor, fogFactor);
#		endif
	}
#	endif

#	if defined(TESTCUBEMAP) && defined(ENVMAP) && defined(WORLD_PROBES)
	baseColor.xyz = 0.0;
	specularColor = 0.0;
	diffuseColor = 0.0;
	dynamicCubemap = true;
	envColor = 1.0;
	material.Roughness = 0.0;
	color.xyz = 0;
#	endif

#	if defined(LANDSCAPE) && !defined(LOD_LAND_BLEND)
	psout.Diffuse.w = 0;
#	else
	float alpha = baseColor.w;
#		if defined(EMAT) && !defined(LANDSCAPE)
#			if defined(PARALLAX)
	alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
#			elif defined(MATERIAL_FORGE)
	[branch] if (PBRParallax)
	{
		alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
	}
#			endif
#		endif
#		if defined(PIXL_AUTO_PARALLAX)
	[branch] if (autoParallaxApplied)
		alpha = TexColorSampler.SampleBias(SampColorSampler, uvOriginal, SharedData::MipBias).w;
#		endif
#		if defined(DO_ALPHA_TEST)
	[branch] if ((Permutation::PixelShaderDescriptor & Permutation::LightingFlags::AdditionalAlphaMask) != 0)
	{
		uint2 alphaMask = input.Position.xy;
		alphaMask.x = ((alphaMask.x << 2) & 12);
		alphaMask.x = (alphaMask.y & 3) | (alphaMask.x & ~3);
		const float maskValues[16] = {
			0.003922,
			0.533333,
			0.133333,
			0.666667,
			0.800000,
			0.266667,
			0.933333,
			0.400000,
			0.200000,
			0.733333,
			0.066667,
			0.600000,
			0.996078,
			0.466667,
			0.866667,
			0.333333,
		};

		float testTmp = 0;
		if (MaterialData.z - maskValues[alphaMask.x] < 0) {
			discard;
		}
	}
	else
#		endif  // defined(DO_ALPHA_TEST)
	{
		alpha *= MaterialData.z;
	}
#		if !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
	alpha *= input.Color.w;
#		endif  // !(defined(TREE_ANIM) || defined(LODOBJECTSHD) || defined(LODOBJECTS))
#		if defined(DO_ALPHA_TEST)
#			if defined(DEPTH_WRITE_DECALS)
	if (alpha - 0.0156862754 < 0) {
		discard;
	}
	alpha = saturate(1.05 * alpha);
#			endif  // DEPTH_WRITE_DECALS
	if (alpha - AlphaTestRefRS < 0) {
		discard;
	}
#		endif      // DO_ALPHA_TEST

#		if defined(ANISOTROPIC_ALPHA)
	// Uniform alpha material settings
	uint AlphaMaterialModel = ThinSurface::GetMaterialModelFromDescriptor(Permutation::ExtraFeatureDescriptor);
	float AlphaMaterialReduction = 0.f;
	float AlphaMaterialSoftness = 0.f;
	float AlphaMaterialStrength = 0.f;
	[branch] if (AlphaMaterialModel == ThinSurface::MaterialModel::Default)
	{
		AlphaMaterialModel = SharedData::thinSurfaceSettings.MaterialModel;
		AlphaMaterialReduction = SharedData::thinSurfaceSettings.Reduction;
		AlphaMaterialSoftness = SharedData::thinSurfaceSettings.Softness;
		AlphaMaterialStrength = SharedData::thinSurfaceSettings.Strength;
	}

	[branch] if (ThinSurface::IsValidMaterial(AlphaMaterialModel))
	{
		if (alpha >= 0.0156862754 && alpha < 1.0) {
			float originalAlpha = alpha;
			alpha = alpha * (1.0 - AlphaMaterialReduction);
			[branch] if (AlphaMaterialModel == ThinSurface::MaterialModel::AnisotropicFabric)
			{
#			if defined(SKINNED) || !defined(MODELSPACENORMALS)
				alpha = ThinSurface::GetViewDependentAlphaFabric2D(alpha, viewDirection, tbnTr);
#			else
				alpha = ThinSurface::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
#			endif
			}
			else if (AlphaMaterialModel == ThinSurface::MaterialModel::IsotropicFabric)
			{
				alpha = ThinSurface::GetViewDependentAlphaFabric1D(alpha, viewDirection, worldNormal.xyz);
			}
			else
			{
				alpha = ThinSurface::GetViewDependentAlphaNaive(alpha, viewDirection, worldNormal.xyz);
			}
			alpha = saturate(ThinSurface::SoftClamp(alpha, 2.0f - AlphaMaterialSoftness));
			alpha = lerp(alpha, originalAlpha, AlphaMaterialStrength);
		}
	}
#		endif  // ANISOTROPIC_ALPHA

	psout.Diffuse.w = alpha;
#	endif

#	if defined(RADIANT_GRID) && defined(LLFDEBUG)
	if (SharedData::radiantGridSettings.EnableLightsVisualisation) {
		if (SharedData::radiantGridSettings.LightsVisualisationMode == 0) {
			psout.Diffuse.xyz = Color::TurboColormap(RadiantGrid::NumStrictLights >= 7.0);
		} else if (SharedData::radiantGridSettings.LightsVisualisationMode == 1) {
			psout.Diffuse.xyz = Color::TurboColormap((float)RadiantGrid::NumStrictLights / 15.0);
		} else if (SharedData::radiantGridSettings.LightsVisualisationMode == 2) {
			psout.Diffuse.xyz = Color::TurboColormap((float)numClusteredLights / MAX_CLUSTER_LIGHTS);
		} else {
			psout.Diffuse.xyz = shadowColor.xyz;
		}
		baseColor.xyz = 0.0;
	} else {
		psout.Diffuse.xyz = color.xyz;
	}
#	else
	psout.Diffuse.xyz = color.xyz;
#	endif  // defined(RADIANT_GRID)

	psout.MotionVectors.xy = screenMotionVector.xy;
	psout.MotionVectors.zw = float2(0, psout.Diffuse.w);

#	if defined(DEFERRED)

#		if defined(TERRAIN_SEAM)
	[flatten] if (SharedData::terrainSeamSettings.Enabled)
	{
#			if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
		// PIXL_GR_OPAQUE_HULL_V2
		//
		// TerrainSeam intentionally alpha-blends its delayed landscape replay.
		// That is correct for the original seam surface, but not for terrain that
		// GroundResponse has physically moved with HS/DS: using the old seam depth
		// mask as alpha lets previously-rendered actors, vegetation and statics show
		// through the raised shell.
		//
		// Normal gameplay: only pixels with a real snow/mud geometric surface are
		// forced opaque. +64 is a geometry diagnostic that deliberately bypasses
		// material activation, so include it explicitly as an opaque surface too.
		// PIXL_GR_OPAQUE_GEOMETRY_PASS_V3
		// Definitive TerrainSeam/GroundResponse opacity diagnostic.
		// Follow PrepareTerrainPass()'s CPU geometry decision exactly.
		const bool groundRequiresOpaqueCoverage =
			GroundResponseRuntime::IsGeometryPass();
		psout.Diffuse.w =
			groundRequiresOpaqueCoverage
				? 1.0f
				: blendFactorTerrain;
#			else
		psout.Diffuse.w = blendFactorTerrain;
#			endif
	}
#		endif

	psout.MotionVectors.zw = float2(0.0, psout.Diffuse.w);
	psout.Specular = float4(specularColor, psout.Diffuse.w);
	psout.Albedo = float4(outputAlbedo, psout.Diffuse.w);

#		if defined(RAIN_RESPONSE)
	indirectLobeWeights.specular += wetnessReflectance;
	if (waterRoughnessSpecular < 1) {
		// Reflection is from the water film surface; wetnessReflectance scales intensity by wetness amount.
		screenSpaceNormal = normalize(FrameBuffer::WorldToView(wetnessNormal, false));
		material.Roughness = waterRoughnessSpecular;
	}
#		endif

	psout.Reflectance = float4(indirectLobeWeights.specular, psout.Diffuse.w);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(screenSpaceNormal), saturate(1.0 - material.Roughness), psout.Diffuse.w);

#		if defined(SNOW)
#			if defined(MATERIAL_FORGE)
	psout.Parameters.x = Color::RGBToLuminanceAlternative(specularColor);
	psout.Parameters.y = 0;
#			else
	psout.Parameters.x = Color::RGBToLuminanceAlternative(lightsSpecularColor);
#			endif
	psout.Parameters.w = psout.Diffuse.w;
#		endif

	float masksZ = Color::RGBToYCoCg(directionalAmbientColor).x;

#		if defined(TISSUE_DIFFUSION) && defined(SKIN)
	// DialogueFocus raises only the focused face's existing diffusion mask.
	// TissueDiffusion keeps its normal compute path/sample count, avoiding a full-screen cost spike.
	const float pixlDialogueSSSMask =
		saturate(
			baseColor.a *
			(1.0f + 0.10f * pixlDialogueFocus * DialogueFocus::TissueQuality()));
	psout.Masks = float4(pixlDialogueSSSMask, !(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsBeastRace), masksZ, psout.Diffuse.w);
#		else
	psout.Masks = float4(0, 0, masksZ, psout.Diffuse.w);
#		endif

	// Stored as 1 - vertexAO so the cleared default (0) means no occlusion
	// for pixels that do not write to this RT (sky, water, grass, effects).
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, psout.Diffuse.w);

	float stochasticBlend = (screenNoise * screenNoise) < psout.Diffuse.w ? 1.0 : 0.0;
	psout.NormalGlossiness.w = stochasticBlend;
#	endif

#	if !defined(CAMERA_SUITE)  // Do not apply gamma correction before we pass to ISHDR.
	if ((!inWorld && !inReflection) && SharedData::linearLightCoreSettings.enableLinearLightCore && !(Permutation::PixelShaderDescriptor & Permutation::LightingFlags::DefShadow)) {
		psout.Diffuse.xyz = Color::LinearToSrgb(psout.Diffuse.xyz);
	}
#	endif

	const uint physicalDebugMode = SharedData::materialForgeSettings.LegacyPhysicalDebugMode;
	[branch] if (physicalDebugMode != 0)
	{
		float3 physicalDebugColor = 0.0.xxx;
#	if defined(MATERIAL_FORGE)
		if (physicalDebugMode == 1)
			physicalDebugColor = float3(0.05, 0.25, 1.0);
		else if (physicalDebugMode == 2)
			physicalDebugColor = material.BaseColor;
		else if (physicalDebugMode == 3)
			physicalDebugColor = material.Roughness.xxx;
		else if (physicalDebugMode == 4) {
			float metallic = saturate(material.Metallic);
			float dielectricStrength = saturate(Color::RGBToLuminance(material.F0) / 0.08);
			float3 dielectricClass = lerp(float3(0.01, 0.03, 0.10), float3(0.03, 0.35, 1.0), dielectricStrength);
			float3 conductorTint = sqrt(saturate(material.F0));
			physicalDebugColor = lerp(dielectricClass, conductorTint, metallic);
		}
		else if (physicalDebugMode == 5)
			physicalDebugColor = sqrt(saturate(material.F0));
		else if (physicalDebugMode == 6)
			physicalDebugColor = worldNormal.xyz * 0.5 + 0.5;
		else if (physicalDebugMode == 7)
			physicalDebugColor = 1.0 - exp(-max(lightsDiffuseColor * material.BaseColor, 0.0));
		else if (physicalDebugMode == 8)
			physicalDebugColor = 1.0 - exp(-max(lightsSpecularColor, 0.0) * 4.0);
		else if (physicalDebugMode == 10)
			physicalDebugColor = emitColor.xyz;
		else if (physicalDebugMode == 11)
			physicalDebugColor = saturate(material.Metallic).xxx;
		else if (physicalDebugMode == 12)
			physicalDebugColor = saturate(material.Metallic).xxx;
#	else
		if (physicalDebugMode == 1)
			physicalDebugColor = lerp(float3(1.0, 0.05, 0.02), float3(0.05, 1.0, 0.1), legacyPhysicalCoverage);
		else if (physicalDebugMode == 2)
			physicalDebugColor = material.BaseColor;
		else if (physicalDebugMode == 3)
			physicalDebugColor = material.Roughness.xxx;
		else if (physicalDebugMode == 4) {
			float metallic = saturate(material.Metallic);
			float dielectricStrength = saturate(Color::RGBToLuminance(material.F0) / 0.08);
			float3 dielectricClass = lerp(float3(0.01, 0.03, 0.10), float3(0.03, 0.35, 1.0), dielectricStrength);
			float3 conductorTint = sqrt(saturate(material.F0));
			physicalDebugColor = lerp(dielectricClass, conductorTint, metallic);
		}
		else if (physicalDebugMode == 5)
			physicalDebugColor = sqrt(saturate(material.F0));
		else if (physicalDebugMode == 6)
			physicalDebugColor = worldNormal.xyz * 0.5 + 0.5;
		else if (physicalDebugMode == 7)
			physicalDebugColor = 1.0 - exp(-max(lightsDiffuseColor * material.BaseColor, 0.0));
		else if (physicalDebugMode == 8)
			physicalDebugColor = 1.0 - exp(-max(lightsSpecularColor, 0.0) * 4.0);
		else if (physicalDebugMode == 9)
			physicalDebugColor = 1.0 - exp(-max(legacyPhysicalDelta, 0.0) * 8.0);
		else if (physicalDebugMode == 10)
			physicalDebugColor = emitColor.xyz;
		else if (physicalDebugMode == 11)
			physicalDebugColor = 0.0.xxx;  // No authored metallic channel on legacy materials.
		else if (physicalDebugMode == 12)
			physicalDebugColor = saturate(material.Metallic).xxx;
#	endif
		psout.Diffuse.xyz = physicalDebugColor;
#	if defined(DEFERRED)
		psout.Specular.xyz = 0.0;
		psout.Albedo.xyz = 0.0;
		psout.Reflectance.xyz = 0.0;
#	endif
	}

#	if defined(GROUND_RESPONSE) && defined(LANDSCAPE)
	// v2.1 diagnostics fail closed. A stale/wrong b13 can no longer paint terrain
	// brown/cyan because the overlay requires the exact runtime magic/version and
	// the dedicated TerrainDebug bit. The real terrain textures remain visible.
	[branch] if (GroundResponseRuntime::DebugOverlayEnabled())
	{
		float3 groundDebugColor;
		if (GroundRuntimeBoundingBoxCount == 0u) {
			groundDebugColor = float3(1.0f, 0.02f, 0.02f);
		} else if (GroundRuntimeTerrainMaterialForge != 0u &&
			!groundSnowClassificationValid) {
			groundDebugColor = float3(1.0f, 0.0f, 1.0f);
		} else {
			float3 nonSnowColor = float3(0.40f, 0.14f, 0.04f);
			float3 snowColor = float3(0.08f, 0.72f, 1.0f);
			groundDebugColor =
				lerp(nonSnowColor, snowColor, saturate(groundSnowCoverage));
			float3 contactColor =
				lerp(
					float3(0.05f, 0.90f, 0.18f),
					float3(1.0f, 0.92f, 0.05f),
					saturate(groundDeformationFreshness));
			groundDebugColor =
				lerp(
					groundDebugColor,
					contactColor,
					saturate(groundDeformationAmount));

			if (GroundResponseRuntime::GeometrySelfTestEnabled())
				groundDebugColor = lerp(groundDebugColor, 1.0f.xxx, 0.65f);
			else if (GroundResponseRuntime::IsGeometryPass())
				groundDebugColor.b = max(groundDebugColor.b, 0.70f);
		}

		const float debugDiffuseAlpha = 0.28f;
		psout.Diffuse.xyz =
			lerp(psout.Diffuse.xyz, groundDebugColor, debugDiffuseAlpha);
#		if defined(DEFERRED)
		psout.Albedo.xyz =
			lerp(psout.Albedo.xyz, groundDebugColor, 0.18f);
#		endif
	}
#	endif

// PIXL_GR_13BA_REAL_LIGHTING_RAW_ALBEDO_DIAGNOSTIC_V1
//
// 13AZ proved raw landscape sampling/blending is valid.
//
// Preserve the REAL PIXL/Skyrim lit Diffuse output and every normal/specular/
// roughness/reflectance output. Override ONLY the deferred albedo with the
// known-good raw landscape colour.
//
// This isolates:
//   - direct lighting / normals / shadows -> psout.Diffuse remains untouched
//   - broken indirect material albedo     -> replaced with known-good texture
//
// 13AW raster depth remains authoritative.
#if defined(DEFERRED) && defined(GROUND_RESPONSE) && defined(LANDSCAPE)
    [branch] if (GroundResponseRuntime::IsGeometryPass())
    {
        // DO NOT modify psout.Diffuse here.
        // It already contains the real calculated Lighting result.

        psout.Albedo =
            float4(
                saturate(rawBaseColor.rgb),
                1.0f);
    }
#endif

#	if defined(PIXL_VIRTUAL_DEPTH_OUTPUT)
	psout.Depth = pixlVirtualDepth;
#	endif

#	if defined(EMAT)
#		undef COMPUTE_TERRAIN_SHADOW_BASE
#		undef EVAL_TERRAIN_DIR_SHADOW
#		undef LANDSCAPE_PARALLAX_ENABLED
#	endif
#	if defined(PIXL_AUTO_PARALLAX)
#		undef PIXL_AUTO_PARALLAX
#	endif
#	if defined(PIXL_PARALLAX_DEPTH)
#		undef PIXL_PARALLAX_DEPTH
#	endif
#	if defined(PIXL_GROUND_DEFORMATION_DEPTH)
#		undef PIXL_GROUND_DEFORMATION_DEPTH
#	endif
#	if defined(PIXL_VIRTUAL_DEPTH_OUTPUT)
#		undef PIXL_VIRTUAL_DEPTH_OUTPUT
#	endif

	return psout;
}
#endif  // PSHADER
