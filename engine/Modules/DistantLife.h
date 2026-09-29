// PIXL Renderer - distant loaded-light runtime interface.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include "Buffer.h"
#include "RenderModule.h"

#include <array>
#include <unordered_map>

// Experimental renderer-only LOD for genuine, currently loaded exterior lights.
// It never creates, moves, enables, or retains ownership of Skyrim objects.
struct DistantLife : RenderModule
{
	struct Settings
	{
		bool Enabled = false;
		float Intensity = 1.0f;
		float MinimumDistance = 1800.0f;
		float MaximumDistance = 200000.0f;
		bool StaticLights = true;
		bool ActorTorches = false;  // Reserved until a live actor provider is validated.
		bool AtmosphericAttenuation = true;
		bool FarFieldActivity = true;
		float FarFieldDensity = 0.18f;
		float FarFieldMotion = 0.35f;
		std::uint32_t DebugMode = 0;
	};

	std::string GetName() override { return "DistantLife"; }
	std::string GetShortName() override { return "DistantLife"; }
	std::string_view GetCategory() const override { return ModuleGroups::kLighting; }
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;
	void SetupResources() override;
	void Prepass() override;
	void Reset() override;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override;
	void ClearShaderCache() override;

	// Called only from established live-reference paths. The reference is copied
	// immediately and is never retained beyond the call.
	void ObserveStaticLight(
		const RE::TESObjectLIGH* light,
		const RE::TESObjectREFR* reference,
		const RE::NiPoint3* sourcePosition = nullptr,
		bool actorSource = false);
	void DrawDistantLife();
	Settings settings{};

private:
	static constexpr std::uint32_t kMaxEmitters = 256;
	static constexpr std::size_t kMaxRegistryEntries = 1024;

	struct Emitter
	{
		RE::NiPoint3 position{};
		float radius{};
		float3 color{};
		std::uint32_t formID{};
		std::uint32_t worldspace{};
		std::uint32_t ownerID{};
		bool transient{};
		float lastSeen{};
	};

	struct alignas(16) GPUEmitter
	{
		float3 position{};
		float radius{};
		float3 color{};
		float distance{};
	};
	STATIC_ASSERT_ALIGNAS_16(GPUEmitter);
	static_assert(sizeof(GPUEmitter) == 32, "DistantLife emitter ABI mismatch");

	struct alignas(16) TuningData
	{
		float2 renderSize{};
		float2 invRenderSize{};
		float2 outputSize{};
		float2 invOutputSize{};
		float minimumDistance{};
		float maximumDistance{};
		float intensity{};
		float atmosphericAttenuation{};
		std::uint32_t emitterCount{};
		std::uint32_t debugMode{};
		std::uint32_t farFieldActivity{};
		float farFieldDensity{};
		float farFieldMotion{};
		float pad0{};
		float pad1{};
		float pad2{};
	};
	STATIC_ASSERT_ALIGNAS_16(TuningData);
	static_assert(sizeof(TuningData) == 80, "DistantLife tuning ABI mismatch");

	void DiscoverStaticLights(RE::PlayerCharacter* player);
	void DiscoverActorLights(RE::PlayerCharacter* player);
	void ObserveRuntimeLight(const RE::NiLight* light, const RE::Actor* owner, std::uint64_t salt, bool transient);
	void EnsureMask(std::uint32_t width, std::uint32_t height);
	ID3D11ComputeShader* GetBuildMaskShader();
	ID3D11ComputeShader* GetCompositeShader();

	std::unordered_map<std::uint64_t, Emitter> emitters;
	std::array<GPUEmitter, kMaxEmitters> upload{};
	winrt::com_ptr<ID3D11Buffer> emitterBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> emitterSRV;
	std::unique_ptr<ConstantBuffer> tuningCB;
	std::unique_ptr<Texture2D> mask;
	winrt::com_ptr<ID3D11ComputeShader> buildMaskCS;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS;
	std::uint32_t uploadedCount{};
	std::uint32_t worldspace{};
	float elapsedTime{};
	float scanCountdown{};
	bool shaderFailureLogged{};
};
