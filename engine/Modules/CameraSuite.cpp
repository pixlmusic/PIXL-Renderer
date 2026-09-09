#include "CameraSuite.h"

#include "PCH.h"

#include "Buffer.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "LinearLightCore.h"
#include "Menu.h"
#include "RainResponse.h"
#include "ShaderCache.h"
#include "SkyBounce.h"
#include "State.h"
#include "ImageReconstruction.h"
#include "Util.h"
#include <algorithm>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <WICTextureLoader.h>

#define I18N_KEY_PREFIX "feature.camera_suite."

// Win11 24H2 display config types. Compat_ prefix avoids collision with SDK enum members.
typedef enum
{
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_SDR = 0,
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG = 1,
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR = 2
} Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE;

typedef struct Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2
{
	DISPLAYCONFIG_DEVICE_INFO_HEADER header;
	union
	{
		struct
		{
			UINT32 advancedColorSupported: 1;
			UINT32 advancedColorActive: 1;
			UINT32 reserved1: 1;
			UINT32 advancedColorLimitedByPolicy: 1;
			UINT32 highDynamicRangeSupported: 1;
			UINT32 highDynamicRangeUserEnabled: 1;
			UINT32 wideColorSupported: 1;
			UINT32 wideColorUserEnabled: 1;
			UINT32 reserved: 24;
		};
		UINT32 value;
	};
	DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
	UINT32 bitsPerColorChannel;
	Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE activeColorMode;
} Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2;

static constexpr DISPLAYCONFIG_DEVICE_INFO_TYPE kDisplayConfigGetAdvancedColorInfo2 =
	static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);

// HDR display detection
// Credits: Luma Framework by Filippo Tarpini (MIT License)
// https://github.com/Filoppi/Luma-Framework/blob/f1fbc2a36f2d24fd551721ce90f26821a8e754c1/Source/Core/utils/display.hpp
namespace
{
	// Returns the GDI device name for the swap chain's output via GetContainingOutput.
	// Returns false if the output cannot be determined (e.g. Streamline wraps the swap chain).
	bool GetSwapChainOutputDeviceName(IDXGISwapChain* swapChain, WCHAR (&outDeviceName)[32])
	{
		winrt::com_ptr<IDXGIOutput> output;
		if (FAILED(swapChain->GetContainingOutput(output.put())))
			return false;
		DXGI_OUTPUT_DESC desc{};
		if (FAILED(output->GetDesc(&desc)))
			return false;
		wcsncpy_s(outDeviceName, desc.DeviceName, _TRUNCATE);
		// DeviceName is ASCII (e.g. "\\.\DISPLAY1") â€” safe to log as narrow string
		char narrowName[32]{};
		WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, narrowName, sizeof(narrowName), nullptr, nullptr);
		logger::debug("[HDR] Swap chain output device: {}", narrowName);
		return true;
	}

	bool GetDisplayConfigPathInfo(IDXGISwapChain* swapChain, DISPLAYCONFIG_PATH_INFO& outPathInfo)
	{
		WCHAR deviceName[32]{};
		if (!GetSwapChainOutputDeviceName(swapChain, deviceName))
			return false;

		uint32_t pathCount, modeCount;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
			return false;

		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
			return false;

		for (auto& pathInfo : paths) {
			// DISPLAYCONFIG_SOURCE_IN_USE skips inactive sources (disconnected displays)
			if (!(pathInfo.flags & DISPLAYCONFIG_PATH_ACTIVE) ||
				!(pathInfo.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE))
				continue;

			// QDC_ONLY_ACTIVE_PATHS excludes virtual-mode paths; no index selection needed
			DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
			sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			sourceName.header.size = sizeof(sourceName);
			sourceName.header.adapterId = pathInfo.sourceInfo.adapterId;
			sourceName.header.id = pathInfo.sourceInfo.id;
			if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
				if (wcscmp(sourceName.viewGdiDeviceName, deviceName) == 0) {
					outPathInfo = pathInfo;
					return true;
				}
			}
		}
		return false;
	}

	bool IsHDRSupportedAndEnabled(IDXGISwapChain* swapChain, bool& supported, bool& enabled)
	{
		supported = false;
		enabled = false;

		DISPLAYCONFIG_PATH_INFO pathInfo{};
		if (!GetDisplayConfigPathInfo(swapChain, pathInfo)) {
			// GetContainingOutput fails under frame-gen wrappers. Fall back to enumerating
			// the device adapter's outputs by HMONITOR. Only detects active HDR, not capable.
			HWND outputWindow = nullptr;
			DXGI_SWAP_CHAIN_DESC scDescFull{};
			if (SUCCEEDED(swapChain->GetDesc(&scDescFull)))
				outputWindow = scDescFull.OutputWindow;

			HMONITOR hMonitor = outputWindow ? MonitorFromWindow(outputWindow, MONITOR_DEFAULTTONEAREST) : nullptr;
			if (!hMonitor) {
				logger::warn("[HDR] HDR detection failed - cannot determine monitor from swap chain OutputWindow");
				return false;
			}

			// Enumerate outputs on the device's own adapter; avoids touching other GPUs.
			winrt::com_ptr<IDXGIDevice> dxgiDevice;
			winrt::com_ptr<IDXGIAdapter> adapter;
			if (globals::d3d::device &&
				SUCCEEDED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))) &&
				SUCCEEDED(dxgiDevice->GetAdapter(adapter.put()))) {
				for (UINT oi = 0;; ++oi) {
					winrt::com_ptr<IDXGIOutput> out;
					HRESULT hr = adapter->EnumOutputs(oi, out.put());
					if (hr == DXGI_ERROR_NOT_FOUND)
						break;
					if (FAILED(hr)) {
						logger::debug("[HDR] EnumOutputs failed: hr=0x{:08X}", static_cast<unsigned>(hr));
						break;
					}
					DXGI_OUTPUT_DESC outDesc{};
					if (FAILED(out->GetDesc(&outDesc)) || outDesc.Monitor != hMonitor)
						continue;
					winrt::com_ptr<IDXGIOutput6> out6;
					if (SUCCEEDED(out->QueryInterface(IID_PPV_ARGS(out6.put())))) {
						DXGI_OUTPUT_DESC1 desc1{};
						if (SUCCEEDED(out6->GetDesc1(&desc1))) {
							enabled = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
							supported = enabled;
							logger::debug("[HDR] DXGI fallback detection: colorSpace={}", static_cast<int>(desc1.ColorSpace));
							return true;
						}
					}
				}
			}
			logger::warn("[HDR] HDR detection failed - cannot determine monitor (Streamline/frame-gen?)");
			return false;
		}

		// Try Windows 11 24H2+ API first - directly reports HDR hardware capability
		// Credits: renodx by clshortfuse (MIT License)
		// https://github.com/clshortfuse/renodx/blob/01f3739685ba8f850d82fb1a11a5ec1104e6b1b8/src/games/hollowknight-silksong/addon.cpp#L545
		Compat_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
		colorInfo2.header.type = kDisplayConfigGetAdvancedColorInfo2;
		colorInfo2.header.size = sizeof(colorInfo2);
		colorInfo2.header.adapterId = pathInfo.targetInfo.adapterId;
		colorInfo2.header.id = pathInfo.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&colorInfo2.header) == ERROR_SUCCESS) {
			supported = colorInfo2.highDynamicRangeSupported != 0;
			enabled = colorInfo2.activeColorMode == Compat_DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
			UINT32 hdrSupported = colorInfo2.highDynamicRangeSupported;
			UINT32 activeMode = static_cast<UINT32>(colorInfo2.activeColorMode);
			logger::debug("[HDR] Win11 24H2 detection: highDynamicRangeSupported={}, activeColorMode={}", hdrSupported, activeMode);
			return true;
		}

		// Fallback for older Windows versions
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		colorInfo.header.size = sizeof(colorInfo);
		colorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
		colorInfo.header.id = pathInfo.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS) {
			supported = colorInfo.advancedColorSupported != 0;

			DXGI_COLOR_SPACE_TYPE swapChainOutputColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
			{
				winrt::com_ptr<IDXGIOutput> containingOutput;
				if (SUCCEEDED(swapChain->GetContainingOutput(containingOutput.put()))) {
					winrt::com_ptr<IDXGIOutput6> out6;
					if (SUCCEEDED(containingOutput->QueryInterface(IID_PPV_ARGS(out6.put())))) {
						DXGI_OUTPUT_DESC1 desc1{};
						if (SUCCEEDED(out6->GetDesc1(&desc1)))
							swapChainOutputColorSpace = desc1.ColorSpace;
					}
				}
			}

			enabled = (colorInfo.advancedColorEnabled != 0) && (swapChainOutputColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
			UINT32 advancedSupported = colorInfo.advancedColorSupported;
			UINT32 advancedEnabled = colorInfo.advancedColorEnabled;
			int colorSpaceInt = static_cast<int>(swapChainOutputColorSpace);
			logger::debug("[HDR] Legacy detection: advancedColorSupported={}, advancedColorEnabled={}, swapChainColorSpace={}, enabled={}",
				advancedSupported, advancedEnabled, colorSpaceInt, enabled);
			return true;
		}

		return false;
	}

	// Hook structs for the HDR pipeline - installed in PostPostLoad when ImageReconstruction is not loaded.
	// When ImageReconstruction IS loaded, it installs equivalent hooks covering the same addresses.
	struct HDR_Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
		{
			auto* hdr = &globals::pipeline::cameraSuite;
			hdr->RedirectFramebuffer();
			func(a_this, a3, a_target, a_4, a_5);
			hdr->RestoreFramebuffer();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct HDR_MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1)
		{
			globals::pipeline::cameraSuite.SetUIBuffer();
			func(a1);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void DrawSettingsTooltip(const char* text)
	{
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("%s", text);
	}
}

bool CameraSuite::isHDRMonitor = false;
bool CameraSuite::isHDRCapableMonitor = false;
bool CameraSuite::wasExclusiveFullscreen = false;

bool CameraSuite::DetectHDR()
{
	if (!globals::d3d::swapChain) {
		isHDRMonitor = false;
		isHDRCapableMonitor = false;
		return false;
	}

	bool hdrSupported = false;
	bool hdrEnabled = false;

	IsHDRSupportedAndEnabled(globals::d3d::swapChain, hdrSupported, hdrEnabled);

	isHDRMonitor = hdrEnabled;
	isHDRCapableMonitor = hdrSupported;
	logger::info("[HDR] HDR display detection: supported={}, enabled={}", hdrSupported, hdrEnabled);
	return hdrEnabled;
}

#define PIXL_CAMERA_SETTINGS_JSON_FIELDS(X) \
	X(enableHDR) \
	X(hdrPaperWhite) \
	X(hdrPeakNits) \
	X(hdrUIBrightness) \
	X(dontShowHDRWarning) \
	X(hdrAutoDetected) \
	X(enablePhysicalCamera) \
	X(cameraAutoExposure) \
	X(cameraExposureCompensationEV) \
	X(cameraMinExposureEV) \
	X(cameraMaxExposureEV) \
	X(cameraLowPercentile) \
	X(cameraHighPercentile) \
	X(cameraHighlightProtection) \
	X(cameraShadowDetail) \
	X(cameraContrast) \
	X(cameraLocalExposure) \
	X(cameraAdaptBrightToDark) \
	X(cameraAdaptDarkToBright) \
	X(cameraSaturation) \
	X(cameraToe) \
	X(cameraShoulder) \
	X(cameraInfluence) \
	X(menuSceneBrightness) \
	X(lookPreset) \
	X(lookOpacity) \
	X(enableBloom) \
	X(bloomStrength) \
	X(bloomThreshold) \
	X(bloomRadius) \
	X(enableStormglass) \
	X(stormglassStrength) \
	X(stormglassDropScale) \
	X(stormglassRefraction) \
	X(stormglassTrails) \
	X(stormglassDryingRate) \
	X(enableSubmergedOptics) \
	X(submergedStrength) \
	X(submergedBlur) \
	X(submergedRefraction) \
	X(submergedTransitionSpeed) \
	X(enableColdLens) \
	X(coldLensStrength) \
	X(coldAltitudeStart) \
	X(coldAltitudeFull) \
	X(enableElementalDamageLens) \
	X(elementalLensStrength) \
	X(enableSkyrimDepthOfField) \
	X(enableEnhancedDepthOfField) \
	X(dofAutoFocus) \
	X(dofStrength) \
	X(dofFocusDistance) \
	X(dofFocusRange) \
	X(dofBokehRadius) \
	X(dofHighlightResponse) \
	X(dofFocusEdgeProtection) \
	X(dofForegroundCoverage) \
	X(dofCatEye) \
	X(dofAnamorphicRatio) \
	X(enableModernMotionBlur) \
	X(motionBlurStrength) \
	X(motionBlurShutter) \
	X(motionBlurMaxPixels) \
	X(experimentalBodycam) \
	X(bodycamStrength) \
	X(bodycamDistortion) \
	X(bodycamNoise) \
	X(bodycamVignette) \
	X(bodycamChromaticAberration) \
	X(bodycamSharpen) \
	X(bodycamExposureAggressiveness) \
	X(bodycamHighlightBloom) \
	X(bodycamWhiteBalance)

void to_json(nlohmann::json& json, const CameraSuite::Settings& settings)
{
	json = nlohmann::json::object();
#define PIXL_CAMERA_WRITE_JSON(field) json[#field] = settings.field;
	PIXL_CAMERA_SETTINGS_JSON_FIELDS(PIXL_CAMERA_WRITE_JSON)
#undef PIXL_CAMERA_WRITE_JSON
}

void from_json(const nlohmann::json& json, CameraSuite::Settings& settings)
{
	// WITH_DEFAULT semantics: missing fields retain the in-class defaults. This
	// keeps old user profiles forward compatible as CameraSuite grows.
#define PIXL_CAMERA_READ_JSON(field) \
	if (const auto it = json.find(#field); it != json.end() && !it->is_null()) \
		it->get_to(settings.field);
	PIXL_CAMERA_SETTINGS_JSON_FIELDS(PIXL_CAMERA_READ_JSON)
#undef PIXL_CAMERA_READ_JSON
}

#undef PIXL_CAMERA_SETTINGS_JSON_FIELDS

void CameraSuite::DrawSettings()
{
	auto hdrWarningPopupTitle = std::format("{}##CameraSuite", T(TKEY("warning_popup_title"), "HDR Warning"));

	if (isHDRMonitor) {
		Util::Text::Success(T(TKEY("display_detected"), "Camera Suite Detected"));
	} else if (isHDRCapableMonitor) {
		Util::Text::Warning(T(TKEY("capable_display_windows_hdr_off"), "HDR Capable Display (Windows HDR is off)"));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("capable_display_windows_hdr_off_tooltip_0"), "Your monitor supports HDR, but Windows HDR is currently disabled."));
			ImGui::TextUnformatted(T(TKEY("capable_display_windows_hdr_off_tooltip_1"), "Enable HDR in Windows Display Settings to allow auto-detection."));
		}
	} else {
		Util::Text::Warning(T(TKEY("sdr_display_not_detected"), "SDR Display (HDR not detected)"));
	}

	const bool isExclusiveFullscreen = globals::pipeline::imageReconstruction.loaded ? !globals::pipeline::imageReconstruction.isWindowed : wasExclusiveFullscreen;

	if (isExclusiveFullscreen) {
		ImGui::Spacing();
		Util::Text::WrappedWarning(T(TKEY("exclusive_fullscreen_warning"), "WARNING: Exclusive Fullscreen detected."));
		Util::Text::WrappedWarning(T(TKEY("exclusive_fullscreen_warning_detail"), "HDR is not compatible with Exclusive Fullscreen and may not work correctly. Switch to Borderless Windowed mode for proper HDR support."));
		ImGui::Spacing();
	}

	ImGui::Spacing();

	bool oldEnableHDR;
	bool currentEnableHDR;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		oldEnableHDR = settings.enableHDR;
		currentEnableHDR = settings.enableHDR;
	}

	// Disable the checkbox only when no HDR monitor is detected AND HDR is not already on
	// (allow disabling HDR even on SDR if it was enabled from saved settings).
	if (!isHDRMonitor && !currentEnableHDR) {
		ImGui::BeginDisabled();
	}

	if (ImGui::Checkbox(T(TKEY("enable_hdr"), "Enable HDR"), &currentEnableHDR)) {
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.enableHDR = currentEnableHDR;
			if (settings.enableHDR && !oldEnableHDR) {
				logger::info("HDR: enableHDR changed to: true");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			} else if (!settings.enableHDR && oldEnableHDR) {
				logger::info("HDR: enableHDR changed to: false");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
		}
	}

	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::EndDisabled();
	}

	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (isHDRMonitor) {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip"), "Enable HDR output. Matches vanilla visuals with extended dynamic range."));
		} else if (isHDRCapableMonitor) {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip_windows_off"), "Monitor supports HDR but Windows HDR is off. Enable HDR in Windows Display Settings, then restart the game."));
		} else {
			ImGui::TextUnformatted(T(TKEY("enable_hdr_tooltip_not_detected"), "HDR display not detected. Use Advanced button to override."));
		}
	}

	// Advanced override button â€” shown when HDR is neither active nor auto-detected
	if (!isHDRMonitor && !oldEnableHDR) {
		ImGui::SameLine();
		if (ImGui::Button(T(TKEY("advanced"), "Advanced"))) {
			bool dontShowWarning;
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				dontShowWarning = settings.dontShowHDRWarning;
			}
			if (!dontShowWarning) {
				pendingHDREnable = true;
				showHDRWarningPopup = true;
				ImGui::OpenPopup(hdrWarningPopupTitle.c_str());
			} else {
				// User previously dismissed warnings, enable directly
				{
					std::lock_guard<std::mutex> lock(settingsMutex);
					settings.enableHDR = true;
					logger::info("HDR: enableHDR changed to: true (advanced override, warning suppressed)");
					UpdateHDRData();
					UpdateSwapChainColorSpace();
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			if (isHDRCapableMonitor) {
				ImGui::TextUnformatted(T(TKEY("advanced_tooltip_enable_windows_hdr"), "Enable Windows HDR instead of forcing it here."));
			} else {
				ImGui::TextUnformatted(T(TKEY("advanced_tooltip_force_enable"), "Force enable HDR even without detection (not recommended)."));
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (!isHDRMonitor && settings.enableHDR) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(T(TKEY("enabled_without_detected_display"), "HDR is enabled but no HDR display was detected."));
		}
	}

	if (auto popup = Util::CenteredPopupModal(hdrWarningPopupTitle.c_str(), &showHDRWarningPopup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
		// Prevent background dimming by pushing lower modal dimming
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		Util::Text::Warning(T(TKEY("force_enable_hdr_warning"), "WARNING: Force Enable HDR"));
		ImGui::Separator();
		ImGui::Spacing();
		Util::Text::WrappedWarning(T(TKEY("force_enable_hdr_detected_warning"), "HDR was not detected on your monitor."));
		Util::Text::WrappedWarning(T(TKEY("force_enable_hdr_sdr_warning"), "The game will look VERY WRONG on an SDR (standard) display."));
		ImGui::Spacing();
		ImGui::TextWrapped("%s", T(TKEY("force_enable_hdr_confirm"), "Only proceed if you have an HDR-capable display that was not detected correctly."));
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const auto buttonWidthForLabel = [](const char* label) {
			return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		};
		const char* forceEnableLabel = T(TKEY("force_enable_hdr"), "Force Enable HDR");
		const char* cancelLabel = T(TKEY("cancel"), "Cancel");
		const float buttonWidth = std::max({
			ThemeManager::Constants::POPUP_BUTTON_WIDTH * Util::GetUIScale(),
			buttonWidthForLabel(forceEnableLabel),
			buttonWidthForLabel(cancelLabel)
		});

		if (ImGui::Button(forceEnableLabel, ImVec2(buttonWidth, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = true;
				logger::info("HDR: enableHDR changed to: true (forced override)");
				UpdateHDRData();
				UpdateSwapChainColorSpace();
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(cancelLabel, ImVec2(buttonWidth, 0))) {
			{
				std::lock_guard<std::mutex> lock(settingsMutex);
				settings.enableHDR = false;
			}
			showHDRWarningPopup = false;
			pendingHDREnable = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		bool dontShowWarning;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			dontShowWarning = settings.dontShowHDRWarning;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y * 0.5f));
		ImGui::SetWindowFontScale(0.9f);
		if (ImGui::Checkbox(T(TKEY("dont_show_again"), "Don't show me this again"), &dontShowWarning)) {
			std::lock_guard<std::mutex> lock(settingsMutex);
			settings.dontShowHDRWarning = dontShowWarning;
		}
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleVar();

		ImGui::PopStyleVar();
	}

	bool isHDREnabled;
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		isHDREnabled = settings.enableHDR;
	}

	if (isHDREnabled) {
		ImGui::Spacing();

		uint oldPaperWhite;
		int currentPaperWhite;
		uint oldPeakNits;
		int currentPeakNits;
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			oldPaperWhite = settings.hdrPaperWhite;
			currentPaperWhite = static_cast<int>(settings.hdrPaperWhite);
			oldPeakNits = settings.hdrPeakNits;
			currentPeakNits = static_cast<int>(settings.hdrPeakNits);
		}

		ImGui::SliderInt(T(TKEY("paper_white_nits"), "Paper White (nits)"), &currentPaperWhite, 80, 500, "%d", ImGuiSliderFlags_AlwaysClamp);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPaperWhite >= static_cast<int>(settings.hdrPeakNits)) {
				currentPaperWhite = static_cast<int>(settings.hdrPeakNits) - 1;
			}
			settings.hdrPaperWhite = static_cast<uint>(std::max(currentPaperWhite, 1));
			if (oldPaperWhite != settings.hdrPaperWhite) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("paper_white_tooltip_0"), "How bright SDR white appears on your HDR display."));
			ImGui::TextUnformatted(T(TKEY("paper_white_tooltip_1"), "203 nits is the ITU BT.2408 reference. Increase for a brighter image."));
		}

		ImGui::SliderInt(T(TKEY("peak_brightness_nits"), "Peak Brightness (nits)"), &currentPeakNits, 400, 10000, "%d", ImGuiSliderFlags_AlwaysClamp);
		{
			std::lock_guard<std::mutex> lock(settingsMutex);
			if (currentPeakNits <= static_cast<int>(settings.hdrPaperWhite)) {
				currentPeakNits = static_cast<int>(settings.hdrPaperWhite) + 1;
			}
			settings.hdrPeakNits = static_cast<uint>(currentPeakNits);
			if (oldPeakNits != settings.hdrPeakNits) {
				UpdateHDRData();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("peak_brightness_tooltip_0"), "Maximum brightness your display can produce."));
			ImGui::TextUnformatted(T(TKEY("peak_brightness_tooltip_1"), "Set to match your display's actual peak brightness."));
		}

		ImGui::TextDisabled(T(TKEY("display_reports_max_nits"), "Display reports: %.0f nits max"), cachedDisplayMaxLuminance);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_0"), "Reported by OS/driver (DXGI MaxLuminance), not a direct meter reading."));
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_1"), "It may be EDID metadata and can differ from real highlight peak output."));
			ImGui::TextUnformatted(T(TKEY("display_reports_max_nits_tooltip_2"), "Treat this as a starting point and tune Peak Brightness as needed."));
		}
	}

	// UI brightness slider - only shown when HDR is enabled
	ImGui::Spacing();
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		if (settings.enableHDR) {
			float oldUIBrightness = settings.hdrUIBrightness;
			float currentUIBrightness = settings.hdrUIBrightness;

			ImGui::SliderFloat(T(TKEY("ui_brightness_multiplier"), "UI Brightness Multiplier"), &currentUIBrightness, 0.5f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			if (oldUIBrightness != currentUIBrightness) {
				settings.hdrUIBrightness = currentUIBrightness;
				UpdateHDRData();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_0"), "UI brightness = Paper White x this multiplier in HDR mode."));
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_1"), "1.00x = UI renders at Paper White brightness. Higher values make UI brighter relative to scene content."));
				ImGui::TextUnformatted(T(TKEY("ui_brightness_multiplier_tooltip_2"), "Note: Main menu and loading screens always render at Paper White brightness."));
			}
		}
	}

	// ---------------------------------------------------------------------
	// PIXL PHYSICAL CAMERA
	// ---------------------------------------------------------------------
	ImGui::Spacing();
	ImGui::SeparatorText("Physical Camera");
	ImGui::TextWrapped("Scene-aware exposure and a luminance-preserving camera response keep bright sources intense without throwing away useful shadow detail. Works in SDR and HDR; Linear Light Core provides the cleanest scene-referred input.");
	{
		std::lock_guard<std::mutex> lock(settingsMutex);
		bool changed = false;
		changed |= Util::FeatureToggle("Enable Physical Camera", &settings.enablePhysicalCamera);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Runs the PIXL camera mapper before display output. Disable to return to the normal PIXL Renderer display path.");

		if (settings.enablePhysicalCamera) {
			ImGui::TextDisabled("Recommended: Auto Exposure ON, 0.0 EV, neutral preset");
			changed |= ImGui::SliderFloat("Camera Influence", &settings.cameraInfluence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Controls the SDR blend between Skyrim's authored image and PIXL's photographic response. 25-40% retains the original art direction; HDR always uses the full display mapper.");
			if (ImGui::Button("Neutral")) {
				settings.cameraAutoExposure = true;
				settings.cameraExposureCompensationEV = 0.0f;
				settings.cameraHighlightProtection = 0.65f;
				settings.cameraShadowDetail = 0.14f;
				settings.cameraContrast = 1.0f;
				settings.cameraLocalExposure = 0.12f;
				settings.cameraSaturation = 1.0f;
				settings.cameraToe = 0.12f;
				settings.cameraShoulder = 0.72f;
				settings.cameraInfluence = 0.35f;
				settings.experimentalBodycam = false;
				changed = true;
			}
			DrawSettingsTooltip("Restores a neutral, palette-preserving physical-camera response. Applies immediately.");
			ImGui::SameLine();
			if (ImGui::Button("Cinematic")) {
				settings.cameraAutoExposure = true;
				settings.cameraExposureCompensationEV = 0.0f;
				settings.cameraHighlightProtection = 0.78f;
				settings.cameraShadowDetail = 0.18f;
				settings.cameraContrast = 1.04f;
				settings.cameraLocalExposure = 0.18f;
				settings.cameraSaturation = 0.98f;
				settings.cameraToe = 0.16f;
				settings.cameraShoulder = 0.82f;
				settings.cameraInfluence = 0.65f;
				settings.experimentalBodycam = false;
				changed = true;
			}
			DrawSettingsTooltip("Uses stronger highlight protection and local adaptation with a restrained cinematic contrast curve. Applies immediately.");

			if (ImGui::CollapsingHeader("LUT / Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
				const char* looks[] = {
					"Original", "Nordic Neutral", "Saga", "Dramatic", "Hearthfire", "Bleak",
					"Bleach", "Winter", "Sunset", "Fantasy Green", "Nightfall", "Cinematic"
				};
				int selectedLook = static_cast<int>(settings.lookPreset);
				if (ImGui::Combo("Colour Grade", &selectedLook, looks, static_cast<int>(std::size(looks)))) {
					settings.lookPreset = static_cast<uint>(std::clamp(selectedLook, 0, static_cast<int>(std::size(looks)) - 1));
					LoadLookTexture();
					changed = true;
				}
				DrawSettingsTooltip("Selects a PIXL-authored 32-point LUT after the physical camera and before UI composition. Original is a neutral bypass.");
				ImGui::BeginDisabled(settings.lookPreset == 0);
				int lutOpacityPercent = std::clamp(static_cast<int>(std::lround(settings.lookOpacity * 100.0f)), 0, 100);
				if (ImGui::SliderInt("LUT Opacity", &lutOpacityPercent, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
					settings.lookOpacity = static_cast<float>(lutOpacityPercent) * 0.01f;
					changed = true;
				}
				ImGui::EndDisabled();
				DrawSettingsTooltip("Blends the selected LUT with the neutral tonemap in real time from 0 to 100%. Values around 20-45% preserve weather and texture authorship best.");
			}
			ImGui::SameLine();
			if (ImGui::Button("Bodycam Preset")) {
				settings.cameraAutoExposure = true;
				settings.cameraExposureCompensationEV = 0.0f;
				settings.cameraAdaptBrightToDark = 0.55f;
				settings.cameraAdaptDarkToBright = 0.12f;
				settings.experimentalBodycam = true;
				settings.bodycamStrength = 0.82f;
				settings.bodycamDistortion = 0.13f;
				settings.bodycamNoise = 0.20f;
				settings.bodycamVignette = 0.09f;
				settings.bodycamChromaticAberration = 0.022f;
				settings.bodycamSharpen = 0.12f;
				settings.bodycamExposureAggressiveness = 0.82f;
				settings.bodycamHighlightBloom = 0.18f;
				settings.bodycamWhiteBalance = 0.14f;
				changed = true;
			}
			DrawSettingsTooltip("Enables the experimental body-worn digital-camera profile. This is a stylized preset and is not the recommended neutral rendering mode.");

			changed |= ImGui::Checkbox("Automatic Exposure", &settings.cameraAutoExposure);
			DrawSettingsTooltip("Meters scene luminance from the camera histogram and adapts exposure over time. Disable for a fixed exposure offset.");
			changed |= ImGui::SliderFloat("Exposure Compensation", &settings.cameraExposureCompensationEV, -4.0f, 4.0f, "%+.2f EV", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Offsets the physical-camera exposure in stops. Positive values brighten the scene; negative values preserve more highlight headroom.");
			changed |= ImGui::SliderFloat("Highlight Protection", &settings.cameraHighlightProtection, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Compresses the brightest scene values before display mapping. Higher values retain more bright-source detail with a softer roll-off.");
			changed |= ImGui::SliderFloat("Shadow Detail", &settings.cameraShadowDetail, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Lifts recoverable low-luminance detail without changing the metered exposure. Excessive values can flatten scene depth.");
			changed |= ImGui::SliderFloat("Local Exposure", &settings.cameraLocalExposure, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Blends a small bilateral neighbourhood exposure correction to balance bright windows and dark interiors while respecting edges.");

			if (ImGui::CollapsingHeader("Camera Advanced")) {
				changed |= ImGui::SliderFloat("Minimum Exposure", &settings.cameraMinExposureEV, -10.0f, 0.0f, "%+.1f EV", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Darkest exposure the automatic meter may select. This bounds highlight protection in very bright scenes.");
				changed |= ImGui::SliderFloat("Maximum Exposure", &settings.cameraMaxExposureEV, 0.0f, 10.0f, "%+.1f EV", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Brightest exposure the automatic meter may select. This prevents extreme amplification in very dark scenes.");
				changed |= ImGui::SliderFloat("Metering Low Percentile", &settings.cameraLowPercentile, 0.0f, 0.20f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Fraction of the darkest histogram samples ignored by exposure metering. Raise it to prevent tiny black regions from biasing exposure.");
				changed |= ImGui::SliderFloat("Metering High Percentile", &settings.cameraHighPercentile, 0.80f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Upper histogram percentile used by exposure metering. Lower it to reject small, extremely bright outliers.");
				changed |= ImGui::SliderFloat("Bright to Dark Adaptation", &settings.cameraAdaptBrightToDark, 0.05f, 4.0f, "%.2f s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Approximate adaptation time when entering a darker environment. Higher values make the camera brighten more slowly.");
				changed |= ImGui::SliderFloat("Dark to Bright Adaptation", &settings.cameraAdaptDarkToBright, 0.03f, 2.0f, "%.2f s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Approximate adaptation time when entering a brighter environment. Higher values make the camera darken more slowly.");
				changed |= ImGui::SliderFloat("Contrast", &settings.cameraContrast, 0.75f, 1.30f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Mid-tone contrast applied by the display response. 1.0 is neutral.");
				changed |= ImGui::SliderFloat("Saturation", &settings.cameraSaturation, 0.70f, 1.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Output colour saturation after exposure and highlight handling. 1.0 preserves the source saturation.");
				changed |= ImGui::SliderFloat("Shadow Toe", &settings.cameraToe, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Shapes the darkest part of the response curve. Higher values produce a firmer black toe.");
				changed |= ImGui::SliderFloat("Highlight Shoulder", &settings.cameraShoulder, 0.2f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				DrawSettingsTooltip("Shapes the highlight roll-off. Higher values create a broader, softer shoulder before the display peak.");
			}

			if (ImGui::CollapsingHeader("Experimental - Bodycam Emulation")) {
				Util::Text::WrappedWarning("Experimental. Emulates a modern body-worn digital camera: wide-lens distortion, gain-dependent sensor noise, restrained chromatic aberration, highlight bloom, edge processing and aggressive exposure response. UI is intentionally left undistorted.");
				changed |= ImGui::Checkbox("Enable Bodycam Emulation", &settings.experimentalBodycam);
				DrawSettingsTooltip("Master switch for the stylized lens/sensor pass. Updates immediately and leaves UI rendering undistorted.");
				if (settings.experimentalBodycam) {
					changed |= ImGui::SliderFloat("Emulation Strength", &settings.bodycamStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Overall blend between the neutral camera output and the bodycam emulation.");
					changed |= ImGui::SliderFloat("Wide Lens Distortion", &settings.bodycamDistortion, 0.0f, 0.30f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Radial wide-angle lens distortion applied near the image edges.");
					changed |= ImGui::SliderFloat("Low-Light Sensor Noise", &settings.bodycamNoise, 0.0f, 0.60f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Gain-dependent sensor noise. It becomes more visible in dark regions and at brighter exposure gain.");
					changed |= ImGui::SliderFloat("Lens Vignette", &settings.bodycamVignette, 0.0f, 0.30f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Darkens the image progressively toward the lens edges.");
					changed |= ImGui::SliderFloat("Edge Aberration", &settings.bodycamChromaticAberration, 0.0f, 0.08f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Separates colour channels near the outer lens region. Keep low for a plausible digital-camera response.");
					changed |= ImGui::SliderFloat("Digital Sharpness", &settings.bodycamSharpen, 0.0f, 0.35f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Adds restrained edge enhancement after lens and exposure processing.");
					changed |= ImGui::SliderFloat("Exposure Aggressiveness", &settings.bodycamExposureAggressiveness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Scales the bodycam profile's faster, more visible automatic-exposure response.");
					changed |= ImGui::SliderFloat("Highlight Bloom", &settings.bodycamHighlightBloom, 0.0f, 0.50f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Adds a compact bright-source glow inside the bodycam emulation. This is separate from Skyrim's image-space bloom.");
					changed |= ImGui::SliderFloat("Auto White Balance", &settings.bodycamWhiteBalance, 0.0f, 0.40f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					DrawSettingsTooltip("Strength of the bodycam profile's neutralising white-balance estimate.");
				}
			}
		}

		if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable PIXL Bloom", &settings.enableBloom);
			DrawSettingsTooltip("Scene-linear soft-knee bloom integrated into PIXL Physical Camera. Bright-source colour is retained and isolated fireflies are bounded.");
			ImGui::BeginDisabled(!settings.enableBloom);
			changed |= ImGui::SliderFloat("Bloom Strength", &settings.bloomStrength, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Bloom Threshold", &settings.bloomThreshold, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Bloom Radius", &settings.bloomRadius, 0.1f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Stormglass Weather Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Stormglass", &settings.enableStormglass);
			DrawSettingsTooltip("Adds procedural rain beads and draining trails using Skyrim's live precipitation intensity. The scene refracts through the wet optical surface while HUD and menus remain sharp.");
			ImGui::BeginDisabled(!settings.enableStormglass);
			changed |= ImGui::SliderFloat("Stormglass Presence", &settings.stormglassStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Drop Scale", &settings.stormglassDropScale, 0.50f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Optical Refraction", &settings.stormglassRefraction, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Trail Formation", &settings.stormglassTrails, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Drying Rate", &settings.stormglassDryingRate, 0.01f, 0.50f, "%.3f/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("How quickly residual beads clear once rainfall stops. Surface-break water begins fully wet and uses the same natural drying state.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Submerged Optics", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Submerged Optics", &settings.enableSubmergedOptics);
			DrawSettingsTooltip("Refines Skyrim's authored underwater image with restrained water-type colour absorption, optical softening and a smooth surface-break transition.");
			ImGui::BeginDisabled(!settings.enableSubmergedOptics);
			changed |= ImGui::SliderFloat("Underwater Presence", &settings.submergedStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Optical Softening", &settings.submergedBlur, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Water Refraction", &settings.submergedRefraction, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Surface Transition", &settings.submergedTransitionSpeed, 0.5f, 8.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Controls how quickly the optical response settles when entering or leaving water. Breaking the surface transfers a draining film into Stormglass.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Cold & Elemental Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Environmental Frost Edges", &settings.enableColdLens);
			DrawSettingsTooltip("Builds a restrained crystalline edge response from live snowfall and high exterior altitude. It never affects HUD/menu composition and fades naturally in warmer clear areas.");
			ImGui::BeginDisabled(!settings.enableColdLens);
			changed |= ImGui::SliderFloat("Frost Edge Strength", &settings.coldLensStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Cold Altitude Start", &settings.coldAltitudeStart, 0.0f, 70000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Cold Altitude Full", &settings.coldAltitudeFull, 1000.0f, 90000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
			settings.coldAltitudeFull = std::max(settings.coldAltitudeFull, settings.coldAltitudeStart + 1000.0f);
			ImGui::EndDisabled();
			changed |= ImGui::Checkbox("Elemental Hit Optics", &settings.enableElementalDamageLens);
			DrawSettingsTooltip("Confirmed fire and frost projectile hits on the player produce short heat-distortion or ice-edge pulses. This includes compatible dragon breath projectiles without guessing from nearby spell visuals.");
			ImGui::BeginDisabled(!settings.enableElementalDamageLens);
			changed |= ImGui::SliderFloat("Elemental Presence", &settings.elementalLensStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Skyrim Depth of Field", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Skyrim Depth of Field", &settings.enableSkyrimDepthOfField);
			DrawSettingsTooltip("Enables Skyrim's native image-space depth of field in real time. PIXL's experimental replacement is retired for this release, so weather, interiors and authored image spaces remain in control of focus and blur.");
		}

		if (ImGui::CollapsingHeader("Modern Motion Blur", ImGuiTreeNodeFlags_DefaultOpen)) {
			changed |= ImGui::Checkbox("Enable Camera Motion Blur", &settings.enableModernMotionBlur);
			DrawSettingsTooltip("Depth-aware, shutter-based camera motion blur. UI remains sharp, depth edges reject background bleeding, and tiny temporal jitter is ignored.");
			ImGui::BeginDisabled(!settings.enableModernMotionBlur);
			changed |= ImGui::SliderFloat("Motion Blur Strength", &settings.motionBlurStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::SliderFloat("Shutter Angle", &settings.motionBlurShutter, 0.10f, 1.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Scales camera travel captured during the frame. 0.5 approximates a cinematic 180-degree shutter.");
			changed |= ImGui::SliderFloat("Maximum Motion", &settings.motionBlurMaxPixels, 4.0f, 48.0f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Bounds the blur footprint during rapid turns so the image remains readable and stable.");
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Menus and Loading Screens")) {
			changed |= ImGui::SliderFloat("Scene Brightness", &settings.menuSceneBrightness, 0.75f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			DrawSettingsTooltip("Brightness applied only to the display-referred main-menu and loading-screen scene layer. Gameplay exposure and UI text are unchanged. Applies immediately.");
		}

		if (changed) {
			settings.cameraLowPercentile = std::clamp(settings.cameraLowPercentile, 0.0f, 0.20f);
			settings.cameraHighPercentile = std::clamp(settings.cameraHighPercentile,
				std::max(0.80f, settings.cameraLowPercentile + 0.05f), 1.0f);
			UpdateHDRData();
		}
	}
}

#undef I18N_KEY_PREFIX

void CameraSuite::SaveSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);
	o_json = settings;
	// Keep the core settings below nlohmann's non-intrusive macro arity limit;
	// environmental lens additions remain normal, backward-compatible keys.
	o_json["enableColdLens"] = settings.enableColdLens;
	o_json["coldLensStrength"] = settings.coldLensStrength;
	o_json["coldAltitudeStart"] = settings.coldAltitudeStart;
	o_json["coldAltitudeFull"] = settings.coldAltitudeFull;
	o_json["enableElementalDamageLens"] = settings.enableElementalDamageLens;
	o_json["elementalLensStrength"] = settings.elementalLensStrength;
}

void CameraSuite::LoadSettings(json& o_json)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	bool oldEnableHDR = settings.enableHDR;

	settings = o_json;
	// PIXL's experimental realtime DOF is retired for release. Preserve its old
	// JSON keys for backwards compatibility but never reactivate the compute path.
	settings.enableEnhancedDepthOfField = false;
	settings.enableColdLens = o_json.value("enableColdLens", settings.enableColdLens);
	settings.coldLensStrength = o_json.value("coldLensStrength", settings.coldLensStrength);
	settings.coldAltitudeStart = o_json.value("coldAltitudeStart", settings.coldAltitudeStart);
	settings.coldAltitudeFull = o_json.value("coldAltitudeFull", settings.coldAltitudeFull);
	settings.enableElementalDamageLens =
		o_json.value("enableElementalDamageLens", settings.enableElementalDamageLens);
	settings.elementalLensStrength =
		o_json.value("elementalLensStrength", settings.elementalLensStrength);
	// Camera quality belongs to the coordinated menu contract rather than the
	// authored camera-look JSON. Restore it on startup instead of reverting to
	// the member default after a saved Low/Medium/High selection.
	if (globals::menu)
		cameraQuality = static_cast<std::uint32_t>(std::clamp(globals::menu->GetSettings().CameraQuality, 0, 3));

	// Sanitize PIXL Physical Camera settings loaded from older or hand-edited JSON.
	settings.cameraExposureCompensationEV = std::clamp(settings.cameraExposureCompensationEV, -4.0f, 4.0f);
	settings.cameraMinExposureEV = std::clamp(settings.cameraMinExposureEV, -10.0f, 0.0f);
	settings.cameraMaxExposureEV = std::clamp(settings.cameraMaxExposureEV, 0.0f, 10.0f);
	settings.cameraLowPercentile = std::clamp(settings.cameraLowPercentile, 0.0f, 0.20f);
	settings.cameraHighPercentile = std::clamp(settings.cameraHighPercentile, 0.80f, 1.0f);
	if (settings.cameraHighPercentile <= settings.cameraLowPercentile + 0.05f)
		settings.cameraHighPercentile = std::min(1.0f, settings.cameraLowPercentile + 0.05f);
	settings.cameraHighlightProtection = std::clamp(settings.cameraHighlightProtection, 0.0f, 1.0f);
	settings.cameraShadowDetail = std::clamp(settings.cameraShadowDetail, 0.0f, 0.5f);
	settings.cameraContrast = std::clamp(settings.cameraContrast, 0.75f, 1.30f);
	settings.cameraLocalExposure = std::clamp(settings.cameraLocalExposure, 0.0f, 0.5f);
	settings.cameraAdaptBrightToDark = std::clamp(settings.cameraAdaptBrightToDark, 0.05f, 4.0f);
	settings.cameraAdaptDarkToBright = std::clamp(settings.cameraAdaptDarkToBright, 0.03f, 2.0f);
	settings.cameraSaturation = std::clamp(settings.cameraSaturation, 0.70f, 1.25f);
	settings.cameraToe = std::clamp(settings.cameraToe, 0.0f, 0.5f);
	settings.cameraShoulder = std::clamp(settings.cameraShoulder, 0.2f, 1.5f);
	settings.cameraInfluence = std::clamp(settings.cameraInfluence, 0.0f, 1.0f);
	settings.menuSceneBrightness = std::clamp(settings.menuSceneBrightness, 0.75f, 3.0f);
	settings.lookPreset = std::min(settings.lookPreset, 11u);
	settings.lookOpacity = std::clamp(settings.lookOpacity, 0.0f, 1.0f);
	settings.bloomStrength = std::clamp(settings.bloomStrength, 0.0f, 3.0f);
	settings.bloomThreshold = std::clamp(settings.bloomThreshold, 0.0f, 5.0f);
	settings.bloomRadius = std::clamp(settings.bloomRadius, 0.0f, 5.0f);
	settings.stormglassStrength = std::clamp(settings.stormglassStrength, 0.0f, 1.0f);
	settings.stormglassDropScale = std::clamp(settings.stormglassDropScale, 0.50f, 2.0f);
	settings.stormglassRefraction = std::clamp(settings.stormglassRefraction, 0.0f, 1.5f);
	settings.stormglassTrails = std::clamp(settings.stormglassTrails, 0.0f, 1.0f);
	settings.stormglassDryingRate = std::clamp(settings.stormglassDryingRate, 0.01f, 0.50f);
	settings.submergedStrength = std::clamp(settings.submergedStrength, 0.0f, 1.0f);
	settings.submergedBlur = std::clamp(settings.submergedBlur, 0.0f, 1.0f);
	settings.submergedRefraction = std::clamp(settings.submergedRefraction, 0.0f, 1.5f);
	settings.submergedTransitionSpeed = std::clamp(settings.submergedTransitionSpeed, 0.5f, 8.0f);
	settings.coldLensStrength = std::clamp(settings.coldLensStrength, 0.0f, 1.0f);
	settings.coldAltitudeStart = std::clamp(settings.coldAltitudeStart, 0.0f, 70000.0f);
	settings.coldAltitudeFull = std::clamp(
		settings.coldAltitudeFull,
		settings.coldAltitudeStart + 1000.0f,
		90000.0f);
	settings.elementalLensStrength = std::clamp(settings.elementalLensStrength, 0.0f, 1.0f);
	settings.dofStrength = std::clamp(settings.dofStrength, 0.0f, 1.0f);
	settings.dofFocusDistance = std::clamp(settings.dofFocusDistance, 100.0f, 20000.0f);
	settings.dofFocusRange = std::clamp(settings.dofFocusRange, 100.0f, 20000.0f);
	settings.dofBokehRadius = std::clamp(settings.dofBokehRadius, 0.5f, 2.0f);
	settings.dofHighlightResponse = std::clamp(settings.dofHighlightResponse, 0.0f, 1.0f);
	settings.dofFocusEdgeProtection = std::clamp(settings.dofFocusEdgeProtection, 0.0f, 2.0f);
	settings.dofForegroundCoverage = std::clamp(settings.dofForegroundCoverage, 0.0f, 1.5f);
	settings.dofCatEye = std::clamp(settings.dofCatEye, 0.0f, 1.0f);
	settings.dofAnamorphicRatio = std::clamp(settings.dofAnamorphicRatio, 0.5f, 2.0f);
	settings.motionBlurStrength = std::clamp(settings.motionBlurStrength, 0.0f, 1.0f);
	settings.motionBlurShutter = std::clamp(settings.motionBlurShutter, 0.10f, 1.0f);
	settings.motionBlurMaxPixels = std::clamp(settings.motionBlurMaxPixels, 4.0f, 48.0f);
	settings.bodycamStrength = std::clamp(settings.bodycamStrength, 0.0f, 1.0f);
	settings.bodycamDistortion = std::clamp(settings.bodycamDistortion, 0.0f, 0.30f);
	settings.bodycamNoise = std::clamp(settings.bodycamNoise, 0.0f, 0.60f);
	settings.bodycamVignette = std::clamp(settings.bodycamVignette, 0.0f, 0.30f);
	settings.bodycamChromaticAberration = std::clamp(settings.bodycamChromaticAberration, 0.0f, 0.08f);
	settings.bodycamSharpen = std::clamp(settings.bodycamSharpen, 0.0f, 0.35f);
	settings.bodycamExposureAggressiveness = std::clamp(settings.bodycamExposureAggressiveness, 0.0f, 1.0f);
	settings.bodycamHighlightBloom = std::clamp(settings.bodycamHighlightBloom, 0.0f, 0.50f);
	settings.bodycamWhiteBalance = std::clamp(settings.bodycamWhiteBalance, 0.0f, 0.40f);
	environmentStateValid = false;

	// Defer auto-detection to SetupResources where the swap chain is available.
	// DetectHDR() needs globals::d3d::swapChain which isn't valid during early plugin init.
	// hdrAutoDetected starts false in defaults and is only set true after auto-detect
	// completes in SetupResources, so this correctly triggers on first launch even
	// when the default config was auto-generated with enableHDR: false.
	if (!settings.hdrAutoDetected) {
		pendingAutoDetect = true;
		logger::info("[HDR] Auto-detection not yet run - deferring to SetupResources");
	}

	if (settings.enableHDR != oldEnableHDR) {
		UpdateHDRData();
		UpdateSwapChainColorSpace();
	}
}

void CameraSuite::RestoreDefaultSettings()
{
	bool hdrMonitor = DetectHDR();
	settings.enableHDR = hdrMonitor;
	settings.hdrPaperWhite = 203;
	settings.hdrPeakNits = 1000;
	settings.hdrUIBrightness = 1.0f;
	settings.dontShowHDRWarning = false;
	settings.enablePhysicalCamera = true;
	settings.cameraAutoExposure = true;
	settings.cameraExposureCompensationEV = 0.0f;
	settings.cameraMinExposureEV = -6.0f;
	settings.cameraMaxExposureEV = 6.0f;
	settings.cameraLowPercentile = 0.02f;
	settings.cameraHighPercentile = 0.98f;
	settings.cameraHighlightProtection = 0.65f;
	settings.cameraShadowDetail = 0.14f;
	settings.cameraContrast = 1.0f;
	settings.cameraLocalExposure = 0.12f;
	settings.cameraAdaptBrightToDark = 1.20f;
	settings.cameraAdaptDarkToBright = 0.25f;
	settings.cameraSaturation = 1.0f;
	settings.cameraToe = 0.12f;
	settings.cameraShoulder = 0.72f;
	settings.cameraInfluence = 1.0f;
	settings.menuSceneBrightness = 1.0f;
	settings.lookPreset = 0;
	settings.lookOpacity = 0.35f;
	settings.enableBloom = false;
	settings.bloomStrength = 0.80f;
	settings.bloomThreshold = 1.20f;
	settings.bloomRadius = 1.00f;
	settings.enableStormglass = true;
	settings.stormglassStrength = 0.58f;
	settings.stormglassDropScale = 1.00f;
	settings.stormglassRefraction = 0.65f;
	settings.stormglassTrails = 0.55f;
	settings.stormglassDryingRate = 0.055f;
	settings.enableSubmergedOptics = true;
	settings.submergedStrength = 0.32f;
	settings.submergedBlur = 0.22f;
	settings.submergedRefraction = 0.42f;
	settings.submergedTransitionSpeed = 3.20f;
	settings.enableColdLens = true;
	settings.coldLensStrength = 0.28f;
	settings.coldAltitudeStart = 28000.0f;
	settings.coldAltitudeFull = 60000.0f;
	settings.enableElementalDamageLens = true;
	settings.elementalLensStrength = 0.45f;
	settings.enableSkyrimDepthOfField = true;
	settings.enableEnhancedDepthOfField = false;
	settings.dofAutoFocus = true;
	settings.dofStrength = 0.24f;
	settings.dofFocusDistance = 2200.0f;
	settings.dofFocusRange = 480.0f;
	settings.dofBokehRadius = 1.0f;
	settings.dofHighlightResponse = 0.28f;
	settings.dofFocusEdgeProtection = 0.85f;
	settings.dofForegroundCoverage = 0.70f;
	settings.dofCatEye = 0.20f;
	settings.dofAnamorphicRatio = 1.0f;
	settings.enableModernMotionBlur = false;
	settings.motionBlurStrength = 0.45f;
	settings.motionBlurShutter = 0.50f;
	settings.motionBlurMaxPixels = 24.0f;
	settings.experimentalBodycam = false;
	settings.bodycamStrength = 0.75f;
	settings.bodycamDistortion = 0.12f;
	settings.bodycamNoise = 0.18f;
	settings.bodycamVignette = 0.08f;
	settings.bodycamChromaticAberration = 0.025f;
	settings.bodycamSharpen = 0.10f;
	settings.bodycamExposureAggressiveness = 0.75f;
	settings.bodycamHighlightBloom = 0.15f;
	settings.bodycamWhiteBalance = 0.12f;
	environmentStateValid = false;
	environmentTime = 0.0f;
	stormglassRainIntensityState = 0.0f;
	coldLensState = 0.0f;
	fireLensState = 0.0f;
	frostImpactLensState = 0.0f;
	stormglassWetnessState = 0.0f;
	surfaceBreakFilmState = 0.0f;
	submergedBlendState = 0.0f;
	stormglassLateralInertiaState = 0.0f;
	stormglassPreviousCameraYaw = 0.0f;
	stormglassCameraYawValid = false;
	wasPlayerUnderwater = false;
}

void CameraSuite::DataLoaded()
{
	// Use Skyrim's built-in ini setting to upgrade all HDR render targets to 16-bit float format.
	auto setting = RE::GetINISetting("bUse64bitsHDRRenderTarget:Display");
	if (setting) {
		setting->data.b = true;
		logger::info("[Camera Suite] Enabled bUse64bitsHDRRenderTarget - all required render targets will use R16G16B16A16_FLOAT");
	} else {
		logger::warn("[Camera Suite] bUse64bitsHDRRenderTarget ini setting not found");
	}
}

void CameraSuite::PostPostLoad()
{
	// When ImageReconstruction is loaded it installs equivalent hooks for these same addresses in its own
	// PostPostLoad. Only install here when ImageReconstruction is absent.
	if (!globals::pipeline::imageReconstruction.loaded) {
		logger::info("[Camera Suite] Installing HDR pipeline hooks (ImageReconstruction not loaded)");
		stl::detour_thunk<HDR_MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));
		stl::write_thunk_call<HDR_Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));
	}
}

void CameraSuite::SetupResources()
{
	if (hdrTexture || outputTexture || uiTexture || hdrDataCB) {
		DestroyResources();
	}

	DetectHDR();

	if (pendingAutoDetect) {
		pendingAutoDetect = false;
		std::lock_guard<std::mutex> lock(settingsMutex);
		settings.enableHDR = isHDRMonitor;
		settings.hdrAutoDetected = true;
		logger::info("[HDR] Auto-configured HDR based on display: {}", isHDRMonitor ? "enabled" : "disabled");
	}

	cachedDisplayMaxLuminance = GetDisplayMaxLuminance();

	// Set up swap chain color space BEFORE querying format and creating textures
	// This ensures outputTexture matches the actual swap chain format for CopyResource
	UpdateSwapChainColorSpace();

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	// Get the actual swap chain format for output texture
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR format
	DXGI_SWAP_CHAIN_DESC scDesc;
	if (SUCCEEDED(globals::d3d::swapChain->GetDesc(&scDesc))) {
		swapChainFormat = scDesc.BufferDesc.Format;
		logger::info("[HDR] Swap chain format: {} ({})", (int)swapChainFormat,
			swapChainFormat == DXGI_FORMAT_R10G10B10A2_UNORM  ? "R10G10B10A2_UNORM (HDR10)" :
			swapChainFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? "R16G16B16A16_FLOAT (scRGB)" :
			swapChainFormat == DXGI_FORMAT_R8G8B8A8_UNORM     ? "R8G8B8A8_UNORM (SDR 8-bit)" :
			swapChainFormat == DXGI_FORMAT_B8G8R8A8_UNORM     ? "B8G8R8A8_UNORM (SDR 8-bit)" :
																"other");
	}

	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	hdrTexture = new Texture2D(texDesc, "HDR::HdrTexture");
	hdrTexture->CreateSRV(srvDesc);
	hdrTexture->CreateUAV(uavDesc);

	// RTV so ISHDR can render directly into this float texture
	D3D11_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
	hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hdrRtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	hdrRtvDesc.Texture2D.MipSlice = 0;
	hdrTexture->CreateRTV(hdrRtvDesc);

	// Output texture must match the swap chain format: ApplyHDR does
	// CopyResource(backBuffer, outputTexture) and CopyResource requires identical formats.
	texDesc.Format = swapChainFormat;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	outputTexture = new Texture2D(texDesc, "HDR::OutputTexture");
	outputTexture->CreateSRV(srvDesc);
	outputTexture->CreateUAV(uavDesc);

	// UI texture for separate UI rendering
	// Use R8G8B8A8_UNORM (8-bit SDR) - vanilla UI is SDR and 8-bit precision
	// naturally truncates near-black ghost bar artifacts to zero
	D3D11_TEXTURE2D_DESC uiTexDesc = texDesc;
	uiTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uiTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	D3D11_SHADER_RESOURCE_VIEW_DESC uiSrvDesc = srvDesc;
	uiSrvDesc.Format = uiTexDesc.Format;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uiUavDesc = uavDesc;
	uiUavDesc.Format = uiTexDesc.Format;

	uiTexture = new Texture2D(uiTexDesc, "HDR::UiTexture");
	uiTexture->CreateSRV(uiSrvDesc);
	uiTexture->CreateUAV(uiUavDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	uiTexture->CreateRTV(rtvDesc);

	// PIXL Physical Camera metering resources. A compact 256-bin histogram
	// feeds a single persistent exposure value; no CPU readback is required.
	{
		D3D11_TEXTURE2D_DESC histogramDesc{};
		histogramDesc.Width = 256;
		histogramDesc.Height = 1;
		histogramDesc.MipLevels = 1;
		histogramDesc.ArraySize = 1;
		histogramDesc.Format = DXGI_FORMAT_R32_UINT;
		histogramDesc.SampleDesc.Count = 1;
		histogramDesc.Usage = D3D11_USAGE_DEFAULT;
		histogramDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC histogramSrv{};
		histogramSrv.Format = histogramDesc.Format;
		histogramSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		histogramSrv.Texture2D.MipLevels = 1;
		D3D11_UNORDERED_ACCESS_VIEW_DESC histogramUav{};
		histogramUav.Format = histogramDesc.Format;
		histogramUav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		cameraHistogramTexture = new Texture2D(histogramDesc, "PIXL Camera::Histogram");
		cameraHistogramTexture->CreateSRV(histogramSrv);
		cameraHistogramTexture->CreateUAV(histogramUav);

		D3D11_TEXTURE2D_DESC exposureDesc = histogramDesc;
		exposureDesc.Width = 1;
		exposureDesc.Format = DXGI_FORMAT_R32_FLOAT;
		D3D11_SHADER_RESOURCE_VIEW_DESC exposureSrv = histogramSrv;
		exposureSrv.Format = exposureDesc.Format;
		D3D11_UNORDERED_ACCESS_VIEW_DESC exposureUav = histogramUav;
		exposureUav.Format = exposureDesc.Format;

		cameraExposureTexture = new Texture2D(exposureDesc, "PIXL Camera::Exposure");
		cameraExposureTexture->CreateSRV(exposureSrv);
		cameraExposureTexture->CreateUAV(exposureUav);
		const FLOAT initialExposure[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		globals::d3d::context->ClearUnorderedAccessViewFloat(cameraExposureTexture->uav.get(), initialExposure);
	}

	SetupCameraFinishingResources(hdrTexture->desc);

	D3D11_SAMPLER_DESC finishingSamplerDesc{};
	finishingSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	finishingSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	finishingSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	finishingSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	finishingSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&finishingSamplerDesc, lookSampler.put()));
	Util::SetResourceName(lookSampler.get(), "PIXL Camera::LinearClampSampler");

	hdrDataCB = new ConstantBuffer(ConstantBufferDesc<HDRDataCB>(), "HDR::DataCB");

	UpdateHDRData();

	// The presentation composite is required by framebuffer/UI redirection, so
	// validate it during setup. Optional finishing shaders compile on their first
	// real dispatch instead: disabled bloom, Stormglass and frame-generation UI
	// paths should not add shader compilation work merely because CameraSuite is
	// loaded. Every execution path below already obtains its shader through the
	// corresponding Get*CS() helper.
	GetHDROutputCS();
	LoadLookTexture();
	LoadElementalLensTextures();

	UpgradeLDRRenderTargets();
}

void CameraSuite::SetupCameraFinishingResources(const D3D11_TEXTURE2D_DESC& sceneDesc)
{
	auto releaseTexture = [](Texture2D*& texture) {
		delete texture;
		texture = nullptr;
	};
	for (auto** texture : { &cameraLocalExposureTexture, &stormglassFieldTexture, &bloomHalfTexture, &bloomQuarterTexture,
		     &bloomEighthTexture, &bloomSixteenthTexture, &bloomEighthScratchTexture,
		     &bloomQuarterScratchTexture, &bloomHalfScratchTexture })
		releaseTexture(*texture);

	auto createTexture = [](UINT width, UINT height, DXGI_FORMAT format, const char* name) -> Texture2D* {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = std::max(width, 1u);
		desc.Height = std::max(height, 1u);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		try {
			auto texture = std::make_unique<Texture2D>(desc, name);
			texture->CreateSRV(srvDesc);
			texture->CreateUAV(uavDesc);
			return texture.release();
		} catch (const std::exception& error) {
			logger::warn("[PIXL Camera] Could not create {} (format {}): {}", name, static_cast<int>(format), error.what());
			return nullptr;
		}
	};

	const UINT halfWidth = (sceneDesc.Width + 1u) / 2u;
	const UINT halfHeight = (sceneDesc.Height + 1u) / 2u;
	const UINT quarterWidth = (halfWidth + 1u) / 2u;
	const UINT quarterHeight = (halfHeight + 1u) / 2u;
	const UINT eighthWidth = (quarterWidth + 1u) / 2u;
	const UINT eighthHeight = (quarterHeight + 1u) / 2u;
	const UINT sixteenthWidth = (eighthWidth + 1u) / 2u;
	const UINT sixteenthHeight = (eighthHeight + 1u) / 2u;

	DXGI_FORMAT bloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	UINT formatSupport = 0;
	if (SUCCEEDED(globals::d3d::device->CheckFormatSupport(DXGI_FORMAT_R11G11B10_FLOAT, &formatSupport)) &&
		(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0 &&
		(formatSupport & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0)
		bloomFormat = DXGI_FORMAT_R11G11B10_FLOAT;

	auto createBloomSet = [&](DXGI_FORMAT format) {
		bloomHalfTexture = createTexture(halfWidth, halfHeight, format, "PIXL Camera::BloomHalf");
		bloomQuarterTexture = createTexture(quarterWidth, quarterHeight, format, "PIXL Camera::BloomQuarter");
		bloomEighthTexture = createTexture(eighthWidth, eighthHeight, format, "PIXL Camera::BloomEighth");
		bloomSixteenthTexture = createTexture(sixteenthWidth, sixteenthHeight, format, "PIXL Camera::BloomSixteenth");
		bloomEighthScratchTexture = createTexture(eighthWidth, eighthHeight, format, "PIXL Camera::BloomEighthScratch");
		bloomQuarterScratchTexture = createTexture(quarterWidth, quarterHeight, format, "PIXL Camera::BloomQuarterScratch");
		bloomHalfScratchTexture = createTexture(halfWidth, halfHeight, format, "PIXL Camera::BloomHalfScratch");
		return bloomHalfTexture && bloomQuarterTexture && bloomEighthTexture && bloomSixteenthTexture &&
		       bloomEighthScratchTexture && bloomQuarterScratchTexture && bloomHalfScratchTexture;
	};

	if (!createBloomSet(bloomFormat) && bloomFormat != DXGI_FORMAT_R16G16B16A16_FLOAT) {
		for (auto** texture : { &bloomHalfTexture, &bloomQuarterTexture, &bloomEighthTexture,
			     &bloomSixteenthTexture, &bloomEighthScratchTexture, &bloomQuarterScratchTexture,
			     &bloomHalfScratchTexture })
			releaseTexture(*texture);
		bloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		createBloomSet(bloomFormat);
	}

	cameraLocalExposureTexture = createTexture(quarterWidth, quarterHeight, DXGI_FORMAT_R16_FLOAT, "PIXL Camera::LocalExposure");
	stormglassFieldTexture = createTexture(quarterWidth, quarterHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, "PIXL Camera::StormglassField");
	logger::info("[PIXL Camera] Finishing resources: bloom={}x{} format={}, local exposure={}x{}, Stormglass={}x{}",
		halfWidth, halfHeight, static_cast<int>(bloomFormat), quarterWidth, quarterHeight, quarterWidth, quarterHeight);
}

void CameraSuite::BeginUIRendering()
{
	// Skip if D3D12 frame gen is active - it has its own UI buffer handling
	if (globals::pipeline::imageReconstruction.d3d12SwapChainActive)
		return;

	if (renderingUI)
		return;

	if (!uiTexture || !uiTexture->rtv)
		return;

	auto context = globals::d3d::context;

	if (savedRTV) {
		savedRTV->Release();
		savedRTV = nullptr;
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}

	context->OMGetRenderTargets(1, &savedRTV, &savedDSV);

	// Do NOT clear - vanilla UI has already rendered to uiTexture via SetUIBuffer()
	// Just ensure ImGui also renders to the same texture
	ID3D11RenderTargetView* rtv = uiTexture->rtv.get();
	context->OMSetRenderTargets(1, &rtv, nullptr);

	renderingUI = true;
}

void CameraSuite::EndUIRendering()
{
	if (globals::pipeline::imageReconstruction.d3d12SwapChainActive)
		return;

	if (!renderingUI)
		return;

	auto context = globals::d3d::context;

	context->OMSetRenderTargets(1, &savedRTV, savedDSV);

	if (savedRTV) {
		savedRTV->Release();
		savedRTV = nullptr;
	}
	if (savedDSV) {
		savedDSV->Release();
		savedDSV = nullptr;
	}

	renderingUI = false;
}

void CameraSuite::RedirectFramebuffer()
{
	if (!NeedsPresentationComposite() || !hdrTexture || !hdrTexture->rtv)
		return;

	// SDR main/loading imagery is already display-referred and is composed into
	// Skyrim's native framebuffer. Redirecting ISHDR to the scene-linear camera
	// target leaves that layer stale or black on several menu render paths.
	auto* ui = globals::game::ui;
	if (!settings.enableHDR && globals::state->IsDisplayReferredModelMenuOpen(ui))
		return;

	if (!GetHDROutputCS())
		return;

	if (framebufferRedirected)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	savedFramebufferTexture = fb.texture;
	savedFramebufferSRV = fb.SRV;
	savedFramebufferRTV = fb.RTV;

	// Redirect to hdrTexture (R16G16B16A16_FLOAT) so ISHDR can write values >1.0
	fb.texture = reinterpret_cast<ID3D11Texture2D*>(hdrTexture->resource.get());
	fb.SRV = hdrTexture->srv.get();
	fb.RTV = hdrTexture->rtv.get();

	framebufferRedirected = true;
}

void CameraSuite::RestoreFramebuffer()
{
	if (!framebufferRedirected)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	fb.texture = savedFramebufferTexture;
	fb.SRV = savedFramebufferSRV;
	fb.RTV = savedFramebufferRTV;

	savedFramebufferTexture = nullptr;
	savedFramebufferSRV = nullptr;
	savedFramebufferRTV = nullptr;
	framebufferRedirected = false;
}

bool CameraSuite::IsFGCompositingThisFrame() const
{
	return globals::pipeline::imageReconstruction.ShouldUseFrameGenerationThisFrame();
}

CameraSuite::D3D12UIBufferMode CameraSuite::GetD3D12UIBufferMode()
{
	D3D12UIBufferMode mode;
	if (!globals::pipeline::imageReconstruction.d3d12SwapChainActive)
		return mode;

	const bool hdrReady = loaded && hdrDataCB && outputTexture;
	const bool hdrShaderAvailable = hdrReady && GetHDROutputCS() != nullptr;

	mode.useUIBuffer = hdrShaderAvailable || IsFGCompositingThisFrame();
	mode.useFallbackCopy = hdrReady && !hdrShaderAvailable;
	return mode;
}

bool CameraSuite::ShouldUseD3D12UIBuffer()
{
	return GetD3D12UIBufferMode().useUIBuffer;
}

void CameraSuite::SetUIBuffer()
{
	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	// D3D12 swap chain path: route UI to uiBufferWrapped only when a compositor
	// (ApplyHDR or FFX FG UI composition) will read it; otherwise render UI
	// directly into the wrapped back buffer so it survives Present when both
	// compositors are skipped (HDR unloaded + FG off/paused). If HDR is loaded
	// but the shader is missing, keep UI in kFRAMEBUFFER so the ApplyHDR
	// fallback copy carries it to the wrapped back buffer.
	if (globals::pipeline::imageReconstruction.d3d12SwapChainActive) {
		auto& imageReconstruction = globals::pipeline::imageReconstruction;
		if (!imageReconstruction.dx12SwapChain.swapChainBufferWrapped || !imageReconstruction.dx12SwapChain.swapChainBufferWrapped->rtv)
			return;

		const auto uiBufferMode = GetD3D12UIBufferMode();

		if (uiBufferMode.useUIBuffer && (!imageReconstruction.dx12SwapChain.uiBufferWrapped || !imageReconstruction.dx12SwapChain.uiBufferWrapped->rtv))
			return;

		ID3D11RenderTargetView* targetRTV = uiBufferMode.useUIBuffer ?
		                                        imageReconstruction.dx12SwapChain.uiBufferWrapped->rtv :
		                                    uiBufferMode.useFallbackCopy ? fb.RTV :
		                                                                   imageReconstruction.dx12SwapChain.swapChainBufferWrapped->rtv;

		if (uiBufferMode.useUIBuffer) {
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			globals::d3d::context->ClearRenderTargetView(targetRTV, clearColor);
		}

		fb.RTV = targetRTV;
		globals::d3d::context->OMSetRenderTargets(1, &fb.RTV, nullptr);
		return;
	}

	// Vanilla output: UI composites directly to kFRAMEBUFFER when no PIXL
	// presentation effect needs a separate scene/UI composite.
	if (!NeedsPresentationComposite())
		return;

	// Don't redirect if the HDR compute shader isn't available - vanilla UI path works without it
	if (!GetHDROutputCS())
		return;

	if (!uiTexture || !uiTexture->rtv || !hdrDataCB || !outputTexture)
		return;

	if (!savedFramebufferRTV) {
		savedFramebufferRTV = fb.RTV;
	}

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	fb.RTV = uiTexture->rtv.get();
	globals::d3d::context->OMSetRenderTargets(1, &fb.RTV, nullptr);
}

bool CameraSuite::UsesDeferredPresentComposite() const
{
	return loaded && NeedsPresentationComposite() &&
	       !globals::pipeline::imageReconstruction.d3d12SwapChainActive && uiTexture && uiTexture->rtv && hdrOutputCS;
}

bool CameraSuite::NeedsPresentationComposite() const
{
	const bool bloom = settings.enableBloom && settings.bloomStrength > 1e-4f;
	const bool look = settings.lookPreset > 0u && settings.lookOpacity > 1e-4f;
	const bool environment = settings.enableStormglass || settings.enableSubmergedOptics ||
		settings.enableColdLens || settings.enableElementalDamageLens;
	const bool motionBlur = settings.enableModernMotionBlur && settings.motionBlurStrength > 1e-4f;
	return settings.enableHDR || settings.enablePhysicalCamera || bloom || look || environment || motionBlur;
}

void CameraSuite::SyncFramebufferUIRedirect()
{
	if (!uiTexture || !uiTexture->rtv)
		return;

	auto& fb = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
	fb.RTV = uiTexture->rtv.get();
	globals::d3d::context->OMSetRenderTargets(1, &fb.RTV, nullptr);
}

namespace
{
	struct PresentSuppressionScope
	{
		PresentSuppressionScope() { globals::pipeline::cameraSuite.SetPresentSuppressed(true); }
		~PresentSuppressionScope() { globals::pipeline::cameraSuite.SetPresentSuppressed(false); }
	};

	struct SwapChainPresentBottom
	{
		static HRESULT WINAPI thunk(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
		{
			if (globals::pipeline::cameraSuite.IsPresentSuppressed())
				return S_OK;

			return func(This, SyncInterval, Flags);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Vtable slot 35 â€” patches alpha blend to [One, InvSrcAlpha, Add] when UI is composited from a
	// separate buffer. Mods using [InvSrcAlpha, Zero] (e.g. IED) write alpha*(1-alpha) instead
	// of alpha, causing ~94% scene bleed through opaque windows in the HDROutputCS composite.
	struct ID3D11DeviceContext_OMSetBlendState
	{
		static void WINAPI thunk(ID3D11DeviceContext* This, ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
		{
			if (pBlendState) {
				auto& hdr = globals::pipeline::cameraSuite;
				const bool d3d11HdrCapture = hdr.loaded && hdr.NeedsPresentationComposite() && hdr.uiTexture;
				const bool fgCapture = globals::pipeline::imageReconstruction.d3d12SwapChainActive;
				if (d3d11HdrCapture || fgCapture)
					pBlendState = hdr.GetPatchedAlphaBlendState(pBlendState);
			}
			func(This, pBlendState, BlendFactor, SampleMask);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void CameraSuite::InstallSwapChainPresentHooks(IDXGISwapChain* swapChain)
{
	stl::detour_vfunc<8, SwapChainPresentBottom>(swapChain);
	stl::detour_vfunc<35, ID3D11DeviceContext_OMSetBlendState>(globals::d3d::context);
}

ID3D11BlendState* CameraSuite::GetPatchedAlphaBlendState(ID3D11BlendState* original)
{
	auto it = patchedBlendStateCache.find(original);
	if (it != patchedBlendStateCache.end())
		return it->second ? it->second.get() : original;

	D3D11_BLEND_DESC desc{};
	original->GetDesc(&desc);

	const int slotCount = desc.IndependentBlendEnable ? 8 : 1;
	bool needsPatch = false;
	for (int i = 0; i < slotCount; i++) {
		const auto& rt = desc.RenderTarget[i];
		if (rt.BlendEnable &&
			(rt.SrcBlendAlpha != D3D11_BLEND_ONE ||
				rt.DestBlendAlpha != D3D11_BLEND_INV_SRC_ALPHA ||
				rt.BlendOpAlpha != D3D11_BLEND_OP_ADD)) {
			needsPatch = true;
			break;
		}
	}

	if (!needsPatch) {
		patchedBlendStateCache[original] = nullptr;
		return original;
	}

	for (int i = 0; i < slotCount; i++) {
		auto& rt = desc.RenderTarget[i];
		if (rt.BlendEnable) {
			rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		}
	}
	if (!desc.IndependentBlendEnable) {
		for (int i = 1; i < 8; i++)
			desc.RenderTarget[i] = desc.RenderTarget[0];
	}

	winrt::com_ptr<ID3D11BlendState> patched;
	if (FAILED(globals::d3d::device->CreateBlendState(&desc, patched.put()))) {
		patchedBlendStateCache[original] = nullptr;
		return original;
	}

	auto* raw = patched.get();
	patchedBlendStateCache[original] = std::move(patched);
	return raw;
}

HRESULT CameraSuite::PresentToSwapChain(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
	return SwapChainPresentBottom::func(swapChain, syncInterval, flags);
}

void CameraSuite::DrawImGuiForPresent(bool frameGenActive, bool hdrReady)
{
	if (frameGenActive) {
		// The sidecar invokes this after ApplyHDR has copied the finished scene.
		// kFRAMEBUFFER is no longer the presented target at this stage.
		auto& sidecar = globals::pipeline::imageReconstruction.dx12SwapChain;
		if (sidecar.swapChainBufferWrapped && sidecar.swapChainBufferWrapped->rtv) {
			auto* target = sidecar.swapChainBufferWrapped->rtv;
			globals::d3d::context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(sidecar.swapChainDesc.Width);
			viewport.Height = static_cast<float>(sidecar.swapChainDesc.Height);
			viewport.MaxDepth = 1.0f;
			globals::d3d::context->RSSetViewports(1, &viewport);
		}
	} else if (hdrReady && uiTexture && uiTexture->rtv && uiTexture->resource) {
		ID3D11RenderTargetView* uiRTV = uiTexture->rtv.get();
		D3D11_TEXTURE2D_DESC texDesc{};
		uiTexture->resource->GetDesc(&texDesc);

		if (texDesc.Width > 0) {
			globals::d3d::context->OMSetRenderTargets(1, &uiRTV, nullptr);

			D3D11_VIEWPORT uiViewport{};
			uiViewport.Width = static_cast<float>(texDesc.Width);
			uiViewport.Height = static_cast<float>(texDesc.Height);
			uiViewport.MinDepth = 0.0f;
			uiViewport.MaxDepth = 1.0f;
			globals::d3d::context->RSSetViewports(1, &uiViewport);
		}
	} else {
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		globals::d3d::context->OMSetRenderTargets(1, &data.RTV, nullptr);
	}
}

void CameraSuite::RunHDRBeforePresentChain(bool hdrReady)
{
	if (!hdrReady)
		return;

	ID3D11RenderTargetView* nullRTV = nullptr;
	globals::d3d::context->OMSetRenderTargets(1, &nullRTV, nullptr);
	ApplyHDR();
}

HRESULT CameraSuite::RunPresentChainWithHDR(
	IDXGISwapChain* swapChain,
	UINT syncInterval,
	UINT flags,
	bool hdrReady,
	bool frameGenActive,
	const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain)
{
	if (UsesDeferredPresentComposite()) {
		SyncFramebufferUIRedirect();
		{
			PresentSuppressionScope suppress;
			const HRESULT suppressedResult = presentChain(swapChain, syncInterval, flags);
			if (FAILED(suppressedResult))
				logger::warn("Suppressed presentChain returned {:08X} (expected S_OK)", static_cast<unsigned>(suppressedResult));
		}

		ID3D11RenderTargetView* nullRTV = nullptr;
		globals::d3d::context->OMSetRenderTargets(1, &nullRTV, nullptr);
		ApplyHDR();
		const HRESULT retval = PresentToSwapChain(swapChain, syncInterval, flags);
		ClearUIBuffer();
		return retval;
	}

	RunHDRBeforePresentChain(hdrReady);

	if (hdrReady) {
		if (!frameGenActive) {
			ClearUIBuffer();
		}
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		globals::d3d::context->OMSetRenderTargets(1, &data.RTV, nullptr);
	}

	return presentChain(swapChain, syncInterval, flags);
}

HRESULT CameraSuite::HandleSwapChainPresent(
	IDXGISwapChain* swapChain,
	UINT syncInterval,
	UINT flags,
	const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain)
{
	const bool frameGenActive = globals::pipeline::imageReconstruction.d3d12SwapChainActive;
	const bool hdrReady = loaded && hdrDataCB && outputTexture && (NeedsPresentationComposite() || frameGenActive);

	DrawRendererUIForPresent();

	return RunPresentChainWithHDR(swapChain, syncInterval, flags, hdrReady, frameGenActive, presentChain);
}

void CameraSuite::DrawRendererUIForPresent()
{
	const bool frameGenActive = globals::pipeline::imageReconstruction.d3d12SwapChainActive;
	const bool hdrReady = loaded && hdrDataCB && outputTexture && (NeedsPresentationComposite() || frameGenActive);

	D3D11_VIEWPORT savedViewport{};
	UINT viewportCount = 1;
	globals::d3d::context->RSGetViewports(&viewportCount, &savedViewport);

	DrawImGuiForPresent(frameGenActive, hdrReady);
	globals::menu->DrawOverlay();
	if (viewportCount > 0)
		globals::d3d::context->RSSetViewports(1, &savedViewport);
}

void CameraSuite::ClearUIBuffer()
{
	if (globals::pipeline::imageReconstruction.d3d12SwapChainActive)
		return;

	if (!uiTexture || !uiTexture->rtv)
		return;

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	globals::d3d::context->ClearRenderTargetView(uiTexture->rtv.get(), clearColor);

	if (savedFramebufferRTV) {
		auto& data = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
		data.RTV = savedFramebufferRTV;
		savedFramebufferRTV = nullptr;
	}
}

void CameraSuite::ApplyHDR()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "HDR Processing");

	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!hdrDataCB || !hdrTexture || !outputTexture)
		return;

	auto& imageReconstruction = globals::pipeline::imageReconstruction;

	auto context = globals::d3d::context;
	auto state = globals::state;
	auto renderer = globals::game::renderer;

	UpdateHDRData();

	state->BeginPerfEvent("HDR Processing");

	{
		// When HDR is enabled, ISHDR wrote to hdrTexture (float16, values >1.0 preserved).
		// When SDR, ISHDR wrote to kFRAMEBUFFER (UNORM, tonemapped 0-1).
		auto& framebufferRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];

		// Scene SRV selection:
		// - HDR: hdrTexture has float16 scene values >1.0 preserved from ISHDR.
		// - SDR: kFRAMEBUFFER has the tonemapped 0-1 ISHDR output.
		auto* ui = globals::game::ui;
		const bool displayReferredMenu = globals::state->IsDisplayReferredModelMenuOpen(ui);
		ID3D11ShaderResourceView* sceneSRV =
			(!settings.enableHDR && displayReferredMenu) ? framebufferRT.SRV :
			(NeedsPresentationComposite() && hdrTexture && hdrTexture->srv) ? hdrTexture->srv.get() :
																			framebufferRT.SRV;

		// Choose the correct UI buffer based on which path is active.
		ID3D11ShaderResourceView* uiSRV = nullptr;
		if (imageReconstruction.d3d12SwapChainActive && imageReconstruction.dx12SwapChain.uiBufferWrapped) {
			uiSRV = imageReconstruction.dx12SwapChain.uiBufferWrapped->srv;
		} else if (uiTexture && uiTexture->srv) {
			uiSRV = uiTexture->srv.get();
		}

		if (!GetHDROutputCS()) {
			// Fallback: HDR shader files not present - copy kFRAMEBUFFER directly to output
			if (imageReconstruction.d3d12SwapChainActive) {
				// SetUIBuffer keeps non-FG fallback UI in kFRAMEBUFFER; FG keeps using
				// uiBufferWrapped for FidelityFX UI composition.
				context->CopyResource(imageReconstruction.dx12SwapChain.swapChainBufferWrapped->resource11, framebufferRT.texture);
			} else {
				ID3D11Texture2D* backBuffer = nullptr;
				HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
				if (SUCCEEDED(hr) && backBuffer) {
					context->CopyResource(backBuffer, framebufferRT.texture);
					backBuffer->Release();
				}
			}

			state->EndPerfEvent();
			return;
		}

		UpdatePhysicalCameraExposure(sceneSRV);
		RunCameraFinishingPasses(sceneSRV);
		// Auxiliary readiness changes per frame (menus, disabled controls and
		// allocation/compile fallbacks), so refresh the ABI-stable presentation CB.
		hdrDataCB->Update(BuildHDRData());
		DispatchHDROutput(sceneSRV, uiSRV, outputTexture->uav.get());
	}

	if (imageReconstruction.d3d12SwapChainActive) {
		if (imageReconstruction.dx12SwapChain.swapChainBufferWrapped &&
			imageReconstruction.dx12SwapChain.swapChainBufferWrapped->resource11 &&
			outputTexture && outputTexture->resource) {
			context->CopyResource(imageReconstruction.dx12SwapChain.swapChainBufferWrapped->resource11, outputTexture->resource.get());
		}
	} else {
		ID3D11Texture2D* backBuffer = nullptr;
		HRESULT hr = globals::d3d::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(hr) && backBuffer) {
			context->CopyResource(backBuffer, outputTexture->resource.get());
			backBuffer->Release();
		}
	}

	state->EndPerfEvent();
}

void CameraSuite::LoadLookTexture()
{
	lookTextureView = nullptr;
	if (settings.lookPreset == 0)
		return;

	constexpr std::array<const wchar_t*, 12> paths{
		L"", L"Data\\Shaders\\CameraSuite\\Looks\\NordicNeutral.png", L"Data\\Shaders\\CameraSuite\\Looks\\Saga.png",
		L"Data\\Shaders\\CameraSuite\\Looks\\Dramatic.png", L"Data\\Shaders\\CameraSuite\\Looks\\Hearthfire.png", L"Data\\Shaders\\CameraSuite\\Looks\\Bleak.png",
		L"Data\\Shaders\\CameraSuite\\Looks\\Bleach.png", L"Data\\Shaders\\CameraSuite\\Looks\\Winter.png", L"Data\\Shaders\\CameraSuite\\Looks\\Sunset.png",
		L"Data\\Shaders\\CameraSuite\\Looks\\FantasyGreen.png", L"Data\\Shaders\\CameraSuite\\Looks\\Nightfall.png", L"Data\\Shaders\\CameraSuite\\Looks\\Cinematic.png"
	};
	const uint index = std::min(settings.lookPreset, static_cast<uint>(paths.size() - 1));
	// LUT bytes are sampled as encoded grading values and explicitly decoded in
	// HLSL, so prevent WIC metadata from silently creating an sRGB SRV.
	const HRESULT result = DirectX::CreateWICTextureFromFileEx(
		globals::d3d::device,
		paths[index],
		0,
		D3D11_USAGE_IMMUTABLE,
		D3D11_BIND_SHADER_RESOURCE,
		0,
		0,
		DirectX::WIC_LOADER_IGNORE_SRGB,
		nullptr,
		lookTextureView.put());
	if (FAILED(result) || !lookTextureView) {
		logger::error("[LUT / Tonemap] Could not load selected LUT {} (HRESULT 0x{:08X}); using Original", index, static_cast<uint32_t>(result));
		settings.lookPreset = 0;
	} else {
		logger::info("[LUT / Tonemap] Loaded PIXL LUT {}", index);
	}
}

void CameraSuite::LoadElementalLensTextures()
{
	frostLensTextureView = nullptr;
	fireLensTextureView = nullptr;

	struct LensAsset
	{
		const wchar_t* path;
		const char* label;
		winrt::com_ptr<ID3D11ShaderResourceView>* view;
	};
	const LensAsset assets[] = {
		{ L"Data\\Shaders\\CameraSuite\\Lens\\FrostMask.png", "frost", &frostLensTextureView },
		{ L"Data\\Shaders\\CameraSuite\\Lens\\FireMask.png", "fire", &fireLensTextureView }
	};

	for (const auto& asset : assets) {
		// The artwork is display-colour RGBA. Force an sRGB SRV so its RGB is
		// linearised before it is composited over PIXL's scene-linear image; alpha
		// remains the authored optical coverage.
		const HRESULT result = DirectX::CreateWICTextureFromFileEx(
			globals::d3d::device,
			asset.path,
			0,
			D3D11_USAGE_IMMUTABLE,
			D3D11_BIND_SHADER_RESOURCE,
			0,
			0,
			DirectX::WIC_LOADER_FORCE_SRGB,
			nullptr,
			asset.view->put());
		if (FAILED(result) || !*asset.view) {
			logger::warn(
				"[PIXL Camera] Optional {} lens artwork unavailable (HRESULT 0x{:08X}); using procedural fallback",
				asset.label,
				static_cast<std::uint32_t>(result));
		} else {
			logger::info("[PIXL Camera] Loaded authored {} lens artwork", asset.label);
		}
	}
}

void CameraSuite::DispatchHDROutput(ID3D11ShaderResourceView* sceneSRV, ID3D11ShaderResourceView* uiSRV, ID3D11UnorderedAccessView* uav)
{
	auto context = globals::d3d::context;
	auto computeShader = GetHDROutputCS();
	if (!computeShader || !uav)
		return;

	ID3D11ShaderResourceView* views[9] = {
		sceneSRV,
		uiSRV,
		cameraExposureTexture ? cameraExposureTexture->srv.get() : nullptr,
		lookTextureView.get(),
		bloomPassReady && bloomHalfScratchTexture ? bloomHalfScratchTexture->srv.get() : nullptr,
		localExposurePassReady && cameraLocalExposureTexture ? cameraLocalExposureTexture->srv.get() : nullptr,
		stormglassPassReady && stormglassFieldTexture ? stormglassFieldTexture->srv.get() : nullptr,
		frostLensTextureView.get(),
		fireLensTextureView.get()
	};
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);
	// CameraSuite owns its DOF compositor and therefore binds scene depth at the
	// SharedData slot explicitly. This removes the old dependency on Skyrim
	// deciding to schedule ISDepthOfField and also survives post-process state
	// resets that clear the global t17 binding before Present.
	ID3D11ShaderResourceView* sceneDepthView = Util::GetCurrentSceneDepthSRV(true);
	context->CSSetShaderResources(17, 1, &sceneDepthView);
	ID3D11SamplerState* samplers[1] = { lookSampler.get() };
	context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	ID3D11UnorderedAccessView* uavs[1] = { uav };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };
	context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

	context->CSSetShader(computeShader, nullptr, 0);

	auto dispatchCount = Util::GetScreenDispatchCount(false);
	globals::profiler->BeginPass("CameraSuite::HDROutput");
	context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	globals::profiler->EndPass();

	for (auto& view : views)
		view = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);
	sceneDepthView = nullptr;
	context->CSSetShaderResources(17, 1, &sceneDepthView);
	samplers[0] = nullptr;
	context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	cbs[0] = nullptr;
	context->CSSetConstantBuffers(0, ARRAYSIZE(cbs), cbs);

	context->CSSetShader(nullptr, nullptr, 0);
}

void CameraSuite::SnapshotCleanScene()
{
	if (!hdrTexture || !hdrTexture->resource)
		return;

	const D3D11_TEXTURE2D_DESC& sceneDesc = hdrTexture->desc;

	// (Re)create on first use or when the scene texture changes.
	if (cleanSceneCapture) {
		const D3D11_TEXTURE2D_DESC& capDesc = cleanSceneCapture->desc;
		if (capDesc.Width != sceneDesc.Width || capDesc.Height != sceneDesc.Height || capDesc.Format != sceneDesc.Format) {
			cleanSceneCapture->srv = nullptr;
			cleanSceneCapture->resource = nullptr;
			delete cleanSceneCapture;
			cleanSceneCapture = nullptr;
		}
	}

	if (!cleanSceneCapture) {
		D3D11_TEXTURE2D_DESC capDesc = sceneDesc;
		capDesc.MipLevels = 1;
		capDesc.ArraySize = 1;
		capDesc.SampleDesc.Count = 1;
		capDesc.SampleDesc.Quality = 0;
		capDesc.Usage = D3D11_USAGE_DEFAULT;
		capDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		capDesc.CPUAccessFlags = 0;
		capDesc.MiscFlags = 0;

		cleanSceneCapture = new Texture2D(capDesc, "HDR::CleanSceneCapture");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = capDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		cleanSceneCapture->CreateSRV(srvDesc);
	}

	globals::d3d::context->CopyResource(cleanSceneCapture->resource.get(), hdrTexture->resource.get());
	cleanSceneCaptureFrame = globals::state->frameCount;
}

bool CameraSuite::IsCleanSceneCaptureFresh() const
{
	return cleanSceneCapture && cleanSceneCapture->srv && cleanSceneCaptureFrame == globals::state->frameCount;
}

ID3D11Texture2D* CameraSuite::ComposeCleanCapture(ID3D11ShaderResourceView* sceneSRV, bool sdrPreview)
{
	std::lock_guard<std::mutex> lock(settingsMutex);

	if (!NeedsPresentationComposite() || !sceneSRV || !hdrDataCB || !outputTexture || !outputTexture->uav || !outputTexture->resource)
		return nullptr;

	if (!GetHDROutputCS())
		return nullptr;

	// Null UI SRV (t1 samples as 0) leaves the scene alone: no UI, menu, or blur.
	HDRDataCB data = BuildHDRData();
	data.previewSDR = sdrPreview ? 1.f : 0.f;
	// Clean captures can target a different scene than the previous presentation
	// frame; never reuse stale low-resolution finishing surfaces.
	data.auxiliaryPassMask = 0.0f;
	hdrDataCB->Update(data);

	DispatchHDROutput(sceneSRV, nullptr, outputTexture->uav.get());

	// Restore the canonical CB for the display composite this frame.
	UpdateHDRData();

	return outputTexture->resource.get();
}

void CameraSuite::DestroyResources()
{
	lookTextureView = nullptr;
	lookSampler = nullptr;
	if (hdrTexture) {
		hdrTexture->srv = nullptr;
		hdrTexture->uav = nullptr;
		hdrTexture->rtv = nullptr;
		hdrTexture->resource = nullptr;
		delete hdrTexture;
		hdrTexture = nullptr;
	}

	if (outputTexture) {
		outputTexture->srv = nullptr;
		outputTexture->uav = nullptr;
		outputTexture->resource = nullptr;
		delete outputTexture;
		outputTexture = nullptr;
	}

	if (uiTexture) {
		uiTexture->srv = nullptr;
		uiTexture->uav = nullptr;
		uiTexture->rtv = nullptr;
		uiTexture->resource = nullptr;
		delete uiTexture;
		uiTexture = nullptr;
	}

	if (cleanSceneCapture) {
		cleanSceneCapture->srv = nullptr;
		cleanSceneCapture->resource = nullptr;
		delete cleanSceneCapture;
		cleanSceneCapture = nullptr;
		cleanSceneCaptureFrame = UINT32_MAX;
	}

	if (cameraHistogramTexture) {
		cameraHistogramTexture->srv = nullptr;
		cameraHistogramTexture->uav = nullptr;
		cameraHistogramTexture->resource = nullptr;
		delete cameraHistogramTexture;
		cameraHistogramTexture = nullptr;
	}

	if (cameraExposureTexture) {
		cameraExposureTexture->srv = nullptr;
		cameraExposureTexture->uav = nullptr;
		cameraExposureTexture->resource = nullptr;
		delete cameraExposureTexture;
		cameraExposureTexture = nullptr;
	}

	auto destroyTexture = [](Texture2D*& texture) {
		if (!texture)
			return;
		texture->srv = nullptr;
		texture->uav = nullptr;
		texture->resource = nullptr;
		delete texture;
		texture = nullptr;
	};
	destroyTexture(cameraLocalExposureTexture);
	destroyTexture(bloomHalfTexture);
	destroyTexture(bloomQuarterTexture);
	destroyTexture(bloomEighthTexture);
	destroyTexture(bloomSixteenthTexture);
	destroyTexture(bloomEighthScratchTexture);
	destroyTexture(bloomQuarterScratchTexture);
	destroyTexture(bloomHalfScratchTexture);
	destroyTexture(stormglassFieldTexture);
	localExposurePassReady = false;
	bloomPassReady = false;
	stormglassPassReady = false;

	if (hdrDataCB) {
		delete hdrDataCB;
		hdrDataCB = nullptr;
	}

	RestoreLDRRenderTargets();
}

void CameraSuite::UpgradeLDRRenderTargets()
{
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;

	static const RE::RENDER_TARGETS::RENDER_TARGET ldrTargets[] = {
		RE::RENDER_TARGETS::kLDR_DOWNSAMPLE0,
		RE::RENDER_TARGETS::kLDR_BLURSWAP,
		RE::RENDER_TARGETS::kBLURFULL_BUFFER,
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY,
		RE::RENDER_TARGETS::kIMAGESPACE_TEMP_COPY2,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_1,
		RE::RENDER_TARGETS::kTEMPORAL_AA_UI_ACCUMULATION_2,
	};

	for (auto targetId : ldrTargets) {
		auto& rt = renderer->GetRuntimeData().renderTargets[targetId];
		if (!rt.texture)
			continue;

		D3D11_TEXTURE2D_DESC origDesc{};
		rt.texture->GetDesc(&origDesc);

		if (origDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
			continue;

		SavedRenderTarget saved;
		saved.texture = rt.texture;
		saved.RTV = rt.RTV;
		saved.SRV = rt.SRV;
		saved.UAV = rt.UAV;

		D3D11_TEXTURE2D_DESC newDesc = origDesc;
		newDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		ID3D11Texture2D* newTexture = nullptr;
		if (FAILED(device->CreateTexture2D(&newDesc, nullptr, &newTexture)))
			continue;

		ID3D11RenderTargetView* newRTV = nullptr;
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		if (FAILED(device->CreateRenderTargetView(newTexture, &rtvDesc, &newRTV))) {
			newTexture->Release();
			continue;
		}

		ID3D11ShaderResourceView* newSRV = nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(newTexture, &srvDesc, &newSRV))) {
			newRTV->Release();
			newTexture->Release();
			continue;
		}

		ID3D11UnorderedAccessView* newUAV = nullptr;
		if (rt.UAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			rt.UAV->GetDesc(&uavDesc);
			uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

			if (FAILED(device->CreateUnorderedAccessView(newTexture, &uavDesc, &newUAV))) {
				newSRV->Release();
				newRTV->Release();
				newTexture->Release();
				continue;
			}
		}

		rt.texture = newTexture;
		rt.RTV = newRTV;
		rt.SRV = newSRV;
		rt.UAV = newUAV;

		savedLDRTargets.push_back({ targetId, saved });
		logger::debug("[HDR] Upgraded render target {} to R16G16B16A16_FLOAT (was format {})", static_cast<int>(targetId), static_cast<int>(origDesc.Format));
	}
}

void CameraSuite::RestoreLDRRenderTargets()
{
	auto renderer = globals::game::renderer;

	for (auto& [targetId, saved] : savedLDRTargets) {
		auto& rt = renderer->GetRuntimeData().renderTargets[targetId];

		if (rt.texture)
			rt.texture->Release();
		if (rt.RTV)
			rt.RTV->Release();
		if (rt.SRV)
			rt.SRV->Release();
		if (rt.UAV)
			rt.UAV->Release();

		rt.texture = saved.texture;
		rt.RTV = saved.RTV;
		rt.SRV = saved.SRV;
		rt.UAV = saved.UAV;
	}
	savedLDRTargets.clear();
}

void CameraSuite::ClearShaderCache()
{
	if (hdrOutputCS) {
		hdrOutputCS->Release();
		hdrOutputCS = nullptr;
	}
	if (uiBrightnessCS) {
		uiBrightnessCS->Release();
		uiBrightnessCS = nullptr;
	}
	if (physicalCameraHistogramCS) {
		physicalCameraHistogramCS->Release();
		physicalCameraHistogramCS = nullptr;
	}
	if (physicalCameraExposureCS) {
		physicalCameraExposureCS->Release();
		physicalCameraExposureCS = nullptr;
	}
	if (physicalCameraLocalExposureCS) {
		physicalCameraLocalExposureCS->Release();
		physicalCameraLocalExposureCS = nullptr;
	}
	if (bloomPrefilterCS) {
		bloomPrefilterCS->Release();
		bloomPrefilterCS = nullptr;
	}
	if (bloomDownsampleCS) {
		bloomDownsampleCS->Release();
		bloomDownsampleCS = nullptr;
	}
	if (bloomUpsampleCS) {
		bloomUpsampleCS->Release();
		bloomUpsampleCS = nullptr;
	}
	if (stormglassFieldCS) {
		stormglassFieldCS->Release();
		stormglassFieldCS = nullptr;
	}
}

ID3D11ComputeShader* CameraSuite::GetHDROutputCS()
{
	if (!hdrOutputCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		hdrOutputCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CameraSuite\\HDROutputCS.hlsl", defines, "cs_5_0"));
		if (!hdrOutputCS) {
			logger::error("HDR: Failed to compile HDROutputCS.hlsl");
		}
	}
	return hdrOutputCS;
}

ID3D11ComputeShader* CameraSuite::GetPhysicalCameraHistogramCS()
{
	if (!physicalCameraHistogramCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		physicalCameraHistogramCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CameraSuite\\PhysicalCameraHistogramCS.hlsl", defines, "cs_5_0"));
		if (!physicalCameraHistogramCS)
			logger::error("PIXL Physical Camera: Failed to compile PhysicalCameraHistogramCS.hlsl");
	}
	return physicalCameraHistogramCS;
}

ID3D11ComputeShader* CameraSuite::GetPhysicalCameraExposureCS()
{
	if (!physicalCameraExposureCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		physicalCameraExposureCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CameraSuite\\PhysicalCameraExposureCS.hlsl", defines, "cs_5_0"));
		if (!physicalCameraExposureCS)
			logger::error("PIXL Physical Camera: Failed to compile PhysicalCameraExposureCS.hlsl");
	}
	return physicalCameraExposureCS;
}

ID3D11ComputeShader* CameraSuite::GetPhysicalCameraLocalExposureCS()
{
	if (!physicalCameraLocalExposureCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		physicalCameraLocalExposureCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\CameraSuite\\PhysicalCameraLocalExposureCS.hlsl", defines, "cs_5_0"));
		if (!physicalCameraLocalExposureCS)
			logger::error("PIXL Physical Camera: Failed to compile PhysicalCameraLocalExposureCS.hlsl");
	}
	return physicalCameraLocalExposureCS;
}

ID3D11ComputeShader* CameraSuite::GetBloomPrefilterCS()
{
	if (!bloomPrefilterCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		bloomPrefilterCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\CameraSuite\\BloomPrefilterCS.hlsl", defines, "cs_5_0"));
		if (!bloomPrefilterCS)
			logger::error("PIXL Camera: Failed to compile BloomPrefilterCS.hlsl");
	}
	return bloomPrefilterCS;
}

ID3D11ComputeShader* CameraSuite::GetBloomDownsampleCS()
{
	if (!bloomDownsampleCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		bloomDownsampleCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\CameraSuite\\BloomDownsampleCS.hlsl", defines, "cs_5_0"));
		if (!bloomDownsampleCS)
			logger::error("PIXL Camera: Failed to compile BloomDownsampleCS.hlsl");
	}
	return bloomDownsampleCS;
}

ID3D11ComputeShader* CameraSuite::GetBloomUpsampleCS()
{
	if (!bloomUpsampleCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		bloomUpsampleCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\CameraSuite\\BloomUpsampleCS.hlsl", defines, "cs_5_0"));
		if (!bloomUpsampleCS)
			logger::error("PIXL Camera: Failed to compile BloomUpsampleCS.hlsl");
	}
	return bloomUpsampleCS;
}

ID3D11ComputeShader* CameraSuite::GetStormglassFieldCS()
{
	if (!stormglassFieldCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		stormglassFieldCS = static_cast<ID3D11ComputeShader*>(
			Util::CompileShader(L"Data\\Shaders\\CameraSuite\\StormglassFieldCS.hlsl", defines, "cs_5_0"));
		if (!stormglassFieldCS)
			logger::error("PIXL Stormglass: Failed to compile StormglassFieldCS.hlsl");
	}
	return stormglassFieldCS;
}

void CameraSuite::UpdatePhysicalCameraExposure(ID3D11ShaderResourceView* sceneSRV)
{
	if (!settings.enablePhysicalCamera || !sceneSRV || !cameraExposureTexture || !cameraExposureTexture->uav || !hdrDataCB)
		return;

	auto* context = globals::d3d::context;
	auto* cb = hdrDataCB->CB();

	if (settings.cameraAutoExposure && cameraHistogramTexture && cameraHistogramTexture->uav && cameraHistogramTexture->srv) {
		const UINT clearHistogram[4] = { 0u, 0u, 0u, 0u };
		context->ClearUnorderedAccessViewUint(cameraHistogramTexture->uav.get(), clearHistogram);

		if (auto* histogramCS = GetPhysicalCameraHistogramCS()) {
			ID3D11ShaderResourceView* srv = sceneSRV;
			ID3D11UnorderedAccessView* uav = cameraHistogramTexture->uav.get();
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShader(histogramCS, nullptr, 0);

			D3D11_TEXTURE2D_DESC sceneDesc{};
			ID3D11Resource* sceneResource = nullptr;
			sceneSRV->GetResource(&sceneResource);
			ID3D11Texture2D* sceneTexture = nullptr;
			if (sceneResource) {
				sceneResource->QueryInterface(IID_PPV_ARGS(&sceneTexture));
				sceneResource->Release();
			}
			if (sceneTexture) {
				sceneTexture->GetDesc(&sceneDesc);
				sceneTexture->Release();
			}

			if (sceneDesc.Width && sceneDesc.Height) {
				constexpr std::array<UINT, 4> histogramStrides{ 8u, 6u, 5u, 4u };
				const UINT meterStride = histogramStrides[std::clamp(cameraQuality, 0u, 3u)];
				const UINT meterWidth = (sceneDesc.Width + meterStride - 1u) / meterStride;
				const UINT meterHeight = (sceneDesc.Height + meterStride - 1u) / meterStride;
				globals::profiler->BeginPass("CameraSuite::PhysicalCameraHistogram");
				context->Dispatch((meterWidth + 7u) / 8u, (meterHeight + 7u) / 8u, 1);
				globals::profiler->EndPass();
			}

			srv = nullptr;
			uav = nullptr;
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);
		}
	}

	if (auto* exposureCS = GetPhysicalCameraExposureCS()) {
		ID3D11ShaderResourceView* histogramSRV = settings.cameraAutoExposure && cameraHistogramTexture ? cameraHistogramTexture->srv.get() : nullptr;
		ID3D11UnorderedAccessView* exposureUAV = cameraExposureTexture->uav.get();
		context->CSSetShaderResources(0, 1, &histogramSRV);
		context->CSSetUnorderedAccessViews(0, 1, &exposureUAV, nullptr);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetShader(exposureCS, nullptr, 0);
		globals::profiler->BeginPass("CameraSuite::PhysicalCameraExposure");
		context->Dispatch(1, 1, 1);
		globals::profiler->EndPass();

		histogramSRV = nullptr;
		exposureUAV = nullptr;
		context->CSSetShaderResources(0, 1, &histogramSRV);
		context->CSSetUnorderedAccessViews(0, 1, &exposureUAV, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	ID3D11Buffer* nullCB = nullptr;
	context->CSSetConstantBuffers(0, 1, &nullCB);
}

void CameraSuite::RunCameraFinishingPasses(ID3D11ShaderResourceView* sceneSRV)
{
	localExposurePassReady = false;
	bloomPassReady = false;
	stormglassPassReady = false;

	const bool wantsPhysicalCamera = settings.enablePhysicalCamera;
	const bool wantsBloom = settings.enableBloom && settings.bloomStrength > 1e-4f;
	// Stormglass must follow the actual rendered exterior sky, not the generic
	// globals::state->inWorld flag. The diagnostic proved inWorld/map state can
	// be false/stale while Skyrim is visibly rendering an exterior rain scene.
	// Sky::Mode::kFull is the authoritative presentation test here.
	const auto* presentationSky = RE::Sky::GetSingleton();
	const bool displayModelMenuOpen = globals::state &&
		globals::state->IsDisplayReferredModelMenuOpen(globals::game::ui);
	const bool environmentPresentation =
		presentationSky &&
		presentationSky->mode.get() == RE::Sky::Mode::kFull &&
		!displayModelMenuOpen;
	const bool stormglassLensVisible = (1.0f - std::clamp(submergedBlendState, 0.0f, 1.0f) * 1.25f) > 0.001f;
	const bool wantsStormglass = settings.enableStormglass && environmentPresentation && stormglassLensVisible && stormglassFieldTexture &&
		std::max({ stormglassRainIntensityState, stormglassWetnessState, surfaceBreakFilmState }) > 0.002f;
	if ((!wantsPhysicalCamera && !wantsBloom && !wantsStormglass) || !sceneSRV || !hdrDataCB || !cameraExposureTexture ||
		globals::state->IsDisplayReferredModelMenuOpen(globals::game::ui))
		return;

	auto* context = globals::d3d::context;
	ID3D11Buffer* cb = hdrDataCB->CB();
	ID3D11SamplerState* sampler = lookSampler.get();

	auto dispatchPass = [&](const char* profilerName,
		ID3D11ComputeShader* shader,
		ID3D11ShaderResourceView* const* srvs,
		UINT srvCount,
		Texture2D* output) -> bool {
		if (!shader || !output || !output->uav)
			return false;

		context->CSSetShaderResources(0, srvCount, srvs);
		ID3D11UnorderedAccessView* uav = output->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(shader, nullptr, 0);
		globals::profiler->BeginPass(profilerName);
		context->Dispatch((output->desc.Width + 7u) / 8u, (output->desc.Height + 7u) / 8u, 1u);
		globals::profiler->EndPass();

		ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, srvCount, nullSrvs);
		uav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
		return true;
	};

	if (wantsPhysicalCamera && settings.cameraLocalExposure > 1e-4f && cameraLocalExposureTexture) {
		ID3D11ShaderResourceView* localSrvs[2] = { sceneSRV, cameraExposureTexture->srv.get() };
		localExposurePassReady = dispatchPass(
			"CameraSuite::LocalExposure", GetPhysicalCameraLocalExposureCS(), localSrvs, 2u, cameraLocalExposureTexture);
	}

	if (wantsStormglass) {
		// Reuse Skyrim's precipitation-occlusion depth map so the lens receives
		// new rain only when the camera is exposed to the weather. This is the
		// same roof/bridge/awning mask the engine builds for rain particles.
		ID3D11ShaderResourceView* precipitationOcclusionSRV = nullptr;
		if (auto* renderer = globals::game::renderer) {
			auto& precipitationOcclusion =
				renderer->GetDepthStencilData().depthStencils[
					RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
			precipitationOcclusionSRV = precipitationOcclusion.depthSRV;
		}

		// SkyBounce's 3D probe field is a much stronger shelter signal than the
		// single live precipitation projection: it accumulates hemispherical sky
		// visibility around the camera and therefore sees roofs, bridges and awnings.
		ID3D11ShaderResourceView* skyBounceProbeSRV = nullptr;
		auto& skyBounce = globals::pipeline::skyBounce;
		if (skyBounce.texProbeArray && skyBounce.texProbeArray->srv)
			skyBounceProbeSRV = skyBounce.texProbeArray->srv.get();

		ID3D11ShaderResourceView* stormglassSrvs[2] = {
			precipitationOcclusionSRV,
			skyBounceProbeSRV
		};
		stormglassPassReady = dispatchPass(
			"CameraSuite::StormglassField", GetStormglassFieldCS(), stormglassSrvs, 2u, stormglassFieldTexture);
	}

	if (wantsBloom && bloomHalfTexture && bloomQuarterTexture && bloomEighthTexture &&
		bloomSixteenthTexture && bloomEighthScratchTexture && bloomQuarterScratchTexture &&
		bloomHalfScratchTexture) {
		ID3D11ShaderResourceView* prefilterSrvs[2] = { sceneSRV, cameraExposureTexture->srv.get() };
		bool valid = dispatchPass("CameraSuite::BloomPrefilter", GetBloomPrefilterCS(), prefilterSrvs, 2u, bloomHalfTexture);

		ID3D11ShaderResourceView* halfSrv[1] = { bloomHalfTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomDownsampleHalf", GetBloomDownsampleCS(), halfSrv, 1u, bloomQuarterTexture);
		ID3D11ShaderResourceView* quarterSrv[1] = { bloomQuarterTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomDownsampleQuarter", GetBloomDownsampleCS(), quarterSrv, 1u, bloomEighthTexture);
		ID3D11ShaderResourceView* eighthSrv[1] = { bloomEighthTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomDownsampleEighth", GetBloomDownsampleCS(), eighthSrv, 1u, bloomSixteenthTexture);

		ID3D11ShaderResourceView* upsampleEighthSrvs[2] = { bloomSixteenthTexture->srv.get(), bloomEighthTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomUpsampleEighth", GetBloomUpsampleCS(), upsampleEighthSrvs, 2u, bloomEighthScratchTexture);
		ID3D11ShaderResourceView* upsampleQuarterSrvs[2] = { bloomEighthScratchTexture->srv.get(), bloomQuarterTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomUpsampleQuarter", GetBloomUpsampleCS(), upsampleQuarterSrvs, 2u, bloomQuarterScratchTexture);
		ID3D11ShaderResourceView* upsampleHalfSrvs[2] = { bloomQuarterScratchTexture->srv.get(), bloomHalfTexture->srv.get() };
		valid = valid && dispatchPass("CameraSuite::BloomUpsampleHalf", GetBloomUpsampleCS(), upsampleHalfSrvs, 2u, bloomHalfScratchTexture);
		bloomPassReady = valid;
	}

	ID3D11SamplerState* nullSampler = nullptr;
	ID3D11Buffer* nullCB = nullptr;
	context->CSSetSamplers(0, 1, &nullSampler);
	context->CSSetConstantBuffers(0, 1, &nullCB);
}

ID3D11ComputeShader* CameraSuite::GetUIBrightnessCS()
{
	if (!uiBrightnessCS) {
		std::vector<std::pair<const char*, const char*>> defines;
		uiBrightnessCS = static_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\CameraSuite\\UIBrightnessCS.hlsl", defines, "cs_5_0"));
		if (!uiBrightnessCS) {
			logger::error("HDR: Failed to compile UIBrightnessCS.hlsl");
		}
	}
	return uiBrightnessCS;
}

void CameraSuite::ScaleUIBrightnessForFG()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "UI Brightness Scale");

	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	// FG merges PQ UI from this pass; paused UI stays gamma for HDROutput.
	if (!IsFGCompositingThisFrame())
		return;

	if (!settings.enableHDR)
		return;

	if (!hdrDataCB || !imageReconstruction.dx12SwapChain.uiBufferWrapped || !imageReconstruction.dx12SwapChain.uiBufferWrapped->uav)
		return;

	auto context = globals::d3d::context;
	auto state = globals::state;

	state->BeginPerfEvent("UI Brightness Scale");

	UpdateHDRData();

	auto dispatchCount = Util::GetScreenDispatchCount(false);

	ID3D11UnorderedAccessView* uavs[1] = { imageReconstruction.dx12SwapChain.uiBufferWrapped->uav };
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	ID3D11Buffer* cbs[1] = { hdrDataCB->CB() };
	context->CSSetConstantBuffers(0, 1, cbs);

	auto computeShader = GetUIBrightnessCS();
	if (computeShader) {
		context->CSSetShader(computeShader, nullptr, 0);
		globals::profiler->BeginPass("CameraSuite::UIBrightness");
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		globals::profiler->EndPass();
	}

	uavs[0] = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	cbs[0] = nullptr;
	context->CSSetConstantBuffers(0, 1, cbs);
	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}

float CameraSuite::GetDisplayMaxLuminance() const
{
	float maxLuminance = 1000.0f;

	winrt::com_ptr<IDXGIOutput> output;
	if (globals::d3d::swapChain && SUCCEEDED(globals::d3d::swapChain->GetContainingOutput(output.put()))) {
		winrt::com_ptr<IDXGIOutput6> output6;
		if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(output6.put())))) {
			DXGI_OUTPUT_DESC1 desc1;
			if (SUCCEEDED(output6->GetDesc1(&desc1))) {
				maxLuminance = desc1.MaxLuminance;
				if (maxLuminance < 80.0f) {
					maxLuminance = 1000.0f;  // Fallback if display reports invalid value
				}
			}
		}
	}
	return maxLuminance;
}

float4 CameraSuite::GetSharedDataHDR() const
{
	if (!loaded)
		return { 0.0f, 0.0f, 0.0f, 0.0f };

	auto* state = globals::state;
	auto* ui = globals::game::ui;
	const bool isMainOrLoading = state->IsDisplayReferredModelMenuOpen(ui);
	const bool inMenuOrPause =
		ui && (ui->GameIsPaused() || isMainOrLoading || state->isMapMenuOpen);

	float menuSceneEncoding = kHdrMenuSceneGameplay;
	if (isMainOrLoading) {
		menuSceneEncoding = kHdrMenuSceneMainOrLoading;
	} else if (inMenuOrPause) {
		menuSceneEncoding = kHdrMenuScenePauseOrMap;
	}

	return {
		settings.enableHDR ? 1.0f : 0.0f,
		static_cast<float>(settings.hdrPaperWhite),
		static_cast<float>(settings.hdrPeakNits),
		menuSceneEncoding
	};
}

CameraSuite::PostProcessSettings CameraSuite::GetPostProcessData() const
{
	// Shared pipeline data is refreshed before Skyrim consumes the image-space
	// constants. Push the user DOF request here as well as at Present so the
	// native pass is scheduled instead of receiving its strength one frame late.
	ApplyPlayerPostProcessing();

	return {
		0u,
		std::clamp(settings.dofBokehRadius, 0.5f, 2.0f),
		std::clamp(settings.dofHighlightResponse, 0.0f, 1.0f),
		std::clamp(settings.dofFocusEdgeProtection, 0.0f, 2.0f),
		std::clamp(settings.dofForegroundCoverage, 0.0f, 1.5f),
		std::clamp(settings.dofCatEye, 0.0f, 1.0f),
		std::clamp(settings.dofAnamorphicRatio, 0.5f, 2.0f),
		std::clamp(cameraQuality, 0u, 3u)
	};
}

CameraSuite::HDRDataCB CameraSuite::BuildHDRData() const
{
	auto* ui = globals::game::ui;
	bool isMainOrLoadingMenu = globals::state->IsDisplayReferredModelMenuOpen(ui);
	// FidelityFX composites UI onto its real/generated frames itself. DLSS-G
	// expects the real backbuffer to already contain HUD pixels and uses the
	// separate UI guide for interpolation, so it must not skip this composite.
	bool skipUIComposite = IsFGCompositingThisFrame() &&
		globals::pipeline::imageReconstruction.dx12SwapChain.presenter == DX12SwapChain::Presenter::kFidelityFX;
	const float frameDelta = std::clamp(static_cast<float>(RE::GetSecondsSinceLastFrame()), 1.0f / 240.0f, 0.1f);
	const std::uint32_t currentFrame = globals::state ? globals::state->frameCount : 0u;

	// Resolve Stormglass precipitation directly from Skyrim's live Sky singleton.
	// This is intentionally independent of RainResponse and is evaluated on every
	// BuildHDRData call so a stale/cached material-weather path cannot suppress the
	// camera lens effect. RainResponse is fused in below as a secondary signal.
	const auto resolveDirectSkyRain = []() -> float {
		auto* sky = RE::Sky::GetSingleton();
		if (!sky)
			sky = globals::game::sky;
		if (!sky || sky->mode.get() != RE::Sky::Mode::kFull)
			return 0.0f;

		const float weatherPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		const bool currentRainy = sky->currentWeather &&
			sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
		const bool lastRainy = sky->lastWeather &&
			sky->lastWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
		const bool engineRain = sky->IsRaining();
		const bool engineSnow = sky->IsSnowing();
		const bool precipitationPresent = sky->precip &&
			(static_cast<bool>(sky->precip->currentPrecip) || static_cast<bool>(sky->precip->lastPrecip));

		// Transition presence from authored weather flags.
		float presence = 0.0f;
		if (currentRainy)
			presence = std::max(presence, weatherPct);
		if (lastRainy)
			presence = std::max(presence, 1.0f - weatherPct);

		// Engine state and an actually-live non-snow precipitation system are
		// authoritative fallbacks for modded weather with unusual metadata.
		if (engineRain)
			presence = std::max(presence, 0.85f);
		if (precipitationPresent && !engineSnow)
			presence = std::max(presence, 0.70f);

		if (presence <= 0.001f)
			return 0.0f;

		// If Skyrim is visibly precipitating, do not allow the optical signal to
		// collapse into an unusably tiny value. Heavy/settled rain still reaches 1.
		return std::clamp(0.50f + 0.50f * presence, 0.0f, 1.0f);
	};
	const float directSkyRain = resolveDirectSkyRain();
	const auto resolveDirectSkySnow = []() -> float {
		auto* sky = RE::Sky::GetSingleton();
		if (!sky)
			sky = globals::game::sky;
		if (!sky || sky->mode.get() != RE::Sky::Mode::kFull || !sky->IsSnowing())
			return 0.0f;

		auto weatherDensity = [](RE::TESWeather* weather) -> float {
			if (!weather || !weather->precipitationData)
				return 0.0f;
			const float density = weather->precipitationData->GetSettingValue(
				RE::BGSShaderParticleGeometryData::DataID::kParticleDensity).f;
			return std::clamp(density / 3.0f, 0.0f, 1.0f);
		};

		const float weatherPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		float intensity = std::lerp(
			weatherDensity(sky->lastWeather),
			weatherDensity(sky->currentWeather),
			weatherPct);
		return std::pow(std::clamp(std::max(intensity, 0.38f), 0.0f, 1.0f), 0.78f);
	};
	const float directSkySnow = resolveDirectSkySnow();

	// Update weather/water optical state once per rendered frame. The same frame
	// can request a gameplay composite, an FG UI composite and a clean capture;
	// advancing the state for each would otherwise make drops run too quickly.
	if (!environmentStateValid || environmentStateFrame != currentFrame) {
		const bool hadState = environmentStateValid;
		const bool paused = ui && ui->GameIsPaused();
		const float simulationDelta = paused ? 0.0f : frameDelta;
		environmentTime = std::fmod(environmentTime + simulationDelta, 4096.0f);

		float altitudeSignal = 0.0f;
		if (settings.enableColdLens) {
			const auto* sky = RE::Sky::GetSingleton();
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (sky && sky->mode.get() == RE::Sky::Mode::kFull && player) {
				const float altitude = player->GetPosition().z;
				const float altitudeStart =
					std::clamp(settings.coldAltitudeStart, 0.0f, 70000.0f);
				const float altitudeFull =
					std::clamp(settings.coldAltitudeFull, altitudeStart + 1000.0f, 90000.0f);
				const float t = std::clamp(
					(altitude - altitudeStart) / std::max(altitudeFull - altitudeStart, 1.0f),
					0.0f,
					1.0f);
				altitudeSignal = t * t * (3.0f - 2.0f * t);
			}
		}
		const float coldTarget = settings.enableColdLens
			? std::clamp(std::max(directSkySnow * 0.78f, altitudeSignal * 0.34f), 0.0f, 1.0f)
			: 0.0f;
		const float coldRate = coldTarget > coldLensState ? 0.85f : 0.22f;
		const float coldResponse = 1.0f - std::exp(-simulationDelta * coldRate);
		coldLensState = std::lerp(coldLensState, coldTarget, coldResponse);

		// Confirmed elemental-hit pulses decay independently from the persistent
		// environmental frost layer. Fire clears quickly; ice clings longer.
		fireLensState = std::max(0.0f, fireLensState - simulationDelta * 0.62f);
		frostImpactLensState = std::max(0.0f, frostImpactLensState - simulationDelta * 0.24f);


		// Driveclub-style lateral water inertia from real camera angular velocity,
		// not mouse input. This therefore works with mouse, controller, scripted
		// camera motion and free camera. The state is filtered to give droplets mass:
		// fast attack when a turn starts, slower release when the camera settles.
		float lateralTarget = 0.0f;
		if (auto* playerCamera = RE::PlayerCamera::GetSingleton();
			playerCamera && playerCamera->cameraRoot) {
			// Do NOT gate camera inertia on globals::state->inWorld. The Stormglass
			// diagnostic proved that flag can be false/stale during normal exterior
			// gameplay, which previously prevented this entire block from running.
			// The lens presentation itself is already correctly gated by Sky::kFull
			// and rain/wetness, so it is safe to track camera angular velocity whenever
			// the rendered camera root exists.
			// CommonLibSSE prebuilt v4.26.1 used by PIXL does not expose
			// PlayerCamera::yaw. Derive yaw from the camera root's world rotation
			// instead. For NiMatrix3's XYZ convention, yaw (Z rotation) is:
			// atan2(m01, m00). This tracks the actual rendered camera rather than
			// raw mouse input and therefore works with controller/scripted cameras too.
			const auto& cameraRotation = playerCamera->cameraRoot->world.rotate;
			const float currentYaw = std::atan2(
				cameraRotation.entry[0][1],
				cameraRotation.entry[0][0]);

			if (stormglassCameraYawValid && !paused && simulationDelta > 1e-5f) {
				constexpr float kTwoPi = 6.2831853071795864769f;
				float yawDelta = std::remainder(currentYaw - stormglassPreviousCameraYaw, kTwoPi);
				float yawVelocity = yawDelta / simulationDelta;

				// Ignore tiny transform/TAA/controller noise, then normalize useful
				// turn speeds into the signed lateral inertia range.
				if (std::abs(yawVelocity) < 0.025f)
					yawVelocity = 0.0f;
				lateralTarget = std::clamp(-yawVelocity * 0.22f, -1.0f, 1.0f);
			}

			stormglassPreviousCameraYaw = currentYaw;
			stormglassCameraYawValid = true;
		} else {
			stormglassCameraYawValid = false;
		}

		const float inertiaDelta = paused ? frameDelta : simulationDelta;
		const bool acceleratingLaterally = std::abs(lateralTarget) > std::abs(stormglassLateralInertiaState);
		const float inertiaRate = acceleratingLaterally ? 12.0f : 4.0f;
		const float inertiaResponse = 1.0f - std::exp(-inertiaDelta * inertiaRate);
		stormglassLateralInertiaState = std::lerp(stormglassLateralInertiaState, lateralTarget, inertiaResponse);
		if (std::abs(stormglassLateralInertiaState) < 1e-4f)
			stormglassLateralInertiaState = 0.0f;

		// Stormglass owns its weather response and must not depend on the Rain
		// Response material feature being enabled/loaded. GetLiveRainIntensity()
		// reads Skyrim's live weather state directly and includes the debug override.
		auto& rainResponse = globals::pipeline::rainResponse;
		const float rainResponseSignal = std::clamp(rainResponse.GetLiveRainIntensity(), 0.0f, 1.0f);
		float targetRain = std::max(directSkyRain, rainResponseSignal);
		float authoredWetness = 0.0f;
		if (rainResponse.loaded && rainResponse.settings.EnableRainResponse) {
			const auto rainData = rainResponse.GetCommonBufferData();
			authoredWetness = std::clamp(rainData.Wetness, 0.0f, 1.0f);
		}

		const float rainAdaptResponse = 1.0f - std::exp(-simulationDelta * (targetRain > stormglassRainIntensityState ? 7.0f : 2.2f));
		stormglassRainIntensityState = std::lerp(stormglassRainIntensityState, targetRain, rainAdaptResponse);
		const float wetnessTarget = std::max(targetRain * 0.86f, authoredWetness * 0.62f);
		if (wetnessTarget > stormglassWetnessState) {
			const float wettingResponse = 1.0f - std::exp(-simulationDelta * 4.5f);
			stormglassWetnessState = std::lerp(stormglassWetnessState, wetnessTarget, wettingResponse);
		} else {
			stormglassWetnessState = std::max(wetnessTarget,
				stormglassWetnessState - simulationDelta * std::clamp(settings.stormglassDryingRate, 0.01f, 0.50f));
		}

		bool playerUnderwater = false;
		if (const auto* waterSystem = RE::TESWaterSystem::GetSingleton())
			playerUnderwater = waterSystem->playerUnderwater;
		if (hadState && wasPlayerUnderwater && !playerUnderwater) {
			surfaceBreakFilmState = 1.0f;
			stormglassWetnessState = std::max(stormglassWetnessState, 0.95f);
		}
		if (playerUnderwater)
			surfaceBreakFilmState = 0.0f;
		else
			surfaceBreakFilmState = std::max(0.0f, surfaceBreakFilmState - simulationDelta * 0.34f);

		const float submergedTarget = playerUnderwater ? 1.0f : 0.0f;
		const float transitionSpeed = std::clamp(settings.submergedTransitionSpeed, 0.5f, 8.0f) *
			(submergedTarget > submergedBlendState ? 1.35f : 0.78f);
		const float submergedResponse = 1.0f - std::exp(-simulationDelta * transitionSpeed);
		submergedBlendState = std::lerp(submergedBlendState, submergedTarget, submergedResponse);
		wasPlayerUnderwater = playerUnderwater;
		environmentStateFrame = currentFrame;
		environmentStateValid = true;
	}

	// Linear Light Core keeps the pipeline linear throughout.
	// Without it, ISHDR gamma-encodes its output even in HDR mode.
	bool isSceneLinear = globals::pipeline::linearLightCore.settings.enableLinearLightCore;

	// Use user-specified peak brightness for highlights compression
	float effectivePeakNits = static_cast<float>(settings.hdrPeakNits);

	HDRDataCB data{};
	data.enableHDR = settings.enableHDR ? 1.f : 0.f;
	data.paperWhite = static_cast<float>(settings.hdrPaperWhite);
	data.peakNits = effectivePeakNits;
	data.skipUIComposite = skipUIComposite ? 1.f : 0.f;
	data.uiBrightness = settings.hdrUIBrightness;
	data.isSceneLinear = isSceneLinear ? 1.f : 0.f;
	data.pad0 = isMainOrLoadingMenu ? 1.f : 0.f;
	data.menuSceneBrightness = std::clamp(settings.menuSceneBrightness, 0.75f, 3.0f);
	// TweenMenu = pause UI. ScaleUIBrightnessForFG skips while GameIsPaused(), so HDROutputCS applies the same mid-alpha boost when compositing gamma UI.
	data.fgTweenMenuMidAlphaBoost = (ui && ui->IsMenuOpen(RE::TweenMenu::MENU_NAME)) ? 1.f : 0.f;
	data.previewSDR = 0.f;
	data.applyAutoHDR = 0.f;
	data.stormglassLateralInertia = std::clamp(stormglassLateralInertiaState, -1.0f, 1.0f);

	data.physicalCameraEnabled = settings.enablePhysicalCamera ? 1.f : 0.f;
	data.cameraAutoExposure = settings.cameraAutoExposure ? 1.f : 0.f;
	data.cameraExposureCompensationEV = std::clamp(settings.cameraExposureCompensationEV, -4.0f, 4.0f);
	data.cameraMinExposureEV = std::clamp(settings.cameraMinExposureEV, -10.0f, 0.0f);
	data.cameraMaxExposureEV = std::clamp(settings.cameraMaxExposureEV, 0.0f, 10.0f);
	data.cameraLowPercentile = std::clamp(settings.cameraLowPercentile, 0.0f, 0.20f);
	data.cameraHighPercentile = std::clamp(settings.cameraHighPercentile, std::max(0.80f, data.cameraLowPercentile + 0.05f), 1.0f);
	data.cameraHighlightProtection = std::clamp(settings.cameraHighlightProtection, 0.0f, 1.0f);
	data.cameraShadowDetail = std::clamp(settings.cameraShadowDetail, 0.0f, 0.5f);
	data.cameraContrast = std::clamp(settings.cameraContrast, 0.75f, 1.30f);
	data.cameraLocalExposure = std::clamp(settings.cameraLocalExposure, 0.0f, 0.5f);
	data.cameraAdaptBrightToDark = std::clamp(settings.cameraAdaptBrightToDark, 0.05f, 4.0f);
	data.cameraAdaptDarkToBright = std::clamp(settings.cameraAdaptDarkToBright, 0.03f, 2.0f);
	data.bodycamEnabled = settings.enablePhysicalCamera && settings.experimentalBodycam ? 1.f : 0.f;
	data.bodycamStrength = std::clamp(settings.bodycamStrength, 0.0f, 1.0f);
	data.bodycamDistortion = std::clamp(settings.bodycamDistortion, 0.0f, 0.30f);
	data.bodycamNoise = std::clamp(settings.bodycamNoise, 0.0f, 0.60f);
	data.bodycamVignette = std::clamp(settings.bodycamVignette, 0.0f, 0.30f);
	data.bodycamChromaticAberration = std::clamp(settings.bodycamChromaticAberration, 0.0f, 0.08f);
	data.bodycamSharpen = std::clamp(settings.bodycamSharpen, 0.0f, 0.35f);
	data.bodycamExposureAggressiveness = std::clamp(settings.bodycamExposureAggressiveness, 0.0f, 1.0f);
	data.bodycamHighlightBloom = std::clamp(settings.bodycamHighlightBloom, 0.0f, 0.50f);
	data.bodycamWhiteBalance = std::clamp(settings.bodycamWhiteBalance, 0.0f, 0.40f);
	data.cameraSaturation = std::clamp(settings.cameraSaturation, 0.70f, 1.25f);
	data.deltaTime = frameDelta;
	data.frameIndex = globals::state ? globals::state->frameCount : 0u;
	data.cameraToe = std::clamp(settings.cameraToe, 0.0f, 0.5f);
	data.cameraShoulder = std::clamp(settings.cameraShoulder, 0.2f, 1.5f);
	data.lookEnabled = settings.lookPreset > 0 && lookTextureView ? 1.0f : 0.0f;
	data.lookOpacity = std::clamp(settings.lookOpacity, 0.0f, 1.0f);
	data.cameraInfluence = std::clamp(settings.cameraInfluence, 0.0f, 1.0f);
	data.auxiliaryPassMask = (bloomPassReady ? 1.0f : 0.0f) +
		(localExposurePassReady ? 2.0f : 0.0f) +
		(stormglassPassReady ? 4.0f : 0.0f);
	data.bloomEnabled = settings.enableBloom ? 1.0f : 0.0f;
	data.bloomStrength = std::clamp(settings.bloomStrength, 0.0f, 3.0f);
	data.bloomThreshold = std::clamp(settings.bloomThreshold, 0.0f, 5.0f);
	data.bloomRadius = std::clamp(settings.bloomRadius, 0.0f, 5.0f);

	// Match the dispatch-side gate: use the actual exterior Sky mode rather than
	// globals::state->inWorld / isMapMenuOpen. Those generic state flags can be
	// false/stale during normal exterior gameplay, which previously zeroed
	// stormglassEnabled even while rain and every GPU resource were valid.
	const auto* presentationSky = RE::Sky::GetSingleton();
	const bool environmentPresentation =
		presentationSky &&
		presentationSky->mode.get() == RE::Sky::Mode::kFull &&
		!isMainOrLoadingMenu;
	data.stormglassEnabled = settings.enableStormglass && environmentPresentation ? 1.0f : 0.0f;
	// Fuse the immediate live Sky result into the outgoing CB as well as the
	// smoothed state. This guarantees HDROutput sees rain on the same frame even
	// if a state/cache path was sampled before Skyrim updated precipitation.
	data.stormglassRainIntensity = std::clamp(
		std::max(stormglassRainIntensityState, directSkyRain), 0.0f, 1.0f);
	data.stormglassWetness = std::clamp(
		std::max(stormglassWetnessState, directSkyRain * 0.80f), 0.0f, 1.0f);
	data.stormglassStrength = std::clamp(settings.stormglassStrength, 0.0f, 1.0f);
	data.stormglassDropScale = std::clamp(settings.stormglassDropScale, 0.50f, 2.0f);
	data.stormglassRefraction = std::clamp(settings.stormglassRefraction, 0.0f, 1.5f);
	data.stormglassTrails = std::clamp(settings.stormglassTrails, 0.0f, 1.0f);
	data.stormglassTime = environmentTime;
	data.surfaceBreakFilm = std::clamp(surfaceBreakFilmState, 0.0f, 1.0f);
	data.submergedOpticsEnabled = settings.enableSubmergedOptics && environmentPresentation ? 1.0f : 0.0f;
	data.submergedBlend = std::clamp(submergedBlendState, 0.0f, 1.0f);
	data.submergedStrength = std::clamp(settings.submergedStrength, 0.0f, 1.0f);
	data.submergedBlur = std::clamp(settings.submergedBlur, 0.0f, 1.0f);
	data.submergedRefraction = std::clamp(settings.submergedRefraction, 0.0f, 1.5f);
	data.submergedFogAmount = 0.35f;
	data.cameraQuality = static_cast<float>(std::clamp(cameraQuality, 0u, 3u));
	data.submergedWaterTint = { 0.10f, 0.22f, 0.24f, 1.0f };
	if (const auto* waterSystem = RE::TESWaterSystem::GetSingleton(); waterSystem && waterSystem->currentWaterType) {
		const auto& waterData = waterSystem->currentWaterType->data;
		constexpr float byteToUnit = 1.0f / 255.0f;
		data.submergedWaterTint = {
			waterData.deepWaterColor.red * byteToUnit,
			waterData.deepWaterColor.green * byteToUnit,
			waterData.deepWaterColor.blue * byteToUnit,
			1.0f
		};
		data.submergedFogAmount = std::clamp(waterData.underwaterFogAmount, 0.0f, 1.0f);
	}

	// PIXL's experimental realtime DOF is retired. Keep the legacy ABI payload
	// neutral so old shader caches and settings remain compatible while Skyrim's
	// native image-space pass owns the visible depth-of-field result.
	const bool dofGameplay = globals::state && !globals::state->isMapMenuOpen &&
		!(ui && ui->GameIsPaused()) && !isMainOrLoadingMenu;
	data.dofEnabled = 0.0f;
	data.dofStrength = std::clamp(settings.dofStrength, 0.0f, 1.0f);
	data.dofFocusDistance = std::clamp(settings.dofFocusDistance, 100.0f, 20000.0f);
	data.dofFocusRange = std::clamp(settings.dofFocusRange, 100.0f, 20000.0f);
	data.dofBokehRadius = std::clamp(settings.dofBokehRadius, 0.5f, 2.0f);
	data.dofHighlightResponse = std::clamp(settings.dofHighlightResponse, 0.0f, 1.0f);
	data.dofFocusEdgeProtection = std::clamp(settings.dofFocusEdgeProtection, 0.0f, 2.0f);
	data.dofForegroundCoverage = std::clamp(settings.dofForegroundCoverage, 0.0f, 1.5f);
	data.dofCatEye = std::clamp(settings.dofCatEye, 0.0f, 1.0f);
	data.dofAnamorphicRatio = std::clamp(settings.dofAnamorphicRatio, 0.5f, 2.0f);
	data.dofQuality = static_cast<float>(std::clamp(cameraQuality, 0u, 3u));
	data.dofAutoFocus = settings.dofAutoFocus ? 1.0f : 0.0f;
	const bool elementalGameplay = environmentPresentation &&
		!(ui && ui->GameIsPaused()) && !isMainOrLoadingMenu;
	data.coldLensAmount = elementalGameplay && settings.enableColdLens
		? std::clamp(std::max(coldLensState, frostImpactLensState), 0.0f, 1.0f)
		: 0.0f;
	data.fireLensAmount = elementalGameplay && settings.enableElementalDamageLens
		? std::clamp(fireLensState, 0.0f, 1.0f)
		: 0.0f;
	data.coldLensStrength = std::clamp(settings.coldLensStrength, 0.0f, 1.0f);
	data.elementalLensStrength = std::clamp(settings.elementalLensStrength, 0.0f, 1.0f);
	data.motionBlurEnabled = settings.enableModernMotionBlur && dofGameplay && !photoModeDofIsolation &&
		Util::GetCurrentSceneDepthSRV(true) ? 1.0f : 0.0f;
	data.motionBlurStrength = std::clamp(settings.motionBlurStrength, 0.0f, 1.0f);
	data.motionBlurShutter = std::clamp(settings.motionBlurShutter, 0.10f, 1.0f);
	data.motionBlurMaxPixels = std::clamp(settings.motionBlurMaxPixels, 4.0f, 48.0f);
	return data;
}

void CameraSuite::UpdateHDRData() const
{
	ApplyPlayerPostProcessing();
	if (!hdrDataCB)
		return;

	hdrDataCB->Update(BuildHDRData());
}

void CameraSuite::ApplyPlayerPostProcessing() const
{
	if (!loaded || !globals::game::imageSpaceManager)
		return;

	auto& hdr = globals::game::imageSpaceManager->GetRuntimeData().data.baseData.hdr;
	// PIXL Bloom owns its low-resolution pyramid when enabled. When disabled,
	// leave Skyrim's authored weather/image-space bloom untouched.
	if (settings.enableBloom)
		hdr.bloomScale = 0.0f;

	// Skyrim owns DOF again. Its Imagespace INI switch is live, so the PIXL toggle
	// can enable/disable the native pass without rewriting SkyrimPrefs.ini.
	const auto applyNativeDofSetting = [&](RE::INISettingCollection* collection) {
		if (!collection)
			return false;
		if (auto* setting = collection->GetSetting("bDoDepthOfField:Imagespace");
			setting && setting->GetType() == RE::Setting::Type::kBool) {
			setting->data.b = settings.enableSkyrimDepthOfField;
			return true;
		}
		return false;
	};
	if (!applyNativeDofSetting(globals::game::iniPrefSettingCollection))
		applyNativeDofSetting(globals::game::iniSettingCollection);
}

void CameraSuite::SetPhotoModeDofIsolation(bool enabled)
{
	if (photoModeDofIsolation == enabled)
		return;

	photoModeDofIsolation = enabled;
	UpdateHDRData();
}

void CameraSuite::TriggerElementalLens(float fireAmount, float frostAmount)
{
	if (!settings.enableElementalDamageLens)
		return;

	fireLensState = std::max(
		fireLensState,
		std::clamp(fireAmount, 0.0f, 1.0f));
	frostImpactLensState = std::max(
		frostImpactLensState,
		std::clamp(frostAmount, 0.0f, 1.0f));
}

void CameraSuite::UpdateSwapChainColorSpace() const
{
	auto& imageReconstruction = globals::pipeline::imageReconstruction;

	// For Frame Gen, update the D3D12 swap chain color space
	if (imageReconstruction.d3d12SwapChainActive) {
		imageReconstruction.dx12SwapChain.SetColorSpace(settings.enableHDR);
		// HDR metadata is not set - some monitors have issues with HDR10 static metadata.
		// DX12SwapChain handles color space only; metadata control is centralized here.
		if (imageReconstruction.dx12SwapChain.swapChain) {
			imageReconstruction.dx12SwapChain.swapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		}
		return;
	}

	IDXGISwapChain4* swapChain4 = nullptr;

	if (globals::d3d::swapChain) {
		globals::d3d::swapChain->QueryInterface(IID_PPV_ARGS(&swapChain4));
	}

	if (!swapChain4)
		return;

	if (settings.enableHDR) {
		HRESULT hr = swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		if (SUCCEEDED(hr)) {
			logger::info("[HDR] Set swap chain color space to HDR10 (PQ/BT.2020)");
			// Do NOT set HDR10 static metadata - it can cause issues on some monitors
			// The HGiG approach is to handle highlights compression in the shader instead.
			swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		} else {
			logger::warn("[HDR] Failed to set HDR10 color space");
		}
	} else {
		HRESULT hr = swapChain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		if (SUCCEEDED(hr)) {
			logger::info("[HDR] Set swap chain color space to SDR (sRGB)");
			swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
		} else {
			logger::warn("[HDR] Failed to set SDR color space");
		}
	}

	swapChain4->Release();
}
