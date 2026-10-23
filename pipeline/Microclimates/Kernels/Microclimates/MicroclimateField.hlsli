#ifndef PIXL_MICROCLIMATE_FIELD_HLSLI
#define PIXL_MICROCLIMATE_FIELD_HLSLI

#ifndef PIXL_MICROCLIMATE_EXTERNAL_CONSTANTS
cbuffer PIXLMicroclimateFieldCB : register(b13)
{
	float4 PIXLMicroFieldOriginExtent;
	float4 PIXLMicroPreviousOriginWind;
	float4 PIXLMicroBaseWeather;
	float4 PIXLMicroEmitterPosition;
	float4 PIXLMicroEmitterWeather;
	float4 PIXLMicroControls;
	float4 PIXLMicroExtendedControls;
};
#endif

Texture2D<float4> PIXLMicroclimateField : register(t101);
Texture2D<float2> PIXLMicroclimateStormField : register(t102);
SamplerState PIXLMicroclimateSampler : register(s4);

namespace Microclimates
{
	float FieldSmootherStep(float value)
	{
		float t = saturate(value);
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	bool ResolveFieldUV(float3 worldPosition, out float2 uv)
	{
		uv = 0.0f.xx;
		if (!all(isfinite(worldPosition)) || !all(isfinite(PIXLMicroFieldOriginExtent.xy)) ||
			!isfinite(PIXLMicroFieldOriginExtent.z) || PIXLMicroFieldOriginExtent.z <= 0.0f)
			return false;

		uv = (worldPosition.xy - PIXLMicroFieldOriginExtent.xy) / PIXLMicroFieldOriginExtent.z;
		return all(isfinite(uv)) && all(uv >= 0.0f) && all(uv <= 1.0f);
	}

	float FieldEdgeFade(float2 uv)
	{
		float2 edgeDistance = min(uv, 1.0f - uv);
		return FieldSmootherStep(saturate(min(edgeDistance.x, edgeDistance.y) / 0.06f));
	}

	float4 SampleField(float3 worldPosition)
	{
		float2 uv;
		if (PIXLMicroControls.x < 0.5f || !ResolveFieldUV(worldPosition, uv))
			return 0.0f.xxxx;
		float4 field = saturate(PIXLMicroclimateField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f));
		return field * FieldEdgeFade(uv);
	}

	float SampleFog(float3 worldPosition)
	{
		if (!all(isfinite(worldPosition)))
			return 0.0f;
		float4 field = SampleField(worldPosition);
		float heightAboveEmitter = max(worldPosition.z - PIXLMicroControls.w - 2500.0f, 0.0f);
		float lowlandScale = lerp(12000.0f, 6000.0f, saturate(PIXLMicroExtendedControls.x));
		return saturate(field.g * exp2(-1.442695f * heightAboveEmitter / lowlandScale));
	}

	float SamplePrecipitation(float3 worldPosition)
	{
		return saturate(SampleField(worldPosition).b);
	}

	float SampleStormPotential(float3 worldPosition)
	{
		float2 uv;
		if (PIXLMicroControls.x < 0.5f || !ResolveFieldUV(worldPosition, uv))
			return 0.0f;
		return saturate(PIXLMicroclimateStormField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f).x) * FieldEdgeFade(uv);
	}

	float SampleWeatherInfluence(float3 worldPosition)
	{
		float2 uv;
		if (PIXLMicroControls.x < 0.5f || !ResolveFieldUV(worldPosition, uv))
			return 0.0f;
		return saturate(PIXLMicroclimateStormField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f).y) * FieldEdgeFade(uv);
	}

	float BlendRegionalPrecipitation(float3 worldPosition, float basePrecipitation)
	{
		float influence = SampleWeatherInfluence(worldPosition);
		float base = (asuint(basePrecipitation) & 0x7f800000u) == 0x7f800000u ? 0.0f : saturate(basePrecipitation);
		return saturate(lerp(base, SamplePrecipitation(worldPosition), influence));
	}
}

#endif
