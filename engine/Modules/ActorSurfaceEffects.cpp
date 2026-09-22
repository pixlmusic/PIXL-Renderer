#include "ActorSurfaceEffects.h"

#include "Globals.h"
#include "Modules/DialogueFocus.h"
#include "Modules/RainResponse.h"
#include "State.h"
#include "WeatherManager.h"
#include "Utils/UI.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace
{
	constexpr std::uint32_t kActorSurfaceMagic = 0x46555341u;    // "ASUF"
	constexpr std::uint32_t kActorSurfaceVersion = 0x00010001u;  // bone-anchored events
	constexpr std::size_t kMaximumGPUEvents = 12;
	constexpr float kEventEpsilon = 1.0e-4f;

	float Saturate(float a_value)
	{
		return std::clamp(std::isfinite(a_value) ? a_value : 0.0f, 0.0f, 1.0f);
	}

	bool IsFinite(const RE::NiPoint3& a_value)
	{
		return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
	}

	float LengthSquared(const RE::NiPoint3& a_value)
	{
		return a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z;
	}

	RE::NiPoint3 ResolveActorRenderOrigin(RE::Actor* a_actor)
	{
		if (!a_actor)
			return {};

		// Actor/Get3D reference roots can remain on the support plane while the
		// animated skeleton has already moved vertically (most visibly during a
		// jump). Lighting receives the skinned skeleton result, so anchor persistent
		// contamination to Skyrim's live NPC Root node first. This keeps a deposit
		// attached to boots/armour instead of leaving it projected at ground height.
		static const RE::BSFixedString kNpcRootNode("NPC Root [Root]");
		if (auto* skeletonRoot = a_actor->GetNodeByName(kNpcRootNode);
			skeletonRoot && IsFinite(skeletonRoot->world.translate)) {
			return skeletonRoot->world.translate;
		}

		// Creatures and unusual skeletons may not expose the humanoid node. Their
		// loaded scene root remains the safest renderer-owned fallback.
		if (auto* root = a_actor->Get3D(); root && IsFinite(root->world.translate))
			return root->world.translate;

		return a_actor->GetPosition();
	}

	RE::NiPoint3 ToActorLocal(RE::Actor* a_actor, const RE::NiPoint3& a_world)
	{
		const RE::NiPoint3 origin = ResolveActorRenderOrigin(a_actor);
		const float yaw = a_actor->GetAngleZ();
		const float cosine = std::cos(yaw);
		const float sine = std::sin(yaw);
		const float dx = a_world.x - origin.x;
		const float dy = a_world.y - origin.y;
		return {
			cosine * dx + sine * dy,
			-sine * dx + cosine * dy,
			a_world.z - origin.z
		};
	}

	std::uint32_t QualityEventLimit(std::uint32_t a_quality)
	{
		constexpr std::array<std::uint32_t, 4> limits{ 4u, 6u, 8u, 12u };
		return limits[std::min<std::uint32_t>(a_quality, 3u)];
	}

	std::uint32_t QualityActorLimit(std::uint32_t a_quality)
	{
		constexpr std::array<std::uint32_t, 4> limits{ 8u, 14u, 20u, 32u };
		return limits[std::min<std::uint32_t>(a_quality, 3u)];
	}

	RE::Actor* ResolveOwningActor(RE::NiAVObject* a_object)
	{
		constexpr std::uint32_t kMaximumSkinBonesToInspect = 256u;

		auto resolveHierarchy = [](RE::NiAVObject* a_root) -> RE::Actor* {
			RE::NiAVObject* current = a_root;
			for (std::uint32_t depth = 0; current && depth < 64u; ++depth) {
				if (auto* owner = current->GetUserData()) {
					if (auto* actor = owner->As<RE::Actor>())
						return actor;
				}
				current = current->parent;
			}
			return nullptr;
		};

		// Skin geometry normally owns the actor directly. Equipped dismember armour
		// can instead keep an armour-instance rootParent while its skin bones point
		// into the actor's live skeleton. Resolve all three engine-owned paths on the
		// current draw so equipment swaps cannot leave stale mesh/actor associations.
		if (auto* actor = resolveHierarchy(a_object))
			return actor;
		if (a_object) {
			if (auto* geometry = a_object->AsGeometry()) {
				if (auto* skin = geometry->GetGeometryRuntimeData().skinInstance.get()) {
					if (auto* actor = resolveHierarchy(skin->rootParent))
						return actor;

					// BSDismemberSkinInstance commonly reaches the owning actor only
					// through the remapped skeleton bones. numMatrices is engine-owned
					// for the duration of SetupGeometry; cap traversal defensively so a
					// malformed/modded skin cannot cause an unbounded render-thread walk.
					if (skin->bones) {
						const std::uint32_t boneCount =
							std::min(skin->numMatrices, kMaximumSkinBonesToInspect);
						for (std::uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
							if (auto* actor = resolveHierarchy(skin->bones[boneIndex]))
								return actor;
						}
					}
				}
			}
		}
		return nullptr;
	}

	struct alignas(16) ActorSurfaceGPUEvent
	{
		float4 LocalCenterRadius{};
		float4 VerticalAmounts{};  // x vertical radius, y fresh snow, z melting snow, w wet mud
		float4 State{};            // x dry mud, y wetness, z stable seed, w splash
		std::array<float4, 3> WorldToDeposit{}; // camera-relative world -> contact reference frame
	};
	static_assert(sizeof(ActorSurfaceGPUEvent) == 96);

	struct alignas(16) CharacterRuntimeGPUData
	{
		// The first 96 bytes are the immutable DialogueFocus v1 ABI.
		DialogueFocus::GPUData Dialogue{};

		std::uint32_t Magic = 0;
		std::uint32_t Version = 0;
		std::uint32_t EventCount = 0;
		std::uint32_t Flags = 0;

		float4 ActorOriginScale{};   // camera-relative xyz, actor scale
		float4 ActorRotationHeight{}; // cos(yaw), sin(yaw), actor height, reserved
		float4 Tuning{};             // strength, softness, breakup, debug mode
		float4 SnowAppearance{};     // linear snow tint rgb, material strength

		std::array<ActorSurfaceGPUEvent, kMaximumGPUEvents> Events{};
	};
	static_assert(offsetof(CharacterRuntimeGPUData, Magic) == sizeof(DialogueFocus::GPUData));
	static_assert(sizeof(CharacterRuntimeGPUData) == 1328, "CharacterRuntimeGPUData must match CharacterRuntime.hlsli");
	static_assert(offsetof(CharacterRuntimeGPUData, Events) == 176);

	struct LocalEffectLobe
	{
		RE::NiPoint3 localCenter{};
		RE::BSFixedString anchorBone;
		RE::NiMatrix3 referenceRotation;
		RE::NiPoint3 referenceOrigin{};
		float referenceScale = 1.0f;
		float horizontalRadius = 1.0f;
		float verticalRadius = 1.0f;
		float snowFresh = 0.0f;
		float snowMelting = 0.0f;
		float mudWet = 0.0f;
		float mudDry = 0.0f;
		float wetness = 0.0f;
		float seed = 0.0f;
		float splash = 0.0f;
		float ageSeconds = 0.0f;
		float lastContactSeconds = 0.0f;

		[[nodiscard]] float TotalAmount() const
		{
			return std::max({ snowFresh, snowMelting, mudWet, mudDry, wetness });
		}
	};

	struct ActorEffectState
	{
		RE::ActorHandle handle{};
		std::uint32_t formID = 0;
		std::vector<LocalEffectLobe> lobes{};
		std::unique_ptr<ConstantBuffer> constantBuffer{};
		float lastInteractionSeconds = 0.0f;
		float lastVisibleSeconds = 0.0f;
		bool prepared = false;
	};

	RE::NiAVObject* FindContactBone(RE::Actor* actor, const RE::NiPoint3& position, float reach)
	{
		static const std::array<RE::BSFixedString, 13> names{
			"NPC L Foot [Lft ]", "NPC R Foot [Rft ]", "NPC L Calf [LClf]", "NPC R Calf [RClf]",
			"NPC L Thigh [LThg]", "NPC R Thigh [RThg]", "NPC Pelvis [Pelv]",
			"NPC Spine2 [Spn2]", "NPC Head [Head]", "NPC L Hand [LHnd]", "NPC R Hand [RHnd]",
			"NPC L Forearm [LLar]", "NPC R Forearm [RLar]" };
		RE::NiAVObject* nearest = nullptr;
		float best = reach * reach;
		for (const auto& name : names) {
			auto* bone = actor->GetNodeByName(name);
			if (!bone || !IsFinite(bone->world.translate) || !std::isfinite(bone->world.scale) || bone->world.scale <= 0.001f)
				continue;
			const float distance = position.GetSquaredDistance(bone->world.translate);
			if (distance < best) { best = distance; nearest = bone; }
		}
		return nearest;
	}

	bool DepositFrame(RE::Actor* actor, const LocalEffectLobe& lobe, RE::NiMatrix3& rotation, RE::NiPoint3& origin)
	{
		if (lobe.anchorBone.empty())
			return false;
		auto* bone = actor->GetNodeByName(lobe.anchorBone);
		if (!bone || !IsFinite(bone->world.translate) || !std::isfinite(bone->world.scale) || bone->world.scale <= 0.001f)
			return false;
		rotation = (lobe.referenceRotation * bone->world.rotate.Transpose()) * (lobe.referenceScale / bone->world.scale);
		for (const auto& row : rotation.entry)
			for (const float value : row)
				if (!std::isfinite(value))
					return false;
		origin = bone->world.translate;
		return true;
	}

	void IndexActorSkeleton(
		RE::Actor* a_actor,
		std::uint32_t a_formID,
		std::unordered_map<const RE::NiAVObject*, std::uint32_t>& a_owners)
	{
		if (!a_actor || a_formID == 0u)
			return;

		auto* root = a_actor->Get3D();
		if (!root)
			return;

		// The index is rebuilt every Prepass and therefore never outlives the
		// current loaded actor scene graph. Dismember armour commonly has no actor
		// userData on its own root, but its NiSkinInstance bones are nodes from this
		// graph. Indexing those nodes gives the render hook a deterministic owner
		// without caching armour meshes or performing a global actor scan per draw.
		std::vector<RE::NiAVObject*> pending{ root };
		constexpr std::size_t kMaximumIndexedNodesPerActor = 4096u;
		for (std::size_t cursor = 0;
			cursor < pending.size() && cursor < kMaximumIndexedNodesPerActor;
			++cursor) {
			auto* object = pending[cursor];
			if (!object)
				continue;
			a_owners.try_emplace(object, a_formID);
			if (auto* node = object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (child)
						pending.push_back(child.get());
				}
			}
		}
	}

	struct ActorSurfaceSetupGeometryHook
	{
		static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags)
		{
			// DialogueFocus is installed first. Its hook runs in this chain and binds
			// the legacy 96-byte payload; contaminated actors then replace it with the
			// extended payload whose prefix is byte-identical.
			func(a_this, a_pass, a_renderFlags);
			globals::pipeline::actorSurfaceEffects.BindLightingGeometry(a_pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

struct ActorSurfaceEffects::Runtime
{
	std::unordered_map<std::uint32_t, std::unique_ptr<ActorEffectState>> actors{};
	std::unordered_map<const RE::NiAVObject*, std::uint32_t> activeSkeletonOwners{};
	std::unique_ptr<ConstantBuffer> neutralBuffer{};
	std::unique_ptr<ConstantBuffer> dialogueOnlyBuffer{};
	mutable std::mutex mutex{};
	float elapsedSeconds = 0.0f;
	std::uint32_t droppedEvents = 0;
	std::uint32_t evictedActors = 0;
	bool hooksInstalled = false;
	bool resourceFailureLogged = false;
};

ActorSurfaceEffects::ActorSurfaceEffects() : runtime(std::make_unique<Runtime>()) {}
ActorSurfaceEffects::~ActorSurfaceEffects() = default;

std::pair<std::string, std::vector<std::string>> ActorSurfaceEffects::GetModuleSummary()
{
	return {
		"Adds actor-anchored snow, mud and melt wetness from real Ground Response contacts.",
		{
			"Localized boots, legs and contacted body regions instead of biome-wide tinting",
			"Frame-rate-independent snow melt, mud drying and wetness recovery",
			"One logical state across player first person, third person and equipment changes",
			"Bounded nearby-NPC pool with no per-actor render targets or extra draw calls",
			"Generic event foundation for future blood, frost, char, ash and spell residue"
		}
	};
}

bool ActorSurfaceEffects::HasShaderDefine(RE::BSShader::Type a_type)
{
	return a_type == RE::BSShader::Type::Lighting;
}

bool ActorSurfaceEffects::AffectsCachedShader(
	RE::BSShader::Type a_type,
	std::uint32_t a_descriptor,
	CachedShaderStage a_stage)
{
	if (a_type != RE::BSShader::Type::Lighting || a_stage != CachedShaderStage::Pixel)
		return false;

	const std::uint32_t technique = (a_descriptor >> 24u) & 0x3Fu;
	const bool skinned = (a_descriptor & (1u << 1u)) != 0u;
	// FaceGen skin/RGB, hair and eye techniques can be actor-owned even when a
	// particular Skyrim permutation omits the generic Skinned descriptor bit.
	return skinned || technique == 4u || technique == 5u || technique == 6u || technique == 16u;
}

void ActorSurfaceEffects::SetupResources()
{
	if (!runtime || !globals::d3d::device)
		return;

	try {
		runtime->neutralBuffer = std::make_unique<ConstantBuffer>(
			ConstantBufferDesc<CharacterRuntimeGPUData>(),
			"ActorSurfaceEffects::NeutralCharacterRuntime");
		runtime->dialogueOnlyBuffer = std::make_unique<ConstantBuffer>(
			ConstantBufferDesc<CharacterRuntimeGPUData>(),
			"ActorSurfaceEffects::DialogueCharacterRuntime");
		const CharacterRuntimeGPUData neutral{};
		runtime->neutralBuffer->Update(neutral);
		runtime->dialogueOnlyBuffer->Update(neutral);
		logger::info("[ActorSurfaceEffects] Ready (actor-local analytical masks, 752-byte shared character ABI)");
	} catch (const std::exception& error) {
		failedLoadedMessage = std::format("Actor Surface Effects disabled: {}", error.what());
		loaded = false;
		settings.Enable = false;
		logger::error("[ActorSurfaceEffects] {}", failedLoadedMessage);
	} catch (...) {
		failedLoadedMessage = "Actor Surface Effects disabled: character runtime buffers could not be created";
		loaded = false;
		settings.Enable = false;
		logger::error("[ActorSurfaceEffects] {}", failedLoadedMessage);
	}
}

void ActorSurfaceEffects::InstallLateHooks()
{
	if (!runtime || runtime->hooksInstalled || !loaded)
		return;

	stl::write_vfunc<0x6, ActorSurfaceSetupGeometryHook>(RE::VTABLE_BSLightingShader[0]);
	runtime->hooksInstalled = true;
	logger::info("[ActorSurfaceEffects] Installed late BSLightingShader character binding hook");
}

bool ActorSurfaceEffects::AddSurfaceEffect(RE::Actor* a_actor, const SurfaceInteractionEvent& a_event)
{
	if (!runtime || !loaded || !settings.Enable || !a_actor || !a_actor->Is3DLoaded())
		return false;
	if ((a_event.type == EffectType::Snow && !settings.EnableSnow) ||
		(a_event.type == EffectType::Mud && !settings.EnableMud)) {
		return false;
	}
	if (!IsFinite(a_event.worldPosition) || !IsFinite(a_event.worldVelocity) ||
		!std::isfinite(a_event.horizontalRadius) || !std::isfinite(a_event.verticalRadius) ||
		!std::isfinite(a_event.intensity) || !std::isfinite(a_event.contactDepth) ||
		!std::isfinite(a_event.splash)) {
		return false;
	}

	const std::uint32_t formID = a_actor->GetFormID();
	if (formID == 0u)
		return false;

	if (auto* player = RE::PlayerCharacter::GetSingleton(); player && a_actor != player) {
		const float maximumDistance = std::clamp(settings.EffectDistance, 800.0f, 8000.0f);
		const float squaredDistance = player->GetPosition().GetSquaredDistance(a_actor->GetPosition());
		if (!std::isfinite(squaredDistance) || squaredDistance > maximumDistance * maximumDistance)
			return false;
	}

	RE::NiPoint3 localPosition = ToActorLocal(a_actor, a_event.worldPosition);
	// Ground contacts come from animated Havok bodies, so their centres contain a
	// few units of walk/run pose motion. Quantise only the vertical anchor at event
	// creation; the mask remains spatially localized in X/Y, while a boot lobe no
	// longer creeps up and down the bind-pose mesh as new footsteps merge into it.
	localPosition.z = std::round(localPosition.z * 0.25f) * 4.0f;
	const float radius = std::clamp(a_event.horizontalRadius, 1.0f, 72.0f);
	// Interaction depth is authoritative for deep snow/mud. The Ground Response
	// contact band normally supplies half this height already; retain whichever is
	// larger so shallow contacts stay tight and deep contacts cover the entered
	// portion of boots/clothing without a fixed biome-height gradient.
	const float verticalRadius = std::clamp(
		std::max(a_event.verticalRadius, a_event.contactDepth * 0.50f + 1.0f),
		1.0f,
		96.0f);
	auto* contactBone = FindContactBone(a_actor, a_event.worldPosition, std::max(radius, verticalRadius) + 24.0f);
	const float amount = Saturate(a_event.intensity * settings.AccumulationStrength);
	if (amount <= kEventEpsilon)
		return false;

	std::scoped_lock lock(runtime->mutex);
	auto found = runtime->actors.find(formID);
	if (found == runtime->actors.end()) {
		const std::uint32_t qualityLimit = QualityActorLimit(settings.EffectQuality);
		const std::uint32_t maximumNPCs = std::clamp(settings.MaximumAffectedNPCs, 1u, 64u);
		const std::size_t maximumActors = std::min(qualityLimit, maximumNPCs) + 1u; // player reserve
		if (runtime->actors.size() >= maximumActors) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 anchor = player ? player->GetPosition() : a_actor->GetPosition();
			auto victim = runtime->actors.end();
			float victimScore = -std::numeric_limits<float>::infinity();
			for (auto it = runtime->actors.begin(); it != runtime->actors.end(); ++it) {
				auto candidate = it->second->handle.get();
				if (!candidate || candidate.get() == player)
					continue;
				const float distanceScore = candidate->GetPosition().GetSquaredDistance(anchor) * 0.0001f;
				const float ageScore = runtime->elapsedSeconds - it->second->lastInteractionSeconds;
				const float score = distanceScore + ageScore * 25.0f;
				if (score > victimScore) {
					victimScore = score;
					victim = it;
				}
			}
			if (victim != runtime->actors.end()) {
				runtime->actors.erase(victim);
				++runtime->evictedActors;
			} else if (a_actor != player) {
				++runtime->droppedEvents;
				return false;
			}
		}

		auto state = std::make_unique<ActorEffectState>();
		state->handle = a_actor->GetHandle();
		state->formID = formID;
		try {
			state->constantBuffer = std::make_unique<ConstantBuffer>(
				ConstantBufferDesc<CharacterRuntimeGPUData>(),
				"ActorSurfaceEffects::CharacterRuntime");
		} catch (...) {
			if (!runtime->resourceFailureLogged) {
				logger::error("[ActorSurfaceEffects] Failed to allocate a character runtime buffer; affected actor skipped");
				runtime->resourceFailureLogged = true;
			}
			return false;
		}
		found = runtime->actors.emplace(formID, std::move(state)).first;
	}

	ActorEffectState& state = *found->second;
	state.handle = a_actor->GetHandle();
	state.lastInteractionSeconds = runtime->elapsedSeconds;
	state.prepared = false;

	LocalEffectLobe* mergeTarget = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	for (auto& lobe : state.lobes) {
		if (lobe.anchorBone != (contactBone ? contactBone->name : RE::BSFixedString{}))
			continue;
		RE::NiPoint3 mergePosition = localPosition;
		RE::NiMatrix3 depositRotation;
		RE::NiPoint3 depositOrigin;
		if (DepositFrame(a_actor, lobe, depositRotation, depositOrigin))
			mergePosition = depositRotation * (a_event.worldPosition - depositOrigin) + lobe.referenceOrigin;
		const RE::NiPoint3 delta{
			lobe.localCenter.x - mergePosition.x,
			lobe.localCenter.y - mergePosition.y,
			lobe.localCenter.z - mergePosition.z
		};
		const float mergeRadius = std::max(lobe.horizontalRadius, radius) * 0.78f;
		const float distanceSquared = LengthSquared(delta);
		const bool sameFamily =
			(a_event.type == EffectType::Snow && (lobe.snowFresh + lobe.snowMelting) > kEventEpsilon) ||
			(a_event.type == EffectType::Mud && (lobe.mudWet + lobe.mudDry) > kEventEpsilon) ||
			(a_event.type == EffectType::Wetness && lobe.wetness > kEventEpsilon);
		if (sameFamily && distanceSquared < mergeRadius * mergeRadius && distanceSquared < bestDistance) {
			mergeTarget = &lobe;
			bestDistance = distanceSquared;
		}
	}

	const std::uint32_t eventLimit = QualityEventLimit(settings.EffectQuality);
	if (!mergeTarget) {
		if (state.lobes.size() >= eventLimit) {
			mergeTarget = &*std::min_element(
				state.lobes.begin(), state.lobes.end(),
				[](const LocalEffectLobe& a, const LocalEffectLobe& b) {
					const float aPriority = a.TotalAmount() - a.ageSeconds * 0.0005f;
					const float bPriority = b.TotalAmount() - b.ageSeconds * 0.0005f;
					return aPriority < bPriority;
				});
			*mergeTarget = {};
		} else {
			state.lobes.emplace_back();
			mergeTarget = &state.lobes.back();
		}
		mergeTarget->localCenter = localPosition;
		if (contactBone) {
			mergeTarget->anchorBone = contactBone->name;
			const float yaw = a_actor->GetAngleZ();
			RE::NiMatrix3 actorInverse;
			actorInverse.entry[0][0] = actorInverse.entry[1][1] = std::cos(yaw);
			actorInverse.entry[0][1] = std::sin(yaw);
			actorInverse.entry[1][0] = -std::sin(yaw);
			mergeTarget->referenceRotation = actorInverse * contactBone->world.rotate;
			mergeTarget->referenceOrigin = ToActorLocal(a_actor, contactBone->world.translate);
			mergeTarget->referenceScale = contactBone->world.scale;
		}
		mergeTarget->horizontalRadius = radius;
		mergeTarget->verticalRadius = verticalRadius;
		const std::uint32_t seedBits = formID * 1664525u +
			static_cast<std::uint32_t>(state.lobes.size()) * 1013904223u;
		mergeTarget->seed = static_cast<float>(seedBits & 0xFFFFu) / 65535.0f;
	} else {
		// A lobe is persistent actor-local state. Repeated animated contacts increase
		// its coverage but must never drag its centre around the character; otherwise
		// the visible snow/mud swims with the gait. Spatially distinct contacts still
		// allocate independent lobes through the merge-radius test above.
		mergeTarget->horizontalRadius = std::max(mergeTarget->horizontalRadius, radius);
		mergeTarget->verticalRadius = std::max(mergeTarget->verticalRadius, verticalRadius);
	}

	auto accumulate = [amount](float& a_channel, float a_scale = 1.0f) {
		const float add = Saturate(amount * a_scale);
		a_channel = Saturate(a_channel + add * (1.0f - a_channel));
	};
	switch (a_event.type) {
	case EffectType::Snow:
		accumulate(mergeTarget->snowFresh);
		break;
	case EffectType::Mud:
		accumulate(mergeTarget->mudWet);
		accumulate(mergeTarget->wetness, 0.55f);
		break;
	case EffectType::Wetness:
		accumulate(mergeTarget->wetness);
		break;
	}
	mergeTarget->splash = std::max(mergeTarget->splash, Saturate(a_event.splash));
	mergeTarget->lastContactSeconds = runtime->elapsedSeconds;
	mergeTarget->ageSeconds = 0.0f;
	return true;
}

void ActorSurfaceEffects::AddGroundContact(
	RE::Actor* a_actor,
	EffectType a_type,
	const RE::NiPoint3& a_worldContactCenter,
	const RE::NiPoint3& a_worldVelocity,
	float a_horizontalRadius,
	float a_verticalRadius,
	float a_contactDepth,
	float a_intensity)
{
	SurfaceInteractionEvent event{};
	event.type = a_type;
	event.worldPosition = a_worldContactCenter;
	event.worldVelocity = a_worldVelocity;
	event.horizontalRadius = a_horizontalRadius;
	event.verticalRadius = a_verticalRadius;
	event.contactDepth = a_contactDepth;
	// Ground Response reports sustained contacts every frame. Convert that stream
	// to an elapsed-time dose so standing/walking accumulation is consistent at
	// 30, 60 and 120+ FPS instead of saturating in two render frames.
	float frameDelta = globals::game::deltaTime ? *globals::game::deltaTime : RE::GetSecondsSinceLastFrame();
	if (!std::isfinite(frameDelta) || frameDelta <= 0.0f)
		frameDelta = 1.0f / 60.0f;
	event.intensity = a_intensity * std::clamp(frameDelta * 5.0f, 0.01f, 0.25f);
	const float horizontalSpeed = std::sqrt(
		a_worldVelocity.x * a_worldVelocity.x + a_worldVelocity.y * a_worldVelocity.y);
	event.splash = Saturate((horizontalSpeed - 70.0f) / 180.0f) *
		(a_type == EffectType::Mud ? 1.0f : 0.35f);
	AddSurfaceEffect(a_actor, event);
}

void ActorSurfaceEffects::Reset()
{
	if (!runtime)
		return;
	if (!settings.Enable) {
		std::scoped_lock lock(runtime->mutex);
		runtime->actors.clear();
		return;
	}

	float dt = globals::game::deltaTime ? *globals::game::deltaTime : RE::GetSecondsSinceLastFrame();
	if (!std::isfinite(dt) || dt < 0.0f || (globals::game::ui && globals::game::ui->GameIsPaused()))
		dt = 0.0f;
	dt = std::clamp(dt, 0.0f, 0.25f);

	const auto& weather = WeatherManager::GetSingleton()->GetContext();
	float rain = Saturate(weather.rainIntensity);
	// Keep RainResponse's explicit debug override useful for diagnosing actor wetness.
	if (globals::pipeline::rainResponse.loaded)
		rain = std::max(rain, Saturate(globals::pipeline::rainResponse.GetLiveRainIntensity()));
	const bool snowing = weather.snowIntensity > 0.01f;
	const float persistenceScale = std::lerp(2.25f, 0.24f, Saturate(settings.Persistence));

	std::scoped_lock lock(runtime->mutex);
	runtime->elapsedSeconds += dt;
	for (auto actorIt = runtime->actors.begin(); actorIt != runtime->actors.end();) {
		ActorEffectState& state = *actorIt->second;
		auto actor = state.handle.get();
		// Death/respawn is a hard spatial boundary. The actor-local snow mask
		// must not survive a death in one snowfield and reappear on the respawned
		// body elsewhere (for example Riverwood). Remove it before any melt or
		// water processing can publish another frame of the old appearance.
		if (actor && actor->IsDead()) {
			actorIt = runtime->actors.erase(actorIt);
			continue;
		}
		const bool actorAvailable = actor && actor->Is3DLoaded();
		const bool interior = actorAvailable && actor->GetParentCell() && actor->GetParentCell()->IsInteriorCell();
		const bool submerged = actorAvailable && actor->IsInWater();
		const float warmth = (interior ? 1.35f : (snowing ? 0.15f : 0.68f)) + rain * 1.5f;

		for (auto& lobe : state.lobes) {
			lobe.ageSeconds += dt;

			if (submerged) {
				// Entering/deeply occupying water is an authoritative wash, not a slow
				// weathering hint. Clear snow and both mud phases immediately while
				// retaining a temporary water film that dries normally after exit.
				lobe.snowFresh = 0.0f;
				lobe.snowMelting = 0.0f;
				lobe.mudWet = 0.0f;
				lobe.mudDry = 0.0f;
				lobe.wetness = 1.0f;
				lobe.splash = 0.0f;
				continue;
			}

			const float freshToMelting = std::min(
				lobe.snowFresh,
				settings.SnowMeltRate * warmth * persistenceScale * dt);
			lobe.snowFresh -= freshToMelting;
			lobe.snowMelting = Saturate(lobe.snowMelting + freshToMelting);

			const float meltToWetness = std::min(
				lobe.snowMelting,
				settings.SnowMeltRate * (0.42f + warmth) * persistenceScale * dt);
			lobe.snowMelting -= meltToWetness;
			lobe.wetness = Saturate(lobe.wetness + meltToWetness * 0.88f);

			if (rain > 0.01f) {
				const float rewet = std::min(lobe.mudDry, rain * 0.035f * dt);
				lobe.mudDry -= rewet;
				lobe.mudWet = Saturate(lobe.mudWet + rewet);
				if (lobe.TotalAmount() > kEventEpsilon)
					lobe.wetness = Saturate(lobe.wetness + rain * 0.020f * dt);
			}

			const float wetMudToDry = std::min(
				lobe.mudWet,
				settings.MudDryRate * (1.0f - rain * 0.92f) * persistenceScale * dt);
			lobe.mudWet -= wetMudToDry;
			lobe.mudDry = Saturate(lobe.mudDry + wetMudToDry);
			lobe.mudDry = std::max(
				0.0f,
				lobe.mudDry - settings.DryMudFadeRate * persistenceScale * dt);
			lobe.wetness = std::max(
				0.0f,
				lobe.wetness - settings.WetnessDryRate * (1.0f - rain) * persistenceScale * dt);
			lobe.splash = std::max(0.0f, lobe.splash - 0.18f * dt);

		}

		state.lobes.erase(
			std::remove_if(
				state.lobes.begin(), state.lobes.end(),
				[](const LocalEffectLobe& a_lobe) { return a_lobe.TotalAmount() <= kEventEpsilon; }),
			state.lobes.end());
		state.prepared = false;

		const float unloadedAge = runtime->elapsedSeconds - state.lastInteractionSeconds;
		if (state.lobes.empty() || (!actorAvailable && unloadedAge > 15.0f)) {
			actorIt = runtime->actors.erase(actorIt);
		} else {
			++actorIt;
		}
	}
}

void ActorSurfaceEffects::Prepass()
{
	if (!runtime || !loaded || !globals::d3d::context ||
		!runtime->neutralBuffer || !runtime->dialogueOnlyBuffer)
		return;

	// Always publish a full-size payload for actor permutations. DialogueFocus's
	// original 96-byte buffer remains valid for its legacy shaders, but the Actor
	// Surface Effects permutations declare the extended 752-byte ABI even when no
	// contamination is present.
	CharacterRuntimeGPUData neutral{};
	runtime->neutralBuffer->Update(neutral);
	CharacterRuntimeGPUData dialogueOnly{};
	const std::uint32_t focusedFormID = DialogueFocus::GetSingleton().GetFocusedFormID();
	if (focusedFormID != 0u) {
		if (auto* focusedActor = RE::TESForm::LookupByID<RE::Actor>(focusedFormID))
			DialogueFocus::GetSingleton().BuildGPUDataForActor(focusedActor, dialogueOnly.Dialogue);
	}
	runtime->dialogueOnlyBuffer->Update(dialogueOnly);

	if (!settings.Enable)
		return;

	const float4 cameraAdjust = globals::game::frameBufferCached.GetCameraPosAdjust();
	auto* player = RE::PlayerCharacter::GetSingleton();
	const RE::NiPoint3 playerPosition = player ? player->GetPosition() : RE::NiPoint3{};
	const float maximumDistance = std::clamp(settings.EffectDistance, 800.0f, 8000.0f);
	const float maximumDistanceSquared = maximumDistance * maximumDistance;
	std::scoped_lock lock(runtime->mutex);
	runtime->activeSkeletonOwners.clear();
	for (auto& [formID, statePointer] : runtime->actors) {
		ActorEffectState& state = *statePointer;
		auto actor = state.handle.get();
		if (!actor || !actor->Is3DLoaded() || !state.constantBuffer || state.lobes.empty())
			continue;
		if (player && actor.get() != player &&
			actor->GetPosition().GetSquaredDistance(playerPosition) > maximumDistanceSquared) {
			state.prepared = false;
			continue;
		}
		IndexActorSkeleton(actor.get(), formID, runtime->activeSkeletonOwners);

		CharacterRuntimeGPUData data{};
		DialogueFocus::GetSingleton().BuildGPUDataForActor(actor.get(), data.Dialogue);
		data.Magic = kActorSurfaceMagic;
		data.Version = kActorSurfaceVersion;
		data.EventCount = std::min<std::uint32_t>(
			static_cast<std::uint32_t>(state.lobes.size()),
			QualityEventLimit(settings.EffectQuality));
		data.Flags = (settings.EnableSnow ? 1u : 0u) | (settings.EnableMud ? 2u : 0u);

		// Match event conversion to the rendered skeleton root. Subtracting this
		// live origin in HLSL cancels jump/gait/root motion rather than sliding an
		// otherwise persistent deposit through the character.
		const RE::NiPoint3 actorPosition = ResolveActorRenderOrigin(actor.get());
		float actorHeight = actor->GetHeight();
		if (!std::isfinite(actorHeight) || actorHeight < 24.0f || actorHeight > 420.0f)
			actorHeight = 128.0f;
		const float yaw = actor->GetAngleZ();
		data.ActorOriginScale = {
			actorPosition.x - cameraAdjust.x,
			actorPosition.y - cameraAdjust.y,
			actorPosition.z - cameraAdjust.z,
			std::clamp(actor->GetScale(), 0.1f, 10.0f)
		};
		data.ActorRotationHeight = { std::cos(yaw), std::sin(yaw), actorHeight, 0.0f };
		data.Tuning = {
			std::clamp(settings.AccumulationStrength, 0.0f, 2.0f),
			std::clamp(settings.MaskSoftness, 0.04f, 0.55f),
			std::clamp(settings.EdgeBreakup, 0.0f, 0.40f),
			static_cast<float>(settings.Debug)
		};
		data.SnowAppearance = { 0.72f, 0.82f, 0.93f, 0.82f };

		for (std::uint32_t index = 0; index < data.EventCount; ++index) {
			const auto& source = state.lobes[index];
			auto& target = data.Events[index];
			if (!source.anchorBone.empty()) {
				RE::NiMatrix3 depositRotation;
				RE::NiPoint3 depositOrigin;
				// Resolve names against the live skeleton; never retain bone pointers
				// across equipment swaps or unloaded/replaced actor scene graphs.
				if (!DepositFrame(actor.get(), source, depositRotation, depositOrigin))
					continue; // Missing anchor: suppress this lobe, do not project it in the wrong space.
				depositOrigin -= RE::NiPoint3{ cameraAdjust.x, cameraAdjust.y, cameraAdjust.z };
				const auto translation = source.referenceOrigin - depositRotation * depositOrigin;
				for (uint32_t row = 0; row < 3; ++row) {
					const auto* r = depositRotation.entry[row];
					const float t = row == 0 ? translation.x : row == 1 ? translation.y : translation.z;
					target.WorldToDeposit[row] = { r[0], r[1], r[2], t };
				}
			}
			// As a deposit ages away its upper boundary recedes toward the original
			// contact plane. This gives snow/mud a gravity-readable downward fade
			// instead of uniformly dissolving the complete vertical lobe in place.
			const float retainedAmount = std::clamp(source.TotalAmount(), 0.0f, 1.0f);
			const float retainedHeight = std::lerp(
				0.12f,
				1.0f,
				std::sqrt(retainedAmount));
			const float sourceRadius = std::max(source.verticalRadius, 1.0f);
			const float lowerBoundary = source.localCenter.z - sourceRadius;
			const float visibleHeight = std::max(2.0f * sourceRadius * retainedHeight, 2.0f);
			const float visibleRadius = visibleHeight * 0.5f;
			const float visibleCenterZ = lowerBoundary + visibleRadius;
			target.LocalCenterRadius = {
				source.localCenter.x,
				source.localCenter.y,
				visibleCenterZ,
				source.horizontalRadius
			};
			target.VerticalAmounts = {
				visibleRadius,
				source.snowFresh,
				source.snowMelting,
				source.mudWet
			};
			target.State = {
				source.mudDry,
				source.wetness,
				source.seed,
				source.splash
			};
		}

		state.constantBuffer->Update(data);
		state.prepared = true;
		state.lastVisibleSeconds = runtime->elapsedSeconds;
	}
}

void ActorSurfaceEffects::BindLightingGeometry(RE::BSRenderPass* a_pass)
{
	if (!runtime || !loaded || !a_pass || !a_pass->geometry || !globals::d3d::context ||
		!runtime->neutralBuffer || !runtime->dialogueOnlyBuffer)
		return;

	auto* actor = ResolveOwningActor(a_pass->geometry);
	std::uint32_t actorFormID = actor ? actor->GetFormID() : 0u;
	auto* skin = a_pass->geometry->GetGeometryRuntimeData().skinInstance.get();

	std::scoped_lock lock(runtime->mutex);
	if (actorFormID == 0u && skin && skin->bones) {
		constexpr std::uint32_t kMaximumSkinBonesToInspect = 256u;
		const std::uint32_t boneCount =
			std::min(skin->numMatrices, kMaximumSkinBonesToInspect);
		for (std::uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
			const auto owner = runtime->activeSkeletonOwners.find(skin->bones[boneIndex]);
			if (owner != runtime->activeSkeletonOwners.end()) {
				actorFormID = owner->second;
				break;
			}
		}
	}

	// Any unowned skinned Lighting draw must still receive the extended neutral
	// ABI; otherwise it could inherit the preceding contaminated actor's b13.
	// Static and landscape geometry remain untouched, preserving Ground Response.
	if (actorFormID == 0u) {
		if (skin) {
			ID3D11Buffer* neutral = runtime->neutralBuffer->CB();
			globals::d3d::context->PSSetConstantBuffers(13, 1, &neutral);
		}
		return;
	}

	ID3D11Buffer* buffer = actorFormID == DialogueFocus::GetSingleton().GetFocusedFormID()
		? runtime->dialogueOnlyBuffer->CB()
		: runtime->neutralBuffer->CB();
	if (settings.Enable) {
		const auto found = runtime->actors.find(actorFormID);
		// A contaminated actor payload already contains the byte-identical
		// DialogueFocus prefix built during Prepass. It must win even when that actor
		// is the dialogue subject; selecting dialogueOnly here erased every surface
		// lobe whenever conversation focus became active.
		if (found != runtime->actors.end() && found->second->prepared && found->second->constantBuffer)
			buffer = found->second->constantBuffer->CB();
	}
	globals::d3d::context->PSSetConstantBuffers(13, 1, &buffer);
}

void ActorSurfaceEffects::ApplyQualityTier(std::uint32_t a_quality)
{
	settings.EffectQuality = std::min(a_quality, 3u);
}

void ActorSurfaceEffects::DrawSettings()
{
	bool changed = false;
	changed |= ImGui::Checkbox("Enable Actor Surface Effects", &settings.Enable);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Adds localized actor-anchored snow, mud and melt wetness only after real Ground Response contact. Disable for a zero-work fallback.");

	ImGui::BeginDisabled(!settings.Enable);
	changed |= ImGui::Checkbox("Snow Accumulation", &settings.EnableSnow);
	changed |= ImGui::Checkbox("Mud Accumulation", &settings.EnableMud);

	static constexpr const char* qualityNames[] = { "Low", "Medium", "High", "Ultra" };
	int quality = static_cast<int>(std::min(settings.EffectQuality, 3u));
	if (ImGui::Combo("Effect Quality", &quality, qualityNames, static_cast<int>(std::size(qualityNames)))) {
		ApplyQualityTier(static_cast<std::uint32_t>(quality));
		changed = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Scales nearby actor capacity and localized contact detail. It does not change the snow or mud art direction.");

	float persistencePercent = settings.Persistence * 100.0f;
	if (ImGui::SliderFloat("Persistence", &persistencePercent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
		settings.Persistence = persistencePercent * 0.01f;
		changed = true;
	}
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("How long snow, mud and melt wetness remain before naturally recovering. Weather still affects the lifecycle.");
	changed |= ImGui::SliderFloat("Accumulation Strength", &settings.AccumulationStrength, 0.0f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);

	if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_None)) {
		int maximumAffectedNPCs = static_cast<int>(settings.MaximumAffectedNPCs);
		if (ImGui::SliderInt("Maximum Affected NPCs", &maximumAffectedNPCs, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp)) {
			settings.MaximumAffectedNPCs = static_cast<std::uint32_t>(maximumAffectedNPCs);
			changed = true;
		}
		changed |= ImGui::SliderFloat("Effect Distance", &settings.EffectDistance, 800.0f, 8000.0f, "%.0f units", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Snow Melt Speed", &settings.SnowMeltRate, 0.001f, 0.05f, "%.3f /s", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Mud Drying Speed", &settings.MudDryRate, 0.001f, 0.04f, "%.3f /s", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Dry Mud Fade Speed", &settings.DryMudFadeRate, 0.0005f, 0.02f, "%.4f /s", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Wetness Drying Speed", &settings.WetnessDryRate, 0.001f, 0.06f, "%.3f /s", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Mask Softness", &settings.MaskSoftness, 0.04f, 0.55f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::SliderFloat("Edge Breakup", &settings.EdgeBreakup, 0.0f, 0.40f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextDisabled("Active actors: %u | contact lobes: %u", GetActiveActorCount(), GetActiveEventCount());

		if (globals::state && globals::state->IsDeveloperMode()) {
			int debug = static_cast<int>(settings.Debug);
			static constexpr const char* debugNames[] = {
				"Off", "Combined Mask", "Snow", "Mud", "Wetness", "Contact Lobes", "Actor-local Coordinates"
			};
			if (ImGui::Combo("Debug View", &debug, debugNames, static_cast<int>(std::size(debugNames)))) {
				settings.Debug = static_cast<DebugView>(std::clamp(debug, 0, 6));
				changed = true;
			}
		}
		ImGui::TreePop();
	}
	ImGui::EndDisabled();

	if (changed && globals::state)
		globals::state->UpdateFeatureData(globals::state->inWorld);
}

void ActorSurfaceEffects::LoadSettings(json& a_json)
{
	settings.Enable = a_json.value("Enable", true);
	settings.EnableSnow = a_json.value("EnableSnow", true);
	settings.EnableMud = a_json.value("EnableMud", true);
	settings.EffectQuality = std::clamp(a_json.value("EffectQuality", 3u), 0u, 3u);
	settings.Persistence = Saturate(a_json.value("Persistence", 0.68f));
	settings.AccumulationStrength = std::clamp(a_json.value("AccumulationStrength", 1.0f), 0.0f, 2.0f);
	settings.SnowMeltRate = std::clamp(a_json.value("SnowMeltRate", 0.010f), 0.001f, 0.05f);
	settings.MudDryRate = std::clamp(a_json.value("MudDryRate", 0.007f), 0.001f, 0.04f);
	settings.DryMudFadeRate = std::clamp(a_json.value("DryMudFadeRate", 0.0025f), 0.0005f, 0.02f);
	settings.WetnessDryRate = std::clamp(a_json.value("WetnessDryRate", 0.014f), 0.001f, 0.06f);
	settings.MaximumAffectedNPCs = std::clamp(a_json.value("MaximumAffectedNPCs", 24u), 1u, 64u);
	settings.EffectDistance = std::clamp(a_json.value("EffectDistance", 3200.0f), 800.0f, 8000.0f);
	settings.MaskSoftness = std::clamp(a_json.value("MaskSoftness", 0.24f), 0.04f, 0.55f);
	settings.EdgeBreakup = std::clamp(a_json.value("EdgeBreakup", 0.16f), 0.0f, 0.40f);
	settings.Debug = DebugView::Off;
}

void ActorSurfaceEffects::SaveSettings(json& a_json)
{
	a_json = {
		{ "Enable", settings.Enable },
		{ "EnableSnow", settings.EnableSnow },
		{ "EnableMud", settings.EnableMud },
		{ "EffectQuality", settings.EffectQuality },
		{ "Persistence", settings.Persistence },
		{ "AccumulationStrength", settings.AccumulationStrength },
		{ "SnowMeltRate", settings.SnowMeltRate },
		{ "MudDryRate", settings.MudDryRate },
		{ "DryMudFadeRate", settings.DryMudFadeRate },
		{ "WetnessDryRate", settings.WetnessDryRate },
		{ "MaximumAffectedNPCs", settings.MaximumAffectedNPCs },
		{ "EffectDistance", settings.EffectDistance },
		{ "MaskSoftness", settings.MaskSoftness },
		{ "EdgeBreakup", settings.EdgeBreakup }
	};
}

void ActorSurfaceEffects::RestoreDefaultSettings()
{
	settings = {};
}

std::uint32_t ActorSurfaceEffects::GetActiveActorCount() const
{
	if (!runtime)
		return 0u;
	std::scoped_lock lock(runtime->mutex);
	return static_cast<std::uint32_t>(runtime->actors.size());
}

std::uint32_t ActorSurfaceEffects::GetActiveEventCount() const
{
	if (!runtime)
		return 0u;
	std::scoped_lock lock(runtime->mutex);
	std::size_t count = 0;
	for (const auto& [formID, state] : runtime->actors) {
		(void)formID;
		count += state->lobes.size();
	}
	return static_cast<std::uint32_t>(std::min<std::size_t>(count, std::numeric_limits<std::uint32_t>::max()));
}
