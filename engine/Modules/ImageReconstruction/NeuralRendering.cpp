#include "NeuralRendering.h"

#include "Util.h"

#include <Windows.h>
#include <Psapi.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

#include <array>
#include <cstring>
#include <format>

namespace
{
	constexpr wchar_t kRuntimeName[] = L"nvngx_dlssnr.dll";
	constexpr auto kFeatureDLSSNR = static_cast<NVSDK_NGX_Feature>(18);
	constexpr std::array<const char*, 5> kRequiredExports{
		"NVSDK_NGX_D3D12_Init_Ext",
		"NVSDK_NGX_D3D12_CreateFeature",
		"NVSDK_NGX_D3D12_EvaluateFeature",
		"NVSDK_NGX_D3D12_ReleaseFeature",
		"NVSDK_NGX_D3D12_Shutdown1",
	};

	using GetUnsignedValue = unsigned int(NVSDK_CONV*)();
	using InitD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
	using ShutdownD3D12 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
	using AllocateParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
	using DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
	using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
	using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
	using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
	using GetModuleFileNameWFunction = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

	GetModuleFileNameWFunction originalGetModuleFileNameW = nullptr;
	HMODULE callerModule = nullptr;
	std::wstring signedRuntimePath;
	std::uint32_t signedPathHits = 0;

	HMODULE FindNGXCoreModule()
	{
		std::array<HMODULE, 1024> modules{};
		DWORD bytesNeeded = 0;
		if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &bytesNeeded))
			return nullptr;

		const std::size_t count = std::min<std::size_t>(modules.size(), bytesNeeded / sizeof(HMODULE));
		for (std::size_t i = 0; i < count; ++i) {
			if (GetProcAddress(modules[i], "NVSDK_NGX_D3D12_AllocateParameters") &&
				GetProcAddress(modules[i], "NVSDK_NGX_D3D12_DestroyParameters"))
				return modules[i];
		}
		return nullptr;
	}

	DWORD WINAPI SignedRuntimeGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
	{
		if (module == callerModule && filename && size > 0 && !signedRuntimePath.empty()) {
			++signedPathHits;
			const DWORD length = static_cast<DWORD>(signedRuntimePath.size());
			const DWORD copyLength = std::min(length, size - 1);
			std::memcpy(filename, signedRuntimePath.data(), copyLength * sizeof(wchar_t));
			filename[copyLength] = L'\0';
			return copyLength < length ? size : length;
		}
		return originalGetModuleFileNameW ? originalGetModuleFileNameW(module, filename, size) : 0;
	}

	// DLSSNR 310.8 asks Windows for its own module path during NGX validation,
	// but expects the signed NGX core identity.  Patch only this runtime's import
	// slot for the duration of the vendor call and restore it immediately.  If the
	// scoped patch cannot be installed, PIXL disables the optional feature.
	class SignedRuntimePathScope
	{
	public:
		SignedRuntimePathScope(HMODULE runtime, const std::filesystem::path& coreIdentity)
		{
			if (!runtime)
				return;
			GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SignedRuntimeGetModuleFileNameW), &callerModule);
			signedRuntimePath = coreIdentity.wstring();
			signedPathHits = 0;

			auto* base = reinterpret_cast<std::byte*>(runtime);
			auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
				return;
			auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE)
				return;
			const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!imports.VirtualAddress)
				return;

			auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
			for (; descriptor->Name; ++descriptor) {
				if (!descriptor->OriginalFirstThunk)
					continue;
				auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
				auto* functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++functions) {
					if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
						continue;
					auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
					if (std::strcmp(reinterpret_cast<const char*>(import->Name), "GetModuleFileNameW") != 0)
						continue;
					slot_ = reinterpret_cast<void**>(&functions->u1.Function);
					originalGetModuleFileNameW = reinterpret_cast<GetModuleFileNameWFunction>(*slot_);
					DWORD oldProtection = 0;
					if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &oldProtection)) {
						*slot_ = reinterpret_cast<void*>(&SignedRuntimeGetModuleFileNameW);
						DWORD ignored = 0;
						VirtualProtect(slot_, sizeof(*slot_), oldProtection, &ignored);
						FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(*slot_));
						installed_ = true;
					}
					return;
				}
			}
		}

		~SignedRuntimePathScope()
		{
			if (installed_ && slot_) {
				DWORD oldProtection = 0;
				if (VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &oldProtection)) {
					*slot_ = reinterpret_cast<void*>(originalGetModuleFileNameW);
					DWORD ignored = 0;
					VirtualProtect(slot_, sizeof(*slot_), oldProtection, &ignored);
					FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(*slot_));
				}
			}
			signedRuntimePath.clear();
			callerModule = nullptr;
			originalGetModuleFileNameW = nullptr;
		}

		[[nodiscard]] bool IsInstalled() const { return installed_; }
		[[nodiscard]] std::uint32_t Hits() const { return signedPathHits; }

	private:
		void** slot_ = nullptr;
		bool installed_ = false;
	};

	std::filesystem::path ResolveRuntimePath(const std::filesystem::path& explicitPath)
	{
		if (!explicitPath.empty())
			return std::filesystem::is_directory(explicitPath) ? explicitPath / kRuntimeName : explicitPath;

		const auto dataPath = Util::PathHelpers::GetDataPath();
		const std::array candidates{
			dataPath / L"Shaders/ImageReconstruction/Streamline" / kRuntimeName,
			dataPath / L"Shaders/Upscaling/Streamline" / kRuntimeName,
			dataPath / L"Shaders/ImageReconstruction/StreamlineDX12" / kRuntimeName,
		};
		for (const auto& candidate : candidates) {
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate, error))
				return candidate;
		}
		return {};
	}
}

NeuralRendering::~NeuralRendering()
{
	Shutdown();
}

bool NeuralRendering::Probe(const std::filesystem::path& explicitPath)
{
	Shutdown();
	runtimePath_ = ResolveRuntimePath(explicitPath);
	if (runtimePath_.empty()) {
		status_ = Status::NotFound;
		detail_ = "nvngx_dlssnr.dll was not found";
		return false;
	}

	const auto version = Util::GetDllVersion(runtimePath_.wstring());
	if (!version) {
		status_ = Status::VersionUnavailable;
		detail_ = "runtime version resource is unavailable";
		return false;
	}
	version_ = Util::GetFormattedVersion(*version);
	// Feature 18 and its string parameter ABI are private and versioned.  Accept
	// only the runtime this contract was validated against rather than guessing.
	if (version->major() != 310 || version->minor() != 8) {
		status_ = Status::UnsupportedVersion;
		detail_ = std::format("validated runtime is DLSSNR 310.8.x; found {}", version_);
		return false;
	}

	module_ = LoadLibraryExW(runtimePath_.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!module_) {
		status_ = Status::LoadFailed;
		detail_ = std::format("LoadLibraryExW failed with {}", GetLastError());
		return false;
	}

	for (const char* exportName : kRequiredExports) {
		if (!GetProcAddress(static_cast<HMODULE>(module_), exportName)) {
			status_ = Status::MissingExport;
			detail_ = std::format("missing export {}", exportName);
			FreeLibrary(static_cast<HMODULE>(module_));
			module_ = nullptr;
			return false;
		}
	}

	auto getApplicationId = reinterpret_cast<GetUnsignedValue>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_GetApplicationId"));
	auto getApiVersion = reinterpret_cast<GetUnsignedValue>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_GetAPIVersion"));
	if (!getApplicationId || !getApiVersion) {
		status_ = Status::MissingExport;
		detail_ = "signed runtime identity exports are missing";
		FreeLibrary(static_cast<HMODULE>(module_));
		module_ = nullptr;
		return false;
	}

	applicationId_ = getApplicationId();
	apiVersion_ = getApiVersion();
	status_ = Status::Ready;
	detail_ = "runtime validated; waiting for PIXL DX12 sidecar";
	return true;
}

bool NeuralRendering::Initialize(ID3D12Device* device)
{
	if (!device)
		return false;
	if (status_ == Status::Initialized && device_ == device)
		return true;
	if (status_ == Status::Initialized && device_ != device)
		Shutdown();
	if (!module_ && !Probe())
		return false;

	auto initialize = reinterpret_cast<InitD3D12>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_Init_Ext"));
	const auto writablePath = Util::PathHelpers::GetDataPath() / L"SKSE/Plugins/PIXLRenderer/NGXCache";
	std::error_code error;
	std::filesystem::create_directories(writablePath, error);

	SignedRuntimePathScope scope(static_cast<HMODULE>(module_), runtimePath_.parent_path() / L"nvngx.dll");
	if (!scope.IsInstalled()) {
		status_ = Status::InitializationFailed;
		detail_ = "could not establish the scoped signed-runtime identity";
		return false;
	}

	ngxResult_ = static_cast<std::uint32_t>(initialize(
		applicationId_, writablePath.c_str(), device, static_cast<NVSDK_NGX_Version>(apiVersion_), nullptr));
	if (ngxResult_ != NVSDK_NGX_Result_Success) {
		status_ = Status::InitializationFailed;
		detail_ = std::format("NGX initialization failed 0x{:08X}; identityHits={}", ngxResult_, scope.Hits());
		return false;
	}

	device_ = device;
	device_->AddRef();
	HMODULE core = FindNGXCoreModule();
	if (!core) {
		status_ = Status::CoreUnavailable;
		detail_ = "NGX parameter allocator was not found in the loaded Streamline core";
		return false;
	}
	auto allocate = reinterpret_cast<AllocateParameters>(
		GetProcAddress(core, "NVSDK_NGX_D3D12_AllocateParameters"));
	NVSDK_NGX_Parameter* parameters = nullptr;
	ngxResult_ = static_cast<std::uint32_t>(allocate(&parameters));
	if (ngxResult_ != NVSDK_NGX_Result_Success || !parameters) {
		status_ = Status::ParameterAllocationFailed;
		detail_ = std::format("NGX parameter allocation failed 0x{:08X}", ngxResult_);
		return false;
	}

	parameters_ = parameters;
	status_ = Status::Initialized;
	detail_ = "PIXL DX12 sidecar integration initialized";
	logger::info("[NeuralRendering] Initialized DLSSNR {} through PIXL DX12 sidecar", version_);
	return true;
}

bool NeuralRendering::Evaluate(
	ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* color,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	ID3D12Resource* output,
	std::uint32_t guideWidth,
	std::uint32_t guideHeight,
	std::uint32_t outputWidth,
	std::uint32_t outputHeight,
	float motionVectorScaleX,
	float motionVectorScaleY,
	const Tuning& tuning,
	bool reset,
	bool latchFailure)
{
	if (status_ == Status::RuntimeFault || status_ != Status::Initialized || !commandList || !color || !depth ||
		!motionVectors || !output || guideWidth <= 1 || guideHeight <= 1 || !outputWidth || !outputHeight)
		return false;

	auto* parameters = static_cast<NVSDK_NGX_Parameter*>(parameters_);
	auto create = reinterpret_cast<CreateFeature>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_CreateFeature"));
	auto evaluate = reinterpret_cast<EvaluateFeature>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_EvaluateFeature"));
	auto release = reinterpret_cast<ReleaseFeature>(
		GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_ReleaseFeature"));

	SignedRuntimePathScope scope(static_cast<HMODULE>(module_), runtimePath_.parent_path() / L"nvngx.dll");
	if (!scope.IsInstalled()) {
		LatchRuntimeFault("scoped signed-runtime identity was unavailable during evaluation");
		return false;
	}

	const bool dimensionsChanged = featureGuideWidth_ != guideWidth || featureGuideHeight_ != guideHeight ||
		featureOutputWidth_ != outputWidth || featureOutputHeight_ != outputHeight;
	if (featureHandle_ && dimensionsChanged) {
		release(static_cast<NVSDK_NGX_Handle*>(featureHandle_));
		featureHandle_ = nullptr;
	}

	if (!featureHandle_) {
		parameters->Reset();
		// Feature 18 follows the same two-extent convention as the native DLSS
		// evaluation that produced Color: Width/Height describe the render-resolution
		// guide domain, while OutWidth/OutHeight describe the display-resolution
		// neural output.  Publishing display dimensions for both domains is harmless
		// in DLAA, but gives the runtime a contradictory contract in scaled DLSS and
		// produces spatially offset/"shadowed" detail.
		parameters->Set("Width", guideWidth);
		parameters->Set("Height", guideHeight);
		parameters->Set("OutWidth", outputWidth);
		parameters->Set("OutHeight", outputHeight);
		parameters->Set("DLSSNR.Width", guideWidth);
		parameters->Set("DLSSNR.Height", guideHeight);
		parameters->Set("DLSSNR.InputWidth", guideWidth);
		parameters->Set("DLSSNR.InputHeight", guideHeight);
		parameters->Set("DLSSNR.OutputWidth", outputWidth);
		parameters->Set("DLSSNR.OutputHeight", outputHeight);
		parameters->Set("DLSSNR.Output.Width", outputWidth);
		parameters->Set("DLSSNR.Output.Height", outputHeight);
		parameters->Set("DLSSNR.Scale", static_cast<float>(outputWidth) / static_cast<float>(guideWidth));
		parameters->Set("DLSSNR.Upscaling", 1u);
		parameters->Set("DLSSNR.ScalingRatio", static_cast<float>(outputWidth) / static_cast<float>(guideWidth));
		parameters->Set("PerfQualityValue", tuning.performanceQuality);
		parameters->Set("DLSSNR.Hint.Render.Preset", tuning.outputPreset);

		NVSDK_NGX_Handle* handle = nullptr;
		ngxResult_ = static_cast<std::uint32_t>(create(commandList, kFeatureDLSSNR, parameters, &handle));
		if (ngxResult_ != NVSDK_NGX_Result_Success || !handle) {
			LatchRuntimeFault(std::format("Feature 18 creation failed 0x{:08X}; identityHits={}", ngxResult_, scope.Hits()), ngxResult_);
			return false;
		}
		featureHandle_ = handle;
		featureGuideWidth_ = guideWidth;
		featureGuideHeight_ = guideHeight;
		featureOutputWidth_ = outputWidth;
		featureOutputHeight_ = outputHeight;
		logger::info(
			"[NeuralRendering] Evaluation contract: guides={}x{}, color={}x{}, output={}x{}, scale={:.3f}x{:.3f}, performance={}, preset={}, motion=normalized render UV",
			guideWidth,
			guideHeight,
			outputWidth,
			outputHeight,
			outputWidth,
			outputHeight,
			static_cast<float>(outputWidth) / static_cast<float>(guideWidth),
			static_cast<float>(outputHeight) / static_cast<float>(guideHeight),
			tuning.performanceQuality,
			tuning.outputPreset);
		reset = true;
	}

	parameters->Reset();
	parameters->Set("DLSSNR.Color", color);
	parameters->Set("DLSSNR.Depth", depth);
	parameters->Set("DLSSNR.MVec", motionVectors);
	parameters->Set("DLSSNR.Output", output);
	parameters->Set("DLSSNR.ColorSubrectBaseX", 0u);
	parameters->Set("DLSSNR.ColorSubrectBaseY", 0u);
	parameters->Set("DLSSNR.ColorSubrectWidth", outputWidth);
	parameters->Set("DLSSNR.ColorSubrectHeight", outputHeight);
	parameters->Set("DLSSNR.DepthSubrectBaseX", 0u);
	parameters->Set("DLSSNR.DepthSubrectBaseY", 0u);
	parameters->Set("DLSSNR.DepthSubrectWidth", guideWidth);
	parameters->Set("DLSSNR.DepthSubrectHeight", guideHeight);
	parameters->Set("DLSSNR.MVecSubrectBaseX", 0u);
	parameters->Set("DLSSNR.MVecSubrectBaseY", 0u);
	parameters->Set("DLSSNR.MVecSubrectWidth", guideWidth);
	parameters->Set("DLSSNR.MVecSubrectHeight", guideHeight);
	parameters->Set("DLSSNR.OutputSubrectBaseX", 0u);
	parameters->Set("DLSSNR.OutputSubrectBaseY", 0u);
	parameters->Set("DLSSNR.OutputSubrectWidth", outputWidth);
	parameters->Set("DLSSNR.OutputSubrectHeight", outputHeight);
	parameters->Set("DLSSNR.MVecScaleX", motionVectorScaleX);
	parameters->Set("DLSSNR.MVecScaleY", motionVectorScaleY);
	parameters->Set("DLSSNR.DepthInverted", 0u);
	parameters->Set("DLSSNR.Enabled", 1u);
	parameters->Set("DLSSNR.Reset", reset ? 1u : 0u);
	parameters->Set("DLSSNR.Intensity", tuning.intensity);
	parameters->Set("DLSSNR.LocalToneStrength", tuning.localToneStrength);
	parameters->Set("DLSSNR.LocalStructureStrength", tuning.localStructureStrength);
	parameters->Set("DLSSNR.SkinStructureStrength", tuning.skinStructureStrength);
	parameters->Set("DLSSNR.UseAutoMask", tuning.useAutoMask ? 1u : 0u);
	parameters->Set("DLSSNR.Style", tuning.style);
	parameters->Set("DLSSNR.UICorrection", tuning.uiCorrection ? 1u : 0u);

	ngxResult_ = static_cast<std::uint32_t>(evaluate(
		commandList, static_cast<NVSDK_NGX_Handle*>(featureHandle_), parameters, nullptr));
	if (ngxResult_ != NVSDK_NGX_Result_Success) {
		if (latchFailure) {
			LatchRuntimeFault(std::format("Feature 18 evaluation failed 0x{:08X}", ngxResult_), ngxResult_);
		} else {
			detail_ = std::format(
				"Optional Photo Finish neural refinement pass failed 0x{:08X}; base neural output retained",
				ngxResult_);
			logger::warn("[NeuralRendering] {}", detail_);
		}
		return false;
	}

	++successfulFrames_;
	return true;
}

void NeuralRendering::ResetFeature()
{
	if (featureHandle_ && module_) {
		auto release = reinterpret_cast<ReleaseFeature>(
			GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_ReleaseFeature"));
		SignedRuntimePathScope scope(static_cast<HMODULE>(module_), runtimePath_.parent_path() / L"nvngx.dll");
		if (release && scope.IsInstalled()) {
			const auto result = release(static_cast<NVSDK_NGX_Handle*>(featureHandle_));
			if (result != NVSDK_NGX_Result_Success)
				logger::warn("[NeuralRendering] Feature release failed: 0x{:08X}", static_cast<std::uint32_t>(result));
		}
	}
	featureHandle_ = nullptr;
	featureGuideWidth_ = featureGuideHeight_ = 0;
	featureOutputWidth_ = featureOutputHeight_ = 0;
	successfulFrames_ = 0;
}

void NeuralRendering::Shutdown()
{
	if (device_ && module_) {
		ResetFeature();
		HMODULE core = FindNGXCoreModule();
		if (parameters_ && core) {
			auto destroy = reinterpret_cast<DestroyParameters>(
				GetProcAddress(core, "NVSDK_NGX_D3D12_DestroyParameters"));
			if (destroy)
				destroy(static_cast<NVSDK_NGX_Parameter*>(parameters_));
		}
		parameters_ = nullptr;

		auto shutdown = reinterpret_cast<ShutdownD3D12>(
			GetProcAddress(static_cast<HMODULE>(module_), "NVSDK_NGX_D3D12_Shutdown1"));
		SignedRuntimePathScope scope(static_cast<HMODULE>(module_), runtimePath_.parent_path() / L"nvngx.dll");
		if (shutdown && scope.IsInstalled())
			shutdown(device_);

		device_->Release();
		device_ = nullptr;
	}
	if (module_)
		FreeLibrary(static_cast<HMODULE>(module_));
	module_ = nullptr;
	parameters_ = nullptr;
	featureHandle_ = nullptr;
	status_ = Status::NotProbed;
	runtimePath_.clear();
	version_.clear();
	detail_.clear();
	ngxResult_ = applicationId_ = apiVersion_ = 0;
	featureGuideWidth_ = featureGuideHeight_ = 0;
	featureOutputWidth_ = featureOutputHeight_ = 0;
	successfulFrames_ = 0;
}

void NeuralRendering::LatchRuntimeFault(std::string detail, std::uint32_t result)
{
	status_ = Status::RuntimeFault;
	detail_ = std::move(detail);
	ngxResult_ = result;
	logger::error("[NeuralRendering] {}; normal DLSS output retained", detail_);
}

const char* NeuralRendering::GetStatusText() const
{
	switch (status_) {
	case Status::NotProbed: return "Not probed";
	case Status::NotFound: return "Runtime not found";
	case Status::VersionUnavailable: return "Version unavailable";
	case Status::UnsupportedVersion: return "Unsupported runtime version";
	case Status::LoadFailed: return "Runtime load failed";
	case Status::MissingExport: return "Required export missing";
	case Status::Ready: return "Runtime ready";
	case Status::InitializationFailed: return "Initialization failed";
	case Status::CoreUnavailable: return "NGX core unavailable";
	case Status::ParameterAllocationFailed: return "Parameter allocation failed";
	case Status::Initialized: return "Active";
	case Status::RuntimeFault: return "Runtime fault - bypassed";
	}
	return "Unknown";
}
