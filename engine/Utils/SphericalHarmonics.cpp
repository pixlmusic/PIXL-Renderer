#include "SphericalHarmonics.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kHalfPi = 0.5f * kPi;
    constexpr float kY00 = 0.28209479177387814347f;
    constexpr float kY1 = 0.48860251190291992159f;
    constexpr float kCosineY00 = 0.88622692545275801365f; // pi * Y00
    constexpr float kCosineY1 = 1.02332670794648848849f;  // 2pi/3 * Y1
    constexpr float kTwoPiOverThree = 2.09439510239319549231f;
    constexpr float kFourPiOverSix = kTwoPiOverThree;
    constexpr float kZonalY20 = 0.31539156525252000603f; // sqrt(5 / 16pi)
    constexpr float kMaxRadianceAnisotropy = 1.73205080756887729353f; // sqrt(3)
    constexpr float kMinLengthSquared = 1e-12f;

    using SphericalHarmonics::SH2;

    // Shared l=0/l=1 construction: keeps sign/order consistent in every path.
    inline SH2 DirectionalSH(const float3& d, float dc, float l1) noexcept
    {
        return SH2(dc, -l1 * d.y, l1 * d.z, -l1 * d.x);
    }

    inline float Dot3(const float3& a, const float3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // The two view-dependent operations explicitly handle degenerate inputs;
    // the generic SH basis operations preserve the unit-direction contract.
    inline float3 UnitOr(const float3& v, const float3& fallback) noexcept
    {
        const float len2 = Dot3(v, v);
        if (!(len2 > kMinLengthSquared) || !std::isfinite(len2))
            return fallback;

        const float invLength = 1.0f / std::sqrt(len2);
        return float3(v.x * invLength, v.y * invLength, v.z * invLength);
    }

    inline float SafeRoughness(float roughness) noexcept
    {
        return std::isfinite(roughness) ? std::clamp(roughness, 0.0f, 1.0f) : 1.0f;
    }

    // Project the six axis samples directly; this is algebraically equivalent
    // to summing six Evaluate(dir) * (4pi/6) terms, without a temporary array,
    // six basis evaluations, or repeated SH2Color additions.
    inline SH2 ProjectSixAxes(float xp, float xn, float yp, float yn, float zp, float zn) noexcept
    {
        constexpr float dcScale = kFourPiOverSix * kY00;
        constexpr float l1Scale = kFourPiOverSix * kY1;
        return SH2(
            dcScale * ((xp + xn) + (yp + yn) + (zp + zn)),
            -l1Scale * (yp - yn),
            l1Scale * (zp - zn),
            -l1Scale * (xp - xn));
    }

    inline float NonnegativeFinite(float v) noexcept
    {
        return std::isfinite(v) ? std::max(0.0f, v) : 0.0f;
    }
}

namespace SphericalHarmonics
{
    SH2 Evaluate(float3 dir)
    {
        return DirectionalSH(dir, kY00, kY1);
    }

    SH2 EvaluateCosineLobe(float3 dir)
    {
        return DirectionalSH(dir, kCosineY00, kCosineY1);
    }

    SH2 EvaluatePhaseHG(float3 dir, float g)
    {
        // Only l=0,1 survive this representation. g=0 is an isotropic phase.
        const float safeG = std::isfinite(g) ? std::clamp(g, -1.0f, 1.0f) : 0.0f;
        return DirectionalSH(dir, kY00, kY1 * safeG);
    }

    float Dot(SH2 a, SH2 b)
    {
        return (a.c0 * b.c0 + a.c1[0] * b.c1[0]) +
               (a.c1[1] * b.c1[1] + a.c1[2] * b.c1[2]);
    }

    float3 Dot(SH2Color a, SH2Color b)
    {
        return float3(Dot(a.r, b.r), Dot(a.g, b.g), Dot(a.b, b.b));
    }

    float Unproject(float3 dir, SH2 sh)
    {
        return kY00 * sh.c0 + kY1 *
            (-sh.c1[0] * dir.y + sh.c1[1] * dir.z - sh.c1[2] * dir.x);
    }

    float3 Unproject(float3 dir, SH2Color sh)
    {
        return float3(Unproject(dir, sh.r), Unproject(dir, sh.g), Unproject(dir, sh.b));
    }

    SH2 Add(SH2 a, SH2 b)
    {
        return SH2(a.c0 + b.c0,
                   a.c1[0] + b.c1[0],
                   a.c1[1] + b.c1[1],
                   a.c1[2] + b.c1[2]);
    }

    SH2Color Add(SH2Color a, SH2Color b)
    {
        return SH2Color(Add(a.r, b.r), Add(a.g, b.g), Add(a.b, b.b));
    }

    SH2 Scale(SH2 sh, float scale)
    {
        return SH2(sh.c0 * scale,
                   sh.c1[0] * scale,
                   sh.c1[1] * scale,
                   sh.c1[2] * scale);
    }

    SH2Color Scale(SH2Color sh, float scale)
    {
        return SH2Color(Scale(sh.r, scale), Scale(sh.g, scale), Scale(sh.b, scale));
    }

    SH2 Rotate(SH2 sh, float3x3 rotMatrix)
    {
        // PIXL's SH l=1 coefficients encode XYZ as (-c1[2], -c1[0], c1[1]).
        // Rotating the raw c1 array as if it were XYZ is mathematically wrong.
        // DirectX XMFLOAT3X3 uses row-vector transformation: p' = p * M.
        const float x = -sh.c1[2];
        const float y = -sh.c1[0];
        const float z =  sh.c1[1];

        const float rx = x * rotMatrix._11 + y * rotMatrix._21 + z * rotMatrix._31;
        const float ry = x * rotMatrix._12 + y * rotMatrix._22 + z * rotMatrix._32;
        const float rz = x * rotMatrix._13 + y * rotMatrix._23 + z * rotMatrix._33;
        return SH2(sh.c0, -ry, rz, -rx);
    }

    SH2Color Rotate(SH2Color sh, float3x3 rotMatrix)
    {
        return SH2Color(Rotate(sh.r, rotMatrix),
                        Rotate(sh.g, rotMatrix),
                        Rotate(sh.b, rotMatrix));
    }

    float FuncProductIntegral(SH2 shL, SH2 shR)
    {
        return Dot(shL, shR); // Orthonormal basis, no additional 4pi factor.
    }

    float3 FuncProductIntegral(SH2Color shL, SH2Color shR)
    {
        return Dot(shL, shR);
    }

    SH2 Product(SH2 shL, SH2 shR)
    {
        // Gaunt coefficients for truncation to l<=1. The l=1*l=1 product
        // contributes to l=0 (and omitted l=2), never to l=1.
        return SH2(
            kY00 * Dot(shL, shR),
            kY00 * (shL.c0 * shR.c1[0] + shL.c1[0] * shR.c0),
            kY00 * (shL.c0 * shR.c1[1] + shL.c1[1] * shR.c0),
            kY00 * (shL.c0 * shR.c1[2] + shL.c1[2] * shR.c0));
    }

    SH2Color Product(SH2Color shL, SH2Color shR)
    {
        return SH2Color(Product(shL.r, shR.r),
                        Product(shL.g, shR.g),
                        Product(shL.b, shR.b));
    }

    SH2 HanningConvolution(SH2 sh, float w)
    {
        if (!(w > 0.0f)) // Zero/negative/NaN: retain legacy "disabled" behavior.
            return sh;

        // Hann's l=1 term is defined for a window at least one band wide.
        // Unclamped widths below one make cos(pi/w) oscillate and reintroduce
        // high-frequency energy; instead attenuate the band to zero.
        const float band1 = w <= 1.0f ? 0.0f :
            0.5f * (1.0f + std::cos(kPi / w));
        return SH2(sh.c0,
                   sh.c1[0] * band1,
                   sh.c1[1] * band1,
                   sh.c1[2] * band1);
    }

    SH2Color HanningConvolution(SH2Color sh, float w)
    {
        return SH2Color(HanningConvolution(sh.r, w),
                        HanningConvolution(sh.g, w),
                        HanningConvolution(sh.b, w));
    }

    SH2 DiffuseConvolution(SH2 sh)
    {
        // Raw irradiance: pi for l=0, 2pi/3 for l=1.
        return SH2(sh.c0 * kPi,
                   sh.c1[0] * kTwoPiOverThree,
                   sh.c1[1] * kTwoPiOverThree,
                   sh.c1[2] * kTwoPiOverThree);
    }

    SH2Color DiffuseConvolution(SH2Color sh)
    {
        return SH2Color(DiffuseConvolution(sh.r),
                        DiffuseConvolution(sh.g),
                        DiffuseConvolution(sh.b));
    }

    // Dominant-direction idea: ProfJack, GDC 2020, Fast Denoising With
    // Self-Stabilizing Reprojection. Half-angle polynomial: Olivier Therrien.
    SH2 FauxSpecularLobe(float3 N, float3 V, float roughness)
    {
        const float3 normal = UnitOr(N, float3(0.0f, 0.0f, 1.0f));
        const float3 view = UnitOr(V, normal);
        const float r = SafeRoughness(roughness);

        const float f = (1.0f - r) * (std::sqrt(1.0f - r) + r);
        const float ndv = Dot3(normal, view);
        // reflect(-V, N), written explicitly to avoid temporary vector types.
        const float3 reflected(-view.x + 2.0f * ndv * normal.x,
                               -view.y + 2.0f * ndv * normal.y,
                               -view.z + 2.0f * ndv * normal.z);
        const float3 dominant = UnitOr(
            float3(reflected.x * f + normal.x * (1.0f - f),
                   reflected.y * f + normal.y * (1.0f - f),
                   reflected.z * f + normal.z * (1.0f - f)), normal);

        const float r2 = r * r;
        const float halfAngle = std::clamp(
            ((4.1679f * r2 - 9.0127f * r + 4.6161f) * r2 +
             1.7048f * r + 0.1f), 0.0f, kHalfPi);

        // Smaller half-angle = sharper lobe, NOT a larger cosine contribution.
        // Both delta and cosine/pi representations have the same l=0 energy;
        // their l=1 magnitudes are Y1 and (2/3)*Y1 respectively. Blend only
        // the l=1 magnitude to avoid constructing/adding two separate lobes.
        const float sharpness = 1.0f - halfAngle / kHalfPi;
        const float l1 = kY1 * ((2.0f + sharpness) / 3.0f);
        return DirectionalSH(dominant, kY00, l1);
    }

    float SHHallucinateZH3Irradiance(SH2 sh, float3 direction)
    {
        const float3 n = UnitOr(direction, float3(0.0f, 0.0f, 1.0f));
        const float irradiance = Dot(sh, EvaluateCosineLobe(n));

        // The missing l=2 term cannot be recovered from SH2. Infer a zonal
        // l=2 lobe only where positive DC and a usable first-order direction
        // exist. Treat near-black/invalid signals conservatively.
        if (!(sh.c0 > 1e-8f) || !std::isfinite(sh.c0))
            return NonnegativeFinite(irradiance);

        // Convert PIXL l=1 order back to the physical directional vector.
        const float3 axis(-sh.c1[2], -sh.c1[0], sh.c1[1]);
        const float axisLen2 = Dot3(axis, axis);
        if (!(axisLen2 > kMinLengthSquared) || !std::isfinite(axisLen2))
            return NonnegativeFinite(irradiance);

        const float axisLength = std::sqrt(axisLen2);
        // A nonnegative directional distribution obeys |l1|/l0 <= sqrt(3).
        // Clamping rejects corrupt/over-directional probe data that otherwise
        // makes the curve-fit generate enormous unphysical highlights.
        const float ratio = std::min(axisLength / sh.c0, kMaxRadianceAnisotropy);
        const float zonalL2 = sh.c0 * (0.08f * ratio + 0.6f * ratio * ratio);
        const float cosTheta = std::clamp(Dot3(axis, n) / axisLength, -1.0f, 1.0f);
        const float y20 = kZonalY20 * (3.0f * cosTheta * cosTheta - 1.0f);

        // l=2 clamped-cosine convolution is pi/4 for RAW irradiance.
        // The previous 1/4 factor applied the normalized cosine weight only
        // to the hallucinated term, underweighting it relative to l=0 and l=1.
        return NonnegativeFinite(irradiance + (kPi * 0.25f) * zonalL2 * y20);
    }

    float3 SHHallucinateZH3Irradiance(SH2Color sh, float3 direction)
    {
        return float3(SHHallucinateZH3Irradiance(sh.r, direction),
                      SHHallucinateZH3Irradiance(sh.g, direction),
                      SHHallucinateZH3Irradiance(sh.b, direction));
    }

    SH2Color DALCToSH(const float3 dalcColors[6])
    {
        if (!dalcColors)
            return SH2Color();

        const float3& xp = dalcColors[0];
        const float3& xn = dalcColors[1];
        const float3& yp = dalcColors[2];
        const float3& yn = dalcColors[3];
        const float3& zp = dalcColors[4];
        const float3& zn = dalcColors[5];
        return SH2Color(
            ProjectSixAxes(xp.x, xn.x, yp.x, yn.x, zp.x, zn.x),
            ProjectSixAxes(xp.y, xn.y, yp.y, yn.y, zp.y, zn.y),
            ProjectSixAxes(xp.z, xn.z, yp.z, yn.z, zp.z, zn.z));
    }
}
