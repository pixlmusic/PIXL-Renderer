#include "Common/SharedData.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"

cbuffer TemporalValidityData : register(b0)
{
	uint2 RenderDim;
	uint HistoryValid;
	uint AnnotationsEnabled;
	uint2 ActiveOffset;
	uint2 Padding2;
};

Texture2D<float> CurrentDepth : register(t0);
Texture2D<float4> CurrentNormalRoughness : register(t1);
Texture2D<float2> CurrentMotion : register(t2);
Texture2D<uint> PreviousPackedSurface : register(t3);
Texture2D<float3> DeferredMaterialMask : register(t4);
Texture2D<float4> NormalWaterMask : register(t5);
Texture2D<float2> TAAMask : register(t6);

RWTexture2D<uint> CurrentPackedSurface : register(u0);
// R: geometric disocclusion; G: reusable history confidence.
RWTexture2D<float2> ConfidenceDisocclusion : register(u1);
// Five material bits followed by the six CPU AnnotationFlag bits.
RWTexture2D<uint> CompactAnnotations : register(u2);

static const float kLogDepthScale = 65535.0 / 20.0;

float3 SafeWorldNormal(float2 encoded)
{
	float3 viewNormal = GBuffer::DecodeNormal(saturate(encoded));
	float3 worldNormal = mul((float3x3)FrameBuffer::CameraViewInverse, viewNormal);
	return normalize(worldNormal);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= RenderDim))
		return;

	uint2 sourcePixel = id.xy + ActiveOffset;
	float deviceDepth = CurrentDepth.Load(int3(sourcePixel, 0));
	float2 encodedNormal = CurrentNormalRoughness.Load(int3(sourcePixel, 0)).xy;
	bool hasGeometry = isfinite(deviceDepth) && deviceDepth > 1e-6 && deviceDepth < 0.999999;
	float linearDepth = hasGeometry ? SharedData::GetScreenDepth(deviceDepth) : 0.0;
	linearDepth = isfinite(linearDepth) ? max(linearDepth, 0.0) : 0.0;
	uint packedDepth = hasGeometry ? (uint)round(saturate(log2(1.0 + linearDepth) / 20.0) * 65535.0) : 0u;
	float3 worldNormal = SafeWorldNormal(encodedNormal);
	float2 worldEncoded = GBuffer::EncodeNormal(worldNormal);
	uint2 packedNormal = (uint2)round(saturate(worldEncoded) * 255.0);
	CurrentPackedSurface[id.xy] = packedDepth | (packedNormal.x << 16u) | (packedNormal.y << 24u);

	float confidence = 0.0;
	if (HistoryValid != 0u && hasGeometry && packedDepth != 0u) {
		float2 motion = CurrentMotion.Load(int3(sourcePixel, 0));
		if (all(isfinite(motion))) {
			float2 previousPixel = float2(id.xy) + motion * float2(RenderDim);
			int2 previousCoord = int2(round(previousPixel));
			if (all(previousCoord >= 0) && all(previousCoord < int2(RenderDim))) {
				uint packedPrevious = PreviousPackedSurface.Load(int3(previousCoord, 0));
				uint previousDepthCode = packedPrevious & 65535u;
				if (previousDepthCode != 0u) {
					float previousDepth = exp2(float(previousDepthCode) / kLogDepthScale) - 1.0;
					float depthTolerance = max(12.0, linearDepth * 0.02);
					float depthWeight = 1.0 - smoothstep(depthTolerance, depthTolerance * 3.0,
						abs(previousDepth - linearDepth));
					float2 previousEncoded = float2((packedPrevious >> 16u) & 255u,
						(packedPrevious >> 24u) & 255u) / 255.0;
					float3 previousNormal = GBuffer::DecodeNormal(previousEncoded);
					float normalWeight = smoothstep(0.55, 0.90, dot(worldNormal, previousNormal));
					confidence = saturate(depthWeight * normalWeight);
				}
			}
		}
	}
	ConfidenceDisocclusion[id.xy] = float2(hasGeometry ? 1.0 - confidence : 0.0, confidence);
	if (AnnotationsEnabled != 0u) {
		float3 material = DeferredMaterialMask.Load(int3(sourcePixel, 0));
		float waterCoverage = saturate(NormalWaterMask.Load(int3(sourcePixel, 0)).z);
		float2 taa = TAAMask.Load(int3(sourcePixel, 0));
		float2 motion = CurrentMotion.Load(int3(sourcePixel, 0));
		uint materialClass = 0u; // Unknown is safer than inferring an authored material from colour.
		uint flags = 0u;
		if (!hasGeometry) {
			materialClass = 17u; // Sky
		} else if (waterCoverage > 0.05) {
			materialClass = 12u; // Water
			flags |= 1u | 2u | 4u;
		} else if (material.x <= 1e-5 && material.y > 0.01) {
			materialClass = 1u; // Ground Response's landscape-only activity tag.
			flags |= 2u | 16u;
		} else if (material.x > 0.01 && (material.y < 0.01 || material.y > 0.99) &&
			abs(material.z - material.x) > 0.05) {
			materialClass = 7u; // Tissue amount; reject effect masks with equal RGB luminance.
		}
		if (isfinite(taa.y) && taa.y > 0.05)
			flags |= 2u;
		if (all(isfinite(motion)) && length(motion * float2(RenderDim)) > 2.0)
			flags |= 4u;
		CompactAnnotations[id.xy] = materialClass | (flags << 5u);
	}
}
