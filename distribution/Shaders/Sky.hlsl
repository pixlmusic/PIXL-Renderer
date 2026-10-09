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
#	include "Microclimates/MicroclimateField.hlsli"

float PixlCloudDensity(float2 uv)
{
	return saturate(TexBaseSampler.Sample(SampBaseSampler, uv).w);
}

float PixlMicroCloudHash(float2 p)
{
	float3 q = frac(float3(p.x, p.y, p.x) * 0.1031f);
	q += dot(q, q.yzx + 33.33f);
	return frac((q.x + q.y) * q.z);
}

float PixlMicroCloudNoise(float2 worldXY)
{
	// Broad world-space structure (several kilometres) keeps distant fronts
	// coherent with the field rather than glued to sky UVs or the camera.
	float2 grid = worldXY / 300000.0f;
	float2 cell = floor(grid);
	float2 f = frac(grid);
	f = f * f * (3.0f - 2.0f * f);
	float a = PixlMicroCloudHash(cell);
	float b = PixlMicroCloudHash(cell + float2(1.0f, 0.0f));
	float c = PixlMicroCloudHash(cell + float2(0.0f, 1.0f));
	float d = PixlMicroCloudHash(cell + 1.0f);
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float4 ApplyMicroclimateSkyWeather(float4 cloud, float3 rayFromCamera)
{
	float rayLengthSq = dot(rayFromCamera, rayFromCamera);
	float3 ray = rayLengthSq > 1e-8f ? rayFromCamera * rsqrt(rayLengthSq) : float3(0.0f, 1.0f, 0.0f);
	float2 horizontalDirection = ray.xy;
	float horizontalLengthSq = dot(horizontalDirection, horizontalDirection);
	if (horizontalLengthSq < 1e-6f)
		horizontalDirection = float2(0.0f, 0.0f);
	else
		horizontalDirection *= rsqrt(horizontalLengthSq);

	float3 cameraAbsolute = FrameBuffer::CameraPosAdjust.xyz;
	// Sample the full regional field from near horizon to distant mountain range.
	static const float sampleDistances[6] = { 80000.0f, 220000.0f, 480000.0f, 900000.0f, 1500000.0f, 2050000.0f };
	float cloudPotential = 0.0f;
	float stormPotential = 0.0f;
	float2 weatherWorldXY = cameraAbsolute.xy;
	[unroll] for (uint sampleIndex = 0; sampleIndex < 6; ++sampleIndex) {
		float2 sampleXY = cameraAbsolute.xy + horizontalDirection * sampleDistances[sampleIndex];
		float3 samplePosition = float3(sampleXY, cameraAbsolute.z);
		float4 field = Microclimates::SampleField(samplePosition);
		float storm = Microclimates::SampleStormPotential(samplePosition);
		float candidate = max(field.a, field.g * 0.35f);
		if (candidate > cloudPotential) {
			cloudPotential = candidate;
			weatherWorldXY = sampleXY;
		}
		stormPotential = max(stormPotential, storm);
	}

	if (cloudPotential <= 0.015f)
		return cloud;

	float shapeNoise = PixlMicroCloudNoise(weatherWorldXY);
	float frontShape = lerp(0.58f, 1.0f, smoothstep(0.18f, 0.82f, shapeNoise));
	float sourceDetail = lerp(0.82f, 1.0f, saturate(cloud.a));
	float regionalCover = saturate(pow(cloudPotential, 0.58f) * frontShape * sourceDetail * 1.85f);
	float regionalAlpha = regionalCover * lerp(0.72f, 0.98f, saturate(stormPotential + cloudPotential * 0.35f));
	cloud.a = max(cloud.a, regionalAlpha);

	// Preserve Skyrim's cloud texture colour and Sky Veil's scattering response;
	// only storm-rich parts gain the cooler, denser rain-cloud body.
	float stormShade = saturate(stormPotential * regionalCover * 0.52f);
	cloud.rgb *= lerp(1.0f.xxx, float3(0.70f, 0.75f, 0.82f), stormShade);
	return cloud;
}

float PixlCloudPhase(float cosTheta, float eccentricity)
{
	// Draine's modified HG phase adds a restrained backward lobe while retaining
	// a normalized angular response. This keeps twilight edges visible without
	// multiplying the authored cloud energy by an unbounded forward spike.
	float g = clamp(eccentricity, 0.0f, 0.85f);
	float g2 = g * g;
	float mu = clamp(cosTheta, -1.0f, 1.0f);
	float denominator = max(1.0f + g2 - 2.0f * g * mu, 0.04f);
	float hg = (1.0f - g2) / pow(denominator, 1.5f);
	const float backwardLobe = 0.22f;
	float angular = (1.0f + backwardLobe * mu * mu) /
		(1.0f + backwardLobe * (1.0f + 2.0f * g2) / 3.0f);
	return min(hg * angular, 8.0f) * 0.125f;
}

float PixlCloudOpticalDepth(float coverage)
{
	// Alpha stores coverage, not density. Convert it before combining offset
	// strata so thin layers accumulate smoothly and opaque texels stay bounded.
	return -log(max(1.0f - saturate(coverage), 1.0e-3f));
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
	float stratumOpticalDepth =
		PixlCloudOpticalDepth(density0) * 0.40f +
		PixlCloudOpticalDepth(density1) * 0.25f +
		PixlCloudOpticalDepth(density2) * 0.20f +
		PixlCloudOpticalDepth(density3) * 0.15f;
	float opticalDepth = max(lerp(PixlCloudOpticalDepth(density0), stratumOpticalDepth,
		saturate(SharedData::skyVeilSettings.DetailStrength)) * SharedData::skyVeilSettings.CloudDensity, 0.0f);
	float transmittance = exp2(-1.442695f * opticalDepth);

	float2 cloudShadowDirection = SharedData::SunDirection.xy;
	float sunDirectionLength = max(length(cloudShadowDirection), 1e-4f);
	cloudShadowDirection /= sunDirectionLength;
	float2 shadowStep = cloudShadowDirection * (0.0015f + 0.0025f * SharedData::skyVeilSettings.CloudDepth);
	float lightOpticalDepth =
		PixlCloudOpticalDepth(PixlCloudDensity(uv + shadowStep)) * 0.55f +
		PixlCloudOpticalDepth(PixlCloudDensity(uv + shadowStep * 2.35f)) * 0.30f +
		PixlCloudOpticalDepth(PixlCloudDensity(uv + shadowStep * 4.0f)) * 0.15f;
	float selfShadow = exp2(-1.442695f * lightOpticalDepth * max(SharedData::skyVeilSettings.SelfShadowStrength, 0.0f));
	float ambientFill = saturate(SharedData::skyVeilSettings.AmbientLighting);
	cloud.xyz *= lerp(ambientFill, 1.0f, selfShadow);

	float3 sunDirection = SharedData::SunDirection.xyz;
	float sunLengthSq = dot(sunDirection, sunDirection);
	sunDirection = sunLengthSq > 1.0e-6f ? sunDirection * rsqrt(sunLengthSq) : float3(0.0f, 0.0f, 1.0f);
	float phase = PixlCloudPhase(dot(viewDirection, sunDirection), SharedData::skyVeilSettings.PhaseEccentricity);
	float edgeDensity = saturate((1.0f - transmittance) * transmittance * 4.0f);
	float3 moon0 = SharedData::MasserDirection.xyz;
	float3 moon1 = SharedData::SecundaDirection.xyz;
	moon0 = dot(moon0, moon0) > 1.0e-6f ? normalize(moon0) : sunDirection;
	moon1 = dot(moon1, moon1) > 1.0e-6f ? normalize(moon1) : sunDirection;
	float3 celestialScattering = max(SharedData::SunColor.xyz, 0.0f) * phase +
		(max(SharedData::MasserColor.xyz, 0.0f) * PixlCloudPhase(dot(viewDirection, moon0), 0.42f) +
		 max(SharedData::SecundaColor.xyz, 0.0f) * PixlCloudPhase(dot(viewDirection, moon1), 0.42f)) * 0.12f;
	float3 silverLining = celestialScattering * edgeDensity * max(SharedData::skyVeilSettings.SilverLining, 0.0f);
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
	baseColor = ApplyMicroclimateSkyWeather(baseColor, input.FogPosition.xyz);
	float worldLengthSq = dot(input.WorldPosition.xyz, input.WorldPosition.xyz);
	float3 worldDirection = worldLengthSq > 1e-8f ? input.WorldPosition.xyz * rsqrt(worldLengthSq) : float3(0.0f, 1.0f, 0.0f);
	baseColor = ApplyPixlLayeredClouds(baseColor, input.TexCoord0.xy, worldDirection);
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
