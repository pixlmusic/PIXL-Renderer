// PIXL PixDiT Photo Mode Enhance compositor (standalone bridge sample).
//
// The model output is never trusted as a direct replacement. This pass applies
// a second bounded residual, highlight/shadow protection, and the user blend to
// an immutable copy of the original Photo Finish image.

Texture2D<float4> OriginalCapture : register(t0);
Texture2D<float4> NeuralCapture : register(t1);
SamplerState LinearClampSampler : register(s0);

cbuffer PixDiTCompositeSettings : register(b0)
{
    float BlendFactor;
    float MaximumResidual;
    float HighlightProtection;
    float ShadowProtection;
    float ExposureSafety;
    float3 Padding;
};

struct PixelInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

float Luminance(float3 color)
{
    return dot(max(color, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
}

float4 main(PixelInput input) : SV_TARGET
{
    float4 original = OriginalCapture.SampleLevel(LinearClampSampler, input.UV, 0.0f);
    float3 enhanced = NeuralCapture.SampleLevel(LinearClampSampler, input.UV, 0.0f).rgb;

    float maximumDelta = clamp(MaximumResidual, 0.0f, 0.25f);
    float3 residual = clamp(
        enhanced - original.rgb,
        -maximumDelta.xxx,
        maximumDelta.xxx);

    float originalLuma = Luminance(original.rgb);
    float highlight = smoothstep(0.72f, 1.02f, originalLuma);
    float deepShadow = 1.0f - smoothstep(0.0f, 0.045f, originalLuma);
    float protection =
        (1.0f - highlight * saturate(HighlightProtection)) *
        (1.0f - deepShadow * saturate(ShadowProtection));

    // ExposureSafety allows future HDR-aware models to add an independent guard;
    // the current SDR bridge uses 1.0. NaN/Inf rejection occurs on the CPU/GPU
    // inference boundary before this compositor is dispatched.
    float safeBlend = saturate(BlendFactor) * protection * saturate(ExposureSafety);
    float3 result = saturate(original.rgb + residual * safeBlend);
    return float4(result, original.a);
}

