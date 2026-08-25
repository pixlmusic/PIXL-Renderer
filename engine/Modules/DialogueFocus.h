#pragma once

#include "Buffer.h"
#include "Globals.h"
#include "Hooks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @brief Dialogue-specific renderer focus service.
 *
 * This intentionally does NOT derive from RenderModule in the first implementation.
 * It has no ModuleVersions/INI dependency and does not alter SharedData::FeatureData (b6),
 * so the established PIXL feature-buffer ABI remains byte-for-byte unchanged.
 *
 * Runtime detection uses Skyrim's MenuTopicManager speaker/currentTopicInfo pair, with
 * SubtitleManager and the documented lastSpeaker/lastTopicInfo close-tail as guarded
 * fallbacks. Only the focused actor's Lighting geometry receives the private PS b13
 * payload. Landscape b13 remains owned by GroundResponse.
 */
class DialogueFocus
{
public:
	static DialogueFocus& GetSingleton()
	{
		static DialogueFocus singleton;
		return singleton;
	}

	static constexpr std::uint32_t kMagic = 0x434F4644u;    // "DFOC" in little endian
	static constexpr std::uint32_t kVersion = 0x00010000u;  // 1.0

	struct Settings
	{
		bool Enabled = true;

		// Temporal hysteresis. Dialogue should feel like quality naturally settles in,
		// never like a renderer mode visibly switches.
		float FadeInSeconds = 0.32f;
		float FadeOutSeconds = 0.85f;
		float ClosedLineGraceSeconds = 2.50f;

		// Upper-body spatial focus in Skyrim world units.
		float HeadRadius = 92.0f;
		float ShoulderRadius = 138.0f;

		// Conservative quality weights consumed by Lighting.hlsl.
		float SkinQuality = 0.78f;
		float EyeQuality = 1.00f;
		float HairQuality = 0.72f;
		float TissueQuality = 0.55f;

		float ContactShadowQuality = 0.42f;
		float LocalLightingQuality = 0.30f;  // reserved for the local-light phase
		float MicroDetailQuality = 0.52f;
		float EyeReflectionQuality = 0.38f;

		// Populated/reserved for the environmental FX phase. Kept in the ABI now so
		// breath/steam can be added without changing the character Lighting payload.
		float Coldness = 0.0f;
		float Wetness = 0.0f;
		float BreathStrength = 0.0f;
		float SteamStrength = 0.0f;
	};

	struct alignas(16) GPUData
	{
		std::uint32_t Magic = 0;
		std::uint32_t Version = 0;
		float FocusBlend = 0.0f;
		float SessionActive = 0.0f;

		float4 HeadPositionRadius = { 0.0f, 0.0f, 0.0f, 0.0f };
		float4 ShoulderPositionRadius = { 0.0f, 0.0f, 0.0f, 0.0f };

		// x skin, y eye, z hair, w tissue
		float4 Quality = { 0.0f, 0.0f, 0.0f, 0.0f };

		// x contact shadows, y local lighting, z microdetail, w eye reflections
		float4 Lighting = { 0.0f, 0.0f, 0.0f, 0.0f };

		// x coldness, y wetness, z breath, w shoulder/body steam
		float4 Environment = { 0.0f, 0.0f, 0.0f, 0.0f };
	};
	static_assert((sizeof(GPUData) % 16) == 0, "DialogueFocus GPUData must be 16-byte aligned.");
	static_assert(sizeof(GPUData) == 96, "DialogueFocus GPUData must match DialogueFocus.hlsli.");

	Settings settings{};

	void SetupResources()
	{
		if (focusCB || neutralCB)
			return;

		focusCB = eastl::make_unique<ConstantBuffer>(
			ConstantBufferDesc<GPUData>(), "DialogueFocus::FocusCB");
		neutralCB = eastl::make_unique<ConstantBuffer>(
			ConstantBufferDesc<GPUData>(), "DialogueFocus::NeutralCB");

		// Zero/invalid magic is deliberate. If a non-focused actor is rendered after
		// the focused actor, the shader sees a guaranteed inert payload.
		GPUData neutral{};
		neutralCB->Update(neutral);
		focusCB->Update(neutral);

		logger::debug("[DialogueFocus] Runtime resources initialized (PS b13 character-only ABI v1).");
	}

	void InstallHooks()
	{
		if (hooksInstalled)
			return;

		Hooks::Install();
		hooksInstalled = true;
	}

	/**
	 * @brief Advances dialogue detection and the smooth focus envelope once per frame.
	 *
	 * The menu itself keeps quality focus active between spoken lines. Once the menu
	 * closes, currentTopicInfo/currentSpeaker can keep the final voiced line alive;
	 * lastSpeaker/lastTopicInfo are accepted only for a bounded grace interval.
	 */
	void Update(float a_deltaTime)
	{
		if (!settings.Enabled) {
			Deactivate(std::clamp(a_deltaTime, 0.0f, 0.10f));
			return;
		}

		auto* ui = globals::game::ui;
		const bool menuOpen = ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
		const float dt = std::clamp(a_deltaTime, 0.0f, 0.10f);

		// Transition traces remain available in developer/debug logs without
		// turning ordinary dialogue into release-log traffic.
		if (menuOpen != wasDialogueMenuOpen) {
			if (menuOpen) {
				logger::debug("[DialogueFocus] Dialogue Menu OPEN detected.");
				unresolvedSpeakerLogged = false;
				focusedBindingObserved = false;
			} else {
				logger::debug("[DialogueFocus] Dialogue Menu CLOSED detected.");
				closeTailRemaining = std::max(closeTailRemaining, settings.ClosedLineGraceSeconds);
			}
		}
		wasDialogueMenuOpen = menuOpen;

		auto* topicManager = RE::MenuTopicManager::GetSingleton();

		RE::ActorHandle candidate{};
		bool voiceSignal = false;
		const bool currentTopicSignal = topicManager && topicManager->currentTopicInfo;
		const bool greetingSignal = topicManager && topicManager->isGreetingPlayer;

		// Strongest signal: currentTopicInfo is documented as valid while the NPC is
		// actively speaking. Prefer the current speaker, but accept lastSpeaker if the
		// engine has already rotated the handle while the voiced line is still live.
		if (currentTopicSignal && topicManager) {
			RE::ActorHandle currentSpeaker = ResolveActor(topicManager->speaker);
			if (IsUsableSpeaker(currentSpeaker, true)) {
				candidate = currentSpeaker;
				voiceSignal = true;
			} else {
				RE::ActorHandle recentSpeaker = ResolveActor(topicManager->lastSpeaker);
				if (IsUsableSpeaker(recentSpeaker, true)) {
					candidate = recentSpeaker;
					voiceSignal = true;
				}
			}
		}

		// Greeting state is another engine-owned conversation signal and is useful
		// for dialogue variants that do not keep currentTopicInfo populated continuously.
		// Do not reference newer goodbye-state fields here: PIXL currently builds against the
		// CommonLibSSE-NG v4.26.1 prebuilt, whose MenuTopicManager does not expose it.
		if (!candidate.get() && topicManager && greetingSignal) {
			RE::ActorHandle currentSpeaker = ResolveActor(topicManager->speaker);
			if (!currentSpeaker.get())
				currentSpeaker = ResolveActor(topicManager->lastSpeaker);
			if (IsUsableSpeaker(currentSpeaker, true)) {
				candidate = currentSpeaker;
				voiceSignal = true;
			}
		}

		// During the open Dialogue Menu, keep the conversation actor important even
		// in pauses between response lines. Some dialogue flows temporarily clear
		// speaker before the next response, so lastSpeaker is a safe same-session
		// fallback while the actual Dialogue Menu is open.
		if (!candidate.get() && menuOpen && topicManager) {
			RE::ActorHandle currentSpeaker = ResolveActor(topicManager->speaker);
			if (IsUsableSpeaker(currentSpeaker, true)) {
				candidate = currentSpeaker;
			} else {
				RE::ActorHandle recentSpeaker = ResolveActor(topicManager->lastSpeaker);
				if (IsUsableSpeaker(recentSpeaker, true))
					candidate = recentSpeaker;
			}
		}

		// SubtitleManager is useful for voice tails and for modded dialogue UI flows.
		// Post-close it may also see unrelated ambient speech, so only accept a new
		// subtitle actor while the Dialogue Menu is open or before any speaker has
		// been latched.
		if (auto* subtitleManager = RE::SubtitleManager::GetSingleton()) {
			RE::ActorHandle subtitleSpeaker = ResolveActor(subtitleManager->currentSpeaker);
			auto subtitleActor = subtitleSpeaker.get();
			if (subtitleActor && subtitleActor.get() != RE::PlayerCharacter::GetSingleton()) {
				if (menuOpen || focusedFormID == 0) {
					if (!candidate.get()) {
						candidate = subtitleSpeaker;
						voiceSignal = true;
					}
				} else if (sessionLatched &&
						   focusedFormID != 0 &&
						   subtitleActor->GetFormID() == focusedFormID) {
					candidate = subtitleSpeaker;
					voiceSignal = true;
				}
			}
		}

		// Documented close-tail fallback. It cannot introduce a new actor after close:
		// only the already-focused FormID may own the grace period.
		if (!menuOpen &&
			sessionLatched &&
			!candidate.get() &&
			closeTailRemaining > 0.0f &&
			topicManager &&
			topicManager->lastTopicInfo) {
			RE::ActorHandle lastSpeakerHandle = ResolveActor(topicManager->lastSpeaker);
			auto lastSpeakerActor = lastSpeakerHandle.get();
			if (lastSpeakerActor &&
				focusedFormID != 0 &&
				lastSpeakerActor->GetFormID() == focusedFormID) {
				candidate = lastSpeakerHandle;
				voiceSignal = true;
			}
		}

		// DialogueMenu can become visible one frame before its speaker handle resolves.
		if (menuOpen && !candidate.get() && focusedFormID != 0) {
			auto previous = focusedHandle.get();
			if (previous)
				candidate = focusedHandle;
		}

		auto candidateActor = candidate.get();
		const bool candidateValid =
			candidateActor &&
			candidateActor.get() != RE::PlayerCharacter::GetSingleton();

		if (menuOpen && candidateValid)
			sessionLatched = true;

		// A normal-info diagnostic if Skyrim says dialogue is open but neither manager
		// can resolve an actor. Emit once per menu session, never once per frame.
		if (menuOpen && !candidateValid && !unresolvedSpeakerLogged) {
			const bool topicSpeakerValid =
				topicManager && static_cast<bool>(ResolveActor(topicManager->speaker).get());
			const bool lastSpeakerValid =
				topicManager && static_cast<bool>(ResolveActor(topicManager->lastSpeaker).get());
			logger::warn(
				"[DialogueFocus] Dialogue Menu is open but no NPC speaker resolved. "
				"topicManager={} speaker={} lastSpeaker={} currentTopic={} greeting={}",
				topicManager != nullptr,
				topicSpeakerValid,
				lastSpeakerValid,
				currentTopicSignal,
				greetingSignal);
			unresolvedSpeakerLogged = true;
		}

		if (!menuOpen) {
			if (voiceSignal)
				closeTailRemaining = std::max(closeTailRemaining, 0.35f);
			else
				closeTailRemaining = std::max(0.0f, closeTailRemaining - dt);
		}

		const bool wantFocus =
			candidateValid &&
			(menuOpen || voiceSignal || (sessionLatched && closeTailRemaining > 0.0f));

		if (wantFocus) {
			const std::uint32_t newFormID = candidateActor->GetFormID();
			if (focusedFormID != newFormID) {
				focusedHandle = candidate;
				focusedFormID = newFormID;
				focusedBindingObserved = false;
				logger::debug("[DialogueFocus] ACTIVE speaker={:08X} name=\"{}\" menu={} voice={} greeting={}",
					focusedFormID,
					candidateActor->GetName(),
					menuOpen,
					voiceSignal,
					greetingSignal);
			} else {
				focusedHandle = candidate;
			}
		}

		const float target = wantFocus ? 1.0f : 0.0f;
		const float timeConstant = target > focusBlend ? settings.FadeInSeconds : settings.FadeOutSeconds;
		focusBlend = ExponentialApproach(focusBlend, target, dt, timeConstant);

		if (!wantFocus && focusBlend < 1.0e-3f) {
			if (focusedFormID != 0)
				logger::debug("[DialogueFocus] RELEASED speaker={:08X}.", focusedFormID);
			focusBlend = 0.0f;
			focusedHandle = {};
			focusedFormID = 0;
			sessionLatched = false;
			closeTailRemaining = 0.0f;
			focusedBindingObserved = false;
		}

		PublishGPUData(wantFocus);
	}

	/**
	 * @brief Binds DialogueFocus only for actor-owned Lighting geometry.
	 *
	 * This is the key b13 safety rule. Non-actor geometry (especially landscape)
	 * is never touched, so GroundResponse keeps complete ownership of its landscape
	 * b13 runtime. Every detected non-focused actor receives the neutral buffer to
	 * prevent a focused payload leaking into the next character draw.
	 */
	void BindLightingGeometry(RE::BSRenderPass* a_pass)
	{
		if (!a_pass || !a_pass->geometry || !globals::d3d::context || !focusCB || !neutralCB)
			return;

		auto* ref = a_pass->geometry->GetUserData();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor)
			return;

		const bool isFocused =
			focusBlend > 1.0e-4f &&
			focusedFormID != 0 &&
			actor->GetFormID() == focusedFormID;

		ID3D11Buffer* buffer = isFocused ? focusCB->CB() : neutralCB->CB();
		globals::d3d::context->PSSetConstantBuffers(13, 1, &buffer);

		if (isFocused && !focusedBindingObserved) {
			focusedBindingObserved = true;
			logger::debug(
				"[DialogueFocus] GPU BIND confirmed speaker={:08X} blend={:.3f} (PS b13).",
				focusedFormID,
				focusBlend);
		}
	}

	[[nodiscard]] bool IsActive() const noexcept
	{
		return focusBlend > 1.0e-3f && focusedFormID != 0;
	}

	[[nodiscard]] float GetBlend() const noexcept { return focusBlend; }
	[[nodiscard]] std::uint32_t GetFocusedFormID() const noexcept { return focusedFormID; }

private:
	DialogueFocus() = default;

	static RE::ActorHandle ResolveActor(const RE::ObjectRefHandle& a_handle)
	{
		auto ref = a_handle.get();
		if (!ref)
			return {};

		if (auto* actor = ref->As<RE::Actor>())
			return RE::ActorHandle(actor);

		return {};
	}

	static bool IsFinite(const RE::NiPoint3& a_pos)
	{
		return std::isfinite(a_pos.x) && std::isfinite(a_pos.y) && std::isfinite(a_pos.z);
	}

	bool IsUsableSpeaker(const RE::ActorHandle& a_handle, bool a_allowNewSpeaker) const
	{
		auto actor = a_handle.get();
		if (!actor || actor.get() == RE::PlayerCharacter::GetSingleton())
			return false;

		// Outside the open menu, never switch to an unrelated actor because of a
		// transient manager/subtitle state. The latched speaker owns the close tail.
		return a_allowNewSpeaker || focusedFormID == 0 || actor->GetFormID() == focusedFormID;
	}

	static float ExponentialApproach(float a_current, float a_target, float a_dt, float a_seconds)
	{
		if (a_dt <= 0.0f)
			return a_current;

		const float tau = std::max(a_seconds, 1.0e-3f);
		const float alpha = 1.0f - std::exp(-a_dt / tau);
		return std::clamp(a_current + (a_target - a_current) * alpha, 0.0f, 1.0f);
	}

	void Deactivate(float a_dt)
	{
		focusBlend = ExponentialApproach(focusBlend, 0.0f, a_dt, settings.FadeOutSeconds);
		if (focusBlend < 1.0e-3f) {
			focusBlend = 0.0f;
			focusedHandle = {};
			focusedFormID = 0;
			sessionLatched = false;
			closeTailRemaining = 0.0f;
		}
		PublishGPUData(false);
	}

	void GetActorAnchors(RE::Actor* a_actor, RE::NiPoint3& a_head, RE::NiPoint3& a_shoulders) const
	{
		const RE::NiPoint3 actorPos = a_actor->GetPosition();
		float height = a_actor->GetHeight();
		if (!std::isfinite(height) || height < 48.0f || height > 320.0f)
			height = 128.0f;

		a_head = actorPos;
		a_head.z += height * 0.86f;
		a_shoulders = actorPos;
		a_shoulders.z += height * 0.70f;

		static const RE::BSFixedString kHeadNode("NPC Head [Head]");
		static const RE::BSFixedString kNeckNode("NPC Neck [Neck]");
		static const RE::BSFixedString kSpine2Node("NPC Spine2 [Spn2]");

		if (auto* head = a_actor->GetNodeByName(kHeadNode)) {
			if (IsFinite(head->world.translate))
				a_head = head->world.translate;
		}

		if (auto* neck = a_actor->GetNodeByName(kNeckNode)) {
			if (IsFinite(neck->world.translate))
				a_shoulders = neck->world.translate;
		} else if (auto* spine = a_actor->GetNodeByName(kSpine2Node)) {
			if (IsFinite(spine->world.translate))
				a_shoulders = spine->world.translate;
		}
	}

	void PublishGPUData(bool a_sessionActive)
	{
		if (!focusCB)
			return;

		GPUData data{};

		auto focusedActor = focusedHandle.get();
		if (focusedActor && focusBlend > 0.0f) {
			RE::NiPoint3 head{};
			RE::NiPoint3 shoulders{};
			GetActorAnchors(focusedActor.get(), head, shoulders);

			const float4 cameraAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();

			data.Magic = kMagic;
			data.Version = kVersion;
			data.FocusBlend = focusBlend;
			data.SessionActive = a_sessionActive ? 1.0f : 0.0f;

			data.HeadPositionRadius = {
				head.x - cameraAdjust.x,
				head.y - cameraAdjust.y,
				head.z - cameraAdjust.z,
				std::max(settings.HeadRadius, 1.0f)
			};
			data.ShoulderPositionRadius = {
				shoulders.x - cameraAdjust.x,
				shoulders.y - cameraAdjust.y,
				shoulders.z - cameraAdjust.z,
				std::max(settings.ShoulderRadius, 1.0f)
			};

			data.Quality = {
				std::clamp(settings.SkinQuality, 0.0f, 1.0f),
				std::clamp(settings.EyeQuality, 0.0f, 1.0f),
				std::clamp(settings.HairQuality, 0.0f, 1.0f),
				std::clamp(settings.TissueQuality, 0.0f, 1.0f)
			};
			data.Lighting = {
				std::clamp(settings.ContactShadowQuality, 0.0f, 1.0f),
				std::clamp(settings.LocalLightingQuality, 0.0f, 1.0f),
				std::clamp(settings.MicroDetailQuality, 0.0f, 1.0f),
				std::clamp(settings.EyeReflectionQuality, 0.0f, 1.0f)
			};
			data.Environment = {
				std::clamp(settings.Coldness, 0.0f, 1.0f),
				std::clamp(settings.Wetness, 0.0f, 1.0f),
				std::clamp(settings.BreathStrength, 0.0f, 1.0f),
				std::clamp(settings.SteamStrength, 0.0f, 1.0f)
			};
		}

		focusCB->Update(data);
	}

	eastl::unique_ptr<ConstantBuffer> focusCB = nullptr;
	eastl::unique_ptr<ConstantBuffer> neutralCB = nullptr;

	RE::ActorHandle focusedHandle{};
	std::uint32_t focusedFormID = 0;

	float focusBlend = 0.0f;
	float closeTailRemaining = 0.0f;
	bool sessionLatched = false;
	bool wasDialogueMenuOpen = false;
	bool unresolvedSpeakerLogged = false;
	bool focusedBindingObserved = false;
	bool hooksInstalled = false;

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags)
			{
				// Chain Skyrim and every previously installed PIXL geometry hook first.
				// Lighting setup may rewrite PS constant-buffer bindings, so DialogueFocus
				// must bind b13 last, immediately before control returns to the draw path.
				func(a_this, a_pass, a_renderFlags);
				DialogueFocus::GetSingleton().BindLightingGeometry(a_pass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			logger::info("[DialogueFocus] Installed BSLightingShader geometry hook.");
		}
	};
};
