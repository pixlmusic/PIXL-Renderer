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

bool LaunchExperienceRenderer::isFirstTimeSetupShown = false;
uint32_t LaunchExperienceRenderer::keyThatClosedDialog = 0;

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
	if (isFirstTimeSetupShown)
		return false;
	const auto* menu = Menu::GetSingleton();
	return menu && !menu->GetSettings().FirstTimeSetupCompleted;
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
	const ImVec2 cardSize{ 640.0f * scale, 430.0f * scale };
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
		"Two controls are all you need for normal play. Select the PIXL menu key now; Director remains on Insert.");
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
			"CONTINUE WITH HIGH QUALITY",
			continueSize,
			true);

	const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
	const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
	if (!capturing && (continuePressed || enterPressed || escapePressed))
		MarkFirstTimeSetupComplete(escapePressed ? VK_ESCAPE : (enterPressed ? VK_RETURN : 0));

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
	keyThatClosedDialog = closingKey;
}
