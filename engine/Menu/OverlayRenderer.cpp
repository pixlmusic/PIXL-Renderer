#include "OverlayRenderer.h"
#include "BackgroundBlur.h"
#include "LaunchExperienceRenderer.h"
#include "ThemeManager.h"
#include "TuningWorkspaceRenderer.h"
#include "PIXLStyle.h"

#include <dxgi.h>
#include <array>
#include <format>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <winrt/base.h>

#include "RenderModule.h"
#include "PipelineHealth.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "ModuleVersions.h"
#include "Menu/CursorLoader.h"
#include "Menu/Fonts.h"
#include "ShaderCache.h"
#include "State.h"
#include "Renderer/WorldBenchmark.h"
#include "Util.h"

#include "Modules/PulseProfiler.h"
namespace
{
	std::unordered_map<ImGuiID, float> s_windowOverlapAlpha;
	constexpr std::array<const char*, 30> kCompilerTips{
		"Compiling shaders. The dragons have declined to help.",
		"Page Down opens PIXL Renderer. No console commands or ancient scrolls required.",
		"Home opens Photo Mode. Even a mudcrab deserves a good portrait.",
		"In Photo Mode, End takes the photo. Try to catch the dragon before it catches you.",
		"Photo Mode's Insert key opens effects. It does not insert another dragon.",
		"Cached shaders can be reused. Deleting them makes this reunion longer.",
		"New shader combinations may compile later. Skyrim has a lot of ways to draw a bucket.",
		"Off, TAA, FSR and supported DLSS are available in Quick Setup.",
		"DLSS needs supported NVIDIA hardware. Sweet-talking the checkbox will not work.",
		"Fog Height sets the layer's altitude. Height Falloff decides how quickly it thins above it.",
		"Want taller fog? Lower Height Falloff before blaming the weather wizard.",
		"Sky Protection preserves sky detail without clearing fog from the mountains.",
		"Automatic Weather Atmosphere makes rain and snow denser. Pack an imaginary coat.",
		"WindowLife's outdoor backgrounds are scenery, not a shortcut through the wall.",
		"Interior View: Background Emission brightens the scenery seen through interior windows.",
		"Exterior room glow has its own control. Your neighbour's lamps are safe.",
		"Day and night window artwork blends with game time. No one has to change the paintings.",
		"More sharpness is not always more detail. Sometimes it is just a louder pine needle.",
		"Bloom is seasoning. The sun does not need to become soup.",
		"Save a favourite look before experimenting. Future you appreciates backups.",
		"A preloaded cache helps startup, but unfamiliar shader combinations may still need work.",
		"These tips cannot compile shaders faster. We asked.",
		"Some surfaces need different shaders for rain, snow and lighting. Even rocks have options.",
		"Your GPU is preparing the scenery. The cheese wheels remain Skyrim's responsibility.",
		"Photo Mode pauses the scene. A rare chance for everyone to stop shouting.",
		"If a setting looks wrong, compare one change at a time. Science, but with mammoths.",
		"Frame generation adds frames; it does not make every effect free.",
		"Good reflections need surroundings. A shiny helmet has a lot to think about.",
		"The release ships defaults, not somebody else's UserGraphics file. Your settings stay yours.",
		"Nearly ready is a mood, not a benchmark. The progress counter has the actual numbers."
	};
	constexpr ImGuiWindowFlags SKIP_WINDOW_FLAGS = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
	constexpr const char* MAIN_WINDOW_PREFIX = "PIXL Renderer";

	bool IsMainWindow(ImGuiWindow* win) { return win->Name && strncmp(win->Name, MAIN_WINDOW_PREFIX, strlen(MAIN_WINDOW_PREFIX)) == 0; }

	void DrawShaderCompilationFailures(uint64_t failed, const Menu::ThemeSettings& themeSettings)
	{
		if (failed) {
			ImGui::TextColored(themeSettings.StatusPalette.Error,
				"%llu shader(s) could not be prepared. See PIXLRenderer.log",
				static_cast<unsigned long long>(failed));

			if (PipelineHealth::HasPotentialShaderModifyingFeatures()) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("overlay.modified_features", "Features that may have modified shaders detected. Check RenderModule Issues in the Menu."));
			}
		}
	}

	bool IsVisibleRootWindow(ImGuiWindow* win)
	{
		if (!win || !win->WasActive || win->Hidden)
			return false;
		return !(win->ParentWindow && !win->DockIsActive) && !(win->Flags & SKIP_WINDOW_FLAGS);
	}

	// Patches DrawList background vertices for windows involved in overlap.
	void PatchOverlappingWindowBackgrounds()
	{
		auto* ctx = ImGui::GetCurrentContext();
		if (!ctx)
			return;

		using C = ThemeManager::Constants;
		const float dt = ImGui::GetIO().DeltaTime;

		struct WinInfo
		{
			ImGuiWindow* win;
			ImRect rect;
		};
		std::vector<WinInfo> windows;
		for (int i = 0; i < ctx->Windows.Size; i++) {
			auto* win = ctx->Windows[i];
			if (IsVisibleRootWindow(win))
				windows.push_back({ win, win->Rect() });
		}

		std::unordered_set<ImGuiID> overlapping;
		for (size_t i = 0; i < windows.size(); i++)
			for (size_t j = i + 1; j < windows.size(); j++)
				if (windows[i].rect.Overlaps(windows[j].rect)) {
					auto* a = windows[i].win;
					auto* b = windows[j].win;
					// Main CS window never dims; other windows yield to it
					if (IsMainWindow(a))
						overlapping.insert(b->ID);
					else if (IsMainWindow(b))
						overlapping.insert(a->ID);
					else
						overlapping.insert(a->FocusOrder > b->FocusOrder ? a->ID : b->ID);
				}

		const ImU32 bgRGB = ImGui::GetColorU32(ImGuiCol_WindowBg) & ~IM_COL32_A_MASK;

		for (auto& [win, rect] : windows) {
			const float target = overlapping.count(win->ID) ? C::OVERLAP_MIN_ALPHA : 0.0f;
			float& alpha = s_windowOverlapAlpha[win->ID];
			const float speed = (target > alpha) ? C::OVERLAP_FADEIN_SPEED : C::OVERLAP_FADEOUT_SPEED;
			alpha += (target - alpha) * (std::min)(1.0f, dt * speed);

			if (alpha < C::OVERLAP_ALPHA_EPSILON) {
				alpha = 0.0f;
				continue;
			}

			auto* dl = win->DrawList;
			if (!dl || dl->VtxBuffer.Size == 0)
				continue;

			// Clamp background rect vertex alpha (contiguous bgRGB block at start of DrawList)
			const ImU32 minA = static_cast<ImU32>(alpha * 255.0f);
			for (int v = 0; v < dl->VtxBuffer.Size; v++) {
				auto& vtx = dl->VtxBuffer[v];
				if ((vtx.col & ~IM_COL32_A_MASK) != bgRGB)
					break;
				ImU32 a = (vtx.col >> IM_COL32_A_SHIFT) & 0xFF;
				if (a > 0 && a < minA)
					vtx.col = bgRGB | (minA << IM_COL32_A_SHIFT);
			}
		}

		// Prune stale entries
		for (auto it = s_windowOverlapAlpha.begin(); it != s_windowOverlapAlpha.end();)
			it->second < C::OVERLAP_ALPHA_EPSILON ? it = s_windowOverlapAlpha.erase(it) : ++it;
	}
}  // namespace

void OverlayRenderer::RenderOverlay(
	Menu& menu,
	const std::function<void()>& processInputEventQueue,
	const std::function<void()>& drawSettings,
	const std::function<const char*(std::vector<InputCombo>)>& keyIdToString,
	float& cachedFontSize,
	float currentFontSize)
{
	processInputEventQueue();
	TuningWorkspaceRenderer::UpdateTunerInspection();
	PIXLRenderer::WorldBenchmark::GetSingleton().Update();

	if (ShouldSkipRendering()) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		s_windowOverlapAlpha.clear();
		return;
	}

	HandleFontReload(menu, cachedFontSize, currentFontSize);
	InitializeImGuiFrame(menu);

	RenderShaderCompilationStatus(keyIdToString);
	RenderShaderBlockingStatus();

	if (menu.IsEnabled || LaunchExperienceRenderer::ShouldShowFirstTimeSetup()) {
		const bool inspectionMoving =
			TuningWorkspaceRenderer::IsDirectorInspectionMoving();
		ImGui::GetIO().MouseDrawCursor = !inspectionMoving;
		if (menu.IsEnabled && !inspectionMoving) {
			drawSettings();
		}
	} else {
		ImGui::GetIO().MouseDrawCursor = menu.IsProfilerInteractive();
	}

	TuningWorkspaceRenderer::RenderDirectorPhotoModeOverlay();

	RenderFeatureOverlays();
	RenderFirstTimeSetupOverlay();

	const bool compilerExclusive =
		globals::shaderCache &&
		globals::shaderCache->IsCompiling() &&
		!globals::shaderCache->backgroundCompilation &&
		!menu.IsEnabled;

	if (compilerExclusive) {
		// Compiler startup owns the full presentation layer. Do not inspect or
		// mutate stale ImGui windows from previous frames.
		s_windowOverlapAlpha.clear();
	} else {
		PatchOverlappingWindowBackgrounds();
	}

	FinalizeImGuiFrame();
}

bool OverlayRenderer::ShouldSkipRendering()
{
	auto shaderCache = globals::shaderCache;
	auto* menu = Menu::GetSingleton();
	const bool foregroundCompilation =
		shaderCache &&
		shaderCache->IsCompiling() &&
		!shaderCache->backgroundCompilation;
	const auto failed = shaderCache ? shaderCache->GetCurrentFailedCount() : 0;
	const bool hide = shaderCache ? shaderCache->IsHideErrors() : true;

	return !(foregroundCompilation ||
			 (menu && menu->IsEnabled) ||
			 TuningWorkspaceRenderer::IsDirectorPhotoModeActive() ||
			 (shaderCache && failed && !hide && !shaderCache->backgroundCompilation) ||
			 globals::pipeline::pulseProfiler.settings.ShowInOverlay ||
			 LaunchExperienceRenderer::ShouldShowFirstTimeSetup() ||
			 LaunchExperienceRenderer::ShouldShowControlReminder());
}

void OverlayRenderer::HandleFontReload(Menu& menu, float& cachedFontSize, float currentFontSize)
{
	bool fontSizeChanged = std::abs(cachedFontSize - currentFontSize) > ThemeManager::Constants::FONT_CACHE_EPSILON;
	std::string desiredSignature = menu.BuildFontSignature(currentFontSize);
	bool signatureChanged = desiredSignature != menu.cachedFontSignature;

	if (fontSizeChanged || signatureChanged) {
		if (!ThemeManager::ReloadFont(menu, cachedFontSize)) {
			logger::warn("OverlayRenderer::HandleFontReload() - Font reload failed");
		}
	}
}

void OverlayRenderer::InitializeImGuiFrame(Menu& menu)
{
	// Start the Dear ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	const float displayW = static_cast<float>(desc.BufferDesc.Width);
	const float displayH = static_cast<float>(desc.BufferDesc.Height);
	Util::UpdateImGuiInput(desc.OutputWindow, displayW, displayH);

	ImGui::NewFrame();

	// Detect display size change (cross-session via ini handler, mid-session via member)
	const float2 currentDisplaySize{ displayW, displayH };
	if (menu.lastDisplaySize.x > 0.f && menu.lastDisplaySize != currentDisplaySize) {
		logger::info("Display size changed: {}x{} -> {}x{}, resetting window layout",
			menu.lastDisplaySize.x, menu.lastDisplaySize.y, currentDisplaySize.x, currentDisplaySize.y);
		menu.resetLayout = true;
	}
	menu.lastDisplaySize = currentDisplaySize;

	ThemeManager::SetupImGuiStyle(menu);
}

void OverlayRenderer::RenderShaderCompilationStatus(const std::function<const char*(std::vector<InputCombo>)>& keyIdToString)
{
	auto shaderCache = globals::shaderCache;
	if (!shaderCache)
		return;

	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;

	uint64_t totalShaders = shaderCache->GetTotalTasks();
	uint64_t compiledShaders = shaderCache->GetCompletedTasks();
	uint64_t diskCachedShaders = shaderCache->GetDiskHitTasks();
	uint64_t builtThisRun = compiledShaders > diskCachedShaders ? compiledShaders - diskCachedShaders : 0;

	auto state = globals::state;
	auto& themeSettings = Menu::GetSingleton()->GetTheme();

	const float percent = totalShaders > 0 ? std::clamp(static_cast<float>(compiledShaders) / static_cast<float>(totalShaders), 0.0f, 1.0f) : 0.0f;

	// Foreground compilation owns the startup presentation only until the user
	// chooses ENTER SKYRIM. Once backgroundCompilation is set, compilation may
	// continue, but the full-screen compiler card/backdrop must immediately stop.
	if (shaderCache->IsCompiling() &&
		!shaderCache->backgroundCompilation) {
		if (Menu::GetSingleton()->IsEnabled)
			return;

		ImGuiViewport* viewport =
			ImGui::GetMainViewport();

		// Deterministic opaque compositor backdrop. This overwrites every pixel
		// behind the compiler card each frame and prevents persistent swap-chain
		// / UI-buffer contents from appearing as repeated previous windows.
		ImGui::GetBackgroundDrawList(
			viewport)->AddRectFilled(
				viewport->Pos,
				ImVec2(
					viewport->Pos.x +
						viewport->Size.x,
					viewport->Pos.y +
						viewport->Size.y),
				IM_COL32(
					4,
					6,
					8,
					255));

		const float cardWidth =
			std::clamp(
				viewport->WorkSize.x *
					0.30f,
				480.0f * scale,
				570.0f * scale);
		const bool canEnterSkyrim =
			!shaderCache->backgroundCompilation &&
			shaderCache->menuLoaded;

		float cardHeight =
			462.0f * scale;

		// Grow deterministically for optional diagnostic/status rows. This keeps
		// the compiler surface scroll-free without using ImGui's AlwaysAutoResize
		// + size-constraint path during renderer startup.
		if (state && state->IsDeveloperMode())
			cardHeight += 24.0f * scale;
		if (failed && !hide)
			cardHeight += 34.0f * scale;
		if (canEnterSkyrim)
			cardHeight += 30.0f * scale;

		ImGui::SetNextWindowPos(
			ImVec2(
				viewport->WorkPos.x +
					viewport->WorkSize.x *
						0.5f,
				viewport->WorkPos.y +
					viewport->WorkSize.y *
						0.5f),
			ImGuiCond_Always,
			ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(
			ImVec2(
				cardWidth,
				cardHeight),
			ImGuiCond_Always);

		ImGui::PushStyleVar(
			ImGuiStyleVar_WindowPadding,
			ImVec2(
				24.0f * scale,
				24.0f * scale));
		ImGui::PushStyleVar(
			ImGuiStyleVar_WindowRounding,
			2.0f * scale);
		ImGui::PushStyleVar(
			ImGuiStyleVar_WindowBorderSize,
			0.0f);
		ImGui::PushStyleColor(
			ImGuiCol_WindowBg,
			PIXLUI::ToVec4(
				PIXLUI::Colors::Inset));
		ImGui::PushStyleColor(
			ImGuiCol_Border,
			ImVec4(0, 0, 0, 0));

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_NoSavedSettings;

		if (!ImGui::Begin(
				"ShaderCompilationInfo",
				nullptr,
				flags)) {
			ImGui::End();
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar(3);
			return;
		}

		const ImVec2 panelMin =
			ImGui::GetWindowPos();
		const ImVec2 panelMax(
			panelMin.x +
				ImGui::GetWindowSize().x,
			panelMin.y +
				ImGui::GetWindowSize().y);

		PIXLUI::DrawChrome(
			panelMin,
			panelMax,
			PIXLUI::ChromeStyle::Raised,
			false);

		// Eased clockwise tracer around the panel. The progress bar remains the
		// authoritative completion indicator; this supplies only ambient motion.
		{
			ImDrawList* chromeList = ImGui::GetWindowDrawList();
			const float phase = std::fmod(static_cast<float>(ImGui::GetTime()), 5.2f) / 5.2f;
			const float eased = phase * phase * (3.0f - 2.0f * phase);
			const float inset = 2.0f * scale;
			const ImVec2 a(panelMin.x + inset, panelMin.y + inset);
			const ImVec2 b(panelMax.x - inset, panelMax.y - inset);
			const float width = std::max(1.0f, b.x - a.x);
			const float height = std::max(1.0f, b.y - a.y);
			const float perimeter = 2.0f * (width + height);
			auto pointOnBorder = [&](float distance) {
				distance = std::fmod(distance + perimeter, perimeter);
				if (distance < width)
					return ImVec2(a.x + distance, a.y);
				if (distance < width + height)
					return ImVec2(b.x, a.y + distance - width);
				if (distance < 2.0f * width + height)
					return ImVec2(b.x - (distance - width - height), b.y);
				return ImVec2(a.x, b.y - (distance - 2.0f * width - height));
			};
			const float head = eased * perimeter;
			const float trail = perimeter * 0.14f;
			for (int i = 0; i < 16; ++i) {
				const float t0 = static_cast<float>(i) / 16.0f;
				const float t1 = static_cast<float>(i + 1) / 16.0f;
				const float fade = (1.0f - t0) * (1.0f - t0);
				chromeList->AddLine(
					pointOnBorder(head - trail * t0),
					pointOnBorder(head - trail * t1),
					IM_COL32(67, 220, 232, static_cast<int>(190.0f * fade)),
					2.0f * scale);
			}
		}

		auto centerText =
			[](const char* text,
			   ImU32 color =
				   PIXLUI::Colors::TextMuted) {
				const float x =
					ImGui::GetCursorPosX() +
					std::max(
						0.0f,
						(ImGui::GetContentRegionAvail().x -
						 ImGui::CalcTextSize(text).x) *
							0.5f);

				ImGui::SetCursorPosX(x);
				ImGui::TextColored(
					PIXLUI::ToVec4(color),
					"%s",
					text);
			};

		const auto& logo =
			Menu::GetSingleton()->uiIcons.logo;

		if (logo.texture &&
			logo.size.y > 0.0f) {
			const float logoHeight =
				84.0f * scale;
			const float logoWidth =
				logoHeight *
				(logo.size.x /
				 logo.size.y);

			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				std::max(
					0.0f,
					(ImGui::GetContentRegionAvail().x -
					 logoWidth) *
						0.5f));

			ImGui::Image(
				logo.texture,
				ImVec2(
					logoWidth,
					logoHeight));
		} else {
			centerText(
				"◆",
				PIXLUI::Colors::Text);
		}

		ImGui::Dummy(
			ImVec2(
				0,
				9.0f * scale));

		{
			MenuFonts::FontRoleGuard titleFont(
				Menu::FontRole::Heading);
			ImGui::SetWindowFontScale(1.16f);
			centerText(
				"Renderer",
				PIXLUI::Colors::Text);
			ImGui::SetWindowFontScale(1.0f);
		}

		ImGui::SetWindowFontScale(0.82f);
		const std::string versionText = std::format("VERSION {}", Plugin::DISPLAY_VERSION.data());
		centerText(
			versionText.c_str(),
			PIXLUI::Colors::CyanSoft);
		ImGui::SetWindowFontScale(1.0f);

		ImGui::Dummy(
			ImVec2(
				0,
				14.0f * scale));

		centerText(
			"PREPARING PIXL",
			PIXLUI::Colors::TextMuted);

		ImGui::Dummy(
			ImVec2(
				0,
				7.0f * scale));

		const uint64_t tipEpoch =
			static_cast<uint64_t>(
				ImGui::GetTime() /
				6.0);
		const uint64_t tipSeed =
			tipEpoch +
			totalShaders *
				0x9E3779B185EBCA87ull;
		const char* tip =
			kCompilerTips[
				tipSeed %
				kCompilerTips.size()];

		centerText(
			tip,
			PIXLUI::Colors::TextDim);

		ImGui::Dummy(
			ImVec2(
				0,
				18.0f * scale));

		const ImVec2 barPos =
			ImGui::GetCursorScreenPos();
		const float barWidth =
			ImGui::GetContentRegionAvail().x;
		const float barHeight =
			5.0f * scale;

		ImDrawList* drawList =
			ImGui::GetWindowDrawList();

		drawList->AddRectFilled(
			barPos,
			ImVec2(
				barPos.x + barWidth,
				barPos.y + barHeight),
			IM_COL32(
				47,
				57,
				62,
				230));

		if (percent > 0.0f) {
			drawList->AddRectFilled(
				barPos,
				ImVec2(
					barPos.x +
						barWidth *
							percent,
					barPos.y +
						barHeight),
				PIXLUI::Colors::Cyan);
		}

		ImGui::Dummy(
			ImVec2(
				barWidth,
				barHeight));

		ImGui::Dummy(
			ImVec2(
				0,
				7.0f * scale));

		const auto progressText =
			fmt::format(
				"{:2.0f}%",
				percent * 100.0f);

		centerText(
			progressText.c_str(),
			PIXLUI::Colors::Text);

		const auto cacheProgressText =
			fmt::format(
				"DISK CACHE  {:L}    BUILT NOW  {:L}",
				diskCachedShaders,
				builtThisRun);
		centerText(
			cacheProgressText.c_str(),
			PIXLUI::Colors::TextDim);

		if (state->IsDeveloperMode()) {
			const auto stats =
				shaderCache->
					GetShaderStatsString(
						false);
			centerText(
				stats.c_str(),
				PIXLUI::Colors::TextDim);
		}

		if (canEnterSkyrim) {
			ImGui::Dummy(
				ImVec2(
					0,
					7.0f * scale));

			auto continueText =
				fmt::format(
					"{}  =  ENTER SKYRIM",
					keyIdToString(
						Menu::GetSingleton()
							->GetSettings()
							.SkipCompilationKey));

			centerText(
				continueText.c_str(),
				PIXLUI::Colors::CyanBright);
		}

		if (failed && !hide) {
			ImGui::TextColored(
				PIXLUI::ToVec4(
					PIXLUI::Colors::Warning),
				"%u shader%s failed - details are available after compilation.",
				failed,
				failed == 1 ? "" : "s");
		}

		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
		return;
	}

	if (!shaderCache->IsCompiling() &&
		failed &&
		!hide &&
		(!shaderCache->backgroundCompilation || Menu::GetSingleton()->IsEnabled)) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		if (!ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::End();
			return;
		}

		DrawShaderCompilationFailures(failed, themeSettings);
		ImGui::End();
	}
}



void OverlayRenderer::RenderFeatureOverlays()
{
	// Load overlays. Pulse Profiler is a first-class PIXL diagnostic surface and
	// receives the same signal language as Renderer > Profiling.
	for (RenderModule* feat :
		 RenderModule::GetModuleList()) {
		if (!feat ||
			!feat->loaded)
			continue;

		auto* overlay =
			dynamic_cast<OverlayFeature*>(
				feat);

		if (!overlay)
			continue;

		// Pulse Profiler owns its PIXL styling and size constraints internally.
		// Other overlays retain their established rendering path.
		overlay->DrawOverlay();
	}
}

void OverlayRenderer::FinalizeImGuiFrame()
{
	if (auto* menu = Menu::GetSingleton();
		menu && menu->GetSettings().Theme.UseCustomCursor && Util::CursorLoader::GetLoadedCount() > 0) {
		Util::CursorLoader::DrawCustomCursor(*menu);
	}

	ImGui::Render();

	const bool compilerExclusive =
		globals::shaderCache &&
		globals::shaderCache->IsCompiling() &&
		!globals::shaderCache->backgroundCompilation &&
		!Menu::GetSingleton()->IsEnabled;

	// Startup compilation is intentionally presented over a deterministic
	// Dragonsteel backdrop. Sampling the game/UI backbuffer here can feed PIXL's
	// previous ImGui frame back into itself on some swap-chain paths.
	if (!compilerExclusive)
		BackgroundBlur::RenderBackgroundBlur();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData());
}

void OverlayRenderer::RenderFirstTimeSetupOverlay()
{
	if (LaunchExperienceRenderer::ShouldShowFirstTimeSetup()) {
		LaunchExperienceRenderer::RenderFirstTimeSetupDialog();
	} else if (LaunchExperienceRenderer::ShouldShowControlReminder()) {
		LaunchExperienceRenderer::RenderControlReminder();
	}
}

void OverlayRenderer::RenderShaderBlockingStatus()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!shaderCache || !state || !state->IsDeveloperMode() || shaderCache->blockedKey.empty()) {
		return;
	}

	const float scale = Util::GetUIScale();
	const float pos = ThemeManager::Constants::OVERLAY_WINDOW_POSITION * scale;

	// Stack below shader compilation window if visible
	float yPos = pos;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			yPos = shaderWin->Pos.y + shaderWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	// Also stack below water cache overlay if visible
	if (auto* waterWin = ImGui::FindWindowByName("UWCacheCreationInfo")) {
		if (waterWin->Active && waterWin->Pos.y + waterWin->Size.y > yPos) {
			yPos = waterWin->Pos.y + waterWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}
	ImGui::SetNextWindowPos(ImVec2(pos, yPos));
	if (!ImGui::Begin("ShaderBlockingInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	Util::Text::Error(T("overlay.shader_blocking_active", "Shader Blocking Active"));
	ImGui::Text("Blocked: %s", shaderCache->blockedKey.c_str());

	// Try to get more details from active shaders
	auto activeShaders = shaderCache->GetActiveShaders();

	// Find the index of the blocked shader in the active list (or show N/A if not found)
	size_t blockedIndex = 0;
	bool foundBlocked = false;
	for (size_t i = 0; i < activeShaders.size(); ++i) {
		if (activeShaders[i].key == shaderCache->blockedKey) {
			blockedIndex = i + 1;  // 1-based indexing for display
			foundBlocked = true;
			break;
		}
	}

	if (foundBlocked) {
		ImGui::Text("Index: %zu/%zu", blockedIndex, activeShaders.size());
	} else {
		ImGui::Text("Index: N/A (%zu active)", activeShaders.size());
	}

	for (const auto& shader : activeShaders) {
		if (shader.key == shaderCache->blockedKey) {
			ImGui::Text("Type: %s | Class: %s | Descriptor: 0x%X",
				magic_enum::enum_name(shader.shaderType).data(),
				magic_enum::enum_name(shader.shaderClass).data(),
				shader.descriptor);
			break;
		}
	}

	ImGui::End();
}
