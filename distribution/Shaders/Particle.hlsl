#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

#if defined(ENVCUBE) && defined(RAIN_RESPONSE)
#	include "RainResponse/WorldPrecipitation.hlsli"
#endif

#if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
#	include "RainResponse/Precipitation.hlsli"
#endif

#if !defined(WORLD_PROBES) && defined(AMBIENT_PROBE)
#	undef AMBIENT_PROBE
#endif

struct VS_INPUT
{
	float4 Position: POSITION0;
#if !defined(ENVCUBE)
	float4 Normal: NORMAL0;
#endif
	float4 TexCoord0: TEXCOORD0;
#if defined(ENVCUBE)
	float4
#else
	int4
#endif
		TexCoord1: TEXCOORD1;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float4 Color: COLOR0;
	float2 TexCoord0: TEXCOORD0;
#if defined(ENVCUBE)
	float4 PrecipitationOcclusionTexCoord: TEXCOORD1;
#endif
#if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	// x = world-space clump multiplier, y = gust amount, z = secondary-layer slope, w = stable selector
	float4 RainResponseData: TEXCOORD3;
#endif
#if defined(ENVCUBE) && !defined(RAIN) && defined(RAIN_RESPONSE)
	// x = view distance, y = stable selector, z = live snow amount, w = reserved
	float4 SnowPrecipitationData: TEXCOORD2;
#endif
};

#ifdef VSHADER
cbuffer PerTechnique : register(b0)
{
	float2 ScaleAdjust : packoffset(c0);
};

cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj;  // 0
	row_major float4x4 WorldView;      // 4
#	if defined(ENVCUBE)
	row_major float4x4 PrecipitationOcclusionWorldViewProj;  // 8, 16
#	endif
	float4 fVars0;        // 8, 16 ENVCUBE 12, 20
	float4 fVars1;        // 9, 17 ENVCUBE 13, 21
	float4 fVars2;        // 10, 18 ENVCUBE 14, 22
	float4 fVars3;        // 11, 19 ENVCUBE 15, 23
	float4 fVars4;        // 12, 20 ENVCUBE 16, 24
	float4 Color1;        // 13, 21 ENVCUBE 17, 25
	float4 Color2;        // 14, 22 ENVCUBE 18, 26
	float4 Color3;        // 15, 23 ENVCUBE 19, 27
	float4 Velocity;      // 16, 24 ENVCUBE 20, 28
	float4 Acceleration;  // 17, 25 ENVCUBE 21, 29
	float4 Wind;          // 18, 26 ENVCUBE 22, 30
}

float2x2 GetRotationMatrix(float angle)
{
	float sine, cosine;
	sincos(angle, sine, cosine);

	return float2x2(float2(cosine, -sine), float2(sine, cosine));
}

float2 PixlProjectedClipDirection(float4 fromClip, float4 toClip, float2 fallback)
{
	float2 fromNdc = fromClip.xy / max(abs(fromClip.w), 1e-5f);
	float2 toNdc = toClip.xy / max(abs(toClip.w), 1e-5f);
	float2 direction = toNdc - fromNdc;
	float lenSq = dot(direction, direction);
	return lenSq > 1e-8f ? direction * rsqrt(lenSq) : fallback;
}

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

#	if defined(ENVCUBE)
#		if defined(RAIN)
	float2 positionOffset = input.TexCoord1.xy;
#		else
	float2 snowLocalOffset =
		input.TexCoord1.xy * ScaleAdjust +
		input.TexCoord1.zw;
	float2x2 rotationMatrix = GetRotationMatrix(fVars0.w);
	float2 positionOffset =
		mul(rotationMatrix, snowLocalOffset);
#		endif

	float3 normalizedPosition = (fVars0.xyz + input.Position.xyz) / fVars2.xxx;
	normalizedPosition = normalizedPosition >= -normalizedPosition ? frac(abs(normalizedPosition)) :
	                                                                 -frac(abs(normalizedPosition));

	float4 msPosition;
	msPosition.xyz = normalizedPosition * fVars2.xxx + (-(fVars2.x * 0.5).xxx + fVars1.xyz);
	msPosition.w = 1;

#		if !defined(RAIN) && defined(RAIN_RESPONSE)
	PIXLPrecipitation::SnowDynamics snowDynamics =
		PIXLPrecipitation::EvaluateSnowDynamics(
			msPosition.xyz,
			Wind.xyz);

	msPosition.xyz = snowDynamics.position;

	float2 snowCentreDelta =
		msPosition.xy - fVars1.xy;
	float snowRadiusNorm =
		saturate(
			length(snowCentreDelta) /
			max(fVars2.x * 0.52f, 1.0f));
	float snowShellScale =
		PIXLPrecipitation::GetSnowFarShellScale(
			snowRadiusNorm,
			snowDynamics.selector);

	msPosition.xy =
		fVars1.xy + snowCentreDelta * snowShellScale;
#		endif

	float4 viewPosition = mul(WorldViewProj, msPosition);
#		if defined(RAIN)
	float3 rainVelocity = Velocity.xyz;
#		if defined(RAIN_RESPONSE)
	RainResponse::RainParticleDynamics rainDynamics = RainResponse::EvaluateRainParticleDynamics(msPosition.xyz, rainVelocity);
	rainVelocity = rainDynamics.velocity;
	vsout.RainResponseData = float4(rainDynamics.clump, rainDynamics.gust, rainDynamics.slope, rainDynamics.selector);

	float2 rainCentreDelta = msPosition.xy - fVars1.xy;
	float rainRadiusNorm = saturate(length(rainCentreDelta) / max(fVars2.x * 0.52f, 1.0f));
	float rainShellScale = RainResponse::GetRainFarShellScale(rainRadiusNorm, rainDynamics.selector);
	msPosition.xy = fVars1.xy + rainCentreDelta * rainShellScale;
	viewPosition = mul(WorldViewProj, msPosition);
#		endif
	float4 adjustedMsPosition = msPosition - float4(rainVelocity, 0);
	float positionBlendParam = 0.5 * (1 + input.TexCoord1.y);
	float4 adjustedViewPosition = mul(WorldViewProj, adjustedMsPosition);
	float4 finalViewPosition = lerp(adjustedViewPosition, viewPosition, positionBlendParam);
#		else
	float4 finalViewPosition = viewPosition;
#		endif

#		if defined(RAIN) && defined(RAIN_RESPONSE)
	if (PIXL_EnableWorldSpaceRain != 0u) {
		// Both endpoints are world-space positions. Their projected axis therefore
		// cannot rotate with the camera like a screen-facing rain card.
		float2 projectedFall =
			PixlProjectedClipDirection(
				adjustedViewPosition,
				viewPosition,
				float2(0.0f, -1.0f));

		float2 projectedPerp =
			float2(-projectedFall.y, projectedFall.x);

		// TexCoord1.y already chooses head/tail through positionBlendParam.
		// TexCoord1.x becomes only the narrow cross-streak width.
		vsout.Position.xy =
			finalViewPosition.xy +
			projectedPerp * input.TexCoord1.x;
	} else {
		vsout.Position.xy =
			positionOffset + finalViewPosition.xy;
	}
#		elif !defined(RAIN) && defined(RAIN_RESPONSE)
	if (PIXL_EnableSnowEnhancement != 0u) {
		// Flake axes live in world space. Camera rotation changes only how those
		// axes project, not the flake's physical orientation.
		const float axisProbe = 32.0f;

		float4 axisAClip =
			mul(
				WorldViewProj,
				float4(
					msPosition.xyz +
						snowDynamics.axisA * axisProbe,
					1.0f));

		float4 axisBClip =
			mul(
				WorldViewProj,
				float4(
					msPosition.xyz +
						snowDynamics.axisB * axisProbe,
					1.0f));

		float2 projectedA =
			PixlProjectedClipDirection(
				viewPosition,
				axisAClip,
				float2(1.0f, 0.0f));

		float2 projectedB =
			PixlProjectedClipDirection(
				viewPosition,
				axisBClip,
				float2(0.0f, 1.0f));

		// Avoid projection collapse when one flake axis points almost into camera.
		projectedB -=
			projectedA * dot(projectedB, projectedA);
		float bLenSq = dot(projectedB, projectedB);
		projectedB =
			bLenSq > 1e-6f ?
			projectedB * rsqrt(bLenSq) :
			float2(-projectedA.y, projectedA.x);

		float2 orientedSnowOffset =
			(projectedA * snowLocalOffset.x +
			 projectedB * snowLocalOffset.y) *
			max(PIXL_SnowFlakeScale, 0.01f);

		vsout.Position.xy =
			finalViewPosition.xy +
			orientedSnowOffset;

		vsout.SnowPrecipitationData =
			float4(
				length(msPosition.xyz),
				snowDynamics.selector,
				saturate(PIXL_Snowing),
				0.0f);
	} else {
		vsout.Position.xy =
			positionOffset + finalViewPosition.xy;
		vsout.SnowPrecipitationData =
			float4(
				length(msPosition.xyz),
				0.5f,
				0.0f,
				0.0f);
	}
#		else
	vsout.Position.xy =
		positionOffset + finalViewPosition.xy;
#		endif

	vsout.Position.zw = finalViewPosition.zw;
	vsout.Color.xyz = 1.0.xxx;
	vsout.Color.w = fVars1.w;

	vsout.TexCoord0.xy = input.TexCoord0.xy;

	float4 precipitationOcclusionTexCoord = mul(PrecipitationOcclusionWorldViewProj, msPosition);
	precipitationOcclusionTexCoord.y = -precipitationOcclusionTexCoord.y;
	vsout.PrecipitationOcclusionTexCoord = precipitationOcclusionTexCoord;
#	else
	float tmp2 = input.Normal.w * input.Position.w;
	float tmp1 = tmp2 / fVars0.y;

	float uvScale1, uvScale2, tmp3, tmp4;
	if (tmp1 > fVars2.w) {
		uvScale1 = fVars2.y;
		uvScale2 = 0;
		tmp3 = fVars2.w;
		tmp4 = 1;
	} else if (tmp1 > fVars2.z) {
		uvScale1 = fVars2.x;
		uvScale2 = fVars2.y;
		tmp3 = fVars2.z;
		tmp4 = fVars2.w;
	} else {
		uvScale1 = 0;
		uvScale2 = fVars2.x;
		tmp3 = 0;
		tmp4 = fVars2.z;
	}
	float uvScaleParam = (tmp1 - tmp3) / (tmp4 - tmp3);
	float uvScale = lerp(uvScale1, uvScale2, uvScaleParam);

	vsout.TexCoord0.xy = fVars4.xy * input.TexCoord1.xy;

	float2 uv1 = (input.TexCoord1.zw * 2.0.xx - 1.0.xx) * uvScale;
	float uvAngle = input.TexCoord0.y * input.Position.w + input.TexCoord0.x;
	float2x2 rotationMatrix = GetRotationMatrix(uvAngle);
	float2 positionOffset = mul(rotationMatrix, uv1);

	float4 msPosition;
	msPosition.xyz = -fVars3.xyz +
	                 (((input.Normal.xyz * fVars0.www + Acceleration.xyz) * (tmp2 * tmp2)) * 0.5 +
						 (((fVars0.zzz * input.Normal.xyz) * input.TexCoord0.zzz +
							  (normalize(-Wind.xyz + input.Position.xyz) * Wind.www + Velocity.xyz)) *
								 tmp2 +
							 input.Position.xyz));
	msPosition.w = 1;

	float4 viewPosition = mul(WorldViewProj, msPosition);
	vsout.Position.xy = positionOffset * ScaleAdjust + viewPosition.xy;
	vsout.Position.zw = viewPosition.zw;

	float4 color1, color2;
	float colorTmp1, colorTmp2;
	if (tmp1 > fVars1.z) {
		color1 = Color3.xyzw;
		color2 = float4(Color3.xyz, 0);
		colorTmp1 = fVars1.z;
		colorTmp2 = 1;
	} else if (tmp1 > fVars1.y) {
		color1 = Color2.xyzw;
		color2 = Color3.xyzw;
		colorTmp1 = fVars1.y;
		colorTmp2 = fVars1.z;
	} else if (tmp1 > fVars1.x) {
		color1 = Color1.xyzw;
		color2 = Color2.xyzw;
		colorTmp1 = fVars1.x;
		colorTmp2 = fVars1.y;
	} else {
		color1 = float4(Color1.xyz, 0);
		color2 = Color1.xyzw;
		colorTmp1 = 0;
		colorTmp2 = fVars1.x;
	}
	float colorParam = (tmp1 - colorTmp1) / (colorTmp2 - colorTmp1);
	float4 color = lerp(color1, color2, colorParam);

	vsout.Color.w = fVars3.w * color.w;
	vsout.Color.xyz = color.xyz;
#	endif

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
	float4 Normal: SV_Target1;
};

#ifdef PSHADER

#	if defined(RADIANT_GRID)
#		include "RadiantGrid/RadiantGrid.hlsli"
#	endif

#	if defined(NATURAL_LIGHTING) && defined(RADIANT_GRID)
#		include "NaturalLighting/NaturalLighting.hlsli"
#	endif

SamplerState SampSourceTexture : register(s0);
#	if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
SamplerState SampGrayscaleTexture : register(s1);
#	endif
#	if defined(ENVCUBE)
SamplerState SampPrecipitationOcclusionTexture : register(s2);
SamplerState SampUnderwaterMask : register(s3);
#	endif

Texture2D<float4> TexSourceTexture : register(t0);
#	if defined(GRAYSCALE_TO_COLOR) || defined(GRAYSCALE_TO_ALPHA)
Texture2D<float4> TexGrayscaleTexture : register(t1);
#	endif
#	if defined(ENVCUBE)
Texture2D<float4> TexPrecipitationOcclusionTexture : register(t2);
Texture2D<float4> TexUnderwaterMask : register(t3);
#	endif
cbuffer PerGeometry : register(b2)
{
	float ColorScale : packoffset(c0);
	float3 TextureSize : packoffset(c1);
};

#	define LinearSampler SampSourceTexture
#	include "Common/ShadowSampling.hlsli"

#	if defined(AMBIENT_PROBE)
#		include "AmbientProbe/AmbientProbe.hlsli"
#	endif

#	if defined(WORLD_PROBES)
#		define SampColorSampler SampSourceTexture
#		include "WorldProbes/WorldProbes.hlsli"
#	endif

PS_OUTPUT main(PS_INPUT input, bool frontFace : SV_IsFrontFace)
{
	PS_OUTPUT psout;

#	if defined(ENVCUBE)
	float2 precipitationOcclusionUV = (input.PrecipitationOcclusionTexCoord.xy * 0.5 + 0.5) * TextureSize.x;
	float precipitationOcclusion = -input.PrecipitationOcclusionTexCoord.z + TexPrecipitationOcclusionTexture.Load(float3(precipitationOcclusionUV, 0)).x;
	float2 underwaterMaskUv = TextureSize.yz * input.Position.xy;
	float underwaterMask = TexUnderwaterMask.Sample(SampUnderwaterMask, underwaterMaskUv).x;
	float precipitationVisibility = precipitationOcclusion - underwaterMask;

#		if defined(RAIN) && defined(RAIN_RESPONSE)
	float rainOcclusionPass = step(0.0f, precipitationVisibility);
	// Phase 3 runoff is rendered by a dedicated post-deferred pass. Keep Skyrim's
	// precipitation rejection authoritative here; hidden rain cards are never
	// repurposed as emitters.
	if (precipitationVisibility < 0.0f) {
		discard;
	}
#		else
	if (precipitationVisibility < 0.0f) {
		discard;
	}
#		endif
#	endif

#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	// Reconstruct this rain fragment once for the integrated lit-particle path.
	float2 rainScreenUV = input.Position.xy * SharedData::BufferDim.zw;
	float4 rainPositionCS = float4(2.0f * float2(rainScreenUV.x, 1.0f - rainScreenUV.y) - 1.0f, input.Position.z, 1.0f);
	float4 rainPositionWS4 = mul(FrameBuffer::CameraViewProjInverse, rainPositionCS);
	float3 rainPositionWS = rainPositionWS4.xyz / max(abs(rainPositionWS4.w), 1e-6f);
	float rainViewDistance = SharedData::GetScreenDepth(input.Position.z);
	float rainVisibility = input.RainResponseData.x * RainResponse::GetRainDistanceVisibility(rainViewDistance);
	float rainSecondaryWeight = RainResponse::GetRainSecondaryLayerWeight(input.RainResponseData.y, input.RainResponseData.w);
	float rainImpact = RainResponse::EvaluateRainImpactProximity(rainScreenUV, input.Position.z, input.RainResponseData.w);
#	endif

	float4 sourceColor = TexSourceTexture.Sample(SampSourceTexture, input.TexCoord0);
#	if defined(ENVCUBE) && !defined(RAIN) && defined(RAIN_RESPONSE)
	float snowViewDistance =
		input.SnowPrecipitationData.x;
	float snowAuthoredAlpha =
		sourceColor.w;

	sourceColor.w =
		saturate(
			sourceColor.w *
			PIXLPrecipitation::GetSnowDistanceVisibility(
				snowViewDistance));

	sourceColor.w =
		max(
			sourceColor.w,
			PIXLPrecipitation::GetSnowFinalAlphaFloor(
				snowAuthoredAlpha,
				snowViewDistance));

	float snowOpticalWeight =
		saturate(
			PIXLPrecipitation::GetSnowDistanceWeight(
				snowViewDistance) *
			saturate(PIXL_Snowing) *
			(0.08f +
			 max(PIXL_SnowDistanceVisibility, 0.0f) * 0.22f +
			 max(PIXL_SnowLightingResponse, 0.0f) * 0.14f));

	sourceColor.xyz =
		lerp(
			sourceColor.xyz,
			max(sourceColor.xyz, 0.78f.xxx),
			snowOpticalWeight);
#	endif
#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	float rainAuthoredSourceAlpha = sourceColor.w;
	float secondaryAlpha = 0.0f;
	float4 secondarySource = sourceColor;
	if (rainSecondaryWeight > 1e-4f) {
		float2 secondaryUV = RainResponse::GetRainSecondaryUV(input.TexCoord0, input.RainResponseData.z, input.RainResponseData.w);
		float secondaryInside = RainResponse::IsRainUVInside(secondaryUV);
		secondarySource = TexSourceTexture.Sample(SampSourceTexture, saturate(secondaryUV));
		secondaryAlpha = secondarySource.w * rainSecondaryWeight * secondaryInside;
	}
	secondaryAlpha *= rainOcclusionPass;
	float primaryAlpha = max(sourceColor.w * rainVisibility, 0.0f) * rainOcclusionPass;
	float combinedAlpha = primaryAlpha + secondaryAlpha;
	float secondaryMix = secondaryAlpha / max(combinedAlpha, 1e-4f);
	sourceColor.xyz = lerp(sourceColor.xyz, secondarySource.xyz, saturate(secondaryMix));
	float splashShape = RainResponse::EvaluateRainImpactSprite(input.TexCoord0, input.RainResponseData.z, input.RainResponseData.w);
	float impactAlpha = rainImpact * splashShape * rainOcclusionPass;
	float responseAlpha = impactAlpha;
	sourceColor.w = saturate(combinedAlpha + responseAlpha);

	// Phase 1 only added alpha. Many rain textures carry near-black RGB outside
	// their original streak, so generated splash pixels were effectively invisible.
	float responseWeight = saturate(responseAlpha * 1.35f);
	sourceColor.xyz = lerp(sourceColor.xyz, max(sourceColor.xyz, 0.70f.xxx), responseWeight);

	// Extra lighting cannot brighten a near-black authored rain texture. Give
	// normal rain a controlled neutral-water RGB lift at distance while retaining
	// the active weather texture as the shape and alpha source.
	float rainOpticalWeight = saturate(
		RainResponse::GetRainDistanceWeight(rainViewDistance) *
		saturate(SharedData::rainResponseSettings.Raining) *
		(0.10f +
		 clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f) * 0.34f +
		 max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f) * 0.16f));
	sourceColor.xyz = lerp(
		sourceColor.xyz,
		max(sourceColor.xyz, 0.66f.xxx),
		rainOpticalWeight);
#	endif
	float4 baseColor = input.Color * sourceColor;
	baseColor.xyz = Color::Diffuse(baseColor.xyz);
#	if defined(GRAYSCALE_TO_COLOR)
	float3 grayScaleColor =
		TexGrayscaleTexture.Sample(SampGrayscaleTexture, float2(sourceColor.y, input.Color.x)).xyz;
	baseColor.xyz = grayScaleColor;
#	endif
#	if defined(GRAYSCALE_TO_ALPHA)
	float grayScaleAlpha =
		TexGrayscaleTexture.Sample(SampGrayscaleTexture, float2(sourceColor.w, input.Color.w)).w;
	baseColor.w = grayScaleAlpha;
#	endif
#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	float rainFarAlphaFloor = RainResponse::GetRainFinalAlphaFloor(
		rainAuthoredSourceAlpha,
		input.RainResponseData.x,
		rainViewDistance);
	baseColor.w = max(baseColor.w, rainFarAlphaFloor);

	float rainFarWeight = RainResponse::GetRainDistanceWeight(rainViewDistance);
	float rainFarOpticalLift = 1.0f +
		rainFarWeight *
		clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f) *
		0.42f;
	baseColor.xyz *= rainFarOpticalLift;
#	endif

	float3 propertyColor = 0.0;

	float2 uv = input.Position.xy * SharedData::BufferDim.zw;

	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, input.Position.z, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	float3 rainViewDirectionStd = normalize(-positionWS.xyz);
	float rainAmountStd =
		saturate(SharedData::rainResponseSettings.Raining);
	float rainLightingControlStd =
		max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f);
#	endif

	float unusedDetailedShadow;
	float3 dirLightColor = SharedData::DirLightColor.xyz * ShadowSampling::GetLightingShadow(positionWS.xyz, unusedDetailedShadow);
	float3 ambientColor = max(0, SharedData::GetAmbient(float3(0, 0, 1)));
#	if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		ambientColor = AmbientProbe::GetDiffuseAmbient(ambientColor, float3(0, 0, -1));
	}
#	endif

	propertyColor += dirLightColor;
	propertyColor += ambientColor;

#	if defined(ENVCUBE) && !defined(RAIN) && defined(RAIN_RESPONSE)
	float3 snowViewDirectionStd =
		normalize(-positionWS.xyz);

	propertyColor *=
		PIXLPrecipitation::GetSnowLightingMultiplier(
			input.SnowPrecipitationData.x);

	propertyColor *=
		PIXLPrecipitation::GetSnowDirectionalScatter(
			snowViewDirectionStd);

	float snowDirAlignment =
		pow(
			saturate(
				abs(
					dot(
						snowViewDirectionStd,
						normalize(
							SharedData::DirLightDirection.xyz)))),
			3.0f);

	propertyColor +=
		max(SharedData::DirLightColor.xyz, 0.0f) *
		snowDirAlignment *
		saturate(PIXL_Snowing) *
		(0.05f +
		 max(PIXL_SnowLightingResponse, 0.0f) * 0.18f);
#	endif

#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	// Thin-water forward/back scatter around the directional-light axis. Using the
	// actual light colour means bright overcast/sunlit skies can reveal rain
	// without turning the streaks into self-emissive white lines.
	float rainDirAlignmentStd =
		RainResponse::GetRainLightAlignment(
			rainViewDirectionStd,
			SharedData::DirLightDirection.xyz);
	propertyColor +=
		max(SharedData::DirLightColor.xyz, 0.0f) *
		rainDirAlignmentStd *
		rainAmountStd *
		(0.10f + rainLightingControlStd * 0.42f);

	propertyColor *=
		RainResponse::GetRainDirectionalScatter(rainViewDirectionStd);
#	endif

#	if defined(RADIANT_GRID)
	uint lightCount = 0;
	{
		float3 viewPosition = FrameBuffer::WorldToView(positionWS.xyz);
		float2 screenUV = FrameBuffer::ViewToUV(viewPosition);

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
				float3 lightDirection = light.positionWS.xyz - positionWS.xyz;
				float lightDist = length(lightDirection);

#		if defined(NATURAL_LIGHTING)
				float intensityMultiplier = NaturalLighting::GetAttenuation(lightDist, light);
#		else
				float intensityFactor = saturate(lightDist / light.radius);
				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
#		endif

				float3 lightColor = light.color.xyz * intensityMultiplier;
#		if defined(ENVCUBE) && !defined(RAIN) && defined(RAIN_RESPONSE)
				// Snow crystals should catch local illumination, but Skyrim's close
				// character/dialogue and carried-light response can be several times
				// brighter than the surrounding scene. Compress only the snow-particle
				// local-light term so flakes do not turn into emissive white blocks near
				// actors; directional/ambient snow lighting remains unchanged.
				float snowLocalLuma = max(
					dot(max(lightColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)),
					0.0f);
				lightColor *=
					(0.28f / (1.0f + snowLocalLuma * 0.35f));
#		endif
				propertyColor += lightColor;

#		if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
				float3 rainLocalL =
					lightDirection / max(lightDist, 1e-4f);
				float rainLocalAlignment =
					RainResponse::GetRainLightAlignment(
						rainViewDirectionStd,
						rainLocalL);
				propertyColor +=
					lightColor *
					rainLocalAlignment *
					rainAmountStd *
					(0.08f + rainLightingControlStd * 0.26f);
#		endif
			}
		}
	}
#	endif

#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	// Apply the precipitation response after all accumulated lighting, including
	// RadiantGrid local lights, so rain stays coherent around lanterns and other
	// local emitters instead of only boosting ambient/directional illumination.
	propertyColor *= RainResponse::GetRainLightingMultiplier(rainViewDistance);
#	endif

	psout.Color.xyz = propertyColor * baseColor.xyz;
#	if defined(ENVCUBE) && !defined(RAIN) && defined(RAIN_RESPONSE)
	float snowReadable =
		saturate(
			PIXLPrecipitation::GetSnowDistanceWeight(
				input.SnowPrecipitationData.x) *
			baseColor.w *
			(0.10f +
			 max(PIXL_SnowDistanceVisibility, 0.0f) * 0.24f +
			 max(PIXL_SnowLightingResponse, 0.0f) * 0.12f));

	float3 snowLitFloor =
		propertyColor *
		lerp(0.32f, 0.82f, snowReadable);

	psout.Color.xyz =
		max(
			psout.Color.xyz,
			snowLitFloor * snowReadable);
#	endif
#	if defined(ENVCUBE) && defined(RAIN) && defined(RAIN_RESPONSE)
	// Readability floor remains driven by actual accumulated particle lighting.
	float rainReadableWeight = saturate(
		RainResponse::GetRainDistanceWeight(rainViewDistance) *
		baseColor.w *
		(0.12f +
		 clamp(SharedData::rainResponseSettings.RainDistanceBoost, 0.0f, 1.5f) * 0.32f +
		 max(SharedData::rainResponseSettings.RainLightingBoost, 0.0f) * 0.14f));
	float3 rainLitFloor = propertyColor * lerp(0.30f, 0.86f, rainReadableWeight);
	psout.Color.xyz = max(psout.Color.xyz, rainLitFloor * rainReadableWeight);
#	endif
	psout.Color.w = baseColor.w;
	psout.Normal.w = baseColor.w;
	psout.Normal.xyz = float3(0, 1, 0);

	return psout;
}
#endif
