#include "Atmosphere/VolumetricFogCSCommon.hlsli"

#if defined(RAIN_RESPONSE)
#	include "RainResponse/Precipitation.hlsli"
#endif

RWTexture3D<float4> VBufferA : register(u0);

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!Atmosphere::IsInsideVolumetricGrid(dispatchID))
		return;

	float viewDepth;
	float3 positionWS = Atmosphere::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, viewDepth);

	float extinction = Atmosphere::EvaluateHeightFogExtinction(positionWS, FrameBuffer::CameraPosAdjust.xyz);
	float3 albedo = saturate(SharedData::atmosphereSettings.volumetricFogAlbedo.rgb);
	float3 scattering = extinction * albedo * SharedData::atmosphereSettings.volumetricFogAlbedo.a;

#if defined(RAIN_RESPONSE)
	// Rain aerosol participates in the same physically lit VBuffer as the rest
	// of PIXL fog, but never leaks into interiors, HideSky spaces, or map views.
	[branch] if (!SharedData::InInterior && !SharedData::HideSky && !SharedData::InMapMenu)
		RainResponse::ApplyVolumetricRainMist(positionWS, viewDepth, scattering, extinction);
#endif

	VBufferA[dispatchID] = float4(scattering, extinction);
}
