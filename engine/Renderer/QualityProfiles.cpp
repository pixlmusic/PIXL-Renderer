#include "QualityProfiles.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Modules/Atmosphere.h"
#include "Modules/SkyVeil.h"
#include "Modules/MaterialLayers.h"
#include "Modules/GroundResponse.h"
#include "Modules/FoliageDynamics.h"
#include "Modules/CameraSuite.h"
#include "Modules/StrandShading.h"
#include "Modules/HybridGI.h"
#include "Modules/ContactShadows.h"
#include "Modules/SkinOptics.h"
#include "Modules/TissueDiffusion.h"
#include "Modules/TerrainDetail.h"
#include "Modules/LightVolumes.h"
#include "Modules/WaterOptics.h"
#include "Globals.h"
#include "Menu.h"
#include "State.h"
#include "MaterialForge.h"

namespace PIXLRenderer::QualityProfiles
{
	namespace
	{
		int Clamp(int quality) { return std::clamp(quality, Low, Ultra); }

		struct LightingContract
		{
			int resolutionMode;
			std::uint32_t slices;
			std::uint32_t steps;
			std::uint32_t cacheSamples;
			std::uint32_t cacheTraceSteps;
			std::uint32_t injectionStride;
			bool secondBounce;
			std::uint32_t reflectionSteps;
			std::uint32_t shadowSamples;
			std::uint32_t localShadowLights;
			std::uint32_t historyFrames;
			float blurRadius;
			float radianceFireflyLimit;
			float reflectionFireflyLimit;
			float cacheResponse;
		};

		// Ultra is byte-for-value aligned with the shipped PIXL live-tested
		// configuration. Lower tiers reduce only workload/stability quality; the
		// renderer's authored GI strength, colour, radius and experimental feature
		// switches remain user-owned.
		constexpr std::array<LightingContract, 4> kLightingContracts{
			LightingContract{ 0, 3, 6, 3, 2, 6, false, 16, 1, 1, 20, 2.8f, 4.0f, 4.0f, 0.08f },
			LightingContract{ 0, 4, 8, 4, 3, 4, true, 24, 1, 1, 20, 2.5f, 5.0f, 6.0f, 0.10f },
			LightingContract{ 0, 5, 10, 6, 4, 3, true, 32, 2, 1, 20, 2.2f, 6.0f, 8.0f, 0.12f },
			LightingContract{ 0, 6, 12, 8, 4, 2, true, 48, 2, 1, 18, 2.0f, 8.0f, 10.0f, 0.14f }
		};

		bool NearlyEqual(float left, float right)
		{
			return std::abs(left - right) <= 1.0e-4f;
		}

		bool MatchesLightingContract(const HybridGI::Settings& settings, const LightingContract& contract)
		{
			return settings.EnableGI && settings.EnableBlur && settings.EnableTemporalDenoiser &&
			       settings.EnableWorldCache && settings.EnableDirectionalOcclusion &&
			       settings.EnableBentNormalLighting && settings.EnableSpecularOcclusion &&
			       settings.EnableAdaptiveDenoiser &&
			       settings.ResolutionMode == contract.resolutionMode &&
			       settings.NumSlices == contract.slices &&
			       settings.NumSteps == contract.steps &&
			       settings.WorldCacheSampleCount == contract.cacheSamples &&
			       settings.WorldCacheTraceSteps == contract.cacheTraceSteps &&
			       settings.WorldCacheInjectionStride == contract.injectionStride &&
			       settings.EnableWorldCacheSecondBounce == contract.secondBounce &&
			       settings.ReflectionSteps == contract.reflectionSteps &&
			       settings.MaxAccumFrames == contract.historyFrames &&
			       NearlyEqual(settings.BlurRadius, contract.blurRadius) &&
			       NearlyEqual(settings.RadianceFireflyClamp, contract.radianceFireflyLimit) &&
			       NearlyEqual(settings.ReflectionFireflyClamp, contract.reflectionFireflyLimit) &&
			       NearlyEqual(settings.WorldCacheTemporalResponse, contract.cacheResponse);
		}

		void ApplyLighting(int quality)
		{
			auto& gi = globals::pipeline::hybridGI;
			auto& shadows = globals::pipeline::contactShadows;
			auto& pbr = globals::pipeline::materialForge;
			auto& volumes = globals::pipeline::lightVolumes;

			// Q1 UNIFIED LIGHTING QUALITY
			// Ultra is intentionally the shipped PIXL known-good baseline. Quality
			// profiles change workload/stability controls only; artistic controls such
			// as GI strength, saturation, AO power and radii remain user-owned.
			//
			// Never compound an upscaler's reduced render resolution with another
			// spatial reduction. All tiers trace the complete game render grid and
			// scale through bounded ray density, cache cadence and shadow sampling.
			const auto& contract = kLightingContracts[quality];

			// Radiance Weave: apply the COMPLETE tier contract. The previous Q1
			// draft omitted trace steps and feature gates, allowing settings from a
			// previously selected tier to leak into the new one.
			gi.settings.EnableGI = true;
			gi.settings.EnableBlur = true;
			gi.settings.EnableTemporalDenoiser = true;
			gi.settings.EnableWorldCache = true;
			gi.settings.EnableDirectionalOcclusion = true;
			gi.settings.EnableBentNormalLighting = true;
			gi.settings.EnableSpecularOcclusion = true;
			gi.settings.EnableAdaptiveDenoiser = true;

			gi.settings.ResolutionMode = contract.resolutionMode;
			gi.settings.NumSlices = contract.slices;
			gi.settings.NumSteps = contract.steps;
			gi.settings.WorldCacheSampleCount = contract.cacheSamples;
			gi.settings.WorldCacheTraceSteps = contract.cacheTraceSteps;
			gi.settings.WorldCacheInjectionStride = contract.injectionStride;
			gi.settings.EnableWorldCacheSecondBounce = contract.secondBounce;
			gi.settings.ReflectionSteps = contract.reflectionSteps;
			gi.settings.MaxAccumFrames = contract.historyFrames;
			gi.settings.BlurRadius = contract.blurRadius;
			gi.settings.RadianceFireflyClamp = contract.radianceFireflyLimit;
			gi.settings.ReflectionFireflyClamp = contract.reflectionFireflyLimit;
			gi.settings.WorldCacheTemporalResponse = contract.cacheResponse;

			// Contact Shadows + local-light contact rays are part of Lighting, not
			// Materials. Ultra exactly preserves the live-tested sample counts.
			shadows.bendSettings.SampleCount = contract.shadowSamples;
			pbr.settings.LocalContactShadowLightCount = contract.localShadowLights;

			// Skyrim exposes three native volumetric-lighting grids. Q1 maps Ultra
			// to native High for now instead of abusing the user Custom slot. The
			// module owns this translation so a true Ultra grid can be added later.
			volumes.ApplyRendererQualityTier(quality);

			// Sampling/permutation changes must invalidate both shader state and the
			// accumulated GI/cache data or the visual change can be delayed/hidden.
			gi.recompileFlag = true;
			gi.queuedResetHistory = true;
			gi.queuedResetTemporalHistory = true;
			shadows.InvalidateRaymarchShaders();
		}

		void SetMenuGroupQuality(Group group, int quality)
		{
			auto& menu = globals::menu->GetSettings();
			switch (group) {
			case Group::Lighting: menu.LightingQuality = quality; break;
			case Group::Materials: menu.MaterialsQuality = quality; break;
			case Group::Atmosphere: menu.AtmosphereQuality = quality; break;
			case Group::Water: menu.WaterQuality = quality; break;
			case Group::TerrainVegetation: menu.TerrainVegetationQuality = quality; break;
			case Group::Characters: menu.CharactersQuality = quality; break;
			case Group::Camera: menu.CameraQuality = quality; break;
			case Group::Count: break;
			}
		}

		void ApplyMaterials(int quality)
		{
			auto& pbr = globals::pipeline::materialForge.settings;
			auto& materials = globals::pipeline::materialLayers.settings;
			auto& tuning = globals::pipeline::materialLayers.tuningSettings;
			constexpr std::array<float, 4> specularAA{ 0.45f, 0.60f, 0.75f, 0.98f };
			constexpr std::array<float, 4> multiscatter{ 0.55f, 0.75f, 1.00f, 1.00f };
			constexpr std::array<std::uint32_t, 4> objectNearSteps{ 4, 6, 8, 8 };
			constexpr std::array<std::uint32_t, 4> objectMaxSteps{ 8, 10, 14, 16 };
			constexpr std::array<std::uint32_t, 4> terrainNearSteps{ 4, 8, 10, 12 };
			constexpr std::array<std::uint32_t, 4> terrainMaxSteps{ 8, 12, 16, 20 };
			constexpr std::array<std::uint32_t, 4> detailQuality{ 0, 1, 2, 2 };
			pbr.EnableSpecularAA = 1;
			pbr.SpecularAAStrength = specularAA[quality];
			pbr.EnableGGXMultiScatter = 1;
			pbr.GGXMultiScatterStrength = multiscatter[quality];
			materials.EnableComplexMaterial = 1;
			materials.EnableParallax = quality >= Medium;
			materials.EnableHeightBlending = quality >= High;
			materials.EnableShadows = quality >= Medium;
			// Scale the actual POM ray loops and detail reconstruction modes. Ultra
			// exactly matches the shipped live tuning block.
			tuning.ObjectNearSteps = objectNearSteps[quality];
			tuning.ObjectMaxSteps = objectMaxSteps[quality];
			tuning.ObjectRefinementSteps = 4;
			tuning.TerrainNearSteps = terrainNearSteps[quality];
			tuning.TerrainMaxSteps = terrainMaxSteps[quality];
			tuning.TerrainRefinementSteps = 4;
			tuning.EnableDetailReconstruction = quality >= Medium;
			tuning.DetailQuality = detailQuality[quality];
		}

		void ApplyAtmosphere(int quality)
		{
			auto& clouds = globals::pipeline::skyVeil.settings;
			auto& fog = globals::pipeline::atmosphere.settings;
			clouds.EnableVolumetricClouds = 1;
			clouds.DetailStrength = std::array{ 0.20f, 0.35f, 0.45f, 0.60f }[quality];
			clouds.SelfShadowStrength = std::array{ 0.45f, 0.60f, 0.70f, 0.82f }[quality];
			// Smaller XY footprints and deeper Z grids increase froxel count. The
			// Atmosphere prepass detects these changes and recreates its resources.
			fog.volumetricGridPixelSize = std::array<std::uint32_t, 4>{ 32, 24, 20, 16 }[quality];
			fog.volumetricGridSizeZ = std::array<std::uint32_t, 4>{ 32, 40, 52, 64 }[quality];
			fog.volumetricHistoryMissSampleCount = std::array<std::uint32_t, 4>{ 1, 2, 3, 4 }[quality];
			// Light Volumes are owned by the Lighting quality group. Atmosphere must
			// not silently overwrite their tier after Lighting has been selected.
		}

		void ApplyWater(int quality)
		{
			auto& water = globals::pipeline::waterOptics.settings;
			water.EnableEnhancedSSR = quality >= Medium;
			water.EnableEnhancedCaustics = quality >= Medium;
			water.SSRDistanceScale = std::array{ 0.65f, 0.85f, 1.00f, 1.20f }[quality];
			water.SSREdgeFade = std::array{ 1.35f, 1.15f, 1.00f, 0.90f }[quality];
			water.CausticsDispersion = std::array{ 0.20f, 0.35f, 0.50f, 0.65f }[quality];
			globals::pipeline::hybridGI.recompileFlag = true;
			globals::pipeline::hybridGI.queuedResetHistory = true;
		}

		void ApplyTerrainVegetation(int quality)
		{
			auto& vegetation = globals::pipeline::foliageDynamics.settings;
			auto& ground = globals::pipeline::groundResponse.settings;
			// Material lighting is an artistic/user choice and must not be silently
			// enabled by the motion-quality preset. In particular, selecting a wind
			// quality level must never opt animated trees into a different BRDF.
			// Motion remains an explicit user choice.  Quality controls the cost and
			// character of PIXL wind, but selecting a global preset must not suddenly
			// animate a previously stable load order.
			vegetation.SpecularAA = std::array{ 0.35f, 0.50f, 0.65f, 0.80f }[quality];
			vegetation.FlutterStrength = std::array{ 0.12f, 0.18f, 0.22f, 0.28f }[quality];
			ground.EnableDeformableGround = quality >= Medium;
			ground.EnableSnowDeformation = quality >= Medium;
			ground.EnableMudDeformation = quality >= High;
			globals::pipeline::terrainDetail.settings.enableLODTerrainTilingFix = 1;
		}

		void ApplyCharacters(int quality)
		{
			auto& skin = globals::pipeline::skinOptics.settings;
			auto& sss = globals::pipeline::tissueDiffusion;
			auto& hair = globals::pipeline::strandShading.settings;
			skin.EnableSkin = true;
			skin.EnableSkinDetail = quality >= Medium;
			skin.UseSSS = quality >= Medium;
			sss.settings.BurleySamples = std::array<uint, 4>{ 8, 12, 16, 21 }[quality];
			sss.updateKernels = true;
			hair.Enabled = true;
			hair.HairMode = quality >= High ? 1u : 0u;
			hair.EnableSelfShadow = quality >= Medium;
		}

		void ApplyCamera(int quality)
		{
			// Camera tiers change sampling cost only. Enabling effects or altering
			// exposure/grade here made the same saved look vary across hardware.
			globals::pipeline::cameraSuite.cameraQuality = static_cast<uint32_t>(quality);
		}

		void ApplyGroupSettings(Group group, int quality)
		{
			switch (group) {
			case Group::Lighting: ApplyLighting(quality); break;
			case Group::Materials: ApplyMaterials(quality); break;
			case Group::Atmosphere: ApplyAtmosphere(quality); break;
			case Group::Water: ApplyWater(quality); break;
			case Group::TerrainVegetation: ApplyTerrainVegetation(quality); break;
			case Group::Characters: ApplyCharacters(quality); break;
			case Group::Camera: ApplyCamera(quality); break;
			case Group::Count: break;
			}
		}

		int DetectMaterialsTier()
		{
			const auto& pbr = globals::pipeline::materialForge.settings;
			const auto& materials = globals::pipeline::materialLayers.settings;
			const auto& tuning = globals::pipeline::materialLayers.tuningSettings;
			constexpr std::array<float, 4> specularAA{ 0.45f, 0.60f, 0.75f, 0.98f };
			constexpr std::array<float, 4> multiscatter{ 0.55f, 0.75f, 1.00f, 1.00f };
			constexpr std::array<std::uint32_t, 4> objectNearSteps{ 4, 6, 8, 8 };
			constexpr std::array<std::uint32_t, 4> objectMaxSteps{ 8, 10, 14, 16 };
			constexpr std::array<std::uint32_t, 4> terrainNearSteps{ 4, 8, 10, 12 };
			constexpr std::array<std::uint32_t, 4> terrainMaxSteps{ 8, 12, 16, 20 };
			constexpr std::array<std::uint32_t, 4> detailQuality{ 0, 1, 2, 2 };

			for (int quality = Low; quality <= Ultra; ++quality) {
				if (pbr.EnableSpecularAA == 1 &&
				    NearlyEqual(pbr.SpecularAAStrength, specularAA[quality]) &&
				    pbr.EnableGGXMultiScatter == 1 &&
				    NearlyEqual(pbr.GGXMultiScatterStrength, multiscatter[quality]) &&
				    materials.EnableComplexMaterial == 1 &&
				    (materials.EnableParallax != 0) == (quality >= Medium) &&
				    (materials.EnableHeightBlending != 0) == (quality >= High) &&
				    (materials.EnableShadows != 0) == (quality >= Medium) &&
				    tuning.ObjectNearSteps == objectNearSteps[quality] &&
				    tuning.ObjectMaxSteps == objectMaxSteps[quality] &&
				    tuning.ObjectRefinementSteps == 4 &&
				    tuning.TerrainNearSteps == terrainNearSteps[quality] &&
				    tuning.TerrainMaxSteps == terrainMaxSteps[quality] &&
				    tuning.TerrainRefinementSteps == 4 &&
				    (tuning.EnableDetailReconstruction != 0) == (quality >= Medium) &&
				    tuning.DetailQuality == detailQuality[quality]) {
					return quality;
				}
			}
			return -1;
		}

		int DetectAtmosphereTier()
		{
			const auto& clouds = globals::pipeline::skyVeil.settings;
			const auto& fog = globals::pipeline::atmosphere.settings;
			constexpr std::array<float, 4> detail{ 0.20f, 0.35f, 0.45f, 0.60f };
			constexpr std::array<float, 4> shadow{ 0.45f, 0.60f, 0.70f, 0.82f };
			constexpr std::array<std::uint32_t, 4> gridXY{ 32, 24, 20, 16 };
			constexpr std::array<std::uint32_t, 4> gridZ{ 32, 40, 52, 64 };
			constexpr std::array<std::uint32_t, 4> historyMiss{ 1, 2, 3, 4 };

			for (int quality = Low; quality <= Ultra; ++quality) {
				if (clouds.EnableVolumetricClouds == 1 &&
				    NearlyEqual(clouds.DetailStrength, detail[quality]) &&
				    NearlyEqual(clouds.SelfShadowStrength, shadow[quality]) &&
				    fog.volumetricGridPixelSize == gridXY[quality] &&
				    fog.volumetricGridSizeZ == gridZ[quality] &&
				    fog.volumetricHistoryMissSampleCount == historyMiss[quality]) {
					return quality;
				}
			}
			return -1;
		}

		int DetectWaterTier()
		{
			const auto& water = globals::pipeline::waterOptics.settings;
			constexpr std::array<float, 4> distance{ 0.65f, 0.85f, 1.00f, 1.20f };
			constexpr std::array<float, 4> edgeFade{ 1.35f, 1.15f, 1.00f, 0.90f };
			constexpr std::array<float, 4> dispersion{ 0.20f, 0.35f, 0.50f, 0.65f };

			for (int quality = Low; quality <= Ultra; ++quality) {
				if ((water.EnableEnhancedSSR != 0) == (quality >= Medium) &&
				    (water.EnableEnhancedCaustics != 0) == (quality >= Medium) &&
				    NearlyEqual(water.SSRDistanceScale, distance[quality]) &&
				    NearlyEqual(water.SSREdgeFade, edgeFade[quality]) &&
				    NearlyEqual(water.CausticsDispersion, dispersion[quality])) {
					return quality;
				}
			}
			return -1;
		}

		int DetectTerrainVegetationTier()
		{
			const auto& vegetation = globals::pipeline::foliageDynamics.settings;
			const auto& ground = globals::pipeline::groundResponse.settings;
			const auto& terrain = globals::pipeline::terrainDetail.settings;
			constexpr std::array<float, 4> specularAA{ 0.35f, 0.50f, 0.65f, 0.80f };
			constexpr std::array<float, 4> flutter{ 0.12f, 0.18f, 0.22f, 0.28f };

			for (int quality = Low; quality <= Ultra; ++quality) {
				if (NearlyEqual(vegetation.SpecularAA, specularAA[quality]) &&
				    NearlyEqual(vegetation.FlutterStrength, flutter[quality]) &&
				    ground.EnableDeformableGround == (quality >= Medium) &&
				    ground.EnableSnowDeformation == (quality >= Medium) &&
				    ground.EnableMudDeformation == (quality >= High) &&
				    terrain.enableLODTerrainTilingFix == 1) {
					return quality;
				}
			}
			return -1;
		}

		int DetectCharactersTier()
		{
			const auto& skin = globals::pipeline::skinOptics.settings;
			const auto& sss = globals::pipeline::tissueDiffusion.settings;
			const auto& hair = globals::pipeline::strandShading.settings;
			constexpr std::array<uint, 4> samples{ 8, 12, 16, 21 };

			for (int quality = Low; quality <= Ultra; ++quality) {
				if (skin.EnableSkin &&
				    skin.EnableSkinDetail == (quality >= Medium) &&
				    skin.UseSSS == (quality >= Medium) &&
				    sss.BurleySamples == samples[quality] &&
				    hair.Enabled &&
				    hair.HairMode == (quality >= High ? 1u : 0u) &&
				    (hair.EnableSelfShadow != 0) == (quality >= Medium)) {
					return quality;
				}
			}
			return -1;
		}
	}

	void Apply(Group group, int quality)
	{
		quality = Clamp(quality);
		ApplyGroupSettings(group, quality);
		SetMenuGroupQuality(group, quality);
		globals::state->UpdateFeatureData(globals::state->inWorld);
		globals::state->Save();
	}

	void ApplyGlobal(int quality)
	{
		quality = Clamp(quality);
		auto& menu = globals::menu->GetSettings();
		menu.RendererQuality = quality;
		for (int group = 0; group < static_cast<int>(Group::Count); ++group) {
			const auto qualityGroup = static_cast<Group>(group);
			ApplyGroupSettings(qualityGroup, quality);
			SetMenuGroupQuality(qualityGroup, quality);
		}
		// Commit all seven groups in one coherent feature-data update. Calling
		// Apply() here previously rebuilt feature data seven times.
		globals::state->UpdateFeatureData(globals::state->inWorld);
		globals::state->Save();
	}

	int DetectLightingTier()
	{
		const auto& gi = globals::pipeline::hybridGI.settings;
		const auto& shadows = globals::pipeline::contactShadows.bendSettings;
		const auto& pbr = globals::pipeline::materialForge.settings;
		const auto& volumes = globals::pipeline::lightVolumes.settings;

		for (int quality = Low; quality <= Ultra; ++quality) {
			const auto& contract = kLightingContracts[quality];
			const int nativeVolumeQuality = std::min(quality, High);
			if (MatchesLightingContract(gi, contract) &&
			    shadows.SampleCount == contract.shadowSamples &&
			    pbr.LocalContactShadowLightCount == contract.localShadowLights &&
			    volumes.ExteriorQuality == nativeVolumeQuality &&
			    volumes.InteriorQuality == nativeVolumeQuality)
				return quality;
		}
		return -1;
	}

	int Detect(Group group)
	{
		switch (group) {
		case Group::Lighting: return DetectLightingTier();
		case Group::Materials: return DetectMaterialsTier();
		case Group::Atmosphere: return DetectAtmosphereTier();
		case Group::Water: return DetectWaterTier();
		case Group::TerrainVegetation: return DetectTerrainVegetationTier();
		case Group::Characters: return DetectCharactersTier();
		case Group::Camera:
			return globals::pipeline::cameraSuite.cameraQuality <= static_cast<std::uint32_t>(Ultra) ?
			           static_cast<int>(globals::pipeline::cameraSuite.cameraQuality) :
			           -1;
		case Group::Count: return -1;
		}
		return -1;
	}
}
