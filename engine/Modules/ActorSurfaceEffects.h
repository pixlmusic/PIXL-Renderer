#pragma once

#include "RenderModule.h"
#include "Buffer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Bone-local actor contamination driven by physical surface interactions.
 * Snow, mud and derived wetness persist in a specific animated bone's coordinate
 * frame, rather than in camera/world or actor-yaw projected coordinates.
 * An authored contact requires a live named skeleton bone; missing bones are
 * suppressed instead of projecting a stale mask onto unrelated equipment.
 * The public producer API and the 1360-byte CharacterRuntime shader ABI remain
 * unchanged. Future contamination types can reuse the event producer interface.
 */
struct ActorSurfaceEffects : RenderModule
{
	// This is an HLSL ABI capacity, not an artistic quality setting. CPU event
	// limits must never exceed CharacterRuntime.hlsli's 12-slot Events array.
	static constexpr std::uint32_t kShaderEventCapacity = 12u;
	enum class EffectType : std::uint32_t
	{
		Snow = 0,
		Mud = 1,
		Wetness = 2,
		// Values 3+ are intentionally reserved for future registered effects.
	};

	enum class DebugView : std::uint32_t
	{
		Off = 0,
		CombinedMask,
		Snow,
		Mud,
		Wetness,
		ContactLobes,
		ActorLocalCoordinates,
	};

	struct SurfaceInteractionEvent
	{
		EffectType type = EffectType::Snow;
		RE::NiPoint3 worldPosition{};
		RE::NiPoint3 worldVelocity{};
		float horizontalRadius = 1.0f;
		float verticalRadius = 1.0f;
		float intensity = 1.0f;
		float contactDepth = 0.0f;
		float splash = 0.0f;
	};

	struct Settings
	{
		bool Enable = true;
		bool EnableSnow = true;
		bool EnableMud = true;
		std::uint32_t EffectQuality = 2;  // Low/Medium/High/Cinematic
		float Persistence = 0.68f;
		float AccumulationStrength = 1.0f;

		// Advanced lifecycle controls are expressed as normalized amount/second.
		float SnowMeltRate = 0.010f;
		float MudDryRate = 0.007f;
		float DryMudFadeRate = 0.0025f;
		float WetnessDryRate = 0.014f;
		std::uint32_t MaximumAffectedNPCs = 24;
		float EffectDistance = 3200.0f;
		float MaskSoftness = 0.24f;
		float EdgeBreakup = 0.16f;
		DebugView Debug = DebugView::Off;
	};

	ActorSurfaceEffects();
	~ActorSurfaceEffects();

	ActorSurfaceEffects(const ActorSurfaceEffects&) = delete;
	ActorSurfaceEffects& operator=(const ActorSurfaceEffects&) = delete;

	virtual inline std::string GetName() override { return "Actor Surface Effects"; }
	virtual std::string GetDisplayName() override { return T("feature.actor_surface_effects.name", "Actor Surface Effects"); }
	virtual inline std::string GetShortName() override { return "ActorSurfaceEffects"; }
	virtual inline std::string_view GetShaderDefineName() override { return "ACTOR_SURFACE_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return ModuleGroups::kCharacters; }

	virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;
	virtual bool HasShaderDefine(RE::BSShader::Type a_type) override;
	virtual bool AffectsCachedShader(
		RE::BSShader::Type a_type,
		std::uint32_t a_descriptor,
		CachedShaderStage a_stage) override;

	virtual void SetupResources() override;
	virtual void Reset() override;
	virtual void Prepass() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	virtual void RestoreDefaultSettings() override;

	/** Installs the final character-geometry hook after DialogueFocus. */
	void InstallLateHooks();

	/**
	 * Accepts finite, actor-contact world-space events. Resolves a current named
	 * bone and captures the deposit in that bone's local coordinates. Repeated
	 * contacts accumulate without moving an already deposited mask centre.
	 * Returns false if the actor/skeleton/contact is unavailable or invalid.
	 */
	bool AddSurfaceEffect(RE::Actor* a_actor, const SurfaceInteractionEvent& a_event);

	/** Convenience bridge used by Ground Response's accepted body-contact path. */
	void AddGroundContact(
		RE::Actor* a_actor,
		EffectType a_type,
		const RE::NiPoint3& a_worldContactCenter,
		const RE::NiPoint3& a_worldVelocity,
		float a_horizontalRadius,
		float a_verticalRadius,
		float a_contactDepth,
		float a_intensity);

	/**
	 * Binds DialogueFocus + actor surface data. The GPU event transform is built
	 * on first draw of each actor, after the skeleton has reached its render pose,
	 * then reused for subsequent lighting draws that frame.
	 */
	void BindLightingGeometry(RE::BSRenderPass* a_pass);

	/** Applies the Characters quality contract, capped at shader event capacity. */
	void ApplyQualityTier(std::uint32_t a_quality);

	[[nodiscard]] std::uint32_t GetActiveActorCount() const;
	[[nodiscard]] std::uint32_t GetActiveEventCount() const;

	Settings settings{};

private:
	struct Runtime;
	std::unique_ptr<Runtime> runtime;
};
