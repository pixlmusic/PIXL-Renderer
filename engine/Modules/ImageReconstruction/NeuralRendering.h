#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

/**
 * Optional, experimental NVIDIA DLSS Neural Rendering bridge.
 *
 * The runtime is discovered dynamically and all work is recorded on PIXL's
 * existing DX12 sidecar command list.  A missing or incompatible DLL therefore
 * leaves the normal DLSS result untouched and never becomes a plugin dependency.
 *
 * The Feature 18 parameter contract was independently validated against a
 * GPLv3 Skyrim renderer reference implementation at commit
 * 05e037cad2add33a434c09d7b1260d09d331b6a4. PIXL's resource ownership,
 * presentation integration, settings, diagnostics and fail-safe policy are its
 * own implementation.
 */
class NeuralRendering
{
public:
	enum class Status : std::uint8_t
	{
		NotProbed,
		NotFound,
		VersionUnavailable,
		UnsupportedVersion,
		LoadFailed,
		MissingExport,
		Ready,
		InitializationFailed,
		CoreUnavailable,
		ParameterAllocationFailed,
		Initialized,
		RuntimeFault
	};

	struct Tuning
	{
		float intensity = 0.8f;
		float localToneStrength = 0.75f;
		float localStructureStrength = 0.9f;
		float skinStructureStrength = 0.9f;
		std::uint32_t style = 3;
		std::uint32_t performanceQuality = 5;
		std::uint32_t outputPreset = 0;
		bool useAutoMask = true;
		bool uiCorrection = true;
	};

	~NeuralRendering();
	NeuralRendering() = default;
	NeuralRendering(const NeuralRendering&) = delete;
	NeuralRendering& operator=(const NeuralRendering&) = delete;

	bool Probe(const std::filesystem::path& explicitPath = {});
	bool Initialize(ID3D12Device* device);
	bool Evaluate(
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
		bool latchFailure = true);
	void ResetFeature();
	void Shutdown();

	[[nodiscard]] Status GetStatus() const { return status_; }
	[[nodiscard]] const char* GetStatusText() const;
	[[nodiscard]] const std::string& GetVersion() const { return version_; }
	[[nodiscard]] const std::string& GetDetail() const { return detail_; }
	[[nodiscard]] std::uint32_t GetNgxResult() const { return ngxResult_; }
	[[nodiscard]] std::uint64_t GetSuccessfulFrames() const { return successfulFrames_; }

private:
	void LatchRuntimeFault(std::string detail, std::uint32_t result = 0);

	void* module_ = nullptr;
	void* parameters_ = nullptr;
	void* featureHandle_ = nullptr;
	ID3D12Device* device_ = nullptr;
	Status status_ = Status::NotProbed;
	std::filesystem::path runtimePath_;
	std::string version_;
	std::string detail_;
	std::uint32_t ngxResult_ = 0;
	std::uint32_t applicationId_ = 0;
	std::uint32_t apiVersion_ = 0;
	std::uint32_t featureGuideWidth_ = 0;
	std::uint32_t featureGuideHeight_ = 0;
	std::uint32_t featureOutputWidth_ = 0;
	std::uint32_t featureOutputHeight_ = 0;
	std::uint64_t successfulFrames_ = 0;
};
