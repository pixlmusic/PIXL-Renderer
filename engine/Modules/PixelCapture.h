#pragma once

#include "RenderModule.h"
#include "Utils/Subrect.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct PixelCapture : public RenderModule
{
	/** @brief Stops the background screenshot worker thread on destruction. */
	virtual ~PixelCapture();
	virtual std::string GetName() override { return "Pixel Capture"; }
	virtual std::string GetDisplayName() override { return T("feature.pixel_capture.name", "Pixel Capture"); }
	virtual std::string GetShortName() override { return "PixelCapture"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kUtility; }

	/** @brief Returns true, indicating this feature's settings are always visible in the menu. */
	virtual bool IsInMenu() const override;

	/** @brief Draws the ImGui settings UI for screenshot path, format, crop, and hotkey configuration. */
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	/** @brief Resets transient state (no-op for this feature). */
	virtual void Reset() override;
	/** @brief Called after all features are loaded (no-op for this feature). */
	virtual void PostPostLoad() override;

	/** @brief Captures a screenshot from the current back buffer and enqueues it for async encoding and save. */
	void Capture();
	/** @brief Checks for a pending capture request and executes Capture() if one is pending. Called after HDR Present processing. */
	void ProcessCaptureRequest();
	/** @brief Requests a full-frame capture to an exact path after the current frame is presented. */
	void RequestCaptureToPath(std::filesystem::path outputPath, bool notify = false);
	/** @brief Returns asynchronous screenshot requests that have not finished encoding. */
	[[nodiscard]] std::uint32_t GetPendingCaptureCount() const;

	/**
	 * @brief Starts a high-quality Director Photo Finish capture.
	 *
	 * Sampling is spread across consecutive presented frames; reconstruction,
	 * motion finish, upsampling and image encoding all execute off the render
	 * thread once the samples have been copied to staging textures.
	 */
	void RequestPhotoFinishCapture();

	/** @brief True while Director is collecting temporal samples from presented frames. */
	[[nodiscard]] bool IsPhotoFinishSampling() const;
	/** @brief Number of temporal samples copied for the active Photo Finish capture. */
	[[nodiscard]] std::uint32_t GetPhotoFinishSamplesCaptured() const;
	/** @brief Target temporal sample count for the active Photo Finish capture. */
	[[nodiscard]] std::uint32_t GetPhotoFinishSamplesTarget() const;

	enum class PhotoFinishStage : std::uint32_t
	{
		Idle = 0,
		Accumulating,
		Resolving,
		LensDepthOfField,
		MotionFinish,
		DetailRecovery,
		Upscaling,
		Saving
	};

	/** @brief Current offline photo-processing stage for Director HUD feedback. */
	[[nodiscard]] PhotoFinishStage GetPhotoFinishStage() const;
	/** @brief Normalized 0..1 progress through the current Photo Finish job. */
	[[nodiscard]] float GetPhotoFinishProgress() const;
	/** @brief Short user-facing label for the current Photo Finish stage. */
	[[nodiscard]] const char* GetPhotoFinishStageLabel() const;
	/** @brief True after temporal sampling while the worker is refining/saving. */
	[[nodiscard]] bool IsPhotoFinishProcessing() const;

	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	// HDR PNG quantization (7-16); used when Camera Suite captures the back buffer.
	unsigned int hdrPngBitDepth = 11;
	// SDR output (HDR captures always use PNG).
	bool sdrUsePng = true;
	// After save, put the file path on the clipboard (CF_HDROP).
	bool copyToClipboard = false;

	// ---------------------------------------------------------------------
	// PIXL Director Photo Finish
	// ---------------------------------------------------------------------
	// Regular screenshot hotkeys remain fast/native. Director's TAKE PHOTO
	// explicitly requests this path so ordinary gameplay screenshots are not
	// unexpectedly turned into multi-frame high-resolution jobs.
	bool photoFinishEnabled = true;
	// Output multiplier. Supported values are 1, 2 and 4.
	unsigned int photoFinishScale = 2;
	// User-facing Director fidelity preset:
	// 0 Fast, 1 Enhanced, 2 Cinematic, 3 Ultra.
	unsigned int photoFinishQualityPreset = 1;

	// Consecutive presented frames accumulated before reconstruction.
	// C5D supports deeper still-image accumulation; runtime memory safety may
	// reduce the requested count automatically for very large captures.
	unsigned int photoFinishTemporalSamples = 8;
	// C5E.3 multi-scale luma-guided detail recovery around the jitter-aware resolve.
	float photoFinishDetailStrength = 0.35f;

	// Legacy Photo Lens data is retained for settings compatibility, but the
	// experimental resolve is disabled until its depth edges are release-ready.
	bool photoLensDofEnabled = false;
	unsigned int photoLensDofQuality = 2;  // 0 Fast, 1 High, 2 Cinematic, 3 Ultra
	float photoLensDofStrength = 0.62f;
	unsigned int photoLensDofApertureBlades = 7;
	float photoLensDofHighlightBoost = 0.12f;

	// Optional photographic directional shutter finish.
	bool photoFinishMotionEnabled = false;
	float photoFinishMotionStrength = 0.18f;
	float photoFinishMotionAngleDegrees = 0.0f;

	struct DirectorPhotoPreset
	{
		bool valid = false;
		unsigned int lookPreset = 0;
		float lookOpacity = 0.35f;
		float exposure = 0.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float highlightProtection = 0.0f;
		float shadowDetail = 0.0f;
		bool bloomEnabled = false;
		float bloomStrength = 0.0f;
		float fieldOfView = 75.0f;
		bool motionEnabled = false;
		float motionStrength = 0.18f;
		float motionAngleDegrees = 0.0f;
	};

	std::array<DirectorPhotoPreset, 3> directorPhotoPresets{};

	std::atomic<bool> captureRequested{ false };

private:
	struct PendingScreenshot
	{
		winrt::com_ptr<ID3D11Texture2D> stagingTexture;

		// Director Photo Finish stores one staging texture per presented sample.
		// The render thread only performs GPU copies; CPU reconstruction happens
		// later on the screenshot worker.
		std::vector<winrt::com_ptr<ID3D11Texture2D>> photoFinishSamples;
		// C5E.3 keeps the actual projection jitter paired with each captured frame.
		// The offline resolve uses these positions as sub-pixel reconstruction data
		// instead of discarding them after accumulation diagnostics.
		std::vector<float2> photoFinishJitterOffsets;
		winrt::com_ptr<ID3D11Texture2D> photoDepthStagingTexture;
		DXGI_FORMAT photoDepthFormat = DXGI_FORMAT_UNKNOWN;
		bool photoFinish = false;
		unsigned int photoFinishScale = 1;
		float photoFinishDetailStrength = 0.0f;
		bool photoLensDofEnabled = false;
		unsigned int photoLensDofQuality = 0;
		float photoLensDofStrength = 0.0f;
		unsigned int photoLensDofApertureBlades = 7;
		float photoLensDofHighlightBoost = 0.0f;
		float photoLensDofFocusDistance = 2200.0f;
		float photoLensDofFocusRange = 1600.0f;
		float photoLensDofEdgeProtection = 0.85f;
		float photoLensDofForegroundCoverage = 0.70f;
		float photoLensDofCatEye = 0.20f;
		float photoLensDofAnamorphicRatio = 1.0f;
		bool photoFinishMotionEnabled = false;
		float photoFinishMotionStrength = 0.0f;
		float photoFinishMotionAngleDegrees = 0.0f;

		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		std::filesystem::path outputPath;
		bool saveAsHdrPng = false;
		bool saveAsSdrPng = false;
		int hdrPngBitDepth = 11;
		bool copyToClipboard = false;
		bool notify = true;
	};

	struct PhotoFinishBurst
	{
		std::vector<winrt::com_ptr<ID3D11Texture2D>> samples;
		winrt::com_ptr<ID3D11Texture2D> depthStagingTexture;
		DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t sourceWidth = 0;
		uint32_t sourceHeight = 0;
		uint32_t copyX = 0;
		uint32_t copyY = 0;
		uint32_t copyWidth = 0;
		uint32_t copyHeight = 0;
		unsigned int targetSamples = 1;
		unsigned int outputScale = 1;
		float detailStrength = 0.0f;
		bool photoLensDofEnabled = false;
		unsigned int photoLensDofQuality = 0;
		float photoLensDofStrength = 0.0f;
		unsigned int photoLensDofApertureBlades = 7;
		float photoLensDofHighlightBoost = 0.0f;
		float photoLensDofFocusDistance = 2200.0f;
		float photoLensDofFocusRange = 1600.0f;
		float photoLensDofEdgeProtection = 0.85f;
		float photoLensDofForegroundCoverage = 0.70f;
		float photoLensDofCatEye = 0.20f;
		float photoLensDofAnamorphicRatio = 1.0f;
		std::vector<float2> jitterOffsets;
		// StartPhotoFinishCapture runs after the current frame has already been
		// rendered. Skip that immediate ProcessCaptureRequest call so sample 0 is
		// captured on the next frame after the projection override is applied.
		bool awaitingFirstJitteredFrame = true;
		bool motionEnabled = false;
		float motionStrength = 0.0f;
		float motionAngleDegrees = 0.0f;
		std::filesystem::path outputPath;
		bool saveAsHdrPng = false;
		bool saveAsSdrPng = false;
		int hdrPngBitDepth = 11;
		bool copyToClipboard = false;
		bool notify = true;
	};

	struct CustomCaptureRequest
	{
		std::filesystem::path outputPath;
		bool notify = false;
	};

	std::mutex screenshotQueueMutex;
	std::condition_variable screenshotQueueCV;
	std::queue<PendingScreenshot> screenshotQueue;
	std::thread screenshotWorker;
	bool screenshotWorkerRunning = false;
	mutable std::mutex captureRequestMutex;
	std::optional<CustomCaptureRequest> customCaptureRequest;
	std::atomic_uint32_t pendingCaptureCount{ 0 };

	std::atomic<bool> photoFinishRequested{ false };
	// Separate from normal gameplay screenshot requests so Director END cannot
	// generate both a standard screenshot and a Photo Finish image.
	std::atomic<bool> directorCaptureRequested{ false };
	std::optional<PhotoFinishBurst> photoFinishBurst;
	std::atomic_uint32_t photoFinishSamplesCaptured{ 0 };
	std::atomic_uint32_t photoFinishSamplesTarget{ 0 };
	std::atomic_uint32_t photoFinishStage{
		static_cast<std::uint32_t>(PhotoFinishStage::Idle)
	};
	std::atomic<float> photoFinishProgress{ 0.0f };

	Util::Subrect::Controller subrect;

	// SRV-readable copy used when the capture source's own SRV can't be sampled
	// directly (kFRAMEBUFFER on flat aliases the swap-chain backbuffer).
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;

	void EnsureWorkerThread();
	void StopWorkerThread();
	void EnqueueScreenshot(PendingScreenshot&& screenshot);
	void CaptureImpl(const std::optional<std::filesystem::path>& outputPath, bool notify);
	void StartPhotoFinishCapture();
	void CapturePhotoFinishSample();
	void SetPhotoFinishStage(PhotoFinishStage stage, float progress);
	void ScreenshotWorkerLoop();
	void EnsurePreviewCache(ID3D11Texture2D* sourceTexture);
	static void ShowInGameNotification(std::string message);
};
