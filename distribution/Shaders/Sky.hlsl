#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/SharedData.hlsli"

struct VS_INPUT
{
	float4 Position: POSITION0;

#if defined(TEX) || defined(HORIZFADE)
	float2 TexCoord: TEXCOORD0;
#endif

	float4 Color: COLOR0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;

#if defined(DITHER) && defined(TEX)
	float4 TexCoord0: TEXCOORD0;
#elif defined(DITHER)
	float2 TexCoord0: TEXCOORD3;
#elif defined(TEX) || defined(HORIZFADE)
	float2 TexCoord0: TEXCOORD0;
#endif

#if defined(TEXLERP)
	float2 TexCoord1: TEXCOORD1;
#endif

#if defined(HORIZFADE)
	float TexCoord2: TEXCOORD2;
#endif

#if defined(TEX) || defined(DITHER) || defined(HORIZFADE)
	float4 Color: COLOR0;
#endif

#if !defined(OCCLUSION) && !defined(MOONMASK) && !defined(HORIZFADE)
	float4 SkyBlendColor0: TEXCOORD5;
	float4 SkyBlendColor2: TEXCOORD6;
#endif

	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float3 FogPosition: TEXCOORD4;
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 World : packoffset(c4);
	row_major float4x4 PreviousWorld : packoffset(c8);
	float3 EyePosition : packoffset(c12);
	float VParams : packoffset(c12.w);
	float4 BlendColor[3] : packoffset(c13);
	float2 TexCoordOff : packoffset(c16);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(OCCLUSION)

	// Intentionally left blank

#	elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#	elif defined(HORIZFADE)

	float worldHeight = mul(World, inputPosition).z;
	float eyeHeightDelta = -EyePosition.z + worldHeight;

	vsout.TexCoord0.xy = input.TexCoord;
	vsout.TexCoord2.x = saturate((1.0 / 17.0) * eyeHeightDelta);
	vsout.Color.xyz = BlendColor[0].xyz * VParams;
	vsout.Color.w = BlendColor[0].w;

#	else  // MOONMASK HORIZFADE

#		if defined(DITHER)

#			if defined(TEX)
	vsout.TexCoord0.xyzw = input.TexCoord.xyxy * float4(1.0, 1.0, 501.0, 501.0);
#			else
	float3 inputDirection = normalize(input.Position.xyz);
	inputDirection.y += inputDirection.z;

	vsout.TexCoord0.x = 501 * acos(inputDirection.x);
	vsout.TexCoord0.y = 501 * asin(inputDirection.y);
#			endif  // TEX

#		elif defined(CLOUDS)
	vsout.TexCoord0.xy = TexCoordOff + input.TexCoord;
#		else
	vsout.TexCoord0.xy = input.TexCoord;
#		endif  // DITHER CLOUDS

#		ifdef TEXLERP
	vsout.TexCoord1.xy = TexCoordOff + input.TexCoord;
#		endif  // TEXLERP

	float3 skyColor = BlendColor[0].xyz * input.Color.xxx + BlendColor[1].xyz * input.Color.yyy +
	                  BlendColor[2].xyz * input.Color.zzz;

	vsout.Color.xyz = VParams * skyColor;
	vsout.Color.w = BlendColor[0].w * input.Color.w;
	vsout.SkyBlendColor0 = float4(BlendColor[0].xyz * VParams, 0);
	vsout.SkyBlendColor2 = float4(BlendColor[2].xyz * VParams, 0);
#	endif      // OCCLUSION MOONMASK HORIZFADE

	vsout.Position = mul(WorldViewProj, inputPosition).xyww;
	vsout.WorldPosition = mul(World, inputPosition);
	vsout.FogPosition = vsout.WorldPosition.xyz - EyePosition.xyz;
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	float4 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
#if defined(SKY_VEIL) && defined(CLOUDS) && !defined(DEFERRED)
	float4 SkyVeil: SV_Target3;
#endif
};

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampBlendSampler : register(s1);
SamplerState SampNoiseGradSampler : register(s2);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexBlendSampler : register(t1);
Texture2D<float4> TexNoiseGradSampler : register(t2);

cbuffer PerGeometry : register(b2)
{
	float2 PParams : packoffset(c0);
};

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

#	include "Common/MotionBlur.hlsli"
#	include "Common/SharedData.hlsli"

#	if defined(ATMOSPHERE_PIPELINE)
#		define SampColorSampler SampBaseSampler
#		include "Atmosphere/Atmosphere.hlsli"
#	endif

#	ifdef CAMERA_SUITE
#		include "CameraSuite/HDRSun.hlsli"
#	endif

Texture2D<float> TexDepthSampler : register(t17);

#	ifndef USE_PIXL_LAYERED_VOLUMETRIC_CLOUDS
#		define USE_PIXL_LAYERED_VOLUMETRIC_CLOUDS 1
#	endif

#	if USE_PIXL_LAYERED_VOLUMETRIC_CLOUDS && defined(CLOUDS) && defined(SKY_VEIL)
float PixlCloudDensity(float2 uv)
{
	return saturate(TexBaseSampler.Sample(SampBaseSampler, uv).w);
}

float PixlCloudPhase(float cosTheta, float eccentricity)
{
	// Normalized Henyey-Greenstein lobe, bounded for stable HDR highlights.
	float g = clamp(eccentricity, 0.0f, 0.85f);
	float g2 = g * g;
	float denominator = max(1.0f + g2 - 2.0f * g * cosTheta, 0.04f);
	return min((1.0f - g2) / pow(denominator, 1.5f), 8.0f) * 0.125f;
}

float4 ApplyPixlLayeredClouds(float4 cloud, float2 uv, float3 viewDirection)
{
	if (SharedData::skyVeilSettings.EnableVolumetricClouds == 0)
		return cloud;

	float elevation = saturate(abs(viewDirection.z) * 4.0f);
	float horizonStable = lerp(1.0f, elevation, saturate(SharedData::skyVeilSettings.HorizonFade));
	float2 parallaxDirection = viewDirection.xy / max(abs(viewDirection.z), 0.2f);
	float2 layerStep = parallaxDirection * (0.00125f * SharedData::skyVeilSettings.CloudDepth * horizonStable);

	// Four offset strata turn the authored 2D cloud coverage into a compact optical-depth
	// profile. Symmetric taps avoid directional crawling and retain the original motion vectors.
	float density0 = saturate(cloud.w);
	float density1 = PixlCloudDensity(uv + layerStep);
	float density2 = PixlCloudDensity(uv - layerStep * 0.7f);
	float density3 = PixlCloudDensity(uv + float2(-layerStep.y, layerStep.x) * 0.55f);
	float detailDensity = dot(float4(density0, density1, density2, density3), float4(0.40f, 0.25f, 0.20f, 0.15f));
	detailDensity = lerp(density0, detailDensity, saturate(SharedData::skyVeilSettings.DetailStrength));

	float opticalDepth = max(detailDensity * SharedData::skyVeilSettings.CloudDensity, 0.0f);
	float transmittance = exp2(-1.442695f * opticalDepth);

	float2 sunDirection = SharedData::SunDirection.xy;
	float sunDirectionLength = max(length(sunDirection), 1e-4f);
	sunDirection /= sunDirectionLength;
	float2 shadowStep = sunDirection * (0.0015f + 0.0025f * SharedData::skyVeilSettings.CloudDepth);
	float lightOpticalDepth =
		PixlCloudDensity(uv + shadowStep) * 0.55f +
		PixlCloudDensity(uv + shadowStep * 2.35f) * 0.30f +
		PixlCloudDensity(uv + shadowStep * 4.0f) * 0.15f;
	float selfShadow = exp2(-1.442695f * lightOpticalDepth * max(SharedData::skyVeilSettings.SelfShadowStrength, 0.0f));
	float ambientFill = saturate(SharedData::skyVeilSettings.AmbientLighting);
	cloud.xyz *= lerp(ambientFill, 1.0f, selfShadow);

	float phase = PixlCloudPhase(dot(viewDirection, normalize(SharedData::SunDirection.xyz)), SharedData::skyVeilSettings.PhaseEccentricity);
	float edgeDensity = saturate((1.0f - transmittance) * transmittance * 4.0f);
	float3 silverLining = SharedData::SunColor.xyz * phase * edgeDensity * max(SharedData::skyVeilSettings.SilverLining, 0.0f);
	cloud.xyz += silverLining * max(dot(cloud.xyz, 1.0f / 3.0f), 0.05f);

	float volumetricAlpha = 1.0f - transmittance;
	cloud.w = saturate(lerp(cloud.w, volumetricAlpha, SharedData::skyVeilSettings.CloudDepth * 0.35f));
	return cloud;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	// Color::Sky is float3->float3 (per-channel sky gamma). PParams.yyy broadcasts the packed
	// scalar in PParams.y to RGB; float3 matches output .xyz where skyScale is added.
	float3 skyScale = Color::Sky(PParams.yyy);

#	ifndef OCCLUSION
#		ifndef TEXLERP
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	baseColor.xyz = Color::Sky(baseColor.xyz);
#			ifdef TEXFADE
	baseColor.w *= PParams.x;
#			endif
#		else
	float4 blendColor = TexBlendSampler.Sample(SampBlendSampler, input.TexCoord1.xy);
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	blendColor.xyz = Color::Sky(blendColor.xyz);
	baseColor.xyz = Color::Sky(baseColor.xyz);
	baseColor = PParams.xxxx * (-baseColor + blendColor) + baseColor;
#		endif

#		if defined(CAMERA_SUITE)
	float hdrSunGain = HDRSun::GetHdrSunGain(input.TexCoord0.xy, baseColor);
	baseColor.xyz *= hdrSunGain;
#		endif

#		if defined(DITHER)
	float2 noiseGradUv = float2(0.125, 0.125) * input.Position.xy;
	float noiseGrad = TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x * 0.03125 - 0.0078125;
	noiseGrad *= 10.0;

#			ifdef TEX
	psout.Color.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale;
	psout.Color.xyz *= 1.0 + noiseGrad;
	psout.Color.w = baseColor.w * input.Color.w;
#			else
	float3 skyGradientColor = input.Color.xyz;

	psout.Color.xyz = Color::Sky(skyGradientColor) + skyScale;

	psout.Color.xyz *= 1.0 + noiseGrad;
	psout.Color.w = input.Color.w;
#			endif  // TEX

#		elif defined(MOONMASK)
	psout.Color.xyzw = baseColor;

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#		elif defined(HORIZFADE)
	psout.Color.xyz = float3(1.5, 1.5, 1.5) * (Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#		else

#		if USE_PIXL_LAYERED_VOLUMETRIC_CLOUDS && defined(CLOUDS) && defined(SKY_VEIL)
	baseColor = ApplyPixlLayeredClouds(baseColor, input.TexCoord0.xy, normalize(input.WorldPosition.xyz));
#		endif

	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = Color::Sky(input.Color.xyz) * baseColor.xyz + skyScale;
#		endif

#	else
	psout.Color = float4(0, 0, 0, 1.0);
#	endif  // OCCLUSION

#	if defined(ATMOSPHERE_PIPELINE)
	const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection) != 0;
	if (inReflection && SharedData::atmosphereSettings.enabled) {
		float3 skyFogPosition = normalize(input.FogPosition.xyz) * SharedData::CameraData.x;
		float4 atmosphere = Atmosphere::GetAtmosphereWithoutVolumes(skyFogPosition, FrameBuffer::CameraPosAdjust.xyz, psout.Color.xyz, float4(input.Position.xy * FrameBuffer::DynamicResolutionParams2.xy, input.Position.z, 1));
		psout.Color.xyz = lerp(psout.Color.xyz, atmosphere.xyz, atmosphere.w * (1.0f - saturate(SharedData::atmosphereSettings.skyProtection)));
	}
#	endif

	float2 screenMotionVector = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	psout.MotionVectors = float4(screenMotionVector, 0, psout.Color.w);
	psout.Normal = float4(0.5, 0.5, 0, psout.Color.w);

#	if defined(SKY_VEIL) && defined(CLOUDS) && !defined(DEFERRED)
	psout.SkyVeil = psout.Color.w;

	// Keep sun behind scene depth to prevent halo leaks through geometry.
	float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
	if (depth < input.Position.z)
		psout.Color.w = 0;

#	else
	// Even without cloud shadows enabled, sun disc should be occluded by scene depth (clouds, terrain, etc.)
	if ((Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun)) {
		float depth = TexDepthSampler.Load(int3(input.Position.xy, 0));
		if (depth < input.Position.z)
			psout.Color.w = 0;
	}
#	endif

	return psout;
}
#endif
