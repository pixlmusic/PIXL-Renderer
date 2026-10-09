#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace PIXL::Reconstruction
{
using CameraMatrix = std::array<float, 16>;
struct CameraFrame
{
    CameraMatrix viewInverse{}, projection{}, currentVP{}, previousVP{};
    std::array<float, 3> origin{}, previousOrigin{};
    std::array<float, 2> jitter{};
    uint64_t frame = UINT64_MAX;
    float aspect{}, fov{}, nearPlane{}, farPlane{};
    bool reset = true;
};
struct CameraReprojection
{
    CameraMatrix clipToView{}, clipToPrevious{}, previousToClip{};
};

// Matrices here use row vectors, after transposing the engine cbuffer layout.
// Compute each direction from its constituent transforms in double precision;
// do not invert a rounded float reprojection containing a large origin offset.
inline bool BuildCameraReprojection(const CameraFrame& frame, CameraReprojection& result)
{
    using Wide = std::array<double, 16>;
    auto widen = [](const CameraMatrix& input) { Wide output{}; std::copy(input.begin(), input.end(), output.begin()); return output; };
    auto multiply = [](const Wide& left, const Wide& right) {
        Wide output{};
        for (size_t row = 0; row < 4; ++row)
            for (size_t col = 0; col < 4; ++col)
                for (size_t k = 0; k < 4; ++k) output[row * 4 + col] += left[row * 4 + k] * right[k * 4 + col];
        return output;
    };
    auto inverse = [](Wide input, Wide& output) {
        output = {}; for (size_t i = 0; i < 4; ++i) output[i * 5] = 1;
        for (size_t col = 0; col < 4; ++col) {
            size_t pivot = col;
            for (size_t row = col + 1; row < 4; ++row) if (std::abs(input[row * 4 + col]) > std::abs(input[pivot * 4 + col])) pivot = row;
            const double divisor = input[pivot * 4 + col];
            if (!std::isfinite(divisor) || std::abs(divisor) < 1.0e-15) return false;
            for (size_t k = 0; k < 4; ++k) { std::swap(input[col * 4 + k], input[pivot * 4 + k]); std::swap(output[col * 4 + k], output[pivot * 4 + k]); }
            for (size_t k = 0; k < 4; ++k) { input[col * 4 + k] /= divisor; output[col * 4 + k] /= divisor; }
            for (size_t row = 0; row < 4; ++row) if (row != col) {
                const double factor = input[row * 4 + col];
                for (size_t k = 0; k < 4; ++k) { input[row * 4 + k] -= factor * input[col * 4 + k]; output[row * 4 + k] -= factor * output[col * 4 + k]; }
            }
        }
        return true;
    };
    auto narrow = [](const Wide& input, CameraMatrix& output) {
        for (size_t i = 0; i < 16; ++i) {
            if (!std::isfinite(input[i]) || std::abs(input[i]) > std::numeric_limits<float>::max()) return false;
            output[i] = static_cast<float>(input[i]);
        }
        return true;
    };
    Wide invProjection{};
    if (!inverse(widen(frame.projection), invProjection) || !narrow(invProjection, result.clipToView)) return false;
    if (frame.reset) {
        result.clipToPrevious = {}; result.previousToClip = {};
        for (size_t i = 0; i < 4; ++i) result.clipToPrevious[i * 5] = result.previousToClip[i * 5] = 1;
        return true;
    }
    Wide inverseCurrent{}, inversePrevious{}, forwardOffset{}, reverseOffset{};
    for (size_t i = 0; i < 4; ++i) forwardOffset[i * 5] = reverseOffset[i * 5] = 1;
    for (size_t i = 0; i < 3; ++i) {
        const double delta = static_cast<double>(frame.origin[i]) - static_cast<double>(frame.previousOrigin[i]);
        if (!std::isfinite(delta)) return false;
        forwardOffset[12 + i] = delta; reverseOffset[12 + i] = -delta;
    }
    if (!inverse(widen(frame.currentVP), inverseCurrent) || !inverse(widen(frame.previousVP), inversePrevious)) return false;
    return narrow(multiply(multiply(inverseCurrent, forwardOffset), widen(frame.previousVP)), result.clipToPrevious) &&
           narrow(multiply(multiply(inversePrevious, reverseOffset), widen(frame.currentVP)), result.previousToClip);
}
}
