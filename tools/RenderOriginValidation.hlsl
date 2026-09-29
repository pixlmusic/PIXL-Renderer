#include "Common/PIXLRenderOrigin.hlsli"
RWStructuredBuffer<float4> Results : register(u0);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float3 p = float3(id) + float3(0.125f, 0.25f, 8.5f);
    float3 render = PIXLRenderOrigin::EngineToRender(p);
    Results[0] = PIXLRenderOrigin::ProjectRender(render);
    Results[1] = PIXLRenderOrigin::ProjectPreviousRender(PIXLRenderOrigin::CurrentToPreviousRender(render));
    Results[2] = float4(PIXLRenderOrigin::CurrentEngineToPreviousEngine(p), PIXLRenderOrigin::Epoch());
    Results[3] = float4(PIXLRenderOrigin::WorldToRender(PIXLRenderOrigin::RenderToWorld(render)), PIXLRenderOrigin::HistoryValid());
    Results[4] = SharedData::PreviousRenderOriginHigh + SharedData::PreviousRenderOriginLow;
    Results[5] = float4(PIXLRenderOrigin::ReconstructRenderPosition(float2(0.5f,0.5f), 0.5f), 1);
    Results[6] = float4(PIXLRenderOrigin::PeriodicWorldPosition(render, 4096.0f), 1);
}
