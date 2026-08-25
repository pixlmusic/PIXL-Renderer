#include "CameraSuite/PhysicalCameraCommon.hlsli"

Texture2D<float3> LowResolutionTex : register(t0);
Texture2D<float3> HighResolutionTex : register(t1);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float3> OutputTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 dispatchID : SV_DispatchThreadID)
{
    uint outputWidth, outputHeight;
    OutputTex.GetDimensions(outputWidth, outputHeight);
    if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight)
        return;

    float2 uv = (float2(dispatchID) + 0.5f) / float2(outputWidth, outputHeight);
    uint lowWidth, lowHeight;
    LowResolutionTex.GetDimensions(lowWidth, lowHeight);
    float2 texel = rcp(float2(lowWidth, lowHeight));
    float radius = max(bloomRadius, 0.1f);

    const float2 stepSize = texel * radius;
    // Full 3x3 tent reconstruction.  Diagonal support is essential here: a
    // cross-shaped kernel turns isolated highlights into four offset cutouts.
    float3 low = 0.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2(-1.0f, -1.0f), 0.0f);
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2( 0.0f, -1.0f), 0.0f) * 2.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2( 1.0f, -1.0f), 0.0f);
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2(-1.0f,  0.0f), 0.0f) * 2.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv, 0.0f) * 4.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2( 1.0f,  0.0f), 0.0f) * 2.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2(-1.0f,  1.0f), 0.0f);
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2( 0.0f,  1.0f), 0.0f) * 2.0f;
    low += LowResolutionTex.SampleLevel(LinearClampSampler, uv + stepSize * float2( 1.0f,  1.0f), 0.0f);
    low *= (1.0f / 16.0f);

    float3 high = HighResolutionTex.Load(int3(dispatchID, 0));
    OutputTex[dispatchID] = high + low;
}
