// PIXL Renderer - WindowLife runtime implementation.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "WindowLife.h"
#include "AtmosphereWeather.h"

#include "Globals.h"
#include "State.h"
#include "WeatherManager.h"
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
    InteriorParallaxDepth,
    EnableOutdoorViews,
    OutdoorViewStrength,
    OutdoorViewEmission,
    Refraction,
    SilhouetteSoftness,
    HumanScale,
    OccupantOpacity,
    CurtainStrength,
    RoomDepthStrength,
    EnableAuthoredRooms,
    AuthoredRoomStrength,
	InteriorContrast,
	InteriorEmission,
    InteriorScale,
    InteriorSoftness,
    RoomVolumeStrength,
    WindowRecess,
    AutomaticRoomSizing,
    UseExactGlassMasks,
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
    EnvironmentReflectionStrength,
    InteriorLightingResponse,
    WeatherGlassResponse,
    SunGlintStrength,
    DirectionalRevealStrength,
    RoomVariationStrength,
    CloseLayerFeather,
    SuppressWindowAutoPOM,
    MinShallowWindowRadius,
    MinFullWindowRadius,
    FullWindowVerticality,
    DebugWindowDetection,
    DebugRoomProjection);

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

    std::uint32_t StableHash32(std::string_view text, std::uint32_t seed = 2166136261u)
    {
        std::uint32_t hash = seed;
        for (const unsigned char value : text) {
            hash ^= value;
            hash *= 16777619u;
        }
        return hash;
    }

    void HashCombine32(std::uint32_t& seed, std::uint32_t value)
    {
        seed ^= value + 0x9E3779B9u + (seed << 6u) + (seed >> 2u);
        seed *= 16777619u;
    }
}

void WindowLife::DrawSettings()
{
	ImGui::TextWrapped("WindowLife includes its room artwork; no separate parallax-window mod is required. It applies to recognized architectural glass. Replacement materials with unrecognized texture names may need compatibility work; boarded windows and shadow-mask helper meshes are excluded.");
	ImGui::Checkbox(T("feature.window_life.enable", "Enable Window Life"), &settings.EnableWindowLife);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped("%s", T("feature.window_life.enable_tooltip", "Upgrades architectural glass and adds subtle moving occupants behind suitable exterior and interior windows."));
	}

	ImGui::Checkbox(T("feature.window_life.glass_enable", "Architectural Glass Optics"), &settings.EnableArchitecturalGlass);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped("%s", T("feature.window_life.glass_enable_tooltip", "Applies old-glass Fresnel response, restrained waviness and stable grime/roughness variation to detected architectural panes."));
	}

	if (ImGui::CollapsingHeader(T("feature.window_life.glass", "Architectural Glass"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BeginDisabled(!settings.EnableArchitecturalGlass);
		ImGui::SliderFloat(T("feature.window_life.glass_strength", "Glass Strength"), &settings.GlassStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.glass_reflection", "Reflection Response"), &settings.GlassReflectionBoost, 0.0f, 1.5f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.glass_roughness", "Base Glass Roughness"), &settings.GlassRoughness, 0.06f, 0.80f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.glass_transmission", "Glass Transmission"), &settings.GlassTransmission, 0.65f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.glass_dirt", "Grime Variation"), &settings.GlassDirtStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.glass_distortion", "Old Glass Waviness"), &settings.GlassDistortion, 0.0f, 0.09f, "%.3f");
		ImGui::SliderFloat(T("feature.window_life.glass_normal", "Texture Normal Retention"), &settings.GlassNormalRetention, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.environment_reflection", "Environment Reflection"), &settings.EnvironmentReflectionStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.weather_glass", "Weathered Glass Response"), &settings.WeatherGlassResponse, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Controls mip-filtered grime, rain streaks and cold-weather haze. The response uses live PIXL precipitation state and remains attached to the glass in world space.");
		}
		ImGui::EndDisabled();
		ImGui::Checkbox(T("feature.window_life.suppress_autopom", "Keep Auto-POM Off Glass Panes"), &settings.SuppressWindowAutoPOM);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T("feature.window_life.suppress_autopom_tooltip", "Suppresses synthetic Object Auto-POM only on detected pane pixels. Window frames and surrounding architecture keep their normal material depth."));
		}
	}

	if (ImGui::CollapsingHeader(T("feature.window_life.visibility", "Occupancy"))) {
		ImGui::SliderFloat(T("feature.window_life.occupant_opacity", "Occupant Opacity"), &settings.OccupantOpacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Controls the true opacity of the softly filtered authored people. It no longer makes them into translucent black ghosts.");
		}
		ImGui::SliderFloat(T("feature.window_life.day_activity", "Day Activity"), &settings.DayActivity, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.evening_activity", "Evening Activity"), &settings.EveningActivity, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.late_activity", "Late Night Activity"), &settings.LateNightActivity, 0.0f, 1.0f, "%.2f");
	}

	if (ImGui::CollapsingHeader(T("feature.window_life.eligibility", "Window Eligibility"))) {
		ImGui::SliderFloat(T("feature.window_life.min_shallow_radius", "Minimum Glass-Only Window Size"), &settings.MinShallowWindowRadius, 4.0f, 96.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.min_full_radius", "Minimum Occupied Window Size"), &settings.MinFullWindowRadius, 12.0f, 160.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		settings.MinFullWindowRadius = std::max(settings.MinFullWindowRadius, settings.MinShallowWindowRadius + 1.0f);
		ImGui::SliderFloat(T("feature.window_life.full_verticality", "Full Occupancy Verticality"), &settings.FullWindowVerticality, 0.25f, 0.95f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("%s", T("feature.window_life.eligibility_tooltip", "Tiny dedicated meshes become glass-only. Sloped roof/awning panes are downgraded in the shader even when their parent geometry is large."));
		}
	}

	if (ImGui::CollapsingHeader(T("feature.window_life.optics", "Interior Depth"))) {
		ImGui::SliderFloat("Exterior View: Room Depth", &settings.ParallaxDepth, 0.0f, 216.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Checkbox("Interior View: Outdoor Backgrounds", &settings.EnableOutdoorViews);
		ImGui::SliderFloat("Interior View: Parallax Depth", &settings.InteriorParallaxDepth, 0.0f, 240.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Depth of the outdoor scene and passers seen from inside. Higher values reveal more movement behind the glass as you move; shader cost is unchanged.");
		ImGui::SliderFloat("Outdoor Background Visibility", &settings.OutdoorViewStrength, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Blends authored northern landscapes behind interior windows. Requires OutdoorAtlas; missing artwork retains the original glass and passers.");
		ImGui::SliderFloat("Interior View: Background Emission", &settings.OutdoorViewEmission, 0.0f, 8.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Brightens the day/night landscape seen from inside through refracted glass. Does not change exterior room images, opacity or parallax. No extra texture samples.");
		ImGui::SliderFloat(T("feature.window_life.refraction", "Interior Refraction"), &settings.Refraction, 0.0f, 8.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.softness", "Silhouette Softness"), &settings.SilhouetteSoftness, 0.015f, 0.16f, "%.3f");
		Util::AddTooltip("Softens authored people behind the pane by selecting a filtered atlas mip and widening the alpha edge. The frame and glass stay sharp; there are no extra texture samples.");
		ImGui::SliderFloat(T("feature.window_life.human_scale", "Human Scale"), &settings.HumanScale, 0.65f, 1.35f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.curtain_strength", "Curtain Opacity"), &settings.CurtainStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.room_depth_strength", "Recessed Room Depth"), &settings.RoomDepthStrength, 0.0f, 0.40f, "%.2f");
		ImGui::Checkbox(T("feature.window_life.authored_rooms", "Authored Room Backgrounds"), &settings.EnableAuthoredRooms);
		ImGui::SliderFloat(T("feature.window_life.authored_room_strength", "Authored Room Visibility"), &settings.AuthoredRoomStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.interior_contrast", "Interior Contrast"), &settings.InteriorContrast, 0.50f, 2.0f, "%.2f");
		ImGui::SliderFloat("Exterior View: Room Emission", &settings.InteriorEmission, 0.0f, 6.0f, "%.2fx");
		Util::AddTooltip("Brightens the authored room behind exterior glass while preserving its furniture and light contrast. Higher values increase HDR radiance without adding a render pass.");
		ImGui::SliderFloat(T("feature.window_life.interior_scale", "Interior Scale"), &settings.InteriorScale, 1.0f, 2.50f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Authored Room Softness", &settings.InteriorSoftness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (auto _tt = Util::HoverTooltipWrapper()) {
            ImGui::TextWrapped("Adds a controlled optical softness to the authored room artwork behind the glass. Panes, mullions and the glass surface stay sharp.");
        }
		ImGui::SliderFloat("Room Volume", &settings.RoomVolumeStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Projects the authored room onto a bounded rear wall, side walls, floor and ceiling as the view changes. Zero keeps the original flat mapping. No extra texture sample.");
		ImGui::SliderFloat("Window Recess", &settings.WindowRecess, 0.0f, 18.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		Util::AddTooltip("Places the room behind the glass and wall opening. Higher values reveal more depth at oblique angles.");
		ImGui::SliderFloat(T("feature.window_life.interior_lighting_response", "Interior Lighting Response"), &settings.InteriorLightingResponse, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Interior Scale crops or expands the room artwork in both manual and automatic sizing modes without changing the detected glass boundary or room identity. Contrast separates furniture and walls; emission controls readability through the original glass.");
		}
		ImGui::Checkbox(T("feature.window_life.auto_room_sizing", "Automatic Room Sizing"), &settings.AutomaticRoomSizing);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Optional texture-based aperture fitting. Manual sizing is the default. Automatic mode accepts only confident, plausibly sized fits; texture atlases can still need manual Room Width/Height. Rooms, curtains and occupants share the fit.");
		}
		ImGui::Checkbox(T("feature.window_life.interior_passers", "Interior View Passers-by"), &settings.EnableInteriorPassers);
	}

	if (ImGui::CollapsingHeader(T("feature.window_life.masking", "Pane Mask & Distance"))) {
		if (ImGui::Checkbox(T("feature.window_life.exact_glass_masks", "Use Installed Glass Masks"), &settings.UseExactGlassMasks)) {
			classificationCache.clear();
			activeDataValid = false;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Uses an optional diffuse-matched mask only to keep WindowLife inside real glass. It never controls room UVs, size, occupants or parallax, and safely falls back to procedural detection for replacement textures without a matching mask.");
		}
		ImGui::SliderFloat(T("feature.window_life.pane_threshold", "Pane Threshold"), &settings.PaneThreshold, 0.0f, 0.55f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.pane_softness", "Pane Mask Softness"), &settings.PaneSoftness, 0.03f, 0.50f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.fade_start", "Distance Fade Start"), &settings.DistanceFadeStart, 256.0f, 10000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.fade_end", "Distance Fade End"), &settings.DistanceFadeEnd, 512.0f, 16000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		settings.DistanceFadeEnd = std::max(settings.DistanceFadeEnd, settings.DistanceFadeStart + 1.0f);
	}

	if (globals::state && globals::state->IsDeveloperMode() && ImGui::CollapsingHeader(T("feature.window_life.developer", "Developer"))) {
		ImGui::Spacing();
		ImGui::BeginDisabled(settings.AutomaticRoomSizing);
		ImGui::SliderFloat(T("feature.window_life.room_width", "Manual Room Width"), &settings.RoomWidth, 64.0f, 220.0f, "%.0f");
		ImGui::SliderFloat(T("feature.window_life.room_height", "Manual Room Height"), &settings.RoomHeight, 96.0f, 260.0f, "%.0f");
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("These controls apply only when Automatic Window Room Sizing is disabled. Automatic mode owns its stable calibration and cannot silently inherit stale manual values.");
		}
		ImGui::SliderFloat(T("feature.window_life.day_shadow", "Fallback Day Silhouette Darkness"), &settings.DayShadowStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.night_shadow", "Fallback Night Silhouette Darkness"), &settings.NightShadowStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Used only if the authored occupant atlas is unavailable. Normal installations use Occupant Opacity above.");
		}
		ImGui::SliderFloat(T("feature.window_life.motion_speed", "Activity Speed"), &settings.MotionSpeed, 0.25f, 2.5f, "%.2f");
		ImGui::SliderFloat(T("feature.window_life.sun_glint", "Sun Glint Strength"), &settings.SunGlintStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.directional_reveal", "Directional Reveal Strength"), &settings.DirectionalRevealStrength, 0.0f, 0.60f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.room_variation", "Neighbouring Room Variation"), &settings.RoomVariationStrength, 0.0f, 0.35f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat(T("feature.window_life.layer_feather", "Close Cutout Feather"), &settings.CloseLayerFeather, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Checkbox(T("feature.window_life.debug_detection", "Show Window Class Overlay"), &settings.DebugWindowDetection);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Blue/amber show glass/shallow tiers. Red means native layout rejected, cyan means native background only, and green means a full occupant-safe native layout. Procedural fallback remains independent of optional exact pane masks.");
		}
		if (settings.DebugWindowDetection) {
			const char* projectionViews[] = {
				"Class Overlay", "Base Room Coordinates", "Final Room Coordinates",
				"Framed Room UV", "Room Identity", "Room Basis", "Pane Mask",
				"Room Box Surface", "Room Box Depth", "Glow Guide Signal"
			};
			settings.DebugRoomProjection = std::clamp(settings.DebugRoomProjection, 0, 9);
			ImGui::Combo("Room Projection Debug", &settings.DebugRoomProjection,
				projectionViews, IM_ARRAYSIZE(projectionViews));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped("Developer diagnostic only. Use this to verify that every pane in a physical window shares the same room coordinates, identity and horizontal basis.");
			}
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

    // A renderer/device recreation must never retain views from the old D3D11
    // device. A failed recreation leaves WindowLife neutral rather than binding
    // a stale active SRV on a later Lighting draw.
    activeSRV = nullptr;
    neutralSRV = nullptr;
    activeBuffer = nullptr;
    neutralBuffer = nullptr;
    glassGrimeSRV = nullptr;
    occupantAtlasSRV = nullptr;
    curtainAtlasSRV = nullptr;
    roomAtlasSRV = nullptr;
    outdoorAtlasSRV = nullptr;
    outdoorNightAtlasSRV = nullptr;
    authoredMaskSRVs.clear();
    classificationCache.clear();
    frameBaseData = {};
    currentActiveData = {};
    activeDataValid = false;
    activeDataFrame = ~0u;
    uploadFailureLogged = false;
    invalidGeometryLogged = false;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(PerGeometryData));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = static_cast<UINT>(sizeof(PerGeometryData));

    const HRESULT activeResult = globals::d3d::device->CreateBuffer(&desc, nullptr, activeBuffer.put());
    const HRESULT neutralResult = globals::d3d::device->CreateBuffer(&desc, nullptr, neutralBuffer.put());
    if (FAILED(activeResult) || FAILED(neutralResult)) {
        logger::error("[WindowLife] Per-draw buffer creation failed (active 0x{:08X}, neutral 0x{:08X}); module renders neutral.",
            static_cast<std::uint32_t>(activeResult), static_cast<std::uint32_t>(neutralResult));
        activeBuffer = nullptr;
        neutralBuffer = nullptr;
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 1;
    const HRESULT activeViewResult = globals::d3d::device->CreateShaderResourceView(activeBuffer.get(), &srvDesc, activeSRV.put());
    const HRESULT neutralViewResult = globals::d3d::device->CreateShaderResourceView(neutralBuffer.get(), &srvDesc, neutralSRV.put());
    if (FAILED(activeViewResult) || FAILED(neutralViewResult)) {
        logger::error("[WindowLife] Per-draw SRV creation failed (active 0x{:08X}, neutral 0x{:08X}); module renders neutral.",
            static_cast<std::uint32_t>(activeViewResult), static_cast<std::uint32_t>(neutralViewResult));
        activeSRV = nullptr;
        neutralSRV = nullptr;
        return;
    }

    PerGeometryData neutral{};
    if (!UploadData(neutralBuffer.get(), neutral)) {
        activeSRV = nullptr;
        neutralSRV = nullptr;
        return;
    }
    frameBaseData = {};
    currentActiveData = {};
    activeDataValid = false;
    activeDataFrame = ~0u;

    // Optional Window Shadows-style masks are exact glass stencils only. They are
    // matched to the diffuse texture stem and bound on the real material draw;
    // they never promote a material into a window or control the room layout.
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
                    "[WindowLife] Exact glass mask '{}' could not be loaded or is too small (HRESULT 0x{:08X}).",
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
                    "[WindowLife] Exact glass mask '{}' SRV creation failed (HRESULT 0x{:08X}).",
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
            "[WindowLife] Exact glass-mask directory scan failed (error {}).",
            maskDirectoryError.value());
    }
    logger::info(
        "[WindowLife] Loaded {} optional exact glass masks for final pane clipping ({} failed); procedural room placement remains authoritative.",
        loadedMaskCount,
        failedMaskCount);
    classificationCache.clear();

    // Prefer precompressed DDS atlases with authored mip chains. This avoids WIC
    // decode plus runtime RGBA mip generation and keeps WindowLife's largest
    // optional assets compressed in GPU memory. PNG remains a non-fatal fallback
    // for older packages and for replacement authors who have not converted yet.
    const auto loadAtlas = [&](const std::filesystem::path& ddsPath,
                               const std::filesystem::path& pngFallbackPath,
                               std::string_view label,
                               winrt::com_ptr<ID3D11ShaderResourceView>& target,
                               bool analyticFallback) {
        target = nullptr;

        if (!ddsPath.empty()) {
            DirectX::ScratchImage ddsImage;
            DirectX::TexMetadata ddsMetadata{};
            const HRESULT ddsLoadResult = DirectX::LoadFromDDSFile(
                ddsPath.c_str(),
                DirectX::DDS_FLAGS_NONE,
                &ddsMetadata,
                ddsImage);
            if (SUCCEEDED(ddsLoadResult) &&
                ddsMetadata.width == 2048u &&
                ddsMetadata.height == 2048u &&
                ddsMetadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D &&
                ddsMetadata.arraySize == 1u &&
                ddsMetadata.mipLevels > 1u &&
                DirectX::IsCompressed(ddsMetadata.format)) {
                const DXGI_FORMAT srgbFormat = DirectX::MakeSRGB(ddsMetadata.format);
                if (srgbFormat != DXGI_FORMAT_UNKNOWN && ddsImage.OverrideFormat(srgbFormat)) {
                    ddsMetadata = ddsImage.GetMetadata();
                    const HRESULT srvResult = DirectX::CreateShaderResourceView(
                        globals::d3d::device,
                        ddsImage.GetImages(),
                        ddsImage.GetImageCount(),
                        ddsMetadata,
                        target.put());
                    if (SUCCEEDED(srvResult) && target) {
                        logger::info(
                            "[WindowLife] {} DDS atlas '{}' uploaded as format {} with {} authored mip levels.",
                            label,
                            ddsPath.string(),
                            static_cast<std::uint32_t>(ddsMetadata.format),
                            ddsMetadata.mipLevels);
                        return;
                    }
                    logger::warn(
                        "[WindowLife] {} DDS atlas SRV creation failed (HRESULT 0x{:08X}); trying PNG fallback.",
                        label,
                        static_cast<std::uint32_t>(srvResult));
                    target = nullptr;
                } else {
                    logger::warn(
                        "[WindowLife] {} DDS atlas '{}' could not be exposed as sRGB; trying PNG fallback.",
                        label,
                        ddsPath.string());
                }
            } else if (SUCCEEDED(ddsLoadResult)) {
                logger::warn(
                    "[WindowLife] {} DDS atlas '{}' must be one compressed 2048x2048 2D texture with authored mips (found {}x{}, array {}, mips {}, format {}); trying PNG fallback.",
                    label,
                    ddsPath.string(),
                    ddsMetadata.width,
                    ddsMetadata.height,
                    ddsMetadata.arraySize,
                    ddsMetadata.mipLevels,
                    static_cast<std::uint32_t>(ddsMetadata.format));
            } else {
                logger::info(
                    "[WindowLife] Optional {} DDS atlas '{}' was not loaded (HRESULT 0x{:08X}); trying PNG compatibility fallback.",
                    label,
                    ddsPath.string(),
                    static_cast<std::uint32_t>(ddsLoadResult));
            }
        }

        DirectX::TexMetadata pngMetadata{};
        DirectX::ScratchImage pngImage;
        DirectX::ScratchImage pngMipChain;
        const HRESULT pngLoadResult = DirectX::LoadFromWICFile(
            pngFallbackPath.c_str(),
            DirectX::WIC_FLAGS_FORCE_SRGB,
            &pngMetadata,
            pngImage);
        if (FAILED(pngLoadResult) || pngMetadata.width == 0u || pngMetadata.width != pngMetadata.height) {
            logger::warn(
                "[WindowLife] {} PNG fallback '{}' unavailable or not square (HRESULT 0x{:08X}, {}x{}); {} fallback remains active.",
                label,
                pngFallbackPath.string(),
                static_cast<std::uint32_t>(pngLoadResult),
                pngMetadata.width,
                pngMetadata.height,
                analyticFallback ? "analytic" : "procedural");
            return;
        }

        // The shaders use a 2048 atlas/tile padding contract. Normalize valid
        // square replacement artwork once at load time before generating mips.
        if (pngMetadata.width != 2048u) {
            DirectX::ScratchImage resized;
            const HRESULT resizeResult = DirectX::Resize(
                *pngImage.GetImage(0, 0, 0), 2048u, 2048u,
                static_cast<DirectX::TEX_FILTER_FLAGS>(DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_SEPARATE_ALPHA), resized);
            if (FAILED(resizeResult)) {
                logger::warn("[WindowLife] {} atlas resize failed (HRESULT 0x{:08X}); fallback remains active.",
                    label, static_cast<std::uint32_t>(resizeResult));
                return;
            }
            logger::info("[WindowLife] Normalized {} atlas from {}x{} to 2048x2048", label, pngMetadata.width, pngMetadata.height);
            pngImage = std::move(resized);
            pngMetadata = pngImage.GetMetadata();
        }

        const HRESULT mipResult = DirectX::GenerateMipMaps(
            pngImage.GetImages(),
            pngImage.GetImageCount(),
            pngMetadata,
            static_cast<DirectX::TEX_FILTER_FLAGS>(
                DirectX::TEX_FILTER_CUBIC | DirectX::TEX_FILTER_SEPARATE_ALPHA),
            0u,
            pngMipChain);
        const bool hasMips = SUCCEEDED(mipResult) && pngMipChain.GetImageCount() > 1u;
        const DirectX::Image* uploadImages = hasMips ? pngMipChain.GetImages() : pngImage.GetImages();
        const std::size_t uploadCount = hasMips ? pngMipChain.GetImageCount() : pngImage.GetImageCount();
        const DirectX::TexMetadata& uploadMetadata = hasMips ? pngMipChain.GetMetadata() : pngMetadata;
        const HRESULT srvResult = DirectX::CreateShaderResourceView(
            globals::d3d::device,
            uploadImages,
            uploadCount,
            uploadMetadata,
            target.put());
        if (FAILED(srvResult)) {
            logger::warn(
                "[WindowLife] {} PNG fallback SRV creation failed (HRESULT 0x{:08X}); {} fallback remains active.",
                label,
                static_cast<std::uint32_t>(srvResult),
                analyticFallback ? "analytic" : "procedural");
            target = nullptr;
            return;
        }
        logger::info(
            "[WindowLife] {} PNG compatibility atlas uploaded with {} mip levels.",
            label,
            uploadMetadata.mipLevels);
    };

    loadAtlas(
        "Data\\Shaders\\WindowLife\\RoomAtlas_2k.dds",
        "Data\\Shaders\\WindowLife\\RoomAtlas.png",
        "Room",
        roomAtlasSRV,
        false);
    loadAtlas(
        "Data\\Shaders\\WindowLife\\OutdoorAtlas_2k.dds",
        "Data\\Shaders\\WindowLife\\OutdoorAtlas.png",
        "Outdoor view",
        outdoorAtlasSRV,
        false);
    loadAtlas(
        "Data\\Shaders\\WindowLife\\OutdoorAtlasNight_2k.dds",
        "Data\\Shaders\\WindowLife\\OutdoorAtlasNight.png",
        "Outdoor night view",
        outdoorNightAtlasSRV,
        false);
    loadAtlas(
        {},
        "Data\\Shaders\\WindowLife\\OccupantAtlas.png",
        "Occupant",
        occupantAtlasSRV,
        true);
    loadAtlas(
        "Data\\Shaders\\WindowLife\\CurtainAtlas_high-fidelity-2k.dds",
        "Data\\Shaders\\WindowLife\\CurtainAtlas.png",
        "Curtain",
        curtainAtlasSRV,
        true);

    // Small neutral mask texture replacing per-pixel high-frequency analytic
    // grime. Generate a filtered mip chain at load time; failure is non-fatal and
    // the shader retains its restrained low-frequency analytic fallback.
    glassGrimeSRV = nullptr;
    {
        const std::filesystem::path grimePath = "Data\\Shaders\\WindowLife\\GlassGrime_1k.png";
        DirectX::TexMetadata grimeMetadata{};
        DirectX::ScratchImage grimeImage;
        DirectX::ScratchImage grimeMipChain;
        const HRESULT grimeLoadResult = DirectX::LoadFromWICFile(
            grimePath.c_str(),
            DirectX::WIC_FLAGS_FORCE_RGB,
            &grimeMetadata,
            grimeImage);
        if (SUCCEEDED(grimeLoadResult) && grimeMetadata.width == 1024u && grimeMetadata.height == 1024u) {
            const HRESULT mipResult = DirectX::GenerateMipMaps(
                grimeImage.GetImages(),
                grimeImage.GetImageCount(),
                grimeMetadata,
                DirectX::TEX_FILTER_CUBIC,
                0u,
                grimeMipChain);
            const bool hasMips = SUCCEEDED(mipResult) && grimeMipChain.GetImageCount() > 1u;
            const auto* images = hasMips ? grimeMipChain.GetImages() : grimeImage.GetImages();
            const std::size_t imageCount = hasMips ? grimeMipChain.GetImageCount() : grimeImage.GetImageCount();
            const auto& metadata = hasMips ? grimeMipChain.GetMetadata() : grimeMetadata;
            const HRESULT srvResult = DirectX::CreateShaderResourceView(
                globals::d3d::device,
                images,
                imageCount,
                metadata,
                glassGrimeSRV.put());
            if (FAILED(srvResult))
                glassGrimeSRV = nullptr;
        }
        logger::info(
            "[WindowLife] Mip-filtered glass grime texture '{}' {}.",
            grimePath.string(),
            glassGrimeSRV ? "ready" : "unavailable; analytic fallback active");
    }

    logger::info(
		"[WindowLife] Layered-window GPU resources ready (PS t{} grime={}, t{} occupants={}, t{} curtains={}, t{} optional exact pane clip, t{} room atlas={}, t{} structured SRV, 256-byte per-draw payload; FeatureData b6 unchanged).",
        kGlassGrimeSRVSlot,
        glassGrimeSRV ? "ready" : "analytic",
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
    if (!std::isfinite(hour))
        hour = 12.0f;
    // Calendar continues indoors; the active climate supplies twilight timing
    // for both the outdoor-view atlas and exterior room lighting.
    if (const auto* sky = globals::game::sky; sky && sky->currentClimate) {
        const auto& timing = sky->currentClimate->timing;
        const float daylight = PIXL::AtmosphereWeather::DirectionalFogScale(
            hour, timing.sunrise.begin / 6.0f, timing.sunrise.end / 6.0f,
            timing.sunset.begin / 6.0f, timing.sunset.end / 6.0f);
        return std::clamp((1.0f - daylight) / 0.95f, 0.0f, 1.0f);
    }
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
    // Match SharedData::InInterior exactly. Select the atlas once per frame and
    // reuse t126: no additional resource slots or per-draw ABI growth.
    const bool interiorView = Util::IsInterior();
    frameBaseData.Runtime0 = {
        settings.EnableWindowLife ? 1.0f : 0.0f,
        0.6180339887f,
        shadowStrength,
        activity
    };
    frameBaseData.Optics0 = {
        std::clamp(interiorView ? settings.InteriorParallaxDepth : settings.ParallaxDepth, 0.0f, 240.0f),
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
    // Runtime1.x is a debug mode, not a rendering parameter. Existing shaders
    // treat any non-zero value as the historical class overlay, so mode 1 keeps
    // that behaviour and modes 2..10 expose room/material diagnostics.
    const int debugMode = settings.DebugWindowDetection
        ? std::clamp(settings.DebugRoomProjection, 0, 9) + 1
        : 0;
    frameBaseData.Runtime1 = {
        static_cast<float>(debugMode),
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
        std::clamp(settings.CurtainStrength, 0.0f, 1.0f),
        std::clamp(settings.RoomDepthStrength, 0.0f, 0.60f),
        settings.AutomaticRoomSizing ? 1.0f : 0.0f,
        settings.EnableInteriorPassers ? 1.0f : 0.0f
    };
    frameBaseData.Asset0 = {
        0.0f,
        (interiorView ? settings.EnableOutdoorViews && outdoorAtlasSRV : settings.EnableAuthoredRooms && roomAtlasSRV) ? 1.0f : 0.0f,
        std::clamp(interiorView ? settings.OutdoorViewStrength : settings.AuthoredRoomStrength, 0.0f, 2.0f),
        0.0f
    };
	frameBaseData.Presentation0 = {
		std::clamp(settings.InteriorContrast, 0.50f, 2.0f),
		interiorView ? std::clamp(settings.OutdoorViewEmission, 0.0f, 8.0f) : std::clamp(settings.InteriorEmission, 0.0f, 6.0f),
		std::clamp(settings.OccupantOpacity, 0.0f, 1.0f),
		std::clamp(settings.InteriorScale, 1.0f, 2.5f)
	};
    const auto& weather = WeatherManager::GetSingleton()->GetContext();
    const bool snowing = weather.snowIntensity > 0.01f;
    frameBaseData.Layout0 = {
        0.0f,
        0.0f,
        night,
        snowing ? 1.0f : 0.0f
    };
    frameBaseData.Fidelity0 = {
        std::clamp(settings.EnvironmentReflectionStrength, 0.0f, 1.5f),
        std::clamp(settings.InteriorLightingResponse, 0.0f, 1.0f),
        std::clamp(settings.WeatherGlassResponse, 0.0f, 1.0f),
        std::clamp(settings.SunGlintStrength, 0.0f, 1.0f)
    };
    frameBaseData.Fidelity1 = {
        std::clamp(settings.DirectionalRevealStrength, 0.0f, 0.60f),
        std::clamp(settings.RoomVariationStrength, 0.0f, 0.35f),
        std::clamp(settings.CloseLayerFeather, 0.0f, 1.5f),
        interiorView ? 1.0f : 0.0f
    };
    frameBaseData.Presentation1 = {
        std::clamp(settings.InteriorSoftness, 0.0f, 1.0f),
        std::clamp(settings.RoomVolumeStrength, 0.0f, 1.0f),
        std::clamp(settings.WindowRecess, 0.0f, 18.0f),
        0.0f
    };

    activeDataFrame = frame;
}

bool WindowLife::UploadData(ID3D11Buffer* buffer, const PerGeometryData& data) const
{
    if (!buffer || !globals::d3d::context)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = globals::d3d::context->Map(buffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
    if (FAILED(mapResult)) {
        if (!uploadFailureLogged) {
            logger::error("[WindowLife] Per-draw buffer upload failed (HRESULT 0x{:08X}); binding neutral until resources recover.",
                static_cast<std::uint32_t>(mapResult));
            uploadFailureLogged = true;
        }
        return false;
    }
    std::memcpy(mapped.pData, &data, sizeof(data));
    globals::d3d::context->Unmap(buffer, 0u);
    uploadFailureLogged = false;
    return true;
}

void WindowLife::BindNeutral() const
{
    if (!globals::d3d::context)
        return;
    ID3D11ShaderResourceView* srvs[6] = {
        glassGrimeSRV.get(),
        occupantAtlasSRV.get(),
        curtainAtlasSRV.get(),
        nullptr,
        nullptr,
        neutralSRV.get()
    };
    globals::d3d::context->PSSetShaderResources(kGlassGrimeSRVSlot, 6, srvs);
}

ID3D11ShaderResourceView* WindowLife::GetAuthoredMaskSRV(const Classification& classification) const
{
    // Authored room materials carry their own pane layout. A legacy/preset
    // mask is for vanilla glass only; binding it over authored room art clips
    // the procedural aperture and can pull the room projection off-centre.
    if (frameBaseData.Asset0.z > 0.5f ||
        !settings.UseExactGlassMasks || !classification.hasAuthoredMask || classification.authoredMaskKey.empty())
        return nullptr;
    const auto it = authoredMaskSRVs.find(classification.authoredMaskKey);
    return it != authoredMaskSRVs.end() ? it->second.get() : nullptr;
}

void WindowLife::BindActive(const Classification& classification) const
{
    if (!globals::d3d::context)
        return;
    if (!activeSRV) {
        BindNeutral();
        return;
    }
    ID3D11ShaderResourceView* srvs[6] = {
        glassGrimeSRV.get(),
        occupantAtlasSRV.get(),
        frameBaseData.Fidelity1.w > 0.5f ? outdoorNightAtlasSRV.get() : curtainAtlasSRV.get(),
        GetAuthoredMaskSRV(classification),
        frameBaseData.Asset0.y > 0.5f ? (frameBaseData.Fidelity1.w > 0.5f ? outdoorAtlasSRV.get() : roomAtlasSRV.get()) : nullptr,
        activeSRV.get()
    };
    globals::d3d::context->PSSetShaderResources(kGlassGrimeSRVSlot, 6, srvs);
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
	const bool diffuseArchitecture = ContainsAny(diffusePath, {
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
    // Closed/boarded shutters remain opaque architecture even when a texture
    // replacer preserves "window" in the filename.
    const bool closedWindowSurface = ContainsAny(diffusePath, {
        "shutter", "closedwindow", "closed_window", "windowclosed",
        "window_closed", "boarded", "windowboard", "window_board"
    });
    const bool hasGlowTexture = !glowPath.empty();
	// Some Skyrim building materials name the diffuse after the facade but put
	// the actual pane evidence in a dedicated window glow map. The glow guide is
	// accepted only with an architectural diffuse and remains clipped per pixel
	// by PaneMask; a generic building path or unrelated glow map is insufficient.
	const bool windowGlowGuide = hasGlowTexture && ContainsAny(glowPath, {
		"window", "windows", "windowpane", "window_pane", "glasspane", "glass_pane", "glazing"
	});
    const std::string authoredMaskKey = CanonicalWindowMaskKey(diffusePath);
    const bool hasAuthoredMask =
        settings.UseExactGlassMasks &&
        !authoredMaskKey.empty() &&
        authoredMaskSRVs.contains(authoredMaskKey);

    // Texture replacers commonly rename the diffuse while retaining a paired
    // PIXL mask. Treat that exact diffuse/mask pairing as strong material
    // evidence; a generic glow slot alone remains deliberately insufficient.
    const bool authoredWindowMaterial = hasAuthoredMask && !closedWindowSurface;

    if (strongWindow)
        result.score += 8;
    if (glass)
        result.score += 2;
    if (architecture)
        result.score += 3;
    if (hasGlowTexture)
        result.score += 2;
    if (authoredWindowMaterial)
        result.score += 8;
    if (architecture && hasGlowTexture)
        result.score += 3;
    if (obviousNonBuildingGlass && !strongWindow)
        result.score -= 8;
    if (windowProxyMask || closedWindowSurface)
        result.score -= 16;

    const bool architecturalGlass = architecture && glass && !obviousNonBuildingGlass;
    // Architecture is context, never proof. Treating the broad architecture path
    // as sufficient classified doors, roofs, gravestones and entire facade draws
    // as windows. Require a window/glass token on the diffuse or paired glow,
    // or a dedicated authored pane mask matching this material.
    result.isWindow = !windowProxyMask && !closedWindowSurface &&
        (strongWindow || architecturalGlass || authoredWindowMaterial ||
			(diffuseArchitecture && windowGlowGuide && !obviousNonBuildingGlass));
    result.hasGlowTexture = hasGlowTexture;
    // A diffuse-matched exact mask can admit a texture replacer's window into
    // the shallow room tier. Closed/helper proxy surfaces remain excluded above;
    // the mask clips final glass pixels and never moves the room aperture.
    result.hasAuthoredMask = result.isWindow && hasAuthoredMask;
    result.authoredMaskKey = result.hasAuthoredMask ? authoredMaskKey : std::string{};
    result.explicitWindow = strongWindow || (diffuseArchitecture && windowGlowGuide && !obviousNonBuildingGlass);
    result.diffuseNamedWindow = strongWindow;
    result.namedGlass = glass;
    result.diffuseNamedGlass = ContainsAny(diffusePath, { "glass", "stainedglass", "stained_glass" });
    // Cache the material side of the room identity once. The geometry instance
    // contributes its own stable salt in UpdateAndBindActive; separating the two
    // prevents per-pixel texture evidence from changing room identity.
    result.materialIdentity = material->hashKey != 0
        ? material->hashKey
        : StableHash32(!diffusePath.empty() ? diffusePath : allPaths);
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
    if (strongWindow)
        result.materialTier = 3;
    else if (architecturalGlass || (diffuseArchitecture && windowGlowGuide) || authoredWindowMaterial)
        // Exact diffuse-matched masks admit a room but do not infer a safe
        // occupant layer across a large facade draw.
        result.materialTier = 2;

    result.evidence = !diffusePath.empty() ? diffusePath : (!glowPath.empty() ? glowPath : allPaths);

    if (settings.DebugWindowDetection && (result.isWindow || architecture || glass || hasGlowTexture)) {
        logger::info(
            "[WindowLife] CLASSIFY {} tier={} score={} hash={:08X} arch={} glass={} glow={} mask={} explicit={} proxy={} closed={} texture='{}' layout='{}'",
            result.isWindow ? "WINDOW" : "skip",
            result.materialTier,
            result.score,
            material->hashKey,
            architecture ? 1 : 0,
            glass ? 1 : 0,
            hasGlowTexture ? 1 : 0,
            result.hasAuthoredMask ? 1 : 0,
            result.explicitWindow ? 1 : 0,
            windowProxyMask ? 1 : 0,
            closedWindowSurface ? 1 : 0,
            result.evidence,
            result.hasAuthoredMask ? "procedural + exact clip" : "procedural");
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
        classification.diffuseNamedGlass ? 2.0f : (classification.namedGlass ? 1.0f : 0.0f),
        (classification.hasGlowTexture ? 1.0f : 0.0f) +
            (classification.hasAuthoredMask ? 2.0f : 0.0f),
        classification.diffuseNamedWindow ? 2.0f : (classification.explicitWindow ? 1.0f : 0.0f)
    };
    data.Asset0.x = static_cast<float>(classification.roomFamily);
    // Layout eligibility is a cached material property. 2 means an explicit
    // window with a native glow guide, 1 is a generic architectural-glass glow
    // guide, and 0 keeps the geometry fallback. Aperture bounds themselves still
    // require the current draw's UV/world derivatives and are resolved in HLSL.
    data.Layout0.y = classification.hasGlowTexture
        ? (classification.explicitWindow ? 2.0f : 1.0f)
        : 0.0f;
    if (geometry) {
        const auto& center = geometry->worldBound.center;
        data.Geometry0 = { center.x, center.y, center.z, geometryRadius };

        // The NIF geometry name is stable across diffuse/normal texture replacers.
        // It lets dedicated aperture meshes use one auto-scaled room while large
        // walls/facades retain a repeating reference grid.
        const char* rawGeometryName = geometry->name.c_str();
        geometryNameScratch.assign(rawGeometryName ? rawGeometryName : "");
        std::transform(geometryNameScratch.begin(), geometryNameScratch.end(),
            geometryNameScratch.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string_view geometryName = geometryNameScratch;
        const bool dedicatedNameHint = ContainsAny(geometryName, {
            "window", "glass", "pane", "glazing"
        });
        const bool facadeNameHint = ContainsAny(geometryName, {
            "facade", "wall", "house", "building", "exterior"
        });

        const std::array<std::pair<float, float>, 6> familyRoomSizes{{
            { 110.0f, 140.0f }, { 132.0f, 174.0f }, { 118.0f, 138.0f },
            { 104.0f, 154.0f }, { 128.0f, 128.0f }, { 126.0f, 146.0f }
        }};
        const auto familyIndex = static_cast<std::size_t>(
            std::clamp(classification.roomFamily, 0, 5));
        const auto [familyWidth, familyHeight] = familyRoomSizes[familyIndex];
        const float referenceRadius = std::max(
            std::sqrt(familyWidth * familyWidth + familyHeight * familyHeight) * 0.5f,
            1.0f);
        const float radiusRatio = geometryRadius / referenceRadius;
        // Geometry is authoritative. Names only resolve the deliberately wide
        // ambiguous band, keeping vanilla and replacer meshes equivalent.
        float apertureHint = 0.0f;
        if (radiusRatio <= 2.75f)
            apertureHint = 1.0f;
        else if (radiusRatio >= 4.25f)
            apertureHint = -1.0f;
        else if (dedicatedNameHint != facadeNameHint)
            apertureHint = dedicatedNameHint ? 1.0f : -1.0f;
        data.Asset0.w = apertureHint;

        std::uint32_t instanceIdentity = classification.materialIdentity;
        HashCombine32(instanceIdentity, StableHash32(geometryName));
        // Eighth-unit quantization absorbs harmless transform noise while keeping
        // adjacent instances distinct even when they share the same NIF/material.
        HashCombine32(instanceIdentity, static_cast<std::uint32_t>(std::lround(center.x * 8.0f)));
        HashCombine32(instanceIdentity, static_cast<std::uint32_t>(std::lround(center.y * 8.0f)));
        HashCombine32(instanceIdentity, static_cast<std::uint32_t>(std::lround(center.z * 8.0f)));
        data.Layout0.x = static_cast<float>(instanceIdentity & 0x00FFFFFFu) / 16777216.0f;
    } else {
        data.Geometry0 = { 0.0f, 0.0f, 0.0f, geometryRadius };
        data.Layout0.x = static_cast<float>(classification.materialIdentity & 0x00FFFFFFu) / 16777216.0f;
    }

    if (!activeDataValid || std::memcmp(&data, &currentActiveData, sizeof(data)) != 0) {
        if (!UploadData(activeBuffer.get(), data)) {
            activeDataValid = false;
            BindNeutral();
            return;
        }
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

    // Malformed replacement meshes must never upload NaN bounds into the room
    // projector or overflow the stable instance-identity quantization.
    const auto* geometry = pass->geometry;
    if (!geometry || !std::isfinite(geometry->worldBound.radius) ||
        geometry->worldBound.radius < 0.0f || geometry->worldBound.radius > 1.0e8f ||
        !std::isfinite(geometry->worldBound.center.x) ||
        !std::isfinite(geometry->worldBound.center.y) ||
        !std::isfinite(geometry->worldBound.center.z) ||
        std::abs(geometry->worldBound.center.x) > 1.0e8f ||
        std::abs(geometry->worldBound.center.y) > 1.0e8f ||
        std::abs(geometry->worldBound.center.z) > 1.0e8f) {
        if (!invalidGeometryLogged) {
            logger::warn("[WindowLife] Lighting draw has missing or invalid geometry bounds; binding neutral WindowLife data.");
            invalidGeometryLogged = true;
        }
        BindNeutral();
        return;
    }

    auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(pass->shaderProperty->material);
    const auto& classification = GetClassification(material);
    if (!classification.isWindow) {
        BindNeutral();
        return;
    }

    UpdateAndBindActive(classification, geometry);
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
