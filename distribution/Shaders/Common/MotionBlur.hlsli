#ifndef __MOTION_BLUR_DEPENDENCY_HLSL__
#define __MOTION_BLUR_DEPENDENCY_HLSL__

#include "Common/FrameBuffer.hlsli"

namespace MotionBlur
{
	float2 GetSSMotionVector(float4 a_wsPosition, float4 a_previousWSPosition)
	{
		float4 screenPosition = mul(FrameBuffer::CameraViewProjUnjittered, a_wsPosition);
		float4 previousScreenPosition = mul(FrameBuffer::CameraPreviousViewProjUnjittered, a_previousWSPosition);
		// A point on the camera plane has no stable projected position. Avoid
		// propagating infinities/NaNs into temporal motion vectors in that case.
		if (abs(screenPosition.w) <= 1e-6f || abs(previousScreenPosition.w) <= 1e-6f)
			return float2(0.0f, 0.0f);
		screenPosition.xy = screenPosition.xy / screenPosition.ww;
		previousScreenPosition.xy = previousScreenPosition.xy / previousScreenPosition.ww;
		return float2(-0.5, 0.5) * (screenPosition.xy - previousScreenPosition.xy);
	}
}

#endif  // __MOTION_BLUR_DEPENDENCY_HLSL__
