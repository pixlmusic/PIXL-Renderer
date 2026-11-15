#include "ImageReconstruction/UpscaleVS.hlsl"

#ifdef PSHADER

Texture2D<float> InputTexture : register(t0);

float main(VS_OUTPUT input) :
	SV_Target
{
	uint width, height;
	InputTexture.GetDimensions(width, height);
	const uint2 pixel = uint2(input.Position.xy);
	if (pixel.x >= width || pixel.y >= height)
		return 1.0f; // Invalid source pixels are clear depth, not false foreground.
	const float depth = InputTexture.Load(int3(pixel, 0));
	return isfinite(depth) ? saturate(depth) : 1.0f;
}

#endif