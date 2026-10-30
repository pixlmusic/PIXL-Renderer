// PIXL Water Optics v2.0 - stable, footprint-aware adaptive water POM.
// Existing WaterOptics hooks and texture/sampler/constant-buffer contracts remain intact.
namespace WaterOptics
{
#ifndef USE_PIXL_STABLE_WATER_PARALLAX
#define USE_PIXL_STABLE_WATER_PARALLAX 1
#endif
// 0 = low cost, 1 = balanced release default, 2 = cinematic.
#ifndef PIXL_WATER_PARALLAX_QUALITY
#define PIXL_WATER_PARALLAX_QUALITY 1
#endif

    float2 SafeParallaxDirection(float3 viewDirection)
    {
        float lenSq = dot(viewDirection, viewDirection);
        viewDirection = lenSq > 1.0e-8f && all(isfinite(viewDirection))
            ? viewDirection * rsqrt(lenSq) : float3(0.0f, 0.0f, -1.0f);
        // Never allow under-water / back-facing rays to invert parallax.
        float safeZ = max(-viewDirection.z, 0.075f);
        return clamp(-viewDirection.xy / safeZ, -12.0f.xx, 12.0f.xx);
    }

    float ResolveParallaxIntersection(float prevBound, float prevHeight,
        float currBound, float currHeight)
    {
        if (!all(isfinite(float4(prevBound, prevHeight, currBound, currHeight))))
            return saturate(currBound);
        float previous = prevBound - prevHeight;
        float current = currBound - currHeight;
        float denominator = previous - current;
        if (abs(denominator) <= 1.0e-5f)
            return saturate(currBound);
        float intersection = (currBound * previous - prevBound * current) / denominator;
        return clamp(intersection, saturate(prevBound), saturate(currBound));
    }

    float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
    {
        float2 dimensions;
        tex.GetDimensions(dimensions.x, dimensions.y);
        dimensions = max(dimensions, 1.0f.xx);
        float2 dx = ddx(coords) * dimensions;
        float2 dy = ddy(coords) * dimensions;
        // A continuous LOD avoids per-pixel mip popping from stochastic selection.
        // The previous function used screen noise (stable only in screen space).
        float rho2 = max(dot(dx, dx), dot(dy, dy));
        return isfinite(rho2) ? max(0.0f, 0.5f * log2(max(rho2, 1.0e-8f))) : 0.0f;
    }

    float GetHeight(PS_INPUT input, float2 currentOffset,
        float3 normalScalesRcp, float3 mipLevels)
    {
        float3 reciprocalScales = clamp(abs(normalScalesRcp), 0.0f.xxx, 4096.0f.xxx);
        float3 heights;
        heights.x = Normals01Tex.SampleLevel(Normals01Sampler,
            input.TexCoord1.xy + currentOffset * reciprocalScales.x, mipLevels.x).w;
        heights.y = Normals02Tex.SampleLevel(Normals02Sampler,
            input.TexCoord1.zw + currentOffset * reciprocalScales.y, mipLevels.y).w;
        heights.z = Normals03Tex.SampleLevel(Normals03Sampler,
            input.TexCoord2.xy + currentOffset * reciprocalScales.z, mipLevels.z).w;
        heights = saturate(1.0f.xxx - heights) * max(NormalsAmplitude.xyz, 0.0f.xxx);
        return saturate(heights.x + heights.y + heights.z);
    }

    float WaterParallaxFacingFade(float3 viewDirection)
    {
        // Smooth horizon transition replaces the visible hard rejection at 0.075.
        return smoothstep(0.055f, 0.18f, -viewDirection.z);
    }

    float WaterParallaxDistanceFade(float cameraDistance)
    {
        // Fine virtual relief only matters near the receiver. It is not geometry.
        return 1.0f - smoothstep(2400.0f, 6000.0f, cameraDistance);
    }

    float WaterParallaxMipFade(float mip)
    {
        return 1.0f - smoothstep(2.0f, 5.5f, mip);
    }

    int WaterParallaxStepBudget(float facing, float maxMip)
    {
#if PIXL_WATER_PARALLAX_QUALITY >= 2
        float high = 22.0f;
        float low = 10.0f;
#elif PIXL_WATER_PARALLAX_QUALITY >= 1
        float high = 15.0f;
        float low = 7.0f;
#else
        float high = 9.0f;
        float low = 5.0f;
#endif
        float grazing = 1.0f - saturate(facing);
        float budget = lerp(low, high, grazing);
        budget *= 1.0f - 0.40f * smoothstep(1.5f, 5.0f, maxMip);
        return max(4, (int)ceil(budget));
    }

    // Bracket crossing against one consistent set of continuous-LOD height maps.
    // This replaces a fixed 16*3 texture fetches with a perceptually chosen budget.
    float TraceWaterParallax(PS_INPUT input, float2 offset,
        float3 normalScalesRcp, float3 mips, int steps)
    {
        float stepSize = rcp((float)steps);
        float prevBound = 0.0f;
        float prevHeight = 1.0f;
        float currBound = 0.0f;
        float currHeight = 1.0f;
        [loop] for (int i = 0; i < steps; ++i)
        {
            currBound = min(1.0f, prevBound + stepSize);
            currHeight = GetHeight(input, offset * currBound, normalScalesRcp, mips);
            if (currHeight <= currBound)
                break;
            prevBound = currBound;
            prevHeight = currHeight;
        }
        float result = ResolveParallaxIntersection(prevBound, prevHeight,
            currBound, currHeight);
#if PIXL_WATER_PARALLAX_QUALITY >= 2
        // Only refine the actually intersected segment; no second full ray march.
        if (currHeight <= currBound && currBound > prevBound)
        {
            [unroll] for (int refine = 0; refine < 2; ++refine)
            {
                float mid = (prevBound + currBound) * 0.5f;
                float midHeight = GetHeight(input, offset * mid, normalScalesRcp, mips);
                if (midHeight > mid)
                {
                    prevBound = mid;
                    prevHeight = midHeight;
                }
                else
                {
                    currBound = mid;
                    currHeight = midHeight;
                }
            }
            result = ResolveParallaxIntersection(prevBound, prevHeight,
                currBound, currHeight);
        }
#endif
        return saturate(result);
    }

    // Caller may pass scene visibility, but the derivative selection is
    // unconditional so no undefined ddx/ddy occurs in divergent water draws.
    float2 GetParallaxOffsetScene(PS_INPUT input, float3 normalScalesRcp,
        float sceneWeight)
    {
        float3 mips;
        mips.x = GetMipLevel(input.TexCoord1.xy, Normals01Tex, 0.0f);
        mips.y = GetMipLevel(input.TexCoord1.zw, Normals02Tex, 0.0f);
        mips.z = GetMipLevel(input.TexCoord2.xy, Normals03Tex, 0.0f);
        float maxMip = max(mips.x, max(mips.y, mips.z));
        if (max(max(NormalsAmplitude.x, NormalsAmplitude.y), NormalsAmplitude.z) <= 1.0e-5f ||
            sceneWeight <= 1.0e-4f)
            return 0.0f.xx;
        float cameraDistance2 = dot(input.WPosition.xyz, input.WPosition.xyz);
        if (!isfinite(cameraDistance2) || cameraDistance2 <= 1.0e-8f)
            return 0.0f.xx;
        float cameraDistance = sqrt(cameraDistance2);
        float3 viewDirection = input.WPosition.xyz / cameraDistance;
        float facing = saturate(-viewDirection.z);
#if USE_PIXL_STABLE_WATER_PARALLAX
        float visibility = WaterParallaxFacingFade(viewDirection) *
            WaterParallaxDistanceFade(cameraDistance);
        if (visibility <= 1.0e-4f)
            return 0.0f.xx;
        float2 rayDirection = SafeParallaxDirection(viewDirection);
#else
        float visibility = 1.0f;
        float2 rayDirection = viewDirection.xy / -viewDirection.z;
#endif
        visibility *= WaterParallaxMipFade(maxMip) * saturate(sceneWeight);
        if (visibility <= 1.0e-4f)
            return 0.0f.xx;
        float2 rayOffset = rayDirection * (20.0f * visibility);
        int steps = WaterParallaxStepBudget(facing, maxMip);
        float intersection = TraceWaterParallax(input, rayOffset,
            normalScalesRcp, mips, steps);
        return rayOffset * intersection;
    }

    // ABI-compatible original hook. v2 Water.hlsl calls the weighted overload.
    float2 GetParallaxOffset(PS_INPUT input, float3 normalScalesRcp)
    {
        return GetParallaxOffsetScene(input, normalScalesRcp, 1.0f);
    }

#if defined(FLOWMAP)
    float GetFlowmapHeight(PS_INPUT input, float2 uvShift,
        float multiplier, float offset, float mipLevel)
    {
        FlowmapData flowData = GetFlowmapDataUV(input, uvShift);
        float2 baseUV = offset + (flowData.flowVector -
            float2(multiplier * ((0.001f * ReflectionColor.w) * flowData.color.w), 0.0f));
        return FlowMapNormalsTex.SampleLevel(FlowMapNormalsSampler,
            baseUV, mipLevel).w;
    }

    float GetFlowmapBlendedHeight(PS_INPUT input, float2 normalMul,
        float2 uvShift, float mipLevel)
    {
        float height0 = GetFlowmapHeight(input, uvShift, 9.92f, 0.0f, mipLevel);
        float height1 = GetFlowmapHeight(input, float2(0.0f, uvShift.y), 10.64f, 0.27f, mipLevel);
        float height2 = GetFlowmapHeight(input, 0.0f.xx, 8.0f, 0.0f, mipLevel);
        float height3 = GetFlowmapHeight(input, float2(uvShift.x, 0.0f), 8.48f, 0.62f, mipLevel);
        return saturate(normalMul.y * (normalMul.x * height2 +
            (1.0f - normalMul.x) * height3) +
            (1.0f - normalMul.y) * (normalMul.x * height1 +
            (1.0f - normalMul.x) * height0));
    }

    float WaterFlowmapFootprint(PS_INPUT input, float2 flowmapDims)
    {
        // Use existing flowmap tile dimensions instead of ignoring the parameter.
        float2 dimensions = clamp(abs(flowmapDims), 1.0f.xx, 4096.0f.xx);
        float2 dx = ddx(input.TexCoord3.xy) * dimensions;
        float2 dy = ddy(input.TexCoord3.xy) * dimensions;
        return sqrt(max(dot(dx, dx), dot(dy, dy)));
    }

    float GetFlowmapParallaxAmountScene(PS_INPUT input,
        float2 flowmapDims, float3 viewDirection, float sceneWeight)
    {
        float facing = saturate(-viewDirection.z);
        float flowFootprint = WaterFlowmapFootprint(input, flowmapDims);
        float visibility = WaterParallaxFacingFade(viewDirection) * saturate(sceneWeight);
        if (visibility <= 1.0e-4f)
            return 0.0f;
#if USE_PIXL_STABLE_WATER_PARALLAX
        float2 parallaxDir = SafeParallaxDirection(viewDirection);
#else
        float2 parallaxDir = viewDirection.xy / -viewDirection.z;
#endif
        parallaxDir.y = -parallaxDir.y;
        float footprintFade = 1.0f - smoothstep(1.5f, 5.0f, flowFootprint);
        float distanceFade = WaterParallaxDistanceFade(length(input.WPosition.xyz));
        visibility *= footprintFade * distanceFade;
        if (visibility <= 1.0e-4f)
            return 0.0f;
        parallaxDir *= 0.008f * saturate(facing * 2.0f) * visibility;
        float2 dimensions = max(abs(flowmapDims), 1.0f.xx);
        float2 uvShiftPx = rcp(128.0f * dimensions);
        // LOD is selected once outside the divergent ray-march loop.
        float heightMip = max(0.0f,
            0.5f * log2(max(flowFootprint * flowFootprint, 1.0e-8f)));
#if PIXL_WATER_PARALLAX_QUALITY >= 2
        int steps = max(6, (int)ceil(lerp(8.0f, 16.0f, 1.0f - facing)));
#elif PIXL_WATER_PARALLAX_QUALITY >= 1
        int steps = max(5, (int)ceil(lerp(6.0f, 11.0f, 1.0f - facing)));
#else
        int steps = max(4, (int)ceil(lerp(4.0f, 7.0f, 1.0f - facing)));
#endif
        float stepSize = rcp((float)steps);
        float prevBound = 0.0f;
        float prevHeight = 1.0f;
        float currBound = 0.0f;
        float currHeight = 1.0f;
        [loop] for (int step = 0; step < steps; ++step)
        {
            currBound = min(1.0f, prevBound + stepSize);
            PS_INPUT offsetInput = input;
            offsetInput.TexCoord3.xy = input.TexCoord3.xy + currBound * parallaxDir;
            float2 cellBlend = 1.0f.xx -
                abs(frac(offsetInput.TexCoord2.zw * (64.0f * dimensions)) * 2.0f - 1.0f.xx);
            cellBlend = cellBlend * cellBlend * (3.0f.xx - 2.0f * cellBlend);
            currHeight = 1.0f - GetFlowmapBlendedHeight(offsetInput,
                cellBlend, uvShiftPx, heightMip);
            if (currHeight <= currBound)
                break;
            prevBound = currBound;
            prevHeight = currHeight;
        }
        return ResolveParallaxIntersection(prevBound, prevHeight,
            currBound, currHeight) * visibility;
    }

    float GetFlowmapParallaxAmount(PS_INPUT input,
        float2 flowmapDims, float3 viewDirection)
    {
        return GetFlowmapParallaxAmountScene(input, flowmapDims, viewDirection, 1.0f);
    }

    float GetFlowmapParallaxHeight(PS_INPUT input, float2 currentOffset,
        float3 normalScalesRcp, float mipLevel)
    {
        float2 offsetScale = clamp(abs(normalScalesRcp.x), 0.0f, 4096.0f).xx;
        float height = Normals01Tex.SampleLevel(Normals01Sampler,
            input.TexCoord1.xy + currentOffset * offsetScale, mipLevel).w;
        return saturate((1.0f - height) * max(NormalsAmplitude.x, 0.0f));
    }

    float2 GetFlowmapParallaxUVOffsetV2(PS_INPUT input,
        float3 viewDirection, float3 normalScalesRcp, float2 flowmapDims)
    {
        float mip = GetMipLevel(input.TexCoord1.xy, Normals01Tex, 0.0f);
        float flowFootprint = WaterFlowmapFootprint(input, flowmapDims);
        float visibility = WaterParallaxFacingFade(viewDirection) *
            WaterParallaxDistanceFade(length(input.WPosition.xyz));
        if (visibility <= 1.0e-4f)
            return 0.0f.xx;
#if USE_PIXL_STABLE_WATER_PARALLAX
        float2 direction = SafeParallaxDirection(viewDirection);
#else
        float2 direction = viewDirection.xy / -viewDirection.z;
#endif
        visibility *= WaterParallaxMipFade(mip) *
            (1.0f - smoothstep(1.5f, 5.0f, flowFootprint));
        if (visibility <= 1.0e-4f)
            return 0.0f.xx;
        float2 rayOffset = direction * (80.0f * visibility);
        int steps = WaterParallaxStepBudget(saturate(-viewDirection.z), mip);
        float stepSize = rcp((float)steps);
        float prevBound = 0.0f;
        float prevHeight = 1.0f;
        float currBound = 0.0f;
        float currHeight = 1.0f;
        [loop] for (int i = 0; i < steps; ++i)
        {
            currBound = min(1.0f, prevBound + stepSize);
            currHeight = GetFlowmapParallaxHeight(input, currBound * rayOffset,
                normalScalesRcp, mip);
            if (currHeight <= currBound)
                break;
            prevBound = currBound;
            prevHeight = currHeight;
        }
        float amount = ResolveParallaxIntersection(prevBound, prevHeight,
            currBound, currHeight);
        return rayOffset * amount;
    }

    float2 GetFlowmapParallaxUVOffset(PS_INPUT input,
        float3 viewDirection, float3 normalScalesRcp)
    {
        // Compatibility hook: callers without tile metadata retain a safe unit tile.
        return GetFlowmapParallaxUVOffsetV2(input, viewDirection,
            normalScalesRcp, 1.0f.xx);
    }

    float2 GetFlowmapParallaxOffset(PS_INPUT input,
        float2 flowmapDimensions, float3 viewDirection, float3 normalScalesRcp)
    {
        return GetFlowmapParallaxUVOffsetV2(input, viewDirection,
            normalScalesRcp, flowmapDimensions);
    }

    // Preferred v2 water normal path: reuse the already-computed flowmap
    // intersection rather than launching an independent 16-step POM ray.
    // Amount includes view/distance/footprint fades in GetFlowmapParallaxAmount.
    float2 GetFlowmapNormalUVOffsetFromAmount(float3 viewDirection,
        float parallaxAmount)
    {
        if (!isfinite(parallaxAmount) || parallaxAmount <= 1.0e-4f)
            return 0.0f.xx;
        return SafeParallaxDirection(viewDirection) *
            (80.0f * saturate(parallaxAmount));
    }
#endif // FLOWMAP
}
