
#include "Common/SharedData.hlsli"

#include "ContactShadows/bend_sss_gpu.hlsli"

// Match bend_sss_gpu.hlsli's struct field types for assignment compatibility.
// TERRAIN_SEAM ON  -> R32_FLOAT (no unorm). OFF -> R24_UNORM_X8_TYPELESS (unorm).
#if defined(TERRAIN_SEAM)
Texture2D<float> DepthTexture : register(t0);  // Depth Buffer Texture (R32_FLOAT)
#else
Texture2D<unorm float> DepthTexture : register(t0);  // Depth Buffer Texture (R24_UNORM_X8_TYPELESS)
#endif
RWTexture2D<unorm float> OutputTexture : register(u0);  // Output screen-space shadow buffer (typically single-channel, 8bit)
SamplerState PointBorderSampler : register(s0);         // A point sampler, with Wrap Mode set to Clamp-To-Border-Color (D3D12_TEXTURE_ADDRESS_MODE_BORDER), and Border Color set to "FarDepthValue" (typically zero), or some other far-depth value out of DepthBounds.
														// If you have issues where invalid shadows are appearing from off-screen, it is likely that this sampler is not correctly setup
cbuffer PerFrame : register(b1)
{
	// Runtime data returned from BuildDispatchList():
	float4 LightCoordinate;  // Values stored in DispatchList::LightCoordinate_Shader by BuildDispatchList()
	int2 WaveOffset;         // Values stored in DispatchData::WaveOffset_Shader by BuildDispatchList()

	// Renderer Specific Values:
	float FarDepthValue;   // Set to the Depth Buffer Value for the far clip plane, as determined by renderer projection matrix setup (typically 0).
	float NearDepthValue;  // Set to the Depth Buffer Value for the near clip plane, as determined by renderer projection matrix setup (typically 1).

	// Sampling data:
	float2 InvDepthTextureSize;  // Inverse of the texture dimensions for 'DepthTexture' (used to convert from pixel coordinates to UVs)
								 // If 'PointBorderSampler' is an Unnormalized sampler, then this value can be hard-coded to 1.
								 // The 'USE_HALF_PIXEL_OFFSET' macro might need to be defined if sampling at exact pixel coordinates isn't precise (e.g., if odd patterns appear in the shadow).

	float2 DynamicRes;

	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(
	int3 groupID : SV_GroupID,
	int groupThreadID : SV_GroupThreadID) {
	DispatchParameters parameters;
	parameters.SetDefaults();

	parameters.LightCoordinate = LightCoordinate;
	parameters.WaveOffset = WaveOffset;
	// Respect the renderer's actual depth convention. Hard-coding near=0/far=1
	// inverted Bend's z_sign/thickness math on reversed-Z configurations and is a
	// major source of detached silhouettes and broken ray continuity.
	parameters.FarDepthValue = FarDepthValue;
	parameters.NearDepthValue = NearDepthValue;

	// Defensive fallback only when the CPU supplied an invalid/equal pair.
	if (abs(parameters.NearDepthValue - parameters.FarDepthValue) < 0.5h) {
		parameters.FarDepthValue = 1.0h;
		parameters.NearDepthValue = 0.0h;
	}
	parameters.InvDepthTextureSize = InvDepthTextureSize;
	parameters.DepthTexture = DepthTexture;
	parameters.OutputTexture = OutputTexture;
	parameters.PointBorderSampler = PointBorderSampler;

	// Preserve runtime tuning while preventing zero/uninitialized values from
	// collapsing the shadow ray. These defaults match Bend's recommended range.
	parameters.SurfaceThickness = SurfaceThickness > 1e-5f ? (half)SurfaceThickness : 0.005h;
	parameters.BilinearThreshold = BilinearThreshold > 1e-5f ? (half)BilinearThreshold : 0.02h;
	// The user-facing control is defined on [1, 4].  Values below one are
	// clamped instead of jumping to maximum contrast.
	parameters.ShadowContrast = clamp((half)ShadowContrast, 1.0h, 4.0h);

	// Broad, flat interior floors still produced dark stepped blotches whenever
	// repeated depth discontinuities were classified as edge casters. Prefer
	// stability in PIXL's default screen-space shadow build.
	parameters.IgnoreEdgePixels = true;

	parameters.DynamicRes = DynamicRes;

	parameters.UsePrecisionOffset = true;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}
