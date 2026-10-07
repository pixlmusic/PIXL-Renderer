#include "Atmosphere/VolumetricFogCSCommon.hlsli"

#if defined(RAIN_RESPONSE)
#	include "RainResponse/Precipitation.hlsli"
#endif

#include "Microclimates/MicroclimateField.hlsli"

RWTexture3D<float4> VBufferA : register(u0);

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!Atmosphere::IsInsideVolumetricGrid(dispatchID))
		return;

	float viewDepth;
	float3 positionWS = Atmosphere::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, viewDepth);

	float extinction = Atmosphere::EvaluateHeightFogExtinction(positionWS, FrameBuffer::CameraPosAdjust.xyz);
	float3 absolutePosition = positionWS + FrameBuffer::CameraPosAdjust.xyz;
	float3 albedo = saturate(SharedData::atmosphereSettings.volumetricFogAlbedo.rgb);
	float3 scattering = extinction * albedo * SharedData::atmosphereSettings.volumetricFogAlbedo.a;
	uint microDebug = (uint)(PIXLMicroControls.y + 0.5f);
	[branch] if (PIXLMicroControls.x > 0.5f && !SharedData::InInterior && !SharedData::HideSky && !SharedData::InMapMenu) {
		float4 microField = Microclimates::SampleField(absolutePosition);
		float regionalFog = Microclimates::SampleFog(absolutePosition);
		float regionalPrecipitation = Microclimates::SamplePrecipitation(absolutePosition);
		// Keep the response spatial: rain aerosol is strongest inside the weather
		// cell and falls away with its local fog field instead of tinting the view.
		float localAerosol = regionalPrecipitation * (0.000025f + regionalFog * 0.000045f);
		extinction += regionalFog * 0.00022f + localAerosol;
		if (microDebug != 0u) {
			float value = microDebug == 1u ? microField.g : microDebug == 2u ? microField.b :
				microDebug == 3u ? microField.r : microDebug == 4u ? microField.a :
				Microclimates::SampleStormPotential(absolutePosition);
			extinction = saturate(value) * 0.00025f;
			float3 debugColor = microDebug == 1u ? float3(0.2f, 0.7f, 1.0f) :
				microDebug == 2u ? float3(0.7f, 0.8f, 1.0f) :
				microDebug == 3u ? float3(0.2f, 1.0f, 0.35f) : float3(0.7f, 0.25f, 1.0f);
			scattering = debugColor * extinction;
		}
	}

#if defined(RAIN_RESPONSE)
	// Rain aerosol participates in the same physically lit VBuffer as the rest
	// of PIXL fog, but never leaks into interiors, HideSky spaces, or map views.
	[branch] if (!SharedData::InInterior && !SharedData::HideSky && !SharedData::InMapMenu)
		RainResponse::ApplyVolumetricRainMist(positionWS, viewDepth, scattering, extinction);
#endif

	VBufferA[dispatchID] = float4(scattering, extinction);
}
