// PIXL Renderer - Foliage Optimizer GPU kernel
// Derived from Community Shaders 1.9.1 Grass Optimizations; adapted for PIXL Renderer.
// SPDX-License-Identifier: GPL-3.0-or-later

Texture2D<float4> BaseTex : register(t0);
RWStructuredBuffer<uint> Result : register(u0);

[numthreads(1, 1, 1)] void main()
{
    // Query the actual resource because Skyrim's rendertexture metadata can still be zero
    // while the grass type is being created.
    uint w, h;
    BaseTex.GetDimensions(w, h);
    if (w == 0 || h == 0)
    {
        Result[0] = asuint(0.0);
        return;
    }

    // Complex grass stores packed normals in the lower half of the atlas. A single border
    // texel is fragile with custom textures/compression, so measure a tiny deterministic
    // 4x2 pattern inside that region. The CPU keeps the existing "length ~= 1" threshold.
    float sumLength = 0.0;
    [unroll] for (uint y = 0; y < 2; ++y)
    {
        [unroll] for (uint x = 0; x < 4; ++x)
        {
            const uint px = min(w - 1, ((2u * x + 1u) * w) / 8u);
            const uint py = min(h - 1, ((2u * y + 5u) * h) / 8u);
            const float3 n = BaseTex.Load(int3(int2(px, py), 0)).xyz * 2.0 - 1.0;
            sumLength += length(n);
        }
    }

    Result[0] = asuint(sumLength * (1.0 / 8.0));
}
