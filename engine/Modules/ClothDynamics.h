#pragma once

#include "Modules/ClothDynamicsState.h"
#include "RenderModule.h"

#include <cstdint>
#include <memory>

/** Experimental actor-health-driven clothing and armor surface wear. */
struct ClothDynamics : RenderModule
{
	enum class DebugMode : std::uint32_t
	{
		Off = 0,
		DamageSeverity,
		ScratchMask,
		TearMask
	};

	struct Settings
	{
		bool Enable = false;
		float ClothingTearStrength = 1.0f;
		float ArmorScratchStrength = 1.0f;
		float HealthInfluence = 1.0f;
		float ResponseSpeed = 3.0f;
		float ActorDistance = 1800.0f;
		DebugMode Debug = DebugMode::Off;
	};

	ClothDynamics();
	~ClothDynamics();
	ClothDynamics(const ClothDynamics&) = delete;
	ClothDynamics& operator=(const ClothDynamics&) = delete;

	std::string GetName() override { return "PIXL Clothing Wear & Damage"; }
	std::string GetDisplayName() override { return "PIXL Clothing Wear & Damage"; }
	std::string GetShortName() override { return "ClothDynamics"; }
	std::string_view GetShaderDefineName() override { return "CLOTHING_DAMAGE"; }
	std::string_view GetCategory() const override { return ModuleGroups::kCharacters; }
	std::pair<std::string, std::vector<std::string>> GetModuleSummary() override;
	bool HasShaderDefine(RE::BSShader::Type a_type) override;
	bool AffectsCachedShader(RE::BSShader::Type a_type, std::uint32_t a_descriptor, CachedShaderStage a_stage) override;

	void SetupResources() override;
	void Reset() override;
	void Prepass() override;
	void DrawSettings() override;
	void LoadSettings(json& a_json) override;
	void SaveSettings(json& a_json) override;
	void RestoreDefaultSettings() override;

	/** Installs an outer draw hook after ActorSurfaceEffects. */
	void InstallLateHooks();
	/** Publishes this actor's same-frame health damage before b13 is bound. */
	void PrepareActorForGeometry(RE::BSRenderPass* a_pass);
	bool GetDamageState(std::uint32_t a_formID, ClothingDamageState& a_out) const;

	[[nodiscard]] std::uint32_t GetActiveActorCount() const;
	[[nodiscard]] std::uint32_t GetUpdatedActorCount() const;

	Settings settings{};

private:
	struct Runtime;
	std::unique_ptr<Runtime> runtime;
};
