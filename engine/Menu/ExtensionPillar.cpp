#include "ExtensionPillar.h"
#include "ExtensionCallback.h"
#include "PIXLStyle.h"
#include "Renderer/ExternalPostProcessing.h"
#include "State.h"

#include <imgui_internal.h>

namespace PIXLUI::Extensions
{
	namespace
	{
		bool open = false;
		Handle selected = 0;
		bool initialized = false;

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
		}
	}

	Registry& GetRegistry()
	{
		static Registry registry;
		return registry;
	}
	void ClosePillar() { open = false; }
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
			open ? std::min(Ref(580.0f), viewport->WorkSize.y - Ref(190.0f)) : buttonHeight + 2.0f * ImGui::GetStyle().WindowPadding.y));
		const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
		                   ImGuiWindowFlags_NoFocusOnAppearing;
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
						if (active->faulted) {
							ImGui::TextWrapped("This extension was stopped after an error. Other PIXL controls remain available. See PIXLRenderer.log.");
						} else if (!status.available) {
							ImGui::TextWrapped("Unavailable: %s", status.reason.empty() ? "Requirements are not met." : status.reason.c_str());
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
	}
}
