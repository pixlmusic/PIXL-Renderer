#include "QualityProfiles.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Modules/Atmosphere.h"
#include "Modules/SkyVeil.h"
#include "Modules/MaterialLayers.h"
#include "Modules/GroundResponse.h"
#include "Modules/ActorSurfaceEffects.h"
#include "Modules/FoliageDynamics.h"
#include "Modules/FoliageOptimizer.h"
#include "Modules/ReactiveFX.h"
#include "Modules/CameraSuite.h"
#include "Modules/StrandShading.h"
#include "Modules/HairReconstruction.h"
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
#include "ModuleRules.h"

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

		// High is byte-for-value aligned with the former Cinematic contract. The
		// new Cinematic tier is deliberately expensive and spends roughly three
		// times the dominant screen-space ray budget where the implementation has
		// safe headroom. Artistic strength/colour/radius controls remain user-owned.
		constexpr std::array<LightingContract, 4> kLightingContracts{
			LightingContract{ 0, 2, 4, 2, 2, 8, false, 12, 1, 1, 16, 3.0f, 4.0f, 4.0f, 0.08f },
			LightingContract{ 0, 4, 8, 4, 3, 4, true, 24, 1, 1, 24, 2.5f, 5.0f, 6.0f, 0.075f },
			LightingContract{ 0, 6, 12, 8, 4, 2, true, 48, 4, 2, 24, 2.0f, 8.0f, 10.0f, 0.07f },
			LightingContract{ 0, 10, 20, 8, 6, 1, true, 64, 12, 2, 36, 1.6f, 12.0f, 16.0f, 0.06f }
		};

		struct MaterialsContract
		{
			float specularAA;
			float multiscatter;
			std::uint32_t objectNearSteps;
			std::uint32_t objectMaxSteps;
			std::uint32_t objectRefinementSteps;
			std::uint32_t terrainNearSteps;
			std::uint32_t terrainMaxSteps;
			std::uint32_t terrainRefinementSteps;
			std::uint32_t detailQuality;
		};

		constexpr std::array<MaterialsContract, 4> kMaterialsContracts{
			MaterialsContract{ 0.50f, 0.55f, 4, 8, 4, 4, 8, 4, 0 },
			MaterialsContract{ 0.75f, 0.75f, 6, 12, 4, 6, 14, 4, 1 },
			MaterialsContract{ 1.34f, 1.00f, 12, 24, 8, 10, 30, 8, 2 },
			MaterialsContract{ 1.50f, 1.00f, 24, 32, 12, 30, 64, 16, 2 }
		};

		struct AtmosphereContract
		{
			std::uint32_t gridPixelSize;
			std::uint32_t gridDepth;
			std::uint32_t historyMissSamples;
		};

		constexpr std::array<AtmosphereContract, 4> kAtmosphereContracts{
			AtmosphereContract{ 64, 24, 1 },
			AtmosphereContract{ 40, 36, 2 },
			AtmosphereContract{ 24, 64, 4 },
			AtmosphereContract{ 16, 80, 8 }
		};

		struct WaterContract
		{
			float traceDistance;
			float edgeFade;
			float traceQuality;
		};

		constexpr std::array<WaterContract, 4> kWaterContracts{
			WaterContract{ 0.65f, 1.35f, 0.0f },
			WaterContract{ 0.90f, 1.00f, 1.0f },
			WaterContract{ 1.20f, 0.60f, 2.0f },
			WaterContract{ 1.50f, 0.25f, 3.0f }
		};

		struct TerrainVegetationContract
		{
			float tessellationNear;
			float tessellationFar;
			std::uint32_t historyTiles;
			float minPixelSize;
			float fullDetailPixelSize;
			float minDensity;
			float simpleShadingPixelSize;
			float meshCostBias;
			float costBiasStartDistance;
			float collisionDistance;
			bool meshLod;
		};

		constexpr std::array<TerrainVegetationContract, 4> kTerrainVegetationContracts{
			TerrainVegetationContract{ 4.0f, 1.25f, 48u, 6.0f, 48.0f, 0.010f, 16.0f, 0.85f, 3000.0f, 1024.0f, true },
			TerrainVegetationContract{ 7.0f, 2.0f, 96u, 3.5f, 28.0f, 0.020f, 9.0f, 0.60f, 5000.0f, 1536.0f, true },
			TerrainVegetationContract{ 10.0f, 2.5f, 192u, 2.0f, 16.0f, 0.030f, 0.0f, 0.40f, 6000.0f, 2048.0f, false },
			TerrainVegetationContract{ 16.0f, 6.0f, 512u, 1.0f, 8.0f, 0.080f, 0.0f, 0.0f, 20000.0f, 4096.0f, false }
		};

		constexpr std::array<std::uint32_t, 4> kCharacterBurleySamples{ 6u, 12u, 24u, 64u };
		constexpr std::array<std::uint32_t, 4> kCharacterActorCaps{ 8u, 16u, 48u, 64u };
		constexpr std::array<float, 4> kCharacterEffectDistances{ 2000.0f, 3200.0f, 4800.0f, 8000.0f };

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
			// High is intentionally the shipped PIXL known-good baseline. Quality
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
			// Materials. High exactly preserves the live-tested sample counts.
			shadows.bendSettings.SampleCount = contract.shadowSamples;
			pbr.settings.LocalContactShadowLightCount = contract.localShadowLights;

			// Skyrim exposes three native volumetric-lighting grids. Both renderer
			// top tiers map to native High rather than abusing the user Custom slot.
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
			const auto& contract = kMaterialsContracts[quality];
			pbr.EnableSpecularAA = 1;
			pbr.SpecularAAStrength = contract.specularAA;
			pbr.EnableGGXMultiScatter = 1;
			pbr.GGXMultiScatterStrength = contract.multiscatter;
			materials.EnableComplexMaterial = 1;
			materials.EnableParallax = quality >= Medium;
			materials.EnableHeightBlending = quality >= High;
			materials.EnableShadows = quality >= Medium;
			// Scale the actual POM ray loops and detail reconstruction modes.
			tuning.ObjectNearSteps = contract.objectNearSteps;
			tuning.ObjectMaxSteps = contract.objectMaxSteps;
			tuning.ObjectRefinementSteps = contract.objectRefinementSteps;
			tuning.TerrainNearSteps = contract.terrainNearSteps;
			tuning.TerrainMaxSteps = contract.terrainMaxSteps;
			tuning.TerrainRefinementSteps = contract.terrainRefinementSteps;
			tuning.EnableDetailReconstruction = quality >= Medium;
			tuning.DetailQuality = contract.detailQuality;
		}

		void ApplyAtmosphere(int quality)
		{
			auto& clouds = globals::pipeline::skyVeil.settings;
			auto& fog = globals::pipeline::atmosphere.settings;
			// Cloud presence and its authored look remain user-owned. The quality
			// contract scales the real volumetric grid cost without silently enabling
			// clouds, which remain an authored choice.
			static_cast<void>(clouds);
			// Smaller XY footprints and deeper Z grids increase froxel count. The
			// Atmosphere prepass detects these changes and recreates its resources.
			const auto& contract = kAtmosphereContracts[quality];
			fog.volumetricGridPixelSize = contract.gridPixelSize;
			fog.volumetricGridSizeZ = contract.gridDepth;
			fog.volumetricHistoryMissSampleCount = contract.historyMissSamples;
			// Light Volumes are owned by the Lighting quality group. Atmosphere must
			// not silently overwrite their tier after Lighting has been selected.
		}

		void ApplyWater(int quality)
		{
			auto& water = globals::pipeline::waterOptics.settings;
			const auto& contract = kWaterContracts[quality];
			// Keep the signature PIXL reflection/caustic path present at every tier;
			// distance and trace sampling scale its cost/clarity instead of reverting Low
			// to a visibly different vanilla water material.
			water.EnableEnhancedSSR = true;
			water.EnableEnhancedCaustics = true;
			water.SSRDistanceScale = contract.traceDistance;
			water.SSREdgeFade = contract.edgeFade;
			water.SSRTraceQuality = contract.traceQuality;
			globals::pipeline::hybridGI.recompileFlag = true;
			globals::pipeline::hybridGI.queuedResetHistory = true;
		}

		void ApplyTerrainVegetation(int quality)
		{
			auto& ground = globals::pipeline::groundResponse.settings;
			auto& foliage = globals::pipeline::foliageOptimizer.settings;
			const auto& contract = kTerrainVegetationContracts[quality];
			// Reactive FX follows the renderer's terrain/vegetation workload tier,
			// while its artistic strength and master enable remain user-owned.
			globals::pipeline::reactiveFX.settings.Quality = static_cast<std::uint32_t>(quality);
			// Vegetation material response and wind character are artistic controls,
			// not workload controls. Preserve them at every quality tier. Scale the
			// expensive raised snow/mud tessellation factors instead. Coverage, depth,
			// classification and distance are deliberately left untouched: lowering a
			// quality preset must never change where Ground Response exists or how it
			// behaves, only how finely its generated surface is subdivided.
			ground.GeometryTessellationNear = contract.tessellationNear;
			ground.GeometryTessellationFar = contract.tessellationFar;
			// Ground Response keeps interaction coverage at every tier.  The preset
			// only changes geometric density and the bounded session-history budget;
			// it never disables snow, mud, marks, or environmental state behind the
			// user's back.
			ground.SessionSurfaceHistoryTileBudget = contract.historyTiles;
			globals::pipeline::terrainDetail.settings.enableLODTerrainTilingFix = 1;

			// Scale only GPU-culling workload controls. Authored wind, vegetation
			// material response and the user's grass range remain untouched.
			foliage.MinPixelSize = contract.minPixelSize;
			foliage.FullDetailPixelSize = contract.fullDetailPixelSize;
			foliage.MinDensity = contract.minDensity;
			foliage.SimpleShadingPixelSize = contract.simpleShadingPixelSize;
			foliage.MeshCostBias = contract.meshCostBias;
			foliage.CostBiasStartDistance = contract.costBiasStartDistance;
			foliage.CollisionDistance = contract.collisionDistance;
			foliage.EnableMeshLOD = contract.meshLod;
			foliage.EnableOcclusionCulling = true;
		}

		void ApplyCharacters(int quality)
		{
			auto& skin = globals::pipeline::skinOptics.settings;
			auto& sss = globals::pipeline::tissueDiffusion;
			auto& hair = globals::pipeline::strandShading.settings;
			skin.EnableSkin = true;
			skin.EnableSkinDetail = quality >= Medium;
			// Dialogue faces must retain the PIXL skin identity even on Low; tiers
			// reduce Burley samples/detail rather than disabling scattering outright.
			skin.UseSSS = true;
			sss.settings.BurleySamples = kCharacterBurleySamples[quality];
			sss.updateKernels = true;
			hair.Enabled = true;
			hair.HairMode = quality >= High ? 1u : 0u;
			hair.EnableSelfShadow = quality >= Medium;
			auto& actorEffects = globals::pipeline::actorSurfaceEffects;
			actorEffects.ApplyQualityTier(static_cast<std::uint32_t>(quality));
			actorEffects.settings.MaximumAffectedNPCs = kCharacterActorCaps[quality];
			actorEffects.settings.EffectDistance = kCharacterEffectDistances[quality];
			globals::pipeline::hairReconstruction.ApplyQualityTier(static_cast<std::uint32_t>(quality));
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

			for (int quality = Low; quality <= Ultra; ++quality) {
				const auto& contract = kMaterialsContracts[quality];
				if (pbr.EnableSpecularAA == 1 &&
				    NearlyEqual(pbr.SpecularAAStrength, contract.specularAA) &&
				    pbr.EnableGGXMultiScatter == 1 &&
				    NearlyEqual(pbr.GGXMultiScatterStrength, contract.multiscatter) &&
				    materials.EnableComplexMaterial == 1 &&
				    (materials.EnableParallax != 0) == (quality >= Medium) &&
				    (materials.EnableHeightBlending != 0) == (quality >= High) &&
				    (materials.EnableShadows != 0) == (quality >= Medium) &&
				    tuning.ObjectNearSteps == contract.objectNearSteps &&
				    tuning.ObjectMaxSteps == contract.objectMaxSteps &&
				    tuning.ObjectRefinementSteps == contract.objectRefinementSteps &&
				    tuning.TerrainNearSteps == contract.terrainNearSteps &&
				    tuning.TerrainMaxSteps == contract.terrainMaxSteps &&
				    tuning.TerrainRefinementSteps == contract.terrainRefinementSteps &&
				    (tuning.EnableDetailReconstruction != 0) == (quality >= Medium) &&
				    tuning.DetailQuality == contract.detailQuality) {
					return quality;
				}
			}
			return -1;
		}

		int DetectAtmosphereTier()
		{
			const auto& fog = globals::pipeline::atmosphere.settings;

			for (int quality = Low; quality <= Ultra; ++quality) {
				const auto& contract = kAtmosphereContracts[quality];
				if (fog.volumetricGridPixelSize == contract.gridPixelSize &&
				    fog.volumetricGridSizeZ == contract.gridDepth &&
				    fog.volumetricHistoryMissSampleCount == contract.historyMissSamples) {
					return quality;
				}
			}
			return -1;
		}

		int DetectWaterTier()
		{
			const auto& water = globals::pipeline::waterOptics.settings;

			for (int quality = Low; quality <= Ultra; ++quality) {
				const auto& contract = kWaterContracts[quality];
				if (water.EnableEnhancedSSR != 0 &&
				    water.EnableEnhancedCaustics != 0 &&
				    NearlyEqual(water.SSRDistanceScale, contract.traceDistance) &&
				    NearlyEqual(water.SSREdgeFade, contract.edgeFade) &&
				    NearlyEqual(water.SSRTraceQuality, contract.traceQuality)) {
					return quality;
				}
			}
			return -1;
		}

		int DetectTerrainVegetationTier()
		{
			const auto& ground = globals::pipeline::groundResponse.settings;
			const auto& terrain = globals::pipeline::terrainDetail.settings;
			const auto& foliage = globals::pipeline::foliageOptimizer.settings;
			for (int quality = Low; quality <= Ultra; ++quality) {
				const auto& contract = kTerrainVegetationContracts[quality];
				if (NearlyEqual(ground.GeometryTessellationNear, contract.tessellationNear) &&
				    NearlyEqual(ground.GeometryTessellationFar, contract.tessellationFar) &&
				    ground.SessionSurfaceHistoryTileBudget == contract.historyTiles &&
				    terrain.enableLODTerrainTilingFix == 1 &&
				    NearlyEqual(foliage.MinPixelSize, contract.minPixelSize) &&
				    NearlyEqual(foliage.FullDetailPixelSize, contract.fullDetailPixelSize) &&
				    NearlyEqual(foliage.MinDensity, contract.minDensity) &&
				    NearlyEqual(foliage.SimpleShadingPixelSize, contract.simpleShadingPixelSize) &&
				    NearlyEqual(foliage.MeshCostBias, contract.meshCostBias) &&
				    NearlyEqual(foliage.CostBiasStartDistance, contract.costBiasStartDistance) &&
				    NearlyEqual(foliage.CollisionDistance, contract.collisionDistance) &&
				    foliage.EnableMeshLOD == contract.meshLod &&
				    foliage.EnableOcclusionCulling) {
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
			const auto& reconstruction = globals::pipeline::hairReconstruction.settings;

			for (int quality = Low; quality <= Ultra; ++quality) {
				if (skin.EnableSkin &&
				    skin.EnableSkinDetail == (quality >= Medium) &&
				    skin.UseSSS &&
				    sss.BurleySamples == kCharacterBurleySamples[quality] &&
				    hair.Enabled &&
				    hair.HairMode == (quality >= High ? 1u : 0u) &&
				    (hair.EnableSelfShadow != 0) == (quality >= Medium) &&
				    (!globals::pipeline::actorSurfaceEffects.loaded ||
				     (globals::pipeline::actorSurfaceEffects.settings.EffectQuality == static_cast<std::uint32_t>(quality) &&
				      globals::pipeline::actorSurfaceEffects.settings.MaximumAffectedNPCs == kCharacterActorCaps[quality] &&
				      NearlyEqual(globals::pipeline::actorSurfaceEffects.settings.EffectDistance, kCharacterEffectDistances[quality]))) &&
				    (!globals::pipeline::hairReconstruction.loaded ||
				     reconstruction.Quality == static_cast<std::uint32_t>(quality))) {
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
		ModuleRules::InvalidateConstraintCache();
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
		ModuleRules::InvalidateConstraintCache();
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
