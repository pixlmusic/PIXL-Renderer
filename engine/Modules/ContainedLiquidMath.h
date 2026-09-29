// PIXL Renderer - profile-aware contained-liquid simulation math.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace PIXL::ContainedLiquidMath {
    // Exact underdamped spring for a piecewise constant acceleration target.
    inline void Spring(float& x,float& v,float target,float damping,float dt,float omega=8.0f)
    {
        if (!std::isfinite(x) || !std::isfinite(v) || !std::isfinite(target) ||
            !std::isfinite(damping) || !std::isfinite(dt) || !std::isfinite(omega)) {
            x=0.0f;v=0.0f;return;
        }
        damping=std::clamp(damping,0.01f,0.98f);
        dt=std::clamp(dt,0.0f,0.1f);
        omega=std::clamp(omega,2.0f,20.0f);
        const float a=damping*omega,b=omega*std::sqrt(1.0f-damping*damping);
        const float c=std::cos(b*dt),s=std::sin(b*dt),e=std::exp(-a*dt);
        const float y=x-target,q=(v+a*y)/b;
        x=target+e*(y*c+q*s);
        v=e*(v*c-(a*v+omega*omega*y)*s/b);
        if (std::abs(x)>0.22f) {x=std::clamp(x,-0.22f,0.22f);v=0;}
    }
    // Fraction below a plane in an affine unit sphere/ellipsoid.
    inline float FillHeight(float fraction)
    {
        float lo=-1,hi=1;
        for (int i=0;i<20;++i) {
            const float h=(lo+hi)*0.5f;
            if ((2+3*h-h*h*h)*0.25f<fraction)lo=h;else hi=h;
        }
        return (lo+hi)*0.5f;
    }

    template <std::size_t N>
    inline float ProfileRadius(const std::array<float,N>& profile,float z)
    {
        static_assert(N>=2);
        const float coordinate=std::clamp((z+1.0f)*0.5f,0.0f,1.0f)*static_cast<float>(N-1);
        const auto lower=static_cast<std::size_t>(std::floor(coordinate));
        const auto upper=std::min(lower+1,N-1);
        const float t=coordinate-static_cast<float>(lower);
        return std::lerp(profile[lower],profile[upper],t);
    }

    inline float DiskFractionBelow(float normalizedOffset)
    {
        if (normalizedOffset<=-1.0f) return 0.0f;
        if (normalizedOffset>=1.0f) return 1.0f;
        const float root=std::sqrt(std::max(1.0f-normalizedOffset*normalizedOffset,0.0f));
        return 0.5f+(std::asin(normalizedOffset)+normalizedOffset*root)/std::numbers::pi_v<float>;
    }

    // Integrates an axisymmetric piecewise-linear bottle profile clipped by
    // plane.x*x + plane.y*y + plane.z*z <= offset. Coordinates are normalized
    // profile coordinates, while plane carries the real scaled support lengths.
    template <std::size_t N>
    inline float ProfileVolumeBelow(const std::array<float,N>& profile,
        const std::array<float,3>& plane,float offset)
    {
        constexpr int integrationSteps=64;
        const float radialPlane=std::hypot(plane[0],plane[1]);
        const auto integrateArea=[&](float startZ,float endZ) {
            if (!(endZ>startZ)) return 0.0f;
            float volume=0.0f;
            for (int step=0;step<=integrationSteps;++step) {
                const float z=std::lerp(startZ,endZ,static_cast<float>(step)/integrationSteps);
                const float radius=std::max(ProfileRadius(profile,z),0.0f);
                const float weight=(step==0||step==integrationSteps)?0.5f:1.0f;
                volume+=std::numbers::pi_v<float>*radius*radius*weight;
            }
            return volume*((endZ-startZ)/integrationSteps);
        };
        if (radialPlane<=1.0e-6f) {
            if (std::abs(plane[2])<=1.0e-6f)
                return offset>=0.0f?integrateArea(-1.0f,1.0f):0.0f;
            const float boundary=std::clamp(offset/plane[2],-1.0f,1.0f);
            return plane[2]>0.0f?integrateArea(-1.0f,boundary):integrateArea(boundary,1.0f);
        }
        float clipped=0.0f;
        for (int step=0;step<=integrationSteps;++step) {
            const float z=-1.0f+2.0f*static_cast<float>(step)/integrationSteps;
            const float radius=std::max(ProfileRadius(profile,z),0.0f);
            float fraction;
            fraction=DiskFractionBelow((offset-plane[2]*z)/std::max(radialPlane*radius,1.0e-6f));
            const float area=std::numbers::pi_v<float>*radius*radius*fraction;
            const float weight=(step==0||step==integrationSteps)?0.5f:1.0f;
            clipped+=area*weight;
        }
        return clipped*(2.0f/integrationSteps);
    }

    template <std::size_t N>
    inline float ProfileFillOffset(const std::array<float,N>& profile,
        const std::array<float,3>& plane,float fraction)
    {
        fraction=std::clamp(fraction,0.0f,1.0f);
        const float maxRadius=*std::max_element(profile.begin(),profile.end());
        const float support=std::hypot(plane[0],plane[1])*maxRadius+std::abs(plane[2]);
        const float total=ProfileVolumeBelow(profile,plane,support+1.0f);
        if (!(total>1.0e-6f) || !std::isfinite(total)) return 0.0f;
        float lo=-support,hi=support;
        for (int iteration=0;iteration<24;++iteration) {
            const float middle=(lo+hi)*0.5f;
            if (ProfileVolumeBelow(profile,plane,middle)<total*fraction) lo=middle;
            else hi=middle;
        }
        return (lo+hi)*0.5f;
    }
}
