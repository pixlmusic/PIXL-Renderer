#include "Menu.h"

#include <SKSE/InputMap.h>

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <algorithm>
#include <cmath>
#include <dinput.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

#include <RE/S/SendHUDMessage.h>

#include "Deferred.h"
#include "RenderModule.h"
#include "PipelineHealth.h"
#include "ModuleVersions.h"
#include "Modules/ImageReconstruction.h"
#include "I18n/I18n.h"
#include "Menu/WorkshopToolsRenderer.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/CursorLoader.h"
#include "Menu/TuningWorkspaceRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/LaunchExperienceRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/PIXLRendererPage.h"
#include "Menu/PIXLStyle.h"
#include "Menu/RuntimeSettingsRenderer.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Modules/PulseProfiler.h"
#include "Modules/PixelCapture.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	WindowBorder,
	FrameBorder,
	Separator,
	ResizeGrip)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::StatusPaletteColors,
	Disable,
	Error,
	Warning,
	RestartNeeded,
	CurrentHotkey,
	SuccessColor,
	InfoColor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FeatureHeadingColors,
	ColorDefault,
	ColorHovered,
	MinimizedFactor,
	FeatureTitleScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::ScrollbarOpacitySettings,
	Background,
	Thumb,
	ThumbHovered,
	ThumbActive)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FontRoleSettings,
	Family,
	Style,
	File,
	SizeScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImGuiStyle,
	WindowPadding,
	WindowRounding,
	WindowBorderSize,
	WindowMinSize,
	ChildRounding,
	ChildBorderSize,
	PopupRounding,
	PopupBorderSize,
	FramePadding,
	FrameRounding,
	FrameBorderSize,
	ItemSpacing,
	ItemInnerSpacing,
	CellPadding,
	IndentSpacing,
	ColumnsMinSpacing,
	ScrollbarSize,
	ScrollbarRounding,
	GrabMinSize,
	GrabRounding,
	LogSliderDeadzone,
	ImageRounding,
	ImageBorderSize,
	TabRounding,
	TabBorderSize,
	TabCloseButtonMinWidthSelected,
	TabCloseButtonMinWidthUnselected,
	TabBarBorderSize,
	TabBarOverlineSize,
	TableAngledHeadersAngle,
	TableAngledHeadersTextAlign,
	TreeLinesSize,
	TreeLinesRounding,
	DragDropTargetRounding,
	DragDropTargetBorderSize,
	DragDropTargetPadding,
	ColorMarkerSize,
	ColorButtonPosition,
	ButtonTextAlign,
	SelectableTextAlign,
	SeparatorTextBorderSize,
	SeparatorTextAlign,
	SeparatorTextPadding,
	DockingSeparatorSize,
	MouseCursorScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::CursorImageSettings,
	File,
	HotspotX,
	HotspotY)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::CursorSettings,
	Scale,
	File,
	HotspotX,
	HotspotY)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings,
	FontSize,
	FontName,
	GlobalScale,
	FontRoles,
	UseSimplePalette,
	ShowActionIcons,
	UseMonochromeIcons,
	UseMonochromeLogo,
	ShowFooter,
	CenterHeader,
	TooltipHoverDelay,
	BackgroundBlurEnabled,
	UseCustomCursor,
	Cursor,
	ScrollbarOpacity,
	Palette,
	StatusPalette,
	FeatureHeading,
	Style)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
	OverlayToggleKey,
	ShaderBlockPrevKey,
	ShaderBlockNextKey,
	EnableShaderBlocking,
	FirstTimeSetupCompleted,
	SkipClearCacheConfirmation,
	AutoHideFeatureList,
	SkipConstraintWarning,
	RequireShiftToDock,
	UseResolutionFont,
	AdvancedMode,
	DeveloperMode,
	RendererQuality,
	LightingQuality,
	MaterialsQuality,
	AtmosphereQuality,
	WaterQuality,
	TerrainVegetationQuality,
	CharactersQuality,
	CameraQuality,
	Theme,
	SelectedThemePreset)

bool IsEnabled = false;
std::unordered_map<std::string, int> Menu::categoryCounts;

namespace
{
	struct CursorTypeKey
	{
		const char* key;
		ImGuiMouseCursor type;
	};

	constexpr CursorTypeKey kCursorTypeKeys[] = {
		{ "Arrow", ImGuiMouseCursor_Arrow },
		{ "TextInput", ImGuiMouseCursor_TextInput },
		{ "ResizeAll", ImGuiMouseCursor_ResizeAll },
		{ "ResizeNS", ImGuiMouseCursor_ResizeNS },
		{ "ResizeEW", ImGuiMouseCursor_ResizeEW },
		{ "ResizeNESW", ImGuiMouseCursor_ResizeNESW },
		{ "ResizeNWSE", ImGuiMouseCursor_ResizeNWSE },
		{ "Hand", ImGuiMouseCursor_Hand },
		{ "NotAllowed", ImGuiMouseCursor_NotAllowed },
	};
}

void Menu::CursorFromJson(const json& cursorJson, ThemeSettings::CursorSettings& cursor)
{
	cursor.Types = {};

	if (!cursorJson.contains("Types")) {
		return;
	}

	const auto& types = cursorJson["Types"];
	if (types.is_object()) {
		for (const auto& [key, type] : kCursorTypeKeys) {
			if (types.contains(key) && types[key].is_object()) {
				types[key].get_to(cursor.Types[static_cast<size_t>(type)]);
			}
		}
		return;
	}

	// Legacy: sparse array indexed by ImGuiMouseCursor_*
	if (types.is_array()) {
		for (size_t i = 0; i < ImGuiMouseCursor_COUNT && i < types.size(); ++i) {
			if (types[i].is_object()) {
				types[i].get_to(cursor.Types[i]);
			}
		}
	}
}
void Menu::CursorToJson(json& cursorJson, const ThemeSettings::CursorSettings& cursor)
{
	json types = json::object();
	for (const auto& [key, type] : kCursorTypeKeys) {
		const auto& settings = cursor.Types[static_cast<size_t>(type)];
		if (!settings.File.empty() || settings.HotspotX != 0.0f || settings.HotspotY != 0.0f) {
			types[key] = settings;
		}
	}
	if (!types.empty()) {
		cursorJson["Types"] = types;
	}
}

// Pad FontRoles JSON array with defaults if shorter than FontRole::Count.
// Prevents deserialization failure when loading old settings with fewer font roles.
static void SanitizeFontRolesJson(json& themeJson)
{
	if (!themeJson.contains("FontRoles") || !themeJson["FontRoles"].is_array())
		return;

	auto& fontRoles = themeJson["FontRoles"];
	const size_t expected = static_cast<size_t>(Menu::FontRole::Count);

	if (fontRoles.size() < expected) {
		auto defaults = Menu::ThemeSettings{}.FontRoles;
		for (size_t i = fontRoles.size(); i < expected; ++i) {
			fontRoles.push_back(defaults[i]);
		}
	}
}

// Serialize palette as named color map. Resilient to ImGui enum reordering.
void Menu::PaletteToJson(json& themeJson, const std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	json colors = json::object();
	for (int i = 0; i < ImGuiCol_COUNT; i++)
		colors[ImGui::GetStyleColorName(i)] = palette[i];
	themeJson["Colors"] = colors;
}

// Deserialize palette from named color map (preferred) or legacy positional array.
void Menu::PaletteFromJson(const json& themeJson, std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	ThemeSettings defaults;
	palette = defaults.FullPalette;

	auto loadVec4 = [](const json& c) -> ImVec4 {
		if (c.is_array() && c.size() >= 4)
			return c.get<ImVec4>();
		return ImVec4(0, 0, 0, 0);
	};

	if (themeJson.contains("Colors") && themeJson["Colors"].is_object()) {
		// Named color map: look up each color by ImGui's style color name
		const auto& colors = themeJson["Colors"];
		for (int i = 0; i < ImGuiCol_COUNT; i++) {
			const char* name = ImGui::GetStyleColorName(i);
			if (colors.contains(name) && colors[name].is_array())
				palette[i] = loadVec4(colors[name]);
		}
	} else if (themeJson.contains("FullPalette") && themeJson["FullPalette"].is_array()) {
		// Legacy positional array
		const auto& arr = themeJson["FullPalette"];

		if (arr.size() == 55) {
			// Migrate from ImGui 1.90 (55 entries) to 1.92+ (62 entries).
			// Tab/TabHovered swapped, 7 new slots inserted mid-enum.
			for (int i = 0; i <= 32; i++)
				palette[i] = loadVec4(arr[i]);
			// [33] InputTextCursor: stays default
			palette[34] = loadVec4(arr[34]);  // old TabHovered â†’ TabHovered
			palette[35] = loadVec4(arr[33]);  // old Tab â†’ Tab (swapped)
			palette[36] = loadVec4(arr[35]);  // old TabActive â†’ TabSelected
			// [37] TabSelectedOverline: stays default
			palette[38] = loadVec4(arr[36]);  // old TabUnfocused â†’ TabDimmed
			palette[39] = loadVec4(arr[37]);  // old TabUnfocusedActive â†’ TabDimmedSelected
			// [40] TabDimmedSelectedOverline: stays default
			for (int i = 38; i <= 48; i++)
				palette[i + 3] = loadVec4(arr[i]);
			// [52] TextLink: stays default
			palette[53] = loadVec4(arr[49]);  // TextSelectedBg
			// [54] TreeLines: stays default
			palette[55] = loadVec4(arr[50]);  // DragDropTarget
			// [56] DragDropTargetBg: stays default
			// [57] UnsavedMarker: stays default
			for (int i = 51; i <= 54; i++)
				palette[i + 7] = loadVec4(arr[i]);
		} else {
			// Direct positional load (matching or close size)
			size_t count = std::min(arr.size(), static_cast<size_t>(ImGuiCol_COUNT));
			for (size_t i = 0; i < count; i++)
				palette[i] = loadVec4(arr[i]);
		}
	}
}

std::optional<Menu::FontRole> Menu::ResolveFontRole(std::string_view key)
{
	for (size_t i = 0; i < FontRoleDescriptors.size(); ++i) {
		if (FontRoleDescriptors[i].key == key) {
			return static_cast<FontRole>(i);
		}
	}
	return std::nullopt;
}

std::string Menu::BuildFontSignature(float baseFontSize) const
{
	return MenuFonts::BuildFontSignature(settings.Theme, baseFontSize);
}

const Menu::ThemeSettings::FontRoleSettings& Menu::GetDefaultFontRole(FontRole role)
{
	return MenuFonts::GetDefaultRole(role);
}

Menu::~Menu()
{  // Release icon textures if loaded
	uiIcons.saveSettings.Release();
	uiIcons.loadSettings.Release();
	uiIcons.deleteSettings.Release();
	uiIcons.clearCache.Release();
	uiIcons.logo.Release();
	uiIcons.featureSettingRevert.Release();
	uiIcons.discord.Release();
	uiIcons.characters.Release();
	uiIcons.display.Release();
	uiIcons.grass.Release();
	uiIcons.lighting.Release();
	uiIcons.sky.Release();
	uiIcons.landscape.Release();
	uiIcons.water.Release();
	uiIcons.debug.Release();
	uiIcons.materials.Release();
	uiIcons.postProcessing.Release();

	uiIcons.search.Release();

	Util::CursorLoader::Shutdown();

	// Clean up blur resources
	BackgroundBlur::Cleanup();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;
}

void Menu::Load(json& o_json)
{
	// Store current Theme state before loading config
	auto currentTheme = settings.Theme;

	settings = o_json;
	// Quality values are persisted user input and may come from older or
	// hand-edited JSON. Keep every array-indexed tier inside the public contract.
	settings.RendererQuality = std::clamp(settings.RendererQuality, 0, 3);
	settings.LightingQuality = std::clamp(settings.LightingQuality, 0, 3);
	settings.MaterialsQuality = std::clamp(settings.MaterialsQuality, 0, 3);
	settings.AtmosphereQuality = std::clamp(settings.AtmosphereQuality, 0, 3);
	settings.WaterQuality = std::clamp(settings.WaterQuality, 0, 3);
	settings.TerrainVegetationQuality = std::clamp(settings.TerrainVegetationQuality, 0, 3);
	settings.CharactersQuality = std::clamp(settings.CharactersQuality, 0, 3);
	settings.CameraQuality = std::clamp(settings.CameraQuality, 0, 3);
	// Migrate the inherited first-party theme to PIXL's product skin. Explicit
	// alternate themes remain respected; only the old generic default changes.
	if (settings.SelectedThemePreset == "Default")
		settings.SelectedThemePreset = "PIXL";

	// Restore Theme - don't load it from config, only from theme preset files
	settings.Theme = currentTheme;

	// Migration: Convert legacy uint32_t keys to InputCombo vectors if needed
	auto migrateKey = [](json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_number_integer()) {
			uint32_t legacyKey = j[keyName].get<uint32_t>();
			target.clear();
			if (legacyKey != 0) {
				target.push_back(InputCombo::Keyboard(legacyKey));
			}
		}
	};

	migrateKey(o_json, "ToggleKey", settings.ToggleKey);
	migrateKey(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	migrateKey(o_json, "EffectToggleKey", settings.EffectToggleKey);
	migrateKey(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	migrateKey(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	migrateKey(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	migrateKey(o_json, "ScreenshotKey", settings.ScreenshotKey);

	// Helper for new smart serialization with error handling
	auto loadComboList = [](const json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_array()) {
			try {
				InputCombo::ComboList::from_json(j[keyName], target);
			} catch (const std::exception& e) {
				logger::warn("Failed to load combo list '{}': {}, using default", keyName, e.what());
				// Leave target unchanged (keeps default or migrated value)
			}
		}
	};

	loadComboList(o_json, "ToggleKey", settings.ToggleKey);
	loadComboList(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	loadComboList(o_json, "EffectToggleKey", settings.EffectToggleKey);
	loadComboList(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	loadComboList(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	loadComboList(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	loadComboList(o_json, "ScreenshotKey", settings.ScreenshotKey);

	// Older configurations used Page Down for both the Tuner and shader-block
	// stepping. Preserve the user's Tuner binding and migrate only the colliding
	// developer bindings to the new Shift+Page Up/Down defaults.
	if (settings.ShaderBlockPrevKey == settings.ToggleKey)
		settings.ShaderBlockPrevKey = { InputCombo::Keyboard(VK_SHIFT), InputCombo::Keyboard(VK_PRIOR) };
	if (settings.ShaderBlockNextKey == settings.ToggleKey)
		settings.ShaderBlockNextKey = { InputCombo::Keyboard(VK_SHIFT), InputCombo::Keyboard(VK_NEXT) };

	// Legacy support: If old config has Theme data and no SelectedThemePreset, load it
	if (o_json.contains("Theme") && o_json["Theme"].is_object() && settings.SelectedThemePreset.empty()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		if (o_json["Theme"].contains("Cursor") && o_json["Theme"]["Cursor"].is_object()) {
			CursorFromJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
		}
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found while loading settings, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}
		logger::info("Loaded legacy Theme data from config (no SelectedThemePreset)");
	}

	// Apply the PIXL product theme on first launch if no theme is selected
	if (!settings.FirstTimeSetupCompleted && settings.SelectedThemePreset.empty()) {
		// Ensure default themes are created/available
		CreateDefaultThemes();

		// Load the PIXL theme and mark it as selected to prevent override
		if (LoadThemePreset("PIXL")) {
			settings.SelectedThemePreset = "PIXL";  // Mark as selected to prevent State::LoadTheme override
			logger::info("Applied PIXL theme on first launch");
		} else {
			logger::warn("Failed to load PIXL theme on first launch");
		}
	} else if (!settings.SelectedThemePreset.empty()) {
		// Load the previously selected theme preset (including custom themes)
		if (LoadThemePreset(settings.SelectedThemePreset)) {
			logger::info("Loaded saved theme preset: {}", settings.SelectedThemePreset);
		} else {
			logger::warn("Failed to load saved theme preset '{}', falling back to PIXL", settings.SelectedThemePreset);
			if (LoadThemePreset("PIXL")) {
				settings.SelectedThemePreset = "PIXL";
			}
		}
	}
}

void Menu::Save(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;

	// Save all settings except Theme values
	// Theme values should only be saved in theme preset files, not in the main config
	o_json = settings;

	// Remove Theme object from config, only keep SelectedThemePreset
	o_json.erase("Theme");

	// Manually save input combos using the smart serializer
	InputCombo::ComboList::to_json(o_json["ToggleKey"], settings.ToggleKey);
	InputCombo::ComboList::to_json(o_json["SkipCompilationKey"], settings.SkipCompilationKey);
	InputCombo::ComboList::to_json(o_json["EffectToggleKey"], settings.EffectToggleKey);
	InputCombo::ComboList::to_json(o_json["OverlayToggleKey"], settings.OverlayToggleKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockPrevKey"], settings.ShaderBlockPrevKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockNextKey"], settings.ShaderBlockNextKey);
	InputCombo::ComboList::to_json(o_json["ScreenshotKey"], settings.ScreenshotKey);
}

void Menu::LoadTheme(json& o_json)
{
	if (o_json["Theme"].is_object()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		if (o_json["Theme"].contains("Cursor") && o_json["Theme"]["Cursor"].is_object()) {
			CursorFromJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
		}
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
		Util::CursorLoader::MigrateLegacyCursorSettings(settings.Theme);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}

		// Apply background blur enabled state from theme
		BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);
	}
}
void Menu::SaveTheme(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;

	if (!Util::ValidateFont(settings.Theme.FontName)) {
		const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
		logger::warn("Font '{}' not found during save, falling back to default font '{}'",
			settings.Theme.FontName, defaults.File);
		settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
		settings.Theme.FontName = defaults.File;
	}

	o_json["Theme"] = settings.Theme;
	PaletteToJson(o_json["Theme"], settings.Theme.FullPalette);
	CursorToJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
}

std::vector<std::string> Menu::DiscoverThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	if (themeManager) {
		themeManager->DiscoverThemes();
		return themeManager->GetThemeNames();
	}
	return {};
}

bool Menu::LoadThemePreset(const std::string& themeName)
{
	if (themeName.empty()) {
		// Empty theme name means custom/user theme
		settings.SelectedThemePreset = "";
		return true;
	}

	auto themeManager = ThemeManager::GetSingleton();
	json themeSettings;

	if (themeManager->LoadTheme(themeName, themeSettings)) {
		// Create a backup of current theme in case loading fails
		ThemeSettings backupTheme = settings.Theme;
		bool hasFontRoles = themeSettings.contains("FontRoles");

		SanitizeFontRolesJson(themeSettings);

		try {
			settings.Theme = themeSettings;
			PaletteFromJson(themeSettings, settings.Theme.FullPalette);
			if (themeSettings.contains("Cursor") && themeSettings["Cursor"].is_object()) {
				CursorFromJson(themeSettings["Cursor"], settings.Theme.Cursor);
			}

			MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
			Util::CursorLoader::MigrateLegacyCursorSettings(settings.Theme);
			auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
			if (!Util::ValidateFont(bodyRole.File)) {
				const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
				logger::warn("Font '{}' from theme '{}' not found, falling back to default font '{}'",
					bodyRole.File, themeName, defaults.File);
				settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
				settings.Theme.FontName = defaults.File;
			}

			settings.SelectedThemePreset = themeName;

			// Schedule deferred font reload if font has changed
			if (settings.Theme.FontName != cachedFontName) {
				pendingFontReload = true;
			}

			// Schedule deferred icon reload to apply theme-specific icon overrides
			pendingIconReload = true;
			pendingCursorReload = true;

			// Apply background blur enabled state from theme
			BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);

			logger::info("Applied theme preset: {}", themeName);
			return true;
		} catch (const std::exception& e) {
			logger::warn("Error loading theme '{}': {}", themeName, e.what());
			settings.Theme = backupTheme;
			return false;
		}
	} else {
		logger::warn("Failed to load theme preset: {}", themeName);
		return false;
	}
}

void Menu::CreateDefaultThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->CreateDefaultThemeFiles();
}

void Menu::Init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// Immediately override ImGui's fallback style with the PIXL product theme.
	// This prevents hardcoded ImGui defaults from ever showing through
	auto* themeManager = ThemeManager::GetSingleton();
	json defaultThemeSettings;
	if (themeManager->LoadTheme("PIXL", defaultThemeSettings)) {
		// Temporarily create a minimal theme structure to apply defaults
		json tempSettings;
		tempSettings["Theme"] = defaultThemeSettings;
		LoadTheme(tempSettings);
		logger::info("Applied PIXL.json theme immediately after ImGui context creation");
	} else {
		logger::warn("Could not load PIXL.json theme - trying direct force application");
		// Last resort: apply the same PIXL palette directly.
		ThemeManager::ForceApplyDefaultTheme();
	}

	// Re-apply the selected developer skin after PIXL is initialized.
	if (!settings.SelectedThemePreset.empty()) {
		auto themeManagerSingleton = ThemeManager::GetSingleton();
		if (themeManagerSingleton && !themeManagerSingleton->IsDiscovered()) {
			themeManagerSingleton->DiscoverThemes();
		}

		if (!LoadThemePreset(settings.SelectedThemePreset)) {
			logger::warn("Failed to re-apply preset '{}' during Menu::Init. Keeping PIXL.", settings.SelectedThemePreset);
		} else {
			logger::info("Re-applied preset '{}' during Menu::Init", settings.SelectedThemePreset);
		}
	}

	auto& imgui_io = ImGui::GetIO();
	imgui_io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
	imgui_io.ConfigDockingWithShift = settings.RequireShiftToDock;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

	cachedIniPath = Util::PathHelpers::GetImGuiIniPath().string();
	imgui_io.IniFilename = cachedIniPath.c_str();

	// Register settings handler to persist display size for cross-session resolution change detection
	ImGuiSettingsHandler handler{};
	handler.TypeName = "PIXLRenderer";
	handler.TypeHash = ImHashStr("PIXLRenderer");
	handler.UserData = &lastDisplaySize;
	handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* { return (void*)1; };
	handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* h, void*, const char* line) {
		float w, ht;
		if (sscanf(line, "DisplaySize=%f,%f", &w, &ht) == 2)
			*static_cast<float2*>(h->UserData) = { w, ht };
	};
	handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
		auto& ds = ImGui::GetIO().DisplaySize;
		buf->appendf("[%s][Data]\nDisplaySize=%g,%g\n\n", h->TypeName, ds.x, ds.y);
	};
	ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

	ThemeManager::ReloadFont(*this, cachedFontSize);

	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!FAILED(globals::d3d::device->QueryInterface(dxgiDevice.put()))) {
			winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
			if (!FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()))) {
				dxgiAdapter->QueryInterface(dxgiAdapter3.put());
			}
		}
	}
	// Load UI icons
	if (!Util::InitializeMenuIcons(this)) {
		logger::warn("Menu::Init() - Failed to load UI icons. Will fallback to text buttons");
	}

	Util::CursorLoader::Reload(this);

	// Initialize background blur system
	if (!BackgroundBlur::Initialize()) {
		logger::warn("Menu::Init() - Failed to initialize background blur system");
	}

	BuildCategoryCounts();

	initialized = true;
}

/**
 * @brief Main UI rendering coordinator for the PIXL Renderer menu
 *
 * This method serves as the primary entry point for rendering the entire menu interface.
 * It handles window setup, docking configuration, and delegates rendering to specialized
 * renderer components for better separation of concerns.
 *
 * The method manages:
 * - ImGui docking space and window positioning
 * - Focus change handling
 * - Dynamic window flags based on docking state
 * - Header, navigation tabs, and settings panels coordination
 */
void Menu::DrawSettings()
{
	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}

	// Apply theme styling with universal contrast enhancement
	ThemeManager::SetupImGuiStyle(*this);

	ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	const auto layoutCond =
		resetLayout
			? ImGuiCond_Always
			: ImGuiCond_FirstUseEver;

	if (settings.AdvancedMode) {
		const ImVec2 viewportSize =
			ImGui::GetMainViewport()->Size;

		float fixedHeight =
			viewportSize.y *
			PIXLUI::Layout::ViewportHeightRatio;
		float fixedWidth =
			fixedHeight *
			(PIXLUI::Layout::ReferenceWidth /
			 PIXLUI::Layout::ReferenceHeight);

		const float maxWidth =
			viewportSize.x *
			PIXLUI::Layout::ViewportWidthSafety;
		if (fixedWidth > maxWidth) {
			fixedWidth = maxWidth;
			fixedHeight =
				fixedWidth /
				(PIXLUI::Layout::ReferenceWidth /
				 PIXLUI::Layout::ReferenceHeight);
		}

		const ImVec2 fixedSize(
			std::floor(fixedWidth),
			std::floor(fixedHeight));

		PIXLUI::SetReferenceScale(
			fixedSize.y /
			PIXLUI::Layout::ReferenceHeight);

		ImGui::SetNextWindowPos(
			ImGui::GetMainViewport()->GetCenter(),
			layoutCond,
			ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(
			fixedSize,
			layoutCond);
		ImGui::SetNextWindowSizeConstraints(
			fixedSize,
			ImGui::GetMainViewport()->WorkSize);
	} else {
		ImGui::SetNextWindowPos(
			Util::GetNativeViewportSizeScaled(0.5f),
			layoutCond,
			ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(
			ImVec2(
				Util::GetNativeViewportSizeScaled(0.72f).x,
				Util::GetNativeViewportSizeScaled(0.78f).y),
			layoutCond);
	}

	resetLayout = false;
	auto versionStr = Util::GetFormattedVersion(Plugin::VERSION);
	auto expectedTag = std::format("v{}", versionStr);
	auto displayTitle = Plugin::BUILD_DESCRIBE == expectedTag ? std::format("PIXL Renderer {}", versionStr) : std::format("PIXL Renderer {} [{}]", versionStr, Plugin::BUILD_DESCRIBE);
	// Use ### to keep a stable window ID regardless of build suffix, preserving docking state
	auto title = std::format("{}###PIXLRenderer", displayTitle);

	// PIXL's main experience is an intentionally stable floating canvas.
	ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar;

	// Advanced tuning is resizable; keep the reference scale independent of user
	// size so enlarging the canvas adds usable space rather than larger controls.

	// Only hide title bar when not docked.
	if (settings.AdvancedMode) {
		ImGui::PushStyleColor(ImGuiCol_WindowBg, PIXLUI::ToVec4(PIXLUI::Colors::Window));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PIXLUI::Scale(8.0f), PIXLUI::Scale(8.0f)));
	}
	Util::BeginWithRoundedClose(title.c_str(), &IsEnabled, windowFlags);
	{
		if (settings.AdvancedMode) {
			const ImVec2 windowMin = ImGui::GetWindowPos();
			const ImVec2 windowMax(
				windowMin.x + ImGui::GetWindowSize().x,
				windowMin.y + ImGui::GetWindowSize().y);
			PIXLUI::DrawWindowShell(windowMin, windowMax);
		}
		static float savedLookAt = -100.0f;

		if (settings.AdvancedMode) {
			const ImVec2 rootPos = ImGui::GetWindowPos();

			const ImVec2 headerStart(
				rootPos.x + PIXLUI::Ref(PIXLUI::Layout::TuneHeaderX),
				rootPos.y + PIXLUI::Ref(PIXLUI::Layout::TuneHeaderY));
			const ImVec2 headerSize(
				PIXLUI::Ref(PIXLUI::Layout::TuneHeaderWidth),
				PIXLUI::Ref(PIXLUI::Layout::TuneHeaderHeight));

			PIXLUI::DrawChrome(
				headerStart,
				ImVec2(headerStart.x + headerSize.x, headerStart.y + headerSize.y),
				PIXLUI::ChromeStyle::Header,
				true);

			const float brandMarkHeight =
				PIXLUI::Ref(34.0f);
			float brandTextX =
				headerStart.x +
				PIXLUI::Ref(22.0f);

			if (uiIcons.logo.texture &&
				uiIcons.logo.size.y > 0.0f) {
				const float brandMarkWidth =
					brandMarkHeight *
					(uiIcons.logo.size.x /
					 uiIcons.logo.size.y);

				ImGui::SetCursorScreenPos(
					ImVec2(
						headerStart.x +
							PIXLUI::Ref(20.0f),
						headerStart.y +
							PIXLUI::Ref(15.0f)));
				ImGui::Image(
					uiIcons.logo.texture,
					ImVec2(
						brandMarkWidth,
						brandMarkHeight));

				brandTextX +=
					brandMarkWidth +
					PIXLUI::Ref(12.0f);
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					brandTextX,
					headerStart.y +
						PIXLUI::Ref(20.0f)));
			{
				MenuFonts::FontRoleGuard titleFont(
					Menu::FontRole::Title);
				ImGui::SetWindowFontScale(1.18f);
				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::Text),
					"PIXL Renderer");
				ImGui::SetWindowFontScale(1.0f);
			}

			ImGui::SameLine(
				0.0f,
				PIXLUI::Ref(9.0f));
			{
				MenuFonts::FontRoleGuard subtext(
					Menu::FontRole::Subtext);
				ImGui::SetWindowFontScale(0.82f);
				ImGui::TextColored(
					PIXLUI::ToVec4(
						PIXLUI::Colors::CyanSoft),
					"VERSION 1.0.2");
				ImGui::SetWindowFontScale(1.0f);
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					headerStart.x + PIXLUI::Ref(978.0f),
					headerStart.y + PIXLUI::Ref(22.0f)));
			if (PIXLUI::ActionButton(
					"RESTORE",
					ImVec2(PIXLUI::Ref(124.0f), PIXLUI::Ref(32.0f)),
					false)) {
				globals::state->Load();
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					headerStart.x + PIXLUI::Ref(1114.0f),
					headerStart.y + PIXLUI::Ref(22.0f)));
			if (PIXLUI::ActionButton(
					"SAVE LOOK",
					ImVec2(PIXLUI::Ref(132.0f), PIXLUI::Ref(32.0f)),
					true)) {
				globals::state->Save();
				globals::state->SaveTheme();
				savedLookAt = static_cast<float>(ImGui::GetTime());
			}

			const ImVec2 commandStart(
				rootPos.x + PIXLUI::Ref(PIXLUI::Layout::TuneCommandX),
				rootPos.y + PIXLUI::Ref(PIXLUI::Layout::TuneCommandY));

			ImGui::SetCursorScreenPos(commandStart);
			if (PIXLUI::ActionButton(
					"RETURN TO PLAYER UI",
					ImVec2(PIXLUI::Ref(158.0f), PIXLUI::Ref(29.0f)),
					false)) {
				settings.AdvancedMode = false;
				settings.DeveloperMode = false;
				globals::state->Save();
			}

			ImGui::SetCursorScreenPos(
				ImVec2(
					commandStart.x + PIXLUI::Ref(173.0f),
					commandStart.y + PIXLUI::Ref(7.0f)));
			{
				MenuFonts::FontRoleGuard sub(Menu::FontRole::Subtext);
				ImGui::TextColored(
					PIXLUI::ToVec4(PIXLUI::Colors::TextDim),
					"PIXL WORKSHOP");
			}

		} else {
			// Public shell remains untouched in Pass A.
			PIXLUI::PanelScope productHeader(
				"##PIXLProductHeaderPanel",
				ImVec2(0, PIXLUI::Scale(68.0f)),
				true,
				ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar,
				true);
			if (productHeader && ImGui::BeginTable(
					"##PIXLProductHeader",
					2,
					ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
				ImGui::TableSetupColumn("Brand", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 276.0f * Util::GetUIScale());
				ImGui::TableNextColumn();

				// FINAL PUBLIC HEADER GEOMETRY
				// Keep the brand group visually centred in the 68px header
				// instead of accumulating SameLine/cursor offsets.
				const float uiScale =
					Util::GetUIScale();
				const float markHeight =
					34.0f * uiScale;
				const ImVec2 brandCellStart =
					ImGui::GetCursorScreenPos();
				const ImVec2 brandCellAvail =
					ImGui::GetContentRegionAvail();

				float brandX =
					brandCellStart.x +
					10.0f * uiScale;
				const float brandY =
					brandCellStart.y +
					std::max(
						0.0f,
						(brandCellAvail.y -
						 markHeight) *
							0.5f);

				if (uiIcons.logo.texture &&
					uiIcons.logo.size.y > 0.0f) {
					const float markWidth =
						markHeight *
						(uiIcons.logo.size.x /
						 uiIcons.logo.size.y);

					ImGui::SetCursorScreenPos(
						ImVec2(
							brandX,
							brandY));
					ImGui::Image(
						uiIcons.logo.texture,
						ImVec2(
							markWidth,
							markHeight));

					brandX +=
						markWidth +
						11.0f * uiScale;
				}

				ImGui::SetCursorScreenPos(
					ImVec2(
						brandX,
						brandY +
							5.0f * uiScale));
				{
					MenuFonts::FontRoleGuard titleFont(
						Menu::FontRole::Title);
					ImGui::SetWindowFontScale(1.14f);
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::Text),
						"PIXL Renderer");
					ImGui::SetWindowFontScale(1.0f);
				}

				ImGui::SameLine(
					0.0f,
					8.0f * uiScale);
				{
					MenuFonts::FontRoleGuard subtext(
						Menu::FontRole::Subtext);
					ImGui::SetWindowFontScale(0.80f);
					ImGui::TextColored(
						PIXLUI::ToVec4(
							PIXLUI::Colors::CyanSoft),
					"VERSION 1.0.2");
					ImGui::SetWindowFontScale(1.0f);
				}

				// Submit the full brand cell so the table owns the geometry
				// even though the visual contents above are positioned exactly.
				ImGui::SetCursorScreenPos(brandCellStart);
				ImGui::Dummy(
					ImVec2(
						brandCellAvail.x,
						brandCellAvail.y));

				ImGui::TableNextColumn();

				const ImVec2 actionCellStart =
					ImGui::GetCursorScreenPos();
				const ImVec2 actionCellAvail =
					ImGui::GetContentRegionAvail();
				const float restoreWidth =
					104.0f * uiScale;
				const float saveWidth =
					132.0f * uiScale;
				const float actionGap =
					10.0f * uiScale;
				const float actionHeight =
					30.0f * uiScale;
				const float actionWidth =
					restoreWidth +
					actionGap +
					saveWidth;
				const float actionX =
					actionCellStart.x +
					std::max(
						0.0f,
						(actionCellAvail.x -
						 actionWidth) *
							0.5f);
				const float actionY =
					actionCellStart.y +
					std::max(
						0.0f,
						(actionCellAvail.y -
						 actionHeight) *
							0.5f);

				ImGui::SetCursorScreenPos(
					ImVec2(
						actionX,
						actionY));
				if (PIXLUI::ActionButton(
						"RESTORE",
						ImVec2(
							restoreWidth,
							actionHeight),
						false)) {
					globals::state->Load();
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextWrapped(
						"Restores the last saved PIXL look.");

				ImGui::SameLine(
					0.0f,
					actionGap);

				if (PIXLUI::ActionButton(
						"SAVE LOOK",
						ImVec2(
							saveWidth,
							actionHeight),
						true)) {
					globals::state->Save();
					globals::state->SaveTheme();
					savedLookAt =
						static_cast<float>(
							ImGui::GetTime());
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextWrapped(
						"Saves every PIXL graphics and camera adjustment for future sessions.");

				ImGui::SetCursorScreenPos(actionCellStart);
				ImGui::Dummy(
					ImVec2(
						actionCellAvail.x,
						actionCellAvail.y));

				ImGui::EndTable();
			}
			ImGui::Spacing();
		}

		float footer_height = 0.0f;

		if (settings.AdvancedMode) {
			TuningWorkspaceRenderer::RenderFeatureList(
				footer_height, selectedFeatureMenu, featureSearch, pendingFeatureSelection,
				categoryExpansionStates,
				[this]() { DrawGeneralSettings(); },
				[this]() { DrawAdvancedSettings(); });
		} else {
			ImGui::BeginChild("##PIXLGraphics", ImVec2(0, -footer_height), false);
			PIXLRendererPage::Render();
			ImGui::EndChild();
		}

		// Draw global popups (needs to be called once per frame)
		Util::DrawClearShaderCacheConfirmation();
	}
	ImGui::End();
	if (settings.AdvancedMode) {
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
}

/**
 * @brief Renders the General settings tab content
 *
 * Delegates rendering to RuntimeSettingsRenderer for the general configuration panel,
 * which includes Shaders, Keybindings, and Interface sub-tabs. This method provides
 * the callback for key-to-string conversion while maintaining separation of concerns.
 */
void Menu::DrawGeneralSettings()
{
	// Prepare settings state for the renderer
	RuntimeSettingsRenderer::SettingsState state{
		.settingToggleKey = settingToggleKey,
		.settingsEffectsToggle = settingsEffectsToggle,
		.settingSkipCompilationKey = settingSkipCompilationKey,
		.settingOverlayToggleKey = settingOverlayToggleKey,
		.settingShaderBlockPrevKey = settingShaderBlockPrevKey,
		.settingShaderBlockNextKey = settingShaderBlockNextKey,
		.settingScreenshotKey = settingScreenshotKey
	};

	// Render settings using extracted component
	RuntimeSettingsRenderer::RenderGeneralSettings(state);
}

/**
 * @brief Renders the Advanced settings tab content
 *
 * Delegates rendering to WorkshopToolsRenderer for developer and advanced user
 * settings. Uses lambda callbacks to access private Menu methods while maintaining
 * encapsulation and proper separation of concerns.
 */
void Menu::DrawAdvancedSettings()
{
	WorkshopToolsRenderer::RenderAdvancedSettings(
		[this]() { DrawDisableAtBootSettings(); });
}

void Menu::DrawDisableAtBootSettings()
{
	auto state = globals::state;
	auto& disabledFeatures = state->GetDisabledFeatures();

	ImGui::Text("%s",
		T("menu.disable_at_boot_desc",
			"Select features to disable at boot. "
			"This is the same as deleting a feature.ini file. "
			"Restart will be required to reenable."));

	ImGui::Spacing();

	if (ImGui::CollapsingHeader(T("menu.features", "Features"), ImGuiTreeNodeFlags_DefaultOpen)) {
		// Prepare a sorted list of feature pointers
		auto featureList = RenderModule::GetModuleList();
		std::sort(featureList.begin(), featureList.end(), [](RenderModule* a, RenderModule* b) {
			return a->GetShortName() < b->GetShortName();
		});

		// Display sorted features using PIXL's own Sigil Latch control rather
		// than the stock square ImGui checkbox inherited from CS.
		for (auto* feature : featureList) {
			if (feature->IsHiddenUnreleased())
				continue;

			const std::string featureName =
				feature->GetShortName();

			bool isDisabled =
				disabledFeatures.contains(
					featureName) &&
				disabledFeatures[
					featureName];

			if (PIXLUI::LabeledToggle(
					featureName.c_str(),
					&isDisabled)) {
				disabledFeatures[
					featureName] =
						isDisabled;
			}
		}
	}
}

void Menu::DrawFooter()
{
	ImGui::TextDisabled("PIXL ENGINE DIAGNOSTICS");
	ImGui::SameLine();
	ImGui::TextDisabled("GPU  %s", globals::state->adapterDescription.c_str());
}

/**
 * @brief Main overlay rendering coordinator
 *
 * Delegates all overlay rendering to OverlayRenderer while providing necessary
 * callbacks for input processing, settings rendering, and key mapping. This method
 * serves as the bridge between Menu's state and the extracted overlay rendering logic.
 *
 * Handles input event processing, shader compilation status, feature overlays,
 * A/B testing, and ImGui frame management through the specialized renderer component.
 */
void Menu::DrawOverlay()
{
	// Only process reloads when ImGui is NOT in an active frame
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	bool canReload = ctx && !ctx->WithinFrameScope && ctx->WithinEndChildID == 0;

	// Process deferred font reload BEFORE any ImGui operations
	// This is the safest place to do font atlas modifications
	if (pendingFontReload && canReload) {
		// Call ReloadFont first - only clear flag if it succeeds
		if (ThemeManager::ReloadFont(*this, cachedFontSize)) {
			// Reload completed successfully
			pendingFontReload = false;
		} else {
			// Reload failed - keep flag true to retry next frame
			logger::warn("Menu::DrawOverlay() - Font reload failed, will retry next frame");
		}
	}

	// Process deferred icon reload BEFORE rendering
	if (pendingIconReload && canReload) {
		if (Util::IconLoader::InitializeMenuIcons(this)) {
			pendingIconReload = false;
		} else {
			// Icons are optional in PIXL's text-first interface. A missing optional
			// asset must never become a per-frame disk probe/log storm.
			pendingIconReload = false;
			logger::warn("Menu::DrawOverlay() - Optional icons unavailable; continuing with text controls");
		}
	}

	if (pendingCursorReload && canReload) {
		static bool loggedCursorReloadRetry = false;
		if (Util::CursorLoader::Reload(this)) {
			pendingCursorReload = false;
			loggedCursorReloadRetry = false;
		} else if (!loggedCursorReloadRetry) {
			logger::warn("Menu::DrawOverlay() - Cursor reload deferred (will retry when ready)");
			loggedCursorReloadRetry = true;
		}
	}

	OverlayRenderer::RenderOverlay(
		*this,
		[this]() { ProcessInputEventQueue(); },
		[this]() { DrawSettings(); },
		[](std::vector<InputCombo> keys) -> const char* {
			static std::string result_cache;
			result_cache = Util::Input::KeyIdToString(keys);
			return result_cache.c_str();
		},
		cachedFontSize,
		ThemeManager::ResolveFontSize(*this));
}

/**
 * @brief Processes queued input events
 *
 * This method handles the logic of routing input events to appropriate handlers:
 * - Keyboard and mouse events are processed directly for ImGui integration
 * - Includes key state normalization and stuck key detection/correction
 *
 * The method maintains thread safety through mutex protection of the input event queue.
 *
 * @note This method contains Menu-specific logic and state management that makes it
 *       inappropriate for extraction to a utility class.
 */
void Menu::ProcessInputEventQueue()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	ImGuiIO& io = ImGui::GetIO();
	for (auto& event : _keyEventQueue) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			io.AddInputCharacter(event.keyCode);
			continue;
		}
		if (event.device == RE::INPUT_DEVICE::kMouse) {
			logger::trace("Detect mouse scan code {} value {} pressed: {}", event.keyCode, event.value, event.IsPressed());
			if (event.keyCode > 7) {  // middle scroll
				io.AddMouseWheelEvent(0, event.value * (event.keyCode == 8 ? 1 : -1));
			} else {
				if (event.keyCode > 5)
					event.keyCode = 5;
				io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
			}
		}

		if (event.device == RE::INPUT_DEVICE::kGamepad &&
			event.eventType == RE::INPUT_EVENT_TYPE::kButton &&
			event.IsDown()) {
			std::uint32_t gamepadKey =
				SKSE::InputMap::GamepadMaskToKeycode(
					event.keyCode);

			if (gamepadKey ==
				SKSE::InputMap::kMaxMacros) {
				gamepadKey =
					event.keyCode;
			}

			if (TuningWorkspaceRenderer::
					IsDirectorPhotoModeActive()) {
				TuningWorkspaceRenderer::
					HandleDirectorGamepadInput(
						gamepadKey);
				continue;
			}

			if (TuningWorkspaceRenderer::
					HandleDirectorGamepadInput(
						gamepadKey)) {
				continue;
			}
		}

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = Util::Input::DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));

			if (TuningWorkspaceRenderer::
					IsDirectorPhotoModeActive()) {
				if (event.IsDown()) {
					TuningWorkspaceRenderer::
						HandleDirectorKeyboardInput(
							key);
				}

				// Director is modal. Do not run normal PIXL hotkeys or ImGui key
				// routing here. Hooks.cpp separately forwards only camera controls.
				continue;
			}

			if (event.IsDown() &&
				TuningWorkspaceRenderer::
					HandleDirectorKeyboardInput(
						key)) {
				continue;
			}

			// Alt+N is a deliberately fixed, discoverable release shortcut. It is
			// omitted from first-run setup to keep onboarding focused on navigation;
			// the launch reminder and Camera page advertise it when applicable.
			if (event.IsDown() && key == 'N' &&
				(GetAsyncKeyState(VK_MENU) & Constants::KEY_PRESSED_MASK)) {
				const std::string status = globals::pipeline::imageReconstruction
					.ToggleNeuralRenderingFromHotkey();
				if (auto* task = SKSE::GetTaskInterface()) {
					task->AddTask([status]() {
						RE::SendHUDMessage::ShowHUDMessage(status.c_str(), nullptr, true);
					});
				}
				_comboFiredKeys.insert(key);
				continue;
			}

			const bool wasCapturingHotkey = IsCapturingHotkeyInput();
			const bool allowSetupCloseKey = wasCapturingHotkey && LaunchExperienceRenderer::ShouldShowFirstTimeSetup() &&
			                                (key == VK_RETURN || key == VK_ESCAPE);

			// Dispatch bound hotkey actions for `key`. Combo bindings (modifier + key)
			// fire on key-down for responsiveness; single-key bindings fire on key-up.
			auto dispatchHotkeyActions = [this, key](bool combosOnly) {
				struct KeyAction
				{
					std::vector<InputCombo>& settingKey;
					std::function<void()> action;
				};
				auto shaderCache = globals::shaderCache;
				KeyAction keyActions[] = {
					{ settings.ToggleKey, [this]() {
						 if (!LaunchExperienceRenderer::ShouldShowFirstTimeSetup()) {
							 IsEnabled = !IsEnabled;
							 if (IsEnabled)
								 ImGui::GetIO().ClearInputKeys();  // Prevent toggle key from remaining "held" in ImGui after open.
						 }
					 } },
					{ settings.SkipCompilationKey, [this, shaderCache]() {
						 // ENTER SKYRIM converts foreground compilation into background
						 // compilation. OverlayRenderer observes this flag on the same
						 // frame and releases the exclusive compiler presentation.
						 if (shaderCache &&
							 !ShouldSwallowInput() &&
							 shaderCache->IsCompiling() &&
							 !shaderCache->backgroundCompilation &&
							 shaderCache->menuLoaded) {
							 shaderCache->backgroundCompilation = true;
							 logger::info("ENTER SKYRIM accepted: shader compilation continuing in background.");
						 }
					 } },
					{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnabled()); } },
					{ settings.ShaderBlockPrevKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(); } },
					{ settings.ShaderBlockNextKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(false); } },
					{ settings.OverlayToggleKey, []() { Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible; } },
					{ settings.ScreenshotKey, []() {
						 // END/Photo Mode belongs exclusively to Director capture.
						 if (TuningWorkspaceRenderer::IsDirectorPhotoModeActive())
							 return;

						 if (globals::pipeline::pixelCapture.loaded)
							 globals::pipeline::pixelCapture.captureRequested = true;
					 } },
				};
				for (const auto& ka : keyActions) {
					const bool isCombo = ka.settingKey.size() > 1;
					if (isCombo == combosOnly && InputCombo::MatchesKeyboardCombo(ka.settingKey, key)) {
						ka.action();
						return true;
					}
				}
				return false;
			};

			// Hardcoded Shift+Enter toggle for the CS menu (always available)
			if (event.IsDown() && key == VK_RETURN && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
				if (!LaunchExperienceRenderer::ShouldShowFirstTimeSetup()) {
					IsEnabled = !IsEnabled;
					if (IsEnabled)
						ImGui::GetIO().ClearInputKeys();
				}
				continue;
			}

			if (!event.IsPressed()) {
				// Skip key release if it was used to close the first-time setup dialog
				if (LaunchExperienceRenderer::ShouldSkipKeyRelease(key)) {
					io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), event.IsPressed());
					continue;
				}

				struct HotkeyAction
				{
					std::vector<InputCombo>* settingKey;
					bool* settingFlag;
					std::function<void(std::vector<InputCombo>)> action;
				};
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](std::vector<InputCombo> keys) {
						 settings.ToggleKey = keys;
						 settingToggleKey = false;
					 } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](std::vector<InputCombo> keys) { settings.SkipCompilationKey = keys; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](std::vector<InputCombo> keys) { settings.EffectToggleKey = keys; settingsEffectsToggle = false; } },
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](std::vector<InputCombo> keys) { settings.OverlayToggleKey = keys; settingOverlayToggleKey = false; } },
					{ &settings.ShaderBlockPrevKey, &settingShaderBlockPrevKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockPrevKey = keys; settingShaderBlockPrevKey = false; } },
					{ &settings.ShaderBlockNextKey, &settingShaderBlockNextKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockNextKey = keys; settingShaderBlockNextKey = false; } },
					{ &settings.ScreenshotKey, &settingScreenshotKey, [this](std::vector<InputCombo> keys) { settings.ScreenshotKey = keys; settingScreenshotKey = false; } },
				};
				bool handled = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						// During first-time setup, don't capture Enter or Escape as hotkeys
						// These keys are reserved for closing the dialog, unless we are recording a modifier
						if (LaunchExperienceRenderer::ShouldShowFirstTimeSetup() && (key == VK_RETURN || key == VK_ESCAPE)) {
							// Do not stop capture here, just let it pass through to the UI
							// The UI code in LaunchExperienceRenderer checks for Enter/Escape and completes setup
							*(h.settingFlag) = false;  // Cancel hotkey capture mode
							handled = true;
							break;
						}

						// Ignore modifier-only key releases during recording
						bool isModifier = (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
										   key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
										   key == VK_MENU || key == VK_LMENU || key == VK_RMENU);

						if (isModifier) {
							handled = true;
							break;
						}

						// Capture modifiers + key
						std::vector<InputCombo> combo;

						// Add active modifiers to combo
						if ((GetAsyncKeyState(VK_CONTROL) & Constants::KEY_PRESSED_MASK) &&
							key != VK_CONTROL && key != VK_LCONTROL && key != VK_RCONTROL)
							combo.push_back(InputCombo::Keyboard(VK_CONTROL));
						if ((GetAsyncKeyState(VK_SHIFT) & Constants::KEY_PRESSED_MASK) &&
							key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT)
							combo.push_back(InputCombo::Keyboard(VK_SHIFT));
						if ((GetAsyncKeyState(VK_MENU) & Constants::KEY_PRESSED_MASK) &&
							key != VK_MENU && key != VK_LMENU && key != VK_RMENU)
							combo.push_back(InputCombo::Keyboard(VK_MENU));

						combo.push_back(InputCombo::Keyboard(key));

						h.action(combo);
						handled = true;
						break;
					}
				}
				if (!handled) {
					// Single-key hotkeys fire on key-up; combos already fired on key-down.
					// If this key's key-down already fired a combo, suppress the single-key
					// binding so releasing the modifier first doesn't trigger it as well.
					if (_comboFiredKeys.erase(key) == 0)
						dispatchHotkeyActions(false);
				}

				// Escape closes the single PIXL interface.
				if (key == VK_ESCAPE) {
					if (IsEnabled) {
						IsEnabled = false;
					}
				}
			} else if (event.IsDown() && !wasCapturingHotkey) {
				// Fire combo hotkeys on the key-down transition so they respond on
				// press rather than release. IsDown() (not IsPressed()) ensures we
				// trigger only once instead of every frame the key is held.
				if (dispatchHotkeyActions(true))
					_comboFiredKeys.insert(key);
			}

			// Don't forward hotkey events to ImGui when input is captured (prevents e.g. End key scrolling the feature list)
			// SkipCompilationKey (ESC) is excluded â€” ESC must reach ImGui for menu/dialog close.
			const std::vector<InputCombo>* hotkeys[] = {
				&settings.ToggleKey, &settings.EffectToggleKey,
				&settings.OverlayToggleKey, &settings.ShaderBlockPrevKey, &settings.ShaderBlockNextKey,
				&settings.ScreenshotKey
			};
			bool isHotkey = ShouldSwallowInput() && std::any_of(std::begin(hotkeys), std::end(hotkeys),
														[key](const auto* combo) { return InputCombo::MatchesKeyboardCombo(*combo, key); });

			// Always forward key-up events. Suppress key-down during active hotkeys,
			// and during hotkey capture except setup close keys (Enter/Escape).
			const bool isKeyDown = event.IsPressed();
			const bool suppressForwarding = isKeyDown && (isHotkey || (wasCapturingHotkey && !allowSetupCloseKey));
			if (!suppressForwarding) {
				// DirectInput loses key-up events after alt-tab; validate against OS state.
				bool pressed = isKeyDown && (GetAsyncKeyState(key) & Constants::KEY_PRESSED_MASK);
				io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), pressed);

				if (key == VK_LCONTROL || key == VK_RCONTROL)
					io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
				else if (key == VK_LSHIFT || key == VK_RSHIFT)
					io.AddKeyEvent(ImGuiMod_Shift, pressed);
				else if (key == VK_LMENU || key == VK_RMENU)
					io.AddKeyEvent(ImGuiMod_Alt, pressed);
			}
		}
	}

	_keyEventQueue.clear();
}

bool Menu::IsCapturingHotkeyInput() const
{
	return settingToggleKey || settingSkipCompilationKey || settingsEffectsToggle ||
	       settingOverlayToggleKey || settingShaderBlockPrevKey || settingShaderBlockNextKey || settingScreenshotKey;
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusChanged()
{
	// Solves the alt+tab stuck issue, but disables tab after tabbing back in.
	if (const auto& inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
		if (const auto& device = inputMgr->GetKeyboard()) {
			device->ClearInputState();
		}
	}
	// Allows tab to work again after alt+tabbing back in.
	ImGui::GetIO().ClearInputKeys();
}

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	for (auto it = *a_events; it; it = it->next) {
		// Accept button, char, and thumbstick events
		if (it->GetEventType() != RE::INPUT_EVENT_TYPE::kButton &&
			it->GetEventType() != RE::INPUT_EVENT_TYPE::kChar &&

			it->GetEventType() != RE::INPUT_EVENT_TYPE::kThumbstick

			)  // we do not care about non button/char/thumbstick events
			continue;

		if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			addToEventQueue(KeyEvent(static_cast<RE::ButtonEvent*>(it)));
		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
			addToEventQueue(KeyEvent(static_cast<CharEvent*>(it)));

		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
			addToEventQueue(KeyEvent(static_cast<RE::ThumbstickEvent*>(it)));
		}
	}
}

bool Menu::ShouldSwallowInput()
{
	return IsEnabled || LaunchExperienceRenderer::ShouldShowFirstTimeSetup();
}

bool Menu::ShouldLockGameInputForDirector() const
{
	return TuningWorkspaceRenderer::IsDirectorPhotoModeActive();
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}

/**
 * @brief Builds category counts for feature organization and display
 *
 * Iterates through all loaded features and counts how many features belong to each
 * category. This information is used for UI organization and displaying category
 * statistics in the feature navigation interface.
 *
 * @note Only counts features that are both loaded and configured to appear in the menu.
 */
void Menu::BuildCategoryCounts()
{
	const std::vector<RenderModule*>& features = RenderModule::GetModuleList();
	categoryCounts.clear();
	// Get the category of each feature, and increment the count for that category
	for (auto& feature : features) {
		if (feature->IsInMenu() && (feature->loaded || PIXLRendererPage::GetPlacement(feature->GetShortName()).has_value())) {
			const auto category = PIXLRendererPage::GetMenuCategory(feature->GetShortName(), feature->GetCategory());
			categoryCounts[std::string(category)]++;
		}
	}
}
