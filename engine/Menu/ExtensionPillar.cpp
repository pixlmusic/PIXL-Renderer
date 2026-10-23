#include "ExtensionPillar.h"
#include "ExtensionCallback.h"
#include "PIXLStyle.h"
#include "Renderer/ExternalPostProcessing.h"
#include "State.h"
#include "PIXL/SurfingAPI.h"

#include <imgui_internal.h>

#include <cmath>
#include <iterator>
#include <Windows.h>

namespace PIXLUI::Extensions
{
	namespace
	{
		bool open = false;
		Handle selected = 0;
		bool initialized = false;
		bool showSurfTelemetry = false;

		const char* SurfStateName(std::uint32_t state) noexcept
		{
			static constexpr const char* names[]{ "On land", "Carrying board", "Deploying", "Paddling",
				"Catching wave", "Popping up", "Riding", "Wipeout", "Swimming", "Recovering",
				"Shallow water", "Dismounting" };
			return state < std::size(names) ? names[state] : "Unknown";
		}

		template <class Callback>
		bool Invoke(Entry& entry, Callback&& callback)
		{
			const bool wasFaulted = entry.faulted;
			const bool result = InvokeIsolated(entry, std::forward<Callback>(callback));
			if (!wasFaulted && entry.faulted)
				logger::error("[PIXL UI] Extension '{}' isolated after a callback or UI-stack failure", entry.panel.identifier);
			return result;
		}

		void RegisterBuiltins()
		{
			if (initialized)
				return;
			initialized = true;
			(void)GetRegistry().Register({ "pixl.external-post", "External post-processing", "Compatibility", 0,
				[] { ExternalPostProcessing::DrawSettings(); },
				[] { return Availability{ globals::state != nullptr, "Renderer is initializing." }; }, "FX" });
			(void)GetRegistry().Register({ "pixl.session-styles", "Session style library", "Camera", 0,
				[] {
					ImGui::TextWrapped("Named snapshots last for this session. Use SAVE LOOK in PIXL to persist the applied camera settings.");
					ExternalPostProcessing::DrawENBQuickStyles();
				},
				{}, "S" });
			(void)GetRegistry().Register({ "pixl.surfing", "PIXL Surfing", "Gameplay", 0,
				[] {
					ImGui::TextWrapped("PIXL Surfing is installed as a separate SKSE addon. It samples Ocean Dynamics through the versioned ocean API and creates its own board reference at runtime; no separate board item is required.");
					ImGui::SeparatorText("Prototype controls");
					ImGui::TextWrapped("Press V once on land to carry the board. Wade into open ocean until water is near your feet, then press V once to deploy. W paddles, A/D steer or carve, Space attempts to stand when a wave is catchable, and R dismounts.");
					ImGui::TextWrapped("The addon requires the Ocean Dynamics module and live FFT shoreline data.");
					ImGui::SeparatorText("Diagnostics");
					const bool debugChanged = ImGui::Checkbox("Show Surfing Debug Telemetry", &showSurfTelemetry);
					auto* addon = GetModuleHandleW(L"PIXLSurfing.dll");
					auto setDebug = addon ? reinterpret_cast<PIXL_SurfingSetDebugV1>(
						GetProcAddress(addon, "PIXL_Surfing_SetDebug_V1")) : nullptr;
					if (debugChanged && setDebug)
						setDebug(showSurfTelemetry ? 1u : 0u);
					if (showSurfTelemetry) {
						auto getTelemetry = addon ? reinterpret_cast<PIXL_SurfingGetTelemetryV2>(
							GetProcAddress(addon, "PIXL_Surfing_GetTelemetry_V2")) : nullptr;
						PIXL_SurfingTelemetryV2 telemetry{};
						telemetry.base.structSize = sizeof(telemetry);
						if (getTelemetry && getTelemetry(&telemetry)) {
							const auto& base = telemetry.base;
							ImGui::Text("State: %s", SurfStateName(base.state));
							ImGui::Text("Ocean: %s  |  Surface: %s  |  Depth: %s",
								(base.flags & (1u << 0)) ? "connected" : "missing",
								(base.flags & (1u << 1)) ? "valid" : "unavailable",
								(base.flags & (1u << 2)) ? "valid" : "unavailable");
							ImGui::Text("Water height %.1f  |  depth %.1f  |  sample age %.3fs",
								base.waterHeight, base.waterDepth, base.sampleAgeSeconds);
							ImGui::Text("Worldspace %08X  |  surface normal %.2f, %.2f, %.2f",
								base.worldspaceFormID, base.waterNormal[0],
								base.waterNormal[1], base.waterNormal[2]);
							ImGui::Text("Player %.0f, %.0f, %.0f",
								base.playerPosition[0], base.playerPosition[1], base.playerPosition[2]);
							ImGui::Text("Player speed %.1f  |  paddle %.1f  |  steer %.1f  |  controller %s",
								std::hypot(telemetry.playerVelocity[0], telemetry.playerVelocity[1]),
								telemetry.paddleInput, telemetry.steerInput,
								(base.flags & (1u << 5)) ? "owned" : "vanilla");
							ImGui::Text("Weather wind %.1f, %.1f", telemetry.windVelocity[0], telemetry.windVelocity[1]);
							ImGui::Text("Board %.0f, %.0f, %.0f  |  speed %.1f",
								base.boardPosition[0], base.boardPosition[1], base.boardPosition[2],
								std::hypot(base.boardVelocity[0], base.boardVelocity[1]));
							ImGui::Text("Wave dir %.2f, %.2f  |  breaker %.2f  |  catch %.2f  |  balance %.2f",
								base.waveDirection[0], base.waveDirection[1], base.breakerStrength,
								base.catchConfidence, base.balance);
							if (base.state == 1 && (base.flags & (1u << 1)) &&
								(base.flags & (1u << 2)) && base.waterDepth <= 18.0f)
								ImGui::TextColored(ToVec4(Colors::Warning), "Too shallow to deploy; move into deeper open water.");
						} else {
						ImGui::TextWrapped("Surfing telemetry API is unavailable; confirm PIXLSurfing.dll loaded successfully.");
						if (setDebug)
							setDebug(showSurfTelemetry ? 1u : 0u);
					}
					}
				},
				[] {
					const bool installed = GetModuleHandleW(L"PIXLSurfing.dll") != nullptr;
					return Availability{ installed,
						installed ? "Standalone SKSE addon detected." : "Install PIXLSurfing.dll under Data/SKSE/Plugins.",
						installed ? Status::Detected : Status::NotInstalled };
				}, "W" });
		}
	}

	Registry& GetRegistry()
	{
		static Registry registry;
		return registry;
	}
	void ClosePillar() { open = false; }
	bool IsPillarOpen() noexcept { return open; }
	void Shutdown()
	{
		ClosePillar();
		GetRegistry().Shutdown();
	}

	void DrawPillar()
	{
		RegisterBuiltins();
		const auto* viewport = ImGui::GetMainViewport();
		const float margin = Ref(16.0f);
		const float width = std::max(1.0f, std::min(Ref(420.0f), viewport->WorkSize.x - 2.0f * margin));
		const float buttonHeight = std::max(ImGui::GetFrameHeight(), Ref(30.0f));
		// A deliberate button is both discoverable and immune to fly-outs when
		// crossing the screen edge. It never takes camera ownership.
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin,
									viewport->WorkPos.y + Ref(95.0f)),
			ImGuiCond_Always, ImVec2(1, 0));
		ImGui::SetNextWindowSize(ImVec2(open ? width : Ref(118.0f),
			open ? std::max(buttonHeight + 2.0f * ImGui::GetStyle().WindowPadding.y,
				std::min(Ref(580.0f), viewport->WorkSize.y - Ref(190.0f))) : buttonHeight + 2.0f * ImGui::GetStyle().WindowPadding.y));
		const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
		                   ImGuiWindowFlags_NoFocusOnAppearing;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.045f, 0.055f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.055f, 0.065f, 1.0f));
		if (ImGui::Begin("PIXL Extensions###PIXLExtensionPillar", nullptr, flags)) {
			if (ActionButton(open ? "CLOSE EXTENSIONS" : "EXTENSIONS", ImVec2(-1.0f, buttonHeight), open))
				open = !open;
			if (auto tip = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Compatible tools and session styles. Click to open or close; your current module stays selected.");
			if (open) {
				const auto snapshot = GetRegistry().Snapshot();
				std::shared_ptr<Entry> active;
				std::string_view category;
				for (const auto& entry : *snapshot) {
					if (!entry->registered)
						continue;
					if (category != entry->panel.category) {
						category = entry->panel.category;
						ImGui::SeparatorText(entry->panel.category.c_str());
					}
					ImGui::PushID(entry->panel.identifier.c_str());
					if (!entry->panel.icon.empty()) {
						ImGui::TextColored(ToVec4(Colors::CyanSoft), "%s", entry->panel.icon.c_str());
						ImGui::SameLine();
					}
					if (ImGui::Selectable(entry->panel.displayName.c_str(), selected == entry->handle))
						selected = entry->handle;
					if (selected == entry->handle)
						active = entry;
					ImGui::PopID();
				}
				ImGui::Separator();
				if (!active) {
					ImGui::TextWrapped("Choose an extension above. These tools do not replace PIXL's module controls.");
				} else {
					ImGui::PushID(active->panel.identifier.c_str());
					if (ImGui::BeginChild("##ExtensionBody", ImVec2(0, 0))) {
						Availability status;
						if (active->panel.availability)
							Invoke(*active, [&] { status = active->panel.availability(); });
						if (!active->faulted) {
							const char* label = "AVAILABLE";
							ImU32 tone = Colors::CyanSoft;
							switch (status.status) {
							case Status::Detected: label = "DETECTED"; tone = Colors::Warning; break;
							case Status::NotInstalled: label = "NOT INSTALLED"; tone = Colors::TextDim; break;
							case Status::Unsupported: label = "UNSUPPORTED"; tone = Colors::Warning; break;
							case Status::Error: label = "ERROR"; tone = Colors::Warning; break;
							default: break;
							}
							if (!status.available && status.status == Status::Available) {
								label = "UNAVAILABLE";
								tone = Colors::TextDim;
							}
							ImGui::TextColored(ToVec4(tone), "%s", label);
							if (!status.reason.empty())
								ImGui::TextWrapped("%s", status.reason.c_str());
							ImGui::Separator();
						}
						if (active->faulted) {
							ImGui::TextWrapped("This extension was stopped after an error. Other PIXL controls remain available. See PIXLRenderer.log.");
						} else if (!status.available) {
							if (status.reason.empty())
								ImGui::TextWrapped("Requirements are not met.");
						} else {
							EngineeringStyleScope style;
							Invoke(*active, [&] { active->panel.draw(); });
						}
					}
					ImGui::EndChild();
					ImGui::PopID();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}
