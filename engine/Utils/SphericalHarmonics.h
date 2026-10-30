#pragma once

#include <DirectXMath.h>

// PIXL supplies float3 (typically via the renderer's shared/precompiled header).
// Retain the public alias and all signatures for existing source/binary consumers.
using float3x3 = DirectX::XMFLOAT3X3;

namespace SphericalHarmonics
{
    /**
     * Two-band real SH (l = 0, 1), four coefficients in PIXL's existing order:
     *   c0    = Y(0, 0)
     *   c1[0] = Y(1,-1) = -sqrt(3 / 4pi) * y
     *   c1[1] = Y(1, 0) =  sqrt(3 / 4pi) * z
     *   c1[2] = Y(1, 1) = -sqrt(3 / 4pi) * x
     *
     * The coefficient layout is intentionally unchanged (4 contiguous floats).
     * "SH2" is the established PIXL name; it represents two bands, NOT l = 0..2.
     */
    struct SH2
    {
        float c0;
        float c1[3];

        constexpr SH2() noexcept : c0(0.0f), c1{ 0.0f, 0.0f, 0.0f } {}
        constexpr SH2(float dc, float yNeg, float z, float xNeg) noexcept
            : c0(dc), c1{ yNeg, z, xNeg } {}
    };

    /** Three independent SH2 functions, ordered R, G, B (12 floats total). */
    struct SH2Color
    {
        SH2 r;
        SH2 g;
        SH2 b;

        constexpr SH2Color() noexcept : r(), g(), b() {}
        constexpr explicit SH2Color(const SH2& sh) noexcept : r(sh), g(sh), b(sh) {}
        constexpr SH2Color(SH2 red, SH2 green, SH2 blue) noexcept
            : r(red), g(green), b(blue) {}
    };

    static_assert(sizeof(SH2) == 4 * sizeof(float), "SH2 storage/layout changed");
    static_assert(sizeof(SH2Color) == 12 * sizeof(float), "SH2Color storage/layout changed");

    // All directional arguments use renderer/world-space XYZ and should be unit
    // length. These low-level basis evaluators do not renormalize their inputs.
    SH2 Evaluate(float3 dir);
    SH2 EvaluateCosineLobe(float3 dir);           // Unnormalized max(dot(n,w),0).
    SH2 EvaluatePhaseHG(float3 dir, float g);      // l<=1 HG projection; g in [-1,1].

    float Dot(SH2 a, SH2 b);
    float3 Dot(SH2Color a, SH2Color b);
    float Unproject(float3 dir, SH2 sh);
    float3 Unproject(float3 dir, SH2Color sh);

    SH2 Add(SH2 a, SH2 b);
    SH2Color Add(SH2Color a, SH2Color b);
    SH2 Scale(SH2 sh, float scale);
    SH2Color Scale(SH2Color sh, float scale);

    // Rotate the physical l=1 XYZ vector, then restore the PIXL SH sign/order.
    // rotMatrix follows DirectXMath's row-vector (v * M) convention; l=0 unchanged.
    // Matrix must be orthonormal for an actual SH rotation.
    SH2 Rotate(SH2 sh, float3x3 rotMatrix);
    SH2Color Rotate(SH2Color sh, float3x3 rotMatrix);

    float FuncProductIntegral(SH2 shL, SH2 shR);
    float3 FuncProductIntegral(SH2Color shL, SH2Color shR);

    // Orthogonal projection of a product back onto l <= 1; omits l=2 terms.
    SH2 Product(SH2 shL, SH2 shR);
    SH2Color Product(SH2Color shL, SH2Color shR);

    // w <= 0 disables filtering. 0 < w <= 1 fully removes the l=1 band;
    // w > 1 progressively retains it. A non-finite/disabled width is ignored.
    SH2 HanningConvolution(SH2 sh, float w);
    SH2Color HanningConvolution(SH2Color sh, float w);

    // Irradiance (NOT irradiance / pi): l=0 * pi, l=1 * 2pi/3.
    SH2 DiffuseConvolution(SH2 sh);
    SH2Color DiffuseConvolution(SH2Color sh);

    // Energy-preserving l<=1 approximation of a GGX-like lobe. In SH2 even a
    // perfectly smooth reflection remains low-frequency; it is not full GGX.
    // V points from the surface toward the viewer; N is the surface normal.
    SH2 FauxSpecularLobe(float3 N, float3 V, float roughness);

    // Heuristically infer missing l=2 directional structure and estimate
    // NON-NEGATIVE irradiance. Inputs must represent (primarily) nonnegative
    // radiance; this reconstruction is not an exact higher-order projection.
    float SHHallucinateZH3Irradiance(SH2 sh, float3 direction);
    float3 SHHallucinateZH3Irradiance(SH2Color sh, float3 direction);

    // Six equally-weighted axis samples ordered +X,-X,+Y,-Y,+Z,-Z.
    // Exact projection for constant and linear angular functions on these axes.
    SH2Color DALCToSH(const float3 dalcColors[6]);
}
