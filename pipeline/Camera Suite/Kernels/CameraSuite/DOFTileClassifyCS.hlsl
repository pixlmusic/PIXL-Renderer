// PIXL Renderer - DOF tile min/max classification and dilation.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "CameraSuite/DofControl.hlsli"

Texture2D<float> CoCTex : register(t0);
RWTexture2D<float4> TileOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint tileWidth, tileHeight;
    TileOut.GetDimensions(tileWidth, tileHeight);
    if (id.x >= tileWidth || id.y >= tileHeight) return;
    uint width, height;
    CoCTex.GetDimensions(width, height);
    float minCoC = 1.0e20f;
    float maxCoC = -1.0e20f;
    uint2 base = id.xy * 16u;
    [unroll]
    for (uint y = 0; y < 4; ++y) [unroll]
    for (uint x = 0; x < 4; ++x) {
        uint2 p = min(base + uint2(x * 4u, y * 4u), uint2(width, height) - 1u);
        float coc = CoCTex.Load(int3(p, 0));
        minCoC = min(minCoC, coc);
        maxCoC = max(maxCoC, coc);
    }
    float threshold = max(0.75f, dofControlMaxCoCPixels * 0.035f);
    uint flags = 0u;
    if (abs(minCoC) > threshold || abs(maxCoC) > threshold) flags = 1u;
    if (minCoC < -threshold && maxCoC > threshold) flags = 3u;
    else if (minCoC < -threshold) flags = 1u;
    else if (maxCoC > threshold) flags = 2u;
    TileOut[id.xy] = float4(minCoC, maxCoC, (float)flags, 0.0f);
}
