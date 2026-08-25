#include "Common/GBuffer.hlsli"
#include "HybridGI/common.hlsli"

Texture2D<float4> srcReflection : register(t0);
Texture2D<float> srcDepth : register(t1);
Texture2D<float4> srcNormalRoughness : register(t2);
RWTexture2D<float4> outReflection : register(u0);

static const int2 PIXL_REFLECTION_OFFSETS[9] = {
    int2(0, 0), int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1),
    int2(1, 1), int2(-1, 1), int2(1, -1), int2(-1, -1)
};
static const float PIXL_REFLECTION_SPATIAL[9] = {
    1.0f, 0.78f, 0.78f, 0.78f, 0.78f, 0.52f, 0.52f, 0.52f, 0.52f
};

float ReflectionLuminance(float3 c)
{
    return dot(max(c, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ReflectionSafeNormal(uint2 p, float2 uv)
{
    return GBuffer::DecodeNormal(FULLRES_LOAD(srcNormalRoughness, p, uv * (FrameDim * RcpTexDim), samplerLinearClamp).xy);
}

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
    if (any(dtid >= uint2(OUT_FRAME_DIM)))
        return;

    float2 uv = (dtid + 0.5f) * RCP_OUT_FRAME_DIM;
    float4 center = srcReflection[dtid];
    float centerDepth = READ_DEPTH(srcDepth, dtid);
    if (centerDepth <= FP_Z || centerDepth >= DepthFadeRange.y) {
        outReflection[dtid] = 0.0f;
        return;
    }

    float4 nr = FULLRES_LOAD(srcNormalRoughness, dtid, uv * (FrameDim * RcpTexDim), samplerLinearClamp);
    float3 centerNormal = GBuffer::DecodeNormal(nr.xy);
    float centerRoughness = saturate(1.0f - nr.z);

    // Mirror-like reflections must stay sharp. Rough reflections can borrow a
    // wider neighborhood because their true BRDF footprint is already broad.
    int stepPx = centerRoughness > 0.48f ? 2 : 1;
    float filterAmount = smoothstep(0.04f, 0.42f, centerRoughness);
    // Missing stochastic samples need *more* reconstruction, not less.
    // Preserve sharp confident mirror hits, but aggressively repair low-alpha holes.
    filterAmount = max(filterAmount, (1.0f - saturate(center.a)) * 0.72f);

    float3 sum = 0.0f;
    float alphaSum = 0.0f;
    float weightSum = 0.0f;
    float centerLum = ReflectionLuminance(center.rgb);
    float relativeDepthScale = max(centerDepth * 0.015f + ReflectionThickness * 0.35f, 1.0f);

    [unroll]
    for (uint i = 0u; i < 9u; ++i) {
        int2 q = int2(dtid) + PIXL_REFLECTION_OFFSETS[i] * stepPx;
        q = clamp(q, int2(0, 0), int2(OUT_FRAME_DIM) - 1);
        uint2 qu = uint2(q);
        float2 quv = (float2(qu) + 0.5f) * RCP_OUT_FRAME_DIM;

        float4 sampleValue = srcReflection[qu];
        float sampleDepth = READ_DEPTH(srcDepth, qu);
        float4 sampleNR = FULLRES_LOAD(srcNormalRoughness, qu, quv * (FrameDim * RcpTexDim), samplerLinearClamp);
        float3 sampleNormal = GBuffer::DecodeNormal(sampleNR.xy);
        float sampleRoughness = saturate(1.0f - sampleNR.z);

        float depthWeight = exp2(-abs(sampleDepth - centerDepth) / relativeDepthScale * 5.0f);
        float normalAgreement = saturate(dot(centerNormal, sampleNormal));
        float normalWeight = normalAgreement * normalAgreement;
        normalWeight *= normalWeight;
        float roughnessWeight = exp2(-abs(sampleRoughness - centerRoughness) * 12.0f);
        float sampleConfidence = saturate(sampleValue.a);
        // Near-empty samples must not drag a valid neighbourhood toward black.
        // Square confidence so reliable observations dominate hole reconstruction.
        float confidenceWeight = 0.05f + 0.95f * sampleConfidence * sampleConfidence;
        float w = PIXL_REFLECTION_SPATIAL[i] * depthWeight * normalWeight * roughnessWeight * confidenceWeight;

        // Keep a bright stochastic outlier from spreading through the filter,
        // but allow empty/low-confidence centre pixels to be reconstructed from
        // trustworthy neighbours instead of remaining black.
        float3 sampleRgb = max(sampleValue.rgb, 0.0f);
        if (center.a > 0.05f && centerLum > 1e-4f) {
            float sampleLum = ReflectionLuminance(sampleRgb);
            float upperLum = centerLum * lerp(2.0f, 4.0f, centerRoughness) + 0.04f;
            if (sampleLum > upperLum)
                sampleRgb *= upperLum / max(sampleLum, 1e-5f);
        }

        sum += sampleRgb * w;
        alphaSum += sampleValue.a * w;
        weightSum += w;
    }

    float invWeight = rcp(max(weightSum, 1e-5f));
    float3 filtered = sum * invWeight;
    float filteredConfidence = saturate(alphaSum * invWeight);

    // Low-confidence stochastic holes benefit most from the spatial estimate;
    // reliable glossy hits keep more of their temporally reconstructed centre.
    float holeAmount = 1.0f - saturate(center.a);
    float adaptive = saturate(filterAmount * lerp(0.58f, 0.96f, holeAmount));
    float3 result = lerp(max(center.rgb, 0.0f), filtered, adaptive);
    float confidence = max(center.a, filteredConfidence * adaptive);
    outReflection[dtid] = float4(result, saturate(confidence));
}


