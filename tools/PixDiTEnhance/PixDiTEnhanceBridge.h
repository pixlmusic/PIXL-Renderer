#pragma once

// PIXL PixDiT bridge interface sketch. This header is intentionally not included
// in the renderer build until production weights and the DirectML runtime path
// pass the acceptance matrix in hlsl_bridge_spec.md.

#include <cstdint>
#include <filesystem>
#include <string>

namespace PIXL::PhotoMode
{
    enum class NeuralBackend : std::uint32_t
    {
        Disabled = 0,
        Auto,
        DirectML,
        TensorRT,
        CPUDeveloper
    };

    struct PixDiTEnhanceSettings
    {
        bool enabled = false;
        NeuralBackend backend = NeuralBackend::DirectML;
        float blend = 0.35f;
        float maximumResidual = 0.10f;
        float highlightProtection = 0.75f;
        float shadowProtection = 0.35f;
        std::uint32_t tileSize = 512;
        std::uint32_t tileOverlap = 64;
    };

    struct PixDiTEnhanceResult
    {
        bool enhanced = false;
        std::filesystem::path originalPath;
        std::filesystem::path enhancedPath;
        std::string backend;
        std::string failureReason;
        double elapsedMilliseconds = 0.0;
    };
}

