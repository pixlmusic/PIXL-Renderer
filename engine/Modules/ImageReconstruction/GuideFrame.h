#pragma once
#include <cstdint>

namespace PIXL::Reconstruction
{
// Both textures contain input-resolution guides at pixel origin (0,0).
// Publish only after the encoded motion and unexpanded depth copies are issued.
struct NeuralGuideFrame
{
    uint64_t frame = UINT64_MAX;
    uint32_t width = 0;
    uint32_t height = 0;
    bool IsValidForAllocation(uint32_t allocationWidth, uint32_t allocationHeight) const
    {
        // D3D11 guide copies and the D3D12 Present path can observe adjacent
        // engine frames. The tag is useful for diagnostics, but comparing it
        // with a mutable global/camera frame at Present can reject valid guides.
        return frame != UINT64_MAX && width > 1 && height > 1 && width <= allocationWidth && height <= allocationHeight;
    }
};
}
