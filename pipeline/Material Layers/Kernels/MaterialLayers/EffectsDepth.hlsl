#include "Common/SharedData.hlsli"

// Dedicated post-geometry CS bindings; never bound while GBuffer RTVs are active.
Texture2D<float> RasterDepth : register(t0);
Texture2D<float2> MaterialMasks : register(t1);
RWTexture2D<float> EffectsDepth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    EffectsDepth.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    float raster = RasterDepth[id.xy];
    float offset = MaterialMasks[id.xy].y;
    float result = raster;
    if (raster > 0.0f && raster < 1.0f && isfinite(offset) && abs(offset) > 1e-5f) {
        float viewDepth = SharedData::GetScreenDepth(raster);
        // Keep near-camera relief bounded even if an imported preset is extreme.
        offset = clamp(offset, -min(64.0f, viewDepth * 0.1f), min(64.0f, viewDepth * 0.1f));
        float displaced = max(viewDepth + offset, 1e-3f);
        float projected = (SharedData::CameraData.x - SharedData::CameraData.w / displaced) / SharedData::CameraData.z;
        if (isfinite(projected) && projected > 0.0f && projected < 1.0f)
            result = projected;
    }
    EffectsDepth[id.xy] = result;
}
