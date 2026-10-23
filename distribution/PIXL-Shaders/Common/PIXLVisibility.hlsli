// PIXL Renderer shared conservative visibility helpers.
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PIXL_VISIBILITY_HLSLI
#define PIXL_VISIBILITY_HLSLI

namespace PIXLVisibility
{
    float QueryConservativeDepth(Texture2D<float> pyramid, int2 texel, uint mipLevel)
    {
        return pyramid.Load(int3(texel, mipLevel));
    }

    // Skyrim's active scene-depth convention is 0 near / 1 far. The pyramid stores
    // maxima, so visible is the fail-open result whenever the nearest bound is not
    // conclusively behind the farthest covered scene sample.
    bool TestDepthVisibility(float nearestObjectDepth, float conservativeSceneDepth, float bias)
    {
        return nearestObjectDepth <= conservativeSceneDepth + max(bias, 0.0);
    }
}

#endif
