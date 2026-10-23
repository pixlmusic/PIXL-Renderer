namespace WaterOptics
{
	#ifndef USE_PIXL_STABLE_WATER_PARALLAX
	#define USE_PIXL_STABLE_WATER_PARALLAX 1
	#endif

	float2 SafeParallaxDirection(float3 viewDirection)
	{
		float lengthSq = dot(viewDirection, viewDirection);
		viewDirection = lengthSq > 1.0e-8f ? viewDirection * rsqrt(lengthSq) : float3(0, 0, -1);
		float safeZ = -max(abs(viewDirection.z), 0.075);
		return clamp(viewDirection.xy / safeZ, -12.0f.xx, 12.0f.xx);
	}

	float ResolveParallaxIntersection(float prevBound, float prevHeight, float currBound, float currHeight)
	{
		if (!all(isfinite(float4(prevBound, prevHeight, currBound, currHeight))))
			return saturate(currBound);
		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		float denominator = delta2 - delta1;
		return (abs(denominator) > 1e-5) ? saturate((currBound * delta2 - prevBound * delta1) / denominator) : currBound;
	}
	// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
	// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

	// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
	// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
	// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

	float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
	{
		// Compute the current gradients:
		float2 actualTextureDims;
		tex.GetDimensions(actualTextureDims.x, actualTextureDims.y);

		// Use each axis of the bound normal map so rectangular replacements
		// receive the same footprint and avoid oversharpened distant waves.
		float2 textureDims = max(actualTextureDims, 1.0f.xx);

		float2 texCoordsPerSize = coords * textureDims;

		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Find min of change in u and v across quad: compute du and dv magnitude across quad
		float2 dTexCoords = dxSize * dxSize + dySize * dySize;

		// Standard mipmapping uses max here
		float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);

		// Compute the current mip level  (* 0.5 is effectively computing a square root before )
		float mipLevel = max(0.5 * log2(max(minTexCoordDelta, 1e-8f)), 0);

		// Stochastic mip selection: use screen noise to select between adjacent mip levels
		screenNoise = frac(abs(screenNoise));
		mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

		return mipLevel;
	}

	float GetHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float3 mipLevels)
	{
		float3 heights;
		heights.x = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevels.x).w;
		heights.y = Normals02Tex.SampleLevel(Normals02Sampler, input.TexCoord1.zw + currentOffset * normalScalesRcp.y, mipLevels.y).w;
		heights.z = Normals03Tex.SampleLevel(Normals03Sampler, input.TexCoord2.xy + currentOffset * normalScalesRcp.z, mipLevels.z).w;
		heights = saturate(1.0 - heights);
		heights *= max(NormalsAmplitude.xyz, 0.0f);
		return saturate(heights.x + heights.y + heights.z);
	}

	float2 GetParallaxOffset(PS_INPUT input, float3 normalScalesRcp)
	{
		float viewLengthSq = dot(input.WPosition.xyz, input.WPosition.xyz);
		float3 viewDirection = viewLengthSq > 1.0e-8f ? input.WPosition.xyz * rsqrt(viewLengthSq) : float3(0, 0, -1);
	#if USE_PIXL_STABLE_WATER_PARALLAX
		if (-viewDirection.z < 0.075)
			return 0.0.xx;
		float2 parallaxOffsetTS = SafeParallaxDirection(viewDirection);
	#else
		float2 parallaxOffsetTS = viewDirection.xy / -viewDirection.z;
	#endif

		// Parallax scale is also multiplied by normalScalesRcp
		parallaxOffsetTS *= 20.0;

	#if USE_PIXL_STABLE_WATER_PARALLAX
		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, 0);
	#else
		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);
	#endif

		float3 mipLevels;
		mipLevels.x = GetMipLevel(input.TexCoord1.xy, Normals01Tex, screenNoise);
		mipLevels.y = GetMipLevel(input.TexCoord1.zw, Normals02Tex, screenNoise);
		mipLevels.z = GetMipLevel(input.TexCoord2.xy, Normals03Tex, screenNoise);

		float stepSize = rcp(16.0);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] for (int step = 0; step < 16 && currHeight > currBound; ++step)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevels);
		}

		float prevBound = currBound - stepSize;

		float parallaxAmount = ResolveParallaxIntersection(prevBound, prevHeight, currBound, currHeight);

		return parallaxOffsetTS.xy * parallaxAmount;
	}

#if defined(FLOWMAP)
	float GetFlowmapHeight(PS_INPUT input, float2 uvShift, float multiplier, float offset, float mipLevel)
	{
		FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
		float2 baseUV = offset + (flowData.flowVector - float2(multiplier * ((0.001 * ReflectionColor.w) * flowData.color.w), 0));
		return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler, baseUV, mipLevel).w;
	}

	float GetFlowmapBlendedHeight(PS_INPUT input, float2 normalMul, float2 uvShift, float mipLevel)
	{
		float height0 = GetFlowmapHeight(input, uvShift, 9.92, 0, mipLevel);
		float height1 = GetFlowmapHeight(input, float2(0, uvShift.y), 10.64, 0.27, mipLevel);
		float height2 = GetFlowmapHeight(input, 0.0.xx, 8, 0, mipLevel);
		float height3 = GetFlowmapHeight(input, float2(uvShift.x, 0), 8.48, 0.62, mipLevel);

		float blendedHeight =
			normalMul.y * (normalMul.x * height2 + (1 - normalMul.x) * height3) +
			(1 - normalMul.y) * (normalMul.x * height1 + (1 - normalMul.x) * height0);

		return blendedHeight;
	}

	float GetFlowmapParallaxAmount(PS_INPUT input, float2 flowmapDims, float3 viewDirection)
	{
		float viewDotUp = -viewDirection.z;

		if (viewDotUp < 0.05)
			return 0.0;

	#if USE_PIXL_STABLE_WATER_PARALLAX
		float2 parallaxDir = SafeParallaxDirection(viewDirection);
	#else
		float2 parallaxDir = viewDirection.xy / -viewDirection.z;
	#endif
		parallaxDir.y = -parallaxDir.y;

		float parallaxScale = 0.008 * saturate(viewDotUp * 2.0);
		parallaxDir *= parallaxScale;

		flowmapDims = max(abs(flowmapDims), 1.0f.xx);
		float2 uvShiftPx = 1 / (128 * flowmapDims);

		int numSteps = (int)lerp(32.0, 8.0, viewDotUp);
		float stepSize = rcp((float)numSteps);

		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] for (int i = 0; i < numSteps && currHeight > currBound; i++)
		{
			prevHeight = currHeight;
			currBound += stepSize;

			PS_INPUT offsetInput = input;
			offsetInput.TexCoord3.xy = input.TexCoord3.xy + currBound * parallaxDir;

			float2 cellBlend = 1.0f - abs(frac(offsetInput.TexCoord2.zw * (64 * flowmapDims)) * 2.0f - 1.0f);
			cellBlend = cellBlend * cellBlend * (3.0f.xx - 2.0f * cellBlend);
			currHeight = 1.0 - GetFlowmapBlendedHeight(offsetInput, cellBlend, uvShiftPx, 0);
		}

		float prevBound = currBound - stepSize;
		float delta2 = prevBound - prevHeight;
		float delta1 = currBound - currHeight;
		return ResolveParallaxIntersection(prevBound, prevHeight, currBound, currHeight);
	}

	float GetFlowmapParallaxHeight(PS_INPUT input, float2 currentOffset, float3 normalScalesRcp, float mipLevel)
	{
		float height = Normals01Tex.SampleLevel(Normals01Sampler, input.TexCoord1.xy + currentOffset * normalScalesRcp.x, mipLevel).w;
		height = 1.0 - height;
		height *= NormalsAmplitude.x;
		return height;
	}

	float2 GetFlowmapParallaxUVOffset(PS_INPUT input, float3 viewDirection, float3 normalScalesRcp)
	{
	#if USE_PIXL_STABLE_WATER_PARALLAX
		if (-viewDirection.z < 0.075)
			return 0.0.xx;
		float2 parallaxOffsetTS = SafeParallaxDirection(viewDirection);
	#else
		float2 parallaxOffsetTS = viewDirection.xy / -viewDirection.z;
	#endif
		parallaxOffsetTS *= 80.0;

	#if USE_PIXL_STABLE_WATER_PARALLAX
		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, 0);
	#else
		float screenNoise = Random::InterleavedGradientNoise(input.HPosition.xy, SharedData::FrameCount);
	#endif
		float mipLevel = GetMipLevel(input.TexCoord1.xy, Normals01Tex, screenNoise);

		float stepSize = rcp(16.0);
		float currBound = 0.0;
		float currHeight = 1.0;
		float prevHeight = 1.0;

		[loop] for (int step = 0; step < 16 && currHeight > currBound; ++step)
		{
			prevHeight = currHeight;
			currBound += stepSize;
			currHeight = GetFlowmapParallaxHeight(input, currBound * parallaxOffsetTS.xy, normalScalesRcp, mipLevel);
		}

		float prevBound = currBound - stepSize;
		float parallaxAmount = ResolveParallaxIntersection(prevBound, prevHeight, currBound, currHeight);

		return parallaxOffsetTS.xy * parallaxAmount;
	}

	float2 GetFlowmapParallaxOffset(PS_INPUT input, float2 flowmapDimensions, float3 viewDirection, float3 normalScalesRcp)
	{
		return GetFlowmapParallaxUVOffset(input, viewDirection, normalScalesRcp);
	}
#endif
}
