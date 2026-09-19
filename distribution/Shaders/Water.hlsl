#if defined(HORIZON_BLEND)
namespace HorizonBlend
{
	// Depth (z/w) that water folded back from beyond the far clip plane lands at: eight
	// depth quanta inside the far plane, exactly representable in both D24 and D32F
	// buffers - behind everything the scene rendered, in front of the depth clear.
	static const float FoldedDepth = 1.0 - 8.0 / 16777216.0;

	// Scene depth at or beyond this counts as "nothing rendered behind the water": the
	// clear value, folded far water (FoldedDepth), or an external plugin's depth-clamped
	// far-water backdrop a few quanta inside that. The margin is a few world units at the
	// far plane, where no real surface can render.
	static const float EmptyDepthThreshold = 1.0 - 64.0 / 16777216.0;

	// HorizonFix supplies a far-water skirt folded just inside the far plane.
	// Fade only that folded, near-horizontal water into the scene atmosphere;
	// ordinary close and mid-range water retains its authored colour.
	static const float FadeStartElevation = 0.16;
	static const float FadeEndElevation = 0.025;
	static const float FadeStartDistance = 0.70;
	static const float FadeEndDistance = 0.98;
}
#endif

#if defined(UNDERWATERMASK)

struct VS_INPUT
{
	float4 Position: POSITION0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
};

#	ifdef VSHADER

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float z = min(1, 1e-4 * max(0, input.Position.z - 70000)) * 0.5 + input.Position.z;
	vsout.Position = float4(input.Position.xy, z, 1);

	return vsout;
}
#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#	ifdef PSHADER
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	psout.Color = 1;

	return psout;
}
#	endif

#else

#	include "Common/FrameBuffer.hlsli"
#	include "Common/Game.hlsli"
#	include "Common/MotionBlur.hlsli"
#	include "Common/Permutation.hlsli"
#	include "Common/Random.hlsli"
#	include "Common/Shading.hlsli"
#	include "Common/BRDF.hlsli"
#	include "Common/Color.hlsli"

#	define WATER

#	include "Common/SharedData.hlsli"

#	ifndef USE_PIXL_WATER_OPTICS
#		define USE_PIXL_WATER_OPTICS 1
#	endif

struct VS_INPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER) || defined(STENCIL) || defined(SIMPLE)
	float4 Position: POSITION0;
#		if defined(NORMAL_TEXCOORD)
	float2 TexCoord0: TEXCOORD0;
#		endif
#		if defined(VC)
	float4 Color: COLOR0;
#		endif
#	endif

#	if defined(LOD)
	float4 Position: POSITION0;
#		if defined(VC)
	float4 Color: COLOR0;
#		endif
#	endif
};

struct VS_OUTPUT
{
#	if defined(SPECULAR) || defined(UNDERWATER)
	float4 HPosition: SV_POSITION0;
#		if !defined(WATERBODY)
	float4 FogParam: COLOR0;
#		endif
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
	float4 TexCoord2: TEXCOORD2;
#		if defined(WADING) || (defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS))) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)) || ((defined(SPECULAR) && NUM_SPECULAR_LIGHTS == 0) && defined(FLOWMAP) /*!defined(NORMAL_TEXCOORD) && !defined(BLEND_NORMALS) && !defined(VC)*/)
	float4 TexCoord3: TEXCOORD3;
#		endif
#		if defined(FLOWMAP)
	nointerpolation float2 TexCoord4: TEXCOORD4;
#		endif
#		if NUM_SPECULAR_LIGHTS == 0
	float4 MPosition: TEXCOORD5;
#		endif
#	endif

#	if defined(SIMPLE)
	float4 HPosition: SV_POSITION0;
	float4 FogParam: COLOR0;
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
	float4 TexCoord2: TEXCOORD2;
	float4 MPosition: TEXCOORD5;
#	endif

#	if defined(LOD)
	float4 HPosition: SV_POSITION0;
	float4 FogParam: COLOR0;
	float4 WPosition: TEXCOORD0;
	float4 TexCoord1: TEXCOORD1;
#	endif

#	if defined(STENCIL)
	float4 HPosition: SV_POSITION0;
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
#	endif

	float4 NormalsScale: TEXCOORD8;
};

#	ifdef VSHADER

cbuffer PerTechnique : register(b0)
{
	float4 QPosAdjust : packoffset(c0);
};

cbuffer PerMaterial : register(b1)
{
	float4 VSFogParam : packoffset(c0);
	float4 VSFogNearColor : packoffset(c1);
	float4 VSFogFarColor : packoffset(c2);
	float4 NormalsScroll0 : packoffset(c3);
	float4 NormalsScroll1 : packoffset(c4);
	float4 NormalsScale : packoffset(c5);
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 World : packoffset(c0);
	row_major float4x4 PreviousWorld : packoffset(c4);
	row_major float4x4 WorldViewProj : packoffset(c8);
	float3 ObjectUV : packoffset(c12);
	float4 CellTexCoordOffset : packoffset(c13);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout = (VS_OUTPUT)0;

	vsout.NormalsScale = NormalsScale;

	float4 inputPosition = float4(input.Position.xyz, 1.0);
	float4 worldPos = mul(World, inputPosition);
	float4 worldViewPos = mul(WorldViewProj, inputPosition);

	float heightMult = min((1.0 / 10000.0) * max(worldViewPos.z - 70000, 0), 1);

	vsout.HPosition.xy = worldViewPos.xy;
	vsout.HPosition.z = heightMult * 0.5 + worldViewPos.z;
	vsout.HPosition.w = worldViewPos.w;

#	if defined(HORIZON_BLEND)
	vsout.HPosition.z = min(vsout.HPosition.z, vsout.HPosition.w * HorizonBlend::FoldedDepth);
#	endif

#		if defined(STENCIL)
	vsout.WorldPosition = worldPos;
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);
#		else

#			if !defined(WATERBODY)
	float fogDistanceFactor = min(VSFogFarColor.w, pow(saturate(length(worldViewPos.xyz) * VSFogParam.y - VSFogParam.x), NormalsScale.w));
	vsout.FogParam.xyz = lerp(VSFogNearColor.xyz, VSFogFarColor.xyz, fogDistanceFactor);
	vsout.FogParam.w = fogDistanceFactor;
#			endif

	vsout.WPosition.xyz = worldPos.xyz;
	vsout.WPosition.w = length(worldPos.xyz);

#			if defined(LOD)
	float4 posAdjust =
		ObjectUV.x ? 0.0 : (QPosAdjust.xyxy + worldPos.xyxy) / NormalsScale.xxyy;

	vsout.TexCoord1.xyzw = NormalsScroll0 + posAdjust;
#			else
#				if !defined(SPECULAR) || (NUM_SPECULAR_LIGHTS == 0)
	vsout.MPosition.xyzw = inputPosition.xyzw;
#				endif

	float2 posAdjust = worldPos.xy + QPosAdjust.xy;

	float2 scrollAdjust1 = posAdjust / NormalsScale.xx;
	float2 scrollAdjust2 = posAdjust / NormalsScale.yy;
	float2 scrollAdjust3 = posAdjust / NormalsScale.zz;

#				if defined(WATERBODY) && defined(NORMAL_TEXCOORD)
	float2 cellShift = float2(floor(ObjectUV.z * 0.5), floor((ObjectUV.z - 1.0) * 0.5));
	float2 scaledUV = input.TexCoord0.xy * ObjectUV.z - cellShift;
#				endif

#				if !(defined(FLOWMAP) && (defined(REFRACTIONS) || defined(BLEND_NORMALS) || defined(DEPTH) || NUM_SPECULAR_LIGHTS == 0))
#					if defined(NORMAL_TEXCOORD)
	float3 normalsScale = 0.001 * NormalsScale.xyz;
	if (ObjectUV.x) {
		scrollAdjust1 = input.TexCoord0.xy / normalsScale.xx;
		scrollAdjust2 = input.TexCoord0.xy / normalsScale.yy;
		scrollAdjust3 = input.TexCoord0.xy / normalsScale.zz;
	}
#					else
	if (ObjectUV.x) {
		scrollAdjust1 = 0.0;
		scrollAdjust2 = 0.0;
		scrollAdjust3 = 0.0;
	}
#					endif
#				endif

	vsout.TexCoord1 = 0.0;
	vsout.TexCoord2 = 0.0;
#				if defined(FLOWMAP)
#					if !(((defined(SPECULAR) || NUM_SPECULAR_LIGHTS == 0) || (defined(UNDERWATER) && defined(REFRACTIONS))) && !defined(NORMAL_TEXCOORD))
#						if defined(BLEND_NORMALS)
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
#						else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = 0.0;
	vsout.TexCoord2.xy = 0.0;
#						endif
#					endif
#					if !defined(NORMAL_TEXCOORD)
	vsout.TexCoord3 = 0.0;
#					elif defined(WADING)
#						if defined(WATERBODY)
	float2 wadingUV = (input.TexCoord0.xy - 0.5f) * 0.5f;
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + wadingUV) / ObjectUV.xy;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + wadingUV;
#						else
	vsout.TexCoord2.zw = ((-0.5 + input.TexCoord0.xy) * 0.1 + CellTexCoordOffset.xy) +
	                     float2(CellTexCoordOffset.z, -CellTexCoordOffset.w + ObjectUV.x) / ObjectUV.xx;
	vsout.TexCoord3.xy = -0.25 + (input.TexCoord0.xy * 0.5 + ObjectUV.yz);
#						endif
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#					elif (defined(REFRACTIONS) || NUM_SPECULAR_LIGHTS == 0 || defined(BLEND_NORMALS))
#						if defined(WATERBODY)
	float2 dims = float2(ObjectUV.x, ObjectUV.y);
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + scaledUV) / dims;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + scaledUV;
	vsout.TexCoord3.zw = scaledUV;
#						else
	vsout.TexCoord2.zw = (CellTexCoordOffset.xy + input.TexCoord0.xy) / ObjectUV.xx;
	vsout.TexCoord3.xy = CellTexCoordOffset.zw + input.TexCoord0.xy;
	vsout.TexCoord3.zw = input.TexCoord0.xy;
#						endif
#					endif
	vsout.TexCoord4 = ObjectUV.xy;
#				else
	vsout.TexCoord1.xy = NormalsScroll0.xy + scrollAdjust1;
	vsout.TexCoord1.zw = NormalsScroll0.zw + scrollAdjust2;
	vsout.TexCoord2.xy = NormalsScroll1.xy + scrollAdjust3;
	vsout.TexCoord2.z = worldViewPos.w;
	vsout.TexCoord2.w = 0;
#					if (defined(WADING) || (defined(VERTEX_ALPHA_DEPTH) && defined(VC)))
	vsout.TexCoord3 = 0.0;
#						if (defined(NORMAL_TEXCOORD) && ((!defined(BLEND_NORMALS) && !defined(VERTEX_ALPHA_DEPTH)) || defined(WADING)))
	vsout.TexCoord3.xy = input.TexCoord0;
#						endif
#						if defined(VERTEX_ALPHA_DEPTH) && defined(VC)
	vsout.TexCoord3.z = input.Color.w;
#						endif
#					endif
#				endif
#			endif
#		endif

	return vsout;
}

#	endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#	if defined(UNDERWATER) || defined(SIMPLE) || defined(LOD) || defined(SPECULAR)
	float4 Lighting: SV_Target0;
#	endif

#	if defined(STENCIL)
	float4 WaterMask: SV_Target0;
	float2 MotionVector: SV_Target1;
#	endif
};

#	ifdef PSHADER

SamplerState ReflectionSampler : register(s0);
SamplerState RefractionSampler : register(s1);
SamplerState DisplacementSampler : register(s2);
SamplerState CubeMapSampler : register(s3);
SamplerState Normals01Sampler : register(s4);
SamplerState Normals02Sampler : register(s5);
SamplerState Normals03Sampler : register(s6);
SamplerState DepthSampler : register(s7);
SamplerState FlowMapSampler : register(s8);
SamplerState FlowMapNormalsSampler : register(s9);
SamplerState SSRReflectionSampler : register(s10);
SamplerState RawSSRReflectionSampler : register(s11);

Texture2D<float4> ReflectionTex : register(t0);
Texture2D<float4> RefractionTex : register(t1);
Texture2D<float4> DisplacementTex : register(t2);
TextureCube<float4> CubeMapTex : register(t3);
Texture2D<float4> Normals01Tex : register(t4);
Texture2D<float4> Normals02Tex : register(t5);
Texture2D<float4> Normals03Tex : register(t6);
Texture2D<float4> DepthTex : register(t7);
Texture2D<float4> FlowMapTex : register(t8);
Texture2D<float4> FlowMapNormalsTex : register(t9);
Texture2D<float4> SSRReflectionTex : register(t10);
Texture2D<float4> RawSSRReflectionTex : register(t11);
// WaterOptics reserves t65 for receiver caustics, t66 for the linear,
// high-resolution foam-coverage mask, and t67 for the game's authored rapid-water
// artwork. The rapid layer is projected in world space and advected by flow so
// it follows tessellated/displaced water rather than a legacy flat overlay.
Texture2D<float> WaterFoamStencil : register(t66);
Texture2D<float4> AuthoredRapidWater : register(t67);

cbuffer PerTechnique : register(b0)
{
	float4 VPOSOffset : packoffset(c0);  // inverse main render target width and height in xy, 0 in zw
	float4 PosAdjust : packoffset(c1);   // inverse framebuffer range in w
	float4 CameraDataWater : packoffset(c2);
	float4 SunDir : packoffset(c3);
	float4 SunColor : packoffset(c4);
}

cbuffer PerMaterial : register(b1)
{
	float4 ShallowColor : packoffset(c0);
	float4 DeepColor : packoffset(c1);
	float4 ReflectionColor : packoffset(c2);
	float4 FresnelRI : packoffset(c3);    // Fresnel amount in x, specular power in z
	float4 BlendRadius : packoffset(c4);  // flowmap scale in y, specular radius in z
	float4 VarAmounts : packoffset(c5);   // Sun specular power in x, reflection amount in y, alpha in z, refraction magnitude in w
	float4 NormalsAmplitude : packoffset(c6);
	float4 WaterParams : packoffset(c7);   // noise falloff in x, reflection magnitude in y, sun sparkle power in z, framebuffer range in w
	float4 FogNearColor : packoffset(c8);  // above water fog amount in w
	float4 FogFarColor : packoffset(c9);
	float4 FogParam : packoffset(c10);      // above water fog distance far in z, above water fog range in w
	float4 DepthControl : packoffset(c11);  // depth reflections factor in x, depth refractions factor in y, depth normals factor in z, depth specular lighting factor in w
	float4 SSRParams : packoffset(c12);     // fWaterSSRIntensity in x, fWaterSSRBlurAmount in y, inverse main render target width and height in zw
	float4 SSRParams2 : packoffset(c13);    // fWaterSSRNormalPerturbationScale in x
}

cbuffer PerGeometry : register(b2)
{
	float4x4 TextureProj : packoffset(c0);
	float4 ReflectPlane[1] : packoffset(c4);
	float4 ProjData : packoffset(c5);
	float4 LightPos[8] : packoffset(c6);
	float4 LightColor[8] : packoffset(c14);
}

#		define SampColorSampler Normals01Sampler
#		define LinearSampler Normals01Sampler

#		if defined(SKY_BOUNCE)
#			include "SkyBounce/SkyBounce.hlsli"
#		endif

#		if defined(ATMOSPHERE_PIPELINE)
#			include "Atmosphere/Atmosphere.hlsli"
#		endif

#		include "Common/ShadowSampling.hlsli"

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
float GetWaterFogFade()
{
#			if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		return Atmosphere::GetVanillaFogFade(PosAdjust.w);
	}
#			endif
	return PosAdjust.w;
}

#			if defined(FLOWMAP)

/**
 * Structure containing complete flowmap information
 */
struct FlowmapData
{
	float4 color;       // Raw flowmap color (R=flow_x, G=flow_y, B=flow_strength, A=flow_mask)
	float2 flowVector;  // Flow vector (coordinate space depends on source function)
};

/**
 * Gets raw flowmap data before UV-space coordinate transformation
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for sampling the flowmap texture
 * @return FlowmapData with raw components:
 *         - color: Raw flowmap texture sample (RG=rotation, B=strength, A=mask)
 *         - flowVector: Base flow vector before any coordinate transformation
 *                      Ready for direct application of rotation matrix for world positioning
 *
 * @details This function provides flowmap data in its original coordinate space, suitable
 *          for world-space positioning effects (like ripple movement). The flowVector has
 *          NOT been transformed for UV-space normal sampling - that transformation is only
 *          applied in GetFlowmapDataUV() which uses transpose for UV coordinate perturbation.
 *
 *          Use this function when you need to apply the rotation matrix directly for
 *          world-space effects without needing to reverse any existing transformations.
 *
 * @see GetFlowmapDataUV() for UV-space normal sampling (applies transpose transformation)
 */
FlowmapData GetFlowmapDataTextureSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data;
	data.color = FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + uvShift, 0);
	data.flowVector = (64 * input.TexCoord3.xy) * sqrt(max(1.01 - data.color.z, 0.0f));
	// NOTE: flowVector is NOT transformed yet - this is the raw vector before rotation matrix
	return data;
}
/**
 * Samples flowmap texture and calculates UV-space flow data for texture sampling
 *
 * @param input Pixel shader input containing texture coordinates and world position data
 * @param uvShift UV offset for sampling the flowmap texture (used for animation/variation)
 * @return FlowmapData Complete flowmap information with UV-space flow vector
 *
 * @details This function:
 *          - Samples the flowmap texture at the specified UV coordinates
 *          - Decodes flow direction from RG channels (remapped from [0,1] to [-1,1])
 *          - Calculates flow strength using the blue channel with sqrt falloff
 *          - Applies transpose rotation matrix to transform flow direction to UV space
 *          - Scales flow vector by world position and strength factors
 *
 * @note Flowmap format:
 *       - Red channel: Flow direction X component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Green channel: Flow direction Y component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Blue channel: Flow strength (0 = no flow, 1 = maximum flow)
 *       - Alpha channel: Flow mask/intensity multiplier
 */
FlowmapData GetFlowmapDataUV(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowSinCos = data.color.xy * 2 - 1;
	float2x2 flowRotationMatrix = float2x2(flowSinCos.x, flowSinCos.y, -flowSinCos.y, flowSinCos.x);
	data.flowVector = mul(transpose(flowRotationMatrix), data.flowVector);
	return data;
}
// ----------------------------------------------------------------
// Flowmap Parallax Functions
// ----------------------------------------------------------------

/**
 * Samples height from flowmap texture using the same 4-sample blend as flowmap normals
 * This ensures height transitions match the normal transitions exactly
 *
 * @param input PS_INPUT for flowmap coordinate access
 * @param normalMul The blend weights from the flowmap system (same as used for normals)
 * @param uvShift The UV shift value (1 / (128 * flowmapDimensions))
 * @param mipLevel Mip level for texture sampling
 */
float GetFlowmapHeightBlended(PS_INPUT input, float2 normalMul, float2 uvShift, float mipLevel)
{
	// Sample height using the EXACT same UV computation as GetFlowmapNormal
	// This ensures the height blending matches the normal blending perfectly

	// Sample 0: uvShift, multiplier=9.92, offset=0
	FlowmapData flowData0 = GetFlowmapDataUV(input, uvShift);
	float2 uv0 = 0 + (flowData0.flowVector - float2(9.92 * ((0.001 * ReflectionColor.w) * flowData0.color.w), 0));
	float height0 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv0, mipLevel).w;

	// Sample 1: float2(0, uvShift.y), multiplier=10.64, offset=0.27
	FlowmapData flowData1 = GetFlowmapDataUV(input, float2(0, uvShift.y));
	float2 uv1 = 0.27 + (flowData1.flowVector - float2(10.64 * ((0.001 * ReflectionColor.w) * flowData1.color.w), 0));
	float height1 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv1, mipLevel).w;

	// Sample 2: 0.0.xx, multiplier=8, offset=0
	FlowmapData flowData2 = GetFlowmapDataUV(input, 0.0.xx);
	float2 uv2 = 0 + (flowData2.flowVector - float2(8 * ((0.001 * ReflectionColor.w) * flowData2.color.w), 0));
	float height2 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv2, mipLevel).w;

	// Sample 3: float2(uvShift.x, 0), multiplier=8.48, offset=0.62
	FlowmapData flowData3 = GetFlowmapDataUV(input, float2(uvShift.x, 0));
	float2 uv3 = 0.62 + (flowData3.flowVector - float2(8.48 * ((0.001 * ReflectionColor.w) * flowData3.color.w), 0));
	float height3 = FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, uv3, mipLevel).w;

	// Use the EXACT same blending formula as flowmap normals
	float blendedHeight =
		normalMul.y * (normalMul.x * height2 + (1 - normalMul.x) * height3) +
		(1 - normalMul.y) * (normalMul.x * height1 + (1 - normalMul.x) * height0);

	return blendedHeight;
}

// Keep this for compatibility - just forwards to the proper function
float GetFlowmapHeightBarycentric(PS_INPUT input, float2 flowmapDimensions, float2 baseUV, float mipLevel)
{
	// This is now unused - we use GetFlowmapHeightBlended directly
	return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, baseUV, mipLevel).w;
}

/**
 * Computes mip level for flowmap texture sampling
 */
float GetFlowmapMipLevel(float2 flowmapUV)
{
	float2 textureDims;
	FlowMapNormalsTex.GetDimensions(textureDims.x, textureDims.y);

	textureDims /= 8.0;

	float2 texCoordsPerSize = flowmapUV * textureDims;
	float2 dxSize = ddx(texCoordsPerSize);
	float2 dySize = ddy(texCoordsPerSize);
	float2 dTexCoords = dxSize * dxSize + dySize * dySize;
	float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);
	return max(0.5 * log2(max(minTexCoordDelta, 1e-8f)), 0);
}

/**
 * Samples height from flowmap texture (riverflow.dds alpha channel)
 * Uses the same UV calculation as GetFlowmapNormal for consistency
 */

/**
 * Generates flowmap-based normal (no parallax - flowmap normals are not parallax-shifted)
 * Uses mip clamping to preserve detail at distance and prevent over-blurring
 */
float3 GetFlowmapNormal(PS_INPUT input, float2 uvShift, float multiplier, float offset)
{
	FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
	float2 uv = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));

	float2 dx = ddx(uv);
	float2 dy = ddy(uv);
	float mipLevel = 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8f));
	mipLevel = clamp(mipLevel + SharedData::MipBias, 0, 5);

	float mipScale = exp2(-mipLevel);
	float2 scaledFlowVector = flowData.flowVector * mipScale;
	float2 scaledUv = offset + (scaledFlowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));

	return float3(FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, scaledUv, mipLevel).xy, flowData.color.z);
}

/**
 * Gets flowmap data with world-space flow vector for positioning effects
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for flowmap sampling (used for animation phases)
 * @return FlowmapData Complete flowmap information with world-space flow vector
 *
 * @details This function:
 *          - Samples raw flowmap data (before UV-space transformations)
 *          - Decodes flow direction from flowmap RG channels
 *          - Applies component-wise directional transformation
 *          - Returns complete flowmap data with world-space flow vector
 *
 * @note Use this for effects that need to move with water current (ripples, debris, foam, etc.)
 *       For UV-space normal sampling, use GetFlowmapDataUV() instead
 */
FlowmapData GetFlowmapDataWorldSpace(PS_INPUT input, float2 uvShift)
{
	FlowmapData data = GetFlowmapDataTextureSpace(input, uvShift);
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}

/**
 * Converts existing texture-space flowmap data to world-space (avoids duplicate sampling)
 *
 * @param textureSpaceData FlowmapData from GetFlowmapDataTextureSpace()
 * @return FlowmapData Complete flowmap data with world-space flow vector
 *
 * @note Use this overload when you already have texture-space flowmap data to avoid duplicate texture sampling
 */
FlowmapData GetFlowmapDataWorldSpace(FlowmapData textureSpaceData)
{
	FlowmapData data = textureSpaceData;
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}
#			endif

#			if defined(LOD)
#				undef WATER_OPTICS
#				undef RAIN_RESPONSE
#			endif

#			if defined(WATER_OPTICS) && !defined(VC)
#				define WATER_PARALLAX
#				include "WaterOptics/WaterParallax.hlsli"
#			endif

#			if defined(WORLD_PROBES)
#				include "WorldProbes/WorldProbes.hlsli"
#			endif

#			if defined(RAIN_RESPONSE)
#				include "RainResponse/RainResponse.hlsli"
#			endif

// Structure to return both normal and ripple/splash color information
struct WaterNormalData
{
	float3 normal;
	float4 rippleInfo;  // xyz = scaled ripple normal (normalized normal * intensity), w = splash effect intensity
};

WaterNormalData GetWaterNormal(PS_INPUT input, float distanceFactor, float normalsDepthFactor, float3 viewDirection, float depth, float wetnessOcclusion)
{
	WaterNormalData result;
	result.rippleInfo = float4(0, 0, 0, 0);

	float3 normalScalesRcp = rcp(max(abs(input.NormalsScale.xyz), 1e-5f.xxx));

#			if defined(WATER_PARALLAX)
	float2 parallaxOffset = WaterOptics::GetParallaxOffset(input, normalScalesRcp);
#			endif

#			if defined(FLOWMAP)
#				if defined(WATERBODY)
	float2 flowmapDimensions = input.TexCoord4.xy;
#				else
	float2 flowmapDimensions = input.TexCoord4.xx;
#				endif
	flowmapDimensions = max(abs(flowmapDimensions), 1.0f.xx);
	float2 uvShift = 1 / (128 * flowmapDimensions);

	// Compute flowmap parallax and create parallaxed input for normal sampling
	PS_INPUT flowmapInput = input;
	float2 flowmapParallaxOffset = float2(0, 0);
#				if defined(WATER_PARALLAX) && !defined(LOD)
	float parallaxAmount = WaterOptics::GetFlowmapParallaxAmount(input, flowmapDimensions, viewDirection);
	float2 parallaxDir = viewDirection.xy / max(-viewDirection.z, 0.075f);
	parallaxDir.y = -parallaxDir.y;
	float viewDotUp = -viewDirection.z;
	parallaxDir *= 0.008 * saturate(viewDotUp * 2.0);
	flowmapInput.TexCoord3.xy = input.TexCoord3.xy + parallaxAmount * parallaxDir;
	flowmapParallaxOffset = WaterOptics::GetFlowmapParallaxOffset(input, flowmapDimensions, viewDirection, normalScalesRcp);
#				endif

	// Calculate cell blend weights using parallaxed input
	float2 normalMul = 0.5 + -(-0.5 + abs(frac(flowmapInput.TexCoord2.zw * (64 * flowmapDimensions)) * 2 - 1));

	// Sample flowmap normals with parallax applied
	float3 flowmapNormal0 = GetFlowmapNormal(flowmapInput, uvShift, 9.92, 0);
	float3 flowmapNormal1 = GetFlowmapNormal(flowmapInput, float2(0, uvShift.y), 10.64, 0.27);
	float3 flowmapNormal2 = GetFlowmapNormal(flowmapInput, 0.0.xx, 8, 0);
	float3 flowmapNormal3 = GetFlowmapNormal(flowmapInput, float2(uvShift.x, 0), 8.48, 0.62);

	float2 flowmapNormalWeighted =
		normalMul.y * (normalMul.x * flowmapNormal2.xy + (1 - normalMul.x) * flowmapNormal3.xy) +
		(1 - normalMul.y) *
			(normalMul.x * flowmapNormal1.xy + (1 - normalMul.x) * flowmapNormal0.xy);
	float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));
	float3 flowmapNormal =
		float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)) *
				   max(0.4, normalsDepthFactor),
			0);
	flowmapNormal.z =
		sqrt(saturate(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y));
	float2 baseNormalUv = input.TexCoord1.xy;
#				if defined(WATER_PARALLAX)
	// Use flowmap-derived parallax offset for base normals
	baseNormalUv += flowmapParallaxOffset.xy * normalScalesRcp.x;
#				endif
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, baseNormalUv, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#			endif  // End of FLOWMAP block

#			if !defined(FLOWMAP)
#				if defined(WATER_PARALLAX)
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy + parallaxOffset.xy * normalScalesRcp.x, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#				else
	float3 normals1 = Normals01Tex.SampleBias(Normals01Sampler, input.TexCoord1.xy, SharedData::MipBias).xyz * 2.0 + float3(-1, -1, -2);
#				endif
#			endif  // End of !FLOWMAP block
#			if defined(FLOWMAP) && !defined(BLEND_NORMALS)
#				ifdef DISABLE_FLOWMAP_NORMALS
	// FLOWMAP NORMALS DISABLED: Using only base normals (flow system still active for ripples/splashes)
	float3 finalNormal = normalize(normals1 + float3(0, 0, 1));
#				else
	// FLOWMAP NORMALS ENABLED: Blending flow-based normals with base normals
	float3 finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), flowmapNormal, distanceFactor));
#				endif
#			elif !defined(LOD)

#				if defined(WATER_PARALLAX)
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw + parallaxOffset.xy * normalScalesRcp.y, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy + parallaxOffset.xy * normalScalesRcp.z, SharedData::MipBias).xyz * 2.0 - 1.0;
#				else
	float3 normals2 = Normals02Tex.SampleBias(Normals02Sampler, input.TexCoord1.zw, SharedData::MipBias).xyz * 2.0 - 1.0;
	float3 normals3 = Normals03Tex.SampleBias(Normals03Sampler, input.TexCoord2.xy, SharedData::MipBias).xyz * 2.0 - 1.0;
#				endif

	float3 blendedNormal = normalize(float3(0, 0, 1) + NormalsAmplitude.x * normals1 +
									 NormalsAmplitude.y * normals2 + NormalsAmplitude.z * normals3);
#				if defined(UNDERWATER)
	float3 finalNormal = blendedNormal;
#				else
	float3 finalNormal = normalize(lerp(float3(0, 0, 1), blendedNormal, normalsDepthFactor));
#				endif

#				if defined(FLOWMAP)
	float normalBlendFactor =
		normalMul.y * ((1 - normalMul.x) * flowmapNormal3.z + normalMul.x * flowmapNormal2.z) +
		(1 - normalMul.y) * (normalMul.x * flowmapNormal1.z + (1 - normalMul.x) * flowmapNormal0.z);
	finalNormal = normalize(lerp(normals1 + float3(0, 0, 1), normalize(lerp(finalNormal, flowmapNormal, normalBlendFactor)), distanceFactor));
#				endif
#			else
	float3 finalNormal =
		normalize(float3(0, 0, 1) + NormalsAmplitude.xxx * normals1);
#			endif

#			if defined(WADING)
#				if defined(FLOWMAP)
	float2 displacementUv = input.TexCoord3.zw;
#				else
	float2 displacementUv = input.TexCoord3.xy;
#				endif
	float3 displacement = normalize(float3(NormalsAmplitude.w * (-0.5 + DisplacementTex.Sample(DisplacementSampler, displacementUv).zw),
		0.04));
	finalNormal = lerp(displacement, finalNormal, displacement.z);
#			endif

#			if defined(RAIN_RESPONSE)
	// Rain Response Debug System:
	// DEBUG_RAIN_RESPONSE Color Legend:
	// - BRIGHT MAGENTA: Ripples, BRIGHT GREEN: Splashes, CYAN: Both effects
	float4 raindropInfo = float4(0, 0, 1, 0);
	float maxRainDropDistance = SharedData::rainResponseSettings.RaindropFxRange * SharedData::rainResponseSettings.RaindropFxRange * 3;
	float rainDropDistance = dot(input.WPosition.xyz, input.WPosition.xyz);
	float distanceFadeout = saturate((1 - saturate(rainDropDistance / maxRainDropDistance)) * 3);
	if (finalNormal.z > 0 && SharedData::rainResponseSettings.Raining > 0.0f && SharedData::rainResponseSettings.EnableRaindropFx &&
		(rainDropDistance < maxRainDropDistance) && wetnessOcclusion > 0.05) {
		float rippleStrengthModifier = (wetnessOcclusion * wetnessOcclusion) * distanceFadeout;
		float3 rippleWPosition = input.WPosition.xyz + finalNormal * 16;
#				if defined(WATER_PARALLAX)
		rippleWPosition.xy += parallaxOffset;
#				endif
#				if defined(FLOWMAP)
		// Flow-following ripple enhancement: Makes raindrops follow water current
		FlowmapData worldFlowData = GetFlowmapDataWorldSpace(input, float2(0, 0));

		// Calculate flow-aware ripple offset using centralized timing logic
		// Parameters: avgFlowmapMultiplier=9.26 (average of GetWaterNormal flowmap normal multipliers: 9.92, 10.64, 8, 8.48)
		// uvToWorldScale=0.125 (1/8 - relates to 64× texture coordinate scaling factor)
		float2 flowOffset = RainResponse::GetFlowAwareRippleOffset(
			worldFlowData.flowVector,
			worldFlowData.color.w,      // Flow strength from flowmap alpha
			0.001 * ReflectionColor.w,  // Reflection timing scale (matches GetFlowmapNormal)
			9.26,                       // Average flowmap normal multiplier
			0.125                       // UV-to-world scale factor (1/8)
		);

		rippleWPosition.xy += flowOffset;
#				endif
		raindropInfo = RainResponse::GetRainDrops(rippleWPosition + FrameBuffer::CameraPosAdjust.xyz, SharedData::rainResponseSettings.Time, finalNormal, rippleStrengthModifier);

		// Calculate ripple and splash color intensities
		float rippleIntensity = length(raindropInfo.xy) * rippleStrengthModifier;
		float splashIntensity = raindropInfo.w * distanceFadeout;

		// Store ripple and splash information for color effects
		result.rippleInfo.xyz = raindropInfo.xyz * rippleIntensity;
		result.rippleInfo.w = splashIntensity;
	}
	float3 rippleNormal = normalize(raindropInfo.xyz);
	finalNormal = ReorientNormal(rippleNormal, finalNormal);
#			endif

	result.normal = finalNormal;
	return result;
}

float3 GetWaterSpecularColor(PS_INPUT input, float3 normal, float3 viewDirection, float distanceFactor, float skyBounceSpecular)
{
	if (!(Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Reflections))
		return ReflectionColor.xyz * VarAmounts.y * clamp(SharedData::waterOpticsSettings.ReflectionBrightness, 0.5f, 1.15f);

	float3 R = reflect(viewDirection, WaterParams.y * normal + float3(0, 0, 1 - WaterParams.y));
	float waterRoughness = 0.0f;
#			if USE_PIXL_WATER_OPTICS
	float normalVariance = max(dot(ddx_coarse(normal), ddx_coarse(normal)), dot(ddy_coarse(normal), ddy_coarse(normal)));
	waterRoughness = clamp(0.045f + sqrt(saturate(normalVariance)) * 0.35f, 0.045f, 0.65f);
#			endif
	float3 reflectionColor = CubeMapTex.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz;

#			if defined(WORLD_PROBES)
	float3 dynamicCubemap;
	if (SharedData::InInterior) {
		dynamicCubemap = WorldProbes::EnvTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz;
	} else {
		float3 specularIrradiance = 1.0;
		if (skyBounceSpecular < 1.0)
			specularIrradiance = Color::IrradianceToLinear(WorldProbes::EnvTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz);

		float3 specularIrradianceReflections = 1.0;
		if (skyBounceSpecular > 0.0)
			specularIrradianceReflections = Color::IrradianceToLinear(WorldProbes::EnvReflectionsTexture.SampleLevel(CubeMapSampler, R, waterRoughness * 7.0f).xyz);

		dynamicCubemap = Color::IrradianceToGamma(lerp(specularIrradiance, specularIrradianceReflections, skyBounceSpecular));
	}

	float reflectionAmount = saturate(length(input.WPosition.xyz) / 1024.0);

	if (SharedData::HideSky)
		reflectionAmount = 0.0;
	reflectionColor = lerp(dynamicCubemap, reflectionColor, reflectionAmount);
#			endif

#			if !defined(LOD) && NUM_SPECULAR_LIGHTS == 0
	float pointingDirection = dot(viewDirection, R) * 0.5 + 0.5;
	float pointingAlignment = dot(reflect(viewDirection, float3(0, 0, 1)), R) * 0.5 + 0.5;
	float ssrAmount = sqrt(saturate(min(pointingAlignment, pointingDirection)));
	float2 ssrReflectionUv = ((FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy) * SSRParams.zw) + 0.05 * normal.xy;
	float2 ssrReflectionUvDR = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(ssrReflectionUv);
	float4 ssrReflectionColorBlurred = SSRReflectionTex.Sample(SSRReflectionSampler, ssrReflectionUvDR);
	float4 ssrReflectionColorRaw = RawSSRReflectionTex.Sample(RawSSRReflectionSampler, ssrReflectionUvDR);
	float4 ssrReflectionColor = lerp(ssrReflectionColorBlurred, ssrReflectionColorRaw, ssrAmount * 0.7);
#			if USE_PIXL_WATER_OPTICS
	ssrReflectionColor = lerp(ssrReflectionColorRaw, ssrReflectionColorBlurred, waterRoughness);
#			endif
	float3 finalSsrReflectionColor = max(0, ssrReflectionColor.xyz);
	float ssrFraction = saturate(ssrReflectionColor.w * distanceFactor * ssrAmount);
#			if USE_PIXL_WATER_OPTICS
	float2 edgeDistance = min(ssrReflectionUvDR, 1.0f.xx - ssrReflectionUvDR);
	float edgeConfidence = smoothstep(0.0f, 0.06f, min(edgeDistance.x, edgeDistance.y));
	float luminanceReference = max(Color::RGBToLuminance(reflectionColor), 0.05f);
	float luminanceSSR = Color::RGBToLuminance(finalSsrReflectionColor);
	float surfaceSsrStrength = SharedData::waterOpticsSettings.EnableEnhancedSSR != 0 ?
		clamp(SharedData::waterOpticsSettings.SurfaceSSRStrength, 0.0f, 2.5f) : 1.0f;
	// Keep night reflections tied to the fallback environment instead of allowing
	// a bright screen-space source to exceed it by the former fixed 8x ceiling.
	float ssrLuminanceCeiling = lerp(2.25f, 3.75f, saturate((surfaceSsrStrength - 1.0f) / 1.5f));
	finalSsrReflectionColor *= min(1.0f, (luminanceReference * ssrLuminanceCeiling) / max(luminanceSSR, 1e-4f));
	// Boost confidence rather than raw radiance, preserving reflected colour and
	// the cubemap fallback while making valid on-screen detail more authoritative.
	ssrFraction = (1.0f - exp2(-ssrFraction * surfaceSsrStrength * 1.25f)) * edgeConfidence * saturate(1.0f - waterRoughness * 0.50f);
#			endif
	reflectionColor = lerp(reflectionColor, finalSsrReflectionColor, ssrFraction);
#			endif

	// The cubemap remains useful at distance, but an unattenuated HDR sample can
	// read as a bright strip through atmospheric fog. Fade only the far-water
	// reflection contribution; refraction and the fog composite remain unchanged.
	float farWaterReflectionFade = lerp(
		1.0f,
		0.72f,
		smoothstep(1800.0f, 7000.0f, length(input.WPosition.xyz)));
	return reflectionColor * farWaterReflectionFade *
		clamp(SharedData::waterOpticsSettings.ReflectionBrightness, 0.5f, 1.15f);
}

float GetScreenDepthWater(float2 screenPosition)
{
	float depth = DepthTex.Load(float3(screenPosition, 0)).x;
	float denominator = -depth * CameraDataWater.z + CameraDataWater.x;
	denominator = denominator >= 0.0f ? max(denominator, 1e-6f) : min(denominator, -1e-6f);
	return CameraDataWater.w / denominator;
}

float3 GetLdotN(float3 normal)
{
#			if defined(UNDERWATER)
	return 1;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 1;
	return saturate(dot(SunDir.xyz, normal));
#			endif
}

float GetFresnelValue(float3 normal, float3 viewDirection)
{
#			if defined(UNDERWATER)
	float3 actualNormal = -normal;
#			else
	float3 actualNormal = normal;
#			endif
	float viewAngle = 1 - saturate(dot(-viewDirection, actualNormal));
#			if USE_PIXL_WATER_OPTICS
	float dielectricF0 = clamp(FresnelRI.x, 0.0204f, 0.08f);
	return dielectricF0 + (1.0f - dielectricF0) * BRDF::Pow5(viewAngle);
#			else
	return (1 - FresnelRI.x) * pow(viewAngle, 5) + FresnelRI.x;
#			endif
}

struct DiffuseOutput
{
	float3 refractionColor;
	float3 refractionDiffuseColor;
	float depth;
	float refractionMul;
	float3 refractedViewDirection;
	float receiverDistance;
};

DiffuseOutput GetWaterDiffuseColor(PS_INPUT input, float3 normal, float3 viewDirection, inout float4 distanceMul, float refractionsDepthFactor, float fresnel, float3 viewPosition, float depth)
{
#			if defined(REFRACTIONS)
	float4 refractionNormal = mul(transpose(TextureProj), float4((VarAmounts.w * refractionsDepthFactor * normal.xy) + input.MPosition.xy, input.MPosition.z, 1));

	float refractionW = refractionNormal.w >= 0.0f ? max(refractionNormal.w, 1e-6f) : min(refractionNormal.w, -1e-6f);
	float2 refractionUvRaw = float2(refractionNormal.x, refractionNormal.w - refractionNormal.y) / refractionW;
	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

	float2 refractionScreenPosition = FrameBuffer::DynamicResolutionParams1.xy * (refractionUvRaw / VPOSOffset.xy);
	float viewZ = viewPosition.z >= 0.0f ? max(viewPosition.z, 1e-6f) : min(viewPosition.z, -1e-6f);
	float4 refractionWorldPosition = float4(input.WPosition.xyz * depth / viewZ, 0);

#				if defined(DEPTH) && !defined(VERTEX_ALPHA_DEPTH)
	float refractionDepth = GetScreenDepthWater(refractionScreenPosition);
	depth = refractionDepth;
	float2 safeProj = max(abs(ProjData.xy), 1e-6f.xx);
	float refractionDepthMul = length(float3((((VPOSOffset.zw + refractionUvRaw) * 2 - 1)) * refractionDepth / safeProj, refractionDepth));

	float3 refractionDepthAdjustedViewDirection = -viewDirection * refractionDepthMul;
	float refractionViewSurfaceAngle = dot(refractionDepthAdjustedViewDirection, ReflectPlane[0].xyz);

	float safeRefractionAngle = refractionViewSurfaceAngle >= 0.0f ? max(refractionViewSurfaceAngle, 1e-6f) : min(refractionViewSurfaceAngle, -1e-6f);
	float refractionPlaneMul = (1 - ReflectPlane[0].w / safeRefractionAngle);

	if (refractionPlaneMul < 0.0) {
		refractionUvRaw = FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
	} else {
		distanceMul = saturate(refractionPlaneMul * float4(length(refractionDepthAdjustedViewDirection).xx, abs(refractionViewSurfaceAngle).xx) / max(abs(FogParam.z), 1e-5f));

		refractionWorldPosition = mul(FrameBuffer::CameraViewProjInverse, float4((refractionUvRaw * 2 - 1) * float2(1, -1), DepthTex.Load(float3(refractionScreenPosition, 0)).x, 1));
		float worldW = refractionWorldPosition.w >= 0.0f ? max(refractionWorldPosition.w, 1e-6f) : min(refractionWorldPosition.w, -1e-6f);
		refractionWorldPosition.xyz /= worldW;
	}

#					if defined(HORIZON_BLEND)
	if (DepthTex.Load(float3(refractionScreenPosition, 0)).x >= HorizonBlend::EmptyDepthThreshold)
		distanceMul = 1.0.xxxx;
#					endif
#				endif

	float2 refractionUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(refractionUvRaw);
	float3 refractionColor = RefractionTex.Sample(RefractionSampler, refractionUV).xyz;
	float3 refractionDiffuseColor = lerp(Color::Water(ShallowColor.xyz), Color::Water(DeepColor.xyz), distanceMul.y);
#				if USE_PIXL_WATER_OPTICS
	// Preserve Skyrim's refracted scene detail and use the authored deep colour
	// only as a relative absorption tint. Treating the gamma-space deep colour
	// itself as Beer-Lambert transmittance collapsed canals into a flat green fill.
	float3 deepTint = max(Color::Water(DeepColor.xyz), 0.001f.xxx);
	float maxTint = max(max(deepTint.r, deepTint.g), max(deepTint.b, 0.02f));
	float3 relativeTint = saturate(deepTint / maxTint);
	float3 absorption = 1.0f.xxx - relativeTint;
	float tintStrength = saturate(SharedData::waterOpticsSettings.WaterTintStrength);
	float opticalDepth = saturate(distanceMul.y) * lerp(0.20f, 1.10f, tintStrength);
	float3 transmittance = exp2(-absorption * opticalDepth);
	float inScatter = (1.0f - exp2(-opticalDepth * 0.50f)) * tintStrength * 0.45f;
	refractionColor = lerp(refractionColor * transmittance, refractionDiffuseColor, inScatter);
#				endif

#				if defined(UNDERWATER)
	float refractionMul = 0;
#				else
	float refractionMul = 1 - pow(saturate((-distanceMul.x * FogParam.z + FogParam.z) / max(abs(FogParam.w), 1e-5f)), max(FogNearColor.w, 0.0f));
#				endif

	DiffuseOutput output;
	output.refractionColor = refractionColor;
	output.refractionDiffuseColor = refractionDiffuseColor;
	output.depth = depth;
	output.refractionMul = refractionMul;
	output.refractedViewDirection = normalize(refractionWorldPosition.xyz - input.WPosition.xyz);
	output.receiverDistance = length(refractionWorldPosition.xyz - input.WPosition.xyz);
	return output;
#			else
	DiffuseOutput output;
	output.refractionColor = lerp(Color::Water(ShallowColor.xyz), Color::Water(DeepColor.xyz), fresnel) * GetLdotN(normal);
	output.refractionDiffuseColor = output.refractionColor;
	output.depth = 1;
	output.refractionMul = 1;
	output.refractedViewDirection = viewDirection;
	output.receiverDistance = 0;
	return output;
#			endif
}

float GetDynamicSurfaceCausticFocus(float3 normal, float3 worldPosition, float receiverDistance)
{
	// The opaque Lighting pass cannot see Skyrim's animated water-normal SRVs:
	// those resources are material-local t4-t6 bindings in this Water pass. Build
	// a bounded first-order refractive footprint here instead, after the complete
	// wave, flowmap and rain-ripple normal has been composed by GetWaterNormal().
	float sunLengthSq = dot(SunDir.xyz, SunDir.xyz);
	float3 sunDirection = SunDir.xyz * rsqrt(max(sunLengthSq, 1e-8f));
	float3 transmittedRay = refract(-sunDirection, normal, 0.750187f); // air -> water
	float safeRayDepth = max(-transmittedRay.z, 0.15f);
	float2 raySlope = transmittedRay.xy / safeRayDepth;

	// Convert screen-space ray-slope derivatives to the local horizontal water
	// basis. The determinant guard keeps edge-on/degenerate quads finite.
	float2 dPdx = ddx_coarse(worldPosition.xy);
	float2 dPdy = ddy_coarse(worldPosition.xy);
	float2 dSdx = ddx_coarse(raySlope);
	float2 dSdy = ddy_coarse(raySlope);
	float basisDet = dPdx.x * dPdy.y - dPdx.y * dPdy.x;
	float safeBasisDet = abs(basisDet) > 1e-5f ? basisDet : (basisDet < 0.0f ? -1e-5f : 1e-5f);
	float invBasisDet = rcp(safeBasisDet);

	float2 slopeGradientX = float2(
		(dSdx.x * dPdy.y - dSdy.x * dPdx.y) * invBasisDet,
		(dSdy.x * dPdx.x - dSdx.x * dPdy.x) * invBasisDet);
	float2 slopeGradientY = float2(
		(dSdx.y * dPdy.y - dSdy.y * dPdx.y) * invBasisDet,
		(dSdy.y * dPdx.x - dSdx.y * dPdy.x) * invBasisDet);

	// The Jacobian measures the area change of the refracted solar footprint at
	// the visible receiver. A reciprocal area above one is focusing; below one is
	// spreading. Clamp both depth and response to avoid singular highlights.
	float focusDepth = min(max(receiverDistance, 0.0f), 768.0f);
	float j00 = 1.0f + focusDepth * slopeGradientX.x;
	float j01 = focusDepth * slopeGradientX.y;
	float j10 = focusDepth * slopeGradientY.x;
	float j11 = 1.0f + focusDepth * slopeGradientY.y;
	float footprintArea = abs(j00 * j11 - j01 * j10);
	float focusedLight = clamp(rcp(max(footprintArea, 0.55f)), 0.72f, 1.55f);

	float validBasis = abs(basisDet) > 1e-5f ? 1.0f : 0.0f;
	float daylight = smoothstep(0.04f, 0.28f, sunDirection.z) * (sunLengthSq > 1e-8f ? 1.0f : 0.0f);
	float validTransmission = smoothstep(0.02f, 0.17f, -transmittedRay.z);
	float depthFade = smoothstep(6.0f, 48.0f, focusDepth) * (1.0f - smoothstep(512.0f, 768.0f, focusDepth));
	float focusStrength = saturate(SharedData::waterOpticsSettings.CausticsFocus * 0.42f) *
		saturate(SharedData::waterOpticsSettings.CausticsStrength * 0.65f) *
		saturate(SharedData::waterOpticsSettings.CausticsVisibility * 0.80f) *
		validBasis * validTransmission * daylight * depthFade;
	return lerp(1.0f, focusedLight, focusStrength);
}

float GetDynamicWaterFoam(PS_INPUT input, float3 surfaceNormal)
{
#			if !defined(REFRACTIONS) || defined(LOD) || defined(SIMPLE) || defined(UNDERWATER) || (defined(SPECULAR) && NUM_SPECULAR_LIGHTS != 0)
	return 0.0f;
#			else
	if (SharedData::waterOpticsSettings.EnableDynamicFoam == 0)
		return 0.0f;

	float3 absolutePosition = input.WPosition.xyz + FrameBuffer::CameraPosAdjust.xyz;
	float foamScale = max(SharedData::waterOpticsSettings.FoamScale, 0.5f);
	float2 flowDirection = 0.0f.xx;
	float flowStrength = 0.0f;
	float flowChange = 0.0f;

#			if defined(FLOWMAP)
	float4 flowSample = FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw, 0.0f);
	float2 decodedFlow = -(flowSample.xy * 2.0f - 1.0f);
	float decodedLengthSq = dot(decodedFlow, decodedFlow);
	if (decodedLengthSq > 1e-6f)
		flowDirection = decodedFlow * rsqrt(decodedLengthSq);
	flowStrength = saturate(flowSample.w * sqrt(max(1.01f - flowSample.z, 0.0f)));

	// Fixed flowmap-texel differences identify bends, convergence and colliding
	// currents without introducing a screen-size or camera-orientation dependency.
	uint flowWidth;
	uint flowHeight;
	FlowMapTex.GetDimensions(flowWidth, flowHeight);
	float2 flowTexel = rcp(max(float2(flowWidth, flowHeight), 1.0f.xx));
	float2 flowX = -(FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + float2(flowTexel.x, 0.0f), 0.0f).xy * 2.0f - 1.0f);
	float2 flowY = -(FlowMapTex.SampleLevel(FlowMapSampler, input.TexCoord2.zw + float2(0.0f, flowTexel.y), 0.0f).xy * 2.0f - 1.0f);
	flowChange = saturate((length(flowX - decodedFlow) + length(flowY - decodedFlow)) * 1.8f) * flowStrength;
#			endif

	// Measure the opaque receiver immediately behind this water pixel. The former
	// refracted-ray distance could become nearly constant over a coarse Skyrim water
	// quad, exposing the mesh as large rectangular foam tiles. Unrefracted per-pixel
	// scene depth creates a continuous contact band on the water surface instead.
	float2 screenPosition =
		FrameBuffer::DynamicResolutionParams1.xy *
		(FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);
	float sceneViewDepth = abs(GetScreenDepthWater(screenPosition));
	float waterViewDepth = abs(mul(FrameBuffer::CameraView, float4(input.WPosition.xyz, 1.0f)).z);
	float receiverSeparation = max(sceneViewDepth - waterViewDepth, 0.0f);
	float validReceiver = (sceneViewDepth < 1000000.0f && sceneViewDepth >= waterViewDepth) ? 1.0f : 0.0f;
	float shallowContact =
		(1.0f - smoothstep(5.0f, 92.0f, receiverSeparation)) * validReceiver;

	// Sample two differently oriented, flow-advected layers. Derivative-aware
	// sampling selects the generated mip chain and keeps thin stencil filaments
	// stable under TAA/DLSS rather than crawling at distance.
	float2 flowAdvection =
		flowDirection * SharedData::Timer * lerp(0.012f, 0.052f, flowStrength);
	float2 foamUv0 = absolutePosition.xy * (0.0045f * foamScale) - flowAdvection;
	float2 rotatedPosition = float2(
		absolutePosition.x * 0.819152f - absolutePosition.y * 0.573576f,
		absolutePosition.x * 0.573576f + absolutePosition.y * 0.819152f);
	float2 foamUv1 = rotatedPosition * (0.0078f * foamScale) - flowAdvection.yx * float2(-0.73f, 0.73f);
	float stencil0 = WaterFoamStencil.SampleGrad(
		SampColorSampler, foamUv0, ddx_coarse(foamUv0), ddy_coarse(foamUv0));
	float stencil1 = WaterFoamStencil.SampleGrad(
		SampColorSampler, foamUv1, ddx_coarse(foamUv1), ddy_coarse(foamUv1));
	float foamStencil = smoothstep(0.18f, 0.70f, stencil0 * 0.68f + stencil1 * 0.32f);

	float surfaceActivity = saturate(
		length(surfaceNormal.xy) * 4.25f +
		flowStrength * 0.45f +
		flowChange * 1.70f);
	float contactActivity = smoothstep(0.06f, 0.55f, surfaceActivity);
	float contactFoam = shallowContact * contactActivity;
	float convergenceFoam = flowChange * smoothstep(0.22f, 0.75f, foamStencil);

	// Re-project the vanilla whitewater artwork onto the current water surface.
	// Two phase-shifted samples preserve the authored breakup while avoiding a
	// rigid, camera-relative decal. The flow mask suppresses it on still lakes;
	// strong flow and convergence make rapids visible without requiring users to
	// disable the game's water effects.
	float2 rapidAdvection = flowDirection * SharedData::Timer * lerp(0.018f, 0.082f, flowStrength);
	float2 rapidUv0 = absolutePosition.xy * (0.0105f * foamScale) - rapidAdvection;
	float2 rapidUv1 = absolutePosition.xy * (0.0165f * foamScale) + rapidAdvection.yx * float2(-0.62f, 0.62f) + 0.37f;
	float rapid0 = dot(AuthoredRapidWater.SampleGrad(
		SampColorSampler, rapidUv0, ddx_coarse(rapidUv0), ddy_coarse(rapidUv0)).rgb, 0.333333f.xxx);
	float rapid1 = dot(AuthoredRapidWater.SampleGrad(
		SampColorSampler, rapidUv1, ddx_coarse(rapidUv1), ddy_coarse(rapidUv1)).rgb, 0.333333f.xxx);
	float rapidPattern = smoothstep(0.26f, 0.72f, rapid0 * 0.62f + rapid1 * 0.38f);
	float rapidActivity = smoothstep(0.16f, 0.62f, flowStrength) *
		saturate(0.38f + flowChange * 1.45f);
	float authoredRapidFoam = rapidPattern * rapidActivity;

	// Foam is now entirely water-owned: no player-centred projected wake and no
	// camera-relative component. Flow/contact coverage is applied as a pixel layer.
	float foam = max(contactFoam * 0.82f + convergenceFoam * 0.58f, authoredRapidFoam * 0.92f);
	return saturate(
		foam * lerp(0.22f, 1.0f, foamStencil) *
		SharedData::waterOpticsSettings.FoamStrength);
#			endif
}

float3 GetSunColor(float3 normal, float3 viewDirection, float3 worldPosition)
{
#			if defined(UNDERWATER)
	return 0.0.xxx;
#			else
	if (Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)
		return 0.0.xxx;

	float3 reflectionDirection = reflect(viewDirection, normal);
	float sunAlignment = saturate(dot(reflectionDirection, normalize(SunDir.xyz)));
	float reflectionMul = exp2(VarAmounts.x * log2(max(sunAlignment, 1e-6f)));
#			if USE_PIXL_WATER_OPTICS
	// Flat water produces a compact directional sun glint, while choppy water
	// widens and softens it. Use screen-space normal variance as a cheap analytic
	// footprint so the new lobe gains definition without turning high-frequency
	// ripples into temporal fireflies. The authored Skyrim lobe remains the broad
	// base and the combined response is energy bounded.
	float normalVariance = max(
		dot(ddx_coarse(normal), ddx_coarse(normal)),
		dot(ddy_coarse(normal), ddy_coarse(normal)));
	float surfaceRoughness = clamp(
		0.035f + sqrt(saturate(normalVariance)) * 0.20f,
		0.035f,
		0.42f);
	float roughnessFactor = saturate((surfaceRoughness - 0.035f) / 0.385f);
	float tightExponent = lerp(1400.0f, 96.0f, roughnessFactor);
	float tightGlint = exp2(tightExponent * log2(max(sunAlignment, 1e-6f)));
	float NdotV = saturate(dot(-viewDirection, normal));
	float dielectricF0 = clamp(FresnelRI.x, 0.0204f, 0.08f);
	float fresnel = dielectricF0 + (1.0f - dielectricF0) * BRDF::Pow5(1.0f - NdotV);
	float authoredLobe = reflectionMul * lerp(1.0f, 0.78f, roughnessFactor);
	float directionalGlint = tightGlint * lerp(0.55f, 1.05f, fresnel);
	reflectionMul = min(max(authoredLobe, directionalGlint), 1.05f);
#			endif

	float llDirLightMult = (SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear) ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
	float3 sunColor = Color::DirectionalLight((SunColor.xyz * SunDir.w) / max(llDirLightMult, 1e-5), SharedData::linearLightCoreSettings.isDirLightLinear) * (1.0 - exp(-DeepColor.w)) * llDirLightMult;
#				if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		sunColor *= Atmosphere::GetSunlightFogAttenuation(worldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#				endif
	return reflectionMul * sunColor;
#			endif
}
#		endif

#		if defined(RADIANT_GRID)
#			include "RadiantGrid/RadiantGrid.hlsli"
#		endif

#		if defined(NATURAL_LIGHTING) && defined(RADIANT_GRID)
#			include "NaturalLighting/NaturalLighting.hlsli"
#		endif

#		if defined(AMBIENT_PROBE)
#			include "AmbientProbe/AmbientProbe.hlsli"
#		endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 screenPosition = FrameBuffer::DynamicResolutionParams1.xy * (FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy);

#		if defined(SIMPLE) || defined(UNDERWATER) || defined(LOD) || defined(SPECULAR)
	float3 viewDirection = normalize(input.WPosition.xyz);

	float waterFadeRange = WaterParams.x - 8192.0f;
	waterFadeRange = waterFadeRange >= 0.0f ? max(waterFadeRange, 1e-5f) : min(waterFadeRange, -1e-5f);
	float distanceFactor = saturate(lerp(FrameBuffer::FrameParams.w, 1, (length(input.WPosition.xyz) - 8192) / waterFadeRange));
	float4 distanceMul = saturate(lerp(VarAmounts.z, 1, -(distanceFactor - 1))).xxxx;
	float distanceBlendFactor = distanceFactor;
#			if defined(WATERBODY)
	distanceBlendFactor = 1.0f;
#			endif

	bool isSpecular = false;

	float depth = 0;

#			if defined(DEPTH)
#				if defined(VERTEX_ALPHA_DEPTH)
#					if defined(VC)
	distanceMul = saturate(input.TexCoord3.z);
#					endif
#				else
	distanceMul = 0;

	depth = GetScreenDepthWater(screenPosition);
	float2 depthOffset =
		FrameBuffer::DynamicResolutionParams2.xy * input.HPosition.xy * VPOSOffset.xy + VPOSOffset.zw;
	float depthMul = length(float3((depthOffset * 2 - 1) * depth / max(abs(ProjData.xy), 1e-6f.xx), depth));
	float3 depthAdjustedViewDirection = -viewDirection * depthMul;
	float viewSurfaceAngle = dot(depthAdjustedViewDirection, ReflectPlane[0].xyz);

	float safeViewSurfaceAngle = viewSurfaceAngle >= 0.0f ? max(viewSurfaceAngle, 1e-6f) : min(viewSurfaceAngle, -1e-6f);
	float planeMul = (1 - ReflectPlane[0].w / safeViewSurfaceAngle);
	distanceMul = saturate(
		planeMul * float4(length(depthAdjustedViewDirection).xx, abs(viewSurfaceAngle).xx) /
		max(abs(FogParam.z), 1e-5f));

#					if defined(HORIZON_BLEND)
	if (DepthTex.Load(float3(screenPosition, 0)).x >= HorizonBlend::EmptyDepthThreshold)
		distanceMul = 1.0.xxxx;
#					endif
#				endif
#			endif

#			if defined(UNDERWATER)
	float4 depthControl = float4(0, 1, 1, 0);
#			elif defined(LOD)
	float4 depthControl = float4(1, 0, 0, 1);
#			elif defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float4 depthControl = float4(0, 0, 1, 0);
#			else
	float4 depthControl = DepthControl * (distanceMul - 1) + 1;
#			endif
	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);

#			if defined(SKY_BOUNCE)
	float wetnessOcclusion = 1.0;

	float3 positionMSSkyBounce = input.WPosition.xyz;

	sh2 skyBounceSH = SkyBounce::SampleNoBias(positionMSSkyBounce);
	float skyBounce = SphericalHarmonics::Unproject(skyBounceSH, float3(0, 0, 1));

	float skyBounceDiffuse = SkyBounce::EvaluateDiffuse(skyBounceSH, float3(0, 0, 1), SkyBounce::GetFadeOutFactor(input.WPosition.xyz));

	wetnessOcclusion = inWorld ? pow(saturate(skyBounce), 2) : 0;
#			endif

#			if defined(SKY_BOUNCE)
	WaterNormalData waterData = GetWaterNormal(input, distanceBlendFactor, depthControl.z, viewDirection, depth, wetnessOcclusion);
#			else
	WaterNormalData waterData = GetWaterNormal(input, distanceBlendFactor, depthControl.z, viewDirection, depth, inWorld);
#			endif

	float3 normal = waterData.normal;
    // Recover the large-scale surface orientation from actual rasterized
    // geometry. Flat water is unchanged; displaced water tilts the existing
    // PIXL ripple normal so Fresnel and reflections follow the wave surface.
    float3 surfaceCross = cross(ddx(input.WPosition.xyz), ddy(input.WPosition.xyz));
    float surfaceLengthSq = dot(surfaceCross, surfaceCross);
    float3 surfaceNormal = surfaceLengthSq > 1e-12 ? surfaceCross * rsqrt(surfaceLengthSq) : float3(0, 0, 1);
    surfaceNormal *= surfaceNormal.z < 0 ? -1 : 1;
    float3 tilt = float3(-surfaceNormal.y, surfaceNormal.x, 0);
    normal = normalize(normal + cross(tilt, normal) + cross(tilt, cross(tilt, normal)) / max(1 + surfaceNormal.z, 1e-4));
#			if USE_PIXL_WATER_OPTICS
	float mainNormalVariance = max(dot(ddx_coarse(normal), ddx_coarse(normal)), dot(ddy_coarse(normal), ddy_coarse(normal)));
	// Preserve readable night reflections while still widening highlights where
	// the normal field genuinely becomes turbulent. The earlier 0.65 ceiling
	// blurred calm canals into a flat refraction colour.
	float pixlWaterRoughness = clamp(0.035f + sqrt(saturate(mainNormalVariance)) * 0.20f, 0.035f, 0.42f);
#			endif

#			if defined(SKY_BOUNCE)
	sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normal, -viewDirection, 0.0);
	float skyBounceSpecular = SkyBounce::EvaluateSpecular(skyBounceSH, specularLobe, SkyBounce::GetFadeOutFactor(input.WPosition.xyz));
#			endif

	float fresnel = GetFresnelValue(normal, viewDirection);

#			if defined(SPECULAR) && (NUM_SPECULAR_LIGHTS != 0)
	float3 finalColor = 0.0.xxx;

#				if !defined(RADIANT_GRID)
	[unroll] for (int lightIndex = 0; lightIndex < NUM_SPECULAR_LIGHTS; ++lightIndex)
	{
		float3 lightVector = LightPos[lightIndex].xyz - (PosAdjust.xyz + input.WPosition.xyz);
		float3 lightDirection = normalize(normalize(lightVector) - viewDirection);
		float lightFade = saturate(length(lightVector) / max(abs(LightPos[lightIndex].w), 1e-5f));
		float lightColorMul = (1 - lightFade * lightFade);
		float LdotN = saturate(dot(lightDirection, normal));
		float3 lightColor = (Color::PointLight(LightColor[lightIndex].xyz) * pow(LdotN, FresnelRI.z)) * lightColorMul;
		finalColor += lightColor;
	}
#				endif

	finalColor *= fresnel;
#				if defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override specular color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorSpecular(waterData.rippleInfo, 2.5, 4.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#				endif

	isSpecular = true;
#			else

#				if defined(SKY_BOUNCE)
	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, skyBounceSpecular);
#				else
	float3 specularColor = GetWaterSpecularColor(input, normal, viewDirection, distanceFactor, 1.0);
#				endif

	DiffuseOutput diffuseOutput = GetWaterDiffuseColor(input, normal, viewDirection, distanceMul, depthControl.y, fresnel, viewPosition, depth);

#				if USE_PIXL_WATER_OPTICS && defined(REFRACTIONS) && !defined(UNDERWATER)
	float dynamicCausticFocus = GetDynamicSurfaceCausticFocus(normal, input.WPosition.xyz, diffuseOutput.receiverDistance);
	float dynamicCausticsEnabled =
		(SharedData::waterOpticsSettings.EnableEnhancedCaustics != 0 &&
		 !(Permutation::PixelShaderDescriptor & Permutation::WaterFlags::Interior)) ? 1.0f : 0.0f;
	// Modulate only the visible refracted receiver. Reflections, fog, water tint,
	// authored receiver caustics and the alpha/fresnel contract remain unchanged.
	diffuseOutput.refractionColor *= lerp(1.0f, dynamicCausticFocus, dynamicCausticsEnabled);
#				endif

	float surfaceShadow;
	float dirShadow = ShadowSampling::Get3DFilteredShadow(input.WPosition.xyz, diffuseOutput.refractedViewDirection, input.HPosition.xy, surfaceShadow);

	float3 dirColor;
	float3 ambientColor;
	ShadowSampling::ExtractLighting(diffuseOutput.refractionDiffuseColor, dirColor, ambientColor);

	dirColor *= dirShadow;

#				if defined(SKY_BOUNCE)
	ambientColor = Color::IrradianceToLinear(ambientColor);
	ambientColor *= skyBounceDiffuse;
	ambientColor = Color::IrradianceToGamma(ambientColor);
#				endif

	diffuseOutput.refractionDiffuseColor = dirColor + ambientColor;

	float3 diffuseColor = lerp(diffuseOutput.refractionColor, diffuseOutput.refractionDiffuseColor, diffuseOutput.refractionMul);

	depthControl = DepthControl * (distanceMul - 1) + 1;

	float3 specularLighting = 0;

#				if defined(RADIANT_GRID)
	uint lightCount = 0;

	uint clusterIndex = 0;
	if (RadiantGrid::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = RadiantGrid::lightGrid[clusterIndex].lightCount;
		uint lightOffset = RadiantGrid::lightGrid[clusterIndex].offset;
		[loop] for (uint i = 0; i < lightCount; i++)
		{
			uint clusteredLightIndex = RadiantGrid::lightList[lightOffset + i];
			RadiantGrid::Light light = RadiantGrid::lights[clusteredLightIndex];
			if (RadiantGrid::IsLightIgnored(light) || light.lightFlags & RadiantGrid::LightFlags::Shadow) {
				continue;
			}

			float3 lightDirection = light.positionWS.xyz - input.WPosition.xyz;
			float lightDist = length(lightDirection);

#					if defined(NATURAL_LIGHTING)
			float intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
#					else
			float intensityFactor = saturate(lightDist / max(light.radius, 1e-5f));
			float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#					endif

			float3 normalizedLightDirection = normalize(lightDirection);

			float3 H = normalize(normalizedLightDirection - viewDirection);
			float HdotN = saturate(dot(H, normal));

			const bool isPointLightLinear = light.lightFlags & RadiantGrid::LightFlags::Linear;
			float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * pow(HdotN, FresnelRI.z) * light.fade;
#			if USE_PIXL_WATER_OPTICS
			float NdotL = saturate(dot(normal, normalizedLightDirection));
			float NdotV = saturate(dot(normal, -viewDirection));
			float VdotH = saturate(dot(-viewDirection, H));
			float3 F = BRDF::F_Schlick(clamp(FresnelRI.x, 0.0204f, 0.08f).xxx, VdotH);
			float D = BRDF::D_GGX(pixlWaterRoughness, HdotN);
			float Vis = BRDF::Vis_SmithJointApprox(pixlWaterRoughness, NdotV, NdotL);
			lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * D * Vis * F * NdotL * light.fade;
#			endif
			specularLighting += lightColor * intensityMultiplier;
		}
	}
	specularColor += specularLighting * 3;
#				endif

#				if defined(UNDERWATER)
	float3 horizonResolvedFogColor = Color::Fog(FogFarColor.xyz);
	float3 finalSpecularColor = lerp(Color::Water(ShallowColor.xyz), specularColor, 0.5);
#				if USE_PIXL_WATER_OPTICS
	float underwaterDistance = length(input.WPosition.xyz) * GAME_UNIT_TO_M;
	float3 underwaterScatter = max(Color::Water(DeepColor.xyz), 0.001f.xxx);
	float3 underwaterAbsorption = -log(max(underwaterScatter, 0.02f.xxx));
	float3 underwaterTransmittance = exp(-underwaterAbsorption * underwaterDistance * 0.035f);
	float3 underwaterSurface = (1.0f - fresnel) * diffuseColor + fresnel * finalSpecularColor;
	float3 finalColor = underwaterSurface * underwaterTransmittance + underwaterScatter * (1.0f.xxx - underwaterTransmittance);
#				else
	float3 finalColor = saturate(1 - length(input.WPosition.xyz) * 0.002) * ((1 - fresnel) * (diffuseColor - finalSpecularColor)) + finalSpecularColor;
#				endif
	// Add ripple and splash color effects for underwater
#					if defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization (darker for underwater)
	float3 debugColor = RainResponse::GetDebugWetnessColorUnderwater(waterData.rippleInfo, 1.5, 2.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#					endif
#				else

	float3 sunColor = GetSunColor(normal, viewDirection, input.WPosition.xyz) * surfaceShadow;
	float dynamicFoam = GetDynamicWaterFoam(input, normal);
	float3 dynamicFoamColor = Color::Water(lerp(float3(0.62f, 0.68f, 0.69f), float3(0.88f, 0.91f, 0.90f), saturate(SunColor.w)));

#					if defined(VC)
	float specularFraction = lerp(1, fresnel * diffuseOutput.refractionMul, distanceBlendFactor);
	float3 finalColorPreFog = lerp(diffuseColor, specularColor, specularFraction) + sunColor * depthControl.w;
	finalColorPreFog = lerp(finalColorPreFog, dynamicFoamColor, dynamicFoam * (1.0f - fresnel * 0.45f));

#						if !defined(WATERBODY)
	float fogDistanceFactor = input.FogParam.w;
	float3 fogColor = Color::Fog(input.FogParam.xyz);
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(length(input.WPosition.xyz) * FogParam.y - FogParam.x), FresnelRI.y));
	float3 fogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor));
#						endif

	fogDistanceFactor = Color::FogAlpha(fogDistanceFactor);

#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		fogColor = AmbientProbe::GetFogAmbientColor(fogColor);
	}
#						endif
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		float4 atmosphere = Atmosphere::GetAtmosphere(input.WPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, fogColor, float4(input.HPosition.xy * FrameBuffer::DynamicResolutionParams2.xy, input.HPosition.z, 1));
		if (Atmosphere::ShouldDisableVanillaFog()) {
			fogColor = atmosphere.xyz;
			fogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, fogColor, atmosphere.w);
		} else {
			fogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
			float3 expFogColor = atmosphere.xyz * GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, expFogColor, atmosphere.w);
		}
	} else {
		fogColor *= GetWaterFogFade();
		finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
	}
#						else
	fogColor *= GetWaterFogFade();
	finalColorPreFog = lerp(finalColorPreFog, fogColor, fogDistanceFactor);
#						endif

	float3 finalColor = finalColorPreFog;
	horizonResolvedFogColor = fogColor;

#						if defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif

#					else
	float specularFraction = lerp(1, fresnel, distanceBlendFactor);
	float3 finalColorPreFog = lerp(diffuseOutput.refractionDiffuseColor, specularColor, specularFraction) + sunColor * depthControl.w;
	finalColorPreFog = lerp(finalColorPreFog, dynamicFoamColor, dynamicFoam * (1.0f - fresnel * 0.45f));

#						if !defined(WATERBODY)
	float fogDistanceFactor = input.FogParam.w;
	float3 preFogColor = Color::Fog(input.FogParam.xyz);
#						else
	float fogDistanceFactor = min(FogFarColor.w, pow(saturate(length(input.WPosition.xyz) * FogParam.y - FogParam.x), FresnelRI.y));
	float3 preFogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogDistanceFactor));
#						endif

	fogDistanceFactor = Color::FogAlpha(fogDistanceFactor);

#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		preFogColor = AmbientProbe::GetFogAmbientColor(preFogColor);
	}
#						endif
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		float4 atmosphere = Atmosphere::GetAtmosphere(input.WPosition.xyz, FrameBuffer::CameraPosAdjust.xyz, preFogColor, float4(input.HPosition.xy * FrameBuffer::DynamicResolutionParams2.xy, input.HPosition.z, 1));
		if (Atmosphere::ShouldDisableVanillaFog()) {
			preFogColor = atmosphere.xyz;
			preFogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, preFogColor, atmosphere.w);
		} else {
			preFogColor *= GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
			float3 expFogColor = atmosphere.xyz * GetWaterFogFade();
			finalColorPreFog = lerp(finalColorPreFog, expFogColor, atmosphere.w);
		}
	} else {
		preFogColor *= GetWaterFogFade();
		finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
	}
#						else
	preFogColor *= GetWaterFogFade();

	finalColorPreFog = lerp(finalColorPreFog, preFogColor, fogDistanceFactor);
#						endif

	float3 refractionColor = diffuseOutput.refractionColor;

	float fogFactor = min(FogParam.w, pow(saturate(-diffuseOutput.depth * FogParam.y - FogParam.x), FogParam.z));
	float3 fogColor = Color::Fog(lerp(FogNearColor.xyz, FogFarColor.xyz, fogFactor));
#						if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled && Atmosphere::ShouldDisableVanillaFog()) {
		fogFactor = 0;
	}
#						endif
#						if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		fogColor = AmbientProbe::GetFogAmbientColor(fogColor);
	}
#						endif
	refractionColor = lerp(refractionColor, fogColor, Color::FogAlpha(fogFactor));

	float3 finalColor = lerp(refractionColor, finalColorPreFog, diffuseOutput.refractionMul);
	horizonResolvedFogColor = fogColor;
#						if defined(RAIN_RESPONSE) && defined(DEBUG_RAIN_RESPONSE)
	// DEBUG MODE: Override water color with debug visualization
	float3 debugColor = RainResponse::GetDebugWetnessColorStandard(waterData.rippleInfo, 2.0, 3.0);
	if (any(debugColor)) {
		finalColor = debugColor;
	}
#						endif
#					endif

#				endif
#			endif
#		if defined(HORIZON_BLEND)
	// The folded far-water skirt is a geometry extension, not a real receiver.
	// At a grazing view angle it previously remained fully opaque and formed a
	// bright horizontal line against the sky. Blend it toward the already
	// computed atmospheric fog colour only for distant, near-horizontal rays.
	float viewElevation = abs(normalize(input.WPosition.xyz).z);
	float horizonAngleFade = smoothstep(
		HorizonBlend::FadeStartElevation,
		HorizonBlend::FadeEndElevation,
		viewElevation);
	float horizonDistanceFade = smoothstep(
		HorizonBlend::FadeStartDistance,
		HorizonBlend::FadeEndDistance,
		distanceBlendFactor);
	float horizonSkirtFade = horizonAngleFade * horizonDistanceFade;
	// Reuse the fog colour resolved by the active water path.  This already
	// includes PIXL's ambient/atmosphere/water-fade handling; sampling the raw
	// FogFarColor here creates a bright band that does not match the scene.
	float3 horizonFogColor = horizonResolvedFogColor;
	// Keep ordinary water fully shaded; only the folded far-plane skirt fades into
	// the atmosphere.  Reversing these arguments would turn all non-horizon water
	// into fog because horizonSkirtFade is zero for normal water surfaces.
	finalColor = lerp(finalColor, horizonFogColor, horizonSkirtFade);
#		endif
	psout.Lighting = float4(finalColor, isSpecular);
#		endif

#		if defined(STENCIL)
	float3 viewDirection = normalize(input.WorldPosition.xyz);
	float3 geometricNormal = cross(ddx_coarse(input.WorldPosition.xyz), ddy_coarse(input.WorldPosition.xyz));
	float geometricNormalLengthSq = dot(geometricNormal, geometricNormal);
	float3 normal = geometricNormalLengthSq > 1e-10f ? geometricNormal * rsqrt(geometricNormalLengthSq) : float3(0, 0, 1);
	float VdotN = dot(viewDirection, normal);
	psout.WaterMask = float4(0, 0, VdotN, 0);

	psout.MotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
#		endif

	return psout;
}

#	endif

#endif
