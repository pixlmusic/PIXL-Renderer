#pragma once

#include "Buffer.h"

#include <cstdint>
#include <memory>

/**
 * Localized, actor-anchored material contamination driven by physical surface
 * interactions. Version 1 implements snow, mud and derived wetness while the
 * event API remains generic for future blood/frost/char/ash/residue producers.
 */
struct ActorSurfaceEffects : RenderModule
{
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
		std::uint32_t EffectQuality = 3;  // Low/Medium/High/Ultra
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

	/** Generic localized event API for Ground Response and future producers. */
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

	/** Binds a combined DialogueFocus + surface-effect character payload. */
	void BindLightingGeometry(RE::BSRenderPass* a_pass);

	/** Applies the Characters quality contract without changing artistic controls. */
	void ApplyQualityTier(std::uint32_t a_quality);

	[[nodiscard]] std::uint32_t GetActiveActorCount() const;
	[[nodiscard]] std::uint32_t GetActiveEventCount() const;

	Settings settings{};

private:
	struct Runtime;
	std::unique_ptr<Runtime> runtime;
};
