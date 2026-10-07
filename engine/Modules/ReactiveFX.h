// PIXL Renderer - physically reactive event and VFX framework.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once

#include "Buffer.h"
#include "RenderModule.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

struct ReactiveFX : RenderModule
{
	enum class SurfaceType : std::uint32_t
	{
		Unknown,
		Stone,
		Dirt,
		Mud,
		Snow,
		Ice,
		Grass,
		Wood,
		Metal,
		Flesh,
		Water,
		Sand,
		Ash
	};

	enum class SourceType : std::uint32_t
	{
		Unknown,
		Footstep,
		Blade,
		Blunt,
		Arrow,
		Fire,
		Frost,
		Shock,
		Restoration,
		Conjuration,
		Illusion,
		Shout,
		HeavyImpact,
		Dwemer,
		AmbientFire,
		AmbientCandle
	};

	enum class ImpulseType : std::uint32_t
	{
		Radial,
		Directional,
		TravellingWave
	};

	enum class ParticleType : std::uint32_t
	{
		Spark = 0,
		FrostCrystal = 1,
		Fragment = 2,
		SmokeDust = 3,
		SoftPuff = 4,
		MagicMote = 5,
		HotStreak = 6,
		LeafCard = 7
	};

	struct Settings
	{
		bool Enabled = false;
		std::uint32_t Quality = 2;
		float MaximumDistance = 12000.0f;
		float ParticleIntensity = 1.0f;
		float VegetationResponse = 1.0f;
		bool EnableParticleCollision = true;
		bool EnableVegetationResponse = true;
		bool EnableHeroDebris = false;
		bool EnableSecondaryImpacts = false;
		std::uint32_t DebugMode = 0;
	};

	struct Event
	{
		RE::NiPoint3 position{};
		RE::NiPoint3 direction{};
		float radius = 64.0f;
		float strength = 1.0f;
		SurfaceType surface = SurfaceType::Unknown;
		SourceType source = SourceType::Unknown;
		ImpulseType impulse = ImpulseType::Radial;
		std::uint32_t sourceFormID = 0;
		std::uint32_t seed = 0;
	};

	std::string GetName() override { return "PIXL Reactive FX"; }
	std::string GetDisplayName() override { return "PIXL Reactive FX"; }
	std::string GetShortName() override { return "ReactiveFX"; }
	std::string_view GetCategory() const override { return ModuleGroups::kOther; }
	std::string_view GetShaderDefineName() override { return "REACTIVE_FX"; }
	// ReactiveFX is a standalone compute/composite extension. It no longer
	// alters the grass vertex path; impulses remain available to GPU particles.
	bool HasShaderDefine(RE::BSShader::Type) override { return false; }
	bool AffectsCachedShader(RE::BSShader::Type, std::uint32_t, CachedShaderStage) override { return false; }
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;

	void SetupResources() override;
	void Prepass() override;
	void Reset() override;
	void DataLoaded() override;
	void DrawSettings() override;
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void RestoreDefaultSettings() override;
	void ClearShaderCache() override;

	void DrawReactiveFX();
	void QueueEvent(const Event& event);
	void ApplyImpulse(
		const RE::NiPoint3& position,
		const RE::NiPoint3& direction,
		float radius,
		float strength,
		float falloff,
		ImpulseType type);
	void QueueProjectileImpact(
		RE::Projectile* projectile,
		const RE::NiPoint3& position,
		const RE::NiPoint3& velocity,
		const RE::BGSMaterialType* material,
		RE::TESObjectREFR* target = nullptr);
	void QueueFootstep(
		const RE::NiPoint3& position,
		const RE::NiPoint3& velocity,
		SurfaceType surface,
		float intensity);
	void MarkReconstructionReactive(
		ID3D11UnorderedAccessView* target,
		std::uint32_t width,
		std::uint32_t height);

	static SurfaceType ClassifyMaterial(const RE::BGSMaterialType* material);
	static const char* SurfaceName(SurfaceType surface);
	static const char* SourceName(SourceType source);

	Settings settings{};

	struct HitEventSink : RE::BSTEventSink<RE::TESHitEvent>
	{
		static HitEventSink* GetSingleton();
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESHitEvent*,
			RE::BSTEventSource<RE::TESHitEvent>*) override;
	};

	struct SpellCastEventSink : RE::BSTEventSink<RE::TESSpellCastEvent>
	{
		static SpellCastEventSink* GetSingleton();
		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESSpellCastEvent*,
			RE::BSTEventSource<RE::TESSpellCastEvent>*) override;
	};

private:
	static constexpr std::uint32_t kMaximumParticles = 16384;
	static constexpr std::uint32_t kMaximumSpawnCommands = 2048;
	static constexpr std::uint32_t kMaximumQueuedEvents = 128;
	static constexpr std::uint32_t kMaximumImpulses = 16;
	static constexpr std::uint32_t kHeroParticleReserve = 256;

	struct alignas(16) GPUParticle
	{
		float3 position{};
		float age{};
		float3 velocity{};
		float lifetime{};
		float3 acceleration{};
		float drag{};
		float4 colorEmission{};
		float size{};
		float rotation{};
		float angularVelocity{};
		float restitution{};
		float friction{};
		float collisionThickness{};
		std::uint32_t type{};
		std::uint32_t flags{};
		std::uint32_t bounceCount{};
		std::uint32_t maxBounces{};
		float fadeIn{};
		float fadeOut{};
	};
	STATIC_ASSERT_ALIGNAS_16(GPUParticle);
	static_assert(sizeof(GPUParticle) == 112, "ReactiveFX particle ABI mismatch");

	struct alignas(16) SpawnCommand
	{
		std::uint32_t slot{};
		std::uint32_t pad0{};
		std::uint32_t pad1{};
		std::uint32_t pad2{};
		GPUParticle particle{};
	};
	STATIC_ASSERT_ALIGNAS_16(SpawnCommand);
	static_assert(sizeof(SpawnCommand) == 128, "ReactiveFX spawn ABI mismatch");

	struct alignas(16) GPUImpulse
	{
		float3 position{};
		float radius{};
		float3 direction{};
		float strength{};
		float age{};
		float previousAge{};
		float duration{};
		float waveSpeed{};
		std::uint32_t type{};
		std::uint32_t active{};
		float falloff{};
		float verticalInfluence{};
	};
	STATIC_ASSERT_ALIGNAS_16(GPUImpulse);
	static_assert(sizeof(GPUImpulse) == 64, "ReactiveFX impulse ABI mismatch");

	struct alignas(16) TuningData
	{
		float2 renderSize{};
		float2 invRenderSize{};
		float deltaTime{};
		float gravity{};
		float particleIntensity{};
		float maximumDistance{};
		std::uint32_t particleCapacity{};
		std::uint32_t spawnCount{};
		std::uint32_t collisionBudget{};
		std::uint32_t debugMode{};
		float collisionEnabled{};
		std::uint32_t activeImpulseCount{};
		std::uint32_t collisionPhase{};
		float opticalActive{};
	};
	STATIC_ASSERT_ALIGNAS_16(TuningData);
	static_assert(sizeof(TuningData) == 64, "ReactiveFX tuning ABI mismatch");

	struct ActiveImpulse
	{
		GPUImpulse gpu{};
	};

	struct Recipe
	{
		std::uint32_t primaryCount{};
		std::uint32_t secondaryCount{};
		float speedMin{};
		float speedMax{};
		float lifetimeMin{};
		float lifetimeMax{};
		float sizeMin{};
		float sizeMax{};
		float3 primaryColor{};
		float3 secondaryColor{};
		float gravityScale{};
		float drag{};
		float restitution{};
		float friction{};
		std::uint32_t primaryType{};
		std::uint32_t secondaryType{};
		bool additivePrimary{};
		bool additiveSecondary{};
		bool collisionPrimary{};
		bool collisionSecondary{};
		bool pooledDebris{};
	};

	void ProcessQueuedEvents();
	void QueueAmbientEmitters(float deltaTime);
	void SpawnEvent(const Event& event, float primaryScale = 1.0f, float secondaryScale = 1.0f);
	void AddImpulseForEvent(const Event& event);
	void UploadSpawnCommands();
	void UploadImpulses(float deltaTime);
	void EnsureMask(std::uint32_t width, std::uint32_t height);
	void EnsureSceneColorCopy(const D3D11_TEXTURE2D_DESC& sourceDesc, std::uint32_t width, std::uint32_t height);
	bool EnsureShaders();
	Recipe ResolveRecipe(SourceType source, SurfaceType surface) const;
	SourceType ClassifyMagic(const RE::MagicItem* magic) const;
	void QueueDebugEvent(SourceType source, SurfaceType surface, ImpulseType impulse);

	std::mutex eventMutex;
	std::array<std::vector<Event>, 2> eventQueues{};
	std::uint32_t producerQueue{};
	std::vector<SpawnCommand> spawnCommands;
	std::array<ActiveImpulse, kMaximumImpulses> impulses{};
		std::uint32_t nextParticleSlot{};
		std::uint32_t nextDebrisSlot{};
	std::uint32_t nextImpulseSlot{};
	std::uint32_t particleMaskFrame{ ~0u };
	std::atomic<std::uint32_t> droppedEvents{};
	std::uint32_t droppedParticles{};
	std::uint32_t lastEventSeed{ 1u };
	std::uint32_t activeImpulseCount{};
	std::uint32_t collisionPhase{};
	std::uint32_t activeQuality{ ~0u };
		float simulationTimeRemaining{};
		float opticalTimeRemaining{};
		float footstepCooldown{};
		float ambientScanCountdown{};
	SurfaceType lastSurface{ SurfaceType::Unknown };
	SourceType lastSource{ SourceType::Unknown };

	std::unique_ptr<Buffer> particles;
	winrt::com_ptr<ID3D11Buffer> spawnBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> spawnSRV;
	winrt::com_ptr<ID3D11Buffer> impulseBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView> impulseSRV;
	std::unique_ptr<ConstantBuffer> tuningCB;
	std::unique_ptr<Texture2D> particleMask;
	std::unique_ptr<Texture2D> sceneColorCopy;
	winrt::com_ptr<ID3D11ComputeShader> spawnCS;
	winrt::com_ptr<ID3D11ComputeShader> simulateCS;
	winrt::com_ptr<ID3D11ComputeShader> buildMaskCS;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS;
	winrt::com_ptr<ID3D11ComputeShader> reconstructionMaskCS;
	bool shaderCompilationAttempted{};
	bool resourceCreationAttempted{};
	bool reactivePassReady{};
	bool resourceFailureLogged{};
	bool eventSinksRegistered{};
	bool wasEnabled{};
	float resourceRetryDelay{};
};
