// depth-aware upsampling: https://gist.github.com/pixelmager/a4364ea18305ed5ca707d89ddc5f8743

#include "Common/FastMath.hlsli"
#include "Common/GBuffer.hlsli"
#include "HybridGI/common.hlsli"

Texture2D<half> srcDepth : register(t0);
Texture2D<half> srcAo : register(t1);           // low-res
Texture2D<half4> srcIlY : register(t2);         // low-res
Texture2D<half2> srcIlCoCg : register(t3);      // low-res
Texture2D<half4> srcGiSpecular : register(t4);  // low-res
Texture2D<half4> srcBentVisibility : register(t5); // low-res: encoded bent normal.xy, visibility, confidence
Texture2D<float2> srcNormal : register(t6);      // full pyramid: octahedral view-space normal

RWTexture2D<half> outAo : register(u0);
RWTexture2D<half4> outIlY : register(u1);
RWTexture2D<half2> outIlCoCg : register(u2);
RWTexture2D<half4> outGiSpecular : register(u3);
RWTexture2D<half4> outBentVisibility : register(u4);

#define min4(v) min(min(v.x, v.y), min(v.z, v.w))
#define max4(v) max(max(v.x, v.y), max(v.z, v.w))
#define BLEND_WEIGHT(a, b, c, d, w, sumw) ((a * w.x + b * w.y + c * w.z + d * w.w) / max(sumw, 1e-5))

float4 BlendBentVisibility(float4 a, float4 b, float4 c, float4 d, float4 w, float sumw)
{
    float invWeight = rcp(max(sumw, 1e-5f));

    // Octahedral normal coordinates are not linear vectors. Decode before an
    // edge-aware blend, then re-encode the normalized result.
    float4 confidence = saturate(float4(a.w, b.w, c.w, d.w));
    float4 normalWeight = w * lerp(0.20f.xxxx, 1.0f.xxxx, confidence);
    float normalWeightSum = max(dot(normalWeight, 1.0f.xxxx), 1e-5f);

    // The weighted vector is normalized immediately, so dividing by the weight
    // sum first is mathematically redundant. Keep the original cancellation
    // threshold by scaling it by weightSum^2, saving one reciprocal + 3 multiplies.
    float3 bent =
        GBuffer::DecodeNormal(a.xy) * normalWeight.x +
        GBuffer::DecodeNormal(b.xy) * normalWeight.y +
        GBuffer::DecodeNormal(c.xy) * normalWeight.z +
        GBuffer::DecodeNormal(d.xy) * normalWeight.w;
    // Opposing edge samples can cancel to a zero vector. Avoid feeding a NaN
    // into the octahedral encoder; fall back to the strongest nearby sample.
    float bentLengthSq = dot(bent, bent);
    float cancellationThreshold = 1e-6f * normalWeightSum * normalWeightSum;
    if (bentLengthSq > cancellationThreshold) {
        bent *= rsqrt(bentLengthSq);
    } else {
        float2 fallbackEncoded = normalWeight.x >= max(max(normalWeight.y, normalWeight.z), normalWeight.w) ? a.xy :
            (normalWeight.y >= max(normalWeight.z, normalWeight.w) ? b.xy :
            (normalWeight.z >= normalWeight.w ? c.xy : d.xy));
        bent = GBuffer::DecodeNormal(fallbackEncoded);
    }

    float visibility = dot(float4(a.z, b.z, c.z, d.z), w) * invWeight;
    float outConfidence = dot(confidence, w) * invWeight;
    return float4(GBuffer::EncodeNormal(bent), saturate(visibility), saturate(outConfidence));
}

[numthreads(8, 8, 1)]
void main(const uint2 dtid : SV_DispatchThreadID)
{
    if (any(dtid >= uint2(FrameDim)))
        return;

#ifdef HALF_RES
    int2 px00 = (dtid >> 1) + (dtid & 1) - 1;
#else
    int2 px00 = (dtid >> 2) + (dtid & 2) / 2 - 1;
#endif
    int2 lowMax = int2(OUT_FRAME_DIM) - 1;
    px00 = clamp(px00, 0, lowMax);
    int2 px10 = clamp(px00 + int2(1, 0), 0, lowMax);
    int2 px01 = clamp(px00 + int2(0, 1), 0, lowMax);
    int2 px11 = clamp(px00 + int2(1, 1), 0, lowMax);

    float4 d = float4(
        srcDepth.Load(int3(px00, RES_MIP)),
        srcDepth.Load(int3(px01, RES_MIP)),
        srcDepth.Load(int3(px10, RES_MIP)),
        srcDepth.Load(int3(px11, RES_MIP)));

    float mind = min4(d);
    float maxd = max4(d);
    float avg = max(dot(d, 0.25.xxxx), 1e-4);
    float edgeThreshold = UpsampleEdgeThreshold > 0.0 ? UpsampleEdgeThreshold : 0.10;

    // Depth alone cannot distinguish two surfaces that meet at a crease (or
    // thin alpha-tested geometry at almost the same depth).  Use the normal
    // pyramid that HybridGI already generated to prevent AO, radiance and the
    // bent direction from crossing those boundaries during reconstruction.
    float3 receiverNormal = GBuffer::DecodeNormal(srcNormal.Load(int3(dtid, 0)));
    float3 n00 = GBuffer::DecodeNormal(srcNormal.Load(int3(px00, RES_MIP)));
    float3 n01 = GBuffer::DecodeNormal(srcNormal.Load(int3(px01, RES_MIP)));
    float3 n10 = GBuffer::DecodeNormal(srcNormal.Load(int3(px10, RES_MIP)));
    float3 n11 = GBuffer::DecodeNormal(srcNormal.Load(int3(px11, RES_MIP)));
    float4 normalSimilarity = saturate(float4(
        dot(receiverNormal, n00), dot(receiverNormal, n01),
        dot(receiverNormal, n10), dot(receiverNormal, n11)));
    bool smoothNeighborhood = ((maxd - mind) / avg) < edgeThreshold && min4(normalSimilarity) > 0.85f;

    float ao;
    float4 y;
    float2 coCg;
    float4 giSpecular;
    float4 bentVisibility;

    if (!smoothNeighborhood)
    {
        // At a depth discontinuity choose the low-res samples whose depth most
        // closely matches the full-resolution receiver.  This avoids bleeding
        // reflected radiance or a bent normal from the opposite side of an edge.
        float bgDepth = srcDepth.Load(int3(dtid, 0));
        float4 dd = abs(d - bgDepth);
        // Retain a small floor so a receiver near a heavily downsampled normal
        // still has a deterministic fallback instead of producing zero weight.
        float4 normalWeight = 0.01f.xxxx + pow(normalSimilarity, 8.0f);
        float4 w = rcp(dd + 1e-4) * normalWeight;
        float sumw = dot(w, 1.0.xxxx);

        ao = BLEND_WEIGHT(srcAo[px00], srcAo[px01], srcAo[px10], srcAo[px11], w, sumw);
        y = BLEND_WEIGHT(srcIlY[px00], srcIlY[px01], srcIlY[px10], srcIlY[px11], w, sumw);
        coCg = BLEND_WEIGHT(srcIlCoCg[px00], srcIlCoCg[px01], srcIlCoCg[px10], srcIlCoCg[px11], w, sumw);
        giSpecular = BLEND_WEIGHT(srcGiSpecular[px00], srcGiSpecular[px01], srcGiSpecular[px10], srcGiSpecular[px11], w, sumw);
        bentVisibility = BlendBentVisibility(
            srcBentVisibility[px00], srcBentVisibility[px01],
            srcBentVisibility[px10], srcBentVisibility[px11], w, sumw);
    }
    else
    {
        float2 lowUv = (dtid + 0.5) * RcpFrameDim * OUT_FRAME_DIM * RcpTexDim;
        ao = srcAo.SampleLevel(samplerLinearClamp, lowUv, 0);
        y = srcIlY.SampleLevel(samplerLinearClamp, lowUv, 0);
        coCg = srcIlCoCg.SampleLevel(samplerLinearClamp, lowUv, 0);
        giSpecular = srcGiSpecular.SampleLevel(samplerLinearClamp, lowUv, 0);
        bentVisibility = srcBentVisibility.SampleLevel(samplerLinearClamp, lowUv, 0);
        float3 smoothBent = GBuffer::DecodeNormal(bentVisibility.xy);
        if (dot(smoothBent, smoothBent) > 0.25f)
            bentVisibility.xy = GBuffer::EncodeNormal(normalize(smoothBent));
    }

    outAo[dtid] = ao;
    outIlY[dtid] = y;
    outIlCoCg[dtid] = coCg;
    outGiSpecular[dtid] = giSpecular;
    outBentVisibility[dtid] = bentVisibility;
}
