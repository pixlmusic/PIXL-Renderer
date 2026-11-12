#include "Common/FrameBuffer.hlsli"
#include "HybridGI/common.hlsli"

Texture2D<float> srcSceneVoxelDepth : register(t0);
RWTexture3D<uint> outSceneVoxelWinner : register(u0);

cbuffer SceneVoxelCB : register(b3)
{
	float3 SceneVoxelWorldMin;
	float SceneVoxelSize;
	uint SceneVoxelDimension;
	uint SceneVoxelValid;
	uint SceneVoxelRadianceStride;
	uint SceneVoxelPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint stride = clamp(SceneVoxelRadianceStride, 1u, 8u);
	uint2 pixel = dispatchThreadID.xy * stride;
	if (any(pixel >= uint2(OUT_FRAME_DIM)) || SceneVoxelValid == 0u ||
		SceneVoxelDimension == 0u || !(SceneVoxelSize > 0.0f))
		return;

	uint pixelIndex = pixel.y * (uint)OUT_FRAME_DIM.x + pixel.x;
	if (pixelIndex >= 0x03ffffffu)
		return;
	float depth = READ_DEPTH(srcSceneVoxelDepth, pixel);
	if (!isfinite(depth) || depth <= FP_Z || depth >= DepthFadeRange.y)
		return;

	float2 screenPos = (pixel + 0.5f) * RCP_OUT_FRAME_DIM;
	float3 viewPosition = ScreenToViewPosition(screenPos, depth);
	float3 renderPosition = TraceViewToWorldPosition(viewPosition, FrameBuffer::CameraViewInverse);
	float3 worldPosition = renderPosition + FrameBuffer::CameraPosAdjust.xyz;
	if (!all(isfinite(worldPosition)))
		return;

	int3 cell = int3(floor((worldPosition - SceneVoxelWorldMin) / SceneVoxelSize));
	if (any(cell < 0) || any(cell >= int(SceneVoxelDimension)))
		return;

	float3 voxelOffset = frac((worldPosition - SceneVoxelWorldMin) / SceneVoxelSize) - 0.5f;
	uint priority = (uint)round(saturate(dot(voxelOffset, voxelOffset) / 0.75f) * 63.0f);
	uint candidate = (priority << 26u) | pixelIndex;
	InterlockedMin(outSceneVoxelWinner[uint3(cell)], candidate);
}
