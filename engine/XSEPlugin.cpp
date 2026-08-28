#include "Deferred.h"
#include "Modules/ImageReconstruction.h"
#include "FrameAnnotations.h"
#include "Globals.h"
#include "Hooks.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "SceneSettingsManager.h"
#include "ShaderCache.h"
#include "State.h"

#include "ENB/ENBSeriesAPI.h"

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

namespace
{
	std::atomic_bool s_dataLoadedFinalized{ false };

	void FinalizeRendererDataLoaded()
	{
		if (globals::game::quitGame.load(std::memory_order_acquire)) {
			logger::info("Game was closed, skipping feature DataLoaded methods");
			return;
		}

		if (s_dataLoadedFinalized.exchange(true, std::memory_order_acq_rel))
			return;

		auto shaderCache = globals::shaderCache;
		if (shaderCache->IsDiskCache())
			shaderCache->WriteDiskCacheInfo();

		RenderModule::ForEachLoadedModule("DataLoaded", [](RenderModule* feature) { feature->DataLoaded(); });
		logger::info("Renderer DataLoaded finalization complete");
	}

	void FinalizeRendererDataLoadedAfterCompilation()
	{
		auto shaderCache = globals::shaderCache;
		if (!shaderCache->IsCompiling() || shaderCache->backgroundCompilation.load(std::memory_order_acquire)) {
			FinalizeRendererDataLoaded();
			return;
		}

		// Do not block the SKSE DataLoaded message handler. Blocking it stops
		// Skyrim's frame pump immediately after the compiler card first appears,
		// leaving only the game's loading spinner visible while worker threads keep
		// compiling. A lightweight waiter preserves the existing foreground gate,
		// while the main/render threads continue presenting the progress overlay.
		logger::info("Foreground shader compilation active; keeping the PIXL compiler panel live until the gate is released");
		std::thread([shaderCache]() {
			while (shaderCache->IsCompiling() &&
			       !shaderCache->backgroundCompilation.load(std::memory_order_acquire) &&
			       !globals::game::quitGame.load(std::memory_order_acquire)) {
				std::this_thread::sleep_for(100ms);
			}

			if (globals::game::quitGame.load(std::memory_order_acquire)) {
				logger::info("Game was closed, skipping deferred feature DataLoaded methods");
				return;
			}

			if (auto* taskInterface = SKSE::GetTaskInterface()) {
				taskInterface->AddTask([]() { FinalizeRendererDataLoaded(); });
			} else {
				logger::error("Unable to finalize renderer DataLoaded state: SKSE task interface is unavailable");
			}
		}).detach();
	}
}

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	// Keep the standalone renderer from co-loading the retired modular host. The
	// name is assembled so PIXL's own binaries and source contain no legacy brand.
	const auto retiredHostName = std::string("Community") + "Shaders.dll";
	const auto legacyDLL = Util::PathHelpers::GetDataPath() / "SKSE" / "Plugins" / retiredHostName;
	std::error_code legacyError;
	if (std::filesystem::exists(legacyDLL, legacyError)) {
		logger::critical("A conflicting legacy shader-hook DLL is installed beside PIXLRenderer.dll.");
		MessageBoxW(nullptr,
			L"PIXL Renderer found a conflicting legacy shader-hook DLL.\n\n"
			L"Disable or remove the other renderer, then launch Skyrim again. "
			L"PIXL Renderer already contains the complete rendering pipeline.",
			L"PIXL Renderer - installation conflict", MB_OK | MB_ICONERROR);
		return false;
	}
	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);
	return Load();
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
		{
			if (errors.empty()) {
				Deferred::Hooks::Install();
				Hooks::Install();
				EngineFix::InstallOnPostPostLoadFixes();
				FrameAnnotations::OnPostPostLoad();

				auto shaderCache = globals::shaderCache;

				// Run feature PostPostLoad() first so features can disable themselves if needed
				RenderModule::ForEachLoadedModule("PostPostLoad", [](RenderModule* feature) { feature->PostPostLoad(); });

				// Register scene settings event handler (Interior Only transitions)
				SceneSettingsManager::MenuOpenCloseEventHandler::Register();

				// Now validate disk cache after features have had a chance to modify their state
				shaderCache->ValidateDiskCache();

				if (shaderCache->UseFileWatcher())
					shaderCache->StartFileWatcher();
			}

			break;
		}
	case SKSE::MessagingInterface::kDataLoaded:
		{
			for (auto it = errors.begin(); it != errors.end(); ++it) {
				auto& errorMessage = *it;
				RE::DebugMessageBox(std::format("PIXL Renderer\n{}, will disable the renderer", errorMessage).c_str());
			}

			if (errors.empty()) {
				globals::OnDataLoaded();
				EngineFix::InstallOnDataLoadedFixes();
				FrameAnnotations::OnDataLoaded();

				auto shaderCache = globals::shaderCache;
				shaderCache->menuLoaded = true;
				FinalizeRendererDataLoadedAfterCompilation();
			}

			break;
		}
	}
}

bool Load()
{
	if (ENB_API::RequestENBAPI()) {
		logger::info("ENB detected, disabling all hooks and features");
		return true;
	}

	auto privateProfileRedirectorVersion = Util::GetDllVersion(L"Data/SKSE/Plugins/PrivateProfileRedirector.dll");
	if (privateProfileRedirectorVersion.has_value() && privateProfileRedirectorVersion.value().compare(REL::Version(0, 6, 2)) == std::strong_ordering::less) {
		stl::report_and_fail("Old version of PrivateProfileRedirector detected, 0.6.2+ required if using it."sv);
	}

	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", MessageHandler);

	Util::PathHelpers::MigrateLegacyPluginState();
	globals::OnInit();
	globals::ReInit();

	auto state = globals::state;

	// Initialize i18n system (loads English fallback and discovers available locales)
	I18n::GetSingleton()->Init();

	state->Load();
	state->LoadTheme();  // Load theme settings from SettingsTheme.json

	// Initialize theme system - create default themes and discover existing ones
	globals::menu->CreateDefaultThemes();  // Creates JSON files if they don't exist
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->DiscoverThemes();  // Discover all available themes

	auto log = spdlog::default_logger();
	log->set_level(state->GetLogLevel());

	const std::array incompatibleDLLs = {
		L"Data/SKSE/Plugins/ShaderTools.dll",
		L"Data/SKSE/Plugins/SSEShaderTools.dll",
		L"Data/SKSE/Plugins/SkyrimUpscaler.dll",
		L"Data/SKSE/Plugins/EVLaS.dll",
		L"Data/SKSE/Plugins/AELAS.dll",
		L"Data/SKSE/Plugins/SSEReShadeHelper.dll",
		L"Data/SKSE/Plugins/TAASharpen.dll",
		L"Data/SKSE/Plugins/NVIDIA_Reflex.dll",
		L"Data/SKSE/Plugins/MARA.dll",
		L"Data/SKSE/Plugins/NativeWaterLightStabilizer.dll"
	};

	for (const auto dll : incompatibleDLLs) {
		if (LoadLibrary(dll)) {
			auto errorMessage = std::format("Incompatible DLL {} detected", stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
			logger::error("{}", errorMessage);
			errors.push_back(errorMessage);
		}
	}

	auto pushMissingDllError = [&](std::string_view dllName) {
		auto errorMessage = std::format("Required DLL {} was missing", dllName);
		logger::error("{}", errorMessage);
		errors.push_back(errorMessage);
	};

	if (!LoadLibrary(L"Data/SKSE/Plugins/EngineFixes.dll")) {
		pushMissingDllError(stl::utf16_to_utf8(L"Data/SKSE/Plugins/EngineFixes.dll").value_or("<unicode conversion error>"s));
	}

	// Empty RequiredDLLs array, if necessary we can add a dll here in the future without needing to modify the plugin loading logic.
	const std::array<LPCWSTR, 0> requiredDLLs{};

	for (const auto dll : requiredDLLs) {
		if (!LoadLibrary(dll)) {
			pushMissingDllError(stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
		}
	}

	if (errors.empty()) {
		Hooks::InstallEarlyHooks();
		logger::info("Calling feature Load methods");
		RenderModule::ForEachLoadedModule("Load", [](RenderModule* feature) { feature->Load(); });
	}

	return true;
}
