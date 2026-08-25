
#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/MotionBlur.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
Texture2D<float3> SpecularTexture : register(t0);
Texture2D<unorm float3> AlbedoTexture : register(t1);
Texture2D<unorm float3> NormalRoughnessTexture : register(t2);
Texture2D<float3> MasksTexture : register(t3);
Texture2D<unorm float> Masks2Texture : register(t9);
Texture2D<float3> PixlInputRadianceTexture : register(t16);
Texture2D<float4> PixlBentVisibilityTexture : register(t17);

cbuffer PixlGIDebugCB : register(b13)
{
	uint PixlGIDebugMode;
	float PixlGIDebugGain;
	uint PixlSpecularOcclusionEnabled;
	float PixlSpecularOcclusionStrength;
};

RWTexture2D<float4> MainRW : register(u0);
RWTexture2D<float4> NormalTAAMaskSpecularMaskRW : register(u1);
RWTexture2D<float2> MotionVectorsRW : register(u2);

// 24/32-bit depth: TerrainSeam ON -> R32_FLOAT (no unorm),
// OFF -> R24_UNORM_X8_TYPELESS game depth (unorm).
#if defined(TERRAIN_SEAM)
Texture2D<float> DepthTexture : register(t4);
#else
Texture2D<unorm float> DepthTexture : register(t4);
#endif

#if defined(WORLD_PROBES)
Texture2D<float3> ReflectanceTexture : register(t5);
TextureCube<float3> EnvTexture : register(t6);
TextureCube<float3> EnvReflectionsTexture : register(t7);
SamplerState LinearSampler : register(s0);
#endif

#if defined(SKY_BOUNCE)
#	define SKY_BOUNCE_PROBE_REGISTER t8
#	include "SkyBounce/SkyBounce.hlsli"
#endif

#if defined(HYBRID_GI)
Texture2D<float4> SsgiAoTexture : register(t10);
Texture2D<float4> SsgiYTexture : register(t11);
Texture2D<float4> SsgiCoCgTexture : register(t12);
Texture2D<float4> SsgiSpecularTexture : register(t13);

void SampleHybridGI(uint2 pixCoord, float3 normalWS, out float ao, out float3 il)
{
	ao = 1 - SsgiAoTexture[pixCoord].x;
	float4 ssgiIlYSh = SsgiYTexture[pixCoord];
	// without ZH hallucination
	// float ssgiIlY = SphericalHarmonics::FuncProductIntegral(ssgiIlYSh, SphericalHarmonics::EvaluateCosineLobe(normalWS));
	float ssgiIlY = SphericalHarmonics::SHHallucinateZH3Irradiance(ssgiIlYSh, normalWS);
	float2 ssgiIlCoCg = SsgiCoCgTexture[pixCoord].xy;
	il = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg)));
}

float4 SamplePixlBentVisibility(uint2 pixCoord, float3 geometricNormal)
{
	float4 packed = PixlBentVisibilityTexture[pixCoord];
	float confidence = saturate(packed.w);
	float4 result = float4(geometricNormal, 1.0f);
	if (confidence > 1e-4f) {
		float3 bentNormal = GBuffer::DecodeNormal(packed.xy);
		if (dot(bentNormal, bentNormal) < 0.25f)
			bentNormal = geometricNormal;
		bentNormal = normalize(lerp(geometricNormal, bentNormal, confidence));
		result = float4(bentNormal, saturate(packed.z));
	}
	return result;
}

float ComputePixlSpecularVisibility(float3 normal, float3 view, float3 bentNormal, float hemisphereVisibility, float roughness)
{
	float result = 1.0f;
	if (PixlSpecularOcclusionEnabled != 0u) {
		float NdotV = saturate(dot(normal, view));
		float alpha = max(roughness * roughness, 1e-4f);
		float broadVisibility = saturate(SpecularOcclusion(NdotV, alpha, saturate(hemisphereVisibility)));

		// Treat the bent normal and visible-hemisphere fraction as a coarse
		// visibility cone. Smooth lobes prefer the reflected ray direction while
		// rough lobes progressively converge toward hemispherical visibility.
		float3 R = reflect(-view, normal);
		float coneCos = lerp(0.0f, 0.92f, 1.0f - saturate(hemisphereVisibility));
		float directionalVisibility = smoothstep(coneCos - 0.12f, coneCos + 0.12f, dot(R, bentNormal));
		float roughWeight = smoothstep(0.08f, 0.72f, roughness);
		float visibility = lerp(directionalVisibility, broadVisibility, roughWeight);
		result = lerp(1.0f, saturate(visibility), saturate(PixlSpecularOcclusionStrength));
	}
	return result;
}

void SampleHybridGISpecular(uint2 pixCoord, sh2 lobe, out float3 legacyIl, out float3 hybridReflection, out float hybridConfidence)
{
	float4 ssgiIlYSh = SsgiYTexture[pixCoord];
	float ssgiIlY = SphericalHarmonics::FuncProductIntegral(ssgiIlYSh, lobe);
	float2 ssgiIlCoCg = SsgiCoCgTexture[pixCoord].xy;

	// The SH term is incident rough-specular irradiance and is still useful
	// where the stochastic tracer has no screen/world observation.
	legacyIl = max(0, Color::YCoCgToRGB(float3(ssgiIlY, ssgiIlCoCg / Math::PI)));

	// Hybrid reflection is incident radiance. Material reflectance is applied
	// once in the composite after confidence replaces the probe/SH fallback.
	float4 hybrid = SsgiSpecularTexture[pixCoord];
	hybridReflection = max(hybrid.rgb, 0.0f);
	hybridConfidence = saturate(hybrid.a);
}
#endif

#if defined(AMBIENT_PROBE)
#	if !defined(WORLD_PROBES)
#		undef AMBIENT_PROBE
#	else
#		define AMBIENT_PROBE_DEFERRED
#		include "AmbientProbe/AmbientProbe.hlsli"
#	endif
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside screen bounds
	if (any(dispatchID.xy >= uint2(SharedData::BufferDim.xy)))
		return;

	float2 uv = float2(dispatchID.xy + 0.5) * SharedData::BufferDim.zw;
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // adjust for dynamic res

	float3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	float3 normalVS = GBuffer::DecodeNormal(normalGlossiness.xy);

	float3 diffuseColor = MainRW[dispatchID.xy].xyz;
	float3 specularColor = SpecularTexture[dispatchID.xy];
	float3 albedo = AlbedoTexture[dispatchID.xy];

	float depth = DepthTexture[dispatchID.xy];
	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse, positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

	if (depth == 1.0)
		MotionVectorsRW[dispatchID.xy] = MotionBlur::GetSSMotionVector(positionWS, positionWS);  // Apply sky motion vectors

	float glossiness = normalGlossiness.z;

	float3 linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
	float3 normalWS = normalize(mul(FrameBuffer::CameraViewInverse, float4(normalVS, 0)).xyz);
	float3 pixlDebugDiffuseGI = 0.0;
	float3 pixlDebugSpecularGI = 0.0;
	float pixlDebugAO = 0.0;
	float4 pixlBentVisibility = float4(normalWS, 1.0f);
	float3 ambientNormalWS = normalWS;
	float pixlSpecularVisibility = 1.0f;

#if defined(HYBRID_GI)

	float ssgiAo;
	float3 ssgiIl;
	SampleHybridGI(dispatchID.xy, normalWS, ssgiAo, ssgiIl);
	pixlBentVisibility = SamplePixlBentVisibility(dispatchID.xy, normalWS);
	ambientNormalWS = pixlBentVisibility.xyz;
	pixlDebugAO = 1.0 - ssgiAo;
	pixlDebugDiffuseGI = ssgiIl;

	// Masks2.x stores 1 - vertexAO (Lighting.hlsl only); cleared to 0 for
	// pixels with no vertex AO contribution, so vertexAO defaults to 1.
	float vertexAO = 1.0 - Masks2Texture[dispatchID.xy].x;
	ssgiAo = saturate(ssgiAo / max(vertexAO, EPSILON_DIVISION));

	float3 linAlbedo = Color::IrradianceToLinear(albedo / Color::PBRLightingScale);
	float3 multiBounceHybridGIAo = MultiBounceAO(linAlbedo, ssgiAo);

	float3 directionalAmbientColor = 0;

#	if defined(AMBIENT_PROBE)
	if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
		float3 vanillaDALC = Color::Ambient(max(0, SharedData::GetAmbient(ambientNormalWS)));

#		if defined(SKY_BOUNCE)
		float3 positionMS = positionWS.xyz;
		sh2 skyBounceSH = SkyBounce::Sample(positionMS.xyz, ambientNormalWS);
		float skyBounceDiffuse = SkyBounce::EvaluateDiffuse(skyBounceSH, ambientNormalWS);
		directionalAmbientColor = AmbientProbe::GetDiffuseAmbientOccluded(vanillaDALC, -ambientNormalWS, skyBounceDiffuse) * albedo;
#		else
		directionalAmbientColor = AmbientProbe::GetDiffuseAmbient(vanillaDALC, -ambientNormalWS) * albedo;
#		endif

		directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
		directionalAmbientColor.x = MasksTexture[dispatchID.xy].z;
		directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
		directionalAmbientColor = max(0, directionalAmbientColor);
	} else
#	endif
	{
		directionalAmbientColor = Color::Ambient(max(0, SharedData::GetAmbient(ambientNormalWS)));
		directionalAmbientColor *= albedo;

		directionalAmbientColor = Color::RGBToYCoCg(directionalAmbientColor);
		directionalAmbientColor.x = MasksTexture[dispatchID.xy].z;
		directionalAmbientColor = Color::YCoCgToRGB(directionalAmbientColor);
		directionalAmbientColor = max(0, directionalAmbientColor);
	}

	{
		float maxScale = 1.0;
		if (directionalAmbientColor.x > 0.0)
			maxScale = min(maxScale, diffuseColor.x / directionalAmbientColor.x);
		if (directionalAmbientColor.y > 0.0)
			maxScale = min(maxScale, diffuseColor.y / directionalAmbientColor.y);
		if (directionalAmbientColor.z > 0.0)
			maxScale = min(maxScale, diffuseColor.z / directionalAmbientColor.z);
		directionalAmbientColor *= maxScale;

		diffuseColor = max(0.0, diffuseColor - directionalAmbientColor);
		linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
		linDiffuseColor *= sqrt(multiBounceHybridGIAo);
		diffuseColor = Color::IrradianceToGamma(linDiffuseColor);
		diffuseColor += Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColor) * multiBounceHybridGIAo);
		linDiffuseColor = Color::IrradianceToLinear(diffuseColor);
	}

	linDiffuseColor += ssgiIl * linAlbedo;
#endif

	float3 color = linDiffuseColor + specularColor;

#if defined(WORLD_PROBES)

	float3 reflectance = ReflectanceTexture[dispatchID.xy];

	if (any(reflectance > 0.0)) {
		float3 V = -normalize(positionWS.xyz);
		float3 R = reflect(-V, normalWS);

		float roughness = 1.0 - glossiness;
		float level = roughness * 7.0;

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(normalWS, V, roughness);

		float3 finalIrradiance = 0;

		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(max(0, SharedData::GetAmbient(R)))) * Color::ReflectionNormalisationScale;

#	if defined(SKY_BOUNCE)
		float3 positionMS = positionWS.xyz;

		sh2 skyBounceSH = SkyBounce::Sample(positionMS.xyz, R);
		float skyBounceSpecular = SkyBounce::EvaluateSpecular(skyBounceSH, specularLobe);
		float skyBounceVisibility = SkyBounce::EvaluateVisibility(skyBounceSH);
#	endif

#	if defined(AMBIENT_PROBE)
		if (SharedData::ambientProbeSettings.EnableAmbientProbe) {
			float3 envSample = EnvTexture.SampleLevel(LinearSampler, R, level);
			float3 fullSample = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
			float3 envSpecular, skySpecular;

			if (SharedData::ambientProbeSettings.DALCMode >= 2) {
				// Mode 2/3: DALC-normalized env scaled by DALCAmount + sky overlay
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
				envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::ambientProbeSettings.DALCAmount;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::ambientProbeSettings.SkyProbeScale;
#		if defined(SKY_BOUNCE)
				envSpecular *= (SharedData::ambientProbeSettings.DALCMode == 3) ? skyBounceVisibility : 1.0;
				skySpecular *= skyBounceSpecular;
#		endif
			} else {
				// Mode 0/1: AmbientProbe ratio-based
				float3 ratio = AmbientProbe::GetAmbientRatio();
				envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::ambientProbeSettings.EnvironmentProbeScale;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::ambientProbeSettings.SkyProbeScale;
#		if defined(SKY_BOUNCE)
				skySpecular *= skyBounceSpecular;
#		endif
			}
			if (SharedData::InInterior) {
				skySpecular = 0;
			}

			finalIrradiance = envSpecular + skySpecular;
		} else
#	endif
		{
			// Fallback without AmbientProbe: normalize-by-luminance with DALC
#	if defined(INTERIOR)
			float3 specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#	elif defined(SKY_BOUNCE)
			float3 specularIrradianceReflections = 0.0;
			if (skyBounceSpecular > 0.0) {
				specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
				float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, R, 15));
				specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
				specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
			}
			float3 specularIrradiance = 0.0;
			if (skyBounceSpecular < 1.0) {
				specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level);
				float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, R, 15));
				float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skyBounceSpecular);
				specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
				specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
			}
			finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skyBounceSpecular);
#	else
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#	endif
		}

#	if defined(HYBRID_GI)
		float3 ssgiIlSpecular;
		float3 hybridReflection;
		float hybridConfidence;
		SampleHybridGISpecular(dispatchID.xy, specularLobe, ssgiIlSpecular, hybridReflection, hybridConfidence);

		pixlSpecularVisibility = ComputePixlSpecularVisibility(normalWS, V, pixlBentVisibility.xyz, pixlBentVisibility.w, roughness);
		finalIrradiance *= pixlSpecularVisibility;

		// Keep the SH lobe as part of the fallback. The confidence blend below
		// replaces that fallback with a validated stochastic observation.
		ssgiIlSpecular = Color::RGBToYCoCg(ssgiIlSpecular);
		if (ssgiIlSpecular.x > 0.0) {
			ssgiIlSpecular = max(0, Color::YCoCgToRGB(float3(ssgiIlSpecular.x, lerp(ssgiIlSpecular.yz, Color::RGBToYCoCg(finalIrradiance).yz, 0.5))));
		} else {
			ssgiIlSpecular = 0;
		}

		float3 fallbackIrradiance = finalIrradiance + ssgiIlSpecular;
		finalIrradiance = lerp(fallbackIrradiance, hybridReflection, hybridConfidence);
		pixlDebugSpecularGI = reflectance * finalIrradiance;
#	endif

		color += reflectance * finalIrradiance;
	}

#endif

	color = Color::IrradianceToGamma(color);

#if defined(HYBRID_GI)
	// Full-screen diagnostic views are selected at runtime, so capturing them
	// never requires a shader recompile or invalidates DLSS history.
	if (PixlGIDebugMode != 0u) {
		float3 diagnostic = 0.0;
		if (PixlGIDebugMode == 1u)
			diagnostic = pixlDebugAO.xxx;
		else if (PixlGIDebugMode == 2u || (PixlGIDebugMode >= 5u && PixlGIDebugMode <= 9u))
			diagnostic = pixlDebugDiffuseGI;
		else if (PixlGIDebugMode == 3u)
			diagnostic = pixlDebugSpecularGI;
		else if (PixlGIDebugMode == 4u)
			diagnostic = PixlInputRadianceTexture[dispatchID.xy];
		else if (PixlGIDebugMode == 10u)
			diagnostic = pixlBentVisibility.xyz * 0.5f + 0.5f;
		else if (PixlGIDebugMode == 11u)
			diagnostic = pixlBentVisibility.www;
		else if (PixlGIDebugMode == 12u)
			diagnostic = pixlSpecularVisibility.xxx;
		else if (PixlGIDebugMode == 13u)
			diagnostic = pixlDebugSpecularGI;
		color = saturate(Color::IrradianceToGamma(max(diagnostic * PixlGIDebugGain, 0.0)));
	}
#endif

	MainRW[dispatchID.xy] = float4(color, 1.0);
	NormalTAAMaskSpecularMaskRW[dispatchID.xy] = float4(GBuffer::EncodeNormalVanilla(normalVS), 0.0, 0.0);
}
