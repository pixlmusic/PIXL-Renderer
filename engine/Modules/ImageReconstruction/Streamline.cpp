#include "Streamline.h"

#include <algorithm>
#include <cmath>
#include <dxgi.h>
#include <dxgi1_3.h>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../ImageReconstruction.h"
#include "DX12SwapChain.h"

namespace
{
	constexpr UINT NVIDIA_VENDOR_ID = 0x10DE;
}

void LoggingCallback(sl::LogType type, const char* msg)
{
	if (!msg) {
		logger::warn("[StreamlineSDK] Received an empty log message");
		return;
	}

	// Remove trailing newlines from the raw message
	std::string rawMsg(msg);
	while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
		rawMsg.pop_back();

	// Remove leading bracketed metadata
	const char* p = msg;
	while (*p == '[') {
		const char* close = strchr(p, ']');
		if (!close)
			break;
		p = close + 1;
		// Skip whitespace after each bracketed section
		while (*p == ' ' || *p == '\t') ++p;
	}
	// Now p points to the first non-bracketed section (file/line info or message)
	std::string cleanMsg(p);
	// Trim leading/trailing whitespace and newlines
	size_t start = cleanMsg.find_first_not_of(" \t\r\n");
	size_t end = cleanMsg.find_last_not_of(" \t\r\n");
	if (start != std::string::npos && end != std::string::npos)
		cleanMsg = cleanMsg.substr(start, end - start + 1);
	else
		cleanMsg.clear();

	// If the cleaned message is empty or only bracketed tokens, log the raw message
	bool onlyBrackets = true;
	for (char c : cleanMsg) {
		if (c != '[' && c != ']' && c != ' ' && c != '\t') {
			onlyBrackets = false;
			break;
		}
	}
	if (cleanMsg.empty() || onlyBrackets) {
		logger::debug("[StreamlineSDK:RAW] {}", rawMsg);
		return;
	}

	// Use a clear prefix
	const char* prefix = "[StreamlineSDK]";
	switch (type) {
	case sl::LogType::eInfo:
		logger::debug("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eError:
		logger::error("{} {}", prefix, cleanMsg);
		break;
	}
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

void Streamline::LoadInterposer()
{
	triedInitialization = true;

	std::wstring interposerPath = std::wstring(Streamline::PluginDir) + L"\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::warn("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::debug("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	Streamline::dllVersions = Util::EnumerateDllVersions(pluginDir);
	for (const auto& [name, versionStr] : Streamline::dllVersions)
		logger::info("[Streamline] {} version: {}", name, versionStr);

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };

	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	// Set log level from settings
	switch (globals::pipeline::imageReconstruction.settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	case 0:
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;
	std::error_code pluginPathError;
	auto pluginDirAbsolute = std::filesystem::absolute(std::filesystem::path(Streamline::PluginDir), pluginPathError);
	if (pluginPathError)
		pluginDirAbsolute = std::filesystem::path(Streamline::PluginDir);
	static std::wstring pluginDirAbsoluteW;
	pluginDirAbsoluteW = pluginDirAbsolute.wstring();
	static const wchar_t* pluginPaths[1]{};
	pluginPaths[0] = pluginDirAbsoluteW.c_str();
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;
	logger::info("[Streamline] Plugin search path: {}", pluginDirAbsolute.string());

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTagForFrame = (PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	std::string missingExports;
	const auto requireExport = [&](auto function, std::string_view name) {
		if (function)
			return;
		if (!missingExports.empty())
			missingExports += ", ";
		missingExports += name;
	};
	requireExport(slInit, "slInit");
	requireExport(slShutdown, "slShutdown");
	requireExport(slIsFeatureSupported, "slIsFeatureSupported");
	requireExport(slIsFeatureLoaded, "slIsFeatureLoaded");
	requireExport(slSetFeatureLoaded, "slSetFeatureLoaded");
	requireExport(slEvaluateFeature, "slEvaluateFeature");
	requireExport(slAllocateResources, "slAllocateResources");
	requireExport(slFreeResources, "slFreeResources");
	requireExport(slSetTagForFrame, "slSetTagForFrame");
	requireExport(slGetFeatureRequirements, "slGetFeatureRequirements");
	requireExport(slGetFeatureVersion, "slGetFeatureVersion");
	requireExport(slUpgradeInterface, "slUpgradeInterface");
	requireExport(slSetConstants, "slSetConstants");
	requireExport(slGetNativeInterface, "slGetNativeInterface");
	requireExport(slGetFeatureFunction, "slGetFeatureFunction");
	requireExport(slGetNewFrameToken, "slGetNewFrameToken");
	requireExport(slSetD3DDevice, "slSetD3DDevice");
	if (!missingExports.empty()) {
		logger::error("[Streamline] Interposer is missing required exports: {}", missingExports);
		FreeLibrary(interposer);
		interposer = nullptr;
		return;
	}

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		featureDLSS = false;
		featureReflex = false;
		featurePCL = false;
		reflexSupportedOnCurrentAdapter = false;
		reflexOptionsCache = {};
		lastReflexSleepFrame = UINT32_MAX;
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	logger::info("[Streamline] Checking features");
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);
	reflexSupportedOnCurrentAdapter = adapterDesc.VendorId == NVIDIA_VENDOR_ID;

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	auto checkFeatureAvailability = [&](sl::Feature feature, const char* featureName, bool& outAvailable) {
		outAvailable = false;
		bool loaded = false;
		if (SL_FAILED(result, slIsFeatureLoaded(feature, loaded))) {
			logger::warn("[Streamline] {} load-state query failed: {}", featureName, magic_enum::enum_name(result));
			return;
		}
		if (!loaded) {
			logger::info("[Streamline] {} feature is not loaded", featureName);
			sl::FeatureRequirements featureRequirements;
			sl::Result requirementsResult = slGetFeatureRequirements(feature, featureRequirements);
			if (requirementsResult != sl::Result::eOk) {
				logger::info("[Streamline] {} feature failed to load due to: {}", featureName, magic_enum::enum_name(requirementsResult));
			}
			return;
		}

		logger::info("[Streamline] {} feature is loaded", featureName);
		outAvailable = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;
	};

	checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);
	if (reflexSupportedOnCurrentAdapter) {
		checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
		checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);
	} else {
		featureReflex = false;
		featurePCL = false;
	}

	if (featureDLSS) {
		isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);

		if (isRTXBelow40series)
			logger::info("[Streamline] Older RTX GPU detected; PIXL runtime policy will follow the user's compatibility/latest-model preference");
		else
			logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	if (reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Reflex {} available", featureReflex ? "is" : "is not");
		logger::info("[Streamline] PCL {} available", featurePCL ? "is" : "is not");
	} else {
		logger::info("[Streamline] Reflex/PCL disabled on non-NVIDIA adapter");
	}
	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction
	const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
		fn = nullptr;
		const sl::Result bindResult = slGetFeatureFunction(feature, functionName, fn);
		if (bindResult != sl::Result::eOk)
			logger::warn("[Streamline] {} bind failed with {}", functionName, magic_enum::enum_name(bindResult));
		return bindResult == sl::Result::eOk && fn != nullptr;
	};

	if (featureDLSS) {
		bool dlssFunctionsBound = true;
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		dlssFunctionsBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
		featureDLSS = dlssFunctionsBound;
		if (!featureDLSS)
			logger::error("[Streamline] DLSS exports are incomplete; DLSS has been disabled safely");
	}

	slReflexGetState = nullptr;
	slReflexSleep = nullptr;
	slReflexSetOptions = nullptr;
	slPCLSetMarker = nullptr;
	featureReflex = false;
	featurePCL = false;

	if (slGetFeatureFunction && reflexSupportedOnCurrentAdapter) {
		if (slSetFeatureLoaded) {
			// Reflex/PCL availability can change after device bind; request explicit load here.
			const auto requestFeatureLoad = [&](sl::Feature feature, const char* featureName) {
				const sl::Result loadResult = slSetFeatureLoaded(feature, true);
				if (loadResult != sl::Result::eOk)
					logger::warn("[Streamline] Failed to request {} load: {}", featureName, magic_enum::enum_name(loadResult));
			};

			requestFeatureLoad(sl::kFeatureReflex, "Reflex");
			requestFeatureLoad(sl::kFeaturePCL, "PCL");
		}

		// Keep runtime controls strict: only advertise Reflex/PCL as available when required entry points bind.
		bool reflexFnsBound = true;
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		reflexFnsBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		featureReflex = reflexFnsBound && slReflexSetOptions && slReflexSleep;

		if (!featureReflex) {
			logger::warn("[Streamline] Reflex functions are missing; Reflex runtime controls will be disabled");
		} else {
			logger::info("[Streamline] Reflex runtime controls are available");
		}

		bool pclFnBound = bindFeatureFn(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		featurePCL = pclFnBound && slPCLSetMarker;
		if (!featurePCL) {
			logger::warn("[Streamline] PCL marker function is unavailable; marker optimization requests will be ignored");
		} else {
			logger::info("[Streamline] PCL marker interface is available");
		}
	} else if (!reflexSupportedOnCurrentAdapter) {
		logger::info("[Streamline] Skipping Reflex/PCL binding on non-NVIDIA adapter");
	}

	reflexOptionsCache = {};
	lastReflexSleepFrame = UINT32_MAX;
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for imageReconstruction and frame generation features.
 */
bool Streamline::EnsureFrameToken()
{
	if (!initialized || !slGetNewFrameToken || !globals::state)
		return false;

	if (!frameChecker.IsNewFrame())
		return frameToken != nullptr;

	if (SL_FAILED(result, slGetNewFrameToken(frameToken, &globals::state->frameCount))) {
		logger::error("[Streamline] Could not get frame token: {}", magic_enum::enum_name(result));
		frameToken = nullptr;
		return false;
	}

	return frameToken != nullptr;
}

bool Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport, bool resetHistory)
{
	if (!globals::pipeline::imageReconstruction.streamline.initialized)
		return false;

	if (!EnsureFrameToken())
		return false;

	sl::Constants slConstants = {};

	slConstants.cameraAspectRatio = (float)globals::game::graphicsState->screenWidth / (float)globals::game::graphicsState->screenHeight;

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust();
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	recalculateCameraMatrices(slConstants);

	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	auto jitter = imageReconstruction.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = resetHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	slConstants.mvecScale = { 1.0f, 1.0f };
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		logger::error("[Streamline] Could not set constants");
		return false;
	}

	return true;
}

bool Streamline::IsRTXAndBelow40Series(IDXGIAdapter* a_adapter)
{
	DXGI_ADAPTER_DESC adapterDesc = {};

	a_adapter->GetDesc(&adapterDesc);

	UINT vendorId = adapterDesc.VendorId;
	UINT deviceId = adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != 0x10DE)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x2600)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

void Streamline::SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width)
{
	sl::DLSSOptions dlssOptions{};

	// Map quality mode to DLSS mode
	uint32_t qualityMode = globals::pipeline::imageReconstruction.settings.qualityMode;
	switch (qualityMode) {
	case 1:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = (uint)globals::game::graphicsState->screenHeight;

	// Detect HDR from kMAIN format at runtime
	{
		auto renderer = globals::game::renderer;
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC mainDesc;
		static_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&mainDesc);
		bool isHDR = mainDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM;
		dlssOptions.colorBuffersHDR = isHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	}
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	std::optional<sl::DLSSPreset> customPreset;
	switch (globals::pipeline::imageReconstruction.settings.presetDLSS) {
	case 1:
		customPreset = sl::DLSSPreset::ePresetJ;
		break;
	case 2:
		customPreset = sl::DLSSPreset::ePresetK;
		break;
	case 3:
		customPreset = sl::DLSSPreset::ePresetL;
		break;
	case 4:
		customPreset = sl::DLSSPreset::ePresetM;
		break;
	case 5:
		customPreset = sl::DLSSPreset::ePresetF;
		break;
	}

	if (customPreset.has_value()) {
		dlssOptions.dlaaPreset = customPreset.value();
		dlssOptions.ultraQualityPreset = customPreset.value();
		dlssOptions.qualityPreset = customPreset.value();
		dlssOptions.balancedPreset = customPreset.value();
		dlssOptions.performancePreset = customPreset.value();
		dlssOptions.ultraPerformancePreset = customPreset.value();
	} else if (isRTXBelow40series && !globals::pipeline::imageReconstruction.settings.forceLatestDLSSModelOnLegacyRTX) {
		dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.qualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.balancedPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.performancePreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
	} else {
		dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.qualityPreset = sl::DLSSPreset::ePresetM;
		dlssOptions.balancedPreset = sl::DLSSPreset::ePresetM;
		dlssOptions.performancePreset = sl::DLSSPreset::ePresetM;
		dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
	}

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSS");
	}
}

void Streamline::EvaluateDLSS(sl::ViewportHandle vp,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth,
	bool resetHistory)
{
	if (!initialized || !featureDLSS || !slSetTagForFrame || !slEvaluateFeature || !slDLSSSetOptions) {
		logger::error("[Streamline] DLSS evaluation skipped because its runtime interface is incomplete");
		return;
	}

	auto context = globals::d3d::context;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	if (!CheckFrameConstants(vp, resetHistory))
		return;

	const bool emitPCLMarkers =
		globals::pipeline::imageReconstruction.settings.reflexUseMarkersToOptimize &&
		reflexOptionsCache.useMarkersToOptimize &&
		featurePCL;
	const auto emitPCLMarker = [&](sl::PCLMarker marker, const char* stageName, uint32_t stageIndex) {
		if (!emitPCLMarkers || !slPCLSetMarker || !frameToken)
			return;
		const sl::Result markerResult = slPCLSetMarker(marker, *frameToken);
		if (markerResult != sl::Result::eOk) {
			static bool markerErrorLogged[2] = { false, false };
			const uint32_t boundedStageIndex = std::min(stageIndex, 1u);
			if (markerErrorLogged[boundedStageIndex])
				return;
			markerErrorLogged[boundedStageIndex] = true;
			logger::warn(
				"[Streamline] slPCLSetMarker({}) failed: {}",
				stageName,
				magic_enum::enum_name(markerResult));
		}
	};

	SetDLSSOptions(vp, outputWidth);

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }
	};

	const sl::Result tagResult = slSetTagForFrame(*frameToken, vp, tags, _countof(tags), context);
	if (tagResult != sl::Result::eOk) {
		static bool tagErrorLogged = false;
		if (!tagErrorLogged) {
			tagErrorLogged = true;
			logger::error("[Streamline] slSetTagForFrame failed result={}", magic_enum::enum_name(tagResult));
		}
	}

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = { &view };

	auto state = globals::state;
	if (state->frameAnnotations)
		state->BeginPerfEvent("DLSS Evaluate");

	emitPCLMarker(sl::PCLMarker::eRenderSubmitStart, "DLSS-EvaluateStart", 0);
	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
	emitPCLMarker(sl::PCLMarker::eRenderSubmitEnd, "DLSS-EvaluateEnd", 1);

	if (state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		static bool evalErrorLogged = false;
		if (!evalErrorLogged) {
			evalErrorLogged = true;
			logger::error("[Streamline] slEvaluateFeature failed result={}", (int)evalResult);
		}
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, bool resetHistory)
{
	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	// DLSS input and output must not alias. Always write to the intermediate texture,
	// then either sharpen or copy the result back to kMAIN.
	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	ID3D11Resource* colorOut =
		imageReconstruction.sharpenerTexture ? imageReconstruction.sharpenerTexture->resource.get() : a_upscalingTexture;

	sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
	sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

	EvaluateDLSS(viewport,
		a_upscalingTexture, colorOut,
		depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
		extentIn, extentOut, (uint)screenSize.x, resetHistory);
}

void Streamline::UpdateReflex()
{
	if (!initialized || !reflexSupportedOnCurrentAdapter || !featureReflex || !slReflexSetOptions)
		return;

	const auto applyReflexOptionsIfChanged = [&](const sl::ReflexOptions& options, const char* onFailMessage) {
		if (reflexOptionsCache.valid &&
			reflexOptionsCache.mode == options.mode &&
			reflexOptionsCache.frameLimitUs == options.frameLimitUs &&
			reflexOptionsCache.useMarkersToOptimize == options.useMarkersToOptimize) {
			return;
		}

		if (SL_FAILED(result, slReflexSetOptions(options))) {
			logger::error("[Streamline] {}: {}", onFailMessage, magic_enum::enum_name(result));
			return;
		}

		reflexOptionsCache.valid = true;
		reflexOptionsCache.mode = options.mode;
		reflexOptionsCache.frameLimitUs = options.frameLimitUs;
		reflexOptionsCache.useMarkersToOptimize = options.useMarkersToOptimize;
	};

	const auto& imageReconstruction = globals::pipeline::imageReconstruction;
	const bool reflexBlockedByFrameGeneration = imageReconstruction.IsFrameGenerationDx12PathActive();
	if (reflexBlockedByFrameGeneration) {
		sl::ReflexOptions disabledOptions{};
		disabledOptions.mode = sl::ReflexMode::eOff;
		disabledOptions.frameLimitUs = 0u;
		disabledOptions.useMarkersToOptimize = false;
		applyReflexOptionsIfChanged(disabledOptions, "Failed to disable Reflex while frame-generation DX12 path is active");
		return;
	}

	auto& settings = globals::pipeline::imageReconstruction.settings;

	sl::ReflexOptions options{};
	if (!settings.reflexLowLatencyMode) {
		options.mode = sl::ReflexMode::eOff;
	} else {
		options.mode = settings.reflexLowLatencyBoost ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
	}

	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	float reflexFPSLimit = originalReflexFPSLimit;
	if (!std::isfinite(reflexFPSLimit)) {
		reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = reflexFPSLimit;
		logger::warn("[Streamline] reflexFPSLimit is not finite ({}), using {}", originalReflexFPSLimit, reflexFPSLimit);
	}
	const float fpsLimit = std::clamp(reflexFPSLimit, 20.0f, 240.0f);
	options.frameLimitUs = settings.reflexUseFPSLimit ? static_cast<uint32_t>(std::lround(1000000.0 / static_cast<double>(fpsLimit))) : 0u;
	options.useMarkersToOptimize = settings.reflexUseMarkersToOptimize && featurePCL;

	applyReflexOptionsIfChanged(options, "Failed to apply Reflex options");

	if (!slReflexSleep)
		return;

	if (options.mode == sl::ReflexMode::eOff && options.frameLimitUs == 0)
		return;

	const uint32_t currentFrame = globals::state ? globals::state->frameCount : 0;
	// PollInputDevices can run more than once; sleep must happen once per frame token.
	if (lastReflexSleepFrame == currentFrame)
		return;

	if (!EnsureFrameToken())
		return;

	lastReflexSleepFrame = currentFrame;
	if (SL_FAILED(result, slReflexSleep(*frameToken))) {
		logger::warn("[Streamline] Reflex sleep call failed: {}", magic_enum::enum_name(result));
	}
}

/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
void Streamline::DestroyDLSSResources()
{
	if (!slDLSSSetOptions || !slFreeResources)
		return;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;

	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}
