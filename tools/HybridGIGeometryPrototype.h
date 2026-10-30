#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace PIXL::HybridGIGeometryPrototype
{
    using Point=std::array<float,3>;
    // Caller-owned, absolute-world-space data only. This interface deliberately
    // cannot retain BSGeometry/TriShape pointers or borrow engine vertex arrays.
    struct Triangle { Point a{},b{},c{}; bool deforming{}, transparent{}; };
    struct Surfel { Point position{},normal{}; float confidence{0.25f}; std::uint32_t state{6}; };
    struct Batch {
        std::array<Surfel,64> surfels{};
        std::uint32_t count{};
        bool overflow{};
    };
    inline Batch Convert(std::span<const Triangle> triangles, std::uint64_t sourceFrame,
        std::uint64_t sourceEpoch, std::uint64_t expectedFrame, std::uint64_t expectedEpoch)
    {
        Batch result;
        if(sourceFrame!=expectedFrame || sourceEpoch!=expectedEpoch) return result;
        // Bound CPU work as well as output. Overflow requests reference fallback,
        // never a silent claim that the enumerated scene was complete.
        const auto inspected=triangles.size()<256?triangles.size():256;
        result.overflow=triangles.size()>inspected;
        for(std::size_t i=0;i<inspected;++i) {
            const auto& t=triangles[i];
            if(t.deforming || t.transparent) continue;
            bool finite=true; for(const auto* point:{&t.a,&t.b,&t.c}) for(float value:*point) finite&=std::isfinite(value);
            if(!finite) continue;
            Point u{},v{},normal{};
            for(unsigned axis=0;axis<3;++axis) { u[axis]=t.b[axis]-t.a[axis]; v[axis]=t.c[axis]-t.a[axis]; }
            normal={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
            const float squared=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
            if(!std::isfinite(squared) || squared<1e-8f) continue;
            if(result.count==result.surfels.size()) { result.overflow=true; break; }
            auto& surfel=result.surfels[result.count++];
            const float inv=1.0f/std::sqrt(squared);
            for(unsigned axis=0;axis<3;++axis) {
                surfel.position[axis]=t.a[axis]/3.0f+t.b[axis]/3.0f+t.c[axis]/3.0f;
                surfel.normal[axis]=normal[axis]*inv;
            }
        }
        return result;
    }
}
