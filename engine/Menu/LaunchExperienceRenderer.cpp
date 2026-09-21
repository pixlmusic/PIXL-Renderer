#include "LaunchExperienceRenderer.h"
#include "PCH.h"

#include <chrono>
#include <format>
#include <imgui.h>

#include "Globals.h"
#include "Menu.h"
#include "ModuleVersions.h"
#include "State.h"
#include "PIXLStyle.h"
#include "Fonts.h"
#include "Utils/Input.h"
#include "Modules/ImageReconstruction.h"
#include "Renderer/QualityProfiles.h"

bool LaunchExperienceRenderer::isFirstTimeSetupShown = false;
std::uint32_t LaunchExperienceRenderer::keyThatClosedDialog = 0;
bool LaunchExperienceRenderer::quickSetupRequested = false;

namespace
{
	using ReminderClock = std::chrono::steady_clock;
	bool g_reminderStarted = false;
	ReminderClock::time_point g_reminderStart{};
	constexpr float kReminderDurationSeconds = 8.0f;
	bool g_setupRestartNotice = false;
	bool g_setupExitConfirmed = false;
}

bool LaunchExperienceRenderer::ShouldSkipKeyRelease(uint32_t key)
{
	if (keyThatClosedDialog != 0 && keyThatClosedDialog == key) {
		keyThatClosedDialog = 0;
		return true;
	}
	return false;
}

bool LaunchExperienceRenderer::ShouldShowFirstTimeSetup()
{
	if (isFirstTimeSetupShown && !quickSetupRequested)
		return false;
	const auto* menu = Menu::GetSingleton();
	return menu && (quickSetupRequested || !menu->GetSettings().FirstTimeSetupCompleted);
}

void LaunchExperienceRenderer::OpenQuickSetup()
{
	g_setupRestartNotice = false;
	g_setupExitConfirmed = false;
	quickSetupRequested = true;
	isFirstTimeSetupShown = false;
}

bool LaunchExperienceRenderer::ShouldShowControlReminder()
{
	auto* menu = Menu::GetSingleton();
	if (!menu || menu->IsEnabled || !globals::state || !globals::state->inWorld ||
		!menu->GetSettings().FirstTimeSetupCompleted || isFirstTimeSetupShown)
		return false;

	if (!g_reminderStarted) {
		g_reminderStarted = true;
		g_reminderStart = ReminderClock::now();
	}

	const float elapsed = std::chrono::duration<float>(ReminderClock::now() - g_reminderStart).count();
	return elapsed < kReminderDurationSeconds;
}

void LaunchExperienceRenderer::RenderFirstTimeSetupDialog()
{
	if (!ShouldShowFirstTimeSetup())
		return;

	auto* menu = Menu::GetSingleton();
	if (!menu)
		return;

	auto& io = ImGui::GetIO();
	io.WantCaptureMouse = true;
	io.WantCaptureKeyboard = true;
	io.MouseDrawCursor = true;

	const float scale = Util::GetUIScale();
	const ImVec2 cardSize{
		std::max(1.0f, std::min(700.0f * scale, io.DisplaySize.x - 24.0f)),
		std::max(1.0f, std::min(760.0f * scale, io.DisplaySize.y - 24.0f))
	};
	if (!ImGui::IsPopupOpen("##PIXLLaunchExperience"))
		ImGui::OpenPopup("##PIXLLaunchExperience");
	ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });
	ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
	// Modal ordering isolates setup without stealing focus from its combo popups.

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 34.0f * scale, 26.0f * scale });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f * scale, 12.0f * scale });
	const auto flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                   ImGuiWindowFlags_NoTitleBar;

	if (!ImGui::BeginPopupModal("##PIXLLaunchExperience", nullptr, flags)) {
		ImGui::PopStyleVar(3);
		return;
	}

	// Keep the follow-up inside this modal so setup still owns input capture.
	if (g_setupRestartNotice) {
		PIXLUI::SectionBanner("RESTART NEEDED");
		ImGui::TextWrapped("Your graphics choices have been saved. Restart Skyrim to prepare Frame Generation or Neural Rendering. You can keep playing with the currently available image path and restart later.");
		ImGui::TextWrapped("Exit does not save your game. Save any progress first, then relaunch through your usual SKSE or mod-manager shortcut.");
		ImGui::Checkbox("I understand: exit without saving game progress", &g_setupExitConfirmed);
		ImGui::BeginDisabled(!g_setupExitConfirmed);
		const bool exitPressed = PIXLUI::ActionButton("SAVE SETTINGS & EXIT SKYRIM", ImVec2(-1.0f, 38.0f * scale), false);
		ImGui::EndDisabled();
		const bool continueLater = PIXLUI::ActionButton("CONTINUE FOR NOW", ImVec2(-1.0f, 38.0f * scale), true);
		if (continueLater || exitPressed) {
			MarkFirstTimeSetupComplete();
			if (exitPressed) {
				DXGI_SWAP_CHAIN_DESC desc{};
				DWORD processID = 0;
				if (globals::d3d::swapChain && SUCCEEDED(globals::d3d::swapChain->GetDesc(&desc)) && desc.OutputWindow)
					GetWindowThreadProcessId(desc.OutputWindow, &processID);
				if (processID == GetCurrentProcessId()) {
					// Use the existing window shutdown path, never terminate or spawn a process.
					if (!PostMessageW(desc.OutputWindow, WM_CLOSE, 0, 0))
						logger::warn("[PIXL] Could not request game exit; please exit normally.");
				} else {
					logger::warn("[PIXL] Game window unavailable; please exit normally.");
				}
			}
			g_setupRestartNotice = false;
			g_setupExitConfirmed = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		ImGui::PopStyleVar(3);
		return;
	}

	auto* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRectFullScreen();
	drawList->AddRectFilled({ 0.0f, 0.0f }, io.DisplaySize, IM_COL32(0, 0, 0, 210));
	drawList->PopClipRect();

	const float contentWidth = ImGui::GetContentRegionAvail().x;
	auto centerItem = [contentWidth](float width) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (contentWidth - width) * 0.5f));
	};

	if (menu->uiIcons.logo.texture && menu->uiIcons.logo.size.y > 0.0f) {
		const float height = 96.0f * scale;
		const float width = height * (menu->uiIcons.logo.size.x / menu->uiIcons.logo.size.y);
		centerItem(width);
		ImGui::Image(menu->uiIcons.logo.texture, { width, height });
	} else {
		ImGui::Dummy({ 0.0f, 96.0f * scale });
	}

	const char* productName = "PIXL Renderer";
	{
		MenuFonts::FontRoleGuard titleFont(
			Menu::FontRole::Title);
		ImGui::SetWindowFontScale(1.16f);
		centerItem(
			ImGui::CalcTextSize(
				productName).x);
		ImGui::TextColored(
			PIXLUI::ToVec4(
				PIXLUI::Colors::Text),
			"%s",
			productName);
		ImGui::SetWindowFontScale(1.0f);
	}

	const std::string versionText = std::format("VERSION {}", Plugin::DISPLAY_VERSION.data());
	centerItem(
		ImGui::CalcTextSize(
			versionText.c_str()).x);
	ImGui::TextColored(
		PIXLUI::ToVec4(
			PIXLUI::Colors::CyanSoft),
		"%s",
		versionText.c_str());

	ImGui::Spacing();
	PIXLUI::SectionBanner(
		"FIRST RUN");
	ImGui::Spacing();

	ImGui::TextWrapped(
		"Choose a release-safe image path and quality profile. You can reopen this card later with QUICK SETUP.");
	ImGui::Spacing();

	static int setupQuality = 2;
	static int setupUpscaler = 0;
	static bool setupFrameGeneration = false;
	static bool setupNeuralRendering = false;
	const bool dlssAvailable = globals::pipeline::imageReconstruction.streamline.featureDLSS;
	if (ImGui::IsWindowAppearing()) {
		setupQuality = std::clamp(menu->GetSettings().RendererQuality, 0, 3);
		const auto& reconstruction = globals::pipeline::imageReconstruction.settings;
		const auto current = dlssAvailable ? reconstruction.upscaleMethod : reconstruction.upscaleMethodNoDLSS;
		setupUpscaler = std::clamp(static_cast<int>(current), 0, dlssAvailable ? 3 : 2);
		if (setupUpscaler == 3 && reconstruction.qualityMode == 0)
			setupUpscaler = 4;
		setupFrameGeneration = reconstruction.frameGenerationMode != 0;
		setupNeuralRendering = reconstruction.neuralRenderingEnabled;
	}

	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanSoft), "IMAGE PATH");
	const char* upscalerNames[] = { "Off (native, no temporal AA)", "TAA (native)", "FSR 3.1 Quality", "DLSS Quality", "DLAA (DLSS native anti-aliasing)" };
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##PIXLSetupUpscaler", upscalerNames[setupUpscaler])) {
		for (int index = 0; index < IM_ARRAYSIZE(upscalerNames); ++index) {
			const bool unavailable = index >= 3 && !dlssAvailable;
			ImGui::BeginDisabled(unavailable);
			if (ImGui::Selectable(upscalerNames[index], setupUpscaler == index)) setupUpscaler = index;
			ImGui::EndDisabled();
			if (unavailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("DLSS requires supported NVIDIA hardware and an available DLSS runtime.");
		}
		ImGui::EndCombo();
	}
	Util::AddTooltip("DLAA uses DLSS anti-aliasing at native resolution, without an upscaling performance boost. DLSS Quality renders at a lower resolution. Both require a supported NVIDIA device/runtime.");

	ImGui::Checkbox("Enable frame generation", &setupFrameGeneration);
	Util::AddTooltip("Applies frame generation when you save setup, including the low-refresh-rate override. If its sidecar was not created at launch, one restart is required.");
	if (setupFrameGeneration) {
		ImGui::TextWrapped("Uses your selected frame-generation backend. First activation may require a restart; borderless/windowed mode and a compatible runtime are required.");
		if (!globals::pipeline::imageReconstruction.isWindowed)
			ImGui::TextWrapped("Exclusive fullscreen detected: switch to borderless/windowed before relaunching. Restarting alone will not enable frame generation.");
	}

	const auto& reconstructionRuntime = globals::pipeline::imageReconstruction;
	const bool neuralAvailable = dlssAvailable && reconstructionRuntime.streamline.neuralRenderingSupportedOnCurrentAdapter;
	ImGui::BeginDisabled(!neuralAvailable);
	if (ImGui::Checkbox("Enable Neural Rendering (experimental)", &setupNeuralRendering) && setupNeuralRendering && setupUpscaler < 3)
		setupUpscaler = 4;  // NR requires DLSS; preserve native resolution with DLAA.
	ImGui::EndDisabled();
	if (!neuralAvailable)
		ImGui::TextWrapped("Neural Rendering requires supported NVIDIA hardware and the installed NR runtime.");
	if (setupNeuralRendering) {
		ImGui::TextWrapped("Requires DLSS or DLAA and SDR. First activation requires a restart to prepare Neural Rendering; use borderless/windowed mode. Normal DLSS remains the fallback if NR is unavailable.");
		if (setupUpscaler < 3)
			ImGui::TextWrapped("Select DLSS or DLAA above to enable Neural Rendering.");
	}

	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanSoft), "QUALITY PROFILE");
	const char* qualityNames[] = { "Fast", "Balanced", "Enhanced", "Cinematic" };
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::Combo("##PIXLSetupQuality", &setupQuality, qualityNames, IM_ARRAYSIZE(qualityNames));
	Util::AddTooltip("Enhanced is the recommended default: higher lighting and material fidelity with a controlled performance budget. Cinematic is intended for powerful systems and photo work.");

	ImGui::TextWrapped(
		"Press Home for Photo Mode: pause the scene, compose a shot, adjust camera and finish settings, then capture without changing your normal gameplay profile. Page Down opens PIXL Renderer by default.");
	ImGui::Spacing();

	const bool capturing = menu->settingToggleKey;
	const std::string keyLabel = capturing ? "PRESS A KEY" : Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);
	const ImVec2 keyButtonSize{ 245.0f * scale, 40.0f * scale };
	const float controlGap = 18.0f * scale;
	const float controlsWidth = keyButtonSize.x * 2.0f + controlGap;
	centerItem(controlsWidth);
	ImGui::BeginGroup();
	ImGui::TextDisabled("PIXL RENDERER");
	if (capturing) {
		const auto pulse = Util::GetPulsingColor(menu->GetTheme().StatusPalette.CurrentHotkey);
		ImGui::PushStyleColor(ImGuiCol_Button, pulse);
	}
	if (PIXLUI::ActionButton(keyLabel.c_str(), keyButtonSize, capturing) && !capturing)
		menu->settingToggleKey = true;
	if (capturing)
		ImGui::PopStyleColor();
	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "Quality, camera and renderer controls");
	ImGui::EndGroup();

	ImGui::SameLine(0.0f, controlGap);
	ImGui::BeginGroup();
	ImGui::TextDisabled("PHOTO MODE");
	PIXLUI::ActionButton("HOME", keyButtonSize, false);
	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "Photo mode and high-quality capture");
	ImGui::EndGroup();

	ImGui::Spacing();
	const ImVec2 continueSize{ contentWidth, 38.0f * scale };
	const bool continuePressed =
		PIXLUI::ActionButton(
			"SAVE & CONTINUE",
			continueSize,
			true);

	const bool escapePressed = ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
	const bool enterPressed = ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
	if (!capturing && (continuePressed || enterPressed || escapePressed)) {
		if (!escapePressed) {
			// Do not replace the owner's tuned release defaults just by accepting setup.
			if (setupQuality != menu->GetSettings().RendererQuality)
				PIXLRenderer::QualityProfiles::ApplyGlobal(std::clamp(setupQuality, 0, 3));
			auto& reconstruction = globals::pipeline::imageReconstruction.settings;
			const bool selectedDLAA = dlssAvailable && setupUpscaler == 4;
			const uint selectedMethod = selectedDLAA ? 3u : static_cast<uint>(std::clamp(setupUpscaler, 0, dlssAvailable ? 3 : 2));
			reconstruction.upscaleMethod = selectedMethod;
			if (selectedMethod != static_cast<uint>(ImageReconstruction::UpscaleMethod::kDLSS))
				reconstruction.upscaleMethodNoDLSS = selectedMethod;
			reconstruction.qualityMode = !selectedDLAA && selectedMethod >= static_cast<uint>(ImageReconstruction::UpscaleMethod::kFSR) ? 1u : 0u;
			reconstruction.frameGenerationMode = setupFrameGeneration ? 1u : 0u;
			reconstruction.neuralRenderingEnabled = setupNeuralRendering && neuralAvailable && selectedMethod == 3u;
			if (setupFrameGeneration)
				reconstruction.frameGenerationForceEnable = 1;
			if (globals::state)
				globals::state->Save();
			g_setupRestartNotice = globals::pipeline::imageReconstruction.GetFrameGenerationState() == ImageReconstruction::FrameGenerationState::RestartRequired;
			g_setupRestartNotice |= reconstruction.neuralRenderingEnabled &&
				(!reconstructionRuntime.d3d12SwapChainActive || !reconstructionRuntime.neuralRenderingProvisionedAtBoot);
		}
		if (!g_setupRestartNotice) {
			MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : (enterPressed ? VK_RETURN : 0));
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::EndPopup();
	ImGui::PopStyleVar(3);
}

void LaunchExperienceRenderer::RenderControlReminder()
{
	if (!ShouldShowControlReminder())
		return;

	auto* menu = Menu::GetSingleton();
	if (!menu)
		return;

	const float elapsed = std::chrono::duration<float>(ReminderClock::now() - g_reminderStart).count();
	const float fadeIn = std::clamp(elapsed / 0.55f, 0.0f, 1.0f);
	const float fadeOut = std::clamp((kReminderDurationSeconds - elapsed) / 1.5f, 0.0f, 1.0f);
	const float alpha = std::min(fadeIn, fadeOut);
	const float scale = Util::GetUIScale();
	const auto& io = ImGui::GetIO();

	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x - 28.0f * scale, io.DisplaySize.y - 28.0f * scale),
		ImGuiCond_Always,
		ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.88f * alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 14.0f * scale));
	const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

	if (ImGui::Begin("##PIXLControlReminder", nullptr, flags)) {
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanSoft), "PIXL RENDERER READY");
		ImGui::Separator();
		const std::string menuKey = Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::Text), "[ %-12s ]  PIXL MENU", menuKey.c_str());
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::Text), "[ HOME         ]  PHOTO MODE");
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::Text), "[ ALT + N      ]  NEURAL RENDERING");
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "NR requires DLSS and NVIDIA RTX 30-series or newer");
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void LaunchExperienceRenderer::MarkFirstTimeSetupComplete(uint32_t closingKey)
{
	auto* menu = Menu::GetSingleton();
	if (!menu)
		return;
	menu->GetSettings().FirstTimeSetupCompleted = true;
	menu->settingToggleKey = false;
	if (globals::state)
		globals::state->Save();
	isFirstTimeSetupShown = true;
	quickSetupRequested = false;
	keyThatClosedDialog = closingKey;
}
