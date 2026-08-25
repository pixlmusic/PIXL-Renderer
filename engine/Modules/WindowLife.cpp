#include "WindowLife.h"

#include "Globals.h"
#include "State.h"
#include "Util.h"

#include <DirectXTex.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string_view>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    WindowLife::Settings,
    EnableWindowLife,
    EnableArchitecturalGlass,
    DayShadowStrength,
    NightShadowStrength,
    DayActivity,
    EveningActivity,
    LateNightActivity,
    ParallaxDepth,
    Refraction,
    SilhouetteSoftness,
    HumanScale,
    CurtainStrength,
    RoomDepthStrength,
    EnableAuthoredRooms,
    AuthoredRoomStrength,
    UseAuthoredMaskLayout,
    EnableInteriorPassers,
    DistanceFadeStart,
    DistanceFadeEnd,
    PaneThreshold,
    PaneSoftness,
    RoomWidth,
    RoomHeight,
    MotionSpeed,
    GlassStrength,
    GlassReflectionBoost,
    GlassRoughness,
    GlassTransmission,
    GlassDirtStrength,
    GlassDistortion,
    GlassNormalRetention,
    SuppressWindowAutoPOM,
    MinShallowWindowRadius,
    MinFullWindowRadius,
    FullWindowVerticality,
    DebugWindowDetection);

namespace
{
    std::string Lower(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    }

    bool ContainsAny(std::string_view text, std::initializer_list<std::string_view> tokens)
    {
        for (auto token : tokens) {
            if (text.find(token) != std::string_view::npos)
                return true;
        }
        return false;
    }

    bool EndsWith(std::string_view text, std::string_view suffix)
    {
        return text.size() >= suffix.size() &&
            text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string CanonicalWindowMaskKey(std::string_view path)
    {
        const auto slash = path.find_last_of("\\/");
        std::string key = Lower(slash == std::string_view::npos ? path : path.substr(slash + 1));
        const auto extension = key.find_last_of('.');
        if (extension != std::string::npos)
            key.resize(extension);
        if (EndsWith(key, "_mask"))
            key.resize(key.size() - 5u);
        if (EndsWith(key, "noalpha"))
            key.resize(key.size() - 7u);
        while (!key.empty() && (key.back() == '_' || key.back() == '-'))
            key.pop_back();
        return key;
    }

    float Smooth01(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float SmoothRange(float edge0, float edge1, float x)
    {
        if (edge1 <= edge0)
            return x >= edge1 ? 1.0f : 0.0f;
        return Smooth01((x - edge0) / (edge1 - edge0));
    }
}

void WindowLife::DrawSettings()
{
    ImGui::Checkbox(T("feature.window_life.enable", "Enable Window Life"), &settings.EnableWindowLife);
    if (auto _tt = Util::HoverTooltipWrapper()) {
        ImGui::TextWrapped("%s", T("feature.window_life.enable_tooltip", "Upgrades architectural glass and adds subtle moving occupants behind suitable exterior and interior windows."));
    }

    ImGui::Checkbox(T("feature.window_life.glass_enable", "Architectural Glass Optics"), &settings.EnableArchitecturalGlass);
    if (auto _tt = Util::HoverTooltipWrapper()) {
        ImGui::TextWrapped("%s", T("feature.window_life.glass_enable_tooltip", "Applies old-glass Fresnel response, restrained waviness and stable grime/roughness variation to detected architectural panes."));
    }

    ImGui::Spacing();
    ImGui::Text("%s", T("feature.window_life.glass", "Architectural Glass"));
    ImGui::SliderFloat(T("feature.window_life.glass_strength", "Glass Strength"), &settings.GlassStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.glass_reflection", "Reflection Response"), &settings.GlassReflectionBoost, 0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.glass_roughness", "Base Glass Roughness"), &settings.GlassRoughness, 0.06f, 0.80f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.glass_transmission", "Glass Transmission"), &settings.GlassTransmission, 0.65f, 1.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.glass_dirt", "Grime Variation"), &settings.GlassDirtStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.glass_distortion", "Old Glass Waviness"), &settings.GlassDistortion, 0.0f, 0.09f, "%.3f");
    ImGui::SliderFloat(T("feature.window_life.glass_normal", "Texture Normal Retention"), &settings.GlassNormalRetention, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox(T("feature.window_life.suppress_autopom", "Keep Auto-POM Off Glass Panes"), &settings.SuppressWindowAutoPOM);
    if (auto _tt = Util::HoverTooltipWrapper()) {
        ImGui::TextWrapped("%s", T("feature.window_life.suppress_autopom_tooltip", "Suppresses synthetic Object Auto-POM only on detected pane pixels. Window frames and surrounding architecture keep their normal material depth."));
    }

    ImGui::Spacing();
    ImGui::Text("%s", T("feature.window_life.visibility", "Occupancy"));
    ImGui::SliderFloat(T("feature.window_life.day_shadow", "Day Shadow Strength"), &settings.DayShadowStrength, 0.0f, 0.35f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.night_shadow", "Night Shadow Strength"), &settings.NightShadowStrength, 0.0f, 0.85f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.day_activity", "Day Activity"), &settings.DayActivity, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.evening_activity", "Evening Activity"), &settings.EveningActivity, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.late_activity", "Late Night Activity"), &settings.LateNightActivity, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Text("%s", T("feature.window_life.eligibility", "Window Eligibility"));
    ImGui::SliderFloat(T("feature.window_life.min_shallow_radius", "Minimum Shallow Window Radius"), &settings.MinShallowWindowRadius, 4.0f, 96.0f, "%.0f");
    ImGui::SliderFloat(T("feature.window_life.min_full_radius", "Minimum Full Window Radius"), &settings.MinFullWindowRadius, 12.0f, 160.0f, "%.0f");
    ImGui::SliderFloat(T("feature.window_life.full_verticality", "Full Occupancy Verticality"), &settings.FullWindowVerticality, 0.25f, 0.95f, "%.2f");
    if (auto _tt = Util::HoverTooltipWrapper()) {
        ImGui::TextWrapped("%s", T("feature.window_life.eligibility_tooltip", "Tiny dedicated meshes become glass-only. Sloped roof/awning panes are downgraded in the shader even when their parent geometry is large."));
    }

    ImGui::Spacing();
    ImGui::Text("%s", T("feature.window_life.optics", "Interior Depth"));
    ImGui::SliderFloat(T("feature.window_life.parallax", "Interior Parallax Depth"), &settings.ParallaxDepth, 0.0f, 72.0f, "%.1f");
    ImGui::SliderFloat(T("feature.window_life.refraction", "Interior Refraction"), &settings.Refraction, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.softness", "Silhouette Softness"), &settings.SilhouetteSoftness, 0.015f, 0.16f, "%.3f");
    ImGui::SliderFloat(T("feature.window_life.human_scale", "Human Scale"), &settings.HumanScale, 0.65f, 1.35f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.curtain_strength", "Curtain Layer Strength"), &settings.CurtainStrength, 0.0f, 0.55f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.room_depth_strength", "Recessed Room Depth"), &settings.RoomDepthStrength, 0.0f, 0.40f, "%.2f");
    ImGui::Checkbox(T("feature.window_life.authored_rooms", "Authored Room Backgrounds"), &settings.EnableAuthoredRooms);
    ImGui::SliderFloat(T("feature.window_life.authored_room_strength", "Authored Room Visibility"), &settings.AuthoredRoomStrength, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox(T("feature.window_life.mask_layout", "Fit Interiors to Authored Window Masks"), &settings.UseAuthoredMaskLayout);
    ImGui::Checkbox(T("feature.window_life.interior_passers", "Interior View Passers-by"), &settings.EnableInteriorPassers);
    if (auto _tt = Util::HoverTooltipWrapper()) {
        ImGui::TextWrapped("Glow masks automatically fit the room and occupants to each authored window group. Curtains sit near exterior glass; interior views instead show occasional passers-by outside blocking the window light.");
    }

    ImGui::Spacing();
    ImGui::Text("%s", T("feature.window_life.masking", "Pane Mask & Distance"));
    ImGui::SliderFloat(T("feature.window_life.pane_threshold", "Pane Threshold"), &settings.PaneThreshold, 0.0f, 0.55f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.pane_softness", "Pane Mask Softness"), &settings.PaneSoftness, 0.03f, 0.50f, "%.2f");
    ImGui::SliderFloat(T("feature.window_life.fade_start", "Distance Fade Start"), &settings.DistanceFadeStart, 256.0f, 10000.0f, "%.0f");
    ImGui::SliderFloat(T("feature.window_life.fade_end", "Distance Fade End"), &settings.DistanceFadeEnd, 512.0f, 16000.0f, "%.0f");

    if (globals::state && globals::state->IsDeveloperMode()) {
        ImGui::Spacing();
        ImGui::Text("%s", T("feature.window_life.developer", "Developer"));
        ImGui::SliderFloat(T("feature.window_life.room_width", "Fallback Room Width"), &settings.RoomWidth, 64.0f, 220.0f, "%.0f");
        ImGui::SliderFloat(T("feature.window_life.room_height", "Fallback Room Height"), &settings.RoomHeight, 96.0f, 260.0f, "%.0f");
        if (auto _tt = Util::HoverTooltipWrapper()) {
            ImGui::TextWrapped("Used only when a material has no usable authored glow mask, or when authored-mask fitting is disabled. These values no longer change automatic mask-fit acceptance.");
        }
        ImGui::SliderFloat(T("feature.window_life.motion_speed", "Activity Speed"), &settings.MotionSpeed, 0.25f, 2.5f, "%.2f");
        ImGui::Checkbox(T("feature.window_life.debug_detection", "Show Window Class Overlay"), &settings.DebugWindowDetection);
        if (auto _tt = Util::HoverTooltipWrapper()) {
            ImGui::TextWrapped("With authored fitting enabled: red = fit rejected, cyan = room/background fit, green = full occupant-safe fit. Otherwise blue/amber/green show the material tier.");
        }
        if (ImGui::Button(T("feature.window_life.clear_classifier", "Re-scan Window Materials"))) {
            classificationCache.clear();
            logger::info("[WindowLife] Material classification cache cleared.");
        }
    }
}

void WindowLife::LoadSettings(json& o_json)
{
    settings = o_json;
    classificationCache.clear();
    activeDataValid = false;
    activeDataFrame = ~0u;
}

void WindowLife::SaveSettings(json& o_json)
{
    o_json = settings;
}

void WindowLife::RestoreDefaultSettings()
{
    settings = {};
    classificationCache.clear();
    activeDataValid = false;
    activeDataFrame = ~0u;
}

void WindowLife::SetupResources()
{
    if (!globals::d3d::device)
        return;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(PerGeometryData));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = static_cast<UINT>(sizeof(PerGeometryData));

    DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&desc, nullptr, activeBuffer.put()));
    DX::ThrowIfFailed(globals::d3d::device->CreateBuffer(&desc, nullptr, neutralBuffer.put()));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 1;
    DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(activeBuffer.get(), &srvDesc, activeSRV.put()));
    DX::ThrowIfFailed(globals::d3d::device->CreateShaderResourceView(neutralBuffer.get(), &srvDesc, neutralSRV.put()));

    PerGeometryData neutral{};
    UploadData(neutralBuffer.get(), neutral);
    frameBaseData = {};
    currentActiveData = {};
    activeDataValid = false;
    activeDataFrame = ~0u;

    // Window Shadows-style mask atlases are optional runtime inputs, not bundled
    // PIXL assets. When present, bind the mask matching the diffuse texture stem
    // directly to the real window draw. This avoids rendering the helper proxy
    // geometry and gives the shader the authored pane layout even when the NIF did
    // not bind its glow texture to Skyrim's material slot.
    authoredMaskSRVs.clear();
    const std::filesystem::path authoredMaskRoot = "Data\\Textures\\masks";
    std::size_t loadedMaskCount = 0u;
    std::size_t failedMaskCount = 0u;
    std::error_code maskDirectoryError;
    if (std::filesystem::exists(authoredMaskRoot, maskDirectoryError)) {
        for (std::filesystem::directory_iterator it(authoredMaskRoot, maskDirectoryError), end;
             it != end && !maskDirectoryError;
             it.increment(maskDirectoryError)) {
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || entryError)
                continue;
            if (Lower(it->path().extension().string()) != ".dds")
                continue;

            const std::string maskKey = CanonicalWindowMaskKey(it->path().filename().string());
            if (maskKey.empty() || maskKey == "black")
                continue;

            DirectX::TexMetadata maskMetadata{};
            DirectX::ScratchImage maskImage;
            const HRESULT loadResult = DirectX::LoadFromDDSFile(
                it->path().c_str(),
                DirectX::DDS_FLAGS_NONE,
                &maskMetadata,
                maskImage);
            if (FAILED(loadResult) || maskMetadata.width < 8u || maskMetadata.height < 8u) {
                ++failedMaskCount;
                logger::warn(
                    "[WindowLife] Authored pane mask '{}' could not be loaded or is too small (HRESULT 0x{:08X}).",
                    it->path().filename().string(),
                    static_cast<std::uint32_t>(loadResult));
                continue;
            }

            winrt::com_ptr<ID3D11ShaderResourceView> maskSRV;
            const HRESULT srvResult = DirectX::CreateShaderResourceView(
                globals::d3d::device,
                maskImage.GetImages(),
                maskImage.GetImageCount(),
                maskMetadata,
                maskSRV.put());
            if (FAILED(srvResult)) {
                ++failedMaskCount;
                logger::warn(
                    "[WindowLife] Authored pane mask '{}' SRV creation failed (HRESULT 0x{:08X}).",
                    it->path().filename().string(),
                    static_cast<std::uint32_t>(srvResult));
                continue;
            }

            authoredMaskSRVs.insert_or_assign(maskKey, std::move(maskSRV));
            ++loadedMaskCount;
        }
    }
    if (maskDirectoryError) {
        logger::warn(
            "[WindowLife] Authored pane-mask directory scan failed (error {}).",
            maskDirectoryError.value());
    }
    logger::info(
        "[WindowLife] Loaded {} optional authored pane-mask atlases from Data\\Textures\\masks ({} failed).",
        loadedMaskCount,
        failedMaskCount);
    classificationCache.clear();

    roomAtlasSRV = nullptr;
    const std::filesystem::path roomAtlasPath = "Data\\Shaders\\WindowLife\\RoomAtlas.png";
    DirectX::TexMetadata roomMetadata{};
    DirectX::ScratchImage roomImage;
    DirectX::ScratchImage roomMipChain;
    const HRESULT roomLoadResult = DirectX::LoadFromWICFile(
        roomAtlasPath.c_str(),
        DirectX::WIC_FLAGS_FORCE_SRGB,
        &roomMetadata,
        roomImage);
    if (SUCCEEDED(roomLoadResult) && roomMetadata.width == 2048u && roomMetadata.height == 2048u) {
        const HRESULT roomMipResult = DirectX::GenerateMipMaps(
            roomImage.GetImages(),
            roomImage.GetImageCount(),
            roomMetadata,
            static_cast<DirectX::TEX_FILTER_FLAGS>(
                DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_SEPARATE_ALPHA),
            0u,
            roomMipChain);

        const bool hasGeneratedMips = SUCCEEDED(roomMipResult) && roomMipChain.GetImageCount() > 1u;
        const DirectX::Image* uploadImages = hasGeneratedMips
            ? roomMipChain.GetImages()
            : roomImage.GetImages();
        const std::size_t uploadImageCount = hasGeneratedMips
            ? roomMipChain.GetImageCount()
            : roomImage.GetImageCount();
        const DirectX::TexMetadata& uploadMetadata = hasGeneratedMips
            ? roomMipChain.GetMetadata()
            : roomMetadata;

        if (!hasGeneratedMips) {
            logger::warn(
                "[WindowLife] Authored room atlas mip generation failed (HRESULT 0x{:08X}); base-level fallback remains available.",
                static_cast<std::uint32_t>(roomMipResult));
        }

        const HRESULT roomSRVResult = DirectX::CreateShaderResourceView(
            globals::d3d::device,
            uploadImages,
            uploadImageCount,
            uploadMetadata,
            roomAtlasSRV.put());
        if (FAILED(roomSRVResult)) {
            logger::warn(
                "[WindowLife] Authored room atlas SRV creation failed (HRESULT 0x{:08X}); procedural fallback remains active.",
                static_cast<std::uint32_t>(roomSRVResult));
        } else {
            logger::info(
                "[WindowLife] Authored room atlas uploaded with {} mip levels for stable street-distance interiors.",
                uploadMetadata.mipLevels);
        }
    } else if (FAILED(roomLoadResult)) {
        logger::warn(
            "[WindowLife] Authored room atlas '{}' could not be loaded (HRESULT 0x{:08X}); procedural fallback remains active.",
            roomAtlasPath.string(),
            static_cast<std::uint32_t>(roomLoadResult));
    } else {
        logger::warn(
            "[WindowLife] Authored room atlas must be 2048x2048 (found {}x{}); procedural fallback remains active.",
            roomMetadata.width,
            roomMetadata.height);
    }

    // The near curtain and mid-depth occupant layers are independent atlases.
    // Missing assets are intentionally harmless: an unbound SRV samples zero and
    // the shader retains its analytic curtain/person fallback.
    const auto loadLayerAtlas = [&](const std::filesystem::path& path,
                                    std::string_view label,
                                    winrt::com_ptr<ID3D11ShaderResourceView>& target) {
        target = nullptr;
        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image;
        DirectX::ScratchImage mipChain;
        const HRESULT loadResult = DirectX::LoadFromWICFile(
            path.c_str(),
            DirectX::WIC_FLAGS_FORCE_SRGB,
            &metadata,
            image);
        if (FAILED(loadResult) || metadata.width != 2048u || metadata.height != 2048u) {
            logger::warn(
                "[WindowLife] {} atlas '{}' unavailable or not 2048x2048 (HRESULT 0x{:08X}, {}x{}); analytic fallback remains active.",
                label,
                path.string(),
                static_cast<std::uint32_t>(loadResult),
                metadata.width,
                metadata.height);
            return;
        }

        const HRESULT mipResult = DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            static_cast<DirectX::TEX_FILTER_FLAGS>(
                DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_SEPARATE_ALPHA),
            0u,
            mipChain);
        const bool hasMips = SUCCEEDED(mipResult) && mipChain.GetImageCount() > 1u;
        const DirectX::Image* uploadImages = hasMips ? mipChain.GetImages() : image.GetImages();
        const std::size_t uploadCount = hasMips ? mipChain.GetImageCount() : image.GetImageCount();
        const DirectX::TexMetadata& uploadMetadata = hasMips ? mipChain.GetMetadata() : metadata;
        const HRESULT srvResult = DirectX::CreateShaderResourceView(
            globals::d3d::device,
            uploadImages,
            uploadCount,
            uploadMetadata,
            target.put());
        if (FAILED(srvResult)) {
            logger::warn(
                "[WindowLife] {} atlas SRV creation failed (HRESULT 0x{:08X}); analytic fallback remains active.",
                label,
                static_cast<std::uint32_t>(srvResult));
            target = nullptr;
            return;
        }
        logger::info(
            "[WindowLife] {} atlas uploaded with {} mip levels.",
            label,
            uploadMetadata.mipLevels);
    };

    loadLayerAtlas(
        "Data\\Shaders\\WindowLife\\OccupantAtlas.png",
        "Occupant",
        occupantAtlasSRV);
    loadLayerAtlas(
        "Data\\Shaders\\WindowLife\\CurtainAtlas.png",
        "Curtain",
        curtainAtlasSRV);

    logger::info(
        "[WindowLife] Layered-window GPU resources ready (PS t{} occupants={}, t{} curtains={}, t{} optional pane mask, t{} room atlas={}, t{} structured SRV, 176-byte per-draw payload; FeatureData b6 unchanged).",
        kOccupantAtlasSRVSlot,
        occupantAtlasSRV ? "ready" : "analytic",
        kCurtainAtlasSRVSlot,
        curtainAtlasSRV ? "ready" : "analytic",
        kAuthoredMaskSRVSlot,
        kRoomAtlasSRVSlot,
        roomAtlasSRV ? "ready" : "fallback",
        kPerDrawSRVSlot);
}

float WindowLife::GetDayNightBlend(float hour)
{
    hour = std::fmod(std::max(hour, 0.0f), 24.0f);
    if (hour < 5.0f)
        return 1.0f;
    if (hour < 8.0f)
        return 1.0f - SmoothRange(5.0f, 8.0f, hour);
    if (hour < 17.0f)
        return 0.0f;
    if (hour < 20.0f)
        return SmoothRange(17.0f, 20.0f, hour);
    return 1.0f;
}

float WindowLife::GetActivityForHour(float hour, const Settings& s)
{
    hour = std::fmod(std::max(hour, 0.0f), 24.0f);
    if (hour < 5.0f)
        return s.LateNightActivity;
    if (hour < 7.0f) {
        const float t = SmoothRange(5.0f, 7.0f, hour);
        return std::lerp(s.LateNightActivity, s.DayActivity, t);
    }
    if (hour < 17.0f)
        return s.DayActivity;
    if (hour < 20.0f) {
        const float t = SmoothRange(17.0f, 20.0f, hour);
        return std::lerp(s.DayActivity, s.EveningActivity, t);
    }
    if (hour < 23.0f)
        return s.EveningActivity;
    const float t = SmoothRange(23.0f, 24.0f, hour);
    return std::lerp(s.EveningActivity, s.LateNightActivity, t);
}

void WindowLife::RefreshFrameBaseData()
{
    if (!activeBuffer)
        return;

    const std::uint32_t frame = globals::state ? globals::state->frameCount : 0u;
    if (activeDataFrame == frame)
        return;

    float hour = 12.0f;
    auto* calendar = globals::game::calendar;
    if (!calendar)
        calendar = RE::Calendar::GetSingleton();
    if (calendar)
        hour = calendar->GetHour();

    const float night = GetDayNightBlend(hour);
    const float shadowStrength = std::lerp(
        std::clamp(settings.DayShadowStrength, 0.0f, 1.0f),
        std::clamp(settings.NightShadowStrength, 0.0f, 1.0f),
        night);
    const float activity = std::clamp(GetActivityForHour(hour, settings), 0.0f, 1.0f);

    frameBaseData = {};
    frameBaseData.Runtime0 = {
        settings.EnableWindowLife ? 1.0f : 0.0f,
        0.6180339887f,
        shadowStrength,
        activity
    };
    frameBaseData.Optics0 = {
        std::clamp(settings.ParallaxDepth, 0.0f, 96.0f),
        std::clamp(settings.SilhouetteSoftness, 0.005f, 0.25f),
        std::clamp(settings.Refraction, 0.0f, 12.0f),
        std::clamp(settings.HumanScale, 0.5f, 1.6f)
    };
    frameBaseData.Surface0 = {
        std::max(settings.DistanceFadeStart, 0.0f),
        std::max(settings.DistanceFadeEnd, settings.DistanceFadeStart + 1.0f),
        std::clamp(settings.PaneThreshold, 0.0f, 1.0f),
        std::clamp(settings.PaneSoftness, 0.01f, 1.0f)
    };
    frameBaseData.Runtime1 = {
        settings.DebugWindowDetection ? 1.0f : 0.0f,
        std::clamp(settings.RoomWidth, 48.0f, 320.0f),
        std::clamp(settings.RoomHeight, 72.0f, 360.0f),
        std::clamp(settings.MotionSpeed, 0.1f, 4.0f)
    };
    frameBaseData.Glass0 = {
        std::clamp(settings.GlassStrength, 0.0f, 1.0f),
        std::clamp(settings.GlassReflectionBoost, 0.0f, 2.0f),
        std::clamp(settings.GlassRoughness, 0.04f, 1.0f),
        std::clamp(settings.GlassTransmission, 0.5f, 1.0f)
    };
    frameBaseData.Glass1 = {
        std::clamp(settings.GlassDirtStrength, 0.0f, 1.0f),
        std::clamp(settings.GlassDistortion, 0.0f, 0.15f),
        std::clamp(settings.GlassNormalRetention, 0.0f, 1.0f),
        settings.SuppressWindowAutoPOM ? 1.0f : 0.0f
    };
    frameBaseData.Eligibility0 = {
        std::max(settings.MinShallowWindowRadius, 0.0f),
        std::max(settings.MinFullWindowRadius, settings.MinShallowWindowRadius + 1.0f),
        std::clamp(settings.FullWindowVerticality, 0.0f, 1.0f),
        settings.EnableArchitecturalGlass ? 1.0f : 0.0f
    };
    frameBaseData.Interior0 = {
        std::clamp(settings.CurtainStrength, 0.0f, 0.75f),
        std::clamp(settings.RoomDepthStrength, 0.0f, 0.60f),
        settings.UseAuthoredMaskLayout ? 1.0f : 0.0f,
        settings.EnableInteriorPassers ? 1.0f : 0.0f
    };
    frameBaseData.Asset0 = {
        0.0f,
        settings.EnableAuthoredRooms && roomAtlasSRV ? 1.0f : 0.0f,
        std::clamp(settings.AuthoredRoomStrength, 0.0f, 1.0f),
        16.0f
    };

    activeDataFrame = frame;
}

void WindowLife::UploadData(ID3D11Buffer* buffer, const PerGeometryData& data) const
{
    if (!buffer || !globals::d3d::context)
        return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    DX::ThrowIfFailed(globals::d3d::context->Map(buffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));
    std::memcpy(mapped.pData, &data, sizeof(data));
    globals::d3d::context->Unmap(buffer, 0u);
}

void WindowLife::BindNeutral() const
{
    if (!neutralSRV || !globals::d3d::context)
        return;
    ID3D11ShaderResourceView* srvs[5] = {
        occupantAtlasSRV.get(),
        curtainAtlasSRV.get(),
        nullptr,
        settings.EnableAuthoredRooms ? roomAtlasSRV.get() : nullptr,
        neutralSRV.get()
    };
    globals::d3d::context->PSSetShaderResources(kOccupantAtlasSRVSlot, 5, srvs);
}

ID3D11ShaderResourceView* WindowLife::GetAuthoredMaskSRV(const Classification& classification) const
{
    if (!classification.hasAuthoredMask || classification.authoredMaskKey.empty())
        return nullptr;
    const auto it = authoredMaskSRVs.find(classification.authoredMaskKey);
    return it != authoredMaskSRVs.end() ? it->second.get() : nullptr;
}

void WindowLife::BindActive(const Classification& classification) const
{
    if (!activeSRV || !globals::d3d::context)
        return;
    ID3D11ShaderResourceView* srvs[5] = {
        occupantAtlasSRV.get(),
        curtainAtlasSRV.get(),
        GetAuthoredMaskSRV(classification),
        settings.EnableAuthoredRooms ? roomAtlasSRV.get() : nullptr,
        activeSRV.get()
    };
    globals::d3d::context->PSSetShaderResources(kOccupantAtlasSRVSlot, 5, srvs);
}

WindowLife::Classification WindowLife::ClassifyMaterial(const RE::BSLightingShaderMaterialBase* material)
{
    Classification result{};
    if (!material)
        return result;

    auto* textures = material->textureSet.get();
    if (!textures)
        return result;

    std::string allPaths;
    std::string diffusePath;
    std::string glowPath;

    const auto appendPath = [&](RE::BSTextureSet::Texture slot, std::string* capture = nullptr) {
        const char* raw = textures->GetTexturePath(slot);
        if (!raw || !*raw)
            return;
        std::string p = Lower(raw);
        if (capture)
            *capture = p;
        if (!allPaths.empty())
            allPaths.push_back('|');
        allPaths += p;
    };

    appendPath(RE::BSTextureSet::Texture::kDiffuse, &diffusePath);
    appendPath(RE::BSTextureSet::Texture::kGlowMap, &glowPath);
    appendPath(RE::BSTextureSet::Texture::kNormal);
    appendPath(RE::BSTextureSet::Texture::kEnvironmentMask);
    appendPath(RE::BSTextureSet::Texture::kEnvironment);

    if (allPaths.empty())
        return result;

    // The diffuse material identifies the surface being drawn. A glow/normal slot
    // belonging to another atlas is not enough to turn roofs, stucco, doors,
    // shutters, trim, floors or whole facades into windows.
    const bool strongWindow = ContainsAny(diffusePath, {
        "window", "windows", "windowpane", "window_pane", "glasspane", "glass_pane", "glazing"
    });
    const bool glass = ContainsAny(allPaths, { "glass", "stainedglass", "stained_glass" });
    const bool architecture = ContainsAny(allPaths, {
        "architecture", "architectural", "farmhouse", "whiterun", "solitude", "windhelm", "riften", "markarth",
        "winterhold", "college", "castle", "house", "houses", "inn", "shop", "temple", "fort",
        "building", "buildings", "village", "town", "city", "exterior", "facade", "dwemer", "nordic"
    });
    const bool obviousNonBuildingGlass = ContainsAny(allPaths, {
        "bottle", "potion", "alchemy", "clutter", "weapon", "armor", "shield", "crystal", "gem",
        "ice", "eye", "water", "magic", "spell", "effect", "decal", "lantern", "chandelier", "candle", "torch"
    });
    // Window-shadow/interior mods commonly add separate proxy geometry whose
    // diffuse texture is a binary pane mask. The proxy filename still contains
    // "window", but applying WindowLife to that draw exposes the helper mesh in
    // front of or through the building. It is not the authored glass surface.
    const bool windowProxyMask = ContainsAny(diffusePath, {
        "\\masks\\", "/masks/", "_mask.dds", "windowshadow", "window_shadow"
    });
    // Closed/boarded shutters are deliberately opaque architectural surfaces.
    // Their filenames often still contain "window", which previously promoted
    // them into a bright inhabited pane at night.
    const bool closedWindowSurface = ContainsAny(diffusePath, {
        "shutter", "closedwindow", "closed_window", "windowclosed",
        "window_closed", "boarded", "windowboard", "window_board"
    });
    const bool hasGlowTexture = !glowPath.empty();
    const std::string authoredMaskKey = CanonicalWindowMaskKey(diffusePath);
    const bool hasAuthoredMask = !authoredMaskKey.empty() && authoredMaskSRVs.contains(authoredMaskKey);
    const bool mappedAuthoredWindow = architecture && hasAuthoredMask && !obviousNonBuildingGlass;

    if (strongWindow)
        result.score += 8;
    if (glass)
        result.score += 2;
    if (architecture)
        result.score += 3;
    if (hasGlowTexture)
        result.score += 2;
    if (architecture && hasGlowTexture)
        result.score += 3;
    if (hasAuthoredMask)
        result.score += 10;
    if (obviousNonBuildingGlass && !strongWindow)
        result.score -= 8;
    if (windowProxyMask || closedWindowSurface)
        result.score -= 16;

    const bool architecturalGlass = architecture && glass && !obviousNonBuildingGlass;
    // Architecture is context, never proof. Treating the broad architecture path
    // as sufficient classified doors, roofs, gravestones and entire facade draws
    // as windows. Require an explicit window/glass token or a dedicated authored
    // pane mask whose basename matches the diffuse material being drawn.
    result.isWindow = !windowProxyMask && !closedWindowSurface &&
        (strongWindow || architecturalGlass || mappedAuthoredWindow);
    result.hasGlowTexture = hasGlowTexture;
    result.hasAuthoredMask = hasAuthoredMask;
    result.authoredMaskKey = hasAuthoredMask ? authoredMaskKey : std::string{};
    result.explicitWindow = strongWindow || mappedAuthoredWindow;
    result.namedGlass = glass;
    if (ContainsAny(allPaths, { "markarth", "dwemer" }))
        result.roomFamily = 4;
    else if (ContainsAny(allPaths, { "solitude", "castle", "imperial", "palace", "noble" }))
        result.roomFamily = 1;
    else if (ContainsAny(allPaths, { "riften", "canal" }))
        result.roomFamily = 2;
    else if (ContainsAny(allPaths, { "windhelm", "snowquarter" }))
        result.roomFamily = 3;
    else if (ContainsAny(allPaths, { "inn", "shop", "tavern", "merchant", "alchemy" }))
        result.roomFamily = 5;

    // Material tier is only a ceiling. Geometry radius and surface orientation can
    // downgrade it later; they can never promote decorative/non-window materials.
    if (strongWindow || mappedAuthoredWindow)
        result.materialTier = 3;
    else if (architecturalGlass)
        result.materialTier = 2;

    result.evidence = !diffusePath.empty() ? diffusePath : (!glowPath.empty() ? glowPath : allPaths);

    if (settings.DebugWindowDetection && (result.isWindow || architecture || glass || hasGlowTexture)) {
        logger::info(
            "[WindowLife] CLASSIFY {} tier={} score={} hash={:08X} arch={} glass={} glow={} mask={} explicit={} proxy={} closed={} texture='{}' maskKey='{}'",
            result.isWindow ? "WINDOW" : "skip",
            result.materialTier,
            result.score,
            material->hashKey,
            architecture ? 1 : 0,
            glass ? 1 : 0,
            hasGlowTexture ? 1 : 0,
            hasAuthoredMask ? 1 : 0,
            result.explicitWindow ? 1 : 0,
            windowProxyMask ? 1 : 0,
            closedWindowSurface ? 1 : 0,
            result.evidence,
            result.authoredMaskKey);
    }

    return result;
}

const WindowLife::Classification& WindowLife::GetClassification(const RE::BSLightingShaderMaterialBase* material)
{
    static const Classification kNone{};
    if (!material)
        return kNone;

    const std::uintptr_t key = material->hashKey != 0
        ? static_cast<std::uintptr_t>(material->hashKey)
        : reinterpret_cast<std::uintptr_t>(material);

    auto it = classificationCache.find(key);
    if (it != classificationCache.end())
        return it->second;

    auto emplaceResult = classificationCache.emplace(key, ClassifyMaterial(material));
    return emplaceResult.first->second;
}

void WindowLife::UpdateAndBindActive(const Classification& classification, const RE::BSGeometry* geometry)
{
    RefreshFrameBaseData();

    PerGeometryData data = frameBaseData;
    const float geometryRadius = geometry ? std::max(geometry->worldBound.radius, 0.0f) : 0.0f;

    int tier = std::clamp(classification.materialTier, 1, 3);
    if (geometryRadius > 0.0f) {
        if (geometryRadius < settings.MinShallowWindowRadius)
            tier = std::min(tier, 1);
        else if (geometryRadius < settings.MinFullWindowRadius)
            tier = std::min(tier, 2);
    }

    data.Class0 = {
        static_cast<float>(tier),
        classification.namedGlass ? 1.0f : 0.0f,
        (classification.hasGlowTexture ? 1.0f : 0.0f) +
            (classification.hasAuthoredMask ? 2.0f : 0.0f),
        classification.explicitWindow ? 1.0f : 0.0f
    };
    data.Asset0.x = static_cast<float>(classification.roomFamily);
    if (geometry) {
        const auto& center = geometry->worldBound.center;
        data.Geometry0 = { center.x, center.y, center.z, geometryRadius };
    } else {
        data.Geometry0 = { 0.0f, 0.0f, 0.0f, geometryRadius };
    }

    if (!activeDataValid || std::memcmp(&data, &currentActiveData, sizeof(data)) != 0) {
        UploadData(activeBuffer.get(), data);
        currentActiveData = data;
        activeDataValid = true;
    }

    BindActive(classification);
}

void WindowLife::BSLightingShader_SetupGeometry(RE::BSRenderPass* pass)
{
    // The private structured SRV is rebound for EVERY Lighting draw so candidate state cannot leak.
    if (!pass || !pass->shaderProperty || !pass->shaderProperty->material || !settings.EnableWindowLife) {
        BindNeutral();
        return;
    }

    auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(pass->shaderProperty->material);
    const auto& classification = GetClassification(material);
    if (!classification.isWindow) {
        BindNeutral();
        return;
    }

    UpdateAndBindActive(classification, pass->geometry);
}

void WindowLife::Hooks::BSLightingShader_SetupGeometry::thunk(
    RE::BSShader* a_this,
    RE::BSRenderPass* a_pass,
    std::uint32_t a_renderFlags)
{
    // Preserve all previously-installed per-draw hooks, then make WindowLife
    // structured per-draw SRV the final authoritative bind for this draw.
    func(a_this, a_pass, a_renderFlags);

    auto& windowLife = globals::pipeline::windowLife;
    if (windowLife.loaded)
        windowLife.BSLightingShader_SetupGeometry(a_pass);
}

void WindowLife::PostPostLoad()
{
    logger::info("[WindowLife] Layered architectural-glass + inhabited-window renderer ACTIVE");
    Hooks::Install();
}
