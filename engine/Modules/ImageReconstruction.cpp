#include "ImageReconstruction.h"

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "CameraSuite.h"
#include "HybridGI.h"
#include "Hooks.h"
#include "State.h"
#include "Menu.h"
#include "ImageReconstruction/DX12SwapChain.h"
#include "ImageReconstruction/FidelityFX.h"
#include "ImageReconstruction/Streamline.h"
#include "Utils/Game.h"
#include "Utils/UI.h"
#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <directx/d3dx12.h>
#include <format>
#include <filesystem>

#define I18N_KEY_PREFIX "feature.image_reconstruction."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImageReconstruction::Settings,
	upscaleMethod,
	upscaleMethodNoDLSS,
	qualityMode,
	frameLimitMode,
	frameLimitFPS,
	frameGenerationMode,
	frameGenerationBackend,
	dlssgGeneratedFrames,
	frameGenerationForceEnable,
	frameGenerationAllowInMenus,
	streamlineLogLevel,
	sharpnessFSR,
	sharpnessEnabledDLSS,
	sharpnessDLSS,
	presetDLSS,
	forceLatestDLSSModelOnLegacyRTX,
	neuralRenderingEnabled,
	neuralRenderingPreset,
	neuralRenderingQualityMode,
	neuralRenderingOutputPreset,
	neuralRenderingIntensity,
	neuralRenderingLocalTone,
	neuralRenderingLocalStructure,
	neuralRenderingSkinStructure,
	neuralRenderingStyle,
	neuralRenderingAutoMask,
	neuralRenderingUICorrection,
	reflexLowLatencyMode,
	reflexLowLatencyBoost,
	reflexUseMarkersToOptimize,
	reflexUseFPSLimit,
	reflexFPSLimit);

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChainUpscaling;

/**
 * @brief Creates a Direct3D 11 device and swap chain, with support for advanced imageReconstruction and frame generation features.
 *
 * This function intercepts the standard D3D11 device and swap chain creation process to enable integration with Streamline and FidelityFX technologies, as well as optional D3D12 proxying for frame generation. It adjusts swap chain flags for tearing support, manages feature checks, and conditionally routes device creation through Streamline or FidelityFX proxies based on runtime settings and hardware capabilities. If frame generation is enabled and supported, a D3D12 proxy is used; otherwise, the standard D3D11 creation path is followed.
 *
 * @return HRESULT indicating the success or failure of device and swap chain creation.
 */
HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainUpscaling(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);
	globals::state->SetAdapterDescription(adapterDesc.Description);

	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	imageReconstruction.LoadUpscalingSDKs();

	if (imageReconstruction.IsBackendInitialized())
		imageReconstruction.CheckBackendFeatures(pAdapter);

	// FLIP_DISCARD requires BufferCount >= 2 and a flip-model-compatible (non-sRGB) format.
	pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	if (pSwapChainDesc->BufferCount < 2)
		pSwapChainDesc->BufferCount = 2;

	if (globals::pipeline::cameraSuite.loaded) {
		logger::info("[ImageReconstruction] Upgrading swap chain format from {} to R10G10B10A2_UNORM for HDR", static_cast<int>(pSwapChainDesc->BufferDesc.Format));
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	} else if (pSwapChainDesc->BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	const bool sidecarAllowed = pSwapChainDesc->Windowed != FALSE;
	if (!sidecarAllowed) {
		// The isolated DX12 presenter is a windowed/borderless path. Do not force it
		// onto Skyrim's exclusive swap chain: that can leave the image stalled until
		// focus changes. Keep native fullscreen stable and explain the requirement.
		logger::warn("[ImageReconstruction] DX12 sidecar unavailable in exclusive fullscreen; switch Skyrim to borderless/windowed for Neural Rendering or Frame Generation");
	}

	auto refreshRate = ImageReconstruction::GetRefreshRate(pSwapChainDesc->OutputWindow);
	imageReconstruction.refreshRate = refreshRate;

	const bool neuralRenderingProvisioned =
		imageReconstruction.settings.neuralRenderingEnabled &&
		imageReconstruction.settings.upscaleMethod == static_cast<uint>(ImageReconstruction::UpscaleMethod::kDLSS) &&
		imageReconstruction.streamline.neuralRenderingSupportedOnCurrentAdapter;
	const bool frameGenerationRequested = imageReconstruction.settings.frameGenerationMode != 0;
	const bool refreshAllowsFrameGeneration = refreshRate >= 120 || imageReconstruction.settings.frameGenerationForceEnable;
	DX12SwapChain::Presenter requestedPresenter = DX12SwapChain::Presenter::kNone;

	if (sidecarAllowed && frameGenerationRequested && refreshAllowsFrameGeneration) {
		if (!imageReconstruction.UsesDLSSGFrameGeneration()) {
			if (imageReconstruction.HasFrameGenModule())
				requestedPresenter = DX12SwapChain::Presenter::kFidelityFX;
			else
				logger::warn("[Frame Generation] FSR3 runtime is unavailable; preserving the native D3D11 swap chain");
		} else if (imageReconstruction.streamlineDX12.initialized && adapterDesc.VendorId == 0x10DE) {
			// DLSS-G capability is only reliable after the D3D12 instance is bound to
			// a real device. Probe the raw device first so a failed probe cannot taint
			// the independent FidelityFX presenter.
			auto& sidecar = imageReconstruction.dx12SwapChain;
			auto& dlssg = imageReconstruction.streamlineDX12;
			sidecar.CreateD3D12Device(pAdapter);
			if (dlssg.SetD3DDevice12(sidecar.d3d12Device.get())) {
				dlssg.CheckFeatures(pAdapter);
				dlssg.PostDevice();
				if (dlssg.featureDLSSG && dlssg.slUpgradeInterface) {
					logger::info("[Streamline DX12] Upgrading D3D12 device for DLSS-G");
					// Preserve the native device for NGX and resource allocation.
					sidecar.dlssgDevice.copy_from(sidecar.d3d12Device.get());
					auto* proxyDevice = sidecar.dlssgDevice.detach();
					const sl::Result upgradeResult = dlssg.slUpgradeInterface(
						reinterpret_cast<void**>(&proxyDevice));
					sidecar.dlssgDevice.attach(proxyDevice);
					if (upgradeResult == sl::Result::eOk) {
						// The presentation queue must originate from the upgraded device.
						sidecar.RecreateCommandObjects();
						logger::info("[Streamline DX12] D3D12 device and presentation queue upgraded");
						requestedPresenter = DX12SwapChain::Presenter::kDLSSG;
					} else {
						sidecar.dlssgDevice = nullptr;
						logger::error("[Streamline DX12] D3D12 device upgrade failed: {}", magic_enum::enum_name(upgradeResult));
					}
				}
			}
			if (requestedPresenter != DX12SwapChain::Presenter::kDLSSG)
				logger::warn("[Frame Generation] DLSS-G is unavailable; preserving the native D3D11 swap chain");
		} else {
			logger::warn("[Frame Generation] DLSS-G requires its DX12 Streamline runtime on an NVIDIA adapter; preserving the native D3D11 swap chain");
		}
	} else if (frameGenerationRequested && !refreshAllowsFrameGeneration) {
		logger::info("[Frame Generation] Sidecar not provisioned below 120 Hz unless Force Enable is selected");
	}

	if (sidecarAllowed && requestedPresenter == DX12SwapChain::Presenter::kNone && neuralRenderingProvisioned) {
		// Neural-only sessions use a plain native D3D12 swap chain. FidelityFX and
		// the DLSS-G presentation hooks remain completely outside this path.
		requestedPresenter = DX12SwapChain::Presenter::kNeuralOnly;
	}
	const bool shouldProxy = requestedPresenter != DX12SwapChain::Presenter::kNone;

	imageReconstruction.lowRefreshRate = refreshRate < 120;
	imageReconstruction.isWindowed = pSwapChainDesc->Windowed;
	imageReconstruction.frameGenerationRequestedAtBoot =
		requestedPresenter == DX12SwapChain::Presenter::kFidelityFX ||
		requestedPresenter == DX12SwapChain::Presenter::kDLSSG;
	imageReconstruction.neuralRenderingRequestedAtBoot = imageReconstruction.settings.neuralRenderingEnabled;
	imageReconstruction.neuralRenderingProvisionedAtBoot = shouldProxy && neuralRenderingProvisioned;
	imageReconstruction.neuralRenderingQualityModeAtBoot = imageReconstruction.settings.neuralRenderingQualityMode;
	imageReconstruction.neuralRenderingOutputPresetAtBoot = imageReconstruction.settings.neuralRenderingOutputPreset;

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;

	if (shouldProxy) {
		logger::info(
			"[ImageReconstruction] PIXL DX12 sidecar enabled (frameGeneration={}, neuralRenderingLive={}, neuralRenderingPhotoProvisioned={})",
			imageReconstruction.settings.frameGenerationMode != 0,
			imageReconstruction.settings.neuralRenderingEnabled,
			imageReconstruction.neuralRenderingProvisionedAtBoot);

		DX::ThrowIfFailed(D3D11CreateDevice(
				pAdapter,
				DriverType,
				Software,
				Flags,
				&featureLevel,
				1,
				SDKVersion,
				ppDevice,
				pFeatureLevel,
				ppImmediateContext));

		// Interop must retain the real D3D11 device. Streamline's D3D11 wrapper is
		// returned to Skyrim only after the sidecar and its shared fences exist.
		imageReconstruction.SetProxyD3D11Device(*ppDevice);
		imageReconstruction.SetProxyD3D11DeviceContext(*ppImmediateContext);
		imageReconstruction.CreateProxySwapChain(pAdapter, *pSwapChainDesc, requestedPresenter);
		imageReconstruction.CreateProxyInterop();

		*ppSwapChain = imageReconstruction.GetProxySwapChain();
		imageReconstruction.d3d12SwapChainActive = true;

		if (imageReconstruction.IsBackendInitialized()) {
			imageReconstruction.UpgradeBackendInterface(reinterpret_cast<void**>(ppDevice));
			imageReconstruction.SetBackendD3DDevice(*ppDevice);
			imageReconstruction.CheckBackendFeatures(pAdapter);
			imageReconstruction.PostBackendDevice();
		}

		return S_OK;
	}

	auto ret = ptrD3D11CreateDeviceAndSwapChainUpscaling(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (imageReconstruction.IsBackendInitialized()) {
		imageReconstruction.UpgradeBackendInterface((void**)&(*ppDevice));
		imageReconstruction.UpgradeBackendInterface((void**)&(*ppSwapChain));
		imageReconstruction.SetBackendD3DDevice(*ppDevice);
		// Re-check after device bind to ensure feature availability is accurate.
		imageReconstruction.CheckBackendFeatures(pAdapter);
		imageReconstruction.PostBackendDevice();
	}

	return ret;
}

void ImageReconstruction::DrawSettings()
{
	// Display imageReconstruction options in the UI
	std::vector<std::string> upscaleModes = {
		T(TKEY("method_none"), "None"),
		T(TKEY("method_taa"), "TAA")
	};

	std::string fsrLabel = "AMD FSR 3.1";
	upscaleModes.push_back(fsrLabel);

	std::string dlssLabel = "NVIDIA DLSS";
	upscaleModes.push_back(dlssLabel);

	// Determine available modes
	bool featureDLSS = streamline.featureDLSS;
	bool featureFSR = true;  // FSR is always available

	uint32_t* currentUpscaleMode = &settings.upscaleMethod;
	uint32_t availableModes = 1;  // Start with TAA
	if (featureFSR)
		availableModes = 2;  // Add FSR
	if (featureDLSS)
		availableModes = 3;  // Add DLSS if available
	else
		currentUpscaleMode = &settings.upscaleMethodNoDLSS;

	// Dropdown for method selection
	std::vector<const char*> modeLabels;
	for (uint32_t i = 0; i <= availableModes; ++i)
		modeLabels.push_back(upscaleModes[i].c_str());
	ImGui::Combo(T(TKEY("method"), "Method"), (int*)currentUpscaleMode, modeLabels.data(), (int)modeLabels.size());

	*currentUpscaleMode = std::min(availableModes, *currentUpscaleMode);

	// Check the current upscale method
	auto upscaleMethod = GetUpscaleMethod();

	// Display warning for DLSS resolution limits
	if (upscaleMethod == UpscaleMethod::kDLSS) {
		float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
		if (screenSize.x > streamline.MAX_RESOLUTION || screenSize.y > streamline.MAX_RESOLUTION) {
			Util::Text::Warning("Warning: Requested resolution %.0f x %.0f exceeds maximum supported resolution %d x %d for DLSS.",
				screenSize.x, screenSize.y, streamline.MAX_RESOLUTION, streamline.MAX_RESOLUTION);
			Util::Text::Warning("DLSS will not function. Lower your resolution or select a different imageReconstruction method.");
		}
	}

	// Display imageReconstruction settings if applicable
	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		const char* upscalePresetsDLSS[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_dlaa"), "DLAA")
		};
		const char* upscalePresets[] = {
			T(TKEY("preset_ultra_performance"), "Ultra Performance"),
			T(TKEY("preset_performance"), "Performance"),
			T(TKEY("preset_balanced"), "Balanced"),
			T(TKEY("preset_quality"), "Quality"),
			T(TKEY("preset_native_aa"), "Native AA")
		};

		// Compute a safe preset index (4 - qualityMode) clamped to [0,4] to avoid negative/overflow indexing
		int presetIndex = 0;
		if (settings.qualityMode <= 4)
			presetIndex = 4 - static_cast<int>(settings.qualityMode);
		presetIndex = std::clamp(presetIndex, 0, 4);

		// Choose preset name set and the corresponding scales once, then show a
		// single SliderInt to avoid duplicated calls.
		const char* baseLabel = nullptr;

		if (upscaleMethod == UpscaleMethod::kFSR) {
			baseLabel = upscalePresets[presetIndex];
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			baseLabel = upscalePresetsDLSS[presetIndex];
		}

		if (baseLabel) {
			// Format the label with preset name and resolution scale
			std::string labelWithScale = std::format("{} ( {:.2f}x )", baseLabel, (resolutionScale.x + resolutionScale.y) * 0.5f);

			ImGui::SliderInt(T(TKEY("upscale_preset"), "Upscale Preset"), (int*)&settings.qualityMode, 0, 4, labelWithScale.c_str(), ImGuiSliderFlags_AlwaysClamp);
		}

		if (upscaleMethod == UpscaleMethod::kFSR) {
			ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessFSR, 0.0f, 1.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SeparatorText("FSR GENERATION");
			ImGui::Text("AMD FSR 3.1");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.42f, 0.82f, 0.64f, 1.0f), "ACTIVE / DX11");
		} else if (upscaleMethod == UpscaleMethod::kDLSS) {
			ImGui::Checkbox(T(TKEY("enable_sharpening"), "Enable Sharpening"), &settings.sharpnessEnabledDLSS);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T(TKEY("enable_sharpening_tooltip"),
									  "Applies RCAS sharpening to the DLSS output.\n"
									  "Off by default; DLSS already resolves a sharp image."));
			}

			if (settings.sharpnessEnabledDLSS)
				ImGui::SliderFloat(T(TKEY("sharpness"), "Sharpness"), &settings.sharpnessDLSS, 0.0f, 1.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

			const char* presets[] = {
				T(TKEY("dlss_model_preset_default"), "Default"),
				T(TKEY("dlss_model_preset_j"), "Preset J"),
				T(TKEY("dlss_model_preset_k"), "Preset K"),
				T(TKEY("dlss_model_preset_f"), "Preset F (DLAA / Ultra Performance)")
			};
			// Streamline 2.10.3 only accepts Default/J/K/F. Legacy E was
			// removed by NVIDIA, while L/M are explicitly resolved to Default.
			int presetSelection = settings.presetDLSS == 1 ? 1 :
				settings.presetDLSS == 2 ? 2 : settings.presetDLSS == 5 ? 3 : 0;
			if (ImGui::Combo(
					T(TKEY("dlss_model_preset"), "DLSS Model Preset"),
					&presetSelection,
					presets,
					IM_ARRAYSIZE(presets))) {
				constexpr uint storedPreset[]{ 0, 1, 2, 5 };
				settings.presetDLSS = storedPreset[std::clamp(presetSelection, 0, 3)];
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextWrapped(
					"Choose a DLSS SR model preset honored by the installed Streamline 2.10.3 runtime. "
					"NVIDIA removed legacy preset E; requesting it would silently select Default. "
					"Changing this setting requires a restart.");
			}

			if (streamline.isRTXBelow40series) {
				ImGui::SeparatorText("DLSS RUNTIME POLICY");
				if (ImGui::Checkbox("Allow latest model on RTX 20 / 30", &settings.forceLatestDLSSModelOnLegacyRTX))
					globals::state->Save();
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextWrapped("Overrides PIXL's conservative legacy-RTX preset policy and requests the latest installed DLSS model family. Your installed signed NVIDIA runtime remains authoritative; this does not spoof GPU support or replace its DLL.");
				}
				ImGui::TextColored(
					settings.forceLatestDLSSModelOnLegacyRTX ? ImVec4(0.42f, 0.82f, 0.64f, 1.0f) : ImVec4(0.72f, 0.75f, 0.78f, 1.0f),
					"%s",
					settings.forceLatestDLSSModelOnLegacyRTX ? "LATEST INSTALLED MODEL REQUESTED" : "COMPATIBILITY MODEL POLICY");
			}

			if (ImGui::TreeNodeEx("DLSS Neural Rendering (Experimental)")) {
				ImGui::TextWrapped(
					"Runs NVIDIA's experimental DLSSNR 310.8 model through PIXL Renderer's own DX12 sidecar. "
					"It is optional, version-gated, and automatically retains normal DLSS output if evaluation fails.");

				const bool configuredAtBoot = d3d12SwapChainActive && neuralRenderingProvisionedAtBoot;
				if (settings.neuralRenderingEnabled && !configuredAtBoot)
					Util::Text::Warning("Restart required to create the PIXL DX12 sidecar for Neural Rendering.");
				if (settings.frameGenerationMode)
					ImGui::TextWrapped("Frame Generation is scheduled after the neural presentation copy and uses dedicated raw depth/motion guides.");
				if (globals::pipeline::cameraSuite.loaded && globals::pipeline::cameraSuite.settings.enableHDR)
					Util::Text::Warning("The validated DLSSNR 310.8 path is SDR-only; HDR currently uses normal DLSS.");
				if (GetModuleHandleW(L"renodx-dlss.addon64"))
					Util::Text::Warning("RenoDX DLSS addon detected. PIXL's native path is bypassed to prevent double processing.");

				ImGui::Checkbox("Enable Neural Rendering", &settings.neuralRenderingEnabled);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextWrapped(
						"Requires nvngx_dlssnr.dll 310.8.x in Data/Shaders/ImageReconstruction/Streamline. "
						"DLSS sessions provision the sidecar at startup. The real-time toggle can remain off while Photo Finish uses the same model temporarily. No ReShade or RenoDX addon is used by PIXL's path.");
				}

				const char* neuralPresets[]{ "Natural", "Balanced", "Detail", "Strong / Experimental", "Custom" };
				int neuralPreset = static_cast<int>(std::min(settings.neuralRenderingPreset, 4u));
				if (ImGui::Combo("Neural Rendering Preset", &neuralPreset, neuralPresets, IM_ARRAYSIZE(neuralPresets))) {
					settings.neuralRenderingPreset = static_cast<uint>(neuralPreset);
					switch (settings.neuralRenderingPreset) {
					case 0:
						settings.neuralRenderingIntensity = 0.8f;
						settings.neuralRenderingLocalTone = 0.75f;
						settings.neuralRenderingLocalStructure = 0.9f;
						settings.neuralRenderingSkinStructure = 0.9f;
						break;
					case 1:
						settings.neuralRenderingIntensity = 1.0f;
						settings.neuralRenderingLocalTone = 1.0f;
						settings.neuralRenderingLocalStructure = 1.0f;
						settings.neuralRenderingSkinStructure = 1.0f;
						break;
					case 2:
						settings.neuralRenderingIntensity = 1.35f;
						settings.neuralRenderingLocalTone = 0.9f;
						settings.neuralRenderingLocalStructure = 1.6f;
						settings.neuralRenderingSkinStructure = 1.15f;
						break;
					case 3:
						settings.neuralRenderingIntensity = 1.75f;
						settings.neuralRenderingLocalTone = 1.25f;
						settings.neuralRenderingLocalStructure = 1.5f;
						settings.neuralRenderingSkinStructure = 1.3f;
						break;
					default:
						break;
					}
					pendingNeuralRenderingReset.store(true, std::memory_order_release);
				}

				const auto markCustom = [&]() {
					settings.neuralRenderingPreset = 4;
					pendingNeuralRenderingReset.store(true, std::memory_order_release);
				};
				if (ImGui::SliderFloat("NR Intensity", &settings.neuralRenderingIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					markCustom();
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextWrapped("Overall Neural Rendering contribution. Natural (0.80) is the restrained release-safe starting point.");

				if (ImGui::TreeNode("Advanced NR Tuning")) {
					const char* inputResolutionModes[]{
						"DLAA / Native 100%",
						"Quality",
						"Balanced",
						"Performance",
						"Ultra Performance"
					};
					int inputResolutionMode = static_cast<int>(std::min(settings.qualityMode, 4u));
					if (ImGui::Combo(
							"NR / DLSS Input Resolution",
							&inputResolutionMode,
							inputResolutionModes,
							IM_ARRAYSIZE(inputResolutionModes))) {
						settings.qualityMode = static_cast<uint>(inputResolutionMode);
						pendingDLSSReset.store(true, std::memory_order_release);
						pendingNeuralRenderingReset.store(true, std::memory_order_release);
						FidelityFX::needsReset.store(true, std::memory_order_release);
					}
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextWrapped(
							"Controls the real scene-render input shared by DLSS Super Resolution and Neural Rendering. "
							"Lower modes reduce geometry, lighting and model-guide pixels before reconstruction. "
							"This is the supported workload control shown as 'internal resolution' in DLSS benchmarks; "
							"Feature 18's output itself must remain at display resolution with the installed 310.8 runtime.");
					if (globals::game::graphicsState) {
						const auto displayWidth = globals::game::graphicsState->screenWidth;
						const auto displayHeight = globals::game::graphicsState->screenHeight;
						const auto inputWidth = static_cast<std::uint32_t>(
							std::max(1.0f, std::floor(static_cast<float>(displayWidth) * resolutionScale.x)));
						const auto inputHeight = static_cast<std::uint32_t>(
							std::max(1.0f, std::floor(static_cast<float>(displayHeight) * resolutionScale.y)));
						ImGui::TextDisabled(
							"Current scene input: %u x %u (%.0f%% x %.0f%%) -> %u x %u",
							inputWidth,
							inputHeight,
							resolutionScale.x * 100.0f,
							resolutionScale.y * 100.0f,
							displayWidth,
							displayHeight);
					}

					const char* qualityModes[]{
						"Follow Current DLSS Mode",
						"DLAA",
						"Quality",
						"Balanced",
						"Performance",
						"Ultra Performance",
						"Ultra Quality"
					};
					int qualityMode = static_cast<int>(settings.neuralRenderingQualityMode);
					if (ImGui::Combo(
							"NR Quality Contract",
							&qualityMode,
							qualityModes,
							IM_ARRAYSIZE(qualityModes)))
						settings.neuralRenderingQualityMode = static_cast<uint>(qualityMode);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextWrapped("Sets the real NGX PerfQualityValue used when Feature 18 is created. Follow Current DLSS Mode is safest. An override changes model policy, not the allocated output resolution.");

					const char* outputPresets[]{ "Runtime Default", "Preset 1", "Preset 2", "Preset 3" };
					int outputPreset = static_cast<int>(settings.neuralRenderingOutputPreset);
					if (ImGui::Combo(
							"NR Output Preset",
							&outputPreset,
							outputPresets,
							IM_ARRAYSIZE(outputPresets)))
						settings.neuralRenderingOutputPreset = static_cast<uint>(outputPreset);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextWrapped("Selects the installed 310.8 runtime's DLSSNR.Hint.Render.Preset at Feature 18 creation. These private presets are intentionally numbered because NVIDIA does not publish stable semantic names for them.");

					if (settings.neuralRenderingQualityMode != neuralRenderingQualityModeAtBoot ||
						settings.neuralRenderingOutputPreset != neuralRenderingOutputPresetAtBoot)
						Util::Text::Warning("Restart required to apply the NR quality contract or output preset.");
					ImGui::TextWrapped("Model precision: NVIDIA runtime automatic. The installed 310.8 DLL exposes FP8/FP16 kernels but no application INT4 parameter, so PIXL cannot safely force INT4.");

					if (ImGui::SliderFloat("Local Tone", &settings.neuralRenderingLocalTone, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						markCustom();
					if (ImGui::SliderFloat("Local Structure", &settings.neuralRenderingLocalStructure, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						markCustom();
					if (ImGui::SliderFloat("Skin Structure", &settings.neuralRenderingSkinStructure, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						markCustom();
					int neuralStyle = static_cast<int>(settings.neuralRenderingStyle);
					if (ImGui::SliderInt("Model Style", &neuralStyle, 0, 3, "%d", ImGuiSliderFlags_AlwaysClamp)) {
						settings.neuralRenderingStyle = static_cast<uint>(neuralStyle);
						markCustom();
					}
					ImGui::Checkbox("Automatic Character Mask", &settings.neuralRenderingAutoMask);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::TextWrapped(
							"Uses DLSSNR's learned character mask so Skin Structure is concentrated on people. "
							"It is not a terrain mask: terrain and architecture use the global Local Tone and Local Structure controls. "
							"PIXL conditions depth, motion and disocclusion guides separately for world stability.");
					ImGui::Checkbox("UI Correction", &settings.neuralRenderingUICorrection);
					ImGui::TreePop();
				}

				ImGui::Separator();
				ImGui::Text("Runtime: %s", neuralRendering.GetStatusText());
				if (!neuralRendering.GetVersion().empty())
					ImGui::Text("Version: %s", neuralRendering.GetVersion().c_str());
				if (!neuralRendering.GetDetail().empty())
					ImGui::TextWrapped("%s", neuralRendering.GetDetail().c_str());
				if (neuralRendering.GetSuccessfulFrames() > 0)
					ImGui::Text("Successful frames: %llu", static_cast<unsigned long long>(neuralRendering.GetSuccessfulFrames()));

				ImGui::TreePop();
			}
		}
	}

	const bool frameGenerationDx12PathActive = IsFrameGenerationDx12PathActive();

	if (ImGui::TreeNodeEx(T(TKEY("frame_generation"), "Frame Generation"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("%s", T(TKEY("frame_generation_desc"),
							  "Frame Generation interpolates real frames with generated ones for a smoother experience"));
		ImGui::Text("%s", T(TKEY("frame_generation_tech"),
							  "Uses AMD FSR Frame Generation technology"));
		if (HasFrameGenModule())
			ImGui::Text("%s", T(TKEY("frame_generation_available"),
					"AMD FSR Frame Generation is available."));
		if (HasDLSSGModule())
			ImGui::Text("NVIDIA DLSS Frame Generation is available (SM86 proxy compatible).");

		DrawFrameGenerationBackendSelector();
		ImGui::Text("%s", T(TKEY("frame_generation_proxy_note"),
							  "Requires a D3D11 to D3D12 proxy which can create compatibility issues"));
		ImGui::Text("%s", T(TKEY("frame_generation_restart_note"),
							  "Toggling this setting requires a restart to work correctly"));

		bool onlyRequiresRestart = true;

		if (!isWindowed)
			ImGui::TextDisabled("Exclusive fullscreen is active. Changing display mode requires a restart while FG is enabled.");

		if (lowRefreshRate && !settings.frameGenerationForceEnable) {
			Util::Text::Warning("Warning: Requires a high refresh rate monitor or Force Enable Frame Generation");

			onlyRequiresRestart = false;
		}

		if (fidelityFXMissing && !UsesDLSSGFrameGeneration()) {
			Util::Text::Warning("Warning: FidelityFX DLLs are not loaded");

			onlyRequiresRestart = false;
		}

		if (onlyRequiresRestart && settings.frameGenerationMode && !frameGenerationDx12PathActive)
			Util::Text::Warning("Warning: Requires restart");

		if (!settings.frameGenerationMode && frameGenerationDx12PathActive)
			Util::Text::Warning("Warning: Requires restart");

		bool fgEnabled = settings.frameGenerationMode != 0;
		if (ImGui::Checkbox(T(TKEY("frame_generation"), "Frame Generation"), &fgEnabled)) {
			settings.frameGenerationMode = fgEnabled ? 1 : 0;
			if (fgEnabled)
				settings.frameGenerationForceEnable = 1;
		}

		switch (GetFrameGenerationState()) {
		case FrameGenerationState::Active:
			ImGui::TextColored(ImVec4(0.42f, 0.82f, 0.64f, 1.0f), "ON - GENERATING FRAMES");
			break;
		case FrameGenerationState::TemporarilySuspended:
			ImGui::TextColored(ImVec4(0.92f, 0.72f, 0.30f, 1.0f), "ON - TEMPORARILY PAUSED WHILE THIS MENU IS OPEN");
			break;
		case FrameGenerationState::Starting:
			ImGui::TextColored(ImVec4(0.42f, 0.82f, 0.64f, 1.0f), "ON - FRAME-GENERATION PATH READY");
			break;
		case FrameGenerationState::RestartRequired:
			ImGui::TextColored(ImVec4(0.92f, 0.72f, 0.30f, 1.0f), "RESTART REQUIRED TO APPLY THE SWITCH ABOVE");
			break;
		case FrameGenerationState::Unavailable:
			ImGui::TextColored(ImVec4(0.92f, 0.42f, 0.36f, 1.0f), "OFF - CURRENT DISPLAY OR RUNTIME REQUIREMENTS ARE NOT MET");
			break;
		case FrameGenerationState::RuntimeFault:
			ImGui::TextColored(ImVec4(0.92f, 0.42f, 0.36f, 1.0f), "OFF - BACKEND ERROR (SEE PIXLRENDERER.LOG)");
			break;
		case FrameGenerationState::Off:
		default:
			ImGui::TextDisabled("OFF");
			break;
		}

		if (!frameGenerationDx12PathActive)
			ImGui::BeginDisabled();

		bool flEnabled = settings.frameLimitMode != 0;
		if (ImGui::Checkbox(T(TKEY("frame_limit_vrr"), "Frame Limit (Variable Refresh Rate)"), &flEnabled))
			settings.frameLimitMode = flEnabled ? 1 : 0;

		if (!frameGenerationDx12PathActive)
			ImGui::EndDisabled();

		ImGui::TextWrapped("Allows frame generation to function on low refresh rate monitors. Detected: %.2f Hz", refreshRate);
		bool fgForce = settings.frameGenerationForceEnable != 0;
		if (ImGui::Checkbox(T(TKEY("force_enable_frame_generation"), "Force Enable Frame Generation"), &fgForce))
			settings.frameGenerationForceEnable = fgForce ? 1 : 0;

		ImGui::Checkbox(T(TKEY("frame_generation_in_menus"), "Frame Generation in Menus"), &settings.frameGenerationAllowInMenus);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("frame_generation_in_menus_tooltip_1"), "Keeps frame generation active while game menus are open."));
			ImGui::TextUnformatted(T(TKEY("frame_generation_in_menus_tooltip_2"), "May feel smoother, but increases menu input latency."));
		}
		if (UsesDLSSGFrameGeneration()) {
			const char* multipliers[] = { "2x", "3x", "4x" };
			int multiplier = static_cast<int>(std::clamp(settings.dlssgGeneratedFrames, 1u, 3u)) - 1;
			if (ImGui::Combo("DLSS-G frame multiplier", &multiplier, multipliers, _countof(multipliers)))
				settings.dlssgGeneratedFrames = static_cast<uint>(multiplier + 1);
		}

		ImGui::TreePop();
	}

	if (streamline.reflexSupportedOnCurrentAdapter && ImGui::TreeNodeEx(T(TKEY("nvidia_reflex"), "NVIDIA Reflex"), ImGuiTreeNodeFlags_DefaultOpen)) {
		const bool dlssgReflex = dx12SwapChain.presenter == DX12SwapChain::Presenter::kDLSSG;
		const auto& reflexRuntime = dlssgReflex ? streamlineDX12 : streamline;
		const bool reflexBlockedByFrameGeneration = frameGenerationDx12PathActive && !dlssgReflex;
		const bool reflexAvailable = reflexRuntime.IsReflexAvailable();
		const bool reflexControlsAvailable = reflexAvailable && !reflexBlockedByFrameGeneration;
		const bool markerOptimizationAvailable = reflexControlsAvailable && reflexRuntime.featurePCL;
		if (dlssgReflex)
			ImGui::TextWrapped("DLSS-G automatically enables Reflex during generation. Boost and the limiter use its DX12 runtime; the cap is rendered FPS, not generated output.");
		if (reflexBlockedByFrameGeneration) {
			ImGui::TextDisabled("%s", T(TKEY("reflex_blocked_by_fg"), "Reflex is unavailable while the DX12 frame-generation swapchain is active."));
		}

		if (!reflexAvailable) {
			ImGui::TextDisabled("%s", T(TKEY("reflex_not_available"), "Reflex is not available. Ensure sl.reflex.dll is present and restart."));
		}

		if (!reflexControlsAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("low_latency_mode"), "Low Latency Mode"), &settings.reflexLowLatencyMode);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("low_latency_mode_tooltip_1"), "Cuts input delay by syncing CPU work closer to the GPU."));
			ImGui::TextUnformatted(T(TKEY("low_latency_mode_tooltip_2"), "Can reduce max FPS a little, but usually feels more responsive."));
		}

		if (!settings.reflexLowLatencyMode && !dlssgReflex)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("low_latency_boost"), "Low Latency Boost"), &settings.reflexLowLatencyBoost);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("low_latency_boost_tooltip_1"), "Keeps GPU clocks higher to avoid latency spikes at low GPU load."));
			ImGui::TextUnformatted(T(TKEY("low_latency_boost_tooltip_2"), "Useful if frametime jumps; costs extra power and heat."));
		}

		if (!markerOptimizationAvailable)
			ImGui::BeginDisabled();

		ImGui::Checkbox(T(TKEY("use_markers_to_optimize"), "Use Markers To Optimize"), &settings.reflexUseMarkersToOptimize);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("use_markers_to_optimize_tooltip_1"), "Uses frame markers for tighter Reflex timing."));
			ImGui::TextUnformatted(T(TKEY("use_markers_to_optimize_tooltip_2"), "Try On first; turn Off if it causes stutter on your setup."));
		}

		if (!markerOptimizationAvailable)
			ImGui::EndDisabled();

		if (!markerOptimizationAvailable) {
			ImGui::TextDisabled("%s", T(TKEY("marker_optimization_unavailable"), "Marker optimization unavailable (PCL not loaded)."));
		}

		ImGui::Checkbox(T(TKEY("use_fps_limit"), "Use FPS Limit"), &settings.reflexUseFPSLimit);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("use_fps_limit_tooltip_1"), "Uses Reflex's internal FPS cap for steadier frametimes."));
			ImGui::TextUnformatted(T(TKEY("use_fps_limit_tooltip_2"), "Can lower latency versus uncapped rendering."));
		}

		if (!settings.reflexLowLatencyMode && !dlssgReflex)
			ImGui::EndDisabled();

		if (!settings.reflexUseFPSLimit)
			ImGui::BeginDisabled();

		if (!std::isfinite(settings.reflexFPSLimit))
			settings.reflexFPSLimit = 60.0f;
		settings.reflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
		ImGui::SliderFloat(T(TKEY("fps_limit"), "FPS Limit"), &settings.reflexFPSLimit, 20.0f, 240.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T(TKEY("fps_limit_tooltip_1"), "Set your frame cap target."));
			ImGui::TextUnformatted(T(TKEY("fps_limit_tooltip_2"), "Start about 2-3 FPS below refresh rate (e.g. 117 for 120 Hz)."));
		}

		if (!settings.reflexUseFPSLimit)
			ImGui::EndDisabled();

		if (!reflexControlsAvailable)
			ImGui::EndDisabled();

		ImGui::TreePop();
	}

	if (globals::state->IsDeveloperMode() && ImGui::TreeNodeEx(T(TKEY("backend_diagnostics"), "Backend Diagnostics"))) {
		// Streamline log level selection
		const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevelIdx = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo(T(TKEY("streamline_logging"), "Streamline Logging"), &logLevelIdx, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<uint>(logLevelIdx);
		}
		ImGui::TextUnformatted(T(TKEY("streamline_logging_restart_note"), "Changing this requires a restart to take effect."));
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T(TKEY("streamline_logging_tooltip"), "Streamline logging controls the verbosity of NVIDIA Streamline backend logs. Useful for debugging issues with DLSS/DLSS-G."));
		}

		ImGui::Separator();
		Util::DrawDllVersionTable("AMD FidelityFX DLLs (click to open folder)", FidelityFX::PluginDir, FidelityFX::dllVersions, "ffx_dll_versions");
		Util::DrawDllVersionTable("NVIDIA Streamline DLLs (click to open folder)", Streamline::PluginDir, Streamline::dllVersions, "sl_dll_versions");
		ImGui::TreePop();
	}
}

void ImageReconstruction::SaveSettings(json& o_json)
{
	o_json = settings;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->WriteSetting(setting);
		}
	}
}

void ImageReconstruction::LoadSettings(json& o_json)
{
	settings = o_json;

	// Sanitize loaded settings to ensure enum indices are valid
	constexpr auto enumCount = 4;  // UpscaleMethod has 4 values: kNONE, kTAA, kFSR, kDLSS
	if (settings.upscaleMethod >= static_cast<uint>(enumCount)) {
		logger::warn("[ImageReconstruction] Loaded upscaleMethod {} out of range, clamping to {}", settings.upscaleMethod, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethod = enumCount ? enumCount - 1 : 0;
	}
	if (settings.upscaleMethodNoDLSS >= static_cast<uint>(enumCount)) {
		logger::warn("[ImageReconstruction] Loaded upscaleMethodNoDLSS {} out of range, clamping to {}", settings.upscaleMethodNoDLSS, enumCount ? enumCount - 1 : 0);
		settings.upscaleMethodNoDLSS = enumCount ? enumCount - 1 : 0;
	}
	if (settings.presetDLSS > 5 || settings.presetDLSS == 3 || settings.presetDLSS == 4) {
		logger::warn("[ImageReconstruction] Loaded unsupported presetDLSS {}, resetting to 0 (Default)", settings.presetDLSS);
		settings.presetDLSS = 0;
	}
	settings.neuralRenderingPreset = std::min(settings.neuralRenderingPreset, 4u);
	settings.neuralRenderingQualityMode = std::min(settings.neuralRenderingQualityMode, 6u);
	settings.neuralRenderingOutputPreset = std::min(settings.neuralRenderingOutputPreset, 3u);
	settings.neuralRenderingStyle = std::min(settings.neuralRenderingStyle, 3u);
	const auto sanitizeNeuralFloat = [](float& value, float fallback) {
		value = std::isfinite(value) ? std::clamp(value, 0.0f, 2.0f) : fallback;
	};
	sanitizeNeuralFloat(settings.neuralRenderingIntensity, 0.8f);
	sanitizeNeuralFloat(settings.neuralRenderingLocalTone, 0.75f);
	sanitizeNeuralFloat(settings.neuralRenderingLocalStructure, 0.9f);
	sanitizeNeuralFloat(settings.neuralRenderingSkinStructure, 0.9f);
	const float originalReflexFPSLimit = settings.reflexFPSLimit;
	if (!std::isfinite(settings.reflexFPSLimit)) {
		settings.reflexFPSLimit = 60.0f;
		logger::warn(
			"[ImageReconstruction] Loaded reflexFPSLimit {} is not finite, resetting to {}",
			originalReflexFPSLimit,
			settings.reflexFPSLimit);
	}
	const float clampedReflexFPSLimit = std::clamp(settings.reflexFPSLimit, 20.0f, 240.0f);
	if (clampedReflexFPSLimit != settings.reflexFPSLimit) {
		logger::warn(
			"[ImageReconstruction] Loaded reflexFPSLimit {} out of range, clamping to {}",
			settings.reflexFPSLimit,
			clampedReflexFPSLimit);
	}
	settings.reflexFPSLimit = clampedReflexFPSLimit;
	auto iniSettingCollection = globals::game::iniPrefSettingCollection;
	if (iniSettingCollection) {
		auto setting = iniSettingCollection->GetSetting("bUseTAA:Display");
		if (setting) {
			iniSettingCollection->ReadSetting(setting);
		}
	}
}

void ImageReconstruction::RestoreDefaultSettings()
{
	settings = {};
}

void ImageReconstruction::DataLoaded()
{
	// Fix screenshots fix from Engine Fixes
	Util::DisableVanillaTAA();

	// The game defaults this to a non-zero value
	static auto fDRClampOffset = RE::GetINISetting("fDRClampOffset:Display");
	fDRClampOffset->data.f = 0.0f;
}

void ImageReconstruction::Load()
{
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChainUpscaling = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainUpscaling, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
}

struct BSImageSpace_Init_FXAA
{
	static void thunk()
	{
		func();

		// Force FXAA off safely
		auto fxaaEnabled = reinterpret_cast<bool*>(REL::RelocationID(513281, 391028).address());
		*fxaaEnabled = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};
void ImageReconstruction::PostPostLoad()
{
	bool isGOG = !GetModuleHandle(L"steam_api64.dll");
	stl::detour_thunk<MenuManagerDrawInterfaceStartHook>(REL::RelocationID(79947, 82084));

	// Calculates resolution and jitter
	stl::write_thunk_call<Main_UpdateJitter>(REL::RelocationID(75460, 77245).address() + REL::Relocate(0xE5, isGOG ? 0x133 : 0xE2));

	// Disables the original dynamic resolution system
	REL::safe_write(REL::RelocationID(35556, 36555).address() + REL::Relocate(0x2D, 0x2D), REL::NOP5, sizeof(REL::NOP5));

	// Performs imageReconstruction in between volumetric lighting and post processing
	stl::write_thunk_call<Main_PostProcessing>(REL::RelocationID(100430, 107148).address() + REL::Relocate(0x1F0, 0x1E7));

	// Patches RSSetScissorRect calls to use dynamic resolution
	stl::detour_thunk<SetScissorRect>(REL::RelocationID(75564, 77365));

	// Patches facegen texture generation to not use dynamic resolution
	stl::detour_thunk<BSFaceGenManager_UpdatePendingCustomizationTextures>(REL::RelocationID(26455, 27041));

	// Patches precipitation camera to not use dynamic resolution
	stl::write_thunk_call<Main_RenderPrecipitation>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));

	// Forces FXAA off
	stl::detour_thunk<BSImageSpace_Init_FXAA>(REL::RelocationID(98974, 105626));

	MenuOpenCloseEventHandler::Register();

	logger::info("[ImageReconstruction] Installed hooks");
}

RE::BSEventNotifyControl ImageReconstruction::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		auto& reconstruction = globals::pipeline::imageReconstruction;
		reconstruction.pendingDLSSReset.store(true, std::memory_order_release);
		reconstruction.pendingNeuralRenderingReset.store(true, std::memory_order_release);
		// Frame generation owns separate temporal state and may be active even when
		// the selected image reconstruction method is not FSR.
		FidelityFX::needsReset.store(true, std::memory_order_release);
	}
	return RE::BSEventNotifyControl::kContinue;
}

bool ImageReconstruction::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto* ui = globals::game::ui;
	if (!ui) {
		logger::error("[PIXL Image Reconstruction] UI event source unavailable; loading-transition history reset disabled");
		return false;
	}
	auto* eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[PIXL Image Reconstruction] Menu event source unavailable; loading-transition history reset disabled");
		return false;
	}
	eventSource->AddEventSink(&singleton);
	return true;
}

#undef I18N_KEY_PREFIX

ImageReconstruction::UpscaleMethod ImageReconstruction::GetUpscaleMethod() const
{
	if (streamline.featureDLSS)
		return (UpscaleMethod)settings.upscaleMethod;
	return (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void ImageReconstruction::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[ImageReconstruction] Creating texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (a_upscalemethod == UpscaleMethod::kDLSS || a_upscalemethod == UpscaleMethod::kFSR) {
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		if (!reactiveMaskTexture) {
			reactiveMaskTexture = new Texture2D(texDesc);
			reactiveMaskTexture->CreateSRV(srvDesc);
			reactiveMaskTexture->CreateUAV(uavDesc);
		}

		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = new Texture2D(texDesc);
			transparencyCompositionMaskTexture->CreateSRV(srvDesc);
			transparencyCompositionMaskTexture->CreateUAV(uavDesc);
		}
	}

	// Skyrim exposes the main depth buffer as R24G8_TYPELESS. FidelityFX's DX11
	// backend describes that resource as integer color rather than depth, so FSR
	// receives an explicitly encoded R32_FLOAT copy of the same raw device depth.
	if (a_upscalemethod == UpscaleMethod::kFSR && !fsrDepthTexture) {
		main.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;

		srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		fsrDepthTexture = new Texture2D(texDesc);
		fsrDepthTexture->CreateSRV(srvDesc);
		fsrDepthTexture->CreateUAV(uavDesc);
		Util::SetResourceName(fsrDepthTexture->resource.get(), "PIXL FSR Typed Depth");
	}

	// Motion vector copy texture is only needed for DLSS
	if (a_upscalemethod == UpscaleMethod::kDLSS) {
		if (!motionVectorCopyTexture) {
			auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

			D3D11_TEXTURE2D_DESC motionTexDesc{};
			motionVector.texture->GetDesc(&motionTexDesc);

			texDesc.Format = motionTexDesc.Format;
			srvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			motionVectorCopyTexture = new Texture2D(motionTexDesc);
			motionVectorCopyTexture->CreateSRV(srvDesc);
			motionVectorCopyTexture->CreateUAV(uavDesc);
		}

		// RCAS sharpener texture - matches kMAIN format for HDR sharpening
		if (!sharpenerTexture) {
			main.texture->GetDesc(&texDesc);
			main.SRV->GetDesc(&srvDesc);

			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			srvDesc.Format = texDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;

			uavDesc.Format = texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			sharpenerTexture = new Texture2D(texDesc);
			sharpenerTexture->CreateSRV(srvDesc);
			sharpenerTexture->CreateUAV(uavDesc);
		}
	}
}

void ImageReconstruction::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
{
	logger::debug("[ImageReconstruction] Destroying texture resources for method {} ({})", static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

	// Clean up D3D11 textures that are no longer needed
	// Only destroy textures when switching away from methods that use them
	if (a_upscalemethod != UpscaleMethod::kDLSS && a_upscalemethod != UpscaleMethod::kFSR) {
		if (reactiveMaskTexture) {
			reactiveMaskTexture->srv = nullptr;
			reactiveMaskTexture->uav = nullptr;
			reactiveMaskTexture->resource = nullptr;

			delete reactiveMaskTexture;
			reactiveMaskTexture = nullptr;
		}

		if (transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture->srv = nullptr;
			transparencyCompositionMaskTexture->uav = nullptr;
			transparencyCompositionMaskTexture->resource = nullptr;

			delete transparencyCompositionMaskTexture;
			transparencyCompositionMaskTexture = nullptr;
		}
	}

	// Motion vector copy texture is only needed for DLSS - destroy when switching away from DLSS
	if (a_upscalemethod != UpscaleMethod::kDLSS) {
		if (motionVectorCopyTexture) {
			motionVectorCopyTexture->srv = nullptr;
			motionVectorCopyTexture->uav = nullptr;
			motionVectorCopyTexture->resource = nullptr;

			delete motionVectorCopyTexture;
			motionVectorCopyTexture = nullptr;
		}
		if (sharpenerTexture) {
			sharpenerTexture->srv = nullptr;
			sharpenerTexture->uav = nullptr;
			sharpenerTexture->resource = nullptr;

			delete sharpenerTexture;
			sharpenerTexture = nullptr;
		}
	}

	if (a_upscalemethod != UpscaleMethod::kFSR && fsrDepthTexture) {
		fsrDepthTexture->srv = nullptr;
		fsrDepthTexture->uav = nullptr;
		fsrDepthTexture->resource = nullptr;
		delete fsrDepthTexture;
		fsrDepthTexture = nullptr;
	}
}

void ImageReconstruction::CheckResources(UpscaleMethod a_upscalemethod)
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	static bool previousFrameGenMode = false;

	bool frameGenModeCurrent = (settings.frameGenerationMode && d3d12SwapChainActive);
	bool frameGenModeChanged = frameGenModeCurrent != previousFrameGenMode;
	bool upscaleModeChanged = (previousUpscaleMode != a_upscalemethod);

	if (upscaleModeChanged || frameGenModeChanged) {
		logger::debug("[ImageReconstruction] Resource change detected - Upscale: {} ({}) -> {} ({}), FrameGen: {} -> {} (d3d12Active={})",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode), static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod), previousFrameGenMode, frameGenModeCurrent, d3d12SwapChainActive);

		// Destroy previous imageReconstruction method resources (only if they were actually active)
		if (upscaleModeChanged) {
			DestroyUpscalingTextureResources(a_upscalemethod);

			// Only destroy SDK resources if the previous method was actually performing imageReconstruction
			if (previousUpscalingWasActive) {
				if (previousUpscaleMode == UpscaleMethod::kDLSS)
					streamline.DestroyDLSSResources();
				else if (previousUpscaleMode == UpscaleMethod::kFSR)
					fidelityFX.DestroyFSRResources();
			}
			if (a_upscalemethod == UpscaleMethod::kFSR)
				fidelityFX.CreateFSRResources();
		}

		// Create new imageReconstruction method resources
		if (upscaleModeChanged) {
			CreateUpscalingTextureResources(a_upscalemethod);
		}

		// Update tracking for next call
		previousUpscaleMode = a_upscalemethod;
		previousFrameGenMode = (settings.frameGenerationMode && d3d12SwapChainActive);
		previousUpscalingWasActive = IsUpscalingActive();
	}
}

ID3D11ComputeShader* ImageReconstruction::GetEncodeTexturesCS()
{
	auto upscaleMethod = GetUpscaleMethod();
	uint methodIndex = (uint)upscaleMethod;

	if (!encodeTexturesCS[methodIndex]) {
		logger::debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

		std::vector<std::pair<const char*, const char*>> defines;

		// Add upscale method define
		switch (upscaleMethod) {
		case UpscaleMethod::kDLSS:
			defines.push_back({ "DLSS", "" });
			break;
		case UpscaleMethod::kFSR:
			defines.push_back({ "FSR", "" });
			defines.push_back({ "DEPTH_OUTPUT", "" });
			break;
		default:
			// No define for NONE or TAA
			break;
		}

		encodeTexturesCS[methodIndex].attach((ID3D11ComputeShader*)Util::CompileShader(L"Data/Shaders/ImageReconstruction/EncodeTexturesCS.hlsl", defines, "cs_5_0"));
	}
	return encodeTexturesCS[methodIndex].get();
}

ID3D11PixelShader* ImageReconstruction::GetDepthRefractionUpscalePS()
{
	if (!depthRefractionUpscalePS) {
		logger::debug("Compiling DepthRefractionUpscalePS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		depthRefractionUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/ImageReconstruction/DepthRefractionUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return depthRefractionUpscalePS.get();
}

ID3D11PixelShader* ImageReconstruction::GetUnderwaterMaskUpscalePS()
{
	if (!underwaterMaskUpscalePS) {
		logger::debug("Compiling UnderwaterMaskPS.hlsl");
		std::vector<std::pair<const char*, const char*>> defines = { { "PSHADER", "" } };
		underwaterMaskUpscalePS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data/Shaders/ImageReconstruction/UnderwaterMaskUpscalePS.hlsl", defines, "ps_5_0"));
	}

	return underwaterMaskUpscalePS.get();
}

ID3D11VertexShader* ImageReconstruction::GetUpscaleVS()
{
	if (!upscaleVS) {
		logger::debug("Compiling UpscaleVS.hlsl");
		upscaleVS.attach((ID3D11VertexShader*)Util::CompileShader(L"Data/Shaders/ImageReconstruction/UpscaleVS.hlsl", { { "VSHADER", "" } }, "vs_5_0"));
	}

	return upscaleVS.get();
}

eastl::unique_ptr<Texture2D> ImageReconstruction::CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
	bool copyBindFlags, bool createSRV, bool createUAV, const char* name)
{
	D3D11_TEXTURE2D_DESC srcDesc;
	static_cast<ID3D11Texture2D*>(src)->GetDesc(&srcDesc);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = copyBindFlags ? srcDesc.BindFlags : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);

	auto tex = eastl::make_unique<Texture2D>(desc);

	if (name) {
		Util::SetResourceName(tex->resource.get(), name);
	}

	if (createSRV) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = srcDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		tex->CreateSRV(srvDesc);
	}
	if (createUAV) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = srcDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		tex->CreateUAV(uavDesc);
	}
	return tex;
}

int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
{
	const float basePhaseCount = 8.0f;
	const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
	return jitterPhaseCount;
}

// Calculate halton number for index and base.
static float Halton(int32_t index, int32_t base)
{
	float f = 1.0f, result = 0.0f;

	for (int32_t currentIndex = index; currentIndex > 0;) {
		f /= (float)base;
		result = result + f * (float)(currentIndex % base);
		currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
	}

	return result;
}

void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
	const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
	const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

	*outX = x;
	*outY = y;
}

void ImageReconstruction::BeginPhotoCaptureJitter(std::uint32_t targetSamples)
{
	photoCaptureJitterActive = true;
	photoCaptureJitterIndex = 0u;
	photoCaptureJitterPhaseCount =
		std::clamp<std::uint32_t>(
			std::max<std::uint32_t>(targetSamples, 32u),
			32u,
			128u);

	logger::info(
		"Photo Finish projection jitter enabled: {} unique phases requested for {} sample(s).",
		photoCaptureJitterPhaseCount,
		targetSamples);
}

void ImageReconstruction::SetPhotoCaptureJitterSample(std::uint32_t sampleIndex)
{
	photoCaptureJitterIndex = sampleIndex;
}

void ImageReconstruction::EndPhotoCaptureJitter()
{
	if (photoCaptureJitterActive)
		logger::info("Photo Finish projection jitter released.");

	photoCaptureJitterActive = false;
	photoCaptureJitterIndex = 0u;
}

void ImageReconstruction::BeginPhotoCaptureRenderOverride(
	float minimumRenderScale,
	bool enableNeuralRendering)
{
	const bool activateNeural = enableNeuralRendering && CanUsePhotoNeuralRendering();
	photoCaptureNeuralOverrideActive.store(activateNeural, std::memory_order_release);
	// Feature 18's most reliable still-image contract is a native DLAA input.
	// Do not mutate the serialized gameplay setting; expose quality mode 0 only
	// for the lifetime of this transaction and restore it atomically on exit.
	photoCaptureMinimumRenderScale = activateNeural
		? 1.0f
		: std::clamp(minimumRenderScale, 0.0f, 1.0f);
	photoCaptureRenderOverrideActive = true;
	pendingDLSSReset.store(true, std::memory_order_release);
	pendingNeuralRenderingReset.store(true, std::memory_order_release);
	FidelityFX::needsReset.store(true, std::memory_order_release);
	globals::pipeline::hybridGI.queuedResetTemporalHistory.store(true, std::memory_order_release);
	if (activateNeural) {
		logger::info("Photo Finish neural override enabled: temporary DLAA/native input, gameplay Neural Rendering setting preserved.");
	}
	if (photoCaptureMinimumRenderScale > 0.0f) {
		logger::info(
			"Photo Finish internal render override enabled: minimum {:.0f}% of display resolution.",
			photoCaptureMinimumRenderScale * 100.0f);
	} else {
		logger::info("Photo Finish internal render override enabled at the current gameplay scale.");
	}
}

void ImageReconstruction::EndPhotoCaptureRenderOverride()
{
	if (!photoCaptureRenderOverrideActive)
		return;

	photoCaptureRenderOverrideActive = false;
	photoCaptureMinimumRenderScale = 0.0f;
	photoCaptureNeuralOverrideActive.store(false, std::memory_order_release);
	pendingDLSSReset.store(true, std::memory_order_release);
	pendingNeuralRenderingReset.store(true, std::memory_order_release);
	FidelityFX::needsReset.store(true, std::memory_order_release);
	globals::pipeline::hybridGI.queuedResetTemporalHistory.store(true, std::memory_order_release);
	logger::info("Photo Finish render/neural overrides released; gameplay reconstruction restored.");
}

void ImageReconstruction::ConfigureTAA()
{
	auto upscaleMethod = GetUpscaleMethod();

	// Force enable TAA if needed
	Util::SetTemporal(upscaleMethod != UpscaleMethod::kNONE);
}

void ImageReconstruction::ConfigureUpscaling(RE::BSGraphics::State* a_viewport)
{
	auto upscaleMethod = GetUpscaleMethod();

	// Delete or create resources as necessary
	CheckResources(upscaleMethod);

	// Cache original TAA values for UI
	projectionPosScaleX = a_viewport->projectionPosScaleX;
	projectionPosScaleY = a_viewport->projectionPosScaleY;

	// Get full screen size
	auto state = globals::state;
	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

	auto screenWidth = static_cast<int>(screenSize.x);
	auto screenHeight = static_cast<int>(screenSize.y);

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA) {
		float resolutionScaleBase = 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)GetEffectiveQualityMode());
		if (photoCaptureRenderOverrideActive) {
			resolutionScaleBase = std::max(
				resolutionScaleBase,
				photoCaptureMinimumRenderScale);
		}

		auto renderWidth = static_cast<int>(screenWidth * resolutionScaleBase);
		auto renderHeight = static_cast<int>(screenHeight * resolutionScaleBase);

		resolutionScale.x = static_cast<float>(renderWidth) / static_cast<float>(screenWidth);
		resolutionScale.y = static_cast<float>(renderHeight) / static_cast<float>(screenHeight);

		auto phaseCount = GetJitterPhaseCount(renderWidth, screenWidth);

		if (photoCaptureJitterActive) {
			GetJitterOffset(
				&jitter.x,
				&jitter.y,
				static_cast<int32_t>(photoCaptureJitterIndex),
				static_cast<int32_t>(
					std::max<std::uint32_t>(
						photoCaptureJitterPhaseCount,
						static_cast<std::uint32_t>(phaseCount))));
		} else {
			GetJitterOffset(&jitter.x, &jitter.y, state->frameCount, phaseCount);
		}

		a_viewport->projectionPosScaleX = -2.0f * jitter.x / renderWidth;

		a_viewport->projectionPosScaleY = 2.0f * jitter.y / renderHeight;
	} else {
		resolutionScale = { 1.0f, 1.0f };

		if (photoCaptureJitterActive) {
			GetJitterOffset(
				&jitter.x,
				&jitter.y,
				static_cast<int32_t>(photoCaptureJitterIndex),
				static_cast<int32_t>(photoCaptureJitterPhaseCount));

			a_viewport->projectionPosScaleX = -2.0f * jitter.x / screenWidth;
			a_viewport->projectionPosScaleY = 2.0f * jitter.y / screenHeight;
		} else {
			jitter.x = -a_viewport->projectionPosScaleX * screenWidth / 2.0f;

			jitter.y = a_viewport->projectionPosScaleY * screenHeight / 2.0f;
		}
	}

	auto& runtimeData = a_viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = dynamicResolutionWidthRatio;
	runtimeData.dynamicResolutionPreviousHeightRatio = dynamicResolutionHeightRatio;
	runtimeData.dynamicResolutionWidthRatio = resolutionScale.x;
	runtimeData.dynamicResolutionHeightRatio = resolutionScale.y;

	dynamicResolutionWidthRatio = resolutionScale.x;
	dynamicResolutionHeightRatio = resolutionScale.y;

	// Disable dynamic resolution unless the game explicitly enables it
	runtimeData.dynamicResolutionLock = 1;
}

void ImageReconstruction::SetupResources()
{
	QueryPerformanceFrequency(&qpf);

	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	main.texture->GetDesc(&texDesc);
	main.SRV->GetDesc(&srvDesc);
	main.UAV->GetDesc(&uavDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthEnable = true;                           // Enable depth testing
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // Write to all depth bits
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;          // Always pass depth test (write all depths)

	depthStencilDesc.StencilEnable = false;  // Disable stencil testing

	DX::ThrowIfFailed(globals::d3d::device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

	// Create jitter offset constant buffer for depth imageReconstruction
	jitterCB = new ConstantBuffer(ConstantBufferDesc<JitterCB>());

	// Create imageReconstruction data constant buffer for encode textures compute shader
	upscalingDataCB = new ConstantBuffer(ConstantBufferDesc<UpscalingDataCB>());

	// Create blend state for depth imageReconstruction
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].BlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	DX::ThrowIfFailed(globals::d3d::device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

	// Create rasterizer state for fullscreen rendering
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;
	DX::ThrowIfFailed(globals::d3d::device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

	CheckResources(GetUpscaleMethod());

	rcas.Initialize();

	if (d3d12SwapChainActive)
		dx12SwapChain.CreateSharedResources();

	copyDepthToSharedBufferPS.attach((ID3D11PixelShader*)Util::CompileShader(L"Data\\Shaders\\ImageReconstruction\\CopyDepthToSharedBufferPS.hlsl", { { "PSHADER", "" } }, "ps_5_0"));

	// CameraSuite is ordered after ImageReconstruction in the central module list,
	// so its own SetupResources pass sees the finalized reconstruction/swap-chain
	// state. Calling it here as well destroyed and recreated the full HDR/camera
	// resource set twice during every renderer initialization.
}

void ImageReconstruction::ClearShaderCache()
{
	for (int i = 0; i < 4; ++i) {
		encodeTexturesCS[i] = nullptr;  // com_ptr automatically releases
	}

	depthRefractionUpscalePS = nullptr;  // com_ptr automatically releases
	underwaterMaskUpscalePS = nullptr;   // com_ptr automatically releases
	upscaleVS = nullptr;                 // com_ptr automatically releases
}

void ImageReconstruction::CopySharedD3D12Resources(bool a_useNeuralGuides)
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction - Copy Shared D3D12 Resources");
	globals::state->BeginPerfEvent("Copy Shared D3D12 Resources");

	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;

	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	ID3D11Texture2D* motionSource = motionVector.texture;
	if (a_useNeuralGuides && motionVectorCopyTexture && motionVectorCopyTexture->resource)
		motionSource = motionVectorCopyTexture->resource.get();
	auto* motionTarget = a_useNeuralGuides
		? dx12SwapChain.neuralMotionVectorBufferShared12.get()
		: dx12SwapChain.motionVectorBufferShared12.get();
	auto* depthTarget = a_useNeuralGuides
		? dx12SwapChain.neuralDepthBufferShared12.get()
		: dx12SwapChain.depthBufferShared12.get();
	if (!motionTarget || !depthTarget) {
		globals::state->EndPerfEvent();
		return;
	}
	context->CopyResource(motionTarget->resource11, motionSource);

	if (a_useNeuralGuides) {
		const auto guideSize = Util::ConvertToDynamic(float2{
			static_cast<float>(globals::game::graphicsState->screenWidth),
			static_cast<float>(globals::game::graphicsState->screenHeight) });
		dx12SwapChain.neuralGuideWidth = std::max(1u, static_cast<UINT>(guideSize.x));
		dx12SwapChain.neuralGuideHeight = std::max(1u, static_cast<UINT>(guideSize.y));
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	{
		// Set up viewport for fullscreen rendering
		float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		// Set up pixel shader resources
		ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(views), views);

		// Set render target view for pixel shader output
		ID3D11RenderTargetView* rtvs[1] = { depthTarget->rtv };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(copyDepthToSharedBufferPS.get(), nullptr, 0);

		globals::profiler->BeginPass("ImageReconstruction::CopyDepthD3D12");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	// Clean up
	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(views), views);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);

	globals::state->EndPerfEvent();
}

void UpdateCameraData()
{
	using func_t = decltype(&UpdateCameraData);
	static REL::Relocation<func_t> func{ RELOCATION_ID(75472, 77258) };
	func();
}

void ImageReconstruction::PostDisplay()
{
	auto viewport = globals::game::graphicsState;

	viewport->projectionPosScaleX = projectionPosScaleX;
	viewport->projectionPosScaleY = projectionPosScaleY;

	auto& runtimeData = viewport->GetRuntimeData();

	runtimeData.dynamicResolutionPreviousWidthRatio = 1;
	runtimeData.dynamicResolutionPreviousHeightRatio = 1;
	runtimeData.dynamicResolutionWidthRatio = 1;
	runtimeData.dynamicResolutionHeightRatio = 1;
	runtimeData.dynamicResolutionLock = 1;

	globals::game::renderer->UpdateViewPort(0, 0, 1);
	UpdateCameraData();

	if (d3d12SwapChainActive)
		globals::pipeline::cameraSuite.SetUIBuffer();

	globals::state->UpdateSharedData(false, false);
}

void ImageReconstruction::TimerSleepQPC(int64_t targetQPC)
{
	static const int64_t timerFrequency = []() {
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		return frequency.QuadPart;
	}();
	if (timerFrequency <= 0)
		return;

	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
		const int64_t remaining = targetQPC - currentQPC.QuadPart;
		const int64_t twoMilliseconds = (timerFrequency * 2) / 1000;
		if (remaining > twoMilliseconds) {
			const DWORD sleepMs = static_cast<DWORD>(std::max<int64_t>(1, (remaining * 1000 / timerFrequency) - 1));
			Sleep(sleepMs);
		} else {
			YieldProcessor();
		}
	} while (currentQPC.QuadPart < targetQPC);
}

void ImageReconstruction::FrameLimiter()
{
	if (d3d12SwapChainActive) {
		// Use frame latency waitable object if available for better frame pacing
		HANDLE waitableObject = GetFrameLatencyWaitableObject();

		// Wait for the next frame presentation slot
		if (waitableObject)
			WaitForSingleObject(waitableObject, INFINITE);

	}

	if (!settings.frameLimitMode)
		return;
	if (qpf.QuadPart <= 0)
		QueryPerformanceFrequency(&qpf);
	if (qpf.QuadPart <= 0)
		return;

	const double presentedTarget = settings.frameLimitFPS > 0.0f ?
	                                   std::clamp(static_cast<double>(settings.frameLimitFPS), 20.0, 360.0) :
	                                   std::max(static_cast<double>(refreshRate), 20.0);
	const double renderTarget = ShouldUseFrameGenerationThisFrame() ? presentedTarget * 0.5 : presentedTarget;
	const int64_t targetFrameTicks = static_cast<int64_t>(static_cast<double>(qpf.QuadPart) / renderTarget);

	static LARGE_INTEGER lastFrame{};
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	if (lastFrame.QuadPart != 0 && now.QuadPart - lastFrame.QuadPart < targetFrameTicks)
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	QueryPerformanceCounter(&lastFrame);
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double ImageReconstruction::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS && wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						// get the refresh rate
						UINT numerator = p.targetInfo.refreshRate.Numerator;
						UINT denominator = p.targetInfo.refreshRate.Denominator;
						return (double)numerator / (double)denominator;
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

bool ImageReconstruction::IsFrameGenerationDx12PathActive() const
{
	return d3d12SwapChainActive;
}

bool ImageReconstruction::IsFrameGenerationActive() const
{
	if (!IsFrameGenerationDx12PathActive() || !frameGenerationRequestedAtBoot || !settings.frameGenerationMode)
		return false;
	if (dx12SwapChain.presenter == DX12SwapChain::Presenter::kDLSSG)
		return streamlineDX12.featureDLSSG && streamlineDX12.dlssgConfiguredState == 1;
	return dx12SwapChain.presenter == DX12SwapChain::Presenter::kFidelityFX &&
		!fidelityFX.frameGenerationRuntimeFault && fidelityFX.isFrameGenActive;
}

bool ImageReconstruction::IsFrameGenerationTemporarilySuspended() const
{
	auto* ui = globals::game::ui;
	auto* state = globals::state;
	const bool menuOpen = (globals::menu && globals::menu->IsEnabled) ||
		(ui && ui->GameIsPaused()) || (state && state->IsMainOrLoadingMenuOpen(ui));
	const bool presenterAvailable = dx12SwapChain.presenter == DX12SwapChain::Presenter::kDLSSG ?
		streamlineDX12.featureDLSSG :
		(dx12SwapChain.presenter == DX12SwapChain::Presenter::kFidelityFX && !fidelityFX.frameGenerationRuntimeFault);
	return IsFrameGenerationDx12PathActive() && frameGenerationRequestedAtBoot && settings.frameGenerationMode &&
		presenterAvailable && menuOpen && !settings.frameGenerationAllowInMenus;
}

bool ImageReconstruction::ShouldUseFrameGenerationThisFrame() const
{
	const bool presenterAvailable = dx12SwapChain.presenter == DX12SwapChain::Presenter::kDLSSG ?
		streamlineDX12.featureDLSSG :
		(dx12SwapChain.presenter == DX12SwapChain::Presenter::kFidelityFX && !fidelityFX.frameGenerationRuntimeFault);
	return IsFrameGenerationDx12PathActive() && frameGenerationRequestedAtBoot && settings.frameGenerationMode &&
		presenterAvailable && !IsFrameGenerationTemporarilySuspended();
}

bool ImageReconstruction::IsNeuralRenderingConfiguredForSession()
{
	return streamline.neuralRenderingSupportedOnCurrentAdapter && d3d12SwapChainActive &&
	       neuralRenderingProvisionedAtBoot && settings.neuralRenderingEnabled &&
	       GetUpscaleMethod() == UpscaleMethod::kDLSS;
}

bool ImageReconstruction::CanUsePhotoNeuralRendering()
{
	if (!streamline.neuralRenderingSupportedOnCurrentAdapter ||
		!d3d12SwapChainActive || !neuralRenderingProvisionedAtBoot ||
		GetUpscaleMethod() != UpscaleMethod::kDLSS)
		return false;
	if (globals::pipeline::cameraSuite.loaded && globals::pipeline::cameraSuite.settings.enableHDR)
		return false;
	if (GetModuleHandleW(L"renodx-dlss.addon64")) {
		static bool conflictLogged = false;
		if (!conflictLogged) {
			logger::warn("[NeuralRendering] RenoDX DLSS addon is loaded; PIXL native Neural Rendering is bypassed to prevent double processing");
			conflictLogged = true;
		}
		return false;
	}
	const auto status = neuralRendering.GetStatus();
	return status == NeuralRendering::Status::NotProbed ||
	       status == NeuralRendering::Status::Ready ||
	       status == NeuralRendering::Status::Initialized;
}

bool ImageReconstruction::ShouldUseNeuralRenderingThisFrame()
{
	// Live neural processing must not filter the late-drawn settings UI.
	// Explicit photo processing retains its separate capture workflow.
	if (globals::menu && globals::menu->IsEnabled && !IsPhotoNeuralRenderingActive())
		return false;
	if (!settings.neuralRenderingEnabled && !IsPhotoNeuralRenderingActive())
		return false;
	return CanUsePhotoNeuralRendering();
}

void ImageReconstruction::ApplyNeuralRenderingPreset(uint preset)
{
	settings.neuralRenderingPreset = std::min(preset, 4u);
	switch (settings.neuralRenderingPreset) {
	case 0:
		settings.neuralRenderingIntensity = 0.8f;
		settings.neuralRenderingLocalTone = 0.75f;
		settings.neuralRenderingLocalStructure = 0.9f;
		settings.neuralRenderingSkinStructure = 0.9f;
		break;
	case 1:
		settings.neuralRenderingIntensity = 1.0f;
		settings.neuralRenderingLocalTone = 1.0f;
		settings.neuralRenderingLocalStructure = 1.0f;
		settings.neuralRenderingSkinStructure = 1.0f;
		break;
	case 2:
		settings.neuralRenderingIntensity = 1.35f;
		settings.neuralRenderingLocalTone = 0.9f;
		settings.neuralRenderingLocalStructure = 1.6f;
		settings.neuralRenderingSkinStructure = 1.15f;
		break;
	case 3:
		settings.neuralRenderingIntensity = 1.75f;
		settings.neuralRenderingLocalTone = 1.25f;
		settings.neuralRenderingLocalStructure = 1.5f;
		settings.neuralRenderingSkinStructure = 1.3f;
		break;
	default:
		break;
	}
	pendingNeuralRenderingReset.store(true, std::memory_order_release);
}

std::string ImageReconstruction::ToggleNeuralRenderingFromHotkey()
{
	if (!streamline.neuralRenderingSupportedOnCurrentAdapter)
		return "Neural Rendering requires an NVIDIA RTX 30-series GPU or newer.";

	if (GetUpscaleMethod() != UpscaleMethod::kDLSS)
		return "Select DLSS in Camera > Reconstruction before enabling Neural Rendering.";

	if (settings.neuralRenderingEnabled) {
		settings.neuralRenderingEnabled = false;
		pendingNeuralRenderingReset.store(true, std::memory_order_release);
		if (globals::state)
			globals::state->Save();
		return "PIXL Neural Rendering disabled.";
	}

	settings.neuralRenderingEnabled = true;
	pendingNeuralRenderingReset.store(true, std::memory_order_release);
	if (globals::state)
		globals::state->Save();

	if (d3d12SwapChainActive && neuralRenderingProvisionedAtBoot)
		return "PIXL Neural Rendering enabled.";

	return "Neural Rendering enabled for next launch. Restart Skyrim once to provision the sidecar; keep it enabled at startup.";
}

std::string ImageReconstruction::ToggleFrameGenerationFromHotkey()
{
	settings.frameGenerationMode = settings.frameGenerationMode ? 0u : 1u;
	if (settings.frameGenerationMode)
		settings.frameGenerationForceEnable = 1;
	if (globals::state)
		globals::state->Save();
	return settings.frameGenerationMode
		? "Frame Generation enabled for next launch. Restart Skyrim if the runtime path is not active."
		: "Frame Generation disabled for next launch.";
}

ImageReconstruction::FrameGenerationState ImageReconstruction::GetFrameGenerationState() const
{
	const bool requested = settings.frameGenerationMode != 0;
	const bool pathActive = IsFrameGenerationDx12PathActive();

	if (fidelityFX.frameGenerationRuntimeFault)
		return FrameGenerationState::RuntimeFault;
	if (!requested)
		return frameGenerationRequestedAtBoot ? FrameGenerationState::RestartRequired : FrameGenerationState::Off;
	if (!frameGenerationRequestedAtBoot)
		return FrameGenerationState::RestartRequired;
	if (!pathActive) {
		if (!isWindowed || fidelityFXMissing || (lowRefreshRate && !settings.frameGenerationForceEnable))
			return FrameGenerationState::Unavailable;
		return FrameGenerationState::RestartRequired;
	}
	if (IsFrameGenerationTemporarilySuspended())
		return FrameGenerationState::TemporarilySuspended;
	if (IsFrameGenerationActive())
		return FrameGenerationState::Active;
	return FrameGenerationState::Starting;
}

bool ImageReconstruction::IsUpscalingActive() const
{
	auto method = GetUpscaleMethod();

	// Only consider vendor upscalers (FSR/DLSS) as "active" when the
	// selected method actually produces a downscale. If the renderer is
	// currently running at 1:1 (no downscale), treat imageReconstruction as inactive.
	if (!(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS)) {
		return false;
	}

	// resolutionScale.x represents renderWidth / displayWidth.
	return resolutionScale.x < .99f;
}

/**
 * @brief Retrieves the current frame time for frame generation.
 *
 * Returns the frame time from the D3D12 swap chain if frame generation is active; otherwise, returns 0.
 *
 * @return float The current frame time in seconds, or 0 if frame generation is inactive.
 */
float ImageReconstruction::GetFrameGenerationFrameTime() const
{
	if (!IsFrameGenerationActive())
		return 0.0f;

	// Get the current frame time from D3D12 swapchain
	if (dx12SwapChain.swapChain) {
		// Get frame time from the D3D12 SwapChain
		return GetFrameTime();
	}

	return 0.0f;
}

// Unified interface methods
void ImageReconstruction::LoadUpscalingSDKs()
{
	// Initialize imageReconstruction SDK components during plugin startup
	// This ensures all SDKs are available before any D3D device creation
	streamline.LoadInterposer();
	fidelityFX.LoadFFX();  // Only for frame generation now
	const bool needsDX12Sidecar =
		(settings.frameGenerationMode && UsesDLSSGFrameGeneration()) ||
		(settings.neuralRenderingEnabled &&
			settings.upscaleMethod == static_cast<uint>(UpscaleMethod::kDLSS));
	if (!needsDX12Sidecar) {
		// DLSS Neural Rendering also provisions through the D3D12 sidecar. Without
		// loading this interposer at startup, the UI can remain stuck on “restart
		// required” forever even after Skyrim has been restarted.
		return;
	}
	streamlineDX12.renderAPI = sl::RenderAPI::eD3D12;
	// Streamline keeps process-global interposer state. DLSS-G must therefore
	// use a physically separate runtime directory from the D3D11 DLSS instance.
	streamlineDX12.pluginDir = L"Data\\Shaders\\ImageReconstruction\\StreamlineDX12";
	streamlineDX12.instanceTag = "DX12";
	streamlineDX12.LoadInterposer();
}

HANDLE ImageReconstruction::GetFrameLatencyWaitableObject() const
{
	return dx12SwapChain.GetFrameLatencyWaitableObject();
}

float ImageReconstruction::GetFrameTime() const
{
	return dx12SwapChain.GetFrameTime();
}

// Backend interface methods
bool ImageReconstruction::IsBackendInitialized() const
{
	return streamline.initialized;
}

void ImageReconstruction::CheckBackendFeatures(IDXGIAdapter* adapter)
{
	streamline.CheckFeatures(adapter);
	if (!streamline.neuralRenderingSupportedOnCurrentAdapter) {
		// Preserve ordinary DLSS availability, but never leave the experimental
		// Feature 18 master armed on an unsupported vendor/generation.
		settings.neuralRenderingEnabled = false;
	}
}

void ImageReconstruction::UpgradeBackendInterface(void** ppInterface)
{
	if (streamline.initialized && streamline.slUpgradeInterface && ppInterface)
		streamline.slUpgradeInterface(ppInterface);
}

void ImageReconstruction::SetBackendD3DDevice(ID3D11Device* device)
{
	if (streamline.initialized && streamline.slSetD3DDevice && device)
		streamline.slSetD3DDevice(device);
}

void ImageReconstruction::PostBackendDevice()
{
	streamline.PostDevice();
}

// Module availability methods
bool ImageReconstruction::HasFrameGenModule() const
{
	return fidelityFX.featureFSR3FG;
}

bool ImageReconstruction::HasDLSSGModule() const
{
	return streamlineDX12.featureDLSSG;
}

bool ImageReconstruction::DrawFrameGenerationBackendSelector()
{
	// Installation hint only, not a capability check. Do not load third-party DLLs
	// just to populate the menu. A restart is required after installing a proxy.
	static const bool proxyFilesPresent = [] {
		wchar_t executable[32768]{};
		const auto length = GetModuleFileNameW(nullptr, executable, _countof(executable));
		if (!length || length >= _countof(executable))
			return false;
		const auto directory = std::filesystem::path(executable).parent_path();
		std::error_code error;
		if (!std::filesystem::is_regular_file(directory / L"dlssg_sm86.ini", error))
			return false;
		for (const auto* name : { L"version.dll", L"winmm.dll", L"dinput8.dll", L"winhttp.dll", L"dxgi.dll" }) {
			error.clear();
			if (std::filesystem::is_regular_file(directory / name, error))
				return true;
		}
		return false;
	}();
	const bool available = HasDLSSGModule() || proxyFilesPresent;
	const char* labels[] = { "FSR 3 Frame Generation", "DLSSG (optional mod)" };
	const auto selected = std::min<uint>(settings.frameGenerationBackend, 1u);
	bool changed = false;
	if (ImGui::BeginCombo("Frame generation backend", labels[selected])) {
		for (uint index = 0; index < 2; ++index) {
			ImGui::BeginDisabled(index == 1 && !available);
			if (ImGui::Selectable(labels[index], selected == index)) {
				settings.frameGenerationBackend = index;
				changed = true;
			}
			ImGui::EndDisabled();
		}
		ImGui::EndCombo();
	}
	if (!available) {
		ImGui::TextWrapped("DLSSG unavailable: optional mod not detected. Use FSR 3, or install DLSSG separately and restart Skyrim. Not bundled with PIXL.");
	} else if (!HasDLSSGModule()) {
		ImGui::TextWrapped("DLSSG files detected; runtime support is not yet confirmed. Select DLSSG and restart to check compatibility.");
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
		ImGui::TextUnformatted("Optional SM86 mod: github.com/sdli1995/dlssg_for_sm86\nFollow its installation instructions beside SkyrimSE.exe, not in Data. Do not overwrite an existing proxy DLL. See DLSSG_SM86_INTEGRATION.md in the PIXL package. File detection does not guarantee GPU/runtime compatibility.");
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	return changed;
}

bool ImageReconstruction::UsesDLSSGFrameGeneration() const
{
	return settings.frameGenerationBackend == static_cast<uint>(Settings::FrameGenerationBackend::kDLSSG);
}

// Proxy interface methods
void ImageReconstruction::SetProxyD3D11Device(ID3D11Device* device)
{
	dx12SwapChain.SetD3D11Device(device);
}

void ImageReconstruction::SetProxyD3D11DeviceContext(ID3D11DeviceContext* context)
{
	dx12SwapChain.SetD3D11DeviceContext(context);
}

void ImageReconstruction::CreateProxySwapChain(
	IDXGIAdapter* adapter,
	DXGI_SWAP_CHAIN_DESC swapChainDesc,
	DX12SwapChain::Presenter presenter)
{
	dx12SwapChain.CreateSwapChain(adapter, swapChainDesc, presenter);
}

void ImageReconstruction::CreateProxyInterop()
{
	dx12SwapChain.CreateInterop();
}

IDXGISwapChain* ImageReconstruction::GetProxySwapChain()
{
	return dx12SwapChain.GetSwapChainProxy();
}

ImageReconstruction::BlurResources ImageReconstruction::GetBlurResources() const
{
	if (d3d12SwapChainActive) {
		return dx12SwapChain.GetBlurResources();
	}
	return {};
}

void ImageReconstruction::Upscale()
{
	ZoneScoped;
	auto upscaleMethod = GetUpscaleMethod();

	auto state = globals::state;
	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto deferred = globals::deferred;
	if (!state || !context || !renderer || !deferred || !globals::profiler ||
		!globals::game::graphicsState || !upscalingDataCB ||
		!reactiveMaskTexture || !transparencyCompositionMaskTexture) {
		return;
	}

	// Temporal reconstruction must not reuse history across discontinuous camera,
	// projection, or render-resolution state. The same signals already protect
	// PIXL volumetrics; consume them here before either reconstruction backend.
	const auto& frameBuffer = globals::game::frameBufferCached;
	const auto& cameraPos = frameBuffer.GetCameraPosAdjust();
	const auto& previousCameraPos = frameBuffer.GetCameraPreviousPosAdjust();
	const float dx = cameraPos.x - previousCameraPos.x;
	const float dy = cameraPos.y - previousCameraPos.y;
	const float dz = cameraPos.z - previousCameraPos.z;
	constexpr float kCameraCutDistance = 4096.0f;
	const bool cameraCut = dx * dx + dy * dy + dz * dz > kCameraCutDistance * kCameraCutDistance;
	const auto& dynamicResolution = frameBuffer.GetDynamicResolutionParams1();
	const bool dynamicResolutionChanged =
		std::abs(dynamicResolution.x - dynamicResolution.z) > 0.01f ||
		std::abs(dynamicResolution.y - dynamicResolution.w) > 0.01f;
	const float currentFov = Util::GetVerticalFOVRad();
	const bool fovChanged = hasPreviousReconstructionFov &&
		std::abs(currentFov - previousReconstructionFov) > 1e-4f;
	previousReconstructionFov = currentFov;
	hasPreviousReconstructionFov = true;

	if (cameraCut || dynamicResolutionChanged || fovChanged) {
		pendingDLSSReset.store(true, std::memory_order_release);
		pendingNeuralRenderingReset.store(true, std::memory_order_release);
		FidelityFX::needsReset.store(true, std::memory_order_release);
		if (cameraCut)
			globals::pipeline::hybridGI.queuedResetHistory.store(true, std::memory_order_release);
		else
			globals::pipeline::hybridGI.queuedResetTemporalHistory.store(true, std::memory_order_release);
	}
	const bool resetReconstructionHistory = pendingDLSSReset.exchange(false, std::memory_order_acq_rel);

	context->OMSetRenderTargets(0, nullptr, nullptr);  // Unbind all bound render targets

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	if (!main.texture || !motionVector.SRV) {
		return;
	}
	auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
	auto& normals = renderer->GetRuntimeData().renderTargets[deferred->forwardRenderTargets[2]];
	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto* encodeShader = GetEncodeTexturesCS();
	if (!temporalAAMask.SRV || !normals.SRV || !depth.depthSRV || !encodeShader)
		return;
	if (upscaleMethod == UpscaleMethod::kDLSS && (!motionVectorCopyTexture || !motionVectorCopyTexture->uav))
		return;
	if (upscaleMethod == UpscaleMethod::kFSR && (!fsrDepthTexture || !fsrDepthTexture->uav))
		return;

	{
		globals::profiler->BeginPass("ImageReconstruction::EncodeTextures");
		state->BeginPerfEvent("Encode ImageReconstruction Textures");
		TracyD3D11Zone(globals::state->tracyCtx, "Encode ImageReconstruction Textures");

		auto renderSize = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
		uint32_t renderWidth = (uint32_t)renderSize.x;
		uint32_t renderHeight = (uint32_t)renderSize.y;

		ID3D11ShaderResourceView* views[4] = { temporalAAMask.SRV, normals.SRV, motionVector.SRV, depth.depthSRV };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);
		context->CSSetShader(encodeShader, nullptr, 0);

		UpscalingDataCB upscalingData;
		upscalingData.trueSamplingDim = float2((float)renderWidth, (float)renderHeight);
		upscalingDataCB->Update(upscalingData);
		auto upscalingBuffer = upscalingDataCB->CB();
		context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

		// u2 (MotionVectorOutput): DLSS only — 5x5 dilated MVec for ghosting reduction.
		ID3D11UnorderedAccessView* uavs[4] = {
			reactiveMaskTexture->uav.get(),
			transparencyCompositionMaskTexture->uav.get(),
			(upscaleMethod == UpscaleMethod::kDLSS) ? motionVectorCopyTexture->uav.get() : nullptr,
			(upscaleMethod == UpscaleMethod::kFSR && fsrDepthTexture) ? fsrDepthTexture->uav.get() : nullptr
		};
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);

		ID3D11ShaderResourceView* nullViews[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

		ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}

	{
		globals::profiler->BeginPass("ImageReconstruction::Upscale");
		state->BeginPerfEvent("ImageReconstruction");
		TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction Dispatch");

		if (upscaleMethod == UpscaleMethod::kDLSS) {
			streamline.Upscale(main.texture, reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVectorCopyTexture->resource.get(), resetReconstructionHistory);
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			fidelityFX.Upscale(main.texture, fsrDepthTexture->resource.get(), reactiveMaskTexture->resource.get(), transparencyCompositionMaskTexture->resource.get(), motionVector.texture, settings.sharpnessFSR, resetReconstructionHistory);
		}

		state->EndPerfEvent();
		globals::profiler->EndPass();
	}
}

void ImageReconstruction::PerformUpscaling()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction");
	Upscale();
	UpscaleDepth();

	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();

	// Disable dynamic resolution past this point
	runtimeData.dynamicResolutionLock = 1;

	// Updates the PerFrame constant buffer so that dynamic resolution settings are disabled
	UpdateCameraData();
}

void ImageReconstruction::UpscaleDepth()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction - Depth");
	// Optimization overview:
	// 1) Early validation exits before issuing GPU work.
	// 2) Wide-kernel depth mode uses hysteresis to avoid frequent toggles.
	// 3) Resource copies are skipped for aliased src/dst to reduce copy churn.

	// (1) Early validation exits
	if (!IsUpscalingActive()) {
		return;
	}

	auto state = globals::state;
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto deferred = globals::deferred;
	if (!state || !renderer || !context || !deferred || !deferred->linearSampler || !jitterCB || !upscaleRasterizerState || !upscaleBlendState || !upscaleDepthStencilState) {
		return;
	}

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}

	auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];
	auto& depthCopy = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY];
	auto& refractionNormals = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kREFRACTION_NORMALS];
	auto& saoCameraZ = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSAO_CAMERAZ];
	auto& underwaterMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kUNDERWATER_MASK];

	if (!depth.texture || !depth.views[0] || !depthCopy.texture || !depthCopy.depthSRV ||
		!refractionNormals.texture || !refractionNormals.textureCopy || !refractionNormals.SRVCopy || !refractionNormals.RTV || !saoCameraZ.RTV ||
		!underwaterMask.texture || !underwaterMask.textureCopy || !underwaterMask.SRVCopy || !underwaterMask.RTV) {
		return;
	}
	auto* fullscreenVS = GetUpscaleVS();
	auto* depthUpscalePS = GetDepthRefractionUpscalePS();
	auto* underwaterMaskPS = GetUnderwaterMaskUpscalePS();
	if (!fullscreenVS || !depthUpscalePS || !underwaterMaskPS) {
		return;
	}

	state->BeginPerfEvent("Render Target ImageReconstruction");

	// Set up Input Assembler for fullscreen triangle (no vertex/index buffers needed)
	context->IASetInputLayout(nullptr);
	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set up vertex shader that generates fullscreen triangle using SV_VertexID
	context->VSSetShader(fullscreenVS, nullptr, 0);

	// Set up viewport for fullscreen rendering
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = screenSize.x;
	viewport.Height = screenSize.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	// Set rasterizer and blend state
	context->RSSetState(upscaleRasterizerState.get());
	context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

	ID3D11SamplerState* samplers[] = { deferred->linearSampler };
	context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// Set up jitter/depth-kernel constant buffer for imageReconstruction
	JitterCB jitterData;
	jitterData.jitter = jitter;
	// (2) Wide-kernel hysteresis
	{
		constexpr float kEnterWideKernelRatio = 1.55f;
		constexpr float kExitWideKernelRatio = 1.45f;
		const float minScale = std::max(std::min(resolutionScale.x, resolutionScale.y), FLT_EPSILON);
		const float upscaleRatio = 1.0f / minScale;

		if (depthUpscaleUseWideKernel) {
			if (upscaleRatio < kExitWideKernelRatio) {
				depthUpscaleUseWideKernel = false;
			}
		} else {
			if (upscaleRatio > kEnterWideKernelRatio) {
				depthUpscaleUseWideKernel = true;
			}
		}

		jitterData.useWideKernel = depthUpscaleUseWideKernel ? 1.0f : 0.0f;
		jitterData.pad0 = 0.0f;
	}

	jitterCB->Update(jitterData);
	auto bufferArray = jitterCB->CB();
	context->PSSetConstantBuffers(0, 1, &bufferArray);

	// (3) Skip aliased copies
	const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
		if (dst && src && dst != src) {
			context->CopyResource(dst, src);
		}
	};

	{
		TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction - Depth Upscale");

		// Sometimes this is not already copied e.g. map menu.
		// Skip alias copies to reduce unnecessary copy churn.
		copyIfNonAliased(depthCopy.texture, depth.texture);

		// Set depth stencil state to write 0x00
		context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

		copyIfNonAliased(refractionNormals.textureCopy, refractionNormals.texture);

		ID3D11ShaderResourceView* srvs[] = { refractionNormals.SRVCopy, depthCopy.depthSRV, depthCopy.stencilSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { refractionNormals.RTV, saoCameraZ.RTV };
		context->OMSetRenderTargets(2, rtvs, depth.views[0]);

		context->PSSetShader(depthUpscalePS, nullptr, 0);
		globals::profiler->BeginPass("ImageReconstruction::DepthUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	{
		TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction - Underwater Mask");

		viewport.Width = screenSize.x * 0.5f;
		viewport.Height = screenSize.y * 0.5f;
		context->RSSetViewports(1, &viewport);

		copyIfNonAliased(underwaterMask.textureCopy, underwaterMask.texture);

		context->OMSetDepthStencilState(nullptr, 0x00);

		// t0: vanilla mask copy, t1: original depth.
		ID3D11ShaderResourceView* srvs[] = { underwaterMask.SRVCopy, depthCopy.depthSRV };
		context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11RenderTargetView* rtvs[] = { underwaterMask.RTV };
		context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, nullptr);

		context->PSSetShader(underwaterMaskPS, nullptr, 0);
		globals::profiler->BeginPass("ImageReconstruction::UnderwaterMaskUpscale");
		context->Draw(3, 0);
		globals::profiler->EndPass();
	}

	ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
	context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

	state->EndPerfEvent();
}

void ImageReconstruction::ApplySharpening()
{
	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "ImageReconstruction - Sharpening");

	if (!sharpenerTexture)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (!main.texture)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	bool sharpened = false;
	if (settings.sharpnessEnabledDLSS && settings.sharpnessDLSS > 0.0f && main.UAV) {
		// Match FSR3's slider->RCAS conversion exactly (ffx_fsr3upscaler.cpp + FsrRcasCon):
		//   sharpenessRemapped = -2*slider + 2   (sharpness in stops)
		//   rcasAttenuation    = exp2(-sharpenessRemapped) = exp2(2*slider - 2)
		float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
		currentSharpness = exp2(-currentSharpness);

		// DLSS has already written to sharpenerTexture; sharpen directly into kMAIN.UAV.
		const float2 outputDimensions{
			static_cast<float>(globals::game::graphicsState->screenWidth),
			static_cast<float>(globals::game::graphicsState->screenHeight)
		};
		const float2 inputDimensions = Util::ConvertToDynamic(outputDimensions);
		sharpened = rcas.ApplySharpen(
			sharpenerTexture->srv.get(),
			main.UAV,
			currentSharpness,
			reactiveMaskTexture ? reactiveMaskTexture->srv.get() : nullptr,
			transparencyCompositionMaskTexture ? transparencyCompositionMaskTexture->srv.get() : nullptr,
			motionVectorCopyTexture ? motionVectorCopyTexture->srv.get() : nullptr,
			inputDimensions);
	}
	if (!sharpened) {
		// Also resolve when optional RCAS resources are unavailable, so a shader
		// compile failure cannot discard the current reconstructed frame.
		context->CopyResource(main.texture, sharpenerTexture->resource.get());
	}

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void ImageReconstruction::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
{
	globals::pipeline::imageReconstruction.ConfigureTAA();
	func(a_state);
	globals::pipeline::imageReconstruction.ConfigureUpscaling(a_state);
}

void ImageReconstruction::MenuManagerDrawInterfaceStartHook::thunk(int64_t a1)
{
	globals::pipeline::imageReconstruction.PostDisplay();

	// For non-Frame Gen HDR: redirect kFRAMEBUFFER.RTV to UI texture before vanilla UI renders
	// When FG is active, its SetUIBuffer redirects to uiBufferWrapped instead
	// When Camera Suite is not loaded, skip entirely so vanilla UI renders to kFRAMEBUFFER
	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	if (!imageReconstruction.d3d12SwapChainActive && globals::pipeline::cameraSuite.loaded) {
		globals::pipeline::cameraSuite.SetUIBuffer();
	}

	func(a1);
}

void ImageReconstruction::Main_PostProcessing::thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5)
{
	auto& imageReconstruction = globals::pipeline::imageReconstruction;
	auto upscaleMethod = imageReconstruction.GetUpscaleMethod();

	const bool useFrameGeneration = imageReconstruction.ShouldUseFrameGenerationThisFrame();
	const bool useNeuralRendering = imageReconstruction.ShouldUseNeuralRenderingThisFrame();
	if (useFrameGeneration)
		imageReconstruction.CopySharedD3D12Resources(false);

	if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
		imageReconstruction.PerformUpscaling();

	// NR consumes PIXL's encoded/dilated DLSS motion guide. Copy it only after
	// EncodeTexturesCS has produced the current frame, while preserving the raw
	// pre-upscale guide path required by frame generation.
	if (useNeuralRendering)
		imageReconstruction.CopySharedD3D12Resources(true);

	if (upscaleMethod == UpscaleMethod::kDLSS)
		imageReconstruction.ApplySharpening();

	Util::SetTemporal(upscaleMethod == UpscaleMethod::kTAA);

	// Redirect kFRAMEBUFFER to float texture before ISHDR runs so HDR values >1.0 survive
	// When Camera Suite is not loaded, ISHDR writes to vanilla kFRAMEBUFFER (SDR path)
	bool hdrLoaded = globals::pipeline::cameraSuite.loaded;
	if (hdrLoaded)
		globals::pipeline::cameraSuite.RedirectFramebuffer();

	func(a_this, a3, a_target, a_4, a_5);

	// Restore kFRAMEBUFFER after ISHDR — hdrTexture now has the HDR scene
	if (hdrLoaded)
		globals::pipeline::cameraSuite.RestoreFramebuffer();

	Util::SetTemporal(false);
}

void ImageReconstruction::SetScissorRect::thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom)
{
	auto viewport = globals::game::graphicsState;
	auto& runtimeData = viewport->GetRuntimeData();

	if (!runtimeData.dynamicResolutionLock) {
		a_left = static_cast<int>(a_left * runtimeData.dynamicResolutionWidthRatio);
		a_right = static_cast<int>(a_right * runtimeData.dynamicResolutionWidthRatio);

		a_top = static_cast<int>(a_top * runtimeData.dynamicResolutionHeightRatio);
		a_bottom = static_cast<int>(a_bottom * runtimeData.dynamicResolutionHeightRatio);
	}

	func(This, a_left, a_top, a_right, a_bottom);
}

void ImageReconstruction::Main_RenderPrecipitation::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}

void ImageReconstruction::BSFaceGenManager_UpdatePendingCustomizationTextures::thunk()
{
	auto& runtimeData = globals::game::graphicsState->GetRuntimeData();
	runtimeData.dynamicResolutionLock = 1;
	func();
	runtimeData.dynamicResolutionLock = 0;
}
