#pragma once

#include "Buffer.h"
#include "RenderModule.h"

#include <DirectXMath.h>
#include <cstddef>
#include <dxgi.h>
#include <functional>
#include <mutex>
#include <unordered_map>

struct CameraSuite : public RenderModule
{
private:
	static constexpr std::string_view MOD_ID = "179371";

public:
	virtual inline std::string GetName() override { return "Camera Suite"; }
	virtual std::string GetDisplayName() override { return T("feature.camera_suite.name", "Camera Suite"); }
	virtual inline std::string GetShortName() override { return "CameraSuite"; }
	virtual inline std::string GetModuleSupportLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool IsCore() const override { return false; }

	virtual inline std::string_view GetShaderDefineName() override { return "CAMERA_SUITE"; }
	/** @brief Returns true for ImageSpace and Sky shader types. */
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		return shaderType == RE::BSShader::Type::ImageSpace || shaderType == RE::BSShader::Type::Sky;
	}

	/** @brief Returns a localized description and key feature bullet points for the UI. */
	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
	{
		return { T("feature.camera_suite.description", "HDR display output plus PIXL Physical Camera exposure and tone mapping for SDR/HDR."),
			{ T("feature.camera_suite.key_feature_1", "HDR10 output support with 16-bit intermediate rendering and unclamped highlight transport."),
				T("feature.camera_suite.key_feature_2", "PIXL Physical Camera: histogram auto exposure, highlight protection, local adaptation and a luminance-preserving camera response."),
				T("feature.camera_suite.key_feature_3", "Optional experimental bodycam emulation with sensor/lens behavior kept separate from the neutral camera defaults.") } };
	};

	struct Settings
	{
		bool enableHDR = false;           // false = SDR output, true = HDR10 output
		uint hdrPaperWhite = 203;         // Reference white brightness in nits for HDR
		uint hdrPeakNits = 800;           // Maximum display brightness in nits for HDR
		float hdrUIBrightness = 1.0f;     // UI brightness multiplier for HDR mode
		bool dontShowHDRWarning = false;  // User preference to suppress HDR warning popup
		bool hdrAutoDetected = false;     // Has auto-detection run at least once?

		// PIXL Physical Camera. Defaults are intentionally neutral and preserve
		// the authored palette while recovering highlight/shadow information.
		bool enablePhysicalCamera = true;
		bool cameraAutoExposure = true;
		float cameraExposureCompensationEV = 0.0f;
		float cameraMinExposureEV = -6.0f;
		float cameraMaxExposureEV = 6.0f;
		float cameraLowPercentile = 0.02f;
		float cameraHighPercentile = 0.98f;
		float cameraHighlightProtection = 0.65f;
		float cameraShadowDetail = 0.14f;
		float cameraContrast = 1.0f;
		float cameraLocalExposure = 0.12f;
		float cameraAdaptBrightToDark = 1.20f;
		float cameraAdaptDarkToBright = 0.25f;
		float cameraSaturation = 1.0f;
		float cameraToe = 0.12f;
		float cameraShoulder = 0.72f;
		float cameraInfluence = 1.0f;        // Full physical response by default; lower values are an authoring aid
		float menuSceneBrightness = 1.0f;
		uint lookPreset = 0;              // 0=Original, 1..11=curated PIXL LUTs
		float lookOpacity = 0.35f;

		// PIXL-owned global finishing policy. These replace the need for an
		// external injector while keeping Skyrim's existing bloom pass.
		bool enableBloom = false;
		float bloomStrength = 0.80f;
		float bloomThreshold = 1.20f;
		float bloomRadius = 1.00f;

		// Stormglass is PIXL's weather-reactive optical surface. It is procedural
		// and consumes Skyrim's real precipitation intensity; no external lens
		// texture or injector is required.
		bool enableStormglass = true;
		float stormglassStrength = 0.58f;
		float stormglassDropScale = 1.00f;
		float stormglassRefraction = 0.65f;
		float stormglassTrails = 0.55f;
		float stormglassDryingRate = 0.055f;

		// A conservative extension of Skyrim's authored underwater presentation.
		// The surface-break film feeds Stormglass as the camera leaves the water.
		bool enableSubmergedOptics = true;
		float submergedStrength = 0.32f;
		float submergedBlur = 0.22f;
		float submergedRefraction = 0.42f;
		float submergedTransitionSpeed = 3.20f;

		// Environmental and impact-reactive edge optics. The cold layer is driven
		// by real snow weather plus exterior altitude; elemental pulses are fed by
		// confirmed player projectile impacts rather than by camera proximity.
		bool enableColdLens = true;
		float coldLensStrength = 0.28f;
		float coldAltitudeStart = 28000.0f;
		float coldAltitudeFull = 60000.0f;
		bool enableElementalDamageLens = true;
		float elementalLensStrength = 0.45f;

		// Release path: let Skyrim own depth of field and expose only its native
		// runtime enable. The retired PIXL fields remain serialized below solely so
		// older UserGraphics files keep loading without a schema break.
		bool enableSkyrimDepthOfField = true;
		bool enableEnhancedDepthOfField = false;
		bool dofAutoFocus = true;
		float dofStrength = 0.24f;
		float dofFocusDistance = 2200.0f;
		float dofFocusRange = 480.0f;
		float dofBokehRadius = 1.0f;
		float dofHighlightResponse = 0.28f;
		float dofFocusEdgeProtection = 0.85f;
		float dofForegroundCoverage = 0.70f;
		float dofCatEye = 0.20f;
		float dofAnamorphicRatio = 1.0f;

		// Depth-aware camera motion blur. This is deliberately opt-in so the
		// accepted PIXL Ultra presentation remains unchanged until requested.
		// The final CameraSuite pass reconstructs camera motion from scene depth;
		// UI is composited afterwards and therefore remains perfectly sharp.
		bool enableModernMotionBlur = false;
		float motionBlurStrength = 0.45f;
		float motionBlurShutter = 0.50f;
		float motionBlurMaxPixels = 24.0f;

		// Experimental body-worn digital camera emulation. Disabled by default.
		bool experimentalBodycam = false;
		float bodycamStrength = 0.75f;
		float bodycamDistortion = 0.12f;
		float bodycamNoise = 0.18f;
		float bodycamVignette = 0.08f;
		float bodycamChromaticAberration = 0.025f;
		float bodycamSharpen = 0.10f;
		float bodycamExposureAggressiveness = 0.75f;
		float bodycamHighlightBloom = 0.15f;
		float bodycamWhiteBalance = 0.12f;
	};

	// SharedData::HDRData.w: menu/scene path for ISHDR; HDRSun uses w>0 to scale sun toward kMenuSunNits (see HDRSun.hlsli).
	static constexpr float kHdrMenuSceneGameplay = 0.f;
	static constexpr float kHdrMenuScenePauseOrMap = 0.58f;
	static constexpr float kHdrMenuSceneMainOrLoading = 1.f;

	Settings settings;
	uint32_t cameraQuality = 3;  // Runtime cost tier; never changes the authored camera look.
	std::mutex settingsMutex;

	struct alignas(16) PostProcessSettings
	{
		uint32_t EnableEnhancedDepthOfField;
		float DofBokehRadius;
		float DofHighlightResponse;
		float DofFocusEdgeProtection;

		float DofForegroundCoverage;
		float DofCatEye;
		float DofAnamorphicRatio;
		uint32_t DofQuality;
	};
	STATIC_ASSERT_ALIGNAS_16(PostProcessSettings);

	/** @brief Resets HDR settings to defaults, auto-detecting HDR monitor status. */
	virtual void RestoreDefaultSettings() override;
	/** @brief Loads HDR settings from JSON and schedules auto-detection if needed. */
	virtual void LoadSettings(json& o_json) override;
	/** @brief Saves HDR settings to JSON (thread-safe). */
	virtual void SaveSettings(json& o_json) override;
	/** @brief Draws the HDR output and PIXL Physical Camera settings UI. */
	virtual void DrawSettings() override;

	/** @brief Enables the bUse64bitsHDRRenderTarget INI setting for float16 render targets. */
	virtual void DataLoaded() override;
	/** @brief Creates HDR, output, and UI textures, constant buffer, and upgrades LDR render targets. */
	virtual void SetupResources() override;
	/** @brief Releases cached HDR, physical-camera, and UI compute shaders. */
	virtual void ClearShaderCache() override;
	/** @brief Installs HDR pipeline hooks when the ImageReconstruction feature is not loaded. */
	virtual void PostPostLoad() override;

	/** @brief Returns the HDR shared data (enable flag, paper white, peak nits, menu scene encoding). */
	float4 GetSharedDataHDR() const;
	PostProcessSettings GetPostProcessData() const;
	/** Applies translated external preset values through the camera settings lock. */
	void ApplyExternalLook(float exposureEV, float contrast, float saturation, float adaptationSeconds, float highlightProtection, float shadowDetail, float toe, float shoulder, bool bloomEnabled, float bloomStrength, uint lookPreset = 0, float lookOpacity = 0.0f, float influence = 0.85f);
	/** @brief Updates the HDR constant buffer with current settings and menu state. */
	void UpdateHDRData() const;
	/** Applies PIXL's user-facing bloom policy to the current image-space state. */
	void ApplyPlayerPostProcessing() const;
	/** @brief Suspends PIXL depth-dependent camera effects while Director owns the camera. */
	void SetPhotoModeDofIsolation(bool enabled);
	/** @brief Adds a confirmed player-hit elemental optical pulse (0..1). */
	void TriggerElementalLens(float fireAmount, float frostAmount);
	[[nodiscard]] bool IsPhotoModeDofIsolated() const { return photoModeDofIsolation; }
	/** @brief Sets the swap chain color space to HDR10 (PQ/BT.2020) or SDR (sRGB) based on settings. */
	void UpdateSwapChainColorSpace() const;

	/** @brief Redirects UI rendering to the separate UI texture for HDR compositing. */
	void BeginUIRendering();
	/** @brief Restores the original render target after UI rendering. */
	void EndUIRendering();
	/** @brief Returns true while UI is being rendered to the separate UI texture. */
	bool IsRenderingUI() const { return renderingUI; }

	/** @brief Redirects kFRAMEBUFFER to the float16 HDR texture so ISHDR can write values above 1.0. */
	void RedirectFramebuffer();
	/** @brief Restores the original kFRAMEBUFFER texture, SRV, and RTV after HDR rendering. */
	void RestoreFramebuffer();

	/** @brief Redirects kFRAMEBUFFER.RTV to the UI texture for vanilla UI capture. */
	void SetUIBuffer();
	/** @brief Clears the UI texture and restores the original kFRAMEBUFFER.RTV. */
	void ClearUIBuffer();
	/** @brief Returns true when HDR, camera, bloom, or a LUT needs PIXL's final presentation pass. */
	bool NeedsPresentationComposite() const;
	/** @brief Returns true when non-FG HDR deferred compositing is active (composite after Present-hook mods). */
	bool UsesDeferredPresentComposite() const;
	/** @brief Aligns kFRAMEBUFFER.RTV with uiTexture for engine paths when ImGui has already bound the OM. */
	void SyncFramebufferUIRedirect();

	/** @brief Scales UI brightness in the Frame Gen UI buffer using the UI brightness compute shader. */
	void ScaleUIBrightnessForFG();
	/** @brief Returns true when the D3D12 UI buffer path should be used for frame generation. */
	bool ShouldUseD3D12UIBuffer();

	/** @brief Runs the HDR output compute shader to composite scene and UI, then copies to the back buffer. */
	void ApplyHDR();

	/** @brief Snapshots hdrTexture before the menu blur dirties it in place. */
	void SnapshotCleanScene();

	/** @brief Returns true when the snapshot was refreshed this frame; stale means hdrTexture wasn't blurred. */
	bool IsCleanSceneCaptureFresh() const;

	/** @brief Runs the HDR output transform on a clean scene (no UI buffer) and writes into outputTexture.
	 *  @param sceneSRV The clean HDR scene SRV.
	 *  @param sdrPreview If true, applies SDR preview transform instead of HDR output.
	 *  @return The composed output texture.
	 */
	ID3D11Texture2D* ComposeCleanCapture(ID3D11ShaderResourceView* sceneSRV, bool sdrPreview);

	/**
	 * @brief Returns a patched blend state with corrected alpha blending for HDR UI compositing.
	 * @param original The original blend state to potentially patch.
	 * @return The patched blend state, or the original if no patch is needed.
	 */
	ID3D11BlendState* GetPatchedAlphaBlendState(ID3D11BlendState* original);

	/**
	 * @brief Installs swap chain Present vtable hooks and OMSetBlendState detour for HDR pipeline.
	 * @param swapChain The DXGI swap chain to hook.
	 */
	static void InstallSwapChainPresentHooks(IDXGISwapChain* swapChain);
	/**
	 * @brief Handles the swap chain Present call, drawing ImGui overlay and running HDR compositing.
	 * @param swapChain The DXGI swap chain.
	 * @param syncInterval VSync interval.
	 * @param flags Present flags.
	 * @param presentChain The original Present call chain to invoke.
	 * @return The HRESULT from the final Present call.
	 */
	HRESULT HandleSwapChainPresent(
		IDXGISwapChain* swapChain,
		UINT syncInterval,
		UINT flags,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain);
	/**
	 * @brief Draws and processes PIXL's ImGui overlay on the active D3D11
	 * presentation target.
	 *
	 * The native D3D11 Present hook and the D3D12 frame-generation proxy must
	 * both call this exactly once per presented game frame.
	 */
	void DrawRendererUIForPresent();

	/** @brief Returns true while the Present bottom hook is suppressed during deferred compositing. */
	bool IsPresentSuppressed() const { return presentSuppressed; }
	/**
	 * @brief Sets whether the Present bottom hook should be suppressed.
	 * @param value True to suppress, false to allow.
	 */
	void SetPresentSuppressed(bool value) { presentSuppressed = value; }

	/** @brief Releases all HDR textures, constant buffers, and restores LDR render targets. */
	void DestroyResources();

	XM_ALIGNED_STRUCT(16)
	HDRDataCB
	{
		float enableHDR;                 ///< 1.0 = HDR output with PQ, 0.0 = SDR output with gamma
		float paperWhite;                ///< Reference white brightness in nits for HDR
		float peakNits;                  ///< Maximum display brightness in nits for HDR
		float skipUIComposite;           ///< 1.0 = FG handles UI, skip our compositing
		float uiBrightness;              ///< UI brightness multiplier (Frame Gen compositing)
		float isSceneLinear;             ///< 1.0 = Linear Light Core active, scene already linear
		float pad0;                      ///< 1.0 = main menu/loading screen active
		float fgTweenMenuMidAlphaBoost;  ///< 1.0 = TweenMenu (pause) open — FG UIBrightnessCS mid-alpha boost only
		float previewSDR;                ///< 1.0 = emit sRGB SDR (crop preview) instead of PQ HDR10
		float applyAutoHDR;              ///< Reserved compatibility field; PIXL owns the tonemap path and writes zero
		float menuSceneBrightness;          ///< Display-referred gain for main/loading imagery; reuses legacy c2.z padding.
		float stormglassLateralInertia;  ///< Signed filtered camera-yaw inertia, -1..1; reuses c2.w padding

		float physicalCameraEnabled;
		float cameraAutoExposure;
		float cameraExposureCompensationEV;
		float cameraMinExposureEV;

		float cameraMaxExposureEV;
		float cameraLowPercentile;
		float cameraHighPercentile;
		float cameraHighlightProtection;

		float cameraShadowDetail;
		float cameraContrast;
		float cameraLocalExposure;
		float cameraAdaptBrightToDark;

		float cameraAdaptDarkToBright;
		float bodycamEnabled;
		float bodycamStrength;
		float bodycamDistortion;

		float bodycamNoise;
		float bodycamVignette;
		float bodycamChromaticAberration;
		float bodycamSharpen;

		float bodycamExposureAggressiveness;
		float bodycamHighlightBloom;
		float bodycamWhiteBalance;
		float cameraSaturation;

		float deltaTime;
		uint frameIndex;
		float cameraToe;
		float cameraShoulder;

		float lookEnabled;
		float lookOpacity;
		float cameraInfluence;
		float auxiliaryPassMask;          ///< 1=bloom pyramid, 2=local exposure, 4=Stormglass field

		float bloomEnabled;
		float bloomStrength;
		float bloomThreshold;
		float bloomRadius;

		float stormglassEnabled;
		float stormglassRainIntensity;
		float stormglassWetness;
		float stormglassStrength;

		float stormglassDropScale;
		float stormglassRefraction;
		float stormglassTrails;
		float stormglassTime;

		float surfaceBreakFilm;
		float submergedOpticsEnabled;
		float submergedBlend;
		float submergedStrength;

		float submergedBlur;
		float submergedRefraction;
		float submergedFogAmount;
		float cameraQuality;              ///< 0 Low .. 3 Ultra; reuses c15.w without changing the CB layout

		float4 submergedWaterTint;

		float dofEnabled;
		float dofStrength;
		float dofFocusDistance;
		float dofFocusRange;

		float dofBokehRadius;
		float dofHighlightResponse;
		float dofFocusEdgeProtection;
		float dofForegroundCoverage;

		float dofCatEye;
		float dofAnamorphicRatio;
		float dofQuality;
		float dofAutoFocus;

		float coldLensAmount;
		float fireLensAmount;
		float coldLensStrength;
		float elementalLensStrength;

		float motionBlurEnabled;
		float motionBlurStrength;
		float motionBlurShutter;
		float motionBlurMaxPixels;
	};

	static_assert((sizeof(HDRDataCB) % 16) == 0, "CB size not padded correctly");
	static_assert(sizeof(HDRDataCB) == 352, "HDRDataCB must match PhysicalCameraCommon.hlsli (22 float4 registers / 352 bytes).");
	static_assert(offsetof(HDRDataCB, physicalCameraEnabled) == 48);
	static_assert(offsetof(HDRDataCB, cameraHighlightProtection) == 76);
	static_assert(offsetof(HDRDataCB, bodycamEnabled) == 100);
	static_assert(offsetof(HDRDataCB, bodycamExposureAggressiveness) == 128);
	static_assert(offsetof(HDRDataCB, frameIndex) == 148);
	static_assert(offsetof(HDRDataCB, bloomEnabled) == 176);
	static_assert(offsetof(HDRDataCB, stormglassEnabled) == 192);
	static_assert(offsetof(HDRDataCB, surfaceBreakFilm) == 224);
	static_assert(offsetof(HDRDataCB, cameraQuality) == 252);
	static_assert(offsetof(HDRDataCB, submergedWaterTint) == 256);
	static_assert(offsetof(HDRDataCB, dofEnabled) == 272);
	static_assert(offsetof(HDRDataCB, dofBokehRadius) == 288);
	static_assert(offsetof(HDRDataCB, dofCatEye) == 304);
	static_assert(offsetof(HDRDataCB, coldLensAmount) == 320);
	static_assert(offsetof(HDRDataCB, motionBlurEnabled) == 336);

	// HDR data CB contents from current settings/game state (previewSDR=0).
	HDRDataCB BuildHDRData() const;

	ConstantBuffer* hdrDataCB = nullptr;

	// Presentation-state integration is updated at most once per rendered frame
	// even when clean capture and frame-generation paths request extra composites.
	mutable bool environmentStateValid = false;
	mutable std::uint32_t environmentStateFrame = 0;
	mutable float environmentTime = 0.0f;
	mutable float stormglassRainIntensityState = 0.0f;
	mutable float stormglassWetnessState = 0.0f;
	mutable float surfaceBreakFilmState = 0.0f;
	mutable float submergedBlendState = 0.0f;
	mutable float stormglassLateralInertiaState = 0.0f;
	mutable float stormglassPreviousCameraYaw = 0.0f;
	mutable bool stormglassCameraYawValid = false;
	mutable bool wasPlayerUnderwater = false;
	mutable float coldLensState = 0.0f;
	mutable float fireLensState = 0.0f;
	mutable float frostImpactLensState = 0.0f;
	// Main/loading preview geometry can arrive one render pass before its authored
	// colour textures. A short scene-only reveal masks that white placeholder
	// frame without dimming UI text or affecting gameplay exposure.
	mutable bool displayMenuWasActive = false;
	mutable std::uint32_t displayMenuTransitionFrame = UINT32_MAX;
	mutable float displayMenuTransition = 1.0f;

	// Director keeps PIXL's optional depth-dependent camera effects isolated from
	// its clean capture path. Skyrim's native DOF remains governed by the public
	// enableSkyrimDepthOfField setting.
	bool photoModeDofIsolation = false;

	Texture2D* hdrTexture = nullptr;
	Texture2D* outputTexture = nullptr;
	Texture2D* uiTexture = nullptr;          // Separate UI render target for proper compositing
	Texture2D* cleanSceneCapture = nullptr;  // Pre-blur copy of hdrTexture for clean captures
	uint cleanSceneCaptureFrame = UINT32_MAX;  // frameCount when cleanSceneCapture was last refreshed

	ID3D11ComputeShader* hdrOutputCS = nullptr;
	// Avoid retrying a missing/invalid optional HDR shader every frame. The
	// failure is retried when the module shader cache is explicitly cleared.
	bool hdrOutputCompileFailed = false;
	winrt::com_ptr<ID3D11ShaderResourceView> lookTextureView;
	winrt::com_ptr<ID3D11ShaderResourceView> frostLensTextureView;
	winrt::com_ptr<ID3D11ShaderResourceView> fireLensTextureView;
	winrt::com_ptr<ID3D11SamplerState> lookSampler;
	void LoadLookTexture();
	void LoadElementalLensTextures();
	/** @brief Returns the HDR/physical-camera output compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetHDROutputCS();

	Texture2D* cameraHistogramTexture = nullptr;  // 256-bin R32_UINT log-luminance histogram
	Texture2D* cameraExposureTexture = nullptr;   // 1x1 R32_FLOAT adapted exposure multiplier
	Texture2D* cameraLocalExposureTexture = nullptr;  // Quarter-resolution R16_FLOAT local exposure gain
	Texture2D* bloomHalfTexture = nullptr;
	Texture2D* bloomQuarterTexture = nullptr;
	Texture2D* bloomEighthTexture = nullptr;
	Texture2D* bloomSixteenthTexture = nullptr;
	Texture2D* bloomEighthScratchTexture = nullptr;
	Texture2D* bloomQuarterScratchTexture = nullptr;
	Texture2D* bloomHalfScratchTexture = nullptr;
	Texture2D* stormglassFieldTexture = nullptr;
	ID3D11ComputeShader* physicalCameraHistogramCS = nullptr;
	ID3D11ComputeShader* physicalCameraExposureCS = nullptr;
	ID3D11ComputeShader* physicalCameraLocalExposureCS = nullptr;
	ID3D11ComputeShader* bloomPrefilterCS = nullptr;
	ID3D11ComputeShader* bloomDownsampleCS = nullptr;
	ID3D11ComputeShader* bloomUpsampleCS = nullptr;
	ID3D11ComputeShader* stormglassFieldCS = nullptr;
	bool localExposurePassReady = false;
	bool bloomPassReady = false;
	bool stormglassPassReady = false;
	ID3D11ComputeShader* GetPhysicalCameraHistogramCS();
	ID3D11ComputeShader* GetPhysicalCameraExposureCS();
	ID3D11ComputeShader* GetPhysicalCameraLocalExposureCS();
	ID3D11ComputeShader* GetBloomPrefilterCS();
	ID3D11ComputeShader* GetBloomDownsampleCS();
	ID3D11ComputeShader* GetBloomUpsampleCS();
	ID3D11ComputeShader* GetStormglassFieldCS();
	void UpdatePhysicalCameraExposure(ID3D11ShaderResourceView* sceneSRV);
	void RunCameraFinishingPasses(ID3D11ShaderResourceView* sceneSRV);
	void SetupCameraFinishingResources(const D3D11_TEXTURE2D_DESC& sceneDesc);

	ID3D11ComputeShader* uiBrightnessCS = nullptr;
	/** @brief Returns the UI brightness scaling compute shader, compiling it on first use. */
	ID3D11ComputeShader* GetUIBrightnessCS();

	/** @brief Detects whether Windows HDR is currently active on the swap chain's monitor. */
	static bool DetectHDR();
	static bool isHDRMonitor;            // Windows HDR is active (enabled in OS settings)
	static bool isHDRCapableMonitor;     // Monitor supports HDR but Windows HDR may be off
	static bool wasExclusiveFullscreen;  // EFS detected at swapchain creation; incompatible with HDR
	bool pendingAutoDetect = false;

	/** @brief Queries the DXGI output for the display's maximum luminance in nits. */
	float GetDisplayMaxLuminance() const;
	mutable float cachedDisplayMaxLuminance = 1000.0f;

	// Saved state for UI rendering redirection
	bool renderingUI = false;
	ID3D11RenderTargetView* savedRTV = nullptr;
	ID3D11DepthStencilView* savedDSV = nullptr;
	ID3D11RenderTargetView* savedFramebufferRTV = nullptr;  // Original kFRAMEBUFFER.RTV for restoration

	// Saved kFRAMEBUFFER state for HDR redirect (ISHDR writes to hdrTexture instead)
	ID3D11Texture2D* savedFramebufferTexture = nullptr;
	ID3D11ShaderResourceView* savedFramebufferSRV = nullptr;
	bool framebufferRedirected = false;

	/** @brief Upgrades post-tonemapping LDR render targets to R16G16B16A16_FLOAT for HDR values. */
	void UpgradeLDRRenderTargets();
	/** @brief Restores all upgraded render targets to their original format and releases upgraded resources. */
	void RestoreLDRRenderTargets();

	struct SavedRenderTarget
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* RTV = nullptr;
		ID3D11ShaderResourceView* SRV = nullptr;
		ID3D11UnorderedAccessView* UAV = nullptr;
	};

	std::vector<std::pair<RE::RENDER_TARGETS::RENDER_TARGET, SavedRenderTarget>> savedLDRTargets;

private:
	bool showHDRWarningPopup = false;
	bool pendingHDREnable = false;
	bool presentSuppressed = false;
	std::unordered_map<ID3D11BlendState*, winrt::com_ptr<ID3D11BlendState>> patchedBlendStateCache;

	HRESULT PresentToSwapChain(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
	void DrawImGuiForPresent(bool frameGenActive, bool hdrReady);
	void RunHDRBeforePresentChain(bool hdrReady);
	HRESULT RunPresentChainWithHDR(
		IDXGISwapChain* swapChain,
		UINT syncInterval,
		UINT flags,
		bool hdrReady,
		bool frameGenActive,
		const std::function<HRESULT(IDXGISwapChain*, UINT, UINT)>& presentChain);

	struct D3D12UIBufferMode
	{
		bool useUIBuffer = false;
		bool useFallbackCopy = false;
	};

	D3D12UIBufferMode GetD3D12UIBufferMode();

	// Bind scene (t0), UI (t1, may be null), UAV (u0), CB (b0); dispatch the output CS; unbind.
	void DispatchHDROutput(ID3D11ShaderResourceView* sceneSRV, ID3D11ShaderResourceView* uiSRV, ID3D11UnorderedAccessView* uav);

	// True when FFX frame generation is actively compositing UI this frame.
	bool IsFGCompositingThisFrame() const;
};
