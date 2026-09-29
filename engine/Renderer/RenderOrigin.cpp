// PIXL Renderer - stable large-world render-origin service.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "RenderOrigin.h"

#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "Util.h"

namespace PIXL::RenderOrigin
{
    Manager& Get()
    {
        static Manager manager;
        return manager;
    }

    void DrawExperimentalPanel()
    {
        auto& manager = Get();
        auto& menuSettings = globals::menu->GetSettings();
        ImGui::PushID("PIXLRenderOrigin");
        ImGui::TextColored(ImVec4(0.79f, 0.64f, 0.36f, 1.0f), "EXPERIMENTAL / WORLD COORDINATES");
        ImGui::TextWrapped("Rebases PIXL's rendering coordinates near the camera without moving Skyrim objects, physics, scripts, or save data. Boundary and temporal validation is still in progress.");
        if (ImGui::Checkbox("Enable Render Origin", &menuSettings.ExperimentalRenderOriginEnabled)) {
            manager.requestedEnabled = menuSettings.ExperimentalRenderOriginEnabled;
            globals::state->Save();
        }
        if (auto tip = Util::HoverTooltipWrapper())
            ImGui::TextWrapped("Improves numerical precision far from world origin. Disable this first if shadows, fog, water, precipitation, temporal reconstruction, or world-space effects jump at an origin boundary.");
        ImGui::BeginDisabled(!menuSettings.ExperimentalRenderOriginEnabled);
        ImGui::Checkbox("Torture test: 256-unit grid", &manager.smallGrid);
        if (auto tip = Util::HoverTooltipWrapper())
            ImGui::TextWrapped("Forces frequent origin boundaries so discontinuities are easy to reproduce. Intended only for controlled testing.");
        ImGui::Checkbox("Log origin shifts", &manager.verbose);
        if (ImGui::Button("Force origin shift")) manager.forceShift = true;
        ImGui::EndDisabled();
        const auto camera = manager.GetAbsoluteCameraPosition();
        const auto origin = manager.GetCurrentOrigin();
        const auto previous = manager.GetPreviousOrigin();
        const auto delta = manager.GetOriginDelta();
        const auto relative = manager.WorldToRender(camera);
        ImGui::Text("Camera: %.3f, %.3f, %.3f", camera.x, camera.y, camera.z);
        ImGui::Text("Origin: %.0f, %.0f, %.0f", origin.x, origin.y, origin.z);
        ImGui::Text("Previous: %.0f, %.0f, %.0f", previous.x, previous.y, previous.z);
        ImGui::Text("Delta: %.0f, %.0f, %.0f", delta.x, delta.y, delta.z);
        ImGui::Text("Relative camera: %.3f, %.3f, %.3f", relative.x, relative.y, relative.z);
        ImGui::Text("Distance from origin: %.3f", std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z));
        ImGui::Text("Epoch: %llu | shifted: %s | continuity: %s",
            static_cast<unsigned long long>(manager.GetOriginEpoch()),
            manager.ShiftedThisFrame() ? "yes" : "no", manager.HistoryValid() ? "valid" : "discontinuity");
        ImGui::PopID();
    }
}
