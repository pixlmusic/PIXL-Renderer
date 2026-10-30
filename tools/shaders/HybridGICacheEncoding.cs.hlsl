// Offline candidate encoders; production projection/evaluation helpers are
// prepended by the harness. No runtime payload or shader binding changes.
RWStructuredBuffer<float4> results : register(u0);
float4 HalfRound(float4 value) { return f16tof32(f32tof16(value)); }
float4 QuantizedCoefficients(float4 value, float scale)
{
    return round(clamp(value / scale, -2.0f, 2.0f) * 255.5f) * (scale / 255.5f);
}
[numthreads(64,1,1)] void main(uint index : SV_DispatchThreadID)
{
    uint scenario=index/256u, sample=index&255u;
    float z=1.0f-2.0f*(float(sample)+0.5f)/256.0f;
    float angle=float(sample)*2.39996322973f;
    float3 direction=float3(sqrt(saturate(1.0f-z*z))*float2(cos(angle),sin(angle)),z);
    float3 normals[4]={float3(0,0,1),float3(1,0,0),float3(-1,0,0),float3(0,0,-1)};
    float3 colors[4]={float3(1,0.08f,0.01f),float3(0.01f,0.1f,1),float3(0.1f,1,0.08f),float3(1,1,1)};
    float energy=scenario==0u?0.0f:scenario==1u?0.0001f:exp2(float(scenario)-7.0f);
    uint lobes=scenario%4u+1u;
    float4 sh0=0,sh1=0,sh2=0;
    float3 lobeResult=0;
    [loop] for(uint lobe=0u;lobe<lobes;++lobe) {
        float3 color=colors[(lobe+scenario)%4u]*energy;
        float4 a,b,c;
        WorldCacheProjectRadiance(color,normals[lobe],a,b,c);
        sh0+=a/float(lobes); sh1+=b/float(lobes); sh2+=c/float(lobes);
        if(lobe<2u) {
            // Two full-color directional lobes, with compact oct8 normals.
            float2 packed=round(EncodeWorldOctahedron(normals[lobe])*255.0f)/255.0f;
            WorldCacheProjectRadiance(HalfRound(float4(color,0)).xyz,DecodeWorldOctahedron(packed),a,b,c);
            lobeResult+=WorldCacheEvaluateRadiance(a,b,c,direction)/float(lobes);
        } else {
            // Remaining lobes become a nondirectional residual, not discarded.
            lobeResult+=HalfRound(float4(color,0)).xyz*(0.25f/float(lobes));
        }
    }
    float3 reference=WorldCacheEvaluateRadiance(sh0,sh1,sh2,direction);
    float4 a=HalfRound(sh0),b=HalfRound(sh1),c=HalfRound(sh2);
    float3 legacy=WorldCacheEvaluateRadiance(a,b,c,direction);
    float3 l1=WorldCacheEvaluateRadiance(a,0.0f.xxxx,float4(0,c.yzw),direction);
    float scale=max(HalfRound(float4(sh2.w,0,0,0)).x,0.0001f);
    float4 qa=QuantizedCoefficients(sh0,scale),qb=QuantizedCoefficients(sh1,scale);
    float4 qc=float4(QuantizedCoefficients(sh2.xxxx,scale).x,
        round(clamp(sh2.yz,-4.0f,4.0f)*31.75f)/31.75f,scale);
    float3 quantized=WorldCacheEvaluateRadiance(qa,qb,qc,direction);
    float divisor=max(length(reference),0.05f);
    results[index]=float4(legacy,length(legacy-reference)/divisor);
    results[index+4096]=float4(l1,length(l1-reference)/divisor);
    results[index+8192]=float4(lobeResult,length(lobeResult-reference)/divisor);
    results[index+12288]=float4(quantized,length(quantized-reference)/divisor);
}
