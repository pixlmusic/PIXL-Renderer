Texture2D<float3> SourceTex : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float3> OutputTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 dispatchID : SV_DispatchThreadID)
{
    uint outputWidth, outputHeight;
    OutputTex.GetDimensions(outputWidth, outputHeight);
    if (dispatchID.x >= outputWidth || dispatchID.y >= outputHeight)
        return;

    uint sourceWidth, sourceHeight;
    SourceTex.GetDimensions(sourceWidth, sourceHeight);
    const float2 uv = (float2(dispatchID) + 0.5f) / float2(outputWidth, outputHeight);
    const float2 texel = rcp(float2(sourceWidth, sourceHeight));

    // Rotationally symmetric 13-tap reduction.  The inner diagonal taps carry
    // more weight than the wide ring, producing a continuous energy-preserving
    // pyramid without the cardinal lobes of a four-sample box filter.
    float3 result = SourceTex.SampleLevel(LinearClampSampler, uv, 0.0f) * 4.0f;
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0f, -1.0f), 0.0f) * 2.0f;
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0f, -1.0f), 0.0f) * 2.0f;
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0f,  1.0f), 0.0f) * 2.0f;
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0f,  1.0f), 0.0f) * 2.0f;
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2(-2.0f,  0.0f), 0.0f);
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2( 2.0f,  0.0f), 0.0f);
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0f, -2.0f), 0.0f);
    result += SourceTex.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0f,  2.0f), 0.0f);
    OutputTex[dispatchID] = result * (1.0f / 16.0f);
}
