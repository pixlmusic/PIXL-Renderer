#pragma once

#include "RenderModule.h"
#include "ImageReconstruction/DX12SwapChain.h"
#include "ImageReconstruction/FidelityFX.h"
#include "ImageReconstruction/NeuralRendering.h"
#include "ImageReconstruction/RCAS/RCAS.h"
#include "ImageReconstruction/Streamline.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

/**
 * @brief Provides imageReconstruction functionality including DLSS, FSR and TAA.
 *
 * This feature handles various imageReconstruction methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct ImageReconstruction : RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "156952";

public:
	enum class FrameGenerationState : std::uint8_t
	{
		Off,
		Active,
		TemporarilySuspended,
		Starting,
		RestartRequired,
		Unavailable,
		RuntimeFault
	};

	// RenderModule interface
	virtual inline std::string GetName() override { return "ImageReconstruction"; }
	virtual std::string GetDisplayName() override { return T("feature.image_reconstruction.name", "Image Reconstruction"); }
	virtual inline std::string GetShortName() override { return "ImageReconstruction"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline bool IsCore() const override { return false; }
	virtual inline std::string_view GetCategory() const override { return ModuleGroups::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.image_reconstruction.description", "Advanced imageReconstruction and frame generation technologies for improved performance"),
			{ T("feature.image_reconstruction.key_feature_1", "DLSS (Deep Learning Super Sampling) support"),
				T("feature.image_reconstruction.key_feature_2", "FSR (FidelityFX Super Resolution) support"),
				T("feature.image_reconstruction.key_feature_3", "TAA (Temporal Anti-Aliasing) support"),
				T("feature.image_reconstruction.key_feature_4", "Frame generation for supported systems") } };
	};

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kFSR;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 2;  // Safe default: Balanced (1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA)
		uint frameLimitMode = 1;
		float frameLimitFPS = 60.0f;  // final presented FPS; FG schedules real frames at half this rate
		uint frameGenerationMode = 0;
		uint frameGenerationForceEnable = 0;
		bool frameGenerationAllowInMenus = false;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
		float sharpnessFSR = 0.0f;
		bool sharpnessEnabledDLSS = false;
		float sharpnessDLSS = 0.0f;
		uint presetDLSS = 0;  // 0=Default, 1=J, 2=K, 3=L, 4=M, 5=F
		bool forceLatestDLSSModelOnLegacyRTX = false;
		// Experimental DLSS Neural Rendering 310.8 / NGX Feature 18.  The
		// feature is capability-gated and bypasses to ordinary DLSS on failure.
		bool neuralRenderingEnabled = false;
		uint neuralRenderingPreset = 0;  // 0=Natural, 1=Balanced, 2=Detail, 3=Strong, 4=Custom
		// Feature-creation controls recovered from the installed 310.8 runtime.
		// Quality: 0=follow DLSS, 1=DLAA, 2=quality, 3=balanced,
		// 4=performance, 5=ultra performance, 6=ultra quality.
		uint neuralRenderingQualityMode = 0;
		// 0=runtime default, 1..3=private runtime output presets.
		uint neuralRenderingOutputPreset = 0;
		float neuralRenderingIntensity = 0.8f;
		float neuralRenderingLocalTone = 0.75f;
		float neuralRenderingLocalStructure = 0.9f;
		float neuralRenderingSkinStructure = 0.9f;
		uint neuralRenderingStyle = 3;
		bool neuralRenderingAutoMask = true;
		bool neuralRenderingUICorrection = true;
		bool reflexLowLatencyMode = false;
		bool reflexLowLatencyBoost = false;
		bool reflexUseMarkersToOptimize = false;
		bool reflexUseFPSLimit = false;
		float reflexFPSLimit = 60.0f;
	};

	Settings settings;

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	struct UpscalingDataCB
	{
		float2 trueSamplingDim;
		float2 pad0;
	};

	ConstantBuffer* jitterCB = nullptr;
	ConstantBuffer* upscalingDataCB = nullptr;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool fidelityFXMissing = false;
	bool d3d12SwapChainActive = false;
	bool frameGenerationRequestedAtBoot = false;
	bool neuralRenderingRequestedAtBoot = false;
	// Photo Finish can invoke Feature 18 without enabling it during gameplay.
	// Provisioning owns the DX12 sidecar and shared resources; the live setting
	// only decides whether the model runs continuously.
	bool neuralRenderingProvisionedAtBoot = false;
	uint neuralRenderingQualityModeAtBoot = 0;
	uint neuralRenderingOutputPresetAtBoot = 0;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationDx12PathActive() const;
	bool IsFrameGenerationActive() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	FrameGenerationState GetFrameGenerationState() const;
	bool IsFrameGenerationTemporarilySuspended() const;
	bool IsNeuralRenderingConfiguredForSession();
	bool ShouldUseNeuralRenderingThisFrame();
	bool CanUsePhotoNeuralRendering();
	[[nodiscard]] bool IsPhotoNeuralRenderingActive() const
	{
		return photoCaptureNeuralOverrideActive.load(std::memory_order_acquire);
	}
	[[nodiscard]] uint GetEffectiveQualityMode() const
	{
		return IsPhotoNeuralRenderingActive() ? 0u : settings.qualityMode;
	}
	float GetFrameGenerationFrameTime() const;
	bool IsUpscalingActive() const;

	// RenderModule interface overrides
	virtual void DrawSettings() override;
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	**/
	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;

	UpscaleMethod GetUpscaleMethod() const;

	void CheckResources(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[4];  // One for each UpscaleMethod (kNONE, kTAA, kFSR, kDLSS)
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	// Helper: Create a Texture2D matching source format at a given size
	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	void Upscale();

	// Offline Director supersampling. The normal temporal path already jitters,
	// but its phase count can repeat during a long frozen capture. Director pins
	// a deterministic low-discrepancy sequence so every requested sample receives
	// a distinct projection offset and the upscaler receives the matching jitter.
	void BeginPhotoCaptureJitter(std::uint32_t targetSamples);
	void SetPhotoCaptureJitterSample(std::uint32_t sampleIndex);
	void EndPhotoCaptureJitter();
	[[nodiscard]] bool IsPhotoCaptureJitterActive() const { return photoCaptureJitterActive; }
	void BeginPhotoCaptureRenderOverride(float minimumRenderScale, bool enableNeuralRendering);
	void EndPhotoCaptureRenderOverride();

	bool photoCaptureJitterActive = false;
	std::uint32_t photoCaptureJitterIndex = 0;
	std::uint32_t photoCaptureJitterPhaseCount = 32;
	bool photoCaptureRenderOverrideActive = false;
	float photoCaptureMinimumRenderScale = 0.0f;
	std::atomic_bool photoCaptureNeuralOverrideActive{ false };

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;
	Texture2D* motionVectorCopyTexture = nullptr;
	// FidelityFX's DX11 backend cannot infer sampled depth from Skyrim's
	// R24G8_TYPELESS resource. Encode raw device depth into a typed R32_FLOAT
	// texture so FSR receives the format and values it expects.
	Texture2D* fsrDepthTexture = nullptr;
	Texture2D* sharpenerTexture = nullptr;

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;  ///< Only for frame generation
	static inline DX12SwapChain dx12SwapChain;
	static inline NeuralRendering neuralRendering;
	static inline RCAS rcas;  ///< Standalone RCAS sharpening for DLSS

	winrt::com_ptr<ID3D11PixelShader> copyDepthToSharedBufferPS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousUpscalingWasActive = false;
	bool depthUpscaleUseWideKernel = false;
	float previousReconstructionFov = 0.0f;
	bool hasPreviousReconstructionFov = false;

	/**
	 * Set when loading/cell transitions or camera discontinuities invalidate temporal
	 * reconstruction. Consumed by the active DLSS/FSR backend on its next dispatch.
	 * The historical member name is retained because benchmark/runtime helpers already
	 * use it, but the request applies to either reconstruction backend.
	 */
	std::atomic<bool> pendingDLSSReset{ false };
	std::atomic<bool> pendingNeuralRenderingReset{ true };

	void CopySharedD3D12Resources(bool a_useNeuralGuides = false);
	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();

	/**
	 * @brief Applies RCAS sharpening to the main render target after DLSS imageReconstruction.
	 *
	 * Runs in HDR space before tonemapping. Only called when DLSS is active and sharpness > 0.
	 */
	void ApplySharpening();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
	HANDLE GetFrameLatencyWaitableObject() const;
	float GetFrameTime() const;

	// Backend interface methods
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	void UpgradeBackendInterface(void** ppInterface);
	void SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();

	// Module availability methods
	bool HasFrameGenModule() const;

	// Proxy interface methods
	void SetProxyD3D11Device(ID3D11Device* device);
	void SetProxyD3D11DeviceContext(ID3D11DeviceContext* context);
	void CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();

	using BlurResources = DX12SwapChain::BlurResources;

	// Get all D3D11 resources needed for background blur when D3D12 swap chain is active
	BlurResources GetBlurResources() const;

private:
	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};
};
