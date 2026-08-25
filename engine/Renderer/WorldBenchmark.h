#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PIXLRenderer
{
	/** Deterministic cinematic feature tour and visual/performance consistency sweep. */
	class WorldBenchmark
	{
	public:
		static WorldBenchmark& GetSingleton();
		void DrawUI();
		void Update();
	[[nodiscard]] bool IsRunning() const { return running; }

	private:
		enum class Phase {
			Idle,
			LoadLocation,
			AwaitLocation,
			ApplyTime,
			ApplyWeather,
			Settle,
			BeginFlythrough,
			Flythrough,
			AwaitCapture,
			Advance
		};
		void Start();
		void Cancel();
		void QueueCurrentLocation();
		bool BeginCurrentFlythrough();
		void UpdateCurrentFlythrough(std::chrono::steady_clock::time_point now);
		void CaptureFlythroughFrame(std::size_t markerIndex);
		void FinishCurrentFlythrough();
		void SkipCurrentLocation(std::string_view reason);
		void ExitBenchmarkCamera();
		void HideHudForBenchmark();
		void RestoreHudAfterBenchmark();
		void ApplyQualityOverride();
		void RestoreQualityOverride();
		void ResetMetrics();
		void AccumulateMetrics();
		[[nodiscard]] std::size_t GetSceneCount() const;
		[[nodiscard]] std::size_t GetSceneTableIndex() const;
		void WriteManifest() const;
		void WriteSettingsSnapshot() const;
		void WriteSample() const;
		void WriteFailure(std::string_view reason) const;

		bool running = false;
		bool includeScreenshots = true;
		bool cinematicCamera = true;
		bool forceDlaaNoFrameGeneration = true;
		bool disableFrameLimiter = true;
		int routeMode = 0;  // 0 = feature tour, 1 = full consistency sweep
		int settleSeconds = 8;
		int flythroughSeconds = 12;
		std::size_t locationIndex = 0;
		Phase phase = Phase::Idle;
		std::chrono::steady_clock::time_point phaseStarted{};
		std::chrono::steady_clock::time_point stableCellSince{};
		std::uintptr_t originCell = 0;
		std::uintptr_t stableCell = 0;
		bool loadingObserved = false;
		bool fallbackAttempted = false;
		bool cameraOwned = false;
		bool hudVisibilitySnapshotValid = false;
		bool hudWasVisible = true;
		std::uint32_t capturedMarkers = 0;
		float cameraAnchor[3]{};
		float cameraBaseRotation[2]{};
		float originalWorldFov = 75.0f;

		struct QualitySnapshot
		{
			bool valid = false;
			std::uint32_t upscaleMethod = 0;
			std::uint32_t qualityMode = 0;
			std::uint32_t frameGenerationMode = 0;
			std::uint32_t frameLimitMode = 0;
		};
		QualitySnapshot qualitySnapshot{};

		std::vector<float> frameTimeSamples;
		std::vector<float> pixlGpuSamples;
		std::filesystem::path outputDirectory;
		std::string status = "Ready";
	};
}
