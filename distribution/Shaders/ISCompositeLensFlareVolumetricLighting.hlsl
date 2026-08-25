#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float3 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState VLSourceSampler : register(s0);
SamplerState LFSourceSampler : register(s1);

Texture2D<float4> VLSourceTex : register(t0);
Texture2D<float4> LFSourceTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 VolumetricLightingColor : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.0.xxx;

#	if defined(LIGHT_VOLUMES)
	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float volumetricLightingPower = VLSourceTex.Sample(VLSourceSampler, screenPosition).x;
	float3 volumetricLightingColor = VolumetricLightingColor.xyz;
	color += volumetricLightingColor * Color::LightVolumes(volumetricLightingPower.xxx).x;
#	endif

#	if defined(LENS_FLARE)
	float3 lensFlareColor = LFSourceTex.Sample(LFSourceSampler, input.TexCoord).xyz;
	if (SharedData::linearLightCoreSettings.enableLinearLightCore) {
		color += Color::SkyrimGammaToLinear(lensFlareColor);
	} else {
		color += lensFlareColor;
	}
#	endif

	psout.Color = color;

	return psout;
}
#endif
