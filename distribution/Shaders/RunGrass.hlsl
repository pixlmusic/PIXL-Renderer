#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Permutation.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "FoliageDynamics/FoliageWind.hlsli"

#define DEFERRED

#ifdef FOLIAGE_DYNAMICS
#	define GRASS
#endif  // FOLIAGE_DYNAMICS

#if !defined(WORLD_PROBES) && defined(AMBIENT_PROBE)
#	undef AMBIENT_PROBE
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
	float4 Normal: NORMAL0;
	float4 Color: COLOR0;
	float4 InstanceData1: TEXCOORD4;
	float4 InstanceData2: TEXCOORD5;
	float4 InstanceData3: TEXCOORD6;
	float4 InstanceData4: TEXCOORD7;
};

#ifdef FOLIAGE_DYNAMICS
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
	float3 TexCoord: TEXCOORD0;
	float3 ViewSpacePosition: TEXCOORD1;
#	if defined(RENDER_DEPTH)
	float2 Depth: TEXCOORD2;
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
	float4 VertexNormal: POSITION4;
};
#else
struct VS_OUTPUT
{
	float4 HPosition: SV_POSITION0;
	float4 Color: COLOR0;
	float VertexMult: COLOR1;
	float3 TexCoord: TEXCOORD0;
	float4 AmbientColor: TEXCOORD1;
	float3 ViewSpacePosition: TEXCOORD2;
#	if defined(RENDER_DEPTH)
	float2 Depth: TEXCOORD3;
#	endif  // RENDER_DEPTH
	float4 WorldPosition: POSITION1;
	float4 PreviousWorldPosition: POSITION2;
};
#endif

cbuffer PerGeometry : register(
#ifdef VSHADER
						  b2
#else
						  b3
#endif
					  )
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 WorldView : packoffset(c4);
	row_major float4x4 World : packoffset(c8);
	row_major float4x4 PreviousWorld : packoffset(c12);
	float4 FogNearColor : packoffset(c16);
	float3 WindVector : packoffset(c17);
	float WindTimer : packoffset(c17.w);
	float3 DirLightDirection : packoffset(c18);
	float PreviousWindTimer : packoffset(c18.w);
	float3 DirLightColor : packoffset(c19);
	float AlphaParam1 : packoffset(c19.w);
	float3 AmbientColor : packoffset(c20);
	float AlphaParam2 : packoffset(c20.w);
	float3 ScaleMask : packoffset(c21);
	float ShadowClampValue : packoffset(c21.w);
}

#ifdef VSHADER

#	ifdef GROUND_RESPONSE
#		include "GroundResponse\\GroundResponse.hlsli"
#	endif  // GROUND_RESPONSE

cbuffer cb7 : register(b7)
{
	float4 cb7[1];
}

cbuffer cb8 : register(b8)
{
	float4 cb8[240];
}

// Calculate wind displacement for a grass vertex. Current and previous frame
// timers use the identical travelling field so TAA/motion blur remain coherent.
#ifndef USE_PIXL_MULTI_FREQUENCY_WIND
#	define USE_PIXL_MULTI_FREQUENCY_WIND 1
#endif
#ifndef USE_PIXL_FLOW_WIND
#	define USE_PIXL_FLOW_WIND 1
#endif

float3 CalculateWindDisplacement(VS_INPUT input, float windTimer)
{
	float windAngle =
		0.4f * ((input.InstanceData1.x + input.InstanceData1.y) * -0.0078125f + windTimer);
	float windAngleSin, windAngleCos;
	sincos(windAngle, windAngleSin, windAngleCos);

	float windTmp3 = 0.2f * cos(Math::PI * windAngleCos);
	float windTmp1 = sin(Math::PI * windAngleSin);
	float windTmp2 = sin(Math::TAU * windAngleSin);
	float tip = saturate(input.Color.w);
	float tip2 = tip * tip;
	float windPower =
		WindVector.z *
		(((windTmp1 + windTmp2) * 0.3f + windTmp3) * (0.5f * tip2));

	float3 legacyDisplacement = float3(WindVector.xy, 0.0f) * windPower;
	float3 result = legacyDisplacement;

#if USE_PIXL_MULTI_FREQUENCY_WIND && USE_PIXL_FLOW_WIND
	if (SharedData::foliageDynamicsSettings.EnableEnhancedWind != 0)
	{
		float windLengthSq = dot(WindVector.xy, WindVector.xy);
		float windLength = sqrt(max(windLengthSq, 1e-8f));
		float2 windDirection = FoliageWind::SafeDirection(
			WindVector.xy, float2(0.8192319f, 0.5734624f));
		float2 crossWind = float2(-windDirection.y, windDirection.x);

		float spatialScale =
			max(SharedData::foliageDynamicsSettings.WindSpatialScale, 0.05f);
		float gustStrength =
			max(SharedData::foliageDynamicsSettings.GustStrength, 0.0f);
		float flutterStrength =
			max(SharedData::foliageDynamicsSettings.FlutterStrength, 0.0f);

		// InstanceData1 is in the grass model's coordinate system. Transform the
		// instance anchor before restoring Skyrim's camera-relative world origin;
		// simply adding the two translations made gust cells slide on rotated grass.
		float3 absoluteAnchorWS =
			mul(World, float4(input.InstanceData1.xyz, 1.0f)).xyz +
			FrameBuffer::CameraPosAdjust.xyz;
		float2 absoluteAnchor = absoluteAnchorWS.xy;

		float gustNoise = FoliageWind::AdvectedNoise(
			absoluteAnchor, windTimer, windDirection, spatialScale,
			SharedData::foliageDynamicsSettings.GustSpeed, 0.00105f, 3.7f);
		float directionNoise = FoliageWind::AdvectedNoise(
			absoluteAnchor, windTimer, windDirection, spatialScale,
			SharedData::foliageDynamicsSettings.GustSpeed * 0.83f, 0.00215f, 19.4f) * 2.0f - 1.0f;
		float flutterNoise = FoliageWind::AdvectedNoise(
			absoluteAnchor + input.Position.zz * 0.35f, windTimer, windDirection, spatialScale * 1.65f,
			SharedData::foliageDynamicsSettings.FlutterSpeed, 0.0067f, 41.2f) * 2.0f - 1.0f;

		float gustPulse = FoliageWind::SmoothGust(gustNoise);
		float gustSpeed = max(SharedData::foliageDynamicsSettings.GustSpeed, 0.0f);
		float carrierPhase =
			windTimer * (0.31f + 0.24f * gustSpeed) +
			dot(absoluteAnchor, windDirection) * (0.0017f * spatialScale) +
			gustNoise * 1.6f;
		float carrier = sin(carrierPhase);

		// Keep Skyrim's authored bend, but do not make PIXL motion a percentage of
		// that bend. Some ordinary terrain-grass draws receive a near-zero legacy
		// amplitude, which made the previous enhancement a complete no-op. A small
		// ambient field gives those cards natural calm motion; real weather remains
		// authoritative as soon as its energy exceeds that floor.
		float weatherEnergy = abs(WindVector.z) * max(windLength, 0.35f);
		// The previous 2-unit floor was larger than the authored weather bend in
		// calm conditions.  It could rotate a whole card toward the light and make
		// otherwise rough vegetation read as a silver/mirror facet.  Keep just
		// enough ambient energy for ordinary terrain grass to breathe, then let the
		// real weather vector take over continuously.
		float motionEnergy = max(weatherEnergy, 0.32f);
		float gustAmount = min(gustStrength, 2.0f);
		float flutterAmount = min(flutterStrength, 2.0f);

		float legacyEnvelope =
			1.0f + (gustPulse - 0.5f) * (0.30f * gustAmount);
		float3 structural = legacyDisplacement * legacyEnvelope;

		float meander = directionNoise * (0.18f + 0.20f * gustAmount);
		float2 flowingDirection = FoliageWind::SafeDirection(
			windDirection + crossWind * meander, windDirection);
		float forwardBend =
			motionEnergy * tip2 *
			(0.11f + 0.075f * gustAmount) *
			(0.30f + 0.70f * gustPulse) *
			(0.72f + 0.28f * carrier);
		float3 ambientSway = float3(flowingDirection, 0.0f) * forwardBend;

		float3 crossDrift =
			float3(crossWind, 0.0f) *
			(motionEnergy * tip2 * directionNoise *
				(0.012f + 0.032f * gustAmount));
		float3 flutter =
			float3(crossWind, 0.0f) *
			(motionEnergy * tip2 * tip2 * flutterNoise *
				0.028f * flutterAmount);

		// Never scale the authored Skyrim bend itself. The shipped WindStrength=2
		// previously doubled the full card deformation, which can fold grass cards
		// far enough to expose unstable reflective facets. Scale only PIXL's bounded
		// gust/cross-wind/flutter delta; zero now means exact vanilla motion.
		float enhancement = saturate(
			max(SharedData::foliageDynamicsSettings.WindStrength, 0.0f) * 0.5f);
		float3 enhancedDelta =
			(structural - legacyDisplacement) + ambientSway + crossDrift + flutter;
		// Bound the added deformation independently of shader/material normals so
		// aggressive user values cannot fold cards into mirror-like facets.
		float maxDelta = motionEnergy * tip2 * (0.18f + 0.10f * gustAmount);
		float deltaLengthSq = dot(enhancedDelta, enhancedDelta);
		if (deltaLengthSq > maxDelta * maxDelta && maxDelta > 1e-5f)
			enhancedDelta *= maxDelta * rsqrt(deltaLengthSq);
		result += enhancedDelta * enhancement;
	}
#endif

	return result;
}

#	ifdef FOLIAGE_DYNAMICS
float4 GetMSPosition(VS_INPUT input, float3x3 world3x3)
#	else
float4 GetMSPosition(VS_INPUT input)
#	endif
{
	float3 inputPosition = input.Position.xyz * (input.InstanceData4.yyy * ScaleMask.xyz + float3(1, 1, 1));

#	ifdef FOLIAGE_DYNAMICS
	float3 transformedPosition = mul(world3x3, inputPosition);
	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + transformedPosition;
#	else
	float3 instancePosition;
	instancePosition.z = dot(
		float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w), inputPosition);
	instancePosition.x = dot(input.InstanceData2.xyz, inputPosition);
	instancePosition.y = dot(input.InstanceData3.xyz, inputPosition);

	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + instancePosition;
#	endif
	msPosition.w = 1;

	return msPosition;
}

#	ifdef FOLIAGE_DYNAMICS
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));

	float4 msPosition = GetMSPosition(input, world3x3);

	float3 windDisplacement = CalculateWindDisplacement(input, WindTimer);
	float3 previousWindDisplacement = CalculateWindDisplacement(input, PreviousWindTimer);

#		ifdef GROUND_RESPONSE
	float3 displacement, previousDisplacement;
	GroundResponse::GetDisplacedPosition(input, msPosition.xyz, displacement, previousDisplacement);
	msPosition.xyz += displacement;
#		endif  // GROUND_RESPONSE

	msPosition.xyz += windDisplacement;

	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);

	// Note: input.Color.w is used for wind speed
	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.ViewSpacePosition = mul(WorldView, msPosition).xyz;
	vsout.WorldPosition = mul(World, msPosition);

	float4 previousMsPosition = GetMSPosition(input, world3x3);

#		ifdef GROUND_RESPONSE
	previousMsPosition.xyz += previousDisplacement;
#		endif  // GROUND_RESPONSE

	previousMsPosition.xyz += previousWindDisplacement;

	vsout.PreviousWorldPosition = mul(PreviousWorld, previousMsPosition);

	// Vertex normal needs to be transformed to world-space for lighting calculations.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.VertexNormal.w = input.Color.w;

	return vsout;
}
#	else
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 msPosition = GetMSPosition(input);

	float3 windDisplacement = CalculateWindDisplacement(input, WindTimer);
	float3 previousWindDisplacement = CalculateWindDisplacement(input, PreviousWindTimer);

#		ifdef GROUND_RESPONSE
	float3 displacement, previousDisplacement;
	GroundResponse::GetDisplacedPosition(input, msPosition.xyz, displacement, previousDisplacement);
	msPosition.xyz += displacement;
#		endif  // GROUND_RESPONSE

	msPosition.xyz += windDisplacement;

	float4 projSpacePosition = mul(WorldViewProj, msPosition);
	vsout.HPosition = projSpacePosition;

#		if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#		endif  // RENDER_DEPTH

	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	float dirLightAngle = dot(DirLightDirection.xyz, instanceNormal);
	float3 diffuseMultiplier = input.InstanceData1.www * input.Color.xyz;

	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, Math::IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);

	float distanceFade = 1 - saturate((length(projSpacePosition.xyz) - AlphaParam1) / AlphaParam2);

	vsout.Color.xyz = input.Color.xyz;
	vsout.Color.w = distanceFade * perInstanceFade;
	vsout.VertexMult = input.InstanceData1.w;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.AmbientColor.xyz = input.InstanceData1.www * (AmbientColor.xyz * input.Color.xyz);
	vsout.AmbientColor.w = ShadowClampValue;

	vsout.ViewSpacePosition = mul(WorldView, msPosition).xyz;
	vsout.WorldPosition = mul(World, msPosition);

	float4 previousMsPosition = GetMSPosition(input);

#		ifdef GROUND_RESPONSE
	previousMsPosition.xyz += previousDisplacement;
#		endif  // GROUND_RESPONSE

	previousMsPosition.xyz += previousWindDisplacement;

	vsout.PreviousWorldPosition = mul(PreviousWorld, previousMsPosition);

	return vsout;
}

#	endif

#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

#ifdef FOLIAGE_DYNAMICS
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float2 MotionVectors: SV_Target1;
	float4 NormalGlossiness: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Specular: SV_Target4;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif      // RENDER_DEPTH
};
#else
struct PS_OUTPUT
{
#	if defined(RENDER_DEPTH)
	float4 PS: SV_Target0;
#	else
	float4 Diffuse: SV_Target0;
	float2 MotionVectors: SV_Target1;
	float4 Normal: SV_Target2;
	float4 Albedo: SV_Target3;
	float4 Masks: SV_Target6;
	float4 Masks2: SV_Target7;
#	endif
};
#endif

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampShadowMaskSampler : register(s1);



Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexShadowMaskSampler : register(t1);

cbuffer PerFrame : register(b0)
{
	float4 cb0_1[2] : packoffset(c0);
	float4 VPOSOffset : packoffset(c2);
	float4 cb0_2[7] : packoffset(c3);
}



cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

#	if defined(CONTACT_SHADOWS)
#		include "ContactShadows/ContactShadows.hlsli"
#	endif

#	if defined(RADIANT_GRID)
#		include "RadiantGrid/RadiantGrid.hlsli"
#	endif

#	if defined(NATURAL_LIGHTING) && defined(RADIANT_GRID)
#		include "NaturalLighting/NaturalLighting.hlsli"
#	endif

#	define SampColorSampler SampBaseSampler

#	if defined(SKY_BOUNCE)
#		define SKY_BOUNCE_SHADOW_VIS
#	endif

#	if defined(WORLD_PROBES)
#		include "WorldProbes/WorldProbes.hlsli"
#	endif

#	if defined(SKY_BOUNCE)
#		include "SkyBounce/SkyBounce.hlsli"
#	endif

#	if defined(AMBIENT_PROBE)
#		include "AmbientProbe/AmbientProbe.hlsli"
#	endif

#	if defined(ATMOSPHERE_PIPELINE)
#		include "Atmosphere/Atmosphere.hlsli"
#	endif

#	define LinearSampler SampBaseSampler

#	include "Common/ShadowSampling.hlsli"

// Legacy grass fallback for permutations without the enhanced foliage path.
// Enhanced grass below uses FoliageDynamics::GetLightSpecularInput directly.
float3 PixlGrassVisibleSpecular(
	float3 normal,
	float3 viewDirection,
	float3 lightDirection,
	float3 dirLightColor,
	float dirShadow,
	float3 skyLighting,
	float wetness)
{
	float3 N = normalize(normal);
	float3 V = normalize(viewDirection);
	float3 L = normalize(lightDirection);
	float3 Hsum = V + L;
	float hLenSq = dot(Hsum, Hsum);
	float3 H = hLenSq > 1e-6f ? Hsum * rsqrt(hLenSq) : L;

	float NoL = saturate(abs(dot(N, L)));
	float NoV = saturate(abs(dot(N, V)));
	float NoH = saturate(abs(dot(N, H)));
	float VoH = saturate(dot(V, H));

	float gloss = clamp(SharedData::foliageDynamicsSettings.Glossiness, 1.0f, 100.0f);
	float g = saturate((gloss - 1.0f) / 99.0f);
	float roughness = lerp(0.72f, 0.10f, pow(g, 0.65f));
	roughness = lerp(roughness, max(0.08f, roughness * 0.72f), wetness);

	float alpha2 = max(roughness * roughness, 1e-4f);
	float denom = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
	float D = alpha2 / max(3.14159265f * denom * denom, 1e-4f);
	float k = (roughness + 1.0f);
	k = (k * k) * 0.125f;
	float Vis =
		1.0f /
		max((NoV * (1.0f - k) + k) *
			(NoL * (1.0f - k) + k) * 4.0f, 1e-4f);
	float3 F = 0.04f.xxx +
		(1.0f.xxx - 0.04f.xxx) * pow(1.0f - VoH, 5.0f);

	float strength = max(SharedData::foliageDynamicsSettings.SpecularStrength, 0.0f);
	float3 directSpec =
		max(dirLightColor, 0.0f.xxx) *
		saturate(dirShadow) *
		D * Vis * F * NoL *
		strength;

	float grazing = pow(1.0f - NoV, 2.0f);
	float3 skySpec =
		max(skyLighting, 0.0f.xxx) *
		grazing *
		lerp(0.015f, 0.10f, wetness) *
		strength;

	return directSpec + skySpec;
}
#	ifdef FOLIAGE_DYNAMICS
#		include "FoliageDynamics/FoliageDynamics.hlsli"
#		include "FoliageDynamics/FoliageTuning.hlsli"

float PixlFilterGrassPerceptualRoughness(float3 normal, float perceptualRoughness)
{
	perceptualRoughness = clamp(perceptualRoughness, 0.08f, 1.0f);
	if (SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0)
	{
		// Evaluate normal variance once per pixel before any variable-length
		// RadiantGrid loop. Derivatives inside that loop are undefined.
		float3 normalDx = ddx_coarse(normal);
		float3 normalDy = ddy_coarse(normal);
		float normalVariance = max(dot(normalDx, normalDx), dot(normalDy, normalDy));
		float filtered = sqrt(
			perceptualRoughness * perceptualRoughness +
			saturate(normalVariance * max(SharedData::foliageDynamicsSettings.SpecularAA, 0.0f)) * 0.10f);
		perceptualRoughness = clamp(filtered, 0.08f, 1.0f);
	}
	return perceptualRoughness;
}

float3 PixlGrassSpecularInputRoughness(
	float3 lightDirection,
	float3 viewDirection,
	float3 normal,
	float3 lightColor,
	float perceptualRoughness)
{
	float3 halfSum = viewDirection + lightDirection;
	float halfLengthSq = dot(halfSum, halfSum);
	float3 halfDirection =
		halfLengthSq > 1e-6f ? halfSum * rsqrt(halfLengthSq) : lightDirection;
	float normalDotLight = saturate(abs(dot(normal, lightDirection)));
	float normalDotView = saturate(abs(dot(normal, viewDirection)));
	float normalDotHalf = saturate(abs(dot(normal, halfDirection)));
	float viewDotHalf = saturate(dot(viewDirection, halfDirection));
	float distribution = BRDF::D_GGX(perceptualRoughness, normalDotHalf);
	float visibility = BRDF::Vis_SmithJointApprox(
		perceptualRoughness, normalDotView, normalDotLight);
	float3 fresnel = BRDF::F_Schlick(0.04f.xxx, viewDotHalf);
	return max(lightColor, 0.0f.xxx) * distribution * visibility * fresnel * normalDotLight *
		Color::PBRLightingCompensation * Color::PBRLightingScale;
}

float GetSoftLightMultiplier(float angle, float rolloff)
{
	float softLight = saturate((rolloff + angle) / (1 + rolloff));
	float arg1 = (softLight * softLight) * (3 - 2 * softLight);
	float clampedAngle = saturate(angle);
	float arg2 = (clampedAngle * clampedAngle) * (3 - 2 * clampedAngle);
	return saturate(arg1 - arg2);
}

// Stable stochastic coverage for the engine's grass distance/per-instance fade.
// Frame index is deliberately fixed: temporal noise would sparkle in rain/TAA.
void PixlApplyGrassDistanceDither(float coverage, float2 pixelPosition)
{
	coverage = saturate(coverage);
	if (coverage < 0.9995f)
	{
		float threshold = Random::InterleavedGradientNoise(floor(pixelPosition), 0u);
		clip(coverage - threshold);
	}
}

float3 PixlTuneGrassColor(float3 color)
{
	float luma = dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
	color = lerp(luma.xxx, color, FoliageTuning::Saturation());
	color = (color - 0.18f.xxx) * FoliageTuning::Contrast() + 0.18f.xxx;
	return saturate(color);
}

float PixlApplyGrassAlphaControl(float textureAlpha, float2 pixelPosition)
{
	float shapedAlpha =
		pow(saturate(textureAlpha), FoliageTuning::AlphaPower()) *
		FoliageTuning::AlphaCoverage();
	// Distance/per-instance coverage has its own stable dither immediately before
	// this call. Keeping it out of the material cutout prevents alpha tuning from
	// changing the grass-population fade or becoming coupled to vertex motion.
	float combinedAlpha = saturate(shapedAlpha);
	float cutout = saturate(AlphaTestRefRS + FoliageTuning::CutoutBias());

	float ditherAmount = FoliageTuning::EdgeDither();
	if (ditherAmount > 1e-4f)
	{
		float edgeWidth = max(fwidth(combinedAlpha) * 2.0f, 1.0f / 255.0f);
		float softCoverage = saturate((combinedAlpha - cutout) / edgeWidth + 0.5f);
		float noise = Random::InterleavedGradientNoise(floor(pixelPosition), 0u);
		float threshold = lerp(0.5f, noise, ditherAmount);
		clip(softCoverage - threshold);
	}
	else
	{
		clip(combinedAlpha - cutout);
	}

	return saturate(shapedAlpha);
}

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout = (PS_OUTPUT)0;

#		if defined(SKY_BOUNCE_SHADOW_VIS)
	float skylightingShadowVisibility = 1.0;
#		endif

	float x;
	float y;
	TexBaseSampler.GetDimensions(x, y);

	// Multi-Frequency Wind is a motion feature. It must not implicitly opt grass
	// into PIXL's material/specular model. This uniform gate keeps wind-only mode
	// visually compatible with the authored vegetation.
	const bool enhancedVegetation =
		SharedData::foliageDynamicsSettings.EnableEnhancedVegetation != 0;
	const bool grassAlphaControl = FoliageTuning::AlphaControlEnabled();

	const uint complexMode = min(SharedData::foliageDynamicsSettings.ComplexGrassMode, 3u);
	bool complex = false;
	bool flipComplexNormalY = false;

	[branch] if (complexMode == 1u)
	{
		// Basic/Vanilla: sample the full diffuse texture and use the card normal.
		// This is the compatibility path for textures that are not vertically
		// packed Complex Grass assets.
		complex = false;
	}
	else if (complexMode >= 2u)
	{
		complex = true;
		flipComplexNormalY = (complexMode == 3u);
	}
	else
	{
		// Safe automatic detection. The old single-pixel test could accidentally
		// classify an ordinary diffuse texel as a unit vector. Requiring at least
		// two of three independent texels from the packed normal half makes that
		// false-positive path dramatically less likely while retaining author data.
		uint widthU = max((uint)x, 1u);
		uint lastRowU = max((uint)y, 1u) - 1u;
		int3 probeCoords[3] = {
			int3(0, (int)lastRowU, 0),
			int3((int)(widthU / 2u), (int)lastRowU, 0),
			int3((int)(widthU - 1u), (int)lastRowU, 0)
		};

		float tolerance = clamp(SharedData::foliageDynamicsSettings.ComplexGrassThreshold, 0.001f, 0.10f);
		float minLengthSq = (1.0f - tolerance) * (1.0f - tolerance);
		float maxLengthSq = (1.0f + tolerance) * (1.0f + tolerance);
		uint plausibleNormals = 0u;

		[unroll] for (uint probe = 0u; probe < 3u; ++probe)
		{
			float3 candidate = TexBaseSampler.Load(probeCoords[probe]).xyz * 2.0f - 1.0f;
			float candidateLengthSq = dot(candidate, candidate);
			// Tangent-space normals stored in the packed lower half should have a
			// non-negative Z hemisphere. This rejects many diffuse false positives.
			plausibleNormals +=
				(candidateLengthSq >= minLengthSq &&
				 candidateLengthSq <= maxLengthSq &&
				 candidate.z >= 0.0f) ? 1u : 0u;
		}

		complex = plausibleNormals >= 2u;
	}

	float4 baseColor;
	if (complex) {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, float2(input.TexCoord.x, input.TexCoord.y * 0.5), SharedData::MipBias);
	} else {
		baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	}

	PixlApplyGrassDistanceDither(input.Color.w, input.HPosition.xy);

	// Alpha/cutout is a grass material control, not a wind or enhanced-BRDF
	// control. It must remain live with either of those systems disabled.
	[branch] if (grassAlphaControl)
		baseColor.w = PixlApplyGrassAlphaControl(baseColor.w, input.HPosition.xy);

	baseColor.xyz = Color::Diffuse(baseColor.xyz);
	[branch] if (enhancedVegetation)
		baseColor.xyz = PixlTuneGrassColor(baseColor.xyz);

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if (!grassAlphaControl && (diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
	if (SharedData::distanceBlendSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 normal = normalize(input.VertexNormal.xyz);

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float grassViewDistance = length(viewPosition);
	float detailDistanceScale = enhancedVegetation
		? FoliageTuning::DetailDistanceScale()
		: 1.0f;
	float detailTransitionSoftness = enhancedVegetation
		? FoliageTuning::DetailTransitionSoftness()
		: 1.0f;
	float detailFadeCenter = AlphaParam1 * 0.65f * detailDistanceScale;
	float detailFadeHalfWidth = max(128.0f, AlphaParam1 * 0.23f * detailTransitionSoftness);
	float detailFadeStart = max(256.0f, detailFadeCenter - detailFadeHalfWidth);
	float detailFadeEnd = max(detailFadeStart + 256.0f, detailFadeCenter + detailFadeHalfWidth);
	// The old path faded packed-normal AMPLITUDE to zero at this world-space
	// boundary. Forced complex grass therefore changed its material normal in a
	// visible ring around the camera while vanilla grass did not. Keep the normal
	// physically present for the complete card lifetime and use trilinear mip bias
	// only to suppress distant high-frequency detail. Filtering changes bandwidth,
	// not the mean lighting direction, so crossing the interval cannot pop shading.
	float normalFilterAmount = enhancedVegetation
		? smoothstep(detailFadeStart, detailFadeEnd, grassViewDistance)
		: 0.0f;
	float packedNormalMipBias = SharedData::MipBias + normalFilterAmount * 1.25f;
	float4 specColor = complex
		? TexBaseSampler.SampleBias(
			SampBaseSampler,
			float2(input.TexCoord.x, 0.5f + input.TexCoord.y * 0.5f),
			packedNormalMipBias)
		: 1.0f.xxxx;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	// Swaps direction of the backfaces otherwise they seem to get lit from the wrong direction.
	if (!(Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::GrassSphereNormal))
		if (!frontFace)
			normal = -normal;

	// Capture the correctly oriented card normal. Blend it modestly toward world-up
	// so thin cards read as a coherent vegetation volume instead of isolated planar
	// facets. This is intentionally macro-scale; complex normal detail is layered
	// back on below.
	float3 geometricNormal = normal;
	float3 stableMacroNormal =
		normalize(lerp(
			geometricNormal,
			float3(0.0f, 0.0f, 1.0f),
			enhancedVegetation ? 0.18f : 0.0f));
	geometricNormal = stableMacroNormal;
	normal = stableMacroNormal;

	float3x3 tbn = 0;
	float3 mirroredSpecularDetailNormal = normal;

	if (complex)
	{
		float3 normalColor = FoliageDynamics::TransformVegetationNormal(
			specColor.xyz, flipComplexNormalY != FoliageTuning::FlipNormalY());
		[flatten] if (FoliageTuning::FlipNormalX())
			normalColor.x = -normalColor.x;

		// world-space -> tangent-space -> world-space.
		// This is because we don't have pre-computed tangents.
		tbn = FoliageDynamics::CalculateTBN(normal, -input.WorldPosition.xyz, input.TexCoord.xy);
		float3 detailNormal = normalize(mul(normalColor, tbn));
		float detailStrength =
			enhancedVegetation ? FoliageTuning::NormalStrength() : 1.0f;
		normal = normalize(
			geometricNormal +
			(detailNormal - geometricNormal) * detailStrength);
		normal = normalize(
			lerp(normal, geometricNormal,
				enhancedVegetation ? max(FoliageTuning::CardNormalBlend(), 0.22f) : 0.0f));

		mirroredSpecularDetailNormal = normal;
		[flatten] if (enhancedVegetation && FoliageTuning::MirrorSpecularY())
		{
			float3 mirroredNormalColor = normalColor;
			mirroredNormalColor.y = -mirroredNormalColor.y;
			float3 mirroredDetail = normalize(mul(mirroredNormalColor, tbn));
			mirroredSpecularDetailNormal = normalize(
				geometricNormal +
				(mirroredDetail - geometricNormal) * detailStrength);
			mirroredSpecularDetailNormal = normalize(
				lerp(
					mirroredSpecularDetailNormal,
					geometricNormal,
					max(FoliageTuning::CardNormalBlend(), 0.22f)));
		}
	}

	float weatherWetness = saturate(max(
		SharedData::rainResponseSettings.Raining,
		SharedData::rainResponseSettings.Wetness));
	float grassSpecularWetness = enhancedVegetation
		? saturate(weatherWetness * FoliageTuning::WetSpecularBoost())
		: 0.0f;

	float3 dPdx = ddx_coarse(input.WorldPosition.xyz);
	float3 dPdy = ddy_coarse(input.WorldPosition.xyz);
	float3 cardNormalRaw = cross(dPdy, dPdx);
	float cardNormalLengthSq = dot(cardNormalRaw, cardNormalRaw);
	float3 cardNormal =
		cardNormalLengthSq > 1e-10f
			? cardNormalRaw * rsqrt(cardNormalLengthSq)
			: geometricNormal;
	if (dot(cardNormal, viewDirection) < 0.0f)
		cardNormal = -cardNormal;

	// Derivative normals may supply a broad card sheen, but must never dominate
	// the authored vegetation normal or cross into its opposite hemisphere.
	if (dot(cardNormal, geometricNormal) < 0.0f)
		cardNormal = -cardNormal;
	// Wind bends the two triangles of a grass quad by different amounts. Retain a
	// very small derivative contribution for the deformed plane, but keep the
	// coherent per-card/instance normal authoritative so one triangle or blade tip
	// cannot capture the entire reflection.
	cardNormal = normalize(lerp(geometricNormal, cardNormal, 0.06f));

	float macroSpecularBlend = enhancedVegetation
		? saturate(SharedData::foliageDynamicsSettings.GrassMacroSpecular)
		: 0.0f;
	// The UI control is now authoritative. The previous 0.12 multiplier meant a
	// value of 1.0 still retained 88% unstable per-blade detail, fragmenting the
	// GGX lobe into isolated triangle glints.
	macroSpecularBlend *= lerp(0.72f, 0.94f, grassSpecularWetness);
	float3 grassSpecularNormal =
		normalize(lerp(normal, cardNormal, macroSpecularBlend));
	float3 grassMirroredSpecularNormal = normalize(
		lerp(mirroredSpecularDetailNormal, cardNormal, macroSpecularBlend));
	bool mirrorComplexSpecularY =
		enhancedVegetation && complex && FoliageTuning::MirrorSpecularY();

	// Grass is a longitudinal thin dielectric, not only an isotropic flat card.
	// Derive a stable blade direction from world-up projected onto the card plane.
	const float3 grassWorldUp = float3(0.0f, 0.0f, 1.0f);
	float3 bladeTangentRaw =
		grassWorldUp - cardNormal * dot(grassWorldUp, cardNormal);
	float bladeTangentLengthSq = dot(bladeTangentRaw, bladeTangentRaw);
	float3 bladeTangent;
	if (bladeTangentLengthSq > 1e-8f)
	{
		bladeTangent = bladeTangentRaw * rsqrt(bladeTangentLengthSq);
	}
	else
	{
		float3 tangentFallback = cross(cardNormal, float3(1.0f, 0.0f, 0.0f));
		float tangentFallbackLengthSq = dot(tangentFallback, tangentFallback);
		if (tangentFallbackLengthSq <= 1e-8f)
			tangentFallback = cross(cardNormal, float3(0.0f, 1.0f, 0.0f));
		bladeTangent = normalize(tangentFallback);
	}

	// Convert diffuse alpha into binary-like material coverage after the card has
	// already passed Skyrim's cutout/depth path. This preserves anti-aliased edges
	// without making low-alpha stems three times less reflective than opaque tips.
	float grassSpecularCoverage = smoothstep(0.10f, 0.52f, saturate(baseColor.w));

	// Complex grass alpha is not consistently authored as a full-blade dielectric
	// mask; several packs reserve its strongest values for the tip. Make it an
	// explicitly tunable variation channel rather than an implicit hard mask.
	float complexSpecularMapInfluence = enhancedVegetation
		? FoliageTuning::ComplexSpecularMapInfluence()
		: 1.0f;
	float authoredSpecularMask = complex
		? lerp(1.0f, saturate(specColor.w), complexSpecularMapInfluence)
		: lerp(0.30f, 0.62f, grassSpecularWetness);
	float grassSpecularMask = enhancedVegetation
		? lerp(authoredSpecularMask, 1.0f, grassSpecularWetness * 0.35f)
		: 0.0f;

	float configuredGloss =
		clamp(SharedData::foliageDynamicsSettings.Glossiness, 1.0f, 100.0f);
	float grassGloss = configuredGloss;
	float grassRoughness =
		FoliageDynamics::GlossinessToPerceptualRoughness(grassGloss);
	grassRoughness =
		lerp(grassRoughness, max(0.08f, grassRoughness * 0.72f), grassSpecularWetness);
	// Filter the final (including wetness) material roughness once, outside every
	// variable-length RadiantGrid light loop. Besides being derivative-correct,
	// this gives sun and local lights the same normalized, anti-aliased GGX lobe.
	float grassFilteredRoughness = PixlFilterGrassPerceptualRoughness(
		grassSpecularNormal, grassRoughness);
	float grassMirroredFilteredRoughness = grassFilteredRoughness;
	[flatten] if (mirrorComplexSpecularY)
		grassMirroredFilteredRoughness = PixlFilterGrassPerceptualRoughness(
			grassMirroredSpecularNormal, grassRoughness);

	float grassGBufferGloss =
		enhancedVegetation ? saturate(1.0f - grassRoughness) : 0.0f;

	if (enhancedVegetation &&
		(!complex || SharedData::foliageDynamicsSettings.OverrideComplexGrassSettings))
		baseColor.xyz *= SharedData::foliageDynamicsSettings.BasicGrassBrightness;

	float llDirLightMult = (SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear) ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightCoreSettings.isDirLightLinear) * llDirLightMult;
	float3 dirLightColorMultiplier = 1;

#			if defined(ATMOSPHERE_PIPELINE)
	if (SharedData::atmosphereSettings.enabled) {
		dirLightColor *= Atmosphere::GetSunlightFogAttenuation(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);
	}
#			endif

	float dirLightAngle = dot(normal, SharedData::DirLightDirection.xyz);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow *= shadowColor.x;

#			if defined(CONTACT_SHADOWS)
	if (!SharedData::InInterior && dirLightAngle >= 0.0)
		dirDetailedShadow *= ContactShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise);
#			endif  // CONTACT_SHADOWS

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;

	dirLightColor *= dirLightColorMultiplier;

	float softLightRolloff = saturate(input.VertexNormal.w * 10.0) * SharedData::foliageDynamicsSettings.TissueDiffusionAmount * 2.0;

	lightsDiffuseColor += dirLightColor * dirDetailedShadow * saturate(dirLightAngle) * Color::VanillaNormalization();

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#				if defined(SKY_BOUNCE)
	float3 positionMSSkyBounce = input.WorldPosition.xyz;
	sh2 skyBounceSH = SkyBounce::Sample(positionMSSkyBounce, normal
#					if defined(SKY_BOUNCE_SHADOW_VIS)
		, skylightingShadowVisibility
#					endif
	);
	float skyBounceDiffuse = SkyBounce::GetSkyBounceDiffuse(skyBounceSH, positionMSSkyBounce, normal, vertexAO);
#				endif  // SKY_BOUNCE

	float3 albedo = baseColor.xyz * vertexColor;

	float dirSoftShadow = dirDetailedShadow;
#				if defined(SKY_BOUNCE_SHADOW_VIS)
	// SkyBounce supplies a stable low-frequency visibility field, but it must not
	// replace a valid lit raster sample. Camera-mode disagreement in the probe field
	// previously removed all transmitted sunlight from nearby first-person grass.
	dirSoftShadow = max(dirDetailedShadow, skylightingShadowVisibility);
#				endif

	float3 subsurfaceColor = dirLightColor * dirSoftShadow * (GetSoftLightMultiplier(dirLightAngle, softLightRolloff)) * Color::VanillaNormalization();
#			if USE_PIXL_VEGETATION_BRDF
	[branch] if (enhancedVegetation)
		subsurfaceColor = dirSoftShadow * FoliageDynamics::GetGrassDirectionalTransmissionInput(
			SharedData::DirLightDirection.xyz, viewDirection, normal, dirLightColor, baseColor.xyz,
			SharedData::foliageDynamicsSettings.TissueDiffusionAmount * FoliageTuning::TransmissionBoost()) * Color::VanillaNormalization();
#			endif

	float3 directGrassSpecular = 0.0f.xxx;
	[branch] if (enhancedVegetation)
	{
		directGrassSpecular =
			dirDetailedShadow *
			PixlGrassSpecularInputRoughness(
				SharedData::DirLightDirection.xyz,
				viewDirection,
				grassSpecularNormal,
				dirLightColor,
				grassFilteredRoughness);
		[flatten] if (mirrorComplexSpecularY)
		{
			float3 mirroredDirectGrassSpecular =
				dirDetailedShadow *
				PixlGrassSpecularInputRoughness(
					SharedData::DirLightDirection.xyz,
					viewDirection,
					grassMirroredSpecularNormal,
					dirLightColor,
					grassMirroredFilteredRoughness);
			// Average rather than add: the mirrored response broadens orientation
			// coverage without silently doubling normalized GGX energy.
			directGrassSpecular =
				(directGrassSpecular + mirroredDirectGrassSpecular) * 0.5f;
		}
	}

	// Longitudinal thin-blade sunlight sheen.  This complements the existing
	// two-sided GGX lobe and remains driven by real sun colour/shadowing.
	float3 grassSunL = normalize(SharedData::DirLightDirection.xyz);
	float3 grassSunV = normalize(viewDirection);
	float3 grassSunHsum = grassSunL + grassSunV;
	float grassSunHLengthSq = dot(grassSunHsum, grassSunHsum);
	float3 grassSunH =
		grassSunHLengthSq > 1e-8f
			? grassSunHsum * rsqrt(grassSunHLengthSq)
			: grassSunL;

	float tangentDotH = saturate(abs(dot(bladeTangent, grassSunH)));
	float tangentSinH = sqrt(saturate(1.0f - tangentDotH * tangentDotH));
	float gloss01 = saturate((grassGloss - 1.0f) / 99.0f);
	float fibrePower = lerp(5.0f, 30.0f, pow(gloss01, 0.70f));
	float fibreLobe = pow(max(tangentSinH, 1e-4f), fibrePower);

	float thinNoL = lerp(
		0.30f,
		1.0f,
		saturate(abs(dot(cardNormal, grassSunL))));
	float grassVdotH = saturate(dot(grassSunV, grassSunH));
	float fibreFresnel =
		0.04f + 0.96f * pow(1.0f - grassVdotH, 5.0f);
	float fibreStrength = enhancedVegetation
		? lerp(0.035f, 0.18f, grassSpecularWetness)
		: 0.0f;

	float3 directGrassFibreSpecular =
		max(dirLightColor, 0.0f.xxx) *
		saturate(dirDetailedShadow) *
		fibreLobe *
		thinNoL *
		lerp(0.55f, 1.0f, fibreFresnel) *
		fibreStrength;

	directGrassSpecular += directGrassFibreSpecular;
	lightsSpecularColor += directGrassSpecular;

#			if defined(RADIANT_GRID)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (RadiantGrid::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = RadiantGrid::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = RadiantGrid::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = RadiantGrid::lightList[lightOffset + i];
				RadiantGrid::Light light = RadiantGrid::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);

#				if defined(NATURAL_LIGHTING)
				float intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
				if (intensityMultiplier < 1e-5)
					continue;
#				else
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#				endif

				float localLightBoost = enhancedVegetation ? FoliageTuning::LocalLightBoost() : 1.0f;
				float3 lightColor = Color::PointLight(light.color.xyz) * intensityMultiplier * light.fade * localLightBoost;
				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & RadiantGrid::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				float3 normalizedLightDirection = normalize(lightDirection);

				lightColor *= lightShadow;

				float lightAngle = dot(normal, normalizedLightDirection);
				float lightNoL = dot(normalizedLightDirection.xyz, viewDirection);
				float3 lightDiffuseColor;

				lightDiffuseColor = lightColor * saturate(lightAngle);

#				if USE_PIXL_VEGETATION_BRDF
				if (enhancedVegetation)
					subsurfaceColor += FoliageDynamics::GetTransmissionInput(
						normalizedLightDirection, viewDirection, normal, lightColor, baseColor.xyz,
						SharedData::foliageDynamicsSettings.TissueDiffusionAmount * FoliageTuning::TransmissionBoost()) * Color::VanillaNormalization();
				else
					subsurfaceColor += lightColor * GetSoftLightMultiplier(lightAngle, softLightRolloff) * Color::VanillaNormalization();
#				else
				subsurfaceColor += lightColor * GetSoftLightMultiplier(lightAngle, softLightRolloff) * Color::VanillaNormalization();
#				endif

				lightsDiffuseColor += lightDiffuseColor * Color::VanillaNormalization();

				[branch] if (enhancedVegetation)
				{
					float3 localGrassSpecular = PixlGrassSpecularInputRoughness(
							normalizedLightDirection,
							viewDirection,
							grassSpecularNormal,
							lightColor,
							grassFilteredRoughness);
					[flatten] if (mirrorComplexSpecularY)
					{
						float3 mirroredLocalGrassSpecular = PixlGrassSpecularInputRoughness(
							normalizedLightDirection,
							viewDirection,
							grassMirroredSpecularNormal,
							lightColor,
							grassMirroredFilteredRoughness);
						localGrassSpecular =
							(localGrassSpecular + mirroredLocalGrassSpecular) * 0.5f;
					}
					lightsSpecularColor += localGrassSpecular;
				}
			}
		}
	}
#			endif  // RADIANT_GRID

	diffuseColor += lightsDiffuseColor;

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#				if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe)
		directionalAmbientColor = AmbientProbe::GetDiffuseAmbient(directionalAmbientColor, -normal);
#				endif

	float3 grassSkyLighting = directionalAmbientColor;

	diffuseColor += directionalAmbientColor;
	#if USE_PIXL_VEGETATION_BRDF
	if (enhancedVegetation)
	{
		diffuseColor *= albedo;
		diffuseColor += subsurfaceColor;
	}
	else
	{
		diffuseColor += subsurfaceColor * albedo;
		diffuseColor *= albedo;
	}
	#else
	diffuseColor += subsurfaceColor * albedo;
	diffuseColor *= albedo;
	#endif

	directionalAmbientColor *= albedo;

#				if defined(SKY_BOUNCE)
	SkyBounce::ApplySkyBounce(diffuseColor, directionalAmbientColor, albedo, skyBounceDiffuse);
#				endif

	// Grass's dedicated Specular MRT is not consistently visible in the final
	// composition. Put the DIRECTIONAL GGX lobe into the lit target as well,
	// while retaining the normal MRT output for paths that consume it.
	float foliageSpecularStrength = enhancedVegetation
		? max(SharedData::foliageDynamicsSettings.SpecularStrength, 0.0f) *
			FoliageTuning::SpecularNormalization()
		: 0.0f;
	float3 visibleDirectionalSpecular =
		directGrassSpecular *
		grassSpecularMask *
		grassSpecularCoverage *
		foliageSpecularStrength *
		0.30f;
	diffuseColor += visibleDirectionalSpecular;

	specularColor =
		lightsSpecularColor *
		grassSpecularMask *
		grassSpecularCoverage *
		foliageSpecularStrength;

#			if defined(RADIANT_GRID) && defined(LLFDEBUG)
	if (SharedData::radiantGridSettings.EnableLightsVisualisation) {
		if (SharedData::radiantGridSettings.LightsVisualisationMode == 0) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else if (SharedData::radiantGridSettings.LightsVisualisationMode == 1) {
			diffuseColor.xyz = Color::TurboColormap(0);
		} else {
			diffuseColor.xyz = Color::TurboColormap((float)lightCount / MAX_CLUSTER_LIGHTS);
		}
	} else {
		psout.Diffuse = float4(diffuseColor, 1);
	}
#			else
	psout.Diffuse.xyz = diffuseColor;
#			endif

	float3 normalVS = normalize(FrameBuffer::WorldToView(normal, false));
	psout.Albedo = float4(albedo, 1);
	psout.NormalGlossiness = float4(GBuffer::EncodeNormal(normalVS), grassGBufferGloss, 1);

	psout.Specular = float4(specularColor, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);
#		endif
	return psout;
}
#	else
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#		if defined(SKY_BOUNCE_SHADOW_VIS)
	float skylightingShadowVisibility = 1.0;
#		endif

	float4 baseColor = TexBaseSampler.SampleBias(SampBaseSampler, input.TexCoord.xy, SharedData::MipBias);
	PixlApplyGrassDistanceDither(input.Color.w, input.HPosition.xy);

#		if defined(RENDER_DEPTH)
	float diffuseAlpha = input.Color.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#		endif  // RENDER_DEPTH || DO_ALPHA_TEST

#		if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#		else
	if (SharedData::distanceBlendSettings.DisableTerrainVertexColors)
		input.Color.xyz = 1;

	float3 viewPosition = mul(FrameBuffer::CameraView, float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = FrameBuffer::ViewToUV(viewPosition);
	float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);

	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	float llDirLightMult = (SharedData::linearLightCoreSettings.enableLinearLightCore && !SharedData::linearLightCoreSettings.isDirLightLinear) ? SharedData::linearLightCoreSettings.dirLightMult : 1.0f;
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightCoreSettings.isDirLightLinear) * llDirLightMult;

	// Apply world shadow (terrain shadows, cloud shadows) directly to light color
	if (!SharedData::InInterior)
		dirLightColor *= ShadowSampling::GetWorldShadow(input.WorldPosition.xyz, FrameBuffer::CameraPosAdjust.xyz);

	float dirDetailedShadow = 1.0;

	if (!SharedData::InInterior)
		dirDetailedShadow = shadowColor.x;

#			if defined(CONTACT_SHADOWS)
	if (!SharedData::InInterior)
		dirDetailedShadow *= ContactShadows::GetScreenSpaceShadow(input.HPosition.xyz, screenUV, screenNoise);
#			endif  // CONTACT_SHADOWS

	float3 diffuseColor = dirLightColor * dirDetailedShadow;

#			if defined(RADIANT_GRID)
	uint clusterIndex = 0;
	uint lightCount = 0;

	if (RadiantGrid::GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = RadiantGrid::lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = RadiantGrid::lightGrid[clusterIndex].offset;

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint clusteredLightIndex = RadiantGrid::lightList[lightOffset + i];
				RadiantGrid::Light light = RadiantGrid::lights[clusteredLightIndex];

				float3 lightDirection = light.positionWS.xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);

#				if defined(NATURAL_LIGHTING)
				float intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
				if (intensityMultiplier < 1e-5)
					continue;
#				else
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#				endif

				const bool isPointLightLinear = light.lightFlags & RadiantGrid::LightFlags::Linear;
				float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * intensityMultiplier * light.fade;

				float lightShadow = 1.0;

				float shadowComponent = 1.0;
				if (light.lightFlags & RadiantGrid::LightFlags::Shadow) {
					shadowComponent = shadowColor[light.shadowLightIndex];
					lightShadow *= shadowComponent;
				}

				lightColor *= lightShadow;

				diffuseColor += lightColor;
			}
		}
	}
#			endif  // RADIANT_GRID

	float3 ddx = ddx_coarse(input.WorldPosition);
	float3 ddy = ddy_coarse(input.WorldPosition);
	float3 normalRaw = -cross(ddx, ddy);
	float normalLenSq = dot(normalRaw, normalRaw);
	float3 normal =
		normalLenSq > 1e-10f
			? normalRaw * rsqrt(normalLenSq)
			: float3(0.0f, 0.0f, 1.0f);

	// Legacy grass has no authored PS normal. Keep its reconstructed card normal
	// in the visible hemisphere and add only a small blade-like up bias.
	float3 viewDirForNormal = -normalize(input.WorldPosition.xyz);
	if (dot(normal, viewDirForNormal) < 0.0f)
		normal = -normal;
	normal = normalize(lerp(normal, float3(0.0f, 0.0f, 1.0f), 0.35f));

	float3 vertexColor = Color::ColorToLinear(input.Color.xyz);
	float vertexAO = max(max(vertexColor.r, vertexColor.g), vertexColor.b);
	vertexColor /= max(vertexAO, EPSILON_DIVISION);

#			if defined(SKY_BOUNCE)
	float3 positionMSSkyBounce = input.WorldPosition.xyz;
	sh2 skyBounceSH = SkyBounce::Sample(positionMSSkyBounce, normal
#				if defined(SKY_BOUNCE_SHADOW_VIS)
		, skylightingShadowVisibility
#				endif
	);
	float skyBounceDiffuse = SkyBounce::GetSkyBounceDiffuse(skyBounceSH, positionMSSkyBounce, normal, vertexAO);
#			endif  // SKY_BOUNCE

	float3 directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(normal)));

#			if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe)
		directionalAmbientColor = AmbientProbe::GetDiffuseAmbient(directionalAmbientColor, -normal);
#			endif

	float3 grassSkyLighting = directionalAmbientColor;
	float3 albedo = baseColor.xyz * vertexColor;

	diffuseColor += directionalAmbientColor;

	diffuseColor *= albedo;
	directionalAmbientColor *= albedo;

#			if defined(SKY_BOUNCE)
	SkyBounce::ApplySkyBounce(diffuseColor, directionalAmbientColor, albedo, skyBounceDiffuse);
#			endif

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float weatherWetness = saturate(max(
		SharedData::rainResponseSettings.Raining,
		SharedData::rainResponseSettings.Wetness));

	// The legacy grass PS_OUTPUT has no SV_Target4 Specular at all, so this is
	// the only reliable way for the legacy permutation to show highlights.
	diffuseColor += PixlGrassVisibleSpecular(
		normal,
		viewDirection,
		SharedData::DirLightDirection.xyz,
		dirLightColor,
		dirDetailedShadow,
		grassSkyLighting,
		weatherWetness);

	psout.Diffuse.xyz = diffuseColor;

	psout.Diffuse.w = 1;

	psout.MotionVectors = MotionBlur::GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition);
	psout.Normal.xy = GBuffer::EncodeNormal(FrameBuffer::WorldToView(normal, false));
	psout.Normal.zw = 0;

	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, Color::RGBToYCoCg(directionalAmbientColor).x, 0);
	psout.Masks2 = float4(1.0 - vertexAO, 0, 0, 0);
#		endif

	return psout;
}
#	endif

#endif  // PSHADER
