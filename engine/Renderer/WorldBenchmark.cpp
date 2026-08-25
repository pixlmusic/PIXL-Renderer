#include "Renderer/WorldBenchmark.h"

#include <RE/F/FreeCameraState.h>
#include <RE/H/HUDMenu.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <numbers>

#include "Globals.h"
#include "Menu.h"
#include "Modules/ImageReconstruction.h"
#include "Modules/PixelCapture.h"
#include "Modules/PulseProfiler.h"
#include "Profiler.h"
#include "State.h"
#include "Utils/FileSystem.h"

namespace
{
	struct Vec3
	{
		float x;
		float y;
		float z;
	};

	struct CameraKeyframe
	{
		float time;
		Vec3 offset;
		float yawDegrees;
		float pitchDegrees;
		float fovDegrees;
	};

	struct CameraPose
	{
		Vec3 offset{};
		float yawDegrees = 0.0f;
		float pitchDegrees = 0.0f;
		float fovDegrees = 75.0f;
	};

	enum class CameraRig
	{
		RainCamp,
		Harbour,
		CityReveal,
		Wilderness,
		SnowField,
		CharacterArc,
		InteriorDolly,
		Dungeon,
		Cavern,
		Otherworld,
		Ashland
	};

	struct Scene
	{
		const char* label;
		const char* coc;
		const char* fallbackCoc;
		float hour;
		const char* weather;
		const char* condition;
		const char* features;
		CameraRig rig;
	};

	// Feature route: wet mud and precipitation -> water/shore -> city materials and
	// windows -> vegetation -> snow -> characters -> fire lighting -> emissive GI ->
	// frozen-valley atmosphere. The full route retains the broader 20-scene sweep.
	constexpr std::array<Scene, 20> kScenes{
		Scene{ "Whiterun stables rain camp", "WhiterunStables", "Whiterun", 16.5f, "10a234", "rain_mud_camp", "Ground Response mud, wetness, rain, roof runoff, skin and vegetation", CameraRig::RainCamp },
		Scene{ "Solitude docks", "SolitudeDocks01", "SolitudeOrigin", 17.0f, "12f89", "solitude_harbour", "Waterbody, Water Optics, shore wetness, reflections, vegetation and atmosphere", CameraRig::Harbour },
		Scene{ "Solitude window district", "SolitudeOrigin", "Solitude", 20.0f, "10a234", "solitude_windows", "WindowLife, parallax, specular lighting, wet materials and contact shadows", CameraRig::CityReveal },
		Scene{ "Riverwood morning", "Riverwood", "Whiterun", 7.0f, "12f89", "forest_morning", "Foliage Dynamics, SkyBounce, terrain detail, water and atmosphere", CameraRig::Wilderness },
		Scene{ "Markarth noon", "MarkarthOrigin", "Markarth", 12.0f, "81a", "markarth_materials", "PBR materials, parallax depth, hard-surface specular and terrain seams", CameraRig::CityReveal },
		Scene{ "Riften overcast", "RiftenOrigin", "Riften", 15.0f, "12f89", "riften_overcast", "Wet timber, water, vegetation, ambient probes and distance blending", CameraRig::Harbour },
		Scene{ "Windhelm snow", "WindhelmOrigin", "Windhelm", 10.0f, "10e1f1", "windhelm_snow", "Snow deformation, snow precipitation, skin optics, terrain and volumetric light", CameraRig::SnowField },
		Scene{ "Falkreath fog", "Falkreath", "Riverwood", 8.0f, "10a241", "falkreath_fog", "Atmosphere, Sky Veil, foliage transmission and distance blending", CameraRig::Wilderness },
		Scene{ "Morthal dusk", "MorthalExterior01", "Morthal", 18.5f, "10a241", "morthal_dusk", "Fog, shallow water, reeds, indirect light and sky continuity", CameraRig::Wilderness },
		Scene{ "Dawnstar night", "DawnstarExterior01", "Dawnstar", 0.0f, "10e1f1", "dawnstar_snow_night", "Snow, night lighting, emissive response and volumetric occlusion", CameraRig::SnowField },
		Scene{ "Winterhold dawn", "WinterholdExterior01", "Winterhold", 6.0f, "10e1f1", "winterhold_dawn", "Snow deformation, sky continuity, horizon blend and atmosphere", CameraRig::SnowField },
		Scene{ "Dragonsreach character study", "WhiterunDragonsreach", "Whiterun", 14.0f, "81a", "dragonsreach_skin", "Skin Optics, Tissue Diffusion, strand shading, eyes and interior daylight", CameraRig::CharacterArc },
		Scene{ "Blue Palace interior", "SolitudeBluePalace", "SolitudeOrigin", 21.0f, "81a", "blue_palace", "Interior daylight, material layers, light volumes and specular response", CameraRig::InteriorDolly },
		Scene{ "Ratway interior", "RiftenRatway01", "RiftenOrigin", 2.0f, "81a", "ratway_dark", "Low-light GI, contact shadows, wet materials and local lights", CameraRig::Dungeon },
		Scene{ "Embershard firelight", "EmbershardMine01", "Riverwood", 12.0f, "81a", "embershard_fire", "Fire interaction, emissive bounce, volumetrics, skin and contact shadows", CameraRig::Dungeon },
		Scene{ "Bleak Falls Barrow", "BleakFallsBarrow01", "Riverwood", 12.0f, "81a", "barrow_cold", "Cold interior lighting, parallax, terrain detail and volumetric occlusion", CameraRig::Dungeon },
		Scene{ "Blackreach emissive cavern", "BlackreachCity", "Whiterun", 12.0f, "81a", "blackreach_emissive", "Hybrid GI, radiant grid, world probes, emissives and volumetrics", CameraRig::Cavern },
		Scene{ "Sovngarde", "Sovngarde01", "Whiterun", 17.0f, "81a", "sovngarde", "Sky continuity, atmosphere, volumetric light, terrain and vegetation", CameraRig::Otherworld },
		Scene{ "Forgotten Vale", "DLC1FalmerValleyStart", "WinterholdExterior01", 11.0f, "10e1f1", "forgotten_vale", "Snow deformation, frozen water, vegetation, atmosphere and distance blend", CameraRig::SnowField },
		Scene{ "Solstheim ash", "DLC2RavenRock01", "Whiterun", 16.0f, "101c32", "solstheim_ash", "Ash atmosphere, terrain materials, skin, water and distance blending", CameraRig::Ashland }
	};

	constexpr std::array<std::size_t, 9> kFeatureTourScenes{ 0, 1, 2, 3, 6, 11, 14, 16, 18 };
	constexpr std::array<float, 3> kCaptureMarkers{ 0.20f, 0.55f, 0.88f };
	constexpr std::array<const char*, 36> kFeatureCoverage{
		"AmbientProbe", "Atmosphere", "CameraSuite", "ContactShadows",
		"DistanceBlend", "FoliageDynamics", "GroundResponse", "HorizonBlend",
		"HybridGI", "ImageReconstruction", "InteriorDaylight", "LightVolumes",
		"LinearLightCore", "MaterialForge", "MaterialLayers", "NaturalLighting",
		"PixelCapture", "PulseProfiler", "RadiantGrid", "RainResponse",
		"SkinOptics", "SkyBounce", "SkyContinuity", "SkyVeil",
		"StrandShading", "TerrainDetail", "TerrainField", "TerrainOcclusion",
		"TerrainSeam", "ThinSurface", "TissueDiffusion", "VolumeOcclusion",
		"WaterOptics", "Waterbody", "WindowLife", "WorldProbes"
	};

	constexpr std::array<CameraKeyframe, 5> kRainCampRig{
		CameraKeyframe{ 0.00f, { -180.0f, -260.0f, 105.0f }, -18.0f, -7.0f, 68.0f },
		CameraKeyframe{ 0.24f, { -80.0f, -90.0f, 68.0f }, -7.0f, -11.0f, 60.0f },
		CameraKeyframe{ 0.50f, { 55.0f, 120.0f, 82.0f }, 8.0f, -6.0f, 64.0f },
		CameraKeyframe{ 0.76f, { 220.0f, 270.0f, 145.0f }, 24.0f, -4.0f, 72.0f },
		CameraKeyframe{ 1.00f, { 390.0f, 470.0f, 225.0f }, 34.0f, -10.0f, 78.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kHarbourRig{
		CameraKeyframe{ 0.00f, { -330.0f, -380.0f, 95.0f }, -24.0f, -5.0f, 74.0f },
		CameraKeyframe{ 0.23f, { -170.0f, -120.0f, 54.0f }, -10.0f, -9.0f, 62.0f },
		CameraKeyframe{ 0.50f, { 20.0f, 150.0f, 72.0f }, 6.0f, -5.0f, 66.0f },
		CameraKeyframe{ 0.75f, { 260.0f, 330.0f, 155.0f }, 21.0f, -8.0f, 72.0f },
		CameraKeyframe{ 1.00f, { 520.0f, 520.0f, 290.0f }, 34.0f, -13.0f, 80.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kCityRevealRig{
		CameraKeyframe{ 0.00f, { -130.0f, -220.0f, 70.0f }, -16.0f, -4.0f, 65.0f },
		CameraKeyframe{ 0.25f, { -40.0f, -55.0f, 86.0f }, -6.0f, -9.0f, 57.0f },
		CameraKeyframe{ 0.52f, { 115.0f, 145.0f, 145.0f }, 9.0f, -12.0f, 62.0f },
		CameraKeyframe{ 0.77f, { 300.0f, 315.0f, 250.0f }, 23.0f, -16.0f, 70.0f },
		CameraKeyframe{ 1.00f, { 520.0f, 480.0f, 390.0f }, 34.0f, -19.0f, 78.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kWildernessRig{
		CameraKeyframe{ 0.00f, { -220.0f, -300.0f, 85.0f }, -18.0f, -5.0f, 72.0f },
		CameraKeyframe{ 0.26f, { -85.0f, -75.0f, 48.0f }, -6.0f, -8.0f, 62.0f },
		CameraKeyframe{ 0.50f, { 95.0f, 170.0f, 66.0f }, 8.0f, -4.0f, 66.0f },
		CameraKeyframe{ 0.74f, { 320.0f, 410.0f, 155.0f }, 21.0f, -8.0f, 74.0f },
		CameraKeyframe{ 1.00f, { 610.0f, 690.0f, 300.0f }, 32.0f, -13.0f, 80.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kSnowFieldRig{
		CameraKeyframe{ 0.00f, { -210.0f, -270.0f, 58.0f }, -17.0f, -12.0f, 67.0f },
		CameraKeyframe{ 0.25f, { -70.0f, -60.0f, 32.0f }, -6.0f, -16.0f, 56.0f },
		CameraKeyframe{ 0.50f, { 110.0f, 180.0f, 52.0f }, 7.0f, -10.0f, 61.0f },
		CameraKeyframe{ 0.76f, { 330.0f, 430.0f, 150.0f }, 20.0f, -9.0f, 70.0f },
		CameraKeyframe{ 1.00f, { 590.0f, 720.0f, 330.0f }, 31.0f, -15.0f, 79.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kCharacterArcRig{
		CameraKeyframe{ 0.00f, { -95.0f, -145.0f, 72.0f }, -20.0f, -3.0f, 55.0f },
		CameraKeyframe{ 0.25f, { -42.0f, -62.0f, 68.0f }, -10.0f, -4.0f, 48.0f },
		CameraKeyframe{ 0.50f, { 20.0f, -34.0f, 70.0f }, 4.0f, -3.0f, 44.0f },
		CameraKeyframe{ 0.75f, { 78.0f, -70.0f, 76.0f }, 18.0f, -5.0f, 49.0f },
		CameraKeyframe{ 1.00f, { 135.0f, -145.0f, 92.0f }, 30.0f, -8.0f, 58.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kInteriorDollyRig{
		CameraKeyframe{ 0.00f, { -115.0f, -190.0f, 80.0f }, -12.0f, -2.0f, 64.0f },
		CameraKeyframe{ 0.25f, { -52.0f, -82.0f, 76.0f }, -5.0f, -3.0f, 56.0f },
		CameraKeyframe{ 0.50f, { 20.0f, 25.0f, 82.0f }, 3.0f, -5.0f, 52.0f },
		CameraKeyframe{ 0.75f, { 96.0f, 135.0f, 96.0f }, 10.0f, -7.0f, 58.0f },
		CameraKeyframe{ 1.00f, { 180.0f, 245.0f, 118.0f }, 18.0f, -9.0f, 65.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kDungeonRig{
		CameraKeyframe{ 0.00f, { -95.0f, -165.0f, 64.0f }, -13.0f, -2.0f, 62.0f },
		CameraKeyframe{ 0.25f, { -38.0f, -66.0f, 58.0f }, -5.0f, -5.0f, 54.0f },
		CameraKeyframe{ 0.50f, { 24.0f, 38.0f, 64.0f }, 3.0f, -4.0f, 50.0f },
		CameraKeyframe{ 0.75f, { 88.0f, 142.0f, 78.0f }, 11.0f, -8.0f, 56.0f },
		CameraKeyframe{ 1.00f, { 160.0f, 238.0f, 94.0f }, 18.0f, -10.0f, 62.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kCavernRig{
		CameraKeyframe{ 0.00f, { -260.0f, -360.0f, 115.0f }, -19.0f, -3.0f, 76.0f },
		CameraKeyframe{ 0.25f, { -100.0f, -95.0f, 78.0f }, -7.0f, -8.0f, 64.0f },
		CameraKeyframe{ 0.50f, { 110.0f, 170.0f, 120.0f }, 8.0f, -10.0f, 68.0f },
		CameraKeyframe{ 0.75f, { 390.0f, 460.0f, 260.0f }, 23.0f, -13.0f, 76.0f },
		CameraKeyframe{ 1.00f, { 720.0f, 760.0f, 470.0f }, 35.0f, -18.0f, 82.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kOtherworldRig{
		CameraKeyframe{ 0.00f, { -280.0f, -380.0f, 125.0f }, -22.0f, -2.0f, 78.0f },
		CameraKeyframe{ 0.25f, { -110.0f, -90.0f, 92.0f }, -8.0f, -7.0f, 66.0f },
		CameraKeyframe{ 0.50f, { 100.0f, 210.0f, 145.0f }, 7.0f, -9.0f, 70.0f },
		CameraKeyframe{ 0.75f, { 390.0f, 520.0f, 300.0f }, 24.0f, -14.0f, 78.0f },
		CameraKeyframe{ 1.00f, { 760.0f, 880.0f, 540.0f }, 37.0f, -19.0f, 84.0f }
	};

	constexpr std::array<CameraKeyframe, 5> kAshlandRig{
		CameraKeyframe{ 0.00f, { -240.0f, -340.0f, 90.0f }, -18.0f, -4.0f, 74.0f },
		CameraKeyframe{ 0.25f, { -90.0f, -80.0f, 55.0f }, -7.0f, -8.0f, 63.0f },
		CameraKeyframe{ 0.50f, { 105.0f, 180.0f, 82.0f }, 7.0f, -5.0f, 67.0f },
		CameraKeyframe{ 0.75f, { 350.0f, 450.0f, 175.0f }, 21.0f, -10.0f, 74.0f },
		CameraKeyframe{ 1.00f, { 650.0f, 760.0f, 340.0f }, 33.0f, -15.0f, 81.0f }
	};

	struct RigView
	{
		const CameraKeyframe* data;
		std::size_t size;
	};

	template <std::size_t N>
	constexpr RigView MakeRigView(const std::array<CameraKeyframe, N>& rig)
	{
		return { rig.data(), rig.size() };
	}

	RigView GetRig(CameraRig rig)
	{
		switch (rig) {
		case CameraRig::RainCamp: return MakeRigView(kRainCampRig);
		case CameraRig::Harbour: return MakeRigView(kHarbourRig);
		case CameraRig::CityReveal: return MakeRigView(kCityRevealRig);
		case CameraRig::Wilderness: return MakeRigView(kWildernessRig);
		case CameraRig::SnowField: return MakeRigView(kSnowFieldRig);
		case CameraRig::CharacterArc: return MakeRigView(kCharacterArcRig);
		case CameraRig::InteriorDolly: return MakeRigView(kInteriorDollyRig);
		case CameraRig::Dungeon: return MakeRigView(kDungeonRig);
		case CameraRig::Cavern: return MakeRigView(kCavernRig);
		case CameraRig::Otherworld: return MakeRigView(kOtherworldRig);
		case CameraRig::Ashland: return MakeRigView(kAshlandRig);
		default: return MakeRigView(kWildernessRig);
		}
	}

	Vec3 Add(Vec3 a, Vec3 b)
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	Vec3 Multiply(Vec3 value, float scale)
	{
		return { value.x * scale, value.y * scale, value.z * scale };
	}

	Vec3 CatmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
	{
		const float t2 = t * t;
		const float t3 = t2 * t;
		return Multiply(
			Add(
				Add(Multiply(p1, 2.0f), Multiply(Add(Multiply(p0, -1.0f), p2), t)),
				Add(
					Multiply(Add(Add(Multiply(p0, 2.0f), Multiply(p1, -5.0f)), Add(Multiply(p2, 4.0f), Multiply(p3, -1.0f))), t2),
					Multiply(Add(Add(Multiply(p0, -1.0f), Multiply(p1, 3.0f)), Add(Multiply(p2, -3.0f), p3)), t3))),
			0.5f);
	}

	float SmoothStep01(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	CameraPose SampleRig(CameraRig rigId, float progress)
	{
		const auto rig = GetRig(rigId);
		progress = std::clamp(progress, 0.0f, 1.0f);
		std::size_t segment = 0;
		while (segment + 2 < rig.size && progress > rig.data[segment + 1].time)
			++segment;

		const std::size_t i0 = segment > 0 ? segment - 1 : segment;
		const std::size_t i1 = segment;
		const std::size_t i2 = std::min(segment + 1, rig.size - 1);
		const std::size_t i3 = std::min(segment + 2, rig.size - 1);
		const auto& a = rig.data[i1];
		const auto& b = rig.data[i2];
		const float span = std::max(b.time - a.time, 1e-4f);
		const float local = SmoothStep01((progress - a.time) / span);

		CameraPose pose{};
		pose.offset = CatmullRom(rig.data[i0].offset, a.offset, b.offset, rig.data[i3].offset, local);
		pose.yawDegrees = std::lerp(a.yawDegrees, b.yawDegrees, local);
		pose.pitchDegrees = std::lerp(a.pitchDegrees, b.pitchDegrees, local);
		pose.fovDegrees = std::lerp(a.fovDegrees, b.fovDegrees, local);
		return pose;
	}

	constexpr auto kMinimumTransitionTime = std::chrono::seconds(2);
	constexpr auto kStableCellTime = std::chrono::seconds(2);
	constexpr auto kFallbackTransitionTime = std::chrono::seconds(10);
	constexpr auto kTransitionTimeout = std::chrono::seconds(60);

	bool IsLoadingScreenOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
	}

	std::uintptr_t GetPlayerCellKey()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? reinterpret_cast<std::uintptr_t>(player->GetParentCell()) : 0;
	}

	std::string Timestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const auto local = std::chrono::current_zone()->to_local(now);
		return std::format("{:%Y%m%d-%H%M%S}", local);
	}

	float Percentile(std::vector<float> values, float quantile)
	{
		if (values.empty())
			return 0.0f;
		std::sort(values.begin(), values.end());
		const float index = std::clamp(quantile, 0.0f, 1.0f) * static_cast<float>(values.size() - 1);
		const auto lower = static_cast<std::size_t>(std::floor(index));
		const auto upper = std::min(lower + 1, values.size() - 1);
		return std::lerp(values[lower], values[upper], index - static_cast<float>(lower));
	}

	float Average(const std::vector<float>& values)
	{
		if (values.empty())
			return 0.0f;
		float sum = 0.0f;
		for (const float value : values)
			sum += value;
		return sum / static_cast<float>(values.size());
	}
}

namespace PIXLRenderer
{
	WorldBenchmark& WorldBenchmark::GetSingleton()
	{
		static WorldBenchmark instance;
		return instance;
	}

	std::size_t WorldBenchmark::GetSceneCount() const
	{
		return routeMode == 0 ? kFeatureTourScenes.size() : kScenes.size();
	}

	std::size_t WorldBenchmark::GetSceneTableIndex() const
	{
		return routeMode == 0 ? kFeatureTourScenes[locationIndex] : locationIndex;
	}

	void WorldBenchmark::DrawUI()
	{
		ImGui::SeparatorText("PIXL CINEMATIC WORLD BENCHMARK");
		ImGui::TextWrapped("Runs deterministic, unpaused camera flights through feature-focused Skyrim scenes. It records clean reference frames, frame-time distributions, PIXL GPU timing and per-pass data for visual regression, performance comparison and promo capture.");

		ImGui::BeginDisabled(running);
		const char* routes[]{ "Feature tour (9 scenes)", "Full consistency sweep (20 scenes)" };
		ImGui::Combo("Route", &routeMode, routes, 2);
		ImGui::Checkbox("Cinematic spline camera", &cinematicCamera);
		ImGui::SliderInt("Flight time per scene", &flythroughSeconds, 6, 30, "%d seconds", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt("Streaming settle time", &settleSeconds, 4, 30, "%d seconds", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Checkbox("Capture three reference frames per scene", &includeScreenshots);
		ImGui::Checkbox("Force DLAA with frame generation off", &forceDlaaNoFrameGeneration);
		if (forceDlaaNoFrameGeneration)
			ImGui::Checkbox("Disable frame limiter for raw timing", &disableFrameLimiter);
		ImGui::EndDisabled();

		ImGui::TextDisabled("Quality overrides are temporary and restored on completion or cancellation.");
		ImGui::TextDisabled("Video: use an external lossless recorder. The benchmark hides PIXL, drives the camera deterministically and writes shot/timing cues without adding an in-process encoder to the measured GPU workload.");

		if (!running) {
			if (ImGui::Button(routeMode == 0 ? "START FEATURE FILM" : "START FULL BENCHMARK", { -1.0f, 0.0f }))
				Start();
		} else {
			const float progress = static_cast<float>(locationIndex) / static_cast<float>(GetSceneCount());
			ImGui::ProgressBar(progress, { -1.0f, 0.0f }, std::format("{} / {}", locationIndex + 1, GetSceneCount()).c_str());
			ImGui::TextWrapped("%s", status.c_str());
			if (ImGui::Button("CANCEL BENCHMARK", { -1.0f, 0.0f }))
				Cancel();
		}
		if (!outputDirectory.empty())
			ImGui::TextDisabled("Output: %s", outputDirectory.string().c_str());
		ImGui::TextDisabled("COC transitions are hard cuts; each location's camera motion is continuous and cinematic.");
	}

	void WorldBenchmark::ApplyQualityOverride()
	{
		if (!forceDlaaNoFrameGeneration || qualitySnapshot.valid)
			return;

		auto& reconstruction = globals::pipeline::imageReconstruction;
		qualitySnapshot.valid = true;
		qualitySnapshot.upscaleMethod = reconstruction.settings.upscaleMethod;
		qualitySnapshot.qualityMode = reconstruction.settings.qualityMode;
		qualitySnapshot.frameGenerationMode = reconstruction.settings.frameGenerationMode;
		qualitySnapshot.frameLimitMode = reconstruction.settings.frameLimitMode;

		reconstruction.settings.upscaleMethod = static_cast<std::uint32_t>(ImageReconstruction::UpscaleMethod::kDLSS);
		reconstruction.settings.qualityMode = 0;
		reconstruction.settings.frameGenerationMode = 0;
		if (disableFrameLimiter)
			reconstruction.settings.frameLimitMode = 0;
		reconstruction.pendingDLSSReset.store(true, std::memory_order_release);

		logger::info(
			"PIXL world benchmark quality override: method=DLSS, mode=DLAA, frameGeneration=off, frameLimit={}",
			disableFrameLimiter ? "off" : "unchanged");
	}

	void WorldBenchmark::RestoreQualityOverride()
	{
		if (!qualitySnapshot.valid)
			return;

		auto& reconstruction = globals::pipeline::imageReconstruction;
		reconstruction.settings.upscaleMethod = qualitySnapshot.upscaleMethod;
		reconstruction.settings.qualityMode = qualitySnapshot.qualityMode;
		reconstruction.settings.frameGenerationMode = qualitySnapshot.frameGenerationMode;
		reconstruction.settings.frameLimitMode = qualitySnapshot.frameLimitMode;
		reconstruction.pendingDLSSReset.store(true, std::memory_order_release);
		qualitySnapshot.valid = false;
		logger::info("PIXL world benchmark restored image-reconstruction settings");
	}

	void WorldBenchmark::Start()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !player->Is3DLoaded() || !camera || GetPlayerCellKey() == 0) {
			status = "Load a playable save before starting the benchmark";
			return;
		}
		if (camera->IsInFreeCameraMode()) {
			status = "Exit the existing free-camera session before starting";
			return;
		}

		outputDirectory = Util::PathHelpers::GetPluginPath() / "Diagnostics" / "Benchmarks" /
		                  std::format("PIXL-WorldBenchmark-{}", Timestamp());
		std::error_code ec;
		std::filesystem::create_directories(outputDirectory / "Screenshots", ec);
		if (ec) {
			status = std::format("Could not create benchmark folder: {}", ec.message());
			return;
		}

		locationIndex = 0;
		running = true;
		phase = Phase::LoadLocation;
		phaseStarted = std::chrono::steady_clock::now();
		ApplyQualityOverride();
		WriteManifest();
		WriteSettingsSnapshot();
		if (auto* menu = Menu::GetSingleton())
			menu->IsEnabled = false;
		HideHudForBenchmark();
		logger::info("PIXL world benchmark started: {}", outputDirectory.string());
	}

	void WorldBenchmark::Cancel()
	{
		ExitBenchmarkCamera();
		RestoreHudAfterBenchmark();
		RestoreQualityOverride();
		running = false;
		phase = Phase::Idle;
		status = "Cancelled - runtime quality restored";
		logger::info("PIXL world benchmark cancelled");
	}

	void WorldBenchmark::QueueCurrentLocation()
	{
		const auto& scene = kScenes[GetSceneTableIndex()];
		ExitBenchmarkCamera();
		status = std::format("Loading {}", scene.label);
		originCell = GetPlayerCellKey();
		stableCell = 0;
		loadingObserved = false;
		fallbackAttempted = false;
		phaseStarted = std::chrono::steady_clock::now();

		// COC initiates a destructive world transition. Never issue another console
		// operation in this frame while the old world and weather manager tear down.
		RE::Console::ExecuteCommand(std::format("coc {}", scene.coc).c_str());
		phase = Phase::AwaitLocation;
	}

	void WorldBenchmark::SkipCurrentLocation(std::string_view reason)
	{
		ExitBenchmarkCamera();
		const auto& scene = kScenes[GetSceneTableIndex()];
		status = std::format("Skipped {}: {}", scene.label, reason);
		logger::warn("PIXL world benchmark skipped '{}': {}", scene.label, reason);
		WriteFailure(reason);
		phase = Phase::Advance;
		phaseStarted = std::chrono::steady_clock::now();
	}

	bool WorldBenchmark::BeginCurrentFlythrough()
	{
		ResetMetrics();
		capturedMarkers = 0;

		if (!cinematicCamera) {
			phase = Phase::Flythrough;
			phaseStarted = std::chrono::steady_clock::now();
			return true;
		}

		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!camera || camera->IsInFreeCameraMode())
			return false;

		// false keeps world simulation running: actors continue walking, rain falls,
		// wetness evolves and deformation remains representative of gameplay.
		camera->ToggleFreeCameraMode(false);
		if (!camera->IsInFreeCameraMode())
			return false;

		auto* freeCameraState = static_cast<RE::FreeCameraState*>(camera->currentState.get());
		if (!freeCameraState) {
			camera->ToggleFreeCameraMode(false);
			return false;
		}

		cameraOwned = true;
		cameraAnchor[0] = freeCameraState->translation.x;
		cameraAnchor[1] = freeCameraState->translation.y;
		cameraAnchor[2] = freeCameraState->translation.z;
		cameraBaseRotation[0] = freeCameraState->rotation.x;
		cameraBaseRotation[1] = freeCameraState->rotation.y;
		originalWorldFov = camera->GetRuntimeData2().worldFOV;

		phase = Phase::Flythrough;
		phaseStarted = std::chrono::steady_clock::now();
		return true;
	}

	void WorldBenchmark::ExitBenchmarkCamera()
	{
		if (!cameraOwned)
			return;
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (camera) {
			camera->GetRuntimeData2().worldFOV = originalWorldFov;
			if (camera->IsInFreeCameraMode())
				camera->ToggleFreeCameraMode(false);
		}
		cameraOwned = false;
	}

	void WorldBenchmark::HideHudForBenchmark()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
			return;
		auto hud = ui->GetMenu<RE::HUDMenu>();
		if (!hud || !hud->uiMovie)
			return;
		if (!hudVisibilitySnapshotValid) {
			hudWasVisible = hud->uiMovie->GetVisible();
			hudVisibilitySnapshotValid = true;
		}
		hud->uiMovie->SetVisible(false);
	}

	void WorldBenchmark::RestoreHudAfterBenchmark()
	{
		if (!hudVisibilitySnapshotValid)
			return;
		if (auto* ui = RE::UI::GetSingleton()) {
			auto hud = ui->GetMenu<RE::HUDMenu>();
			if (hud && hud->uiMovie)
				hud->uiMovie->SetVisible(hudWasVisible);
		}
		hudVisibilitySnapshotValid = false;
	}

	void WorldBenchmark::ResetMetrics()
	{
		frameTimeSamples.clear();
		pixlGpuSamples.clear();
		frameTimeSamples.reserve(static_cast<std::size_t>(flythroughSeconds * 180));
		pixlGpuSamples.reserve(static_cast<std::size_t>(flythroughSeconds * 180));
	}

	void WorldBenchmark::AccumulateMetrics()
	{
		const float frameMs = globals::pipeline::pulseProfiler.state.frameTimeMs;
		if (std::isfinite(frameMs) && frameMs > 0.0f && frameMs < 1000.0f)
			frameTimeSamples.push_back(frameMs);
		const float pixlGpuMs = globals::profiler ? globals::profiler->GetTotalTimeMs() : 0.0f;
		if (std::isfinite(pixlGpuMs) && pixlGpuMs >= 0.0f && pixlGpuMs < 1000.0f)
			pixlGpuSamples.push_back(pixlGpuMs);
	}

	void WorldBenchmark::CaptureFlythroughFrame(std::size_t markerIndex)
	{
		if (!includeScreenshots || markerIndex >= kCaptureMarkers.size())
			return;
		const auto& scene = kScenes[GetSceneTableIndex()];
		const auto path = outputDirectory / "Screenshots" /
		                  std::format("{:02}-{}-shot-{}.png", locationIndex + 1, scene.condition, markerIndex + 1);
		globals::pipeline::pixelCapture.RequestCaptureToPath(path, false);
	}

	void WorldBenchmark::UpdateCurrentFlythrough(std::chrono::steady_clock::time_point now)
	{
		HideHudForBenchmark();
		const auto& scene = kScenes[GetSceneTableIndex()];
		const float elapsed = std::chrono::duration<float>(now - phaseStarted).count();
		const float duration = static_cast<float>(std::max(flythroughSeconds, 1));
		const float progress = std::clamp(elapsed / duration, 0.0f, 1.0f);

		if (cinematicCamera) {
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !cameraOwned || !camera->IsInFreeCameraMode()) {
				SkipCurrentLocation("cinematic camera was lost");
				return;
			}
			auto* freeCameraState = static_cast<RE::FreeCameraState*>(camera->currentState.get());
			if (!freeCameraState) {
				SkipCurrentLocation("free-camera state unavailable");
				return;
			}

			const CameraPose pose = SampleRig(scene.rig, progress);
			const float baseYaw = cameraBaseRotation[1];
			const float cosYaw = std::cos(baseYaw);
			const float sinYaw = std::sin(baseYaw);
			freeCameraState->translation.x = cameraAnchor[0] + pose.offset.x * cosYaw - pose.offset.y * sinYaw;
			freeCameraState->translation.y = cameraAnchor[1] + pose.offset.x * sinYaw + pose.offset.y * cosYaw;
			freeCameraState->translation.z = cameraAnchor[2] + pose.offset.z;
			constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
			freeCameraState->rotation.x = std::clamp(cameraBaseRotation[0] + pose.pitchDegrees * degreesToRadians, -1.48f, 1.48f);
			freeCameraState->rotation.y = cameraBaseRotation[1] + pose.yawDegrees * degreesToRadians;
			camera->GetRuntimeData2().worldFOV = std::clamp(pose.fovDegrees, 40.0f, 90.0f);
		}

		// Exclude the first 5% after entering free camera so temporal histories and
		// the benchmark quality override have a clean frame to settle.
		if (progress >= 0.05f)
			AccumulateMetrics();

		for (std::size_t marker = 0; marker < kCaptureMarkers.size(); ++marker) {
			const std::uint32_t bit = 1u << marker;
			if (progress >= kCaptureMarkers[marker] && (capturedMarkers & bit) == 0) {
				CaptureFlythroughFrame(marker);
				capturedMarkers |= bit;
			}
		}

		status = std::format(
			"{} - cinematic flight {:3.0f}% | {}",
			scene.label,
			progress * 100.0f,
			scene.features);

		if (progress >= 1.0f)
			FinishCurrentFlythrough();
	}

	void WorldBenchmark::FinishCurrentFlythrough()
	{
		WriteSample();
		ExitBenchmarkCamera();
		phase = includeScreenshots ? Phase::AwaitCapture : Phase::Advance;
		phaseStarted = std::chrono::steady_clock::now();
	}

	void WorldBenchmark::Update()
	{
		if (!running)
			return;
		const auto now = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - phaseStarted);
		const auto& scene = kScenes[GetSceneTableIndex()];

		switch (phase) {
		case Phase::LoadLocation:
			QueueCurrentLocation();
			break;
		case Phase::AwaitLocation: {
			const bool loading = IsLoadingScreenOpen();
			if (loading) {
				loadingObserved = true;
				stableCell = 0;
				status = std::format("{} - loading world data", scene.label);
				break;
			}

			const auto currentCell = GetPlayerCellKey();
			const bool cellChanged = currentCell != 0 && currentCell != originCell;
			const bool transitionCanBeComplete = loadingObserved || cellChanged;
			if (transitionCanBeComplete && currentCell != 0) {
				if (stableCell != currentCell) {
					stableCell = currentCell;
					stableCellSince = now;
				}
				if (now - phaseStarted >= kMinimumTransitionTime && now - stableCellSince >= kStableCellTime) {
					phase = Phase::ApplyTime;
					phaseStarted = now;
					status = std::format("{} - applying lighting condition", scene.label);
				}
			}

			if (!transitionCanBeComplete && !fallbackAttempted && scene.fallbackCoc[0] != '\0' && elapsed >= kFallbackTransitionTime) {
				fallbackAttempted = true;
				originCell = currentCell;
				loadingObserved = false;
				stableCell = 0;
				phaseStarted = now;
				status = std::format("{} - primary COC unavailable, trying {}", scene.label, scene.fallbackCoc);
				RE::Console::ExecuteCommand(std::format("coc {}", scene.fallbackCoc).c_str());
				break;
			}

			if (elapsed >= kTransitionTimeout)
				SkipCurrentLocation("world transition timed out");
			break;
		}
		case Phase::ApplyTime:
			if (IsLoadingScreenOpen()) {
				phase = Phase::AwaitLocation;
				stableCell = 0;
				break;
			}
			RE::Console::ExecuteCommand(std::format("set gamehour to {:.2f}", scene.hour).c_str());
			phase = Phase::ApplyWeather;
			phaseStarted = now;
			break;
		case Phase::ApplyWeather:
			if (elapsed < std::chrono::seconds(1))
				break;
			RE::Console::ExecuteCommand(std::format("fw {} 1", scene.weather).c_str());
			phase = Phase::Settle;
			phaseStarted = now;
			break;
		case Phase::Settle:
			if (IsLoadingScreenOpen() || GetPlayerCellKey() == 0) {
				phase = Phase::AwaitLocation;
				stableCell = 0;
				phaseStarted = now;
				break;
			}
			status = std::format("{} - streaming/temporal settle {} s", scene.label, std::max<std::int64_t>(0, settleSeconds - elapsed.count()));
			if (elapsed.count() >= settleSeconds) {
				phase = Phase::BeginFlythrough;
				phaseStarted = now;
			}
			break;
		case Phase::BeginFlythrough:
			if (!BeginCurrentFlythrough())
				SkipCurrentLocation("could not enter unpaused cinematic camera");
			break;
		case Phase::Flythrough:
			UpdateCurrentFlythrough(now);
			break;
		case Phase::AwaitCapture:
			if ((elapsed >= std::chrono::seconds(1) && globals::pipeline::pixelCapture.GetPendingCaptureCount() == 0) || elapsed >= std::chrono::seconds(20)) {
				phase = Phase::Advance;
				phaseStarted = now;
			}
			break;
		case Phase::Advance:
			if (++locationIndex >= GetSceneCount()) {
				ExitBenchmarkCamera();
				RestoreHudAfterBenchmark();
				RestoreQualityOverride();
				running = false;
				phase = Phase::Idle;
				status = "Complete - runtime quality restored";
				logger::info("PIXL world benchmark complete: {}", outputDirectory.string());
			} else {
				phase = Phase::LoadLocation;
				phaseStarted = now;
			}
			break;
		default:
			break;
		}
	}

	void WorldBenchmark::WriteManifest() const
	{
		nlohmann::json manifest{
			{ "schema", "PIXL.WorldBenchmark.v2" },
			{ "route", routeMode == 0 ? "feature-tour" : "full-consistency" },
			{ "sceneCount", GetSceneCount() },
			{ "settleSeconds", settleSeconds },
			{ "flythroughSeconds", flythroughSeconds },
			{ "cinematicCamera", cinematicCamera },
			{ "screenshotsPerScene", includeScreenshots ? kCaptureMarkers.size() : 0 },
			{ "qualityOverride", {
				{ "enabled", forceDlaaNoFrameGeneration },
				{ "method", forceDlaaNoFrameGeneration ? "DLSS" : "user-setting" },
				{ "mode", forceDlaaNoFrameGeneration ? "DLAA" : "user-setting" },
				{ "frameGeneration", forceDlaaNoFrameGeneration ? "off" : "user-setting" },
				{ "frameLimiter", forceDlaaNoFrameGeneration && disableFrameLimiter ? "off" : "user-setting" }
			} },
			{ "videoCapture", {
				{ "backend", "external" },
				{ "reason", "In-process encoding would contaminate benchmark timings; record the clean deterministic route with OBS or another lossless recorder." },
				{ "hardCutsBetweenLocations", true },
				{ "continuousCameraWithinScene", cinematicCamera },
				{ "captureMarkers", kCaptureMarkers }
			} },
			{ "intendedFeatureCoverage", kFeatureCoverage },
			{ "scenes", nlohmann::json::array() }
		};

		for (std::size_t routeIndex = 0; routeIndex < GetSceneCount(); ++routeIndex) {
			const auto tableIndex = routeMode == 0 ? kFeatureTourScenes[routeIndex] : routeIndex;
			const auto& scene = kScenes[tableIndex];
			manifest["scenes"].push_back({
				{ "sequence", routeIndex + 1 },
				{ "label", scene.label },
				{ "coc", scene.coc },
				{ "fallbackCoc", scene.fallbackCoc },
				{ "hour", scene.hour },
				{ "weather", scene.weather },
				{ "condition", scene.condition },
				{ "features", scene.features }
			});
		}

		std::ofstream output(outputDirectory / "manifest.json");
		if (output)
			output << manifest.dump(2);
	}

	void WorldBenchmark::WriteSettingsSnapshot() const
	{
		if (!globals::state)
			return;
		nlohmann::json settings;
		globals::state->SaveToJson(settings);
		std::ofstream output(outputDirectory / "settings.json");
		if (output)
			output << settings.dump(2);
	}

	void WorldBenchmark::WriteSample() const
	{
		const auto& scene = kScenes[GetSceneTableIndex()];
		const float averageFrameMs = Average(frameTimeSamples);
		const float averagePixlGpuMs = Average(pixlGpuSamples);
		nlohmann::json sample{
			{ "schema", "PIXL.WorldBenchmark.Sample.v2" },
			{ "sequence", locationIndex + 1 },
			{ "scene", scene.label },
			{ "coc", fallbackAttempted ? scene.fallbackCoc : scene.coc },
			{ "fallbackUsed", fallbackAttempted },
			{ "hour", scene.hour },
			{ "weather", scene.weather },
			{ "condition", scene.condition },
			{ "features", scene.features },
			{ "cinematic", cinematicCamera },
			{ "sampleCount", frameTimeSamples.size() },
			{ "frameTimeMs", {
				{ "average", averageFrameMs },
				{ "p50", Percentile(frameTimeSamples, 0.50f) },
				{ "p95", Percentile(frameTimeSamples, 0.95f) },
				{ "p99", Percentile(frameTimeSamples, 0.99f) }
			} },
			{ "averageFps", averageFrameMs > 0.0f ? 1000.0f / averageFrameMs : 0.0f },
			{ "pixlGpuMs", {
				{ "average", averagePixlGpuMs },
				{ "p95", Percentile(pixlGpuSamples, 0.95f) },
				{ "p99", Percentile(pixlGpuSamples, 0.99f) }
			} },
			{ "imageReconstruction", {
				{ "upscaleMethod", static_cast<std::uint32_t>(globals::pipeline::imageReconstruction.GetUpscaleMethod()) },
				{ "qualityMode", globals::pipeline::imageReconstruction.settings.qualityMode },
				{ "frameGenerationActive", globals::pipeline::imageReconstruction.IsFrameGenerationActive() },
				{ "resolutionScaleX", globals::pipeline::imageReconstruction.resolutionScale.x },
				{ "resolutionScaleY", globals::pipeline::imageReconstruction.resolutionScale.y }
			} },
			{ "passes", nlohmann::json::array() }
		};

		if (globals::profiler) {
			for (const auto& timer : globals::profiler->GetResults()) {
				if (timer.valid)
					sample["passes"].push_back({ { "name", timer.name }, { "avgMs", timer.avgMs }, { "p95Ms", timer.p95Ms }, { "p99Ms", timer.p99Ms } });
			}
		}

		std::ofstream output(outputDirectory / std::format("{:02}-{}.json", locationIndex + 1, scene.condition));
		if (output)
			output << sample.dump(2);
	}

	void WorldBenchmark::WriteFailure(std::string_view reason) const
	{
		const auto& scene = kScenes[GetSceneTableIndex()];
		nlohmann::json failure{
			{ "schema", "PIXL.WorldBenchmark.Sample.v2" },
			{ "sequence", locationIndex + 1 },
			{ "scene", scene.label },
			{ "coc", fallbackAttempted ? scene.fallbackCoc : scene.coc },
			{ "condition", scene.condition },
			{ "features", scene.features },
			{ "status", "skipped" },
			{ "reason", reason }
		};
		std::ofstream output(outputDirectory / std::format("{:02}-{}-FAILED.json", locationIndex + 1, scene.condition));
		if (output)
			output << failure.dump(2);
	}
}
