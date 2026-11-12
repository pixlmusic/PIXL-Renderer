#ifndef PIXL_RADIANT_GRID_LOCAL_SHADOW_SAMPLING
#define PIXL_RADIANT_GRID_LOCAL_SHADOW_SAMPLING

struct LocalShadowData
{
	column_major float4x4 ShadowProj;
	column_major float4x4 ShadowProj2;
	float4 Params;   // x: projection type, y: radius, z: depth bias, w: cache fade
	float4 Params2;  // x: spot falloff, y: skinned receiver offset, z: reserved cache slice
	float4 Origin;
};

// Reserved by RadiantGrid for cached native local shadow maps. The same SRVs
// are consumed by direct lighting and HybridGI's transparent-emitter injection.
StructuredBuffer<LocalShadowData> LocalShadows : register(t112);
Texture2DArray<float> LocalShadowMaps : register(t113);

float SampleLocalShadowTap(SamplerState samp, uint slice, float2 uv, float receiverDepth, float texel)
{
	float2 texelPosition = uv / texel - 0.5;
	float2 weight = frac(texelPosition);
	float4 lit = float4(LocalShadowMaps.GatherRed(samp,
		float3((floor(texelPosition) + 1.0) * texel, slice)) >= receiverDepth);
	// D3D Gather order is bottom-left, bottom-right, top-right, top-left.
	return lerp(lerp(lit.w, lit.z, weight.x), lerp(lit.x, lit.y, weight.x), weight.y);
}

float SampleLocalShadowPCF(SamplerState samp, uint slice, uint rotationSeed, float2 uv, float receiverDepth,
	float2 clampMin, float2 clampMax, float texel, uint sampleCount, float radiusUV)
{
	// A parabolic projection can put a receiver beyond its hemisphere atlas.
	// Clamping that UV to the border repeats one depth texel over a large area,
	// producing the blocky false shadows seen on nearby walls and floors.
	if (!all(isfinite(uv)) || any(uv <= clampMin) || any(uv >= clampMax))
		return 1.0f;
	float mapEdge = min(min(uv.x - clampMin.x, uv.y - clampMin.y),
		min(clampMax.x - uv.x, clampMax.y - uv.y));
	float edgeCoverage = smoothstep(texel, texel * 4.0f, mapEdge);
	float shadow = 0.0;
	// Receiver depth widens the fixed Poisson footprint into a bounded
	// contact-hardening approximation. The normalized shadow depth is monotonic
	// for both spotlight and radial point-light maps, so distant receivers get a
	// broader umbra while near contacts retain a tighter edge.
	float depthSoftening = lerp(0.70f, 1.65f, saturate(receiverDepth));
	float effectiveRadiusUV = clamp(radiusUV * depthSoftening, texel, texel * 12.0f);
	[branch] if (sampleCount <= 1) {
		shadow = SampleLocalShadowTap(samp, slice, clamp(uv, clampMin, clampMax), receiverDepth, texel);
	} else {
		// Keep a fixed orientation per cached light. Per-texel or per-frame
		// rotations change the sample footprint as receivers move and show up as
		// crawling/flickering on small emissive sources.
		float angle = frac(rotationSeed * 0.61803398875f) * Math::TAU;
		float sinAngle = 0.0;
		float cosAngle = 1.0;
		sincos(angle, sinAngle, cosAngle);
		const float2x2 rotationMatrix = float2x2(cosAngle, sinAngle, -sinAngle, cosAngle);
		const uint stride = sampleCount >= 8 ? 1 : 2;
		float sum = 0.0;
		[loop] for (uint i = 0; i < 8; i += stride) {
			float2 offset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * effectiveRadiusUV;
			sum += SampleLocalShadowTap(samp, slice, clamp(uv + offset, clampMin, clampMax), receiverDepth, texel);
		}
		shadow = sum * (stride == 1 ? 0.125 : 0.25);
	}
	return lerp(1.0f, shadow, edgeCoverage);
}

float GetLocalShadow(SamplerState samp, uint slice, float3 position, float3 eyePosition,
	float3 towardLight, bool skinnedReceiver, float unusedNoise, uint sampleCountOverride = 0u)
{
	uint dataCount = 0u;
	uint dataStride = 0u;
	uint shadowWidth = 0u;
	uint shadowHeight = 0u;
	uint shadowSlices = 0u;
	LocalShadows.GetDimensions(dataCount, dataStride);
	LocalShadowMaps.GetDimensions(shadowWidth, shadowHeight, shadowSlices);
	if (slice >= dataCount || slice >= shadowSlices || shadowWidth == 0u || shadowHeight == 0u)
		return 1.0;
	LocalShadowData data = LocalShadows[slice];
	if (!all(isfinite(data.Params)) || !all(isfinite(data.Params2)) || !all(isfinite(data.Origin)) || data.Params.y <= 0.0)
		return 1.0;
	if (data.Params.w <= 0.0)
		return 1.0;
	const float texel = SharedData::radiantGridSettings.LocalShadowTexelSize;
	const float radiusUV = SharedData::radiantGridSettings.LocalShadowFilterRadius;
	if (!isfinite(texel) || texel <= 0.0 || texel > 1.0 || !isfinite(radiusUV))
		return 1.0;

	float3 shadowPosition = position + (eyePosition - data.Origin.xyz) + towardLight * (skinnedReceiver ? data.Params2.y : 0.0);
	float4 positionLS = mul(data.ShadowProj, float4(shadowPosition, 1.0));
	const uint configuredSamples = SharedData::radiantGridSettings.LocalShadowSamples;
	const uint sampleCount = sampleCountOverride == 0u ? configuredSamples : min(sampleCountOverride, configuredSamples);
	float shadow = 1.0;

	if ((uint)data.Params.x == 0) {
		// Outside a spotlight's projected map there is no local shadow evidence.
		// The engine light's cone attenuation is already applied by RadiantGrid;
		// visibility must remain fully lit here so it cannot darken the cone edge.
		shadow = 1.0;
		if (positionLS.w > 1e-4) {
			positionLS.xyz /= positionLS.w;
			if (all(abs(positionLS.xy) < 1.0) && positionLS.z >= 0.0) {
				float2 uv = positionLS.xy * 0.5 + 0.5;
				float receiverDepth = positionLS.z - data.Params.z;
				shadow = SampleLocalShadowPCF(samp, slice, slice, uv, receiverDepth,
					texel.xx, (1.0 - texel).xx, texel, sampleCount, radiusUV);
			}
		}
	} else {
		// Skyrim's native dual-paraboloid map packs both hemispheres into one
		// slice: +Z in the upper half and -Z in the lower half. The native
		// shadow-mask shader samples descriptor zero for both hemispheres.
		const bool omni = (uint)data.Params.x == 2;
		// The native dual-paraboloid mask uses the un-divided projected XYZ and
		// selects its packed lower half at z < -1. The single-hemisphere mask
		// uses the same threshold but divides by W before projecting its UV.
		const bool lowerHalf = positionLS.z < -1.0f;
		if (!omni) {
			if (!isfinite(positionLS.w) || abs(positionLS.w) <= 1.0e-4f)
				return 1.0f;
			positionLS.xyz /= positionLS.w;
		}
		if (omni || !lowerHalf) {
			const float positionLengthSq = dot(positionLS.xyz, positionLS.xyz);
			const float3 radialDirection = positionLS.xyz * rsqrt(max(positionLengthSq, 1.0e-8f));
			const float hemisphereSign = lowerHalf ? -1.0f : 1.0f;
			float3 directionSum = radialDirection + float3(0.0, 0.0, hemisphereSign);
			const float directionLengthSq = dot(directionSum, directionSum);
			if (directionLengthSq <= 1.0e-8f)
				directionSum = float3(0.0, 0.0, hemisphereSign);
			float3 lightDirection = normalize(directionSum);
			float axialTerm = lightDirection.z;
			axialTerm = abs(axialTerm) < 1e-4 ? (axialTerm < 0.0 ? -1e-4 : 1e-4) : axialTerm;
			float2 uv = lightDirection.xy / axialTerm * 0.5 + 0.5;
			float2 clampMin = texel.xx;
			float2 clampMax = (1.0f - texel).xx;
			if (omni) {
				uv.y = lowerHalf ? 1.0f - 0.5f * uv.y : 0.5f * uv.y;
				if (lowerHalf)
					clampMin.y = 0.5f + texel;
				else
					clampMax.y = 0.5f - texel;
			}
			float receiverDepth = saturate(length(positionLS.xyz) / data.Params.y) - data.Params.z;
			shadow = SampleLocalShadowPCF(samp, slice, slice, uv, receiverDepth,
				clampMin, clampMax, texel, sampleCount, radiusUV);
		}
	}
	return lerp(1.0, shadow, saturate(data.Params.w));
}

#endif
