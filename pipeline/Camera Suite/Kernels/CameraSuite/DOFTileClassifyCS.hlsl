// PIXL Renderer - complete CoC tile reduction.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.
#include "CameraSuite/DofControl.hlsli"
Texture2D<float> CoCTex : register(t0);
RWTexture2D<float4> TileOut : register(u0);
groupshared float2 CoCRange[64];
[numthreads(8, 8, 1)]
void main(uint3 tile : SV_GroupID, uint3 lane : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    uint width, height;
    CoCTex.GetDimensions(width, height);
    float2 range = float2(1e20f, -1e20f);
    [unroll] for (uint y=0; y<2; ++y) [unroll] for (uint x=0; x<2; ++x) {
        uint2 p = min(tile.xy*16u + lane.xy*2u + uint2(x,y), uint2(width,height)-1u);
        float coc = CoCTex.Load(int3(p,0));
        range = float2(min(range.x,coc),max(range.y,coc));
    }
    CoCRange[index]=range;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride=32u;stride>0u;stride>>=1u) {
        if(index<stride) CoCRange[index]=float2(min(CoCRange[index].x,CoCRange[index+stride].x),max(CoCRange[index].y,CoCRange[index+stride].y));
        GroupMemoryBarrierWithGroupSync();
    }
    if(index==0u) {
        float threshold=max(0.75f,dofControlMaxCoCPixels*0.035f);
        uint flags=(CoCRange[0].x < -threshold ? 1u:0u) | (CoCRange[0].y > threshold ? 2u:0u);
        TileOut[tile.xy]=float4(CoCRange[0],(float)flags,0);
    }
}