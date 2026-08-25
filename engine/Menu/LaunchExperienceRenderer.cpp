#include "LaunchExperienceRenderer.h"
#include "PCH.h"

#include <imgui.h>

#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "PIXLStyle.h"
#include "Fonts.h"
#include "Utils/Input.h"

bool LaunchExperienceRenderer::isFirstTimeSetupShown = false;
uint32_t LaunchExperienceRenderer::keyThatClosedDialog = 0;

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
	const ImVec2 cardSize{ 520.0f * scale, 360.0f * scale };
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

	const bool capturing = menu->settingToggleKey;
	const std::string keyLabel = capturing ? "PRESS A KEY" : Util::Input::KeyIdToString(menu->GetSettings().ToggleKey);
	const char* menuKeyLabel = "MENU KEY";
	centerItem(ImGui::CalcTextSize(menuKeyLabel).x);
	ImGui::TextDisabled("%s", menuKeyLabel);

	const ImVec2 keyButtonSize{ 220.0f * scale, 38.0f * scale };
	centerItem(keyButtonSize.x);
	if (capturing) {
		const auto pulse = Util::GetPulsingColor(menu->GetTheme().StatusPalette.CurrentHotkey);
		ImGui::PushStyleColor(ImGuiCol_Button, pulse);
	}
	if (PIXLUI::ActionButton(keyLabel.c_str(), keyButtonSize, capturing) && !capturing)
		menu->settingToggleKey = true;
	if (capturing)
		ImGui::PopStyleColor();

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
