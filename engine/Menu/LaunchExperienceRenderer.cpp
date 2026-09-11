#include "LaunchExperienceRenderer.h"
#include "PCH.h"

#include <chrono>
#include <imgui.h>

#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "PIXLStyle.h"
#include "Fonts.h"
#include "Utils/Input.h"
#include "Modules/ImageReconstruction.h"
#include "Renderer/QualityProfiles.h"

bool LaunchExperienceRenderer::isFirstTimeSetupShown = false;
uint32_t LaunchExperienceRenderer::keyThatClosedDialog = 0;
bool LaunchExperienceRenderer::quickSetupRequested = false;

namespace
{
	using ReminderClock = std::chrono::steady_clock;
	bool g_reminderStarted = false;
	ReminderClock::time_point g_reminderStart{};
	constexpr float kReminderDurationSeconds = 8.0f;
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
	const ImVec2 cardSize{ 700.0f * scale, 590.0f * scale };
	ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });
	ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
	ImGui::SetNextWindowFocus();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f * scale);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 34.0f * scale, 26.0f * scale });
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f * scale, 12.0f * scale });
	const auto flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                   ImGuiWindowFlags_NoTitleBar;

	if (!ImGui::Begin("##PIXLLaunchExperience", nullptr, flags)) {
		ImGui::End();
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

	const char* versionText =
		"VERSION 1.0";
	centerItem(
		ImGui::CalcTextSize(
			versionText).x);
	ImGui::TextColored(
		PIXLUI::ToVec4(
			PIXLUI::Colors::CyanSoft),
		"%s",
		versionText);

	ImGui::Spacing();
	PIXLUI::SectionBanner(
		"FIRST RUN");
	ImGui::Spacing();

	ImGui::TextColored(
		PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
		"Choose a release-safe image path and quality profile. You can reopen this card later with QUICK SETUP.");
	ImGui::Spacing();

	static int setupQuality = 2;
	static int setupUpscaler = 0;
	if (!quickSetupRequested && menu->GetSettings().FirstTimeSetupCompleted) {
		setupQuality = std::clamp(menu->GetSettings().RendererQuality, 0, 3);
		const auto current = globals::pipeline::imageReconstruction.settings.upscaleMethodNoDLSS;
		setupUpscaler = current == static_cast<uint>(ImageReconstruction::UpscaleMethod::kFSR) ? 1 : 0;
	}

	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanSoft), "IMAGE PATH");
	const char* upscalerNames[] = { "TAA (universal)", "FSR 3.1 Quality" };
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::Combo("##PIXLSetupUpscaler", &setupUpscaler, upscalerNames, IM_ARRAYSIZE(upscalerNames));
	Util::AddTooltip("TAA works on every supported Skyrim setup. FSR 3.1 Quality renders below display resolution for more performance and needs the bundled FidelityFX runtime.");

	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::CyanSoft), "QUALITY PROFILE");
	const char* qualityNames[] = { "Fast", "Balanced", "Enhanced", "Cinematic" };
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::Combo("##PIXLSetupQuality", &setupQuality, qualityNames, IM_ARRAYSIZE(qualityNames));
	Util::AddTooltip("Enhanced is the recommended default: higher lighting and material fidelity with a controlled performance budget. Cinematic is intended for powerful systems and photo work.");

	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
		"Photo Mode is PIXL Director on Insert: pause the scene, compose a shot, adjust camera and finish settings, then capture without changing your normal gameplay profile.");
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
	ImGui::TextDisabled("PIXL DIRECTOR");
	PIXLUI::ActionButton("INSERT", keyButtonSize, false);
	ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::TextDim), "Photo mode and high-quality capture");
	ImGui::EndGroup();

	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 74.0f * scale);
	const ImVec2 continueSize{ contentWidth, 38.0f * scale };
	const bool continuePressed =
		PIXLUI::ActionButton(
			"SAVE & CONTINUE",
			continueSize,
			true);

	const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
	if (!capturing && (continuePressed || enterPressed || escapePressed)) {
		if (!escapePressed) {
			PIXLRenderer::QualityProfiles::ApplyGlobal(std::clamp(setupQuality, 0, 3));
			auto& reconstruction = globals::pipeline::imageReconstruction.settings;
			const uint selectedMethod = setupUpscaler == 1 ?
				static_cast<uint>(ImageReconstruction::UpscaleMethod::kFSR) :
				static_cast<uint>(ImageReconstruction::UpscaleMethod::kTAA);
			reconstruction.upscaleMethod = selectedMethod;
			reconstruction.upscaleMethodNoDLSS = selectedMethod;
			reconstruction.qualityMode = setupUpscaler == 1 ? 1u : 0u;
			if (globals::state)
				globals::state->Save();
		}
		MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : (enterPressed ? VK_RETURN : 0));
	}

	ImGui::End();
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
		ImGui::TextColored(PIXLUI::ToVec4(PIXLUI::Colors::Text), "[ INSERT       ]  PIXL DIRECTOR");
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
