#include "HybridGI.h"
#include "HybridGI/TemporalPolicy.h"
#include "HybridGI/SceneVoxelizer.h"

#include <DirectXTex.h>
#include <DirectXPackedVector.h>

#include "../I18n/I18n.h"
#include "Deferred.h"
#include "Modules/CameraSuite.h"
#include "Modules/AmbientProbe.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/PixelCapture.h"
#include "Modules/LinearLightCore.h"
#include "Modules/RadiantGrid.h"
#include "Globals.h"
#include "Menu.h"
#include "Profiler.h"
#include "State.h"
#include "ShaderCache.h"
#include "MaterialForge.h"
#include "MaterialForge/PhysicalMaterialRegistry.h"
#include "MaterialLayers.h"
#include "Renderer/QualityProfiles.h"
#include "Renderer/LightTransportWorld.h"
#include "Renderer/TemporalContext.h"
#include "Renderer/GPUWorkloadBudgeter.h"
#include "Renderer/ReflectionContext.h"
#include "Renderer/PixelAnnotations.h"
#include "Util.h"
#include "../Menu/PIXLStyle.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"

#include <RE/S/SendHUDMessage.h>
#include <RE/B/BSVisit.h>
#include <RE/B/BSRenderPass.h>
#include <RE/B/BSLightingShaderMaterialBase.h>
#include <RE/B/BSTriShape.h>
#include <RE/N/NiSourceTexture.h>
#include <RE/N/NiNode.h>
#include <RE/T/TES.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/P/PlayerCharacter.h>

#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <windows.h>

#define I18N_KEY_PREFIX "feature.hybrid_gi."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HybridGI::Settings,
	Enabled,
	EnableGI,
	EnableExperimentalSpecularGI,
	EnableVanillaSSAO,
	NumSlices,
	NumSteps,
	ResolutionMode,
	MinScreenRadius,
	AORadius,
	GIRadius,
	Thickness,
	DepthFadeRange,
	GISaturation,
	GIDistanceCompensation,
	AOPower,
	GIStrength,
	EnableTemporalDenoiser,
	EnableBlur,
	EnableAdaptiveDenoiser,
	DepthDisocclusion,
	NormalDisocclusion,
	MaxAccumFrames,
	BlurRadius,
	DistanceNormalisation,
	EnableWorldCache,
	WorldCacheMaxAge,
	WorldCacheSampleCount,
	WorldCacheStrength,
	WorldCacheCellSizeNear,
	WorldCacheCellSizeFar,
	WorldCacheRadius,
	WorldCacheLeakReduction,
	WorldCacheTemporalResponse,
	EnableWorldCacheSecondBounce,
	WorldCacheSecondBounceStrength,
	WorldCacheInjectionStride,
	WorldCacheTraceSteps,
	EnableDirectionalOcclusion,
	DirectionalOcclusionStrength,
	EnableVoxelReflections,
	VoxelReflectionStrength,
	VoxelReflectionRoughnessCutoff,
	EnableBentNormalLighting,
	BentNormalStrength,
	EnableSpecularOcclusion,
	SpecularOcclusionStrength,
	RadianceFireflyClamp,
	UpsampleEdgeThreshold,
	ReflectionIntensity,
	ReflectionMaxRoughness,
	ReflectionMaxDistance,
	ReflectionThickness,
	ReflectionSteps,
	ReflectionTemporalResponse,
	ReflectionFireflyClamp,
	ReflectionWorldFallbackStrength,
	ReflectionRayBias,
	ReflectionRoughnessJitter,
	DebugView,
	DebugGain)


namespace
{
    // The active SSGI path consumes the bounded persistent irradiance atlas for
    // off-screen rays, plus optional Radiant Grid emitter injection and one
    // conservative cache bounce. Keep the atlas windows tightly bounded.
    constexpr bool kEnableWorldCacheForSSGITest = true;
    bool WorldCacheAvailableForSSGITest() { return kEnableWorldCacheForSSGITest; }

	// Skyrim's scenegraph visitor assumes every child pointer has a live NiObject
	// vtable. Streaming/LOD trees can briefly contain a readable object whose
	// vtable has already been cleared. Validate the object and the two virtual
	// slots used by our bounded walk before making either virtual call.
	bool IsReadableSceneMemory(const void* address, std::size_t size)
	{
		if (!address || size == 0)
			return false;
		const auto start = reinterpret_cast<std::uintptr_t>(address);
		if (start > UINTPTR_MAX - size)
			return false;
		const auto end = start + size;
		for (auto cursor = start; cursor < end;) {
			MEMORY_BASIC_INFORMATION info{};
			if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
				info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
				return false;
			const DWORD protection = info.Protect & 0xFFu;
			if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
				protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
				return false;
			const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
			if (regionEnd <= cursor)
				return false;
			cursor = std::min(regionEnd, end);
		}
		return true;
	}

	bool IsExecutableSceneAddress(std::uintptr_t address)
	{
		if (!address)
			return false;
		MEMORY_BASIC_INFORMATION info{};
		if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) || info.State != MEM_COMMIT)
			return false;
		const DWORD protection = info.Protect & 0xFFu;
		return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
			protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
	}

	bool HasSafeSceneVTable(RE::NiAVObject* object, std::unordered_set<std::uintptr_t>& validVTables,
		std::unordered_set<std::uintptr_t>& invalidVTables)
	{
		// NiAVObject/SE has a 0x110-byte instance; AsNode and AsGeometry are
		// NiObject virtual slots 3 and 7 respectively.
		if (!IsReadableSceneMemory(object, 0x110))
			return false;
		const auto vtableAddress = *reinterpret_cast<const std::uintptr_t*>(object);
		if (validVTables.contains(vtableAddress))
			return true;
		if (invalidVTables.contains(vtableAddress))
			return false;
		const auto* vtable = reinterpret_cast<const std::uintptr_t*>(vtableAddress);
		if (!IsReadableSceneMemory(vtable, sizeof(std::uintptr_t) * 8)) {
			invalidVTables.insert(vtableAddress);
			return false;
		}
		if (!IsExecutableSceneAddress(vtable[3]) || !IsExecutableSceneAddress(vtable[7])) {
			invalidVTables.insert(vtableAddress);
			return false;
		}
		validVTables.insert(vtableAddress);
		return true;
	}

	template <class Callback>
	void TraverseScenegraphGeometriesGuarded(RE::NiAVObject* root, const std::array<float, 3>& volumeCenter,
		float volumeHalfExtent, Callback&& callback)
	{
		constexpr std::size_t kMaximumVisitedObjects = 131072;
		std::vector<RE::NiAVObject*> pending;
		std::unordered_set<RE::NiAVObject*> visited;
		std::unordered_set<std::uintptr_t> validVTables;
		std::unordered_set<std::uintptr_t> invalidVTables;
		pending.reserve(4096);
		visited.reserve(8192);
		validVTables.reserve(16);
		invalidVTables.reserve(16);
		if (root)
			pending.push_back(root);

		while (!pending.empty() && visited.size() < kMaximumVisitedObjects) {
			auto* object = pending.back();
			pending.pop_back();
			if (!object || !visited.emplace(object).second || !HasSafeSceneVTable(object, validVTables, invalidVTables))
				continue;

			const auto& bound = object->worldBound;
			const float radius = bound.radius;
			if (std::isfinite(radius) && radius >= 0.0f &&
				std::isfinite(bound.center.x) && std::isfinite(bound.center.y) && std::isfinite(bound.center.z)) {
				const bool intersectsVolume =
					std::abs(bound.center.x - volumeCenter[0]) <= volumeHalfExtent + radius &&
					std::abs(bound.center.y - volumeCenter[1]) <= volumeHalfExtent + radius &&
					std::abs(bound.center.z - volumeCenter[2]) <= volumeHalfExtent + radius;
				if (!intersectsVolume)
					continue;
			}

			if (auto* geometry = object->AsGeometry()) {
				callback(geometry);
				continue;
			}
			auto* node = object->AsNode();
			if (!node || !IsReadableSceneMemory(node, sizeof(RE::NiNode)))
				continue;
			const auto& children = node->GetChildren();
			// Only walk initialized child entries. Capacity includes unused storage,
			// which can contain stale scenegraph pointers during LOD/cell streaming.
			const auto childCount = static_cast<std::size_t>(children.size());
			const auto* first = children.begin();
			if (!first || childCount == 0 || childCount > kMaximumVisitedObjects ||
				!IsReadableSceneMemory(first, childCount * sizeof(RE::NiPointer<RE::NiAVObject>)))
				continue;
			for (std::size_t i = 0; i < childCount; ++i) {
				auto* child = first[i].get();
				if (child)
					pending.push_back(child);
			}
		}
	}

	struct PixlDiagnosticView
	{
		std::string_view name;
		uint giDebugView;
		uint materialDebugMode;
	};

	constexpr std::array PixlDiagnosticViews{
		PixlDiagnosticView{ "00-composite", 0, 0 },
		PixlDiagnosticView{ "01-ao-occlusion", 1, 0 },
		PixlDiagnosticView{ "02-diffuse-gi", 2, 0 },
		PixlDiagnosticView{ "03-specular-gi", 3, 0 },
		PixlDiagnosticView{ "04-lighting-input-radiance", 4, 0 },
		PixlDiagnosticView{ "05-voxel-confidence-coverage", 5, 0 },
		PixlDiagnosticView{ "06-voxel-cascade", 6, 0 },
		PixlDiagnosticView{ "07-cached-irradiance", 7, 0 },
		PixlDiagnosticView{ "08-directional-occlusion", 8, 0 },
		PixlDiagnosticView{ "09-pbr-coverage", 0, 1 },
		PixlDiagnosticView{ "10-pbr-base-color", 0, 2 },
		PixlDiagnosticView{ "11-pbr-roughness", 0, 3 },
		PixlDiagnosticView{ "12-pbr-metal-conductor-classification", 0, 4 },
		PixlDiagnosticView{ "13-pbr-f0", 0, 5 },
		PixlDiagnosticView{ "14-pbr-world-normal", 0, 6 },
		PixlDiagnosticView{ "15-pbr-direct-diffuse", 0, 7 },
		PixlDiagnosticView{ "16-pbr-direct-specular", 0, 8 },
		PixlDiagnosticView{ "17-pbr-physical-delta", 0, 9 },
		PixlDiagnosticView{ "18-pbr-emissive", 0, 10 },
		PixlDiagnosticView{ "19-voxel-reflection-fallback", 9, 0 },
		PixlDiagnosticView{ "20-bent-normal", 10, 0 },
		PixlDiagnosticView{ "21-directional-visibility", 11, 0 },
		PixlDiagnosticView{ "22-specular-occlusion", 12, 0 },
		PixlDiagnosticView{ "23-hybrid-reflections", 13, 0 },
		PixlDiagnosticView{ "24-pbr-raw-authored-metalness", 0, 11 },
		PixlDiagnosticView{ "25-pbr-effective-metalness", 0, 12 },
        PixlDiagnosticView{ "26-cache-log-variance", 14, 0 },
        PixlDiagnosticView{ "27-cache-age", 15, 0 },
        PixlDiagnosticView{ "28-cache-reliability", 16, 0 },
        PixlDiagnosticView{ "29-cache-state", 17, 0 },
        PixlDiagnosticView{ "30-cache-light-change", 18, 0 },
        PixlDiagnosticView{ "31-cache-camera-plane-visibility", 19, 0 },
		PixlDiagnosticView{ "32-local-light-potential", 0, 16 },
		PixlDiagnosticView{ "33-local-light-after-shadows", 0, 17 },
		PixlDiagnosticView{ "34-local-light-after-mapped-shadow", 0, 18 },
	};

	std::string PixlDiagnosticTimestamp()
	{
		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm local{};
		localtime_s(&local, &now);
		return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}",
			local.tm_year + 1900,
			local.tm_mon + 1,
			local.tm_mday,
			local.tm_hour,
			local.tm_min,
			local.tm_sec);
	}

	std::uint64_t HashFileFNV1a(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::uint64_t hash = 1469598103934665603ull;
		std::array<char, 64 * 1024> bytes{};
		while (stream) {
			stream.read(bytes.data(), bytes.size());
			for (std::streamsize index = 0; index < stream.gcount(); ++index) {
				hash ^= static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(index)]);
				hash *= 1099511628211ull;
			}
		}
		return hash;
	}


	enum class PixlQualityPreset
	{
		Performance,
		Balanced,
		Quality,
		Ultra,
		Custom
	};

	const char* GetPresetName(PixlQualityPreset preset)
	{
		switch (preset) {
		case PixlQualityPreset::Performance:
			return "Low";
		case PixlQualityPreset::Balanced:
			return "Medium";
		case PixlQualityPreset::Quality:
			return "High";
		case PixlQualityPreset::Ultra:
			return "Ultra";
		default:
			return "Custom";
		}
	}

	const char* GetResolutionName(int mode)
	{
		switch (mode) {
		case 0:
			return "Full";
		case 2:
			return "Quarter";
		default:
			return "Half";
		}
	}


	const char* GetPresetDescription(PixlQualityPreset preset)
	{
		switch (preset) {
		case PixlQualityPreset::Performance:
			return "Unified Low lighting workload: fewer GI rays, shorter reflection traces and reduced local shadow sampling.";
		case PixlQualityPreset::Balanced:
			return "Unified Medium lighting workload with balanced GI coverage, reflection traces and shadow cost.";
		case PixlQualityPreset::Quality:
			return "Unified High lighting workload with strong GI/reflection density and contact-shadow sampling.";
		case PixlQualityPreset::Ultra:
			return "Unified Ultra lighting workload. This preserves the shipped PIXL known-good baseline.";
		default:
			return "Current values do not exactly match a built-in quality preset.";
		}
	}

	PixlQualityPreset DetectPreset()
	{
		const int quality = PIXLRenderer::QualityProfiles::DetectLightingTier();
		return quality >= PIXLRenderer::QualityProfiles::Low && quality <= PIXLRenderer::QualityProfiles::Ultra ?
		           static_cast<PixlQualityPreset>(quality) :
		           PixlQualityPreset::Custom;
	}

	bool DrawPixlToggleField(const char* label, bool* value)
	{
		// Use the common full-width row instead of positioning the latch relative
		// to the cursor *after* drawing the label.  The old calculation could push
		// the SSAO latch beyond a narrow/table column, leaving only a few pixels of
		// its hit box accessible at some UI scales.
		return PIXLUI::LabeledToggle(label, value);
	}

	bool BeginSettingsTable(const char* id)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
			return false;
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.46f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.54f);
		return true;
	}

	void BeginSettingRow(const char* label, const char* tooltip = nullptr)
	{
		ImGui::TableNextRow(
			ImGuiTableRowFlags_None,
			PIXLUI::Ref(34.0f));
		ImGui::TableSetColumnIndex(0);

		ImGui::AlignTextToFramePadding();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float cy =
			p.y + ImGui::GetTextLineHeight() * 0.5f +
			ImGui::GetStyle().FramePadding.y;

		ImGui::GetWindowDrawList()->AddCircleFilled(
			ImVec2(p.x + PIXLUI::Ref(4.0f), cy),
			PIXLUI::Ref(2.0f),
			PIXLUI::Colors::CyanSoft);

		ImGui::SetCursorPosX(
			ImGui::GetCursorPosX() +
			PIXLUI::Ref(12.0f));
		ImGui::TextColored(
			PIXLUI::ToVec4(PIXLUI::Colors::TextMuted),
			"%s",
			label);

		if (tooltip && tooltip[0] != '\0') {
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::PushTextWrapPos(
					ImGui::GetCursorPosX() +
					PIXLUI::Ref(330.0f));
				ImGui::TextUnformatted(tooltip);
				ImGui::PopTextWrapPos();
			}
		}

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
	}

	void DrawPresetButton(const char* label, PixlQualityPreset preset, PixlQualityPreset activePreset)
	{
		const bool selected =
			activePreset == preset;

		if (PIXLUI::ActionButton(
				label,
				ImVec2(
					ImGui::GetContentRegionAvail().x,
					PIXLUI::Ref(34.0f)),
				selected)) {
			// Q1: the native Radiance page uses the same Lighting authority as the
			// main Quality workspace and Tuner quick panel. No duplicate preset table.
			PIXLRenderer::QualityProfiles::Apply(
				PIXLRenderer::QualityProfiles::Group::Lighting,
				static_cast<int>(preset));
			globals::state->Save();
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::PushTextWrapPos(
				ImGui::GetCursorPosX() +
					PIXLUI::Ref(300.0f));
			ImGui::TextUnformatted(
				GetPresetDescription(preset));
			ImGui::PopTextWrapPos();
		}
	}

}

void HybridGI::CaptureRenderGeometry(RE::BSRenderPass* a_pass)
{
	// The existing SetupGeometry hook runs on Skyrim's render thread. Walk the
	// active world scene root once per frame so off-screen attached-cell geometry
	// participates, then copy a bounded set of rigid opaque triangles immediately.
	// No game object, geometry pointer, or borrowed vertex buffer survives here.
	if (!settings.Enabled || !settings.EnableGI || sceneVoxelDimension == 0)
		return;
	const auto frame = globals::state ? globals::state->frameCount : std::numeric_limits<std::uint32_t>::max();
	std::scoped_lock lock(sceneTriangleMutex);
	constexpr std::size_t kMaxFrameTriangles = PIXL::HybridGIInternal::kMaxGpuSceneTriangles;
	constexpr std::size_t kMaxFrameShapes = 1024;
	if (frame != sceneCaptureFrame) {
		pendingSceneTriangles.clear();
		pendingSceneMaterialTextures.clear();
		pendingSceneMaterialTextureIndices.clear();
		pendingSceneGeometryTokens.clear();
		sceneCaptureFrame = frame;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			sceneCaptureCellKey = reinterpret_cast<std::uintptr_t>(player->GetParentCell());
			sceneCaptureWorldKey = reinterpret_cast<std::uintptr_t>(player->GetWorldspace());
		} else {
			sceneCaptureCellKey = 0;
			sceneCaptureWorldKey = 0;
		}
		pendingSceneCellKey = sceneCaptureCellKey;
		pendingSceneWorldKey = sceneCaptureWorldKey;
	}

	auto captureGeometry = [&](RE::BSGeometry* geometry) {
		if (!geometry || pendingSceneTriangles.size() >= kMaxFrameTriangles || pendingSceneGeometryTokens.size() >= kMaxFrameShapes)
			return;
		const auto& runtime = geometry->GetGeometryRuntimeData();
		if (runtime.skinInstance || runtime.alphaProperty ||
			(runtime.shaderProperty && runtime.shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)))
			return;
		auto* shape = geometry->AsTriShape();
		auto* rendererData = runtime.rendererData;
		if (!shape || !rendererData || !rendererData->rawVertexData || !rendererData->rawIndexData ||
			!rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_VERTEX))
			return;
		const auto token = reinterpret_cast<std::uintptr_t>(geometry);
		if (std::find(pendingSceneGeometryTokens.begin(), pendingSceneGeometryTokens.end(), token) != pendingSceneGeometryTokens.end())
			return;
		const auto shapeData = shape->GetTrishapeRuntimeData();
		const auto stride = rendererData->vertexDesc.GetSize();
		const bool hasUV = rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_UV);
		const auto uvOffset = hasUV ? rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_TEXCOORD0) : 0u;
		const bool validUVOffset = hasUV && uvOffset <= stride && sizeof(std::uint16_t) * 2u <= stride - uvOffset;
		const bool hasVertexColors = rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS);
		const auto colorOffset = hasVertexColors ? rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR) : 0u;
		const bool validColorOffset = hasVertexColors && colorOffset <= stride && sizeof(std::uint8_t) * 4u <= stride - colorOffset;
		const auto triangleCount = static_cast<std::uint32_t>(shapeData.triangleCount);
		const auto vertexCount = static_cast<std::uint32_t>(shapeData.vertexCount);
		if (stride < sizeof(float) * 3 || triangleCount == 0 || triangleCount > 4096 || vertexCount < 3 || vertexCount > 65535)
			return;
		if (triangleCount > kMaxFrameTriangles - pendingSceneTriangles.size())
			return;  // retain whole mesh topology within the per-frame snapshot
		pendingSceneGeometryTokens.push_back(token);  // compared only until this owned snapshot is drained
		const auto readPosition = [&](std::uint16_t index, std::array<float, 3>& result) {
			if (index >= vertexCount)
				return false;
			const auto* p = reinterpret_cast<const float*>(rendererData->rawVertexData + std::size_t(index) * stride);
			if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
				return false;
			const RE::NiPoint3 world = geometry->world * RE::NiPoint3{ p[0], p[1], p[2] };
			if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
				return false;
			result = { world.x, world.y, world.z };
			return true;
		};
		std::array<float, 2> uvScale{ 1.0f, 1.0f };
		std::array<float, 2> uvOffsetValue{};
		const auto readUV = [&](std::uint16_t index, std::array<float, 2>& result) {
			if (!validUVOffset || index >= vertexCount)
				return false;
			const auto* uv = reinterpret_cast<const std::uint16_t*>(rendererData->rawVertexData + std::size_t(index) * stride + uvOffset);
			result = { DirectX::PackedVector::XMConvertHalfToFloat(uv[0]), DirectX::PackedVector::XMConvertHalfToFloat(uv[1]) };
			result[0] = result[0] * uvScale[0] + uvOffsetValue[0];
			result[1] = result[1] * uvScale[1] + uvOffsetValue[1];
			return std::isfinite(result[0]) && std::isfinite(result[1]);
		};
		std::array<float, 3> emissiveColor{};
		bool isEmissive = false;
		if (auto* property = geometry->lightingShaderProp_cast(); property && property->emissiveColor &&
			std::isfinite(property->emissiveMult) && property->emissiveMult > 0.0f) {
			const float intensity = property->emissiveMult;
			emissiveColor = { property->emissiveColor->red * intensity, property->emissiveColor->green * intensity,
				property->emissiveColor->blue * intensity };
			isEmissive = std::isfinite(emissiveColor[0]) && std::isfinite(emissiveColor[1]) && std::isfinite(emissiveColor[2]) &&
				std::max({ emissiveColor[0], emissiveColor[1], emissiveColor[2] }) > 1.0e-4f;
		}
		const auto readVertexColor = [&](std::uint16_t index, std::array<float, 3>& result) {
			if (!validColorOffset || index >= vertexCount)
				return false;
			const auto* vertex = rendererData->rawVertexData + std::size_t(index) * stride + colorOffset;
			if (vertex[3] < 250u)
				return false;  // uncertain vertex-alpha geometry is not a trusted opaque material
			result = { vertex[0] / 255.0f, vertex[1] / 255.0f, vertex[2] / 255.0f };
			return true;
		};
		std::uint32_t textureIndex = UINT32_MAX;
		if (validUVOffset && !isEmissive) {
			if (auto* property = geometry->lightingShaderProp_cast(); property && property->material &&
				property->material->GetType() == RE::BSShaderMaterial::Type::kLighting) {
				auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(property->material);
				uvScale = { material->texCoordScale[0].x, material->texCoordScale[0].y };
				uvOffsetValue = { material->texCoordOffset[0].x, material->texCoordOffset[0].y };
				auto* sourceTexture = material->diffuseTexture.get();
				auto* rendererTexture = sourceTexture ? sourceTexture->rendererTexture : nullptr;
				auto* srv = rendererTexture ? REX::W32::AsReal(rendererTexture->resourceView) : nullptr;
				if (srv) {
					auto found = pendingSceneMaterialTextureIndices.find(srv);
					if (found != pendingSceneMaterialTextureIndices.end()) {
						textureIndex = found->second;
					} else if (pendingSceneMaterialTextures.size() < PIXL::HybridGIInternal::kSceneMaterialTextureSlots) {
						textureIndex = static_cast<std::uint32_t>(pendingSceneMaterialTextures.size());
						winrt::com_ptr<ID3D11ShaderResourceView> ownedTexture;
						ownedTexture.copy_from(srv);
						pendingSceneMaterialTextures.push_back(std::move(ownedTexture));
						pendingSceneMaterialTextureIndices.emplace(srv, textureIndex);
					}
				}
			}
		}
		for (std::uint32_t i = 0; i < triangleCount && pendingSceneTriangles.size() < kMaxFrameTriangles; ++i) {
			const auto base = std::size_t(i) * 3;
			PIXL::HybridGIInternal::SceneTriangle triangle;
			const auto i0 = rendererData->rawIndexData[base];
			const auto i1 = rendererData->rawIndexData[base + 1];
			const auto i2 = rendererData->rawIndexData[base + 2];
			if (readPosition(i0, triangle.a) && readPosition(i1, triangle.b) && readPosition(i2, triangle.c)) {
				readUV(i0, triangle.uvA);
				readUV(i1, triangle.uvB);
				readUV(i2, triangle.uvC);
				triangle.textureIndex = textureIndex;
				std::array<float, 3> materialColor = isEmissive ? emissiveColor : std::array<float, 3>{ 1.0f, 1.0f, 1.0f };
				std::array<float, 3> color0{}, color1{}, color2{};
				if (readVertexColor(i0, color0) && readVertexColor(i1, color1) && readVertexColor(i2, color2)) {
					for (std::size_t channel = 0; channel < 3; ++channel) {
						const float vertexTint = (color0[channel] + color1[channel] + color2[channel]) / 3.0f;
						materialColor[channel] *= vertexTint;
					}
				}
				triangle.packedMaterial = PIXL::HybridGIInternal::PackSceneVoxelMaterial(materialColor, isEmissive);
				pendingSceneTriangles.push_back(triangle);
			}
		}
	};

	bool traversedWorldRoot = worldRootCaptureFrame == frame;
	if (!traversedWorldRoot && frame != std::numeric_limits<std::uint32_t>::max()) {
		if (auto* tes = RE::TES::GetSingleton()) {
			RE::NiNode* sceneRoot = tes->objRoot;
			if (auto* interior = tes->interiorCell; interior && interior->IsAttached()) {
				const auto& interiorRuntime = interior->GetRuntimeData();
				if (interiorRuntime.loadedData && interiorRuntime.loadedData->cell3D)
					sceneRoot = interiorRuntime.loadedData->cell3D.get();
			}
			if (sceneRoot) {
			traversedWorldRoot = true;
			worldRootCaptureFrame = frame;
			const auto eye = Util::GetEyePosition();
			const float voxelPitch = PIXL::HybridGIInternal::SceneVoxelGrid::kVoxelSize;
			const float snappedCenter[3] = {
				std::round(eye.x / voxelPitch) * voxelPitch,
				std::round(eye.y / voxelPitch) * voxelPitch,
				std::round(eye.z / voxelPitch) * voxelPitch
			};
			const float voxelHalfExtent = PIXL::HybridGIInternal::SceneVoxelGrid::kDimension * voxelPitch * 0.5f;
			using Candidate = std::pair<float, RE::BSGeometry*>;
			std::priority_queue<Candidate> nearest;
			TraverseScenegraphGeometriesGuarded(sceneRoot,
				{ snappedCenter[0], snappedCenter[1], snappedCenter[2] }, voxelHalfExtent, [&](RE::BSGeometry* geometry) {
				if (!geometry || pendingSceneGeometryTokens.size() >= kMaxFrameShapes || nearest.size() >= kMaxFrameShapes) {
					if (nearest.size() >= kMaxFrameShapes)
						return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (geometry) {
					const auto& runtime = geometry->GetGeometryRuntimeData();
					if (!runtime.skinInstance && !runtime.alphaProperty &&
						!(runtime.shaderProperty && runtime.shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha))) {
						const auto& bound = geometry->worldBound;
						const float radius = std::max(bound.radius, 0.0f);
						const float dx = bound.center.x - snappedCenter[0];
						const float dy = bound.center.y - snappedCenter[1];
						const float dz = bound.center.z - snappedCenter[2];
						// HTrace voxelizes a bounded, snapped camera volume. Exclude
						// scene geometry outside it before spending shape/triangle budget.
						const bool intersectsVoxelVolume = std::abs(dx) <= voxelHalfExtent + radius &&
							std::abs(dy) <= voxelHalfExtent + radius && std::abs(dz) <= voxelHalfExtent + radius;
						const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - radius, 0.0f);
						if (intersectsVoxelVolume && std::isfinite(distance)) {
							if (nearest.size() < kMaxFrameShapes)
								nearest.emplace(distance, geometry);
							else if (distance < nearest.top().first) {
								nearest.pop();
								nearest.emplace(distance, geometry);
							}
						}
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			std::vector<Candidate> ordered;
			ordered.reserve(nearest.size());
			while (!nearest.empty()) {
				ordered.push_back(nearest.top());
				nearest.pop();
			}
			std::vector<PIXL::HybridGIInternal::SceneGeometryBudgetCandidate> budgetCandidates;
			budgetCandidates.reserve(ordered.size());
			for (const auto& candidate : ordered) {
				auto* geometry = candidate.second;
				if (!geometry)
					continue;
				const auto& runtime = geometry->GetGeometryRuntimeData();
				auto* shape = geometry->AsTriShape();
				auto* rendererData = runtime.rendererData;
				if (!shape || runtime.skinInstance || runtime.alphaProperty ||
					(runtime.shaderProperty && runtime.shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha)) || !rendererData ||
					!rendererData->rawVertexData || !rendererData->rawIndexData ||
					!rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_VERTEX))
					continue;
				const auto shapeData = shape->GetTrishapeRuntimeData();
				const auto triangleCount = static_cast<std::uint32_t>(shapeData.triangleCount);
				const auto vertexCount = static_cast<std::uint32_t>(shapeData.vertexCount);
				const auto stride = rendererData->vertexDesc.GetSize();
				if (stride < sizeof(float) * 3 || triangleCount == 0 || triangleCount > 4096 ||
					vertexCount < 3 || vertexCount > 65535)
					continue;
				budgetCandidates.push_back({ reinterpret_cast<std::uintptr_t>(geometry), candidate.first, triangleCount });
			}

			// Spend the triangle budget by proximity first, then restore canonical
			// identity order so camera-distance changes do not reorder the snapshot.
			const auto admittedIndices = PIXL::HybridGIInternal::SelectSceneGeometryWithinBudget(
				budgetCandidates, kMaxFrameShapes, kMaxFrameTriangles);
			for (const auto index : admittedIndices)
				captureGeometry(reinterpret_cast<RE::BSGeometry*>(budgetCandidates[index].identity));
			}
		}
	}
	// Fallback for unusual render contexts whose submitted geometry is not under
	// TES::objRoot (menus and preview views); pointer identity is frame-local only.
	if (!traversedWorldRoot && a_pass)
		captureGeometry(a_pass->geometry);
}


void HybridGI::RestoreDefaultSettings()
{
	settings = {};
	recompileFlag = true;
}

void HybridGI::DrawSettings()
{
	// The unified module panel exposes every control regardless of legacy UI mode.
	constexpr bool showAdvanced = true;

	if (!ShadersOK()) {
		const bool coreComputeShadersReady = prefilterDepthsCompute && prefilterRadianceCompute &&
			prefilterNormalCompute && radianceDisoccCompute && giCompute;
		if (!coreComputeShadersReady) {
			Util::Text::Error("%s", T(TKEY("shader_compile_error"), "Required GI compute shaders are unavailable."));
			ImGui::TextWrapped("The effect will retry compilation automatically. Check PIXLRenderer.log for compiler diagnostics if the warning persists.");
		} else if (std::clamp(settings.ResolutionMode, 0, 2) != 0 && !upsampleCompute) {
			Util::Text::Error("%s", "The reduced-resolution GI reconstruction shader is unavailable.");
			ImGui::TextWrapped("GI cannot render at the selected resolution until the upsample shader is ready.");
		} else {
			Util::Text::Error("%s", "Hybrid GI render resources are unavailable.");
			ImGui::TextWrapped("The compute shaders are ready, but one or more required GPU resources could not be created.");
		}
	}
	if (settings.EnableWorldCache && !WorldCacheShadersOK())
		Util::Text::Error("%s", "World-cache shaders are unavailable; core screen-space GI will continue without persistent world irradiance.");
	if (settings.EnableExperimentalSpecularGI && !ReflectionShadersOK())
		Util::Text::Error("%s", "Hybrid Reflection tracing is unavailable; diffuse GI will continue normally.");
	else if (settings.EnableExperimentalSpecularGI && settings.EnableBlur && hybridReflectionDenoiseCompute.get() == nullptr)
		Util::Text::Error("%s", "Hybrid Reflection spatial reconstruction is unavailable; temporal reflection output will remain active.");
	if (settings.EnableBlur && !BlurShadersOK())
		Util::Text::Error("%s", "Diffuse spatial denoising is unavailable; the renderer will use the temporally reconstructed GI result.");

	const auto activePreset = DetectPreset();

	// -------------------------------------------------------------------------
	// QUICK SETUP
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("QUICK SETUP");
	ImGui::TextWrapped("Choose a quality target, then adjust bounced light and contact shading. Quality changes affect GPU cost; the sections below fine-tune the result.");

	if (ImGui::BeginTable("PIXL GI Status", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
		ImGui::TableNextColumn();
		DrawPixlToggleField(T(TKEY("enabled"), "Enabled"), &settings.Enabled);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Master switch for PIXL Hybrid GI. When disabled, the GI outputs are cleared and the remaining controls are ignored.");

		ImGui::TableNextColumn();
		{
			auto guard = Util::DisableGuard(!settings.Enabled);
			recompileFlag |= DrawPixlToggleField("Indirect Lighting", &settings.EnableGI);
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Enables bounced indirect light. Ambient occlusion can still be used independently. Changing this automatically rebuilds the affected compute permutation.");
		ImGui::TableNextColumn();
		{
			auto guard = Util::DisableGuard(!settings.Enabled);
			DrawPixlToggleField("PIXL Ambient Occlusion", &settings.EnableAO);
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Enables PIXL's screen-space crevice and contact-depth occlusion independently of indirect light and reflections.");

		ImGui::TableNextColumn();
		DrawPixlToggleField(T(TKEY("vanilla_ssao"), "Vanilla SSAO"), &settings.EnableVanillaSSAO);
		if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextWrapped("Usually leave this off while PIXL AO is active to avoid double-darkening corners and contact areas.");
		ImGui::EndTable();
	}

	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		ImGui::TextUnformatted("Quality Preset");
		if (ImGui::BeginTable("PIXL GI Presets", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
			for (const auto& [label, preset] : std::array{
				std::pair{ "Low", PixlQualityPreset::Performance },
				std::pair{ "Medium", PixlQualityPreset::Balanced },
				std::pair{ "High", PixlQualityPreset::Quality },
				std::pair{ "Ultra", PixlQualityPreset::Ultra } }) {
				ImGui::TableNextColumn();
				DrawPresetButton(label, preset, activePreset);
			}
			ImGui::EndTable();
		}

		ImGui::TextDisabled("Current: %s  |  %s resolution  |  %u x %u screen-space rays / steps",
			GetPresetName(DetectPreset()),
			GetResolutionName(settings.ResolutionMode),
			settings.NumSlices,
			settings.NumSteps);
		ImGui::TextDisabled("Ultra is the shipped PIXL baseline. Unified presets also coordinate contact shadows and Light Volumes without changing artistic GI tuning.");

		if (Util::WarningButton("Restore Unified Ultra")) {
			PIXLRenderer::QualityProfiles::Apply(
				PIXLRenderer::QualityProfiles::Group::Lighting,
				PIXLRenderer::QualityProfiles::Ultra);
			globals::state->Save();
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Restores the shipped unified Ultra lighting workload while preserving your artistic GI strength, colour and radius tuning.");

	}

	// -------------------------------------------------------------------------
	// LIGHTING BALANCE
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("LIGHTING BALANCE");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (BeginSettingsTable("PIXL GI Lighting")) {
			BeginSettingRow("Ambient Occlusion Strength", "Controls contact and crevice darkening. 1.0 is the default strength; increase carefully to avoid crushed corners.");
			{ auto aoGuard = Util::DisableGuard(!settings.EnableAO); ImGui::SliderFloat("##ao_strength", &settings.AOPower, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }

			BeginSettingRow("Contact Depth", "Adds restrained near-field grounding by reusing PIXL GI horizon samples. It does not run a second AO pass or darken direct lights.");
			{ auto aoGuard = Util::DisableGuard(!settings.EnableAO); PIXLUI::Toggle("##contact_depth", &settings.EnableContactDepth); }

			BeginSettingRow("Contact Depth Strength", "Controls small-scale contact and crevice depth. The contribution is bounded to prevent double-darkening with GI.");
			{
				auto contactGuard = Util::DisableGuard(!settings.EnableAO || !settings.EnableContactDepth);
				ImGui::SliderFloat("##contact_depth_strength", &settings.ContactDepthStrength, 0.0f, 0.75f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Indirect Light Strength", "Brightness of bounced diffuse lighting gathered by the GI system.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				ImGui::SliderFloat("##gi_strength", &settings.GIStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Indirect Light Saturation", "Colour carried by bounced lighting. Lower values make bounce light more neutral; 100% preserves the sampled colour.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				Util::PercentageSlider("##gi_saturation", &settings.GISaturation);
			}

			BeginSettingRow("AO Reach", "World-space radius of ambient occlusion. Smaller values create tighter contact shadows; larger values affect broader forms.");
			ImGui::SliderFloat("##ao_radius", &settings.AORadius, 10.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

			BeginSettingRow("GI Reach", "How far the screen-space GI searches for indirect-light contributors. Larger values cover broader lighting but increase the chance of sparse samples.");
			{
				auto ilGuard = Util::DisableGuard(!settings.EnableGI);
				ImGui::SliderFloat("##gi_radius", &settings.GIRadius, 10.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Effect Fade Distance", "Near/far distance where screen-space GI and AO progressively fade to avoid unstable far-field samples.");
			if (ImGui::SliderFloat2("##depth_fade", &settings.DepthFadeRange.x, 1e4f, 5e4f, "%.0f units")) {
				settings.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
				settings.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, settings.DepthFadeRange.y - 100.0f);
			}
			ImGui::EndTable();
		}
	}

	// -------------------------------------------------------------------------
	if (!WorldCacheAvailableForSSGITest())
		ImGui::TextDisabled("Persistent world cache is unavailable in this build.");
    if (WorldCacheAvailableForSSGITest()) {
	// WORLD-SPACE GI
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("WORLD GI");
	ImGui::TextWrapped("The world cache preserves useful lighting outside the current screen view, improving continuity while you turn the camera or move through a scene.");
	{
		auto worldGuard = Util::DisableGuard(!settings.Enabled || !settings.EnableGI);
		DrawPixlToggleField("World Irradiance Cache", &settings.EnableWorldCache);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Stores recently observed scene lighting in two world-space voxel cascades. It augments screen-space GI; unseen geometry is still unknown until observed.");

		{
			auto cacheGuard = Util::DisableGuard(!settings.EnableWorldCache);
			if (BeginSettingsTable("PIXL GI World Cache")) {
				BeginSettingRow("World GI Contribution", "Strength of lighting reconstructed from the persistent world cache. Screen-space GI remains the high-detail primary source.");
				ImGui::SliderFloat("##world_strength", &settings.WorldCacheStrength,
					static_cast<float>(PIXL::Metadata::Settings::HybridWorldCacheStrength.minimum),
					static_cast<float>(PIXL::Metadata::Settings::HybridWorldCacheStrength.maximum), "%.2f", ImGuiSliderFlags_AlwaysClamp);

				BeginSettingRow("World GI Coverage", "Maximum useful world-space radius around the camera. Increasing this extends continuity but makes the fixed cache represent a larger region.");
				ImGui::SliderFloat("##world_radius", &settings.WorldCacheRadius, 256.0f, 4096.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

				BeginSettingRow("Fire & Emissive Injection", "Seeds transparent torch, candle, brazier and glow-emitter lighting into visible world-cache surfaces using Radiant Grid. This fills energy that is absent from the opaque SSGI input.");
				PIXLUI::Toggle("##emitter_injection", &settings.EnableEmitterInjection);

				BeginSettingRow("Emitter Bounce Strength", "Controls the conservative local-light floor used for transparent emitters. Existing opaque scene radiance is never added twice.");
				{
					auto emitterGuard = Util::DisableGuard(!settings.EnableEmitterInjection);
					ImGui::SliderFloat("##emitter_injection_strength", &settings.EmitterInjectionStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}

				BeginSettingRow("Secondary Bounce", "Propagates a bounded amount of cached irradiance into neighbouring cells, carrying light farther around corners.");
				PIXLUI::Toggle("##second_bounce", &settings.EnableWorldCacheSecondBounce);

				BeginSettingRow("Secondary Bounce Strength", "Controls how much previous cached lighting is allowed to propagate. Keep this conservative to avoid energy growth.");
				{
					auto bounceGuard = Util::DisableGuard(!settings.EnableWorldCacheSecondBounce);
					ImGui::SliderFloat("##second_bounce_strength", &settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				}

				ImGui::EndTable();
			}
		}
	}
	}

	// -------------------------------------------------------------------------
	// VISIBILITY + REFLECTIONS
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("VISIBILITY & REFLECTIONS");
	ImGui::TextWrapped("Bent-normal visibility reuses the SSGI visibility rays to steer ambient and rough reflection energy away from blocked directions. Screen-Space Reflections adds stochastic GGX rays against the current depth and radiance buffers.");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (BeginSettingsTable("PIXL GI Visibility")) {
			BeginSettingRow("Bent-Normal Lighting", "Bends ambient/environment sampling toward the visible hemisphere. Most useful in deep corners, under pipes and beside large occluders.");
			PIXLUI::Toggle("##bent_normals", &settings.EnableBentNormalLighting);
			BeginSettingRow("Bent-Normal Strength", "0 keeps the geometric normal. 1 fully follows the visibility direction reconstructed by the horizon tracer.");
			{ auto g = Util::DisableGuard(!settings.EnableBentNormalLighting); ImGui::SliderFloat("##bent_strength", &settings.BentNormalStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
			BeginSettingRow("Physical Specular Occlusion", "Suppresses broad environment-reflection lobes when their visible hemisphere is blocked. Sharp mirror reflections remain much less affected than rough reflections.");
			PIXLUI::Toggle("##spec_occ", &settings.EnableSpecularOcclusion);
			BeginSettingRow("Specular Occlusion Strength", "Controls how strongly directional visibility affects rough AmbientProbe and reflection energy.");
			{ auto g = Util::DisableGuard(!settings.EnableSpecularOcclusion); ImGui::SliderFloat("##spec_occ_strength", &settings.SpecularOcclusionStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
			ImGui::EndTable();
		}

		ImGui::Spacing();
		recompileFlag |= DrawPixlToggleField("Screen-Space Reflections", &settings.EnableExperimentalSpecularGI);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Stochastic GGX reflections traced through the hierarchical screen-space depth and radiance buffers. Screen hits are temporally reconstructed; misses use the normal AmbientProbe/cubemap fallback. Changing this automatically rebuilds the reflection compute pipeline.");

		{
			auto reflectionGuard = Util::DisableGuard(!settings.EnableExperimentalSpecularGI);
			if (BeginSettingsTable("PIXL Hybrid Reflections")) {
				BeginSettingRow("Reflection Strength", "Overall contribution of scene-aware hybrid reflection transport.");
				ImGui::SliderFloat("##hybrid_reflection_strength", &settings.ReflectionIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Maximum Roughness", "Surfaces rougher than this use normal AmbientProbe/cubemaps only, avoiding low-value stochastic noise.");
				ImGui::SliderFloat("##hybrid_max_roughness", &settings.ReflectionMaxRoughness, 0.20f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Off-Screen World Fallback", "Lets the persistent world cache contribute when a reflection leaves the screen or has no reliable hit.");
				if (settings.EnableWorldCache) PIXLUI::Toggle("##world_reflection_fallback", &settings.EnableVoxelReflections);
				if (settings.EnableWorldCache) BeginSettingRow("Fallback Strength", "Strength of the coarse world-cache reflection fallback. Best on rough metal/glossy stone rather than perfect mirrors.");
				if (settings.EnableWorldCache) { auto g = Util::DisableGuard(!settings.EnableVoxelReflections); ImGui::SliderFloat("##world_reflection_strength", &settings.VoxelReflectionStrength, 0.0f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp); }
				ImGui::EndTable();
			}
		}

		if (showAdvanced && settings.EnableExperimentalSpecularGI && ImGui::CollapsingHeader("Reflection tracing details")) {
			ImGui::TextDisabled("Advanced Reflection Tracing");
			if (BeginSettingsTable("PIXL Hybrid Reflections Advanced")) {
				BeginSettingRow("Trace Distance", "Maximum view-space distance a screen-space reflection ray can travel before falling back.");
				ImGui::SliderFloat("##reflection_distance", &settings.ReflectionMaxDistance, 256.0f, 8192.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Trace Steps", "Maximum hierarchical depth tests per stochastic reflection ray.");
				Util::UIntSlider("##reflection_steps", &settings.ReflectionSteps, 8, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Surface Thickness", "Depth tolerance for accepting a screen-space intersection.");
				ImGui::SliderFloat("##reflection_thickness", &settings.ReflectionThickness, 2.0f, 96.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Temporal Response", "How quickly stochastic history accepts new reflection samples. Lower is steadier; higher reacts faster.");
				ImGui::SliderFloat("##reflection_temporal", &settings.ReflectionTemporalResponse, 0.04f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Firefly Limit", "Soft luminance knee for isolated reflection outliers before temporal accumulation.");
				ImGui::SliderFloat("##reflection_firefly", &settings.ReflectionFireflyClamp, 1.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("World Fallback Blend", "Confidence multiplier for world-cache miss fill before deferred cubemap/AmbientProbe remains in charge.");
				ImGui::SliderFloat("##reflection_world_mix", &settings.ReflectionWorldFallbackStrength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("World Fallback Roughness Limit", "Upper roughness limit for the coarse world-cache fallback. Mirror-like surfaces are suppressed automatically because low-frequency SH cannot reproduce sharp reflections.");
				ImGui::SliderFloat("##reflection_world_roughness_cutoff", &settings.VoxelReflectionRoughnessCutoff, 0.20f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Ray Bias", "Offsets reflection rays away from the receiving surface to reduce self-intersections.");
				ImGui::SliderFloat("##reflection_bias", &settings.ReflectionRayBias, 1.0f, 24.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);
				BeginSettingRow("Roughness Stochasticity", "0 approaches a stable mirror direction; 1 samples the full GGX visible-normal distribution.");
				ImGui::SliderFloat("##reflection_jitter", &settings.ReflectionRoughnessJitter, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::EndTable();
			}
		}
	}

	// -------------------------------------------------------------------------
	// TEMPORAL STABILITY / DENOISING
	// -------------------------------------------------------------------------
	PIXLUI::SectionBanner("STABILITY & DENOISING");
	{
		auto guard = Util::DisableGuard(!settings.Enabled);
		if (ImGui::BeginTable("PIXL GI Denoiser Toggles", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableNextColumn();
			const bool temporalChanged = DrawPixlToggleField("Temporal Accumulation", &settings.EnableTemporalDenoiser);
			recompileFlag |= temporalChanged;
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Reprojects stable GI/reflection history using depth, world-position and normal rejection. Changing this automatically rebuilds the affected compute permutations and clears incompatible history.");
			ImGui::TableNextColumn();
			DrawPixlToggleField("Spatial Denoiser", &settings.EnableBlur);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Edge-aware spatial reconstruction for diffuse GI and stochastic Hybrid Reflections. Mirror-like reflections stay intentionally sharp.");
			ImGui::TableNextColumn();
			{
				auto adaptiveGuard = Util::DisableGuard(!settings.EnableBlur);
				DrawPixlToggleField("Adaptive 2-Pass", &settings.EnableAdaptiveDenoiser);
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::TextWrapped("Runs a second edge-aware spatial pass where geometric confidence is low. Applies immediately without a shader rebuild.");
			ImGui::EndTable();
		}

		if (BeginSettingsTable("PIXL GI Stability")) {
			BeginSettingRow("History Length", "Maximum number of frames accumulated. Higher values reduce noise but can increase persistence/ghosting after rapid changes.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::UIntSlider("##history_frames", &settings.MaxAccumFrames, 1, 64, "%d frames", ImGuiSliderFlags_AlwaysClamp);
			}

			BeginSettingRow("Depth Rejection", "Rejects temporal history when reprojected depth changes too much. Lower values are stricter and reduce trails at geometry edges.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::PercentageSlider("##depth_rejection", &settings.DepthDisocclusion, 0.0f, 20.0f);
			}

			BeginSettingRow("Normal Rejection", "Rejects history when surface orientation changes. This is especially useful on animated geometry, thin silhouettes and changing normals.");
			{
				auto temporalGuard = Util::DisableGuard(!settings.EnableTemporalDenoiser);
				Util::PercentageSlider("##normal_rejection", &settings.NormalDisocclusion, 0.0f, 100.0f);
			}

			BeginSettingRow("Denoise Radius", "Spatial blur radius in pixels. Larger values hide more noise but can soften small indirect-light detail.");
			{
				auto blurGuard = Util::DisableGuard(!settings.EnableBlur);
				ImGui::SliderFloat("##blur_radius", &settings.BlurRadius, 0.0f, 30.0f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
			}
			ImGui::EndTable();
		}
	}

	// -------------------------------------------------------------------------
	// ADVANCED
	// -------------------------------------------------------------------------
	if (showAdvanced && ImGui::CollapsingHeader("Sampling & cache quality")) {
		ImGui::TextWrapped("These controls trade GPU cost, cache responsiveness and reconstruction stability. Presets manage them automatically for most users.");
		{
			auto guard = Util::DisableGuard(!settings.Enabled);

			if (ImGui::CollapsingHeader("Screen-Space Sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (BeginSettingsTable("PIXL GI Advanced Screen")) {
					BeginSettingRow("Internal Resolution", "Full preserves maximum detail. Half trades some reconstruction work for performance; Quarter is intended for performance-constrained systems.");
					static constexpr const char* resolutionNames[] = { "Full", "Half", "Quarter" };
					if (ImGui::Combo("##resolution_mode", &settings.ResolutionMode, resolutionNames, static_cast<int>(std::size(resolutionNames))))
						recompileFlag = true;

					BeginSettingRow("Sample Directions", "Number of angular directions traced per pixel. More directions reduce directional noise but increase GI cost.");
					Util::UIntSlider("##num_slices", &settings.NumSlices, 2, 8, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Steps Per Direction", "Number of horizon samples along each direction. More steps improve spatial accuracy and large-radius stability.");
					Util::UIntSlider("##num_steps", &settings.NumSteps, 4, 24, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Adaptive Ray Allocation", "Reduces SSGI sample directions only after temporal history is geometry-validated and converged. Newly visible, disoccluded, or lighting-reactive pixels keep the full ray count. Requires Temporal Denoiser; lower minimum work trades spatial noise for GPU savings.");
					recompileFlag |= DrawPixlToggleField("##adaptive_ray_tiles", &settings.EnableAdaptiveRayAllocation);
                    BeginSettingRow("Experimental Cache Confidence", "Tracks world-cache lighting variance and observation reliability. Disabled by default pending visual validation; adds 512 KiB of cache storage.");
                    recompileFlag |= DrawPixlToggleField("##cache_intelligence", &settings.EnableCacheIntelligence);
                    BeginSettingRow("Experimental Cache Update Budget", "Prioritizes changed and uncertain cells using delayed GPU timings. Requires Cache Confidence; reserves 512 KiB while enabled. Reference updates remain active when timing data is unavailable.");
                    DrawPixlToggleField("##cache_update_budget", &settings.EnableBudgetedCacheUpdates);

					BeginSettingRow("Adaptive Minimum Ray Fraction", "Lowest fraction of configured directions used for converged pixels. Steps per direction are unchanged; at 50%, stable pixels use about half the rays.");
					{
						auto adaptiveGuard = Util::DisableGuard(!settings.EnableAdaptiveRayAllocation || !settings.EnableTemporalDenoiser);
						ImGui::SliderFloat("##adaptive_ray_minimum", &settings.AdaptiveRayMinimum, 0.35f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
					}

					BeginSettingRow("Minimum Screen Radius", "Prevents the far-field effect radius collapsing below this fraction of the display width.");
					ImGui::SliderFloat("##min_screen_radius", &settings.MinScreenRadius, 0.0f, 0.05f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("AO Thickness", "Virtual occluder thickness used by the AO horizon test. Larger values can make thin geometry occlude more aggressively.");
					ImGui::SliderFloat("##thickness", &settings.Thickness, 0.0f, 128.0f, "%.1f units", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("GI Distance Compensation", "Biases distant radiance samples brighter or darker. Zero is neutral and recommended unless tuning a specific look.");
					ImGui::SliderFloat("##gi_distance_comp", &settings.GIDistanceCompensation, -5.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Geometry Blur Weight", "Higher values make the spatial denoiser more sensitive to geometry differences and preserve edges more strongly.");
					ImGui::SliderFloat("##distance_normalisation", &settings.DistanceNormalisation, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Radiance Firefly Limit", "Soft-limits isolated radiance spikes before they enter the persistent world cache. Raise only if very bright emissive bounce looks unnecessarily compressed.");
					ImGui::SliderFloat("##radiance_firefly_limit", &settings.RadianceFireflyClamp, 0.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Upsample Edge Sensitivity", "Depth discontinuity threshold used when reconstructing half/quarter-resolution GI. Lower values preserve thin geometry more aggressively; higher values blend more smoothly.");
					ImGui::SliderFloat("##upsample_edge_threshold", &settings.UpsampleEdgeThreshold, 0.02f, 0.35f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					ImGui::EndTable();
				}
			}

		if (WorldCacheAvailableForSSGITest() && ImGui::CollapsingHeader("World Cache Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
				auto cacheGuard = Util::DisableGuard(!settings.EnableWorldCache);
				if (BeginSettingsTable("PIXL GI Advanced Cache")) {
					BeginSettingRow("World Samples", "Number of cache samples used during world-space reconstruction. Higher values reduce cache noise at additional cost.");
					Util::UIntSlider("##world_samples", &settings.WorldCacheSampleCount, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Cone Trace Steps", "Maximum voxel steps used when sampling world irradiance/occlusion. Higher values see farther through the cache but cost more.");
					Util::UIntSlider("##trace_steps", &settings.WorldCacheTraceSteps, 2, 3, "%d", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Near Voxel Size", "World-space size of high-detail cache cells around the camera. Smaller cells preserve more detail but cover less space.");
					ImGui::SliderFloat("##near_voxel", &settings.WorldCacheCellSizeNear, 64.0f, 256.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Far Voxel Size", "World-space size of the lower-frequency cache cascade. It is clamped to at least the near-cell size during upload.");
					ImGui::SliderFloat("##far_voxel", &settings.WorldCacheCellSizeFar, 256.0f, 1024.0f, "%.0f units", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Leak Rejection", "Rejects cache samples that are likely to cross geometry boundaries. Higher values reduce light leaks but can discard useful indirect light.");
					ImGui::SliderFloat("##leak_reduction", &settings.WorldCacheLeakReduction, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

					BeginSettingRow("Cache Update Speed", "How quickly an existing voxel accepts newly observed lighting. Lower values are steadier; higher values respond faster to real lighting changes.");
					ImGui::SliderFloat("##cache_response", &settings.WorldCacheTemporalResponse, 0.02f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
					BeginSettingRow("Cache Lifetime", "How long unseen voxels remain available. Old cells begin fading after one quarter of this time; shorter lifetimes reduce stale lighting trails, while longer lifetimes preserve more off-screen bounce.");
					int cacheLifetimeSeconds = std::clamp(static_cast<int>(std::lround(settings.WorldCacheMaxAge / 8.0f)), 1, 15);
					if (ImGui::SliderInt("##cache_lifetime", &cacheLifetimeSeconds, 1, 15, "%d s", ImGuiSliderFlags_AlwaysClamp))
						settings.WorldCacheMaxAge = static_cast<uint>(cacheLifetimeSeconds) * 8u;
					BeginSettingRow("Injection Spacing", "Pixel stride used when injecting visible surfaces into the world cache. Smaller values update more pixels and cost more GPU time.");
					Util::UIntSlider("##injection_stride", &settings.WorldCacheInjectionStride, 1, 8, "%d px", ImGuiSliderFlags_AlwaysClamp);
					ImGui::EndTable();
				}
			}
		}
	}

	// Capture stays available outside Developer Mode without occupying the normal
	// lighting controls. The capture restores the previous view and menu state.
	if (ImGui::CollapsingHeader("Diagnostics")) {
		if (ImGui::Button("Capture PIXL GI Diagnostic Set", { -1.0f, 0.0f }))
			RequestDiagnosticCapture();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextWrapped("Captures AO, diffuse GI, reflections and supporting lighting views in PIXL/Diagnostics/Captures. Keep the camera still until capture completes.");
		if (diagnosticCaptureActive || diagnosticCaptureRequested.load(std::memory_order_acquire))
			ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), "Diagnostic capture active - keep the camera still");
	}

	// -------------------------------------------------------------------------
	// DIAGNOSTICS
	// -------------------------------------------------------------------------
	if (globals::state->IsDeveloperMode() && ImGui::CollapsingHeader("Developer & Debug Tools")) {
		static constexpr const char* debugViews[] = {
			"Composite",
			"AO Occlusion",
			"Diffuse GI",
			"Specular GI",
			"Lighting Input Radiance",
			"Voxel Confidence / Coverage",
			"Voxel Cascade",
			"Cached Irradiance",
			"Directional Occlusion (unavailable in SSGI)",
			"Voxel Reflection Fallback (unavailable in SSGI)",
			"Bent Normal",
			"Directional Visibility",
			"Physical Specular Occlusion",
			"Hybrid Reflections",
            "Cache Log Variance", "Cache Age", "Cache Reliability", "Cache State",
            "Cache Light Change", "Cache Camera-Plane Visibility"
		};

		if (BeginSettingsTable("PIXL GI Debug")) {
			BeginSettingRow("Full-Screen View", "Visualizes a single stage of the GI pipeline. Composite returns to normal gameplay output.");
			int debugView = static_cast<int>(settings.DebugView);
			if (ImGui::Combo("##debug_view", &debugView, debugViews, static_cast<int>(std::size(debugViews))))
				settings.DebugView = (debugView == 8 || debugView == 9) ? 0u :
					static_cast<uint>(std::clamp(debugView, 0, static_cast<int>(std::size(debugViews)) - 1));

			BeginSettingRow("Debug Exposure", "Display-only exposure multiplier used by diagnostic visualizations.");
			ImGui::SliderFloat("##debug_gain", &settings.DebugGain, 0.1f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndTable();
		}

		if (ImGui::TreeNode(T(TKEY("buffer_viewer"), "Buffer Viewer"))) {
			static float debugRescale = 0.3f;
			ImGui::SliderFloat(T(TKEY("view_resize"), "View Resize"), &debugRescale, 0.05f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			BUFFER_VIEWER_NODE(texNoise, debugRescale)
			BUFFER_VIEWER_NODE(texWorkingDepth, debugRescale)
			BUFFER_VIEWER_NODE(texPrevGeo, debugRescale)
			BUFFER_VIEWER_NODE(texRadiance, debugRescale)
			BUFFER_VIEWER_NODE(texAo[0], debugRescale)
			BUFFER_VIEWER_NODE(texAo[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlY[1], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[0], debugRescale)
			BUFFER_VIEWER_NODE(texIlCoCg[1], debugRescale)
			BUFFER_VIEWER_NODE(texGiSpecular[0], debugRescale)
			BUFFER_VIEWER_NODE(texGiSpecular[1], debugRescale)
			BUFFER_VIEWER_NODE(texBentVisibility[0], debugRescale)
			BUFFER_VIEWER_NODE(texBentVisibility[1], debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheMetadata, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH0, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH1, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheSH2, debugRescale)
			BUFFER_VIEWER_NODE(texWorldCacheNormal, debugRescale)

			ImGui::TreePop();
		}
	}
}

void HybridGI::RequestDiagnosticCapture()
{
	if (diagnosticCaptureActive || diagnosticCaptureRequested.exchange(true, std::memory_order_acq_rel))
		return;
	logger::info("[PIXL Hybrid GI] diagnostic capture requested");
}

void HybridGI::UpdateDiagnosticCapture()
{
	if (!diagnosticCaptureActive) {
		if (!diagnosticCaptureRequested.exchange(false, std::memory_order_acq_rel))
			return;

		diagnosticCaptureDirectory = Util::PathHelpers::GetDiagnosticsPath() /
		                             std::format("PIXL-HybridGI-{}", PixlDiagnosticTimestamp());
		std::error_code error;
		std::filesystem::create_directories(diagnosticCaptureDirectory, error);
		if (error) {
			logger::error("[PIXL Hybrid GI] failed to create diagnostic directory: {}", error.message());
			NotifyDiagnostic("PIXL GI dump failed - see PIXLRenderer.log");
			return;
		}

		diagnosticSavedMenuEnabled = Menu::GetSingleton()->IsEnabled;
		diagnosticSavedDebugView = settings.DebugView;
		diagnosticSavedMaterialDebugMode = globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode;
		Menu::GetSingleton()->IsEnabled = false;
		diagnosticCaptureIndex = NextDiagnosticView(0);
		diagnosticRecords.clear();
		diagnosticFrozenFrameIndex = globals::state->frameCount;
		diagnosticCaptureActive = true;
		diagnosticWaitingForScreenshot = false;
		ApplyDiagnosticView(diagnosticCaptureIndex);
		logger::info("[PIXL Hybrid GI] diagnostic capture started at {}", diagnosticCaptureDirectory.string());
		NotifyDiagnostic("PIXL GI dump started - keep the camera still");
		return;
	}

	if (diagnosticWaitingForScreenshot) {
		++diagnosticWaitFrames;
		const bool encoderBusy = globals::pipeline::pixelCapture.GetPendingCaptureCount() != 0;
		std::error_code existsError;
		const bool screenshotExists = std::filesystem::exists(diagnosticCurrentScreenshot, existsError);
		if (encoderBusy || !screenshotExists) {
			if (diagnosticWaitFrames < 900)
				return;
			logger::error("[PIXL Hybrid GI] timed out waiting for diagnostic screenshot {}", diagnosticCurrentScreenshot.string());
			FinishDiagnosticCapture(false);
			return;
		}

		diagnosticWaitingForScreenshot = false;
		diagnosticCaptureIndex = NextDiagnosticView(diagnosticCaptureIndex + 1);
		if (diagnosticCaptureIndex >= PixlDiagnosticViews.size()) {
			FinishDiagnosticCapture(true);
			return;
		}
		ApplyDiagnosticView(diagnosticCaptureIndex);
		return;
	}

	if (diagnosticSettleFrames != 0) {
		--diagnosticSettleFrames;
		return;
	}

	const auto& view = PixlDiagnosticViews.at(diagnosticCaptureIndex);
	diagnosticCurrentScreenshot = diagnosticCaptureDirectory / std::format("{}.png", view.name);
	globals::pipeline::pixelCapture.RequestCaptureToPath(diagnosticCurrentScreenshot, false);
	diagnosticRecords.push_back(DiagnosticRecord{
		std::string(view.name),
		diagnosticCurrentScreenshot.filename(),
		view.giDebugView,
		view.materialDebugMode,
		globals::state->frameCount });
	diagnosticWaitingForScreenshot = true;
	diagnosticWaitFrames = 0;
}

std::string_view HybridGI::DiagnosticViewUnavailableReason(size_t index) const
{
	const auto mode = PixlDiagnosticViews.at(index).giDebugView;
	if (mode == 8u || mode == 9u)
		return "legacy GI view is not implemented by the active SSGI kernel";
	if ((mode >= 5u && mode <= 7u) || mode == 9u || (mode >= 14u && mode <= 19u)) {
		if (!settings.EnableWorldCache || !WorldCacheAvailableForSSGITest())
			return "world cache disabled in this build or configuration";
		if (mode >= 14u && !settings.EnableCacheIntelligence)
			return "cache intelligence disabled";
		if (mode == 9u && !settings.EnableVoxelReflections)
			return "voxel reflection fallback disabled";
	}
	if (mode == 8u && (!settings.EnableWorldCache || !settings.EnableDirectionalOcclusion || !WorldCacheAvailableForSSGITest()))
		return "world cache directional occlusion disabled";
	if (mode == 2u && !settings.EnableGI)
		return "diffuse GI disabled";
	if (mode == 1u && !settings.EnableAO)
		return "PIXL ambient occlusion disabled";
	if ((mode == 3u || mode == 13u) && !settings.EnableExperimentalSpecularGI)
		return "screen-space reflections disabled";
	if (mode == 10u && !settings.EnableBentNormalLighting)
		return "bent-normal lighting disabled";
	if (mode == 12u && !settings.EnableSpecularOcclusion)
		return "specular occlusion disabled";
	return {};
}

size_t HybridGI::DiagnosticAvailableViewCount() const
{
	size_t count = 0;
	for (size_t index = 0; index < PixlDiagnosticViews.size(); ++index)
		count += DiagnosticViewUnavailableReason(index).empty();
	return count;
}

size_t HybridGI::NextDiagnosticView(size_t index) const
{
	while (index < PixlDiagnosticViews.size() && !DiagnosticViewUnavailableReason(index).empty())
		++index;
	return index;
}

void HybridGI::ApplyDiagnosticView(size_t index)
{
	const auto& view = PixlDiagnosticViews.at(index);
	settings.DebugView = view.giDebugView;
	globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode = view.materialDebugMode;
	diagnosticSettleFrames = 4;
	diagnosticWaitFrames = 0;
	diagnosticWaitingForScreenshot = false;
	logger::info("[PIXL Hybrid GI] diagnostic view {}/{}: {}",
		diagnosticRecords.size() + 1,
		DiagnosticAvailableViewCount(),
		view.name);
}

void HybridGI::FinishDiagnosticCapture(bool complete)
{
	settings.DebugView = diagnosticSavedDebugView;
	globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode = diagnosticSavedMaterialDebugMode;
	Menu::GetSingleton()->IsEnabled = diagnosticSavedMenuEnabled;
	diagnosticWaitingForScreenshot = false;
	diagnosticCaptureActive = false;
	// Diagnostic SH/material colours are visualization data, never reusable GI
	// history. Preserve the world cache itself and invalidate temporal sampling.
	queuedResetTemporalHistory = true;

	std::error_code copyError;
	const auto settingsPath = Util::PathHelpers::GetSettingsUserPath();
	if (!settingsPath.empty() && std::filesystem::exists(settingsPath))
		std::filesystem::copy_file(settingsPath, diagnosticCaptureDirectory / "SettingsUser.json", std::filesystem::copy_options::overwrite_existing, copyError);
	copyError.clear();
	const auto logPath = Util::PathHelpers::GetLogPath();
	if (!logPath.empty() && std::filesystem::exists(logPath))
		std::filesystem::copy_file(logPath, diagnosticCaptureDirectory / "PIXLRenderer.log", std::filesystem::copy_options::overwrite_existing, copyError);

	WriteDiagnosticManifest(complete);
	if (complete)
		logger::info("[PIXL Hybrid GI] diagnostic capture completed at {}", diagnosticCaptureDirectory.string());
	else
		logger::warn("[PIXL Hybrid GI] diagnostic capture failed at {}", diagnosticCaptureDirectory.string());
	NotifyDiagnostic(complete ?
		std::format("PIXL GI dump complete: {}", diagnosticCaptureDirectory.filename().string()) :
		"PIXL GI dump incomplete - see PIXLRenderer.log");
}

void HybridGI::WriteDiagnosticManifest(bool complete) const
{
	json root;
	root["schemaVersion"] = 7;
	root["product"] = "PIXL PBR Rendering Engine";
	root["productVersion"] = std::string(Plugin::DISPLAY_VERSION);
	root["capture"] = {
		{ "complete", complete },
		{ "frameIndex", globals::state->frameCount },
		{ "viewCount", diagnosticRecords.size() },
		{ "expectedViewCount", DiagnosticAvailableViewCount() },
		{ "menuRestored", Menu::GetSingleton()->IsEnabled == diagnosticSavedMenuEnabled },
		{ "captureStage", "presented-framebuffer" },
		{ "opaqueDebugIsolation", true },
		{ "lateTransparencyIsolation", false },
		{ "hudIsolation", false },
		{ "interpretation", "Each PNG contains an opaque PIXL debug visualization followed by Skyrim's late transparency/effect and HUD presentation passes. Repeated smoke or UI is overlay contamination, not data stored in every inspected buffer." }
	};
	for (size_t index = 0; index < PixlDiagnosticViews.size(); ++index) {
		const auto reason = DiagnosticViewUnavailableReason(index);
		if (!reason.empty())
			root["skippedViews"].push_back({ { "name", PixlDiagnosticViews[index].name }, { "reason", reason } });
	}
	root["settings"]["hybridGI"] = settings;
	const auto& reconstruction = globals::pipeline::imageReconstruction;
	const auto& camera = globals::pipeline::cameraSuite;
	const auto& menuSettings = Menu::GetSingleton()->GetSettings();
	root["settings"]["quality"] = {
		{ "renderer", menuSettings.RendererQuality },
		{ "lighting", menuSettings.LightingQuality },
		{ "materials", menuSettings.MaterialsQuality },
		{ "atmosphere", menuSettings.AtmosphereQuality },
		{ "water", menuSettings.WaterQuality },
		{ "terrainVegetation", menuSettings.TerrainVegetationQuality },
		{ "characters", menuSettings.CharactersQuality },
		{ "camera", menuSettings.CameraQuality }
	};
	root["settings"]["camera"] = {
		{ "physicalCamera", camera.settings.enablePhysicalCamera },
		{ "cameraInfluence", camera.settings.cameraInfluence },
		{ "autoExposure", camera.settings.cameraAutoExposure },
		{ "exposureCompensationEV", camera.settings.cameraExposureCompensationEV },
		{ "bloom", camera.settings.enableBloom },
		{ "bloomStrength", camera.settings.bloomStrength },
		{ "bloomThreshold", camera.settings.bloomThreshold },
		{ "bloomRadius", camera.settings.bloomRadius },
		{ "stormglass", camera.settings.enableStormglass },
		{ "stormglassStrength", camera.settings.stormglassStrength },
		{ "stormglassDropScale", camera.settings.stormglassDropScale },
		{ "stormglassRefraction", camera.settings.stormglassRefraction },
		{ "stormglassTrails", camera.settings.stormglassTrails },
		{ "stormglassDryingRate", camera.settings.stormglassDryingRate },
		{ "submergedOptics", camera.settings.enableSubmergedOptics },
		{ "submergedStrength", camera.settings.submergedStrength },
		{ "submergedBlur", camera.settings.submergedBlur },
		{ "submergedRefraction", camera.settings.submergedRefraction },
		{ "submergedTransitionSpeed", camera.settings.submergedTransitionSpeed },
		{ "depthOfField", camera.settings.enableEnhancedDepthOfField },
		{ "lutPreset", camera.settings.lookPreset },
		{ "lutOpacity", camera.settings.lookOpacity }
	};
	root["settings"]["physicalPBR"] = {
		{ "featureLoaded", globals::pipeline::materialForge.loaded },
		{ "settingsBytes", sizeof(MaterialForge::Settings) },
		{ "vertexAOStrength", globals::pipeline::materialForge.settings.VertexAOStrength },
		{ "legacyPhysicalDirectLighting", globals::pipeline::materialForge.settings.EnableLegacyPhysicalDirectLighting },
		{ "legacyPhysicalSpecularScale", globals::pipeline::materialForge.settings.LegacyPhysicalSpecularScale },
		{ "legacyMetalInference", globals::pipeline::materialForge.settings.EnableLegacyMetalInference },
		{ "legacyMetalInferenceStrength", globals::pipeline::materialForge.settings.LegacyMetalInferenceStrength },
		{ "legacyMetalInferenceThreshold", globals::pipeline::materialForge.settings.LegacyMetalInferenceThreshold },
		{ "legacyMetalInferenceMaximum", globals::pipeline::materialForge.settings.LegacyMetalInferenceMaximum },
		{ "physicalLocalLightFalloff", globals::pipeline::materialForge.settings.EnablePhysicalLocalLightFalloff },
		{ "physicalLocalLightFalloffStrength", globals::pipeline::materialForge.settings.PhysicalLocalLightFalloffStrength },
		{ "localLightMinimumDistance", globals::pipeline::materialForge.settings.LocalLightMinimumDistance },
		{ "localContactShadows", globals::pipeline::materialForge.settings.EnableLocalContactShadows },
		{ "localContactShadowLightCount", globals::pipeline::materialForge.settings.LocalContactShadowLightCount },
		{ "localContactShadowLength", globals::pipeline::materialForge.settings.LocalContactShadowLength },
		{ "localContactShadowStrength", globals::pipeline::materialForge.settings.LocalContactShadowStrength },
		{ "specularAA", globals::pipeline::materialForge.settings.EnableSpecularAA },
		{ "specularAAStrength", globals::pipeline::materialForge.settings.SpecularAAStrength },
		{ "specularAAVarianceClamp", globals::pipeline::materialForge.settings.SpecularAAVarianceClamp },
		{ "ggxMultiScatter", globals::pipeline::materialForge.settings.EnableGGXMultiScatter },
		{ "ggxMultiScatterStrength", globals::pipeline::materialForge.settings.GGXMultiScatterStrength }
	};
	const auto materialDiagnostics = PhysicalMaterial::Registry::GetSingleton().GetDiagnostics();
	const auto& ambientProbe = globals::pipeline::ambientProbe;
	const auto& radiantGrid = globals::pipeline::radiantGrid;
	root["settings"]["ambientProbe"] = {
		{ "enabled", ambientProbe.settings.EnableAmbientProbe != 0u },
		{ "disabledForCurrentScene", ambientProbe.IsDisabledForCurrentScene() },
		{ "dalcAmount", ambientProbe.settings.DALCAmount },
		{ "dalcMode", ambientProbe.settings.DALCMode },
		{ "interiorAmbientScale", ambientProbe.settings.InteriorAmbientScale },
		{ "disableInInteriors", ambientProbe.settings.DisableInInteriors }
	};
	root["classification"] = {
		{ "physicalMaterialCount", materialDiagnostics.materialCount },
		{ "automaticFurDescriptorCount", materialDiagnostics.furMaterialCount },
		{ "invalidPhysicalDescriptorCount", materialDiagnostics.invalidDescriptorCount },
		{ "radiantParticleEmitterCount", radiantGrid.particleEmitterLightCount },
		{ "radiantGlowMappedEmitterCount", radiantGrid.glowMappedEmitterLightCount },
		{ "radiantEmitterBufferStart", radiantGrid.particleLightBufferStart },
		{ "radiantEmitterBufferCount", radiantGrid.particleLightBufferCount },
		{ "emissiveViewScope", "opaque lighting geometry only; effect particles and embers are late transparent passes represented to HybridGI by RadiantGrid emitter proxies" }
	};
	root["runtime"] = {
		{ "featureLoaded", loaded },
		{ "shadersOK", ShadersOK() },
		{ "ambientOcclusionEnabled", settings.Enabled && settings.EnableAO },
		{ "diffuseGIEnabled", settings.Enabled && settings.EnableGI },
		{ "screenSpaceReflectionsEnabled", settings.Enabled && settings.EnableExperimentalSpecularGI },
		{ "screenSpacePointLightShadowsEnabled", radiantGrid.settings.EnableScreenSpaceLocalShadows },
		{ "localContactShadowsEnabled", globals::pipeline::materialForge.settings.EnableLocalContactShadows != 0u },
		{ "diffuseGIAlwaysBoundWithHQSpecular", true },
		{ "cacheConfidenceMaturation", true },
		{ "asymmetricBrightOutlierRejection", true },
		{ "visualizationInjectionIsolated", true },
		{ "diagnosticWorldCacheClockFrozen", true },
		{ "persistentWorldCache", true },
		{ "frameAgeExpiryEnabled", true },
		{ "worldCacheClockHz", 8.0f },
        { "worldCacheClock", worldCacheClock },
        { "cacheIntelligenceEnabled", settings.EnableCacheIntelligence },
        { "cacheIntelligenceAllocatedBytes", texWorldCacheIntelligence && texWorldCachePreviousIntelligence ? 524288 : 0 },
        { "cacheIntelligenceViews", "surface-cell values; 14 log variance, 15 age/lifetime, 16 reliability, 17 state, 18 unresolved light evidence, 19 camera/source plane compatibility; magenta means unavailable" },
        { "cacheStatePalette", "black missing; blue dark; green reliable; yellow uncertain; red stale light; orange occluder; dark grey empty; grey pending" },
		{ "worldCacheDeterministicElection", true },
		{ "worldCacheFrameRateIndependentResponse", true },
		{ "worldCacheBoundedToroidalWindow", true },
		{ "temporalHistoryResetAfterVisualization", true },
		{ "effectiveDepthFadeNear", std::clamp(settings.DepthFadeRange.x, 1e4f, std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f) - 100.f) },
		{ "effectiveDepthFadeFar", std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f) },
		{ "outputAOIndex", outputAoIdx },
		{ "outputGIIndex", outputIlIdx },
		{ "screenWidth", globals::game::graphicsState->screenWidth },
		{ "screenHeight", globals::game::graphicsState->screenHeight },
		{ "worldVoxelAtlasWidth", texWorldCacheMetadata ? texWorldCacheMetadata->desc.Width : 0 },
		{ "worldVoxelAtlasHeight", texWorldCacheMetadata ? texWorldCacheMetadata->desc.Height : 0 },
		{ "worldVoxelSHOrder", 2 },
		{ "worldVoxelSHCoefficients", 9 },
		{ "horizonMaskBins", 32 },
		{ "horizonMaskStorage", "uint-sm5" },
		{ "worldVoxelStorageBytes", 4456448 },
		{ "worldVoxelElectionBytes", 262144 },
		{ "worldVoxelSnapshotCopyBytesPerFrame", 2097152 },
		{ "worldCacheSecondBounce", settings.EnableWorldCacheSecondBounce },
		{ "worldCacheSecondBounceStrength", settings.WorldCacheSecondBounceStrength },
		{ "geometryAwareTemporalReprojection", true },
		{ "materialFresnelSpecularGI", true },
		{ "effectLightingScaleAppliedWithoutLinearLighting", true },
		{ "effectLightingScale", globals::pipeline::linearLightCore.settings.effectLightingMult },
		{ "linearLightingEnabled", globals::pipeline::linearLightCore.settings.enableLinearLightCore != 0u }
	};
    const auto emitterChanges = PIXL::Renderer::LightTransportWorld::Get().AcquireEmitterChanges();
    root["cacheEmitterChanges"] = {
        { "count", emitterChanges.count }, { "overflow", emitterChanges.overflow },
        { "frame", emitterChanges.frame }, { "epoch", emitterChanges.epoch },
        { "sequence", emitterChanges.sequence }, { "published", emitterChanges.published }
    };
    root["repeatableCacheCaptureRecipes"] = {
        { "torch", "Load the same save; wait six seconds facing a torch; capture; walk backward for two seconds; stop and capture again." },
        { "doorway", "Load the same save facing a lit doorway; capture on each side at the same camera height and weather/time." },
        { "cameraCut", "Capture a warmed scene; switch first/third person or teleport; capture immediately and after six seconds." },
        { "floorStack", "Capture directly above and below the same floor/roof, then follow a fixed stair/bridge path through cascade boundaries." },
        { "reflection", "Capture at fixed grazing angles; translate past a doorway and distant silhouette, using the same reconstruction settings." }
    };
	const float renderScaleX = std::clamp(reconstruction.resolutionScale.x, 0.0f, 1.0f);
	const float renderScaleY = std::clamp(reconstruction.resolutionScale.y, 0.0f, 1.0f);
	root["reconstruction"] = {
		{ "method", static_cast<uint32_t>(reconstruction.GetUpscaleMethod()) },
		{ "qualityMode", reconstruction.settings.qualityMode },
		{ "renderScaleX", renderScaleX },
		{ "renderScaleY", renderScaleY },
		{ "estimatedRenderWidth", static_cast<uint32_t>(std::lround(globals::game::graphicsState->screenWidth * renderScaleX)) },
		{ "estimatedRenderHeight", static_cast<uint32_t>(std::lround(globals::game::graphicsState->screenHeight * renderScaleY)) },
		{ "frameGenerationRequested", reconstruction.settings.frameGenerationMode != 0u },
		{ "frameGenerationInteropActive", reconstruction.IsFrameGenerationDx12PathActive() },
		{ "frameGenerationProducingFrames", reconstruction.IsFrameGenerationActive() },
		{ "dynamicResolutionWidthRatio", reconstruction.dynamicResolutionWidthRatio },
		{ "dynamicResolutionHeightRatio", reconstruction.dynamicResolutionHeightRatio },
		{ "jitterX", reconstruction.jitter.x },
		{ "jitterY", reconstruction.jitter.y }
	};

	auto recordBuffer = [&root](std::string_view name, const Texture2D* texture) {
		json entry{
			{ "name", name },
			{ "resident", texture && texture->resource }
		};
		if (texture && texture->resource) {
			entry["width"] = texture->desc.Width;
			entry["height"] = texture->desc.Height;
			entry["mipLevels"] = texture->desc.MipLevels;
			entry["arraySize"] = texture->desc.ArraySize;
			entry["format"] = static_cast<uint32_t>(texture->desc.Format);
			entry["bindFlags"] = texture->desc.BindFlags;
		}
		root["buffers"].push_back(std::move(entry));
	};
	recordBuffer("workingDepth", texWorkingDepth.get());
	recordBuffer("reflectionHiZ", texReflectionDepth.get());
	recordBuffer("previousGeometry", texPrevGeo.get());
	recordBuffer("radiance", texRadiance.get());
	recordBuffer("radianceScratch", texRadianceTemp.get());
	for (size_t index = 0; index < 2; ++index) {
		recordBuffer(std::format("accumulationHistory{}", index), texAccumFrames[index].get());
		recordBuffer(std::format("ambientOcclusion{}", index), texAo[index].get());
		recordBuffer(std::format("diffuseLuminance{}", index), texIlY[index].get());
		recordBuffer(std::format("diffuseChroma{}", index), texIlCoCg[index].get());
		recordBuffer(std::format("specularRadiance{}", index), texGiSpecular[index].get());
		recordBuffer(std::format("bentVisibility{}", index), texBentVisibility[index].get());
	}
	recordBuffer("worldMetadata", texWorldCacheMetadata.get());
	recordBuffer("worldWinners", texWorldCacheWinners.get());
	recordBuffer("worldSH0", texWorldCacheSH0.get());
	recordBuffer("worldSH1", texWorldCacheSH1.get());
	recordBuffer("worldSH2", texWorldCacheSH2.get());
	recordBuffer("worldNormal", texWorldCacheNormal.get());
    recordBuffer("worldIntelligence", texWorldCacheIntelligence.get());
    recordBuffer("previousWorldIntelligence", texWorldCachePreviousIntelligence.get());
	recordBuffer("previousWorldMetadata", texWorldCachePreviousMetadata.get());
	recordBuffer("previousWorldSH0", texWorldCachePreviousSH0.get());
	recordBuffer("previousWorldSH1", texWorldCachePreviousSH1.get());
	recordBuffer("previousWorldSH2", texWorldCachePreviousSH2.get());
	recordBuffer("previousWorldNormal", texWorldCachePreviousNormal.get());
	root["shaders"] = {
		{ "prefilterDepths", prefilterDepthsCompute != nullptr },
		{ "reflectionHiZ", reflectionDepthCompute != nullptr },
		{ "prefilterRadiance", prefilterRadianceCompute != nullptr },
		{ "prefilterNormal", prefilterNormalCompute != nullptr },
		{ "radianceDisocclusion", radianceDisoccCompute != nullptr },
		{ "gi", giCompute != nullptr },
		{ "denoise", blurCompute != nullptr },
		{ "denoiseAtrous", blurAtrousCompute != nullptr },
		{ "upsample", upsampleCompute != nullptr },
		{ "voxelInjection", worldCacheInjectCompute != nullptr },
		{ "voxelSelection", worldCacheSelectCompute != nullptr },
		{ "voxelDecay", worldCacheDecayCompute != nullptr },
		{ "hybridReflections", hybridReflectionCompute != nullptr },
		{ "hybridReflectionDenoise", hybridReflectionDenoiseCompute != nullptr }
	};
	if (auto* shaderCache = globals::shaderCache) {
		const auto total = shaderCache->GetTotalTasks();
		const auto completed = shaderCache->GetCompletedTasks();
		const auto failed = shaderCache->GetFailedTasks();
		const auto compiling = shaderCache->IsCompiling();
		const auto currentFailed = shaderCache->GetCurrentFailedCount();
		root["shaderCompilation"] = {
			{ "compiling", compiling },
			{ "totalTasks", total },
			{ "completedTasks", completed },
			{ "failedTasks", failed },
			{ "currentFailedShaders", currentFailed },
			{ "diskHitTasks", shaderCache->GetDiskHitTasks() },
			{ "pendingTasks", total > completed + failed ? total - completed - failed : 0u },
			{ "readyForVisualDiagnosis", !compiling && total == completed + failed && currentFailed == 0 }
		};
	}

	bool allScreenshotsPresent = diagnosticRecords.size() == DiagnosticAvailableViewCount();
	uint64_t screenshotBytes = 0;
	for (const auto& record : diagnosticRecords) {
		std::error_code fileError;
		const auto screenshotPath = diagnosticCaptureDirectory / record.file;
		const bool filePresent = std::filesystem::exists(screenshotPath, fileError);
		const uint64_t fileBytes = filePresent ? std::filesystem::file_size(screenshotPath, fileError) : 0u;
		allScreenshotsPresent = allScreenshotsPresent && filePresent && !fileError && fileBytes > 0u;
		screenshotBytes += fileBytes;
		root["screenshots"].push_back({
			{ "name", record.name },
			{ "file", record.file.generic_string() },
			{ "filePresent", filePresent && !fileError },
			{ "fileBytes", fileBytes },
			{ "debugView", record.debugView },
			{ "materialDebugMode", record.materialDebugMode },
			{ "frameIndex", record.frameIndex }
		});
	}
	root["captureValidation"] = {
		{ "allScreenshotsPresent", allScreenshotsPresent },
		{ "totalScreenshotBytes", screenshotBytes },
		{ "bufferInventoryCount", root["buffers"].size() },
		{ "authoritative", complete && allScreenshotsPresent }
	};

	if (globals::profiler) {
		for (const auto& timer : globals::profiler->GetResults()) {
			if (!timer.name.starts_with("HybridGI::") && timer.name != "DeferredComposite")
				continue;
            // Request-only statistics; the profiler already resolves timestamps
            // asynchronously. Do not add a GPU wait or normal-frame readback.
            std::array<float, Profiler::kHistorySize> samples{};
            const auto count = std::min(timer.historyCount, Profiler::kHistorySize);
            for (uint32_t i = 0; i < count; ++i)
                samples[i] = timer.GetHistorySample(i);
            std::sort(samples.begin(), samples.begin() + count);
			root["timings"].push_back({
				{ "name", timer.name },
				{ "valid", timer.valid },
				{ "gpuMs", timer.gpuTimeMs },
				{ "averageMs", timer.avgMs },
                { "samples", count },
                { "p50Ms", count ? samples[count / 2] : 0.0f },
                { "maxMs", count ? samples[count - 1] : 0.0f },
				{ "p95Ms", timer.p95Ms },
				{ "p99Ms", timer.p99Ms },
				{ "cpuMs", timer.cpuTimeMs }
			});
		}
	}

	constexpr std::array<std::string_view, 15> shaderFiles{
		"ssgi.cs.hlsl", "worldCache.hlsli", "worldCacheInject.cs.hlsl", "worldCacheDecay.cs.hlsl",
		"hybridReflection.cs.hlsl", "hybridReflectionDenoise.cs.hlsl", "blur.cs.hlsl", "radianceDisocc.cs.hlsl",
		"prefilterDepths.cs.hlsl", "prefilterRadiance.cs.hlsl", "prefilterNormal.cs.hlsl",
		"upsample.cs.hlsl", "common.hlsli", "worldCacheMark.cs.hlsl", "worldCacheEvidence.cs.hlsl"
	};
	for (const auto shaderFile : shaderFiles) {
		const auto path = Util::PathHelpers::GetPIXLShadersPath() / "HybridGI" / shaderFile;
		std::error_code sizeError;
		if (!std::filesystem::exists(path))
			continue;
		root["shaderSources"].push_back({
			{ "file", shaderFile },
			{ "bytes", std::filesystem::file_size(path, sizeError) },
			{ "fnv1a64", std::format("{:016X}", HashFileFNV1a(path)) }
		});
	}

	std::ofstream stream(diagnosticCaptureDirectory / "manifest.json");
	if (!stream) {
		logger::error("[PIXL Hybrid GI] failed to write diagnostic manifest");
		return;
	}
	stream << std::setw(2) << root;
}

void HybridGI::NotifyDiagnostic(std::string message)
{
	if (auto* taskInterface = SKSE::GetTaskInterface()) {
		taskInterface->AddTask([message = std::move(message)]() {
			RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true);
		});
	}
}

void HybridGI::LoadSettings(json& o_json)
{
	if (!o_json.is_object()) {
		settings = {};
		recompileFlag = true;
		queuedResetHistory = true;
		return;
	}
	settings = o_json;
	// This test build replaces the world cache and scene-voxel path with the
	// standalone screen-space integrator. Ignore obsolete saved cache toggles.
	settings.EnableWorldCache = kEnableWorldCacheForSSGITest && settings.EnableWorldCache;
	settings.EnableDirectionalOcclusion = kEnableWorldCacheForSSGITest && settings.EnableDirectionalOcclusion;
	settings.EnableWorldCacheSecondBounce = kEnableWorldCacheForSSGITest && settings.EnableWorldCacheSecondBounce;
	// Append-only settings are loaded explicitly because the legacy settings
	// serializer is already at its macro field limit. Old presets receive the
	// PIXL defaults; new presets round-trip all four values.
	settings.EnableContactDepth = o_json.value("EnableContactDepth", true);
	settings.EnableAO = o_json.value("EnableAO", true);
	settings.ContactDepthStrength = o_json.value("ContactDepthStrength", 0.35f);
	settings.ContactDepthRadius = o_json.value("ContactDepthRadius", 96.0f);
	settings.ContactDepthBias = o_json.value("ContactDepthBias", 0.08f);
	settings.EnableAdaptiveRayAllocation = o_json.value("EnableAdaptiveRayAllocation", false);
    settings.EnableCacheIntelligence = o_json.value("EnableCacheIntelligence", false);
    settings.EnableBudgetedCacheUpdates = o_json.value("EnableBudgetedCacheUpdates", false);
	settings.AdaptiveRayMinimum = o_json.value("AdaptiveRayMinimum", 0.50f);
	settings.EnableEmitterInjection = o_json.value("EnableEmitterInjection", true);
	settings.EnableCacheIntelligence = kEnableWorldCacheForSSGITest && settings.EnableCacheIntelligence;
	settings.EnableBudgetedCacheUpdates = kEnableWorldCacheForSSGITest && settings.EnableBudgetedCacheUpdates;
	settings.EnableEmitterInjection = kEnableWorldCacheForSSGITest && settings.EnableEmitterInjection;
	settings.EmitterInjectionStrength = o_json.value("EmitterInjectionStrength", 0.65f);
	const Settings defaults{};
	const auto finiteOr = [](float value, float fallback) {
		return std::isfinite(value) ? value : fallback;
	};
	settings.WorldCacheTemporalResponse = finiteOr(settings.WorldCacheTemporalResponse, defaults.WorldCacheTemporalResponse);
	settings.RadianceFireflyClamp = finiteOr(settings.RadianceFireflyClamp, defaults.RadianceFireflyClamp);
	settings.ReflectionTemporalResponse = finiteOr(settings.ReflectionTemporalResponse, defaults.ReflectionTemporalResponse);
	settings.DepthFadeRange.x = finiteOr(settings.DepthFadeRange.x, defaults.DepthFadeRange.x);
	settings.DepthFadeRange.y = finiteOr(settings.DepthFadeRange.y, defaults.DepthFadeRange.y);
	settings.ResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	// Keep persisted/UI values inside the ranges implemented by ssgi.cs.hlsl.
	settings.NumSlices = std::clamp(settings.NumSlices, 2u, 8u);
	settings.NumSteps = std::clamp(settings.NumSteps, 4u, 24u);
	settings.DebugView = std::min(settings.DebugView, 19u);
	if (settings.DebugView == 8u || settings.DebugView == 9u)
		settings.DebugView = 0u;
	settings.WorldCacheTraceSteps = std::clamp(settings.WorldCacheTraceSteps, 2u, 3u);
	settings.WorldCacheTemporalResponse = std::clamp(settings.WorldCacheTemporalResponse, 0.02f, 1.0f);
	settings.WorldCacheSecondBounceStrength = std::clamp(settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f);
	settings.VoxelReflectionStrength = std::clamp(settings.VoxelReflectionStrength, 0.0f, 1.5f);
	settings.VoxelReflectionRoughnessCutoff = std::clamp(settings.VoxelReflectionRoughnessCutoff, 0.2f, 1.0f);
	settings.BentNormalStrength = std::clamp(settings.BentNormalStrength, 0.0f, 1.0f);
	settings.SpecularOcclusionStrength = std::clamp(settings.SpecularOcclusionStrength, 0.0f, 1.0f);
	settings.RadianceFireflyClamp = std::clamp(settings.RadianceFireflyClamp, 0.0f, 64.0f);
	settings.UpsampleEdgeThreshold = std::clamp(settings.UpsampleEdgeThreshold, 0.02f, 0.35f);
	settings.ReflectionIntensity = std::clamp(settings.ReflectionIntensity, 0.0f, 2.0f);
	settings.ReflectionMaxRoughness = std::clamp(settings.ReflectionMaxRoughness, 0.05f, 1.0f);
	settings.ReflectionMaxDistance = std::clamp(settings.ReflectionMaxDistance, 256.0f, 8192.0f);
	settings.ReflectionThickness = std::clamp(settings.ReflectionThickness, 1.0f, 128.0f);
	settings.ReflectionSteps = std::clamp(settings.ReflectionSteps, 8u, 64u);
	settings.ReflectionTemporalResponse = std::clamp(settings.ReflectionTemporalResponse, 0.02f, 1.0f);
	settings.ReflectionFireflyClamp = std::clamp(settings.ReflectionFireflyClamp, 0.0f, 64.0f);
	settings.ReflectionWorldFallbackStrength = std::clamp(settings.ReflectionWorldFallbackStrength, 0.0f, 1.5f);
	settings.ReflectionRayBias = std::clamp(settings.ReflectionRayBias, 1.0f, 64.0f);
	settings.ReflectionRoughnessJitter = std::clamp(settings.ReflectionRoughnessJitter, 0.0f, 1.0f);
	settings.ContactDepthStrength = std::clamp(settings.ContactDepthStrength, 0.0f, 0.75f);
	settings.ContactDepthRadius = std::clamp(settings.ContactDepthRadius, 24.0f, 256.0f);
	settings.ContactDepthBias = std::clamp(settings.ContactDepthBias, 0.0f, 0.35f);
	settings.AdaptiveRayMinimum = std::clamp(settings.AdaptiveRayMinimum, 0.35f, 1.0f);
	settings.EmitterInjectionStrength = std::clamp(settings.EmitterInjectionStrength, 0.0f, 1.5f);
	settings.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
	settings.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, settings.DepthFadeRange.y - 100.f);

	recompileFlag = true;
	queuedResetHistory = true;
}

void HybridGI::SaveSettings(json& o_json)
{
	o_json = settings;
	o_json["EnableAO"] = settings.EnableAO;
	o_json["EnableContactDepth"] = settings.EnableContactDepth;
	o_json["ContactDepthStrength"] = settings.ContactDepthStrength;
	o_json["ContactDepthRadius"] = settings.ContactDepthRadius;
	o_json["ContactDepthBias"] = settings.ContactDepthBias;
	o_json["EnableAdaptiveRayAllocation"] = settings.EnableAdaptiveRayAllocation;
    o_json["EnableCacheIntelligence"] = settings.EnableCacheIntelligence;
    o_json["EnableBudgetedCacheUpdates"] = settings.EnableBudgetedCacheUpdates;
	o_json["AdaptiveRayMinimum"] = settings.AdaptiveRayMinimum;
	o_json["EnableEmitterInjection"] = settings.EnableEmitterInjection;
	o_json["EmitterInjectionStrength"] = settings.EmitterInjectionStrength;
}

RE::BSEventNotifyControl HybridGI::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event)
		return RE::BSEventNotifyControl::kContinue;

	if (a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening)
		globals::pipeline::hybridGI.queuedResetHistory = true;
	return RE::BSEventNotifyControl::kContinue;
}

bool HybridGI::MenuOpenCloseEventHandler::Register()
{
	static MenuOpenCloseEventHandler singleton;
	auto ui = globals::game::ui;
	if (!ui) {
		logger::error("UI event source not found");
		return false;
	}
	auto* eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();
	if (!eventSource) {
		logger::error("[PIXL Hybrid GI] Menu event source not found");
		return false;
	}
	eventSource->AddEventSink(&singleton);
	return true;
}

void HybridGI::PostPostLoad()
{
	MenuOpenCloseEventHandler::Register();
}

void HybridGI::SetupResources()
{
    // Legacy scene-voxel injection is intentionally dormant in the SSGI test
    // implementation. It also avoids traversing live scene nodes from the
    // shader setup hook, which appeared in the 2026-11-04 crash stack.
    sceneVoxelDimension = 0;
    cacheScheduleCB.reset();
    texWorldUpdateTime.reset();
    texPreviousWorldUpdateTime.reset();
    cacheBudget.Reset();
	texReflectionDepth = nullptr;
	for (auto& view : uavReflectionDepth)
		view = nullptr;
	reflectionDepthCreationAttempted = false;
	auto renderer = globals::game::renderer;
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!renderer || !device || !context) {
		logger::error("[PIXL Hybrid GI] Renderer/device/context unavailable; module will remain disabled");
		return;
	}
	if (!temporalHistoryId) {
		temporalHistoryId = PIXL::Renderer::TemporalContext::Get().RegisterHistory({
			.name = "HybridGI screen-space accumulation",
			.owner = "HybridGI",
			.invalidateOn = PIXL::Renderer::AllTemporalInvalidations,
			.reset = [this](PIXL::Renderer::TemporalInvalidationReason) { queuedResetTemporalHistory = true; }
		});
	}
	if (!worldHistoryId) {
		using Reason = PIXL::Renderer::TemporalInvalidationReason;
		worldHistoryId = PIXL::Renderer::TemporalContext::Get().RegisterHistory({
			.name = "HybridGI world irradiance cache",
			.owner = "HybridGI",
			.invalidateOn = PIXL::Renderer::TemporalMask(Reason::Teleport) |
				PIXL::Renderer::TemporalMask(Reason::WorldspaceChange) |
				PIXL::Renderer::TemporalMask(Reason::RenderOriginShift) |
				PIXL::Renderer::TemporalMask(Reason::SettingsChange) |
				PIXL::Renderer::TemporalMask(Reason::ModuleReset) |
				PIXL::Renderer::TemporalMask(Reason::DeviceReset),
			.reset = [this](Reason) { queuedResetWorldHistory = true; }
		});
	}

	// This buffer is part of the screen-space GI path, even when the optional
	// world irradiance cache and its scene-voxel capture are disabled.
	try {
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<HybridGICB>(), "HybridGI::CB");
	} catch (const std::exception& error) {
		logger::error("[PIXL Hybrid GI] Screen-space constant buffer allocation failed; GI will remain unavailable: {}", error.what());
		return;
	}

	if (WorldCacheAvailableForSSGITest() && settings.EnableWorldCache && sceneVoxelDimension > 0u) try {
	logger::debug("Creating scene buffers and the HTrace-style voxel volume...");
	{
		sceneVoxelCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SceneVoxelCB>(), "HybridGI::SceneVoxelCB");
		sceneVoxelizeCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SceneVoxelizeCB>(), "HybridGI::SceneVoxelizeCB");
		sceneVoxelScrollCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SceneVoxelScrollCB>(), "HybridGI::SceneVoxelScrollCB");
		D3D11_BUFFER_DESC triangleDesc{};
		triangleDesc.ByteWidth = PIXL::HybridGIInternal::kMaxGpuSceneTriangles * sizeof(PIXL::HybridGIInternal::GpuSceneTriangle);
		triangleDesc.Usage = D3D11_USAGE_DYNAMIC;
		triangleDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		triangleDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		triangleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		triangleDesc.StructureByteStride = sizeof(PIXL::HybridGIInternal::GpuSceneTriangle);
		sceneTriangleBuffer = eastl::make_unique<Buffer>(triangleDesc, nullptr, "HybridGI::SceneTriangles");
		D3D11_SHADER_RESOURCE_VIEW_DESC triangleSrv{};
		triangleSrv.Format = DXGI_FORMAT_UNKNOWN;
		triangleSrv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		triangleSrv.Buffer.NumElements = PIXL::HybridGIInternal::kMaxGpuSceneTriangles;
		sceneTriangleBuffer->CreateSRV(triangleSrv);
		D3D11_TEXTURE3D_DESC voxelDesc{};
		voxelDesc.Width = sceneVoxelDimension;
		voxelDesc.Height = sceneVoxelDimension;
		voxelDesc.Depth = sceneVoxelDimension;
		voxelDesc.MipLevels = 1;
		voxelDesc.Format = DXGI_FORMAT_R32_UINT;
		voxelDesc.Usage = D3D11_USAGE_DEFAULT;
		voxelDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texSceneVoxels = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::CapturedSceneVoxels");
		texSceneVoxelsScratch = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::CapturedSceneVoxelsScratch");
		texSceneVoxelWinners = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::SceneVoxelRadianceWinners");
		sceneVoxelContentsInitialized = false;
		D3D11_SHADER_RESOURCE_VIEW_DESC voxelSrv{};
		voxelSrv.Format = voxelDesc.Format;
		voxelSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		voxelSrv.Texture3D.MipLevels = 1;
		texSceneVoxels->CreateSRV(voxelSrv);
		texSceneVoxelsScratch->CreateSRV(voxelSrv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC voxelUav{};
		voxelUav.Format = voxelDesc.Format;
		voxelUav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		voxelUav.Texture3D.WSize = sceneVoxelDimension;
		texSceneVoxels->CreateUAV(voxelUav);
		texSceneVoxelsScratch->CreateUAV(voxelUav);
		sceneVoxelUAV = texSceneVoxels->uav;
		sceneVoxelScratchUAV = texSceneVoxelsScratch->uav;
		texSceneVoxelWinners->CreateSRV(voxelSrv);
		texSceneVoxelWinners->CreateUAV(voxelUav);
		sceneVoxelWinnerUAV = texSceneVoxelWinners->uav;
		texSceneVoxelSurface = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::SceneVoxelSurface");
		texSceneVoxelSurfaceScratch = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::SceneVoxelSurfaceScratch");
		texSceneVoxelSurface->CreateSRV(voxelSrv);
		texSceneVoxelSurfaceScratch->CreateSRV(voxelSrv);
		texSceneVoxelSurface->CreateUAV(voxelUav);
		texSceneVoxelSurfaceScratch->CreateUAV(voxelUav);
		sceneVoxelSurfaceUAV = texSceneVoxelSurface->uav;
		sceneVoxelSurfaceScratchUAV = texSceneVoxelSurfaceScratch->uav;
		texSceneVoxelMaterial = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::SceneVoxelMaterial");
		texSceneVoxelMaterialScratch = eastl::make_unique<Texture3D>(voxelDesc, "HybridGI::SceneVoxelMaterialScratch");
		texSceneVoxelMaterial->CreateSRV(voxelSrv);
		texSceneVoxelMaterialScratch->CreateSRV(voxelSrv);
		texSceneVoxelMaterial->CreateUAV(voxelUav);
		texSceneVoxelMaterialScratch->CreateUAV(voxelUav);
		sceneVoxelMaterialUAV = texSceneVoxelMaterial->uav;
		sceneVoxelMaterialScratchUAV = texSceneVoxelMaterialScratch->uav;

		D3D11_TEXTURE3D_DESC radianceVoxelDesc = voxelDesc;
		radianceVoxelDesc.Format = DXGI_FORMAT_R32G32_UINT;
		texSceneVoxelRadiance = eastl::make_unique<Texture3D>(radianceVoxelDesc, "HybridGI::SceneVoxelRadiance");
		D3D11_SHADER_RESOURCE_VIEW_DESC radianceVoxelSrv{};
		radianceVoxelSrv.Format = radianceVoxelDesc.Format;
		radianceVoxelSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		radianceVoxelSrv.Texture3D.MipLevels = 1;
		texSceneVoxelRadiance->CreateSRV(radianceVoxelSrv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC radianceVoxelUav{};
		radianceVoxelUav.Format = radianceVoxelDesc.Format;
		radianceVoxelUav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		radianceVoxelUav.Texture3D.WSize = sceneVoxelDimension;
		texSceneVoxelRadiance->CreateUAV(radianceVoxelUav);
		sceneVoxelRadianceUAV = texSceneVoxelRadiance->uav;
		const UINT emptyRadiance[4]{};
		context->ClearUnorderedAccessViewUint(sceneVoxelRadianceUAV.get(), emptyRadiance);
		const double voxelCells = static_cast<double>(sceneVoxelDimension) * sceneVoxelDimension * sceneVoxelDimension;
		logger::info("[PIXL Hybrid GI] Scene voxel volume ready: {}^3 cells at {} world units/cell; texture storage is approximately {:.0f} MiB.",
			sceneVoxelDimension, PIXL::HybridGIInternal::SceneVoxelGrid::kVoxelSize,
			voxelCells * 36.0 / (1024.0 * 1024.0));
	}

	} catch (const std::exception& error) {
		logger::error("[PIXL Hybrid GI] Scene voxel allocation failed; screen-space GI will continue: {}", error.what());
		sceneVoxelDimension = 0;
		sceneTriangleBuffer.reset();
		texSceneVoxels.reset();
		texSceneVoxelsScratch.reset();
		texSceneVoxelWinners.reset();
		texSceneVoxelRadiance.reset();
		texSceneVoxelSurface.reset();
		texSceneVoxelSurfaceScratch.reset();
		texSceneVoxelMaterial.reset();
		texSceneVoxelMaterialScratch.reset();
		sceneVoxelUAV = nullptr;
		sceneVoxelScratchUAV = nullptr;
		sceneVoxelWinnerUAV = nullptr;
		sceneVoxelRadianceUAV = nullptr;
		sceneVoxelSurfaceUAV = nullptr;
		sceneVoxelSurfaceScratchUAV = nullptr;
		sceneVoxelMaterialUAV = nullptr;
		sceneVoxelMaterialScratchUAV = nullptr;
		sceneVoxelContentsInitialized = false;
	}
	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = 64,
			.Height = 64,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(REX::W32::AsW32(&texDesc));
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 5;

		{
			texRadiance = eastl::make_unique<Texture2D>(texDesc, "HybridGI::Radiance");
			texRadiance->CreateSRV(srvDesc);
			// No default UAV needed: prefilterRadiance binds per-mip UAVs via uavRadiance[].

			// Create individual UAVs for each mip level for prefiltering
			for (uint i = 0; i < 5; ++i) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = DXGI_FORMAT_R11G11B10_FLOAT,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texRadiance->resource.get(), &mipUavDesc, uavRadiance[i].put()));
				Util::SetResourceName(uavRadiance[i].get(), "HybridGI::Radiance UAV mip%u", i);
			}

			// Staging texture for mip 0 radiance. radianceDisocc writes it directly,
			// prefilterRadiance reads it as SRV and writes the mip chain back to texRadiance.
			// Avoids a full-texture CopySubresourceRegion each frame.
			D3D11_TEXTURE2D_DESC tempTexDesc = texDesc;
			tempTexDesc.MipLevels = 1;
			tempTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			D3D11_SHADER_RESOURCE_VIEW_DESC tempSrvDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};

			D3D11_UNORDERED_ACCESS_VIEW_DESC tempUavDesc = {
				.Format = DXGI_FORMAT_R11G11B10_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			texRadianceTemp = eastl::make_unique<Texture2D>(tempTexDesc, "HybridGI::RadianceTemp");
			texRadianceTemp->CreateSRV(tempSrvDesc);
			texRadianceTemp->CreateUAV(tempUavDesc);
		}

		texDesc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		texDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		{
			texWorkingDepth = eastl::make_unique<Texture2D>(texDesc, "HybridGI::WorkingDepth");
			texWorkingDepth->CreateSRV(srvDesc);
			for (int i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texWorkingDepth->resource.get(), &uavDesc, uavWorkingDepth[i].put()));
				Util::SetResourceName(uavWorkingDepth[i].get(), "HybridGI::WorkingDepth UAV mip%d", i);
			}
		}

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		{
			texNormal = eastl::make_unique<Texture2D>(texDesc, "HybridGI::Normal");
			texNormal->CreateSRV(srvDesc);
			for (uint i = 0; i < 5; ++i) {
				uavDesc.Texture2D.MipSlice = i;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavNormal[i].put()));
				Util::SetResourceName(uavNormal[i].get(), "HybridGI::Normal UAV mip%u", i);
			}
		}

		uavDesc.Texture2D.MipSlice = 0;
		texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
		// All single-mip storage textures use the same descriptor contract.
		// Capture the descriptors by reference to preserve the format transitions below.
		auto createStorageTexture = [&](const char* name) {
			auto texture = eastl::make_unique<Texture2D>(texDesc, name);
			texture->CreateSRV(srvDesc);
			texture->CreateUAV(uavDesc);
			return texture;
		};
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texIlY[0] = createStorageTexture("HybridGI::IlY[0]");
		texIlY[1] = createStorageTexture("HybridGI::IlY[1]");
		texGiSpecular[0] = createStorageTexture("HybridGI::GiSpecular[0]");
		texGiSpecular[1] = createStorageTexture("HybridGI::GiSpecular[1]");
		texBentVisibility[0] = createStorageTexture("HybridGI::BentVisibility[0]");
		texBentVisibility[1] = createStorageTexture("HybridGI::BentVisibility[1]");

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		texIlCoCg[0] = createStorageTexture("HybridGI::IlCoCg[0]");
		texIlCoCg[1] = createStorageTexture("HybridGI::IlCoCg[1]");

		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R8_UNORM;
		texAo[0] = createStorageTexture("HybridGI::AO[0]");
		texAo[1] = createStorageTexture("HybridGI::AO[1]");
		texAccumFrames[0] = createStorageTexture("HybridGI::AccumFrames[0]");
		texAccumFrames[1] = createStorageTexture("HybridGI::AccumFrames[1]");

		// Temporal geometry is a packed validation payload: 16-bit fixed view depth plus
		// two 8-bit octahedral world-normal components. R32_UINT keeps the same
		// 4-byte footprint while preserving much more useful far-depth precision.
		srvDesc.Format = uavDesc.Format = texDesc.Format = DXGI_FORMAT_R32_UINT;
		{
			texPrevGeo = eastl::make_unique<Texture2D>(texDesc, "HybridGI::PrevGeo");
			texPrevGeo->CreateSRV(srvDesc);
			texPrevGeo->CreateUAV(uavDesc);
		}

		if constexpr (kEnableWorldCacheForSSGITest) {
		// PIXL world cache: two flattened 32^3 cascades. Metadata and surface
		// publication remain atomic R32_UINT. Three RGBA16F payloads hold nine L2
		// luminance SH coefficients plus chroma ratios; sparse injection keeps the
		// additional bandwidth bounded.
		texDesc.Width = 32u * 32u;
		texDesc.Height = 32u * 2u;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_UINT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		srvDesc.Format = DXGI_FORMAT_R32_UINT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uavDesc.Format = DXGI_FORMAT_R32_UINT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D = { .MipSlice = 0 };

		texWorldCacheMetadata = createStorageTexture("PIXL HybridGI::WorldMetadata");
		texWorldCacheWinners = createStorageTexture("PIXL HybridGI::WorldWinners");
		texWorldCacheNormal = createStorageTexture("PIXL HybridGI::WorldNormal");
        try {
            emitterChangesCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<EmitterChangesCB>(), "HybridGI::EmitterChanges");
            texWorldCacheIntelligence = createStorageTexture("PIXL HybridGI::WorldIntelligence");
            texWorldCachePreviousIntelligence = createStorageTexture("PIXL HybridGI::Previous WorldIntelligence");
        } catch (...) {
            texWorldCacheIntelligence.reset();
            texWorldCachePreviousIntelligence.reset();
            settings.EnableCacheIntelligence = false;
            logger::warn("[HybridGI] Cache intelligence allocation failed; retaining legacy world cache.");
        }
		texWorldCachePreviousMetadata = createStorageTexture("PIXL HybridGI::Previous WorldMetadata");
		texWorldCachePreviousNormal = createStorageTexture("PIXL HybridGI::Previous WorldNormal");
		const UINT clearValue[4] = { 0, 0, 0, 0 };
		for (auto* texture : { texWorldCacheMetadata.get(), texWorldCacheNormal.get(),
			texWorldCachePreviousMetadata.get(), texWorldCachePreviousNormal.get(), texWorldCacheIntelligence.get(), texWorldCachePreviousIntelligence.get() })
			if (texture && texture->uav)
                context->ClearUnorderedAccessViewUint(texture->uav.get(), clearValue);

		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texWorldCacheSH0 = createStorageTexture("PIXL HybridGI::WorldSH L0L1");
		texWorldCacheSH1 = createStorageTexture("PIXL HybridGI::WorldSH L2A");
		texWorldCacheSH2 = createStorageTexture("PIXL HybridGI::WorldSH L2B Chroma");
		texWorldCachePreviousSH0 = createStorageTexture("PIXL HybridGI::Previous WorldSH L0L1");
		texWorldCachePreviousSH1 = createStorageTexture("PIXL HybridGI::Previous WorldSH L2A");
		texWorldCachePreviousSH2 = createStorageTexture("PIXL HybridGI::Previous WorldSH L2B Chroma");
		const FLOAT clearFloat[4] = { 0.f, 0.f, 0.f, 0.f };
		for (auto* texture : { texWorldCacheSH0.get(), texWorldCacheSH1.get(), texWorldCacheSH2.get(),
			texWorldCachePreviousSH0.get(), texWorldCachePreviousSH1.get(), texWorldCachePreviousSH2.get() })
			context->ClearUnorderedAccessViewFloat(texture->uav.get(), clearFloat);
		}
	}

	logger::debug("Loading noise texture...");
	{
		DirectX::ScratchImage image;
		try {
			std::filesystem::path path{ "Data\\PIXL-Shaders\\HybridGI\\fast_2uges.dds" };

			DX::ThrowIfFailed(LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texNoise = eastl::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource), "HybridGI::Noise");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texNoise->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texNoise->CreateSRV(srvDesc);
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
		Util::SetResourceName(linearClampSampler.get(), "HybridGI::LinearClampSampler");

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, pointClampSampler.put()));
		Util::SetResourceName(pointClampSampler.get(), "HybridGI::PointClampSampler");
	}

	CompileComputeShaders();
	if (texRadiance) {
		const std::uint64_t pixels = static_cast<std::uint64_t>(texRadiance->desc.Width) * texRadiance->desc.Height;
		PIXL::Renderer::TemporalContext::Get().SetHistoryMemory(temporalHistoryId, pixels * 64u);
	}
	PIXL::Renderer::TemporalContext::Get().SetHistoryMemory(worldHistoryId,
		static_cast<std::uint64_t>(32u * 32u * 32u * 2u) * 68u);
}

namespace
{
	enum HybridGIShaderMask : std::uint32_t
	{
		kShaderPrefilterDepth = 1u << 0,
		kShaderPrefilterRadiance = 1u << 1,
		kShaderPrefilterNormal = 1u << 2,
		kShaderRadianceDisocc = 1u << 3,
		kShaderGI = 1u << 4,
		kShaderBlur = 1u << 5,
		kShaderBlurAtrous = 1u << 6,
		kShaderUpsample = 1u << 7,
		kShaderWorldInject = 1u << 8,
		kShaderWorldSelect = 1u << 9,
		kShaderWorldDecay = 1u << 10,
		kShaderReflection = 1u << 11,
		kShaderReflectionDenoise = 1u << 12,
		kShaderWorldMark = 1u << 13,
        kShaderWorldEvidence = 1u << 14,
		kShaderVoxelize = 1u << 15,
		kShaderVoxelScroll = 1u << 16,
		kShaderVoxelRadianceSelect = 1u << 17,
		kShaderVoxelRadianceResolve = 1u << 18,
		kShaderVoxelLighting = 1u << 19,
		kShaderAll = (1u << 20) - 1u
	};
}

void HybridGI::ClearShaderCache()
{
	// Public/framework shader-cache invalidation keeps its original semantics:
	// source hot-reloads and device rebuilds must never depend on settings deltas.
	for (auto* shader : { &prefilterDepthsCompute, &reflectionDepthCompute,
		&prefilterRadianceCompute, &prefilterNormalCompute, &radianceDisoccCompute,
		&giCompute, &blurCompute, &blurAtrousCompute, &upsampleCompute,
		&worldCacheInjectCompute, &worldCacheSelectCompute, &worldCacheDecayCompute, &worldCacheMarkCompute, &worldCacheEvidenceCompute, &worldCacheEvidenceResolveCompute,
		&hybridReflectionCompute, &hybridReflectionDenoiseCompute, &sceneVoxelizeCompute, &sceneVoxelScrollCompute,
		&sceneVoxelRadianceSelectCompute, &sceneVoxelRadianceResolveCompute, &sceneVoxelLightingCompute })
		*shader = nullptr;
	CompileComputeShaders(kShaderAll);
	queuedResetTemporalHistory = true;
	queuedResetWorldHistory = true;
}

void HybridGI::RecompileChangedShaders()
{
	std::uint32_t mask = 0u;
	const int resolutionMode = std::clamp(settings.ResolutionMode, 0, 2);

	if (!compiledShaderConfigInitialized) {
		mask = kShaderAll;
	} else if (compiledResolutionMode != resolutionMode) {
		// Resolution defines alter the shared OUT_FRAME contract across most passes.
		// Rebuild the complete set rather than maintaining a fragile exception list.
		mask = kShaderAll;
	} else {
		if (compiledTemporalDenoiser != settings.EnableTemporalDenoiser) {
			mask |= kShaderRadianceDisocc | kShaderGI | kShaderBlur | kShaderBlurAtrous | kShaderReflection;
		}
		if (compiledGI != settings.EnableGI)
			mask |= kShaderGI;
		if (compiledHybridReflections != settings.EnableExperimentalSpecularGI)
			// The active SSGI/AO tracer has no reflection-dependent branch. Only
			// history remapping needs the changed specular output contract.
			mask |= kShaderRadianceDisocc;
		if (compiledAdaptiveRayAllocation != settings.EnableAdaptiveRayAllocation)
			mask |= kShaderGI;
	}

	if (compiledCacheIntelligence != settings.EnableCacheIntelligence)
        mask = kShaderAll;

    if (mask == 0u) {
		recompileFlag = false;
		return;
	}

	auto clearIf = [&](std::uint32_t bit, winrt::com_ptr<ID3D11ComputeShader>& shader) {
		if ((mask & bit) != 0u)
			shader = nullptr;
	};
	clearIf(kShaderPrefilterDepth, prefilterDepthsCompute);
	clearIf(kShaderPrefilterDepth, reflectionDepthCompute);
	clearIf(kShaderPrefilterRadiance, prefilterRadianceCompute);
	clearIf(kShaderPrefilterNormal, prefilterNormalCompute);
	clearIf(kShaderRadianceDisocc, radianceDisoccCompute);
	clearIf(kShaderGI, giCompute);
	clearIf(kShaderBlur, blurCompute);
	clearIf(kShaderBlurAtrous, blurAtrousCompute);
	clearIf(kShaderUpsample, upsampleCompute);
	clearIf(kShaderWorldInject, worldCacheInjectCompute);
	clearIf(kShaderWorldSelect, worldCacheSelectCompute);
	clearIf(kShaderWorldDecay, worldCacheDecayCompute);
    clearIf(kShaderWorldMark, worldCacheMarkCompute);
    clearIf(kShaderWorldEvidence, worldCacheEvidenceCompute);
    clearIf(kShaderWorldEvidence, worldCacheEvidenceResolveCompute);
	clearIf(kShaderReflection, hybridReflectionCompute);
	clearIf(kShaderReflectionDenoise, hybridReflectionDenoiseCompute);
	clearIf(kShaderVoxelize, sceneVoxelizeCompute);
	clearIf(kShaderVoxelScroll, sceneVoxelScrollCompute);
	clearIf(kShaderVoxelRadianceSelect, sceneVoxelRadianceSelectCompute);
	clearIf(kShaderVoxelRadianceResolve, sceneVoxelRadianceResolveCompute);
	clearIf(kShaderVoxelLighting, sceneVoxelLightingCompute);

	CompileComputeShaders(mask);
}

void HybridGI::CompileComputeShaders(std::uint32_t a_shaderMask)
{
	struct ShaderCompileInfo
	{
		std::uint32_t bit;
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::array<ShaderCompileInfo, 14> shaderInfos{{
		{ kShaderPrefilterDepth, &prefilterDepthsCompute, "prefilterDepths.cs.hlsl", { { "LINEAR_FILTER", "" } } },
		{ kShaderPrefilterDepth, &reflectionDepthCompute, "prefilterDepths.cs.hlsl", { { "MIN_FILTER", "" } } },
		{ kShaderPrefilterRadiance, &prefilterRadianceCompute, "prefilterRadiance.cs.hlsl", {} },
		{ kShaderPrefilterNormal, &prefilterNormalCompute, "prefilterNormal.cs.hlsl", {} },
		{ kShaderRadianceDisocc, &radianceDisoccCompute, "radianceDisocc.cs.hlsl", {} },
		{ kShaderGI, &giCompute, "ssgi.cs.hlsl", {} },
		{ kShaderBlur, &blurCompute, "blur.cs.hlsl", {} },
		{ kShaderBlurAtrous, &blurAtrousCompute, "blur.cs.hlsl", { { "ATROUS_STEP_2", "" } } },
		{ kShaderUpsample, &upsampleCompute, "upsample.cs.hlsl", {} },
		{ kShaderWorldInject, &worldCacheInjectCompute, "worldCacheInject.cs.hlsl", {} },
		{ kShaderWorldSelect, &worldCacheSelectCompute, "worldCacheInject.cs.hlsl", { { "WORLD_CACHE_SELECT", "" } } },
		{ kShaderWorldDecay, &worldCacheDecayCompute, "worldCacheDecay.cs.hlsl", {} },
		{ kShaderReflection, &hybridReflectionCompute, "hybridReflection.cs.hlsl", {} },
		{ kShaderReflectionDenoise, &hybridReflectionDenoiseCompute, "hybridReflectionDenoise.cs.hlsl", {} },
	}};

	bool intelligenceCompileFailed = false;
	for (auto& info : shaderInfos) {
		if ((a_shaderMask & info.bit) == 0u ||
			((info.bit == kShaderWorldInject || info.bit == kShaderWorldSelect || info.bit == kShaderWorldDecay) && !settings.EnableWorldCache) ||
			((info.bit == kShaderWorldMark || info.bit == kShaderWorldEvidence) && !settings.EnableCacheIntelligence))
			continue;
		// CompileShader hashes the complete define list. This explicit source
		// revision forces only HybridGI's standalone compute artifacts to rebuild
		// without raising the core feature compatibility version in Module.ini.
		info.defines.push_back({ "PIXL_HYBRIDGI_MODULE_REVISION", "\"5-0-8-world-cache-ssgi\"" });
		if (settings.ResolutionMode == 1)
			info.defines.push_back({ "HALF_RES", "" });
		if (settings.ResolutionMode == 2)
			info.defines.push_back({ "QUARTER_RES", "" });
		if (settings.EnableTemporalDenoiser)
			info.defines.push_back({ "TEMPORAL_DENOISER", "" });
		if (settings.EnableGI)
			info.defines.push_back({ "GI", "" });
		if (settings.EnableExperimentalSpecularGI) {
			info.defines.push_back({ "GI_SPECULAR", "" });
			info.defines.push_back({ "HYBRID_REFLECTIONS", "" });
		}
		if (settings.EnableAdaptiveRayAllocation)
			info.defines.push_back({ "ADAPTIVE_RAY_ALLOCATION", "" });
        if (settings.EnableCacheIntelligence)
            info.defines.push_back({ "CACHE_INTELLIGENCE", "" });
		auto path = std::filesystem::path("Data\\PIXL-Shaders\\HybridGI") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.programPtr->attach(rawPtr);
		else if (settings.EnableCacheIntelligence)
			intelligenceCompileFailed = true;
	}
	if (intelligenceCompileFailed) {
		// Never combine new producers with legacy consumers (or vice versa).
		// Retry the complete legacy set once; normal core/cache guards handle
		// any independent failure in that reference path.
		logger::warn("[HybridGI] Cache intelligence shader compilation failed; restoring legacy shaders.");
		settings.EnableCacheIntelligence = false;
		queuedResetWorldHistory = true;
		queuedResetTemporalHistory = true;
		for (auto& info : shaderInfos)
			*info.programPtr = nullptr;
		CompileComputeShaders(kShaderAll);
		return;
	}

	const bool requiredCoreShadersReady = prefilterDepthsCompute && prefilterRadianceCompute &&
		prefilterNormalCompute && radianceDisoccCompute && giCompute &&
		(std::clamp(settings.ResolutionMode, 0, 2) == 0 || upsampleCompute);
	if (!requiredCoreShadersReady) {
		// Compilation can be cancelled when the global shader-cache generation
		// changes during startup. Do not latch that transient null result as a
		// completed configuration: retry with a short backoff until the shaders
		// become available or the compiler reports a real source error.
		compiledShaderConfigInitialized = false;
		recompileFlag = true;
		shaderCompileRetryAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		if (!coreShaderCompileWarningLogged) {
			logger::warn("[HybridGI] Required compute shader set is incomplete (depth={}, radiance={}, normal={}, disocclusion={}, ssgi={}, upsample={}); retrying compilation.",
				prefilterDepthsCompute != nullptr, prefilterRadianceCompute != nullptr,
				prefilterNormalCompute != nullptr, radianceDisoccCompute != nullptr,
				giCompute != nullptr, upsampleCompute != nullptr);
			coreShaderCompileWarningLogged = true;
		}
		return;
	}
	coreShaderCompileWarningLogged = false;
	shaderCompileRetryAfter = {};
	compiledShaderConfigInitialized = true;
	compiledResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	compiledTemporalDenoiser = settings.EnableTemporalDenoiser;
	compiledGI = settings.EnableGI;
	compiledHybridReflections = settings.EnableExperimentalSpecularGI;
	compiledAdaptiveRayAllocation = settings.EnableAdaptiveRayAllocation;
    compiledCacheIntelligence = settings.EnableCacheIntelligence;
	recompileFlag = false;
}

bool HybridGI::CoreShadersOK() const
{
	const bool coreResources = ssgiCB && linearClampSampler && pointClampSampler && texNoise && texWorkingDepth &&
		texPrevGeo && texRadiance && texRadianceTemp && texNormal &&
		texAccumFrames[0] && texAccumFrames[1] && texAo[0] && texAo[1] &&
		texIlY[0] && texIlY[1] && texIlCoCg[0] && texIlCoCg[1] &&
		texGiSpecular[0] && texGiSpecular[1] && texBentVisibility[0] && texBentVisibility[1];

	return coreResources && prefilterDepthsCompute && prefilterRadianceCompute && prefilterNormalCompute &&
	       radianceDisoccCompute && giCompute;
}

bool HybridGI::WorldCacheShadersOK() const
{
	const bool resources = texWorldCacheMetadata && texWorldCacheWinners && texWorldCacheSH0 && texWorldCacheSH1 &&
		texWorldCacheSH2 && texWorldCacheNormal && texWorldCachePreviousMetadata && texWorldCachePreviousSH0 &&
		texWorldCachePreviousSH1 && texWorldCachePreviousSH2 && texWorldCachePreviousNormal && (!settings.EnableCacheIntelligence || (texWorldCacheIntelligence && texWorldCachePreviousIntelligence));
	return resources && worldCacheInjectCompute.get() != nullptr && worldCacheSelectCompute.get() != nullptr && worldCacheDecayCompute.get() != nullptr;
}

bool HybridGI::BlurShadersOK() const
{
	// Pass one is independently useful; a missing optional A-trous permutation
	// should degrade to one spatial pass rather than disable denoising entirely.
	return blurCompute.get() != nullptr;
}

bool HybridGI::ReflectionShadersOK() const
{
	return hybridReflectionCompute.get() != nullptr && reflectionDepthCompute.get() != nullptr;
}

bool HybridGI::EnsureReflectionDepth()
{
	if (texReflectionDepth)
		return true;
	if (reflectionDepthCreationAttempted || !texWorkingDepth)
		return false;
	reflectionDepthCreationAttempted = true;
	try {
		auto desc = texWorkingDepth->desc;
		// Store conservative nearest/farthest view depth per cell. This lets SSR
		// reject ray intervals both in front of and behind every surface in a tile.
		desc.Format = DXGI_FORMAT_R32G32_FLOAT;
		auto texture = eastl::make_unique<Texture2D>(desc, "HybridGI::ReflectionHiZ");
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 5;
		texture->CreateSRV(srvDesc);
		winrt::com_ptr<ID3D11UnorderedAccessView> views[5];
		for (uint i = 0; i < 5; ++i) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
			viewDesc.Format = desc.Format;
			viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			viewDesc.Texture2D.MipSlice = i;
			DX::ThrowIfFailed(globals::d3d::device->CreateUnorderedAccessView(texture->resource.get(), &viewDesc, views[i].put()));
		}
		for (uint i = 0; i < 5; ++i)
			uavReflectionDepth[i] = std::move(views[i]);
		texReflectionDepth = std::move(texture);
		return true;
	} catch (const std::exception& error) {
		logger::warn("[HybridGI] Reflection Hi-Z unavailable: {}; diffuse GI retained", error.what());
		return false;
	}
}

bool HybridGI::UpsampleShaderOK() const
{
	return upsampleCompute.get() != nullptr;
}

bool HybridGI::ShadersOK() const
{
	if (!CoreShadersOK())
		return false;

	// Reduced-resolution output cannot be consumed safely without its explicit
	// full-resolution reconstruction pass. Other advanced features degrade
	// independently instead of taking the complete GI module down.
	return std::clamp(settings.ResolutionMode, 0, 2) == 0 || UpsampleShaderOK();
}

void HybridGI::UpdateSB()
{
	if (!texRadiance || !ssgiCB || !globals::state)
		return;

	float2 res = { (float)texRadiance->desc.Width, (float)texRadiance->desc.Height };
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { std::max(floor(dynres.x), 1.0f), std::max(floor(dynres.y), 1.0f) };

	HybridGICB data{};
	{
		{
			// Use the exact camera matrices uploaded for this frame. shadowState can
			// lag or retain the world camera while Skyrim switches to first person,
			// which reprojected GI into unrelated surfaces and appeared as a broad
			// unowned light. Keeping projection and inverse view on one authority also
			// matches the depth/motion data consumed by Image Reconstruction.
			const auto& frameBuffer = globals::game::frameBufferCached;
			const auto& projection = frameBuffer.GetCameraProj();
			const auto temporalFrame = PIXL::Renderer::TemporalContext::Get().GetFrameSnapshot();
			float4x4 currentInverseView = frameBuffer.GetCameraViewInverse();
			float4x4 previousInverseView = currentInverseView;
			if (temporalFrame.current.frameIndex == globals::state->frameCount) {
				std::memcpy(&currentInverseView, temporalFrame.current.inverseView.data(), sizeof(temporalFrame.current.inverseView));
				if (temporalFrame.previousFrameValid)
					std::memcpy(&previousInverseView, temporalFrame.previous.inverseView.data(), sizeof(temporalFrame.previous.inverseView));
			}
			const float projectionX = std::abs(projection._11) > 1e-6f ? projection._11 : 1.0f;
			const float projectionY = std::abs(projection._22) > 1e-6f ? projection._22 : 1.0f;

			data.PrevInvViewMat = previousInverseView;
			data.NDCToViewMul = { 2.0f / projectionX, -2.0f / projectionY, 0.0f, 0.0f };
			data.NDCToViewAdd = { -1.0f / projectionX, 1.0f / projectionY, 0.0f, 0.0f };

		}

		data.TexDim = res;
		data.RcpTexDim = float2(1.0f) / res;
		data.FrameDim = dynres;
		data.RcpFrameDim = float2(1.0f) / dynres;
		// Keep the world-cache age and stochastic sequence stable while a diagnostic
		// set is captured. The set is long enough to cross the cache's 8-bit frame
		// stamp period, and injection is intentionally isolated from debug colours.
		data.FrameIndex = diagnosticCaptureActive ? diagnosticFrozenFrameIndex : globals::state->frameCount;

		// Cache lifetime is measured at a fixed eight ticks per second instead of
		// rendered frames. This keeps persistence consistent at 30/60/120 FPS and
		// prevents foreground shader compilation from stretching a 72-frame cache
		// into the reported 10-15 second fade/pop cycle. Clamp exceptional stalls so
		// one compilation hitch cannot expire the complete cache in a single frame.
		const float rawDelta = globals::game::deltaTime ? *globals::game::deltaTime : static_cast<float>(RE::GetSecondsSinceLastFrame());
		const float sanitizedDelta = PIXL::HybridGIInternal::SanitizeDeltaTime(rawDelta);
		data.WorldCacheDeltaTime = sanitizedDelta;
		const bool photoFinishSampling = globals::pipeline::pixelCapture.loaded &&
			globals::pipeline::pixelCapture.IsPhotoFinishSampling() &&
			globals::pipeline::pixelCapture.GetPhotoFinishStage() == PixelCapture::PhotoFinishStage::Accumulating;
		const bool cacheUpdatesAllowed = settings.EnableGI && settings.EnableWorldCache &&
			!diagnosticCaptureActive && !photoFinishSampling &&
			globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode == 0u;
		if (cacheUpdatesAllowed) {
			worldCacheClock = PIXL::HybridGIInternal::AdvanceFixedRateClock(
				worldCacheClock, worldCacheClockAccumulator, data.WorldCacheDeltaTime, 8.0f);
			// Keep cache ageing coarse and deterministic, but publish lighting at a
			// finer cadence. The accumulated interval is consumed only after the GPU
			// election and injection dispatches actually run.
			worldCacheInjectionAccumulator = std::min(worldCacheInjectionAccumulator + sanitizedDelta, 0.25f);
			worldCacheInjectionDue = lastWorldCacheInjectionClock == 0xffffffffu || worldCacheInjectionAccumulator >= (1.0f / 30.0f);
			if (worldCacheInjectionDue)
				data.WorldCacheDeltaTime = worldCacheInjectionAccumulator;
		} else {
			worldCacheInjectionAccumulator = 0.0f;
			worldCacheInjectionDue = false;
		}
		data.WorldCacheClock = worldCacheClock;

		const float workloadScale = PIXL::Renderer::GPUWorkloadBudgeter::Get().GetScale(
			PIXL::Renderer::WorkloadDomain::HybridGI);
		data.NumSlices = std::clamp(static_cast<uint>(std::lround(settings.NumSlices * workloadScale)), 2u, 8u);
		data.NumSteps = std::clamp(static_cast<uint>(std::lround(settings.NumSteps * workloadScale)), 4u, 24u);
		data.MinScreenRadius = std::clamp(settings.MinScreenRadius, 0.0f, 1.0f) * dynres.x;

		const float aoRadius = std::clamp(settings.AORadius, 0.0f, 1024.0f);
		const float giRadius = std::clamp(settings.GIRadius, 0.0f, 1024.0f);
		const float contactRadius = settings.EnableAO && settings.EnableContactDepth ? std::clamp(settings.ContactDepthRadius, 0.0f, 256.0f) : 0.0f;
		data.EffectRadius = std::max(std::max(aoRadius, giRadius), std::max(contactRadius, 1.0f));
		data.AORadius = aoRadius / data.EffectRadius;
		data.GIRadius = giRadius / data.EffectRadius;
		data.Thickness = std::clamp(settings.Thickness, 0.0f, 128.0f);
		data.DepthFadeRange.y = std::clamp(settings.DepthFadeRange.y, 1.01e4f, 5e4f);
		data.DepthFadeRange.x = std::clamp(settings.DepthFadeRange.x, 1e4f, data.DepthFadeRange.y - 100.f);
		data.DepthFadeScaleConst = 1.0f / std::max(data.DepthFadeRange.y - data.DepthFadeRange.x, 100.f);

		data.GISaturation = std::clamp(settings.GISaturation, 0.0f, 2.0f);
		data.GIDistanceCompensation = std::clamp(settings.GIDistanceCompensation, -5.0f, 5.0f);
		data.GICompensationMaxDist = aoRadius;

		data.AOPower = settings.EnableAO ? std::clamp(settings.AOPower, 0.0f, 4.0f) : 0.0f;
		data.GIStrength = std::clamp(settings.GIStrength, 0.0f, 4.0f);

		data.DepthDisocclusion = std::clamp(settings.DepthDisocclusion, 0.0f, 0.2f);
		data.NormalDisocclusion = std::clamp(settings.NormalDisocclusion, 0.0f, 1.0f);
		data.MaxAccumFrames = std::clamp(settings.MaxAccumFrames, 1u, 64u);
		data.BlurRadius = std::clamp(settings.BlurRadius, 0.0f, 30.0f);
		data.DistanceNormalisation = std::clamp(settings.DistanceNormalisation, 0.0f, 5.0f);

		const bool worldCacheAvailable = settings.EnableWorldCache && WorldCacheShadersOK();
		data.WorldCacheEnabled = worldCacheAvailable ? 1u : 0u;
		data.WorldCacheMaxAge = std::clamp(settings.WorldCacheMaxAge, 1u, 120u);
		data.WorldCacheSampleCount = std::clamp(settings.WorldCacheSampleCount, 1u, 8u);
		data.WorldCacheTraceSteps = std::clamp(settings.WorldCacheTraceSteps, 2u, 6u);
		data.WorldCacheStrength = std::clamp(settings.WorldCacheStrength,
			static_cast<float>(PIXL::Metadata::Settings::HybridWorldCacheStrength.minimum),
			static_cast<float>(PIXL::Metadata::Settings::HybridWorldCacheStrength.maximum));
		data.WorldCacheCellSizeNear = std::clamp(settings.WorldCacheCellSizeNear, 64.0f, 256.0f);
		data.WorldCacheCellSizeFar = std::max(std::clamp(settings.WorldCacheCellSizeFar, 256.0f, 1024.0f), data.WorldCacheCellSizeNear);
		data.WorldCacheRadius = std::clamp(settings.WorldCacheRadius, 256.0f, 4096.0f);
		data.WorldCacheLeakReduction = std::clamp(settings.WorldCacheLeakReduction, 0.0f, 1.0f);
		data.WorldCacheInjectionStride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
		data.WorldCacheDirectionalOcclusionEnabled = settings.EnableDirectionalOcclusion ? 1u : 0u;
		data.WorldCacheDirectionalOcclusionStrength = std::clamp(settings.DirectionalOcclusionStrength, 0.0f, 1.0f);
		data.DebugView = std::min(settings.DebugView, 19u);
		data.DebugGain = std::clamp(settings.DebugGain, 0.1f, 16.0f);
		data.WorldCacheTemporalResponse = std::clamp(settings.WorldCacheTemporalResponse, 0.02f, 1.0f);
		auto& radiantGrid = globals::pipeline::radiantGrid;
		const auto sharedLights = PIXL::Renderer::LightTransportWorld::Get().AcquireLocalLights();
		const bool sharedLightsReady = globals::state && sharedLights.ValidFor(globals::state->frameCount);
		const bool radiantGridFallbackReady = !settings.EnableCacheIntelligence && radiantGrid.loaded && radiantGrid.lights &&
			radiantGrid.lightIndexList && radiantGrid.lightGrid;
		const std::uint32_t emitterStart = sharedLightsReady ? sharedLights.emitterStart : radiantGrid.particleLightBufferStart;
		const std::uint32_t emitterCount = sharedLightsReady ? sharedLights.emitterCount : radiantGrid.particleLightBufferCount;
		const bool emitterInjectionReady = worldCacheAvailable && settings.EnableEmitterInjection && emitterCount > 0 &&
			(sharedLightsReady || radiantGridFallbackReady);
		data.WorldCacheEmitterInjectionEnabled = emitterInjectionReady ? 1u : 0u;
		data.RadiantParticleLightStart = emitterInjectionReady ? std::min(emitterStart, RadiantGrid::MAX_LIGHTS) : 0u;
		data.RadiantParticleLightCount = emitterInjectionReady ?
			std::min(emitterCount, RadiantGrid::MAX_LIGHTS - data.RadiantParticleLightStart) : 0u;
		data.WorldCacheEmitterInjectionStrength = std::clamp(settings.EmitterInjectionStrength, 0.0f, 1.5f);
		data.WorldCacheReflectionEnabled = worldCacheAvailable && settings.EnableVoxelReflections ? 1u : 0u;
		data.WorldCacheReflectionStrength = std::clamp(settings.VoxelReflectionStrength, 0.0f, 1.5f);
		data.WorldCacheReflectionRoughnessCutoff = std::clamp(settings.VoxelReflectionRoughnessCutoff, 0.2f, 1.0f);
		data.WorldCacheSecondBounceEnabled = worldCacheAvailable && settings.EnableWorldCacheSecondBounce ? 1u : 0u;
		data.WorldCacheSecondBounceStrength = std::clamp(settings.WorldCacheSecondBounceStrength, 0.0f, 0.5f);
		data.BentNormalEnabled = settings.EnableBentNormalLighting ? 1u : 0u;
		data.BentNormalStrength = std::clamp(settings.BentNormalStrength, 0.0f, 1.0f);
		data.SpecularOcclusionEnabled = settings.EnableSpecularOcclusion ? 1u : 0u;
		data.SpecularOcclusionStrength = std::clamp(settings.SpecularOcclusionStrength, 0.0f, 1.0f);
		data.RadianceFireflyClamp = std::max(settings.RadianceFireflyClamp, 0.0f);
		data.UpsampleEdgeThreshold = std::clamp(settings.UpsampleEdgeThreshold, 0.01f, 0.5f);
		data.ReflectionIntensity = std::clamp(settings.ReflectionIntensity, 0.0f, 2.0f);
		data.ReflectionMaxRoughness = std::clamp(settings.ReflectionMaxRoughness, 0.05f, 1.0f);
		data.ReflectionMaxDistance = std::clamp(settings.ReflectionMaxDistance, 256.0f, 8192.0f);
		data.ReflectionThickness = std::clamp(settings.ReflectionThickness, 2.0f, 96.0f);
		data.ReflectionSteps = std::clamp(settings.ReflectionSteps, 8u, 64u);
		data.ReflectionTemporalResponse = std::clamp(settings.ReflectionTemporalResponse, 0.04f, 0.5f);
		data.ReflectionFireflyClamp = std::clamp(settings.ReflectionFireflyClamp, 1.0f, 32.0f);
		data.ReflectionWorldFallbackStrength = std::clamp(settings.ReflectionWorldFallbackStrength, 0.0f, 1.0f);
		data.ReflectionRayBias = std::clamp(settings.ReflectionRayBias, 1.0f, 24.0f);
		data.ReflectionRoughnessJitter = std::clamp(settings.ReflectionRoughnessJitter, 0.0f, 1.0f);
		data.ContactDepthEnabled = settings.EnableAO && settings.EnableContactDepth ? 1u : 0u;
		data.ContactDepthStrength = std::clamp(settings.ContactDepthStrength, 0.0f, 0.75f);
		data.ContactDepthRadius = std::clamp(settings.ContactDepthRadius, 24.0f, 256.0f);
		data.ContactDepthBias = std::clamp(settings.ContactDepthBias, 0.0f, 0.35f);
		data.AdaptiveRayMinimum = std::clamp(settings.AdaptiveRayMinimum, 0.35f, 1.0f);
	}

	ssgiCB->Update(data);
}

void HybridGI::DrawHybridGI()
{
	// ReflectionContext is consumed later in this same deferred pass. Clear the
	// previous frame before any early return so missing D3D state or a failed GI
	// permutation cannot leave an old reflection SRV looking current.
	PIXL::Renderer::ReflectionContext::Get().Invalidate();
    if (!settings.EnableCacheIntelligence || !settings.EnableBudgetedCacheUpdates) {
        cacheScheduleCB.reset(); texWorldUpdateTime.reset(); texPreviousWorldUpdateTime.reset(); cacheBudget.Reset();
    }
	auto context = globals::d3d::context;
	if (!context)
		return;

	// Interior cells reuse local coordinates. Scope cached world irradiance to
	// the active interior so a door transition cannot expose the previous room's
	// voxel data. Exterior cells keep the shared toroidal cache while streaming.
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* currentCell = player ? player->GetParentCell() : nullptr;
	const bool currentSceneIsInterior = currentCell && currentCell->IsInteriorCell();
	const std::uint32_t currentInteriorCellFormId = currentSceneIsInterior ? currentCell->GetFormID() : 0u;
	if (currentCell) {
		if (interiorSceneContextInitialized &&
			(lastSceneWasInterior != currentSceneIsInterior ||
				(currentSceneIsInterior && lastInteriorCellFormId != currentInteriorCellFormId))) {
			queuedResetWorldHistory = true;
		}
		interiorSceneContextInitialized = true;
		lastSceneWasInterior = currentSceneIsInterior;
		lastInteriorCellFormId = currentInteriorCellFormId;
	}

	if (settings.EnableCacheIntelligence && (!texWorldCacheIntelligence || !texWorldCachePreviousIntelligence)) {
		settings.EnableCacheIntelligence = false;
		recompileFlag = true;
		logger::warn("[HybridGI] Cache intelligence unavailable; retaining legacy world cache.");
	}

	auto imageSpaceManager = globals::game::imageSpaceManager;
	if (imageSpaceManager) {
		auto* ssaoParams = imageSpaceManager->GetRuntimeData().BSImagespaceShaderISSAOBlurH;
		// Toggle vanilla SSAO only when Skyrim has materialized the image-space
		// shader object.  The pointer can be recreated across display transitions.
		if (ssaoParams)
			ssaoParams->enableSAO = settings.EnableVanillaSSAO;
	}
	UpdateDiagnosticCapture();
	const uint materialDebugMode = globals::pipeline::materialForge.settings.LegacyPhysicalDebugMode;
	if ((lastRuntimeDebugView != 0u && settings.DebugView == 0u) ||
		(lastRuntimeMaterialDebugMode != 0u && materialDebugMode == 0u))
		queuedResetTemporalHistory = true;
	lastRuntimeDebugView = settings.DebugView;
	lastRuntimeMaterialDebugMode = materialDebugMode;

	// History validity is a semantic contract, not just a loading-screen event.
	// Detect settings that change resolution/permutations or the world-cache
	// coordinate system so old data can never be interpreted under a new layout.
	if (!runtimeConfigInitialized) {
		runtimeConfigInitialized = true;
		lastEnableGI = settings.EnableGI;
		lastEnableAO = settings.EnableAO;
		lastEnableTemporalDenoiser = settings.EnableTemporalDenoiser;
		lastEnableExperimentalSpecularGI = settings.EnableExperimentalSpecularGI;
		lastEnableAdaptiveRayAllocation = settings.EnableAdaptiveRayAllocation;
		lastEnableWorldCache = settings.EnableWorldCache;
        lastEnableCacheIntelligence = settings.EnableCacheIntelligence;
		lastResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
		lastAOPower = settings.AOPower;
		lastAORadius = settings.AORadius;
		lastGIRadius = settings.GIRadius;
		lastGIStrength = settings.GIStrength;
		lastGISaturation = settings.GISaturation;
		lastThickness = settings.Thickness;
		lastDepthFadeNear = settings.DepthFadeRange.x;
		lastDepthFadeFar = settings.DepthFadeRange.y;
		lastContactDepthStrength = settings.ContactDepthStrength;
		lastContactDepthRadius = settings.ContactDepthRadius;
		lastContactDepthBias = settings.ContactDepthBias;
		lastNumSlices = settings.NumSlices;
		lastNumSteps = settings.NumSteps;
		lastEnableContactDepth = settings.EnableContactDepth;
		lastWorldCacheCellSizeNear = settings.WorldCacheCellSizeNear;
		lastWorldCacheCellSizeFar = settings.WorldCacheCellSizeFar;
	} else {
		const int currentResolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
		const bool temporalSemanticChanged =
			lastEnableGI != settings.EnableGI ||
			lastEnableAO != settings.EnableAO ||
			lastEnableTemporalDenoiser != settings.EnableTemporalDenoiser ||
			lastEnableExperimentalSpecularGI != settings.EnableExperimentalSpecularGI ||
			lastEnableAdaptiveRayAllocation != settings.EnableAdaptiveRayAllocation ||
			lastEnableWorldCache != settings.EnableWorldCache ||
            lastEnableCacheIntelligence != settings.EnableCacheIntelligence ||
			lastResolutionMode != currentResolutionMode;
		if (temporalSemanticChanged)
			queuedResetTemporalHistory = true;

		// The denoiser intentionally retains converged history at a low blend
		// rate. If a user changes AO/GI strength, reach, thickness, or sampling,
		// that history belongs to the old artistic target and can make the slider
		// appear ineffective for many frames. Reset once on a real setting change;
		// do not reset for tiny floating point noise from settings serialization.
		constexpr float kArtisticSettingEpsilon = 1.0e-3f;
		const auto changed = [](float a, float b) {
			return std::abs(a - b) > kArtisticSettingEpsilon;
		};
		const bool giArtistryChanged =
			changed(lastAOPower, settings.AOPower) ||
			changed(lastAORadius, settings.AORadius) ||
			changed(lastGIRadius, settings.GIRadius) ||
			changed(lastGIStrength, settings.GIStrength) ||
			changed(lastGISaturation, settings.GISaturation) ||
			changed(lastThickness, settings.Thickness) ||
			changed(lastDepthFadeNear, settings.DepthFadeRange.x) ||
			changed(lastDepthFadeFar, settings.DepthFadeRange.y) ||
			changed(lastContactDepthStrength, settings.ContactDepthStrength) ||
			changed(lastContactDepthRadius, settings.ContactDepthRadius) ||
			changed(lastContactDepthBias, settings.ContactDepthBias) ||
			lastNumSlices != settings.NumSlices ||
			lastNumSteps != settings.NumSteps ||
			lastEnableContactDepth != settings.EnableContactDepth;
		if (giArtistryChanged) {
			queuedResetTemporalHistory = true;
			logger::info("[HybridGI] SSGI/AO controls changed; invalidating accumulated lighting history.");
		}

		const bool cacheStructureChanged =
			lastWorldCacheCellSizeNear != settings.WorldCacheCellSizeNear ||
			lastWorldCacheCellSizeFar != settings.WorldCacheCellSizeFar;
		const bool cacheReenabled = !lastEnableWorldCache && settings.EnableWorldCache;
		if (cacheStructureChanged || cacheReenabled || lastEnableCacheIntelligence != settings.EnableCacheIntelligence)
			queuedResetWorldHistory = true;
		if (cacheStructureChanged)
			queuedResetTemporalHistory = true;

		lastEnableGI = settings.EnableGI;
		lastEnableAO = settings.EnableAO;
		lastEnableTemporalDenoiser = settings.EnableTemporalDenoiser;
		lastEnableExperimentalSpecularGI = settings.EnableExperimentalSpecularGI;
		lastEnableAdaptiveRayAllocation = settings.EnableAdaptiveRayAllocation;
		lastEnableWorldCache = settings.EnableWorldCache;
        lastEnableCacheIntelligence = settings.EnableCacheIntelligence;
		lastResolutionMode = currentResolutionMode;
		lastAOPower = settings.AOPower;
		lastAORadius = settings.AORadius;
		lastGIRadius = settings.GIRadius;
		lastGIStrength = settings.GIStrength;
		lastGISaturation = settings.GISaturation;
		lastThickness = settings.Thickness;
		lastDepthFadeNear = settings.DepthFadeRange.x;
		lastDepthFadeFar = settings.DepthFadeRange.y;
		lastContactDepthStrength = settings.ContactDepthStrength;
		lastContactDepthRadius = settings.ContactDepthRadius;
		lastContactDepthBias = settings.ContactDepthBias;
		lastNumSlices = settings.NumSlices;
		lastNumSteps = settings.NumSteps;
		lastEnableContactDepth = settings.EnableContactDepth;
		lastWorldCacheCellSizeNear = settings.WorldCacheCellSizeNear;
		lastWorldCacheCellSizeFar = settings.WorldCacheCellSizeFar;
	}

	// Rebuild requested permutations before capability validation. The previous
	// order validated stale shaders, then could continue the same frame after a
	// failed rebuild with a null program in a newly-required pass.
	if (settings.Enabled && recompileFlag && std::chrono::steady_clock::now() >= shaderCompileRetryAfter)
		RecompileChangedShaders();

	const bool ready = ShadersOK();
	static int lastReadiness = -1;
	const int readiness = (settings.Enabled ? 1 : 0) | (ready ? 2 : 0) | (settings.EnableGI ? 4 : 0);
	if (readiness != lastReadiness) {
		logger::info("[HybridGI] Runtime readiness: enabled={} ready={} diffuseGI={} core={} upsample={} resolutionMode={}",
			settings.Enabled, ready, settings.EnableGI, CoreShadersOK(), UpsampleShaderOK(), settings.ResolutionMode);
		lastReadiness = readiness;
	}
	if (!(settings.Enabled && ready)) {
		FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		if (texAo[outputAoIdx])
			context->ClearUnorderedAccessViewFloat(texAo[outputAoIdx]->uav.get(), clr);
		if (texIlY[outputIlIdx])
			context->ClearUnorderedAccessViewFloat(texIlY[outputIlIdx]->uav.get(), clr);
		if (texIlCoCg[outputIlIdx])
			context->ClearUnorderedAccessViewFloat(texIlCoCg[outputIlIdx]->uav.get(), clr);
		if (texGiSpecular[outputSpecIdx])
			context->ClearUnorderedAccessViewFloat(texGiSpecular[outputSpecIdx]->uav.get(), clr);
		if (texBentVisibility[outputBentIdx])
			context->ClearUnorderedAccessViewFloat(texBentVisibility[outputBentIdx]->uav.get(), clr);
		return;
	}

	if (!globals::state || !globals::state->sharedDataCB || !globals::game::renderer ||
		!globals::game::graphicsState || !globals::deferred || !globals::profiler)
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "HybridGI");

	static uint lastFrameAoTexIdx = 0;
	static uint lastFrameGITexIdx = 0;
	static uint lastFrameAccumTexIdx = 0;
	static uint lastFrameSpecTexIdx = 0;
	static uint lastFrameBentTexIdx = 0;
	uint inputAoTexIdx = lastFrameAoTexIdx;
	uint inputGITexIdx = lastFrameGITexIdx;
	uint inputSpecTexIdx = lastFrameSpecTexIdx;
	uint inputBentTexIdx = lastFrameBentTexIdx;

	auto clearTemporalHistory = [&]() {
		const FLOAT clr[4] = { 0.f, 0.f, 0.f, 0.f };
		const UINT clearUint[4] = { 0, 0, 0, 0 };
		for (auto& tex : texAccumFrames)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texAo)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texIlY)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texIlCoCg)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texGiSpecular)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		for (auto& tex : texBentVisibility)
			context->ClearUnorderedAccessViewFloat(tex->uav.get(), clr);
		context->ClearUnorderedAccessViewUint(texPrevGeo->uav.get(), clearUint);
	};

	auto clearWorldHistory = [&]() {
		const UINT clearUint[4] = { 0, 0, 0, 0 };
		const FLOAT clearFloat[4] = { 0.f, 0.f, 0.f, 0.f };
		for (auto* texture : { texWorldCacheMetadata.get(), texWorldCacheNormal.get(),
			texWorldCachePreviousMetadata.get(), texWorldCachePreviousNormal.get(), texWorldCacheIntelligence.get(), texWorldCachePreviousIntelligence.get() }) {
			if (texture && texture->uav)
				context->ClearUnorderedAccessViewUint(texture->uav.get(), clearUint);
		}
		for (auto* texture : { texWorldCacheSH0.get(), texWorldCacheSH1.get(), texWorldCacheSH2.get(),
			texWorldCachePreviousSH0.get(), texWorldCachePreviousSH1.get(), texWorldCachePreviousSH2.get() }) {
			if (texture && texture->uav)
				context->ClearUnorderedAccessViewFloat(texture->uav.get(), clearFloat);
		}
		lastWorldCacheDecayClock = 0xffffffffu;
		lastWorldCacheInjectionClock = 0xffffffffu;
		worldCacheInjectionAccumulator = 0.0f;
		worldCacheInjectionDue = true;
	};

	if (queuedResetHistory.exchange(false)) {
		clearTemporalHistory();
		clearWorldHistory();
	}
	if (queuedResetTemporalHistory.exchange(false))
		clearTemporalHistory();
	if (queuedResetWorldHistory.exchange(false))
		clearWorldHistory();

	//////////////////////////////////////////////////////

	UpdateSB();

	//////////////////////////////////////////////////////

	auto renderer = globals::game::renderer;
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto deferred = globals::deferred;

	float2 size = Util::ConvertToDynamic(float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight });
	auto resolution = std::array{
		(uint)std::max(std::isfinite(size.x) ? floor(size.x) : 1.0f, 1.0f),
		(uint)std::max(std::isfinite(size.y) ? floor(size.y) : 1.0f, 1.0f)
	};
	auto resChoices = std::array{
		resolution,
		std::array{ std::max(resolution[0] >> 1, 1u), std::max(resolution[1] >> 1, 1u) },
		std::array{ std::max(resolution[0] >> 2, 1u), std::max(resolution[1] >> 2, 1u) }
	};
	const int resolutionMode = std::clamp(settings.ResolutionMode, 0, 2);
	auto internalRes = resChoices[resolutionMode];
	// The world-space cache consumes the same irradiance field as diffuse GI;
	// when the user selects AO-only, keep the cache resident but stop its update
	// and decay workloads until GI is enabled again.
	const bool worldCacheActive = WorldCacheAvailableForSSGITest() && settings.EnableWorldCache && settings.EnableGI && WorldCacheShadersOK();
	const bool reflectionRequested = settings.EnableExperimentalSpecularGI;
	const bool reflectionShadersReady = ReflectionShadersOK();
	const bool reflectionDepthReady = !reflectionRequested ||
		(reflectionShadersReady && EnsureReflectionDepth());
	const bool reflectionActive = reflectionRequested && reflectionShadersReady && reflectionDepthReady;
	static int lastReflectionReadiness = -1;
	const int reflectionReadiness = (reflectionRequested ? 1 : 0) |
		(reflectionShadersReady ? 2 : 0) | (reflectionDepthReady ? 4 : 0) |
		(reflectionActive ? 8 : 0);
	if (reflectionReadiness != lastReflectionReadiness) {
		logger::info("[HybridGI] SSR readiness: requested={} shaders={} depthResources={} active={}",
			reflectionRequested, reflectionShadersReady, reflectionDepthReady, reflectionActive);
		lastReflectionReadiness = reflectionReadiness;
	}
	const bool diffuseBlurActive = settings.EnableBlur && BlurShadersOK();
	const bool reflectionSpatialActive = reflectionActive && settings.EnableBlur && hybridReflectionDenoiseCompute.get() != nullptr;

	std::array<ID3D11ShaderResourceView*, 22> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 8> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { pointClampSampler.get(), linearClampSampler.get() };
	auto cb = ssgiCB->CB();

	auto resetViews = [&]() {
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	};

	//////////////////////////////////////////////////////

	context->CSSetConstantBuffers(1, 1, &cb);
	auto* sharedDataBuf = globals::state->sharedDataCB->CB();
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// prefilter depths
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Depths");

		srvs.at(0) = globals::pipeline::materialLayers.GetEffectsDepth(Util::GetCurrentSceneDepthSRV());
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavWorkingDepth[i].get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(prefilterDepthsCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::PrefilterDepths");
		context->Dispatch((resolution[0] + 15) >> 4, (resolution[1] + 15) >> 4, 1);
		globals::profiler->EndPass();
	}

	// A true min pyramid is required for conservative hierarchical traversal.
	// Only build it when reflections run; do not change diffuse GI depth semantics.
	if (reflectionActive) {
		resetViews();
		srvs.at(0) = globals::pipeline::materialLayers.GetEffectsDepth(Util::GetCurrentSceneDepthSRV());
		for (int i = 0; i < 5; ++i)
			uavs.at(i) = uavReflectionDepth[i].get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(reflectionDepthCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::ReflectionHiZ");
		context->Dispatch((resolution[0] + 15u) >> 4, (resolution[1] + 15u) >> 4, 1);
		globals::profiler->EndPass();
	}

	// fetch radiance and disocclusion
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Radiance Disocc");

		resetViews();
		srvs.at(0) = REX::W32::AsReal(rts[deferred->forwardRenderTargets[0]].SRV);
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
		srvs.at(3) = texPrevGeo->srv.get();
		srvs.at(4) = REX::W32::AsReal(rts[RE::RENDER_TARGET::kMOTION_VECTOR].SRV);
		srvs.at(5) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(6) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(7) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(8) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(9) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(10) = texBentVisibility[inputBentTexIdx]->srv.get();
		srvs.at(11) = texRadiance->srv.get(); // previous frame; replaced after this dispatch

		uavs.at(0) = texRadianceTemp->uav.get();
		uavs.at(1) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
		uavs.at(2) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(3) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(4) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(5) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		uavs.at(6) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(radianceDisoccCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::RadianceDisocc");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		// Prefilter radiance texture instead of using GenerateMips for proper dynamic resolution handling.
		// radianceDisocc wrote mip 0 directly to texRadianceTemp above, so we can bind it
		// as SRV input here without an intermediate CopySubresourceRegion.
		const bool radiancePyramidNeeded = settings.EnableGI || worldCacheActive ||
			reflectionActive || settings.EnableAdaptiveRayAllocation;
		if (radiancePyramidNeeded) {
			TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Radiance");

			resetViews();
			srvs.at(0) = texRadianceTemp->srv.get();
			for (std::size_t mip = 0; mip < std::size(uavRadiance); ++mip)
				uavs.at(mip) = uavRadiance[mip].get();

			context->CSSetShaderResources(0, 1, srvs.data());
			context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
			context->CSSetShader(prefilterRadianceCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("HybridGI::PrefilterRadiance");
			context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
			globals::profiler->EndPass();
		}

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputSpecTexIdx = !inputSpecTexIdx;
		inputBentTexIdx = !inputBentTexIdx;
		lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
	}

	// Prefilter normals
	{
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Prefilter Normals");

		resetViews();
		srvs.at(0) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
		for (std::size_t mip = 0; mip < std::size(uavNormal); ++mip)
			uavs.at(mip) = uavNormal[mip].get();

		context->CSSetShaderResources(0, 1, srvs.data());
		context->CSSetUnorderedAccessViews(0, 5, uavs.data(), nullptr);
		context->CSSetShader(prefilterNormalCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::PrefilterNormals");
		context->Dispatch((internalRes[0] + 15u) >> 4, (internalRes[1] + 15u) >> 4, 1);
		globals::profiler->EndPass();
	}

	// GI
	{
		const bool cacheInjectionIsolated = diagnosticCaptureActive || materialDebugMode != 0u;
		if (worldCacheActive && !cacheInjectionIsolated) {
            if (settings.EnableCacheIntelligence && worldCacheMarkCompute && emitterChangesCB) {
                const auto changes = PIXL::Renderer::LightTransportWorld::Get().FinalizeEmitterChanges(globals::state->frameCount);
                if (changes.published && changes.frame == globals::state->frameCount && (changes.count || changes.overflow)) {
                    resetViews();
                    EmitterChangesCB data{};
                    data.count = changes.count;
                    data.overflow = changes.overflow ? 1u : 0u;
                    data.spheres = changes.spheres;
                    emitterChangesCB->Update(data);
                    auto* changeBuffer = emitterChangesCB->CB();
                    winrt::com_ptr<ID3D11Buffer> previousChangeBuffer;
                    context->CSGetConstantBuffers(2, 1, previousChangeBuffer.put());
                    context->CSSetConstantBuffers(2, 1, &changeBuffer);
                    srvs.at(0) = texWorldCacheMetadata->srv.get();
                    uavs.at(0) = texWorldCacheIntelligence->uav.get();
                    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
                    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
                    context->CSSetShader(worldCacheMarkCompute.get(), nullptr, 0);
                    globals::profiler->BeginPass("HybridGI::WorldCacheLightChanges");
                    context->Dispatch(128, 8, 1);
                    PIXL::Renderer::LightTransportWorld::Get().AcknowledgeEmitterChanges(changes.epoch, changes.sequence);
                    globals::profiler->EndPass();
                    resetViews();
                    changeBuffer = previousChangeBuffer.get();
                    context->CSSetConstantBuffers(2, 1, &changeBuffer);
                }
            }
			if (worldCacheInjectionDue) {
                CacheScheduleCB schedule{};
                schedule.milliseconds = PIXL::HybridGIInternal::CacheMilliseconds(worldCacheClock, worldCacheClockAccumulator);
                if (settings.EnableCacheIntelligence && settings.EnableBudgetedCacheUpdates) {
                    try {
                        if (!cacheScheduleCB) {
                            auto makeTimeAtlas = [&]() {
                                auto texture = eastl::make_unique<Texture2D>(texWorldCacheMetadata->desc, "HybridGI::Cache Update Time");
                                D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
                                srv.Format = DXGI_FORMAT_R32_UINT; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
                                D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
                                uav.Format = DXGI_FORMAT_R32_UINT; uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                                texture->CreateSRV(srv); texture->CreateUAV(uav);
                                const uint initial[4]{schedule.milliseconds, 0, 0, 0};
                                context->ClearUnorderedAccessViewUint(texture->uav.get(), initial);
                                return texture;
                            };
                            texWorldUpdateTime = makeTimeAtlas(); texPreviousWorldUpdateTime = makeTimeAtlas();
                            cacheScheduleCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CacheScheduleCB>(), "HybridGI::Cache Schedule");
                        }
                        float cost = 0.0f; uint validTimers = 0;
                        for (const auto& timer : globals::profiler->GetResults()) {
                            if ((timer.name == "HybridGI::WorldCacheSelect" || timer.name == "HybridGI::WorldCacheInject") && timer.valid) {
                                cost += timer.avgMs; ++validTimers;
                            }
                        }
                        schedule.fraction = cacheBudget.Update(cost, validTimers == 2);
                        schedule.enabled = 1;
                    } catch (const std::exception& error) {
                        logger::warn("HybridGI cache scheduling unavailable: {}", error.what());
                        settings.EnableBudgetedCacheUpdates = false;
                    }
                }
                if (!schedule.enabled) {
                    cacheScheduleCB.reset(); texWorldUpdateTime.reset(); texPreviousWorldUpdateTime.reset(); cacheBudget.Reset();
                }
                if (cacheScheduleCB) cacheScheduleCB->Update(schedule);
                auto* scheduleBuffer = cacheScheduleCB ? cacheScheduleCB->CB() : nullptr;
                winrt::com_ptr<ID3D11Buffer> previousScheduleBuffer;
                if (settings.EnableCacheIntelligence) {
                    context->CSGetConstantBuffers(4, 1, previousScheduleBuffer.put());
                    context->CSSetConstantBuffers(4, 1, &scheduleBuffer);
                }
				TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - PIXL World Cache Inject");
				resetViews();
				// Snapshot at the bounded radiance-update cadence. The shader reads this
				// immutable copy while updating live UAVs, preserving race-free second bounce.
                globals::profiler->BeginPass("HybridGI::WorldCacheSnapshot");
				context->CopyResource(texWorldCachePreviousMetadata->resource.get(), texWorldCacheMetadata->resource.get());
				context->CopyResource(texWorldCachePreviousSH0->resource.get(), texWorldCacheSH0->resource.get());
				context->CopyResource(texWorldCachePreviousSH1->resource.get(), texWorldCacheSH1->resource.get());
				context->CopyResource(texWorldCachePreviousSH2->resource.get(), texWorldCacheSH2->resource.get());
				context->CopyResource(texWorldCachePreviousNormal->resource.get(), texWorldCacheNormal->resource.get());
                if (settings.EnableCacheIntelligence && texWorldCacheIntelligence && texWorldCachePreviousIntelligence)
                    context->CopyResource(texWorldCachePreviousIntelligence->resource.get(), texWorldCacheIntelligence->resource.get());
                globals::profiler->EndPass();
                srvs.at(12) = texWorldCachePreviousIntelligence ? texWorldCachePreviousIntelligence->srv.get() : nullptr;
                if (schedule.enabled) context->CopyResource(texPreviousWorldUpdateTime->resource.get(), texWorldUpdateTime->resource.get());
                srvs.at(14) = schedule.enabled ? texPreviousWorldUpdateTime->srv.get() : nullptr;
                uavs.at(7) = schedule.enabled ? texWorldUpdateTime->uav.get() : nullptr;
                const auto annotations = PIXL::Renderer::PixelAnnotations::Get().Acquire();
                const bool annotationsReady = settings.EnableCacheIntelligence && globals::state &&
                    annotations.opaqueValid && annotations.frame == globals::state->frameCount &&
                    annotations.width == static_cast<std::uint32_t>(size.x) &&
                    annotations.height == static_cast<std::uint32_t>(size.y);
                srvs.at(13) = annotationsReady ? annotations.deferredMaterialMask.get() : nullptr;
                uavs.at(6) = texWorldCacheIntelligence ? texWorldCacheIntelligence->uav.get() : nullptr;
				srvs.at(0) = texWorkingDepth->srv.get();
				srvs.at(1) = texNormal->srv.get();
				srvs.at(2) = texRadiance->srv.get();
				srvs.at(3) = REX::W32::AsReal(rts[ALBEDO].SRV);
				srvs.at(4) = texWorldCachePreviousMetadata->srv.get();
				srvs.at(5) = texWorldCachePreviousSH0->srv.get();
				srvs.at(6) = texWorldCachePreviousSH1->srv.get();
				srvs.at(7) = texWorldCachePreviousSH2->srv.get();
				srvs.at(8) = texWorldCachePreviousNormal->srv.get();
				auto& radiantGrid = globals::pipeline::radiantGrid;
				const auto sharedLights = PIXL::Renderer::LightTransportWorld::Get().AcquireLocalLights();
				const bool sharedLightsReady = globals::state && sharedLights.ValidFor(globals::state->frameCount);
				srvs.at(9) = !settings.EnableEmitterInjection ? nullptr : sharedLightsReady ?
					sharedLights.lights.get() : radiantGrid.loaded && radiantGrid.lights ? radiantGrid.lights->srv.get() : nullptr;
				srvs.at(10) = !settings.EnableEmitterInjection ? nullptr : sharedLightsReady ?
					sharedLights.lightIndices.get() : radiantGrid.loaded && radiantGrid.lightIndexList ? radiantGrid.lightIndexList->srv.get() : nullptr;
				srvs.at(11) = !settings.EnableEmitterInjection ? nullptr : sharedLightsReady ?
					sharedLights.lightGrid.get() : radiantGrid.loaded && radiantGrid.lightGrid ? radiantGrid.lightGrid->srv.get() : nullptr;
				uavs.at(0) = texWorldCacheMetadata->uav.get();
				uavs.at(1) = texWorldCacheSH0->uav.get();
				uavs.at(2) = texWorldCacheSH1->uav.get();
				uavs.at(3) = texWorldCacheSH2->uav.get();
				uavs.at(4) = texWorldCacheNormal->uav.get();
				uavs.at(5) = texWorldCacheWinners->uav.get();
				const uint emptyWinners[4] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
				context->ClearUnorderedAccessViewUint(texWorldCacheWinners->uav.get(), emptyWinners);
				context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
				context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
				const uint stride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
				const uint injectWidth = (internalRes[0] + stride - 1u) / stride;
				const uint injectHeight = (internalRes[1] + stride - 1u) / stride;
				context->CSSetShader(worldCacheSelectCompute.get(), nullptr, 0);
				globals::profiler->BeginPass("HybridGI::WorldCacheSelect");
				context->Dispatch((injectWidth + 7u) >> 3, (injectHeight + 7u) >> 3, 1);
				globals::profiler->EndPass();
				// Separate dispatches establish visibility of the completed election.
				// Exactly one representative may publish each multi-texture SH record.
				ID3D11UnorderedAccessView* nullWinner = nullptr;
				context->CSSetUnorderedAccessViews(5, 1, &nullWinner, nullptr);
				context->CSSetUnorderedAccessViews(5, 1, &uavs.at(5), nullptr);
				context->CSSetShader(worldCacheInjectCompute.get(), nullptr, 0);
				ID3D11ShaderResourceView* localShadowViews[2]{};
				if (settings.EnableEmitterInjection && radiantGrid.IsLocalShadowCacheActive()) {
					localShadowViews[0] = radiantGrid.localShadowBuffer ? radiantGrid.localShadowBuffer->srv.get() : nullptr;
					localShadowViews[1] = radiantGrid.localShadowCache ? radiantGrid.localShadowCache->srv.get() : nullptr;
				}
				context->CSSetShaderResources(112, ARRAYSIZE(localShadowViews), localShadowViews);
				globals::profiler->BeginPass("HybridGI::WorldCacheInject");
				context->Dispatch((injectWidth + 7u) >> 3, (injectHeight + 7u) >> 3, 1);
				globals::profiler->EndPass();
				ID3D11ShaderResourceView* nullLocalShadowViews[2]{};
				context->CSSetShaderResources(112, ARRAYSIZE(nullLocalShadowViews), nullLocalShadowViews);
                if (settings.EnableCacheIntelligence) {
                    scheduleBuffer = previousScheduleBuffer.get();
                    context->CSSetConstantBuffers(4, 1, &scheduleBuffer);
                }
				                if (settings.EnableCacheIntelligence && worldCacheEvidenceCompute && worldCacheEvidenceResolveCompute) {
                    resetViews();
                    const UINT emptyEvidence[4]{};
                    context->ClearUnorderedAccessViewUint(texWorldCacheWinners->uav.get(), emptyEvidence);
                    srvs.at(0) = Util::GetCurrentSceneDepthSRV(false);
                    uavs.at(0) = texWorldCacheWinners->uav.get();
                    context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
                    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
                    context->CSSetShader(worldCacheEvidenceCompute.get(), nullptr, 0);
                    globals::profiler->BeginPass("HybridGI::WorldCacheEvidence");
                    context->Dispatch((injectWidth + 7u) >> 3, (injectHeight + 7u) >> 3, 1);
                    resetViews();
                    uavs.at(0) = texWorldCacheWinners->uav.get();
                    uavs.at(1) = texWorldCacheMetadata->uav.get();
                    uavs.at(2) = texWorldCacheIntelligence->uav.get();
                    context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
                    context->CSSetShader(worldCacheEvidenceResolveCompute.get(), nullptr, 0);
                    context->Dispatch(128, 8, 1);
                    globals::profiler->EndPass();
                    resetViews();
                }
                lastWorldCacheInjectionClock = worldCacheClock;
				worldCacheInjectionAccumulator = 0.0f;
				worldCacheInjectionDue = false;
			}

			// Invalidate entries after their fixed-time lifetime before the GI and
			// reflection readers run. The cache clock advances at a fixed rate, so
			// sweeping the same atlas on every render frame is redundant at 120 FPS
			// and above. A sweep is still forced after a history reset.
			if (worldCacheClock != lastWorldCacheDecayClock) {
				resetViews();
				uavs.at(0) = texWorldCacheMetadata->uav.get();
				uavs.at(1) = texWorldCacheIntelligence ? texWorldCacheIntelligence->uav.get() : nullptr;
                context->CSSetUnorderedAccessViews(0, 2, uavs.data(), nullptr);
                context->CSSetShader(worldCacheDecayCompute.get(), nullptr, 0);
				globals::profiler->BeginPass("HybridGI::WorldCacheDecay");
				context->Dispatch(((32u * 32u) + 7u) >> 3, ((32u * 2u) + 7u) >> 3, 1);
				globals::profiler->EndPass();
				lastWorldCacheDecayClock = worldCacheClock;
			}
		}

		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - GI");

		// Drain only owned per-pass snapshots. At most 16,384 triangles and 1,024
		// shapes are admitted. A missing capture uploads an empty field (neutral GI).
		std::vector<PIXL::HybridGIInternal::SceneTriangle> sceneTriangles;
		std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> sceneMaterialTextures;
		std::uintptr_t sceneCellKey{}, sceneWorldKey{};
		{
			std::scoped_lock lock(sceneTriangleMutex);
			const auto* scenePlayer = RE::PlayerCharacter::GetSingleton();
			const auto currentCellKey = reinterpret_cast<std::uintptr_t>(scenePlayer ? scenePlayer->GetParentCell() : nullptr);
			const auto currentWorldKey = reinterpret_cast<std::uintptr_t>(scenePlayer ? scenePlayer->GetWorldspace() : nullptr);
			sceneCellKey = currentCellKey;
			sceneWorldKey = currentWorldKey;
			if (pendingSceneCellKey != currentCellKey || pendingSceneWorldKey != currentWorldKey) {
				// Drop one-frame-old snapshots on any cell or worldspace transition;
				// the following render-root capture will repopulate from the new scene.
				pendingSceneTriangles.clear();
				pendingSceneMaterialTextures.clear();
				pendingSceneMaterialTextureIndices.clear();
				pendingSceneGeometryTokens.clear();
				pendingSceneCellKey = currentCellKey;
				pendingSceneWorldKey = currentWorldKey;
			}
			sceneTriangles.swap(pendingSceneTriangles);
			sceneMaterialTextures.swap(pendingSceneMaterialTextures);
			pendingSceneMaterialTextureIndices.clear();
			pendingSceneGeometryTokens.clear();
		}
		std::vector<std::uintptr_t> sceneTextureIdentities;
		sceneTextureIdentities.reserve(sceneMaterialTextures.size());
		for (const auto& texture : sceneMaterialTextures)
			sceneTextureIdentities.push_back(reinterpret_cast<std::uintptr_t>(texture.get()));
		const auto eye = Util::GetEyePosition();
		const std::array<float, 3> cameraWorld{ eye.x, eye.y, eye.z };
		SceneVoxelCB voxelData{};
		SceneVoxelizeCB voxelizeData{};
		const auto dimension = sceneVoxelDimension;
		constexpr float voxelSize = PIXL::HybridGIInternal::SceneVoxelGrid::kVoxelSize;
		const float halfExtent = dimension * voxelSize * 0.5f;
		bool validVoxelOrigin = dimension > 0 &&
			std::all_of(cameraWorld.begin(), cameraWorld.end(), [](float value) { return std::isfinite(value); });
		for (std::size_t axis = 0; axis < 3; ++axis) {
			const float snapped = validVoxelOrigin ? std::round(cameraWorld[axis] / voxelSize) * voxelSize : 0.0f;
			voxelData.worldMin[axis] = snapped - halfExtent;
			voxelizeData.worldMin[axis] = voxelData.worldMin[axis];
		}
		voxelData.voxelSize = voxelSize;
		voxelData.dimension = dimension;
		voxelData.radianceStride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
		// SceneVoxelCB's final word is reserved in the shared ABI. For voxel
		// lighting it carries the active RadiantGrid light count, allowing the
		// world-space voxel pass to avoid camera-frustum cluster culling.
		voxelData.padding = globals::pipeline::radiantGrid.lightCount;
		voxelizeData.voxelSize = voxelSize;
		voxelizeData.dimension = dimension;
		const auto triangleCount = static_cast<std::uint32_t>(std::min<std::size_t>(sceneTriangles.size(), PIXL::HybridGIInternal::kMaxGpuSceneTriangles));
		voxelizeData.triangleCount = validVoxelOrigin ? triangleCount : 0u;
		voxelData.valid = validVoxelOrigin && triangleCount > 0u;
		bool voxelFieldDirty = false;
		if (texSceneVoxels && sceneVoxelUAV && texSceneVoxelSurface && sceneVoxelSurfaceUAV &&
			texSceneVoxelMaterial && sceneVoxelMaterialUAV &&
			sceneVoxelizeCB && sceneVoxelizeCompute && sceneTriangleBuffer) {
			const bool sameOrigin = voxelizedSceneWorldMin == std::array<float, 3>{ voxelData.worldMin[0], voxelData.worldMin[1], voxelData.worldMin[2] };
			const bool sameTriangles = voxelizedSceneTriangles.size() == sceneTriangles.size() &&
				(sceneTriangles.empty() || std::memcmp(voxelizedSceneTriangles.data(), sceneTriangles.data(),
					sceneTriangles.size() * sizeof(PIXL::HybridGIInternal::SceneTriangle)) == 0);
			const bool sameMaterialTextures = voxelizedSceneMaterialTextureIdentities == sceneTextureIdentities;
			const bool sameContext = voxelizedSceneCellKey == sceneCellKey && voxelizedSceneWorldKey == sceneWorldKey;
			voxelFieldDirty = !sceneVoxelContentsInitialized || !sameOrigin || !sameTriangles || !sameMaterialTextures ||
				voxelizedSceneOriginValid != validVoxelOrigin ||
				!sameContext;
			if (voxelFieldDirty) {
				auto scroll = PIXL::HybridGIInternal::ComputeSceneVoxelScroll(
					voxelizedSceneWorldMin,
					{ voxelData.worldMin[0], voxelData.worldMin[1], voxelData.worldMin[2] },
					voxelSize, dimension);
				const bool hasScroll = scroll.valid && (scroll.sourceOffset[0] != 0 ||
					scroll.sourceOffset[1] != 0 || scroll.sourceOffset[2] != 0);
				const bool scrollExistingField = sceneVoxelContentsInitialized && !sameOrigin && sameTriangles &&
					sameContext && validVoxelOrigin && voxelizedSceneOriginValid && hasScroll &&
					texSceneVoxelsScratch && sceneVoxelScratchUAV && texSceneVoxelSurface && texSceneVoxelSurfaceScratch &&
					sceneVoxelSurfaceUAV && sceneVoxelSurfaceScratchUAV && texSceneVoxelMaterial && texSceneVoxelMaterialScratch &&
					sceneVoxelMaterialUAV && sceneVoxelMaterialScratchUAV && sceneVoxelScrollCB && sceneVoxelScrollCompute;

				if (scrollExistingField) {
					SceneVoxelScrollCB scrollData{};
					std::copy(scroll.sourceOffset.begin(), scroll.sourceOffset.end(), scrollData.sourceOffset);
					scrollData.dimension = dimension;
					sceneVoxelScrollCB->Update(scrollData);
					ID3D11ShaderResourceView* sourceVoxels[3] = { texSceneVoxels->srv.get(), texSceneVoxelSurface->srv.get(), texSceneVoxelMaterial->srv.get() };
					ID3D11UnorderedAccessView* destinationVoxels[3] = { sceneVoxelScratchUAV.get(), sceneVoxelSurfaceScratchUAV.get(), sceneVoxelMaterialScratchUAV.get() };
					auto* scrollCb = sceneVoxelScrollCB->CB();
					context->CSSetShaderResources(0, ARRAYSIZE(sourceVoxels), sourceVoxels);
					context->CSSetUnorderedAccessViews(0, ARRAYSIZE(destinationVoxels), destinationVoxels, nullptr);
					context->CSSetConstantBuffers(4, 1, &scrollCb);
					context->CSSetShader(sceneVoxelScrollCompute.get(), nullptr, 0);
					context->Dispatch((dimension + 3u) / 4u, (dimension + 3u) / 4u, (dimension + 3u) / 4u);
					ID3D11ShaderResourceView* nullScrollSrvs[3]{};
					ID3D11UnorderedAccessView* nullScrollUavs[3]{};
					ID3D11Buffer* nullScrollCb = nullptr;
					context->CSSetShaderResources(0, ARRAYSIZE(nullScrollSrvs), nullScrollSrvs);
					context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullScrollUavs), nullScrollUavs, nullptr);
					context->CSSetConstantBuffers(4, 1, &nullScrollCb);
					texSceneVoxels.swap(texSceneVoxelsScratch);
					texSceneVoxelSurface.swap(texSceneVoxelSurfaceScratch);
					texSceneVoxelMaterial.swap(texSceneVoxelMaterialScratch);
					sceneVoxelUAV = texSceneVoxels->uav;
					sceneVoxelScratchUAV = texSceneVoxelsScratch->uav;
					sceneVoxelSurfaceUAV = texSceneVoxelSurface->uav;
					sceneVoxelSurfaceScratchUAV = texSceneVoxelSurfaceScratch->uav;
					sceneVoxelMaterialUAV = texSceneVoxelMaterial->uav;
					sceneVoxelMaterialScratchUAV = texSceneVoxelMaterialScratch->uav;
					voxelizeData.updateMode = 1u;
					std::copy(scroll.sourceOffset.begin(), scroll.sourceOffset.end(), voxelizeData.scrollOffset);
				} else {
					// Context changes, changed geometry or a large/non-lattice move take
					// the conservative full rebuild path.
					const UINT clearValue[4] = {};
					context->ClearUnorderedAccessViewUint(sceneVoxelUAV.get(), clearValue);
					const UINT emptySurface[4] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
					context->ClearUnorderedAccessViewUint(sceneVoxelSurfaceUAV.get(), emptySurface);
					context->ClearUnorderedAccessViewUint(sceneVoxelMaterialUAV.get(), clearValue);
					voxelizeData.updateMode = 0u;
				}

				bool uploadSucceeded = true;
				if (voxelizeData.triangleCount > 0u) {
					std::vector<PIXL::HybridGIInternal::GpuSceneTriangle> gpuTriangles;
					gpuTriangles.reserve(voxelizeData.triangleCount);
					for (std::uint32_t i = 0; i < voxelizeData.triangleCount; ++i) {
						const auto& source = sceneTriangles[i];
						gpuTriangles.push_back({ source.a, source.b, source.c, dimension * dimension, source.packedMaterial,
							source.uvA, source.uvB, source.uvC, source.textureIndex });
					}
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(context->Map(sceneTriangleBuffer->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
						std::memcpy(mapped.pData, gpuTriangles.data(), std::size_t(voxelizeData.triangleCount) * sizeof(PIXL::HybridGIInternal::GpuSceneTriangle));
						context->Unmap(sceneTriangleBuffer->resource.get(), 0);
						sceneVoxelizeCB->Update(voxelizeData);
						ID3D11ShaderResourceView* triangleSrv = sceneTriangleBuffer->srv.get();
						std::array<ID3D11ShaderResourceView*, PIXL::HybridGIInternal::kSceneMaterialTextureSlots> materialSrvs{};
						for (std::size_t textureIndex = 0; textureIndex < sceneMaterialTextures.size() && textureIndex < materialSrvs.size(); ++textureIndex)
							materialSrvs[textureIndex] = sceneMaterialTextures[textureIndex].get();
						ID3D11UnorderedAccessView* voxelUavs[3] = { sceneVoxelUAV.get(), sceneVoxelSurfaceUAV.get(), sceneVoxelMaterialUAV.get() };
						auto* voxelizeCb = sceneVoxelizeCB->CB();
						context->CSSetShaderResources(0, 1, &triangleSrv);
						context->CSSetShaderResources(1, static_cast<UINT>(materialSrvs.size()), materialSrvs.data());
						ID3D11SamplerState* materialSampler = linearClampSampler.get();
						context->CSSetSamplers(0, 1, &materialSampler);
						context->CSSetUnorderedAccessViews(0, ARRAYSIZE(voxelUavs), voxelUavs, nullptr);
						context->CSSetConstantBuffers(4, 1, &voxelizeCb);
						context->CSSetShader(sceneVoxelizeCompute.get(), nullptr, 0);
						context->Dispatch((voxelizeData.triangleCount + 63u) / 64u, 1, 1);
						std::array<ID3D11ShaderResourceView*, PIXL::HybridGIInternal::kSceneMaterialTextureSlots + 1> nullTriangleSrvs{};
						ID3D11UnorderedAccessView* nullVoxelUavs[3]{};
						ID3D11Buffer* nullVoxelizeCb = nullptr;
						context->CSSetShaderResources(0, static_cast<UINT>(nullTriangleSrvs.size()), nullTriangleSrvs.data());
						ID3D11SamplerState* nullMaterialSampler = nullptr;
						context->CSSetSamplers(0, 1, &nullMaterialSampler);
						context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullVoxelUavs), nullVoxelUavs, nullptr);
						context->CSSetConstantBuffers(4, 1, &nullVoxelizeCb);
					} else {
						uploadSucceeded = false;
					}
				}
				if (uploadSucceeded) {
					voxelizedSceneTriangles = sceneTriangles;
					voxelizedSceneMaterialTextureIdentities = sceneTextureIdentities;
					voxelizedSceneWorldMin = { voxelData.worldMin[0], voxelData.worldMin[1], voxelData.worldMin[2] };
					voxelizedSceneCellKey = sceneCellKey;
					voxelizedSceneWorldKey = sceneWorldKey;
					voxelizedSceneOriginValid = validVoxelOrigin;
					sceneVoxelContentsInitialized = true;
				} else {
					sceneVoxelContentsInitialized = false;
					voxelData.valid = false;
				}
			}
			sceneVoxelCB->Update(voxelData);
		}
		// Lighting and screen-radiance transfer each sweep the complete 256^3
		// volume. Refresh immediately after geometry/origin changes, then amortize
		// steady-state refreshes over eight frames instead of dispatching three
		// full-volume workloads on every rendered frame.
		constexpr std::uint32_t kSceneVoxelRadianceRefreshFrames = 8;
		const auto voxelRefreshFrame = globals::state ? globals::state->frameCount : 0u;
		const bool sceneVoxelRefreshDue = voxelFieldDirty ||
			(voxelRefreshFrame % kSceneVoxelRadianceRefreshFrames == 0u);
		const bool voxelRadianceReady = sceneVoxelCB && texSceneVoxelWinners && sceneVoxelWinnerUAV && texSceneVoxelRadiance &&
			sceneVoxelRadianceUAV && sceneVoxelRadianceSelectCompute && sceneVoxelRadianceResolveCompute;
		const bool voxelLightingReady = sceneVoxelCB && texSceneVoxels && texSceneVoxelSurface && texSceneVoxelMaterial &&
			texSceneVoxelRadiance && sceneVoxelRadianceUAV && sceneVoxelLightingCompute;
		if (voxelLightingReady && sceneVoxelRefreshDue) {
			resetViews();
			auto& voxelRadiantGrid = globals::pipeline::radiantGrid;
			ID3D11ShaderResourceView* voxelSrvs[3] = {
				texSceneVoxels->srv.get(), texSceneVoxelSurface->srv.get(), texSceneVoxelMaterial->srv.get()
			};
			ID3D11ShaderResourceView* lightSrvs[3] = {
				voxelRadiantGrid.loaded && voxelRadiantGrid.lights ? voxelRadiantGrid.lights->srv.get() : nullptr,
				voxelRadiantGrid.loaded && voxelRadiantGrid.lightIndexList ? voxelRadiantGrid.lightIndexList->srv.get() : nullptr,
				voxelRadiantGrid.loaded && voxelRadiantGrid.lightGrid ? voxelRadiantGrid.lightGrid->srv.get() : nullptr
			};
			ID3D11ShaderResourceView* localShadowSrvs[2]{};
			if (voxelRadiantGrid.IsLocalShadowCacheActive()) {
				localShadowSrvs[0] = voxelRadiantGrid.localShadowBuffer ? voxelRadiantGrid.localShadowBuffer->srv.get() : nullptr;
				localShadowSrvs[1] = voxelRadiantGrid.localShadowCache ? voxelRadiantGrid.localShadowCache->srv.get() : nullptr;
			}
			ID3D11UnorderedAccessView* radianceUav = sceneVoxelRadianceUAV.get();
			ID3D11Buffer* strictLightsCb = voxelRadiantGrid.strictLightDataCB ? voxelRadiantGrid.strictLightDataCB->CB() : nullptr;
			ID3D11Buffer* sceneVoxelBuffer = sceneVoxelCB->CB();
			ID3D11SamplerState* voxelLightingSampler = linearClampSampler.get();
			context->CSSetShaderResources(0, ARRAYSIZE(voxelSrvs), voxelSrvs);
			context->CSSetShaderResources(35, ARRAYSIZE(lightSrvs), lightSrvs);
			context->CSSetShaderResources(112, ARRAYSIZE(localShadowSrvs), localShadowSrvs);
			context->CSSetUnorderedAccessViews(0, 1, &radianceUav, nullptr);
			context->CSSetConstantBuffers(3, 1, &strictLightsCb);
			context->CSSetConstantBuffers(4, 1, &sceneVoxelBuffer);
			context->CSSetSamplers(0, 1, &voxelLightingSampler);
			context->CSSetShader(sceneVoxelLightingCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("HybridGI::SceneVoxelLighting");
			context->Dispatch((dimension + 3u) / 4u, (dimension + 3u) / 4u, (dimension + 3u) / 4u);
			globals::profiler->EndPass();
			ID3D11ShaderResourceView* nullVoxelSrvs[3]{};
			ID3D11ShaderResourceView* nullLocalShadowSrvs[2]{};
			ID3D11UnorderedAccessView* nullRadianceUav = nullptr;
			ID3D11Buffer* nullVoxelCb = nullptr;
			ID3D11SamplerState* nullVoxelSampler = nullptr;
			context->CSSetShaderResources(0, ARRAYSIZE(nullVoxelSrvs), nullVoxelSrvs);
			context->CSSetShaderResources(35, ARRAYSIZE(lightSrvs), nullVoxelSrvs);
			context->CSSetShaderResources(112, ARRAYSIZE(nullLocalShadowSrvs), nullLocalShadowSrvs);
			context->CSSetUnorderedAccessViews(0, 1, &nullRadianceUav, nullptr);
			context->CSSetConstantBuffers(3, 1, &nullVoxelCb);
			context->CSSetConstantBuffers(4, 1, &nullVoxelCb);
			context->CSSetSamplers(0, 1, &nullVoxelSampler);
		}
		if (voxelRadianceReady && sceneVoxelRefreshDue) {
			resetViews();
			const UINT emptyWinner[4] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
			context->ClearUnorderedAccessViewUint(sceneVoxelWinnerUAV.get(), emptyWinner);

			ID3D11ShaderResourceView* radianceSelectSrvs[1] = { texWorkingDepth ? texWorkingDepth->srv.get() : nullptr };
			ID3D11UnorderedAccessView* radianceSelectUavs[1] = { sceneVoxelWinnerUAV.get() };
			ID3D11Buffer* sceneVoxelBuffer = sceneVoxelCB->CB();
			context->CSSetShaderResources(0, ARRAYSIZE(radianceSelectSrvs), radianceSelectSrvs);
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(radianceSelectUavs), radianceSelectUavs, nullptr);
			context->CSSetConstantBuffers(1, 1, &cb);
			context->CSSetConstantBuffers(3, 1, &sceneVoxelBuffer);
			context->CSSetShader(sceneVoxelRadianceSelectCompute.get(), nullptr, 0);
			const uint radianceStride = std::clamp(settings.WorldCacheInjectionStride, 1u, 8u);
			globals::profiler->BeginPass("HybridGI::SceneVoxelRadianceSelect");
			context->Dispatch((internalRes[0] + radianceStride * 8u - 1u) / (radianceStride * 8u),
				(internalRes[1] + radianceStride * 8u - 1u) / (radianceStride * 8u), 1);
			globals::profiler->EndPass();
			ID3D11ShaderResourceView* nullRadianceSelectSrv = nullptr;
			ID3D11UnorderedAccessView* nullRadianceSelectUav = nullptr;
			context->CSSetShaderResources(0, 1, &nullRadianceSelectSrv);
			context->CSSetUnorderedAccessViews(0, 1, &nullRadianceSelectUav, nullptr);

			ID3D11ShaderResourceView* radianceResolveSrvs[3] = {
				texSceneVoxelWinners->srv.get(), texWorkingDepth ? texWorkingDepth->srv.get() : nullptr,
				texRadiance ? texRadiance->srv.get() : nullptr
			};
			ID3D11UnorderedAccessView* radianceResolveUav = sceneVoxelRadianceUAV.get();
			context->CSSetShaderResources(0, ARRAYSIZE(radianceResolveSrvs), radianceResolveSrvs);
			context->CSSetUnorderedAccessViews(0, 1, &radianceResolveUav, nullptr);
			context->CSSetShader(sceneVoxelRadianceResolveCompute.get(), nullptr, 0);
			globals::profiler->BeginPass("HybridGI::SceneVoxelRadianceResolve");
			context->Dispatch((dimension + 3u) / 4u, (dimension + 3u) / 4u, (dimension + 3u) / 4u);
			globals::profiler->EndPass();
			ID3D11ShaderResourceView* nullRadianceResolveSrvs[3]{};
			ID3D11UnorderedAccessView* nullRadianceResolveUav = nullptr;
			context->CSSetShaderResources(0, ARRAYSIZE(nullRadianceResolveSrvs), nullRadianceResolveSrvs);
			context->CSSetUnorderedAccessViews(0, 1, &nullRadianceResolveUav, nullptr);
			ID3D11Buffer* nullSceneVoxelBuffer = nullptr;
			context->CSSetConstantBuffers(3, 1, &nullSceneVoxelBuffer);
		} else if (!voxelRadianceReady && sceneVoxelRadianceUAV) {
			// Missing shaders/resources must leave a neutral voxel radiance field,
			// never stale data from a previous device or scene generation.
			const UINT emptyRadiance[4]{};
			context->ClearUnorderedAccessViewUint(sceneVoxelRadianceUAV.get(), emptyRadiance);
		}

		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
		srvs.at(2) = texRadiance->srv.get();
		srvs.at(3) = texNoise->srv.get();
		srvs.at(4) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
		srvs.at(5) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(6) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(7) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(8) = texNormal->srv.get();
		srvs.at(9) = worldCacheActive ? texWorldCacheMetadata->srv.get() : nullptr;
		srvs.at(10) = worldCacheActive ? texWorldCacheSH0->srv.get() : nullptr;
		srvs.at(11) = worldCacheActive ? texWorldCacheSH1->srv.get() : nullptr;
		srvs.at(12) = worldCacheActive ? texWorldCacheSH2->srv.get() : nullptr;
		srvs.at(13) = worldCacheActive ? texWorldCacheNormal->srv.get() : nullptr;
        srvs.at(17) = worldCacheActive && texWorldCacheIntelligence ? texWorldCacheIntelligence->srv.get() : nullptr;
			srvs.at(14) = REX::W32::AsReal(rts[REFLECTANCE].SRV);
			srvs.at(15) = texBentVisibility[inputBentTexIdx]->srv.get();
			srvs.at(16) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(18) = texSceneVoxels ? texSceneVoxels->srv.get() : nullptr;
		srvs.at(19) = texSceneVoxelRadiance ? texSceneVoxelRadiance->srv.get() : nullptr;
		srvs.at(20) = texSceneVoxelSurface ? texSceneVoxelSurface->srv.get() : nullptr;
		srvs.at(21) = texSceneVoxelMaterial ? texSceneVoxelMaterial->srv.get() : nullptr;

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(3) = nullptr;  // Hybrid Reflections owns the specular output.
		uavs.at(4) = texPrevGeo->uav.get();
		uavs.at(5) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		auto* sceneVoxelBuffer = sceneVoxelCB ? sceneVoxelCB->CB() : nullptr;
		context->CSSetConstantBuffers(3, 1, &sceneVoxelBuffer);
		context->CSSetShader(giCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::GI");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();
		ID3D11Buffer* nullSceneVoxelBuffer = nullptr;
		context->CSSetConstantBuffers(3, 1, &nullSceneVoxelBuffer);

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputBentTexIdx = !inputBentTexIdx;
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAoTexIdx = inputAoTexIdx;
		lastFrameBentTexIdx = inputBentTexIdx;
	}

	// PIXL Hybrid Reflections: stochastic GGX screen-space trace with temporal
	// history already geometry-remapped by radianceDisocc.
	if (reflectionActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Hybrid Reflections");
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
		srvs.at(2) = texRadiance->srv.get();
		srvs.at(3) = texNoise->srv.get();
		srvs.at(4) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(5) = REX::W32::AsReal(rts[REFLECTANCE].SRV);
		srvs.at(6) = worldCacheActive ? texWorldCacheMetadata->srv.get() : nullptr;
		srvs.at(7) = worldCacheActive ? texWorldCacheSH0->srv.get() : nullptr;
		srvs.at(8) = worldCacheActive ? texWorldCacheSH1->srv.get() : nullptr;
		srvs.at(9) = worldCacheActive ? texWorldCacheSH2->srv.get() : nullptr;
		srvs.at(10) = worldCacheActive ? texWorldCacheNormal->srv.get() : nullptr;
        srvs.at(13) = worldCacheActive && texWorldCacheIntelligence ? texWorldCacheIntelligence->srv.get() : nullptr;
		srvs.at(11) = texReflectionDepth->srv.get();
		srvs.at(12) = settings.EnableTemporalDenoiser ?
			texAccumFrames[lastFrameAccumTexIdx]->srv.get() : nullptr;
		srvs.at(14) = texSceneVoxels ? texSceneVoxels->srv.get() : nullptr;
		uavs.at(0) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		auto* sceneVoxelBuffer = sceneVoxelCB ? sceneVoxelCB->CB() : nullptr;
		context->CSSetConstantBuffers(3, 1, &sceneVoxelBuffer);
		context->CSSetShader(hybridReflectionCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::HybridReflections");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();
		ID3D11Buffer* nullSceneVoxelBuffer = nullptr;
		context->CSSetConstantBuffers(3, 1, &nullSceneVoxelBuffer);
		inputSpecTexIdx = !inputSpecTexIdx;
		lastFrameSpecTexIdx = inputSpecTexIdx;
	} else {
		lastFrameSpecTexIdx = inputSpecTexIdx;
	}

	// Bilateral spatial reconstruction for the stochastic reflection signal.
	// Reuses the existing specular ping-pong textures; no extra full-resolution
	// allocation is required. The user-facing Spatial Denoiser toggle controls
	// both diffuse GI and hybrid reflection reconstruction.
	if (reflectionSpatialActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Hybrid Reflection Denoise");
		resetViews();
		srvs.at(0) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(1) = texWorkingDepth->srv.get();
		srvs.at(2) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
		uavs.at(0) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, 1, uavs.data(), nullptr);
		context->CSSetShader(hybridReflectionDenoiseCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::HybridReflectionDenoise");
		context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();
		inputSpecTexIdx = !inputSpecTexIdx;
		lastFrameSpecTexIdx = inputSpecTexIdx;
	}

	// blur
	if (diffuseBlurActive) {
		TracyD3D11Zone(globals::state->tracyCtx, "HybridGI - Diffuse Blur");
		const uint passCount = settings.EnableAdaptiveDenoiser && blurAtrousCompute.get() != nullptr ? 2u : 1u;
		for (uint passIndex = 0; passIndex < passCount; ++passIndex) {
			resetViews();
			srvs.at(0) = texWorkingDepth->srv.get();
			srvs.at(1) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);
			srvs.at(2) = texAccumFrames[lastFrameAccumTexIdx]->srv.get();
			srvs.at(3) = texIlY[inputGITexIdx]->srv.get();
			srvs.at(4) = texIlCoCg[inputGITexIdx]->srv.get();
			srvs.at(5) = texAo[inputAoTexIdx]->srv.get();
			srvs.at(6) = texBentVisibility[inputBentTexIdx]->srv.get();

			uavs.at(0) = texAccumFrames[!lastFrameAccumTexIdx]->uav.get();
			uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
			uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
			uavs.at(3) = texAo[!inputAoTexIdx]->uav.get();
			uavs.at(4) = texBentVisibility[!inputBentTexIdx]->uav.get();

			context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
			context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
			context->CSSetShader(passIndex == 0 ? blurCompute.get() : blurAtrousCompute.get(), nullptr, 0);
			globals::profiler->BeginPass(passIndex == 0 ? "HybridGI::Denoise" : "HybridGI::DenoiseAtrous");
			context->Dispatch((internalRes[0] + 7u) >> 3, (internalRes[1] + 7u) >> 3, 1);
			globals::profiler->EndPass();

			inputGITexIdx = !inputGITexIdx;
			inputAoTexIdx = !inputAoTexIdx;
			inputBentTexIdx = !inputBentTexIdx;
			lastFrameAccumTexIdx = !lastFrameAccumTexIdx;
		}
		lastFrameGITexIdx = inputGITexIdx;
		lastFrameAoTexIdx = inputAoTexIdx;
		lastFrameBentTexIdx = inputBentTexIdx;
	}

	// upsample
	if (settings.ResolutionMode != 0) {
		resetViews();
		srvs.at(0) = texWorkingDepth->srv.get();
		srvs.at(1) = texAo[inputAoTexIdx]->srv.get();
		srvs.at(2) = texIlY[inputGITexIdx]->srv.get();
		srvs.at(3) = texIlCoCg[inputGITexIdx]->srv.get();
		srvs.at(4) = texGiSpecular[inputSpecTexIdx]->srv.get();
		srvs.at(5) = texBentVisibility[inputBentTexIdx]->srv.get();
		srvs.at(6) = texNormal->srv.get();
		srvs.at(7) = REX::W32::AsReal(rts[NORMALROUGHNESS].SRV);

		uavs.at(0) = texAo[!inputAoTexIdx]->uav.get();
		uavs.at(1) = texIlY[!inputGITexIdx]->uav.get();
		uavs.at(2) = texIlCoCg[!inputGITexIdx]->uav.get();
		uavs.at(3) = texGiSpecular[!inputSpecTexIdx]->uav.get();
		uavs.at(4) = texBentVisibility[!inputBentTexIdx]->uav.get();

		context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
		context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
		context->CSSetShader(upsampleCompute.get(), nullptr, 0);
		globals::profiler->BeginPass("HybridGI::Upsample");
		context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);
		globals::profiler->EndPass();

		inputAoTexIdx = !inputAoTexIdx;
		inputGITexIdx = !inputGITexIdx;
		inputSpecTexIdx = !inputSpecTexIdx;
		inputBentTexIdx = !inputBentTexIdx;
	}

	outputAoIdx = inputAoTexIdx;
	outputIlIdx = inputGITexIdx;
	outputSpecIdx = inputSpecTexIdx;
	outputBentIdx = inputBentTexIdx;
	PIXL::Renderer::ReflectionFrame reflectionFrame{};
	if (reflectionActive && texGiSpecular[outputSpecIdx] && texGiSpecular[outputSpecIdx]->srv) {
		reflectionFrame.radianceConfidence = texGiSpecular[outputSpecIdx]->srv;
		reflectionFrame.width = resolution[0]; reflectionFrame.height = resolution[1];
		reflectionFrame.traceSteps = settings.ReflectionSteps;
		reflectionFrame.maxDistance = settings.ReflectionMaxDistance;
		reflectionFrame.maxRoughness = settings.ReflectionMaxRoughness;
		reflectionFrame.thickness = settings.ReflectionThickness;
		reflectionFrame.screenTrace = true;
		reflectionFrame.worldFallback = settings.EnableVoxelReflections && worldCacheActive;
		reflectionFrame.temporal = settings.EnableTemporalDenoiser;
		reflectionFrame.spatial = reflectionSpatialActive;
		reflectionFrame.valid = true;
	}
	PIXL::Renderer::ReflectionContext::Get().Publish(std::move(reflectionFrame));

	if (worldCacheActive && texWorldCacheSH0 && texWorldCacheSH0->srv) {
		PIXL::Renderer::LightTransportWorld::Get().PublishProbe(
			PIXL::Renderer::ProbeKind::WorldIrradiance, texWorldCacheSH0->srv.get(),
			texWorldCacheSH0->desc.Width, texWorldCacheSH0->desc.Height);
	}

	// cleanup
	resetViews();

	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
	PIXL::Renderer::TemporalContext::Get().SetHistoryValid(temporalHistoryId);
	if (worldCacheActive)
		PIXL::Renderer::TemporalContext::Get().SetHistoryValid(worldHistoryId);
}

#undef I18N_KEY_PREFIX
