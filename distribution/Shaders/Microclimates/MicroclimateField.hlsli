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
	float4 SampleField(float3 worldPosition)
	{
		if (PIXLMicroControls.x < 0.5f)
			return 0.0f.xxxx;
		float2 uv = (worldPosition.xy - PIXLMicroFieldOriginExtent.xy) / max(PIXLMicroFieldOriginExtent.z, 1.0f);
		if (any(uv < 0.0f) || any(uv > 1.0f))
			return 0.0f.xxxx;
		return PIXLMicroclimateField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f);
	}

	float SampleFog(float3 worldPosition)
	{
		float4 field = SampleField(worldPosition);
		float heightAboveEmitter = max(worldPosition.z - PIXLMicroControls.w - 2500.0f, 0.0f);
		float lowlandScale = lerp(12000.0f, 6000.0f, saturate(PIXLMicroExtendedControls.x));
		return saturate(field.g * exp(-heightAboveEmitter / lowlandScale));
	}

	float SamplePrecipitation(float3 worldPosition)
	{
		return saturate(SampleField(worldPosition).b);
	}

	float SampleStormPotential(float3 worldPosition)
	{
		float2 uv = (worldPosition.xy - PIXLMicroFieldOriginExtent.xy) / max(PIXLMicroFieldOriginExtent.z, 1.0f);
		if (PIXLMicroControls.x < 0.5f || any(uv < 0.0f) || any(uv > 1.0f))
			return 0.0f;
		return saturate(PIXLMicroclimateStormField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f).x);
	}

	float SampleWeatherInfluence(float3 worldPosition)
	{
		float2 uv = (worldPosition.xy - PIXLMicroFieldOriginExtent.xy) / max(PIXLMicroFieldOriginExtent.z, 1.0f);
		if (PIXLMicroControls.x < 0.5f || any(uv < 0.0f) || any(uv > 1.0f))
			return 0.0f;
		float2 edgeDistance = min(uv, 1.0f - uv);
	float edgeFade = smoothstep(0.0f, 0.06f, min(edgeDistance.x, edgeDistance.y));
		return saturate(PIXLMicroclimateStormField.SampleLevel(PIXLMicroclimateSampler, uv, 0.0f).y * edgeFade);
	}

	float BlendRegionalPrecipitation(float3 worldPosition, float basePrecipitation)
	{
		float influence = SampleWeatherInfluence(worldPosition);
		return lerp(saturate(basePrecipitation), SamplePrecipitation(worldPosition), influence);
	}
}

#endif
