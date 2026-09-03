#ifndef __SHARED_DATA_DEPENDENCY_HLSL__
#define __SHARED_DATA_DEPENDENCY_HLSL__

#include "Common/FrameBuffer.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace SharedData
{
	cbuffer SharedData : register(b5)
	{
		float4 WaterData[25];
		float4 DirLightDirection;
		float4 DirLightColor;
		float4 SunDirection;
		float4 SunColor;
		float4 MasserDirection;
		float4 MasserColor;
		float4 SecundaDirection;
		float4 SecundaColor;
		float4 CameraData;
		float4 BufferDim;
		float Timer;
		uint FrameCount;
		uint FrameCountAlwaysActive;
		bool InInterior;  // If the current cell is an interior
		bool HasDirectionalShadows;
		bool InMapMenu;           // If the world/local map is open (note that the renderer is still deferred here)
		bool HideSky;             // HideSky flag in WorldSpace, e.g. Blackreach
		float MipBias;            // Offset to mip level for TAA sharpness
		float WaterSystemHeight;  // TES::GetWaterHeight in camera-relative Z; -FLT_MAX when no water body found
		// Reuses the existing 12-byte padding. Current State::SharedDataCB uploads
		// CameraPosAdjust here, so this changes the symbol only — not the b5 layout.
		float3 CameraPosAdjust;
		float4 AmbientSHR;
		float4 AmbientSHG;
		float4 AmbientSHB;
		float4 HDRData;
		// Appended world-space actor state for water interaction. Historical b5
		// fields retain their offsets.
		float4 PlayerWaterPosition;  // xyz absolute position, w in-water flag
		float4 PlayerWaterVelocity;  // xyz world velocity, w horizontal speed
	};

	struct FoliageDynamicsSettings
	{
		float Glossiness;
		float SpecularStrength;
		float TissueDiffusionAmount;
		bool OverrideComplexGrassSettings;

		float BasicGrassBrightness;
		float ComplexGrassThreshold;
		uint TreeFlipNormalY;       // c1.z; replaces padding, no ABI growth
		float GrassMacroSpecular;   // c1.w; replaces padding, no ABI growth

		uint EnableEnhancedVegetation;
		uint EnableEnhancedWind;
		float LeafTransmission;
		float LeafDiffuseWrap;

		float WindStrength;
		float GustStrength;
		float FlutterStrength;
		float WindSpatialScale;

		float GustSpeed;
		float FlutterSpeed;
		float SpecularAA;
		uint ComplexGrassMode;      // c4.w; 0 auto, 1 basic, 2 DX, 3 flip-Y
	};

	struct DeformableGroundSettings
	{
		uint EnableDeformableGround;
		uint EnableSnowDeformation;
		uint EnableMudDeformation;
		uint MudRequiresWetness;

		float SnowMaximumDepth;
		float MudMaximumDepth;
		float GroundNormalStrength;
		float SnowCompactionDarkening;

		float MudDarkening;
		float MudRoughness;
		float MudWetnessThreshold;
		float GroundResponseStrength;

		float2 PosOffset;
		uint2 ArrayOrigin;
	};

	struct MaterialLayerSettings
	{
		bool EnableComplexMaterial;
		bool EnableParallax;
		bool EnableTerrainParallax;
		bool EnableHeightBlending;
		bool EnableShadows;
		bool EnableParallaxWarpingFix;
		uint2 pad0;
	};

	struct WorldProbeCaptureSettings
	{
		uint Enabled;
		float3 pad0;

		float4 CubemapColor;
	};

	struct TerrainOcclusionSettings
	{
		bool EnableTerrainShadow;
		float3 Scale;
		float2 ZRange;
		float2 Offset;
	};

	struct RadiantGridSettings
	{
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		float2 pad0;
		uint4 ClusterSize;
	};

	struct RainResponseSettings
	{
		row_major float4x4 OcclusionViewProj;

		float Time;
		float Raining;
		float Wetness;
		float PuddleWetness;

		bool EnableRainResponse;
		float MaxRainWetness;
		float MaxPuddleWetness;
		float MaxShoreWetness;

		uint ShoreRange;
		float PuddleRadius;
		float PuddleMaxAngle;
		float PuddleMinWetness;

		float MinRainWetness;
		float SkinWetness;
		float WeatherTransitionSpeed;
		bool EnableRaindropFx;

		bool EnableSplashes;
		bool EnableRipples;
		uint EnableVanillaRipples;
		float RaindropFxRange;

		float RaindropGridSizeRcp;
		float RaindropIntervalRcp;
		float RaindropChance;
		float SplashesLifetime;

		float SplashesStrength;
		float SplashesMinRadius;
		float SplashesMaxRadius;
		float RippleStrength;

		float RippleRadius;
		float RippleBreadth;
		float RippleLifetimeRcp;

		uint EnableRainParticleEnhancement;
		float RainClumpStrength;
		float RainClumpSize;
		float RainStreakVariation;

		float RainGustStrength;
		float RainGustFrequency;
		float RainGustChance;
		float RainSecondaryLayerStrength;

		float RainDepthStart;
		float RainDepthEnd;
		float RainDistanceBoost;
		float RainImpactSplashStrength;

		float RainMistStrength;
		float RainMistScale;
		float RainMistHeight;
		float RainLightingBoost;

		// Phase 4: consumes the former 4-byte RainResponse padding slot. No FeatureData ABI growth.
		float RainRunoffStrength;
	};

	struct SkyBounceSettings
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDir;

		float4 PosOffset;   // xyz: cell origin in camera model space
		uint4 ArrayOrigin;  // xyz: array origin
		int4 ValidMargin;

		float MinDiffuseVisibility;
		float MinSpecularVisibility;
		uint2 pad0;
	};

	struct SkyVeilSettings
	{
		float Opacity;
		uint EnableVolumetricClouds;
		float CloudDensity;
		float CloudDepth;

		float SelfShadowStrength;
		float SilverLining;
		float AmbientLighting;
		float DetailStrength;

		float PhaseEccentricity;
		float HorizonFade;
		float2 pad0;
	};

	struct WaterOpticsSettings
	{
		uint EnableEnhancedCaustics;
		float CausticsStrength;
		float CausticsDispersion;
		float CausticsFocus;

		uint EnableEnhancedSSR;
		float SSRThicknessScale;
		float SSRDistanceScale;
		float SSREdgeFade;

		float SurfaceSSRStrength;
		float CausticsVisibility;
		float WaterTintStrength;
		float ReflectionBrightness;

		uint EnableDynamicFoam;
		float FoamStrength;
		float FoamScale;
		float PlayerWakeStrength;
	};

	struct PostProcessSettings
	{
		uint EnableEnhancedDepthOfField;
		float DofBokehRadius;
		float DofHighlightResponse;
		float DofFocusEdgeProtection;

		float DofForegroundCoverage;
		float DofCatEye;
		float DofAnamorphicRatio;
		uint DofQuality;
	};

	struct DistanceBlendSettings
	{
		float LODTerrainBrightness;
		float LODObjectBrightness;
		float LODObjectSnowBrightness;
		bool DisableTerrainVertexColors;
		float LODTerrainGamma;
		float LODObjectGamma;
		float LODObjectSnowGamma;
		float pad0;
	};

	struct StrandShadingSettings
	{
		uint Enabled;
		float HairGlossiness;
		float SpecularMult;
		float DiffuseMult;
		uint EnableTangentShift;
		float PrimaryTangentShift;
		float SecondaryTangentShift;
		float HairSaturation;
		float SpecularIndirectMult;
		float DiffuseIndirectMult;
		float BaseColorMult;
		float Transmission;
		uint EnableSelfShadow;
		float SelfShadowStrength;
		float SelfShadowExponent;
		float SelfShadowScale;
		uint HairMode;  // 0: Kajiya-Kay, 1: Marschner
		uint3 pad;
	};

	/** @brief Terrain Detail feature settings. */
	struct TerrainDetailSettings
	{
		uint enableLODTerrainTilingFix;  ///< 1 = apply variation to LOD terrain.
		uint3 pad;
	};

	struct AmbientProbeSettings
	{
		uint EnableAmbientProbe;
		uint PreserveFogLuminance;
		uint UseStaticAmbientProbe;
		float DALCAmount;
		float EnvironmentProbeScale;
		float SkyProbeScale;
		float EnvironmentProbeSaturation;
		float SkyProbeSaturation;
		float FogAmount;
		uint DALCMode;  // 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky, 3: DALC + Sky (Directional)
		float pad0;
		float pad1;
	};

	struct ThinSurfaceSettings
	{
		uint MaterialModel;  // [0,1,2,3] The MaterialModel
		float Reduction;     // [0, 1.0] The factor to reduce the transparency to matain the average transparency [0,1]
		float Softness;      // [0, 2.0] The soft remap upper limit [0,2]
		float Strength;      // [0, 1.0] The inverse blend weight of the effect
	};

	struct LinearLightCoreSettings
	{
		uint enableLinearLightCore;
		uint isDirLightLinear;
		float dirLightMult;
		float lightGamma;
		float colorGamma;
		float emitColorGamma;
		float glowmapGamma;
		float ambientGamma;
		float fogGamma;
		float fogAlphaGamma;
		float effectGamma;
		float effectAlphaGamma;
		float skyGamma;
		float waterGamma;
		float vlGamma;
		float vanillaDiffuseColorMult;
		float directionalLightMult;
		float pointLightMult;
		float ambientMult;
		float emitColorMult;
		float glowmapMult;
		float effectLightingMult;
		float membraneEffectMult;
		float bloodEffectMult;
		float projectedEffectMult;
		float deferredEffectMult;
		float otherEffectMult;
		uint pad0;
	};

	// Hair Reconstruction replaces the historical eight-register post-process
	// reservation in place. The exact 128-byte footprint is an ABI contract: every
	// following integrated-module field retains its pre-Hair byte offset.
	struct HairReconstructionSettings
	{
		uint Enabled;
		uint Quality;
		float DetectionThreshold;
		float ReconstructionThreshold;

		uint AnisotropicLighting;
		float DirectionBlend;
		float StrandDetail;
		float Transmission;

		uint SecondaryMotion;
		float WindResponse;
		float MotionStrength;
		float Damping;

		uint WetHair;
		float WetDarkening;
		float WetRoughness;
		float WetWeight;

		uint SnowResponse;
		uint ProceduralStrands;
		float StrandDensity;
		float SilhouetteDetail;

		float SimulationDistance;
		uint DebugMode;
		float FrameDelta;
		uint Padding0;

		uint4 Reserved0;
		uint4 Reserved1;
	};
	struct TerrainSeamSettings
	{
		uint Enabled;
		uint3 _padding;
	};

	struct AtmosphereSettings
	{
		uint enabled;
		uint useWorldProbes;
		float startDistance;
		float fogHeight;
		float fogHeightFalloff;
		float fogDensity;
		float directionalInscatteringMultiplier;
		float directionalInscatteringAnisotropy;
		float4 inscatteringTint;
		float cubemapMipLevel;
		float sunlightAttenuationAmount;
		uint respectVanillaFogFade;
		uint disableVanillaFog;
		float4 fogInscatteringColor;
		float originalFogColorAmount;
		uint volumetricFogEnabled;
		uint volumetricGridPixelSize;
		uint volumetricGridSizeZ;
		float volumetricFogDistance;
		float volumetricFogStartDistance;
		float volumetricFogNearFadeInDistance;
		float volumetricFogExtinctionScale;
		float4 volumetricFogAlbedo;
		float4 volumetricFogEmissive;
		float volumetricDirectionalScatteringIntensity;
		float volumetricShadowBias;
		float volumetricDepthDistributionScale;
		float volumetricSkyLightingIntensity;
		float volumetricFogScatteringDistribution;
		float volumetricHistoryWeight;
		uint volumetricHistoryMissSampleCount;
		float volumetricSampleJitterMultiplier;
		float volumetricUpsampleJitterMultiplier;
		float volumetricLocalLightScatteringIntensity;
		float2 pad0;

		uint volumetricUseDisplayResolutionGrid;
		uint volumetricDepthAwareUpsampling;
		float volumetricDepthAwareUpsamplingStrength;
		float volumetricHistoryRadianceClamp;

		float volumetricHistoryDepthRejection;
		uint mapAtmosphereEnabled;
		uint mapDisableVolumetricFog;
		uint mapDisableVanillaFog;

		float mapFogDensityMultiplier;
		float mapFogHeightFalloffMultiplier;
		float mapStartDistance;
		float mapMinimumTransmittance;

		float mapAmbientInscatteringMultiplier;
		float mapDirectionalInscatteringMultiplier;
		float mapSunlightAttenuationMultiplier;
		float mapWorldProbeMultiplier;

		uint automaticWeatherFog;
		float automaticWeatherStrength;
		float minimumAtmosphereTransmittance;
		float weatherMieStrength;
	};

	struct MaterialForgeSettings
	{
		float VertexAOStrength;
#if defined(MATERIAL_FORGE)
		uint unusedLegacyPhysicalDirectLighting;
		uint LegacyPhysicalDebugMode;
		float LegacyPhysicalSpecularScale;
		uint unusedEnableLegacyMetalInference;
		float unusedLegacyMetalInferenceStrength;
		float unusedLegacyMetalInferenceThreshold;
		float unusedLegacyMetalInferenceMaximum;
#else
		uint EnableLegacyPhysicalDirectLighting;
		uint LegacyPhysicalDebugMode;
		float LegacyPhysicalSpecularScale;
		uint EnableLegacyMetalInference;
		float LegacyMetalInferenceStrength;
		float LegacyMetalInferenceThreshold;
		float LegacyMetalInferenceMaximum;
#endif
		uint EnablePhysicalLocalLightFalloff;
		uint EnableLocalContactShadows;
		uint LocalContactShadowLightCount;
		float LocalContactShadowLength;
		float LocalContactShadowStrength;
		uint EnableSpecularAA;
		float SpecularAAStrength;
		float SpecularAAVarianceClamp;
		uint EnableGGXMultiScatter;
		float GGXMultiScatterStrength;
		float LocalLightMinimumDistance;
		float pad0;
	};

	struct SkinOpticsData
	{
		float4 skinParams;
		float4 skinParams2;
		float4 skinDetailParams;
		float4 sssParams;
		float4 fuzzParams;
		float4 physicalParams;
		float4 wetParams;
	};

	cbuffer FeatureData : register(b6)
	{
		FoliageDynamicsSettings foliageDynamicsSettings;
		DeformableGroundSettings deformableGroundSettings;
		MaterialLayerSettings materialLayerSettings;
		WorldProbeCaptureSettings worldProbeCaptureSettings;
		TerrainOcclusionSettings terrainOcclusionSettings;
		RadiantGridSettings radiantGridSettings;
		RainResponseSettings rainResponseSettings;
		SkyBounceSettings skyBounceSettings;
		SkyVeilSettings skyVeilSettings;
		WaterOpticsSettings waterOpticsSettings;
		PostProcessSettings postProcessSettings;
		DistanceBlendSettings distanceBlendSettings;
		StrandShadingSettings strandShadingSettings;
		TerrainDetailSettings terrainDetailSettings;
		AmbientProbeSettings ambientProbeSettings;
		ThinSurfaceSettings thinSurfaceSettings;
		LinearLightCoreSettings linearLightCoreSettings;
		HairReconstructionSettings hairReconstructionSettings;
		TerrainSeamSettings terrainSeamSettings;
		AtmosphereSettings atmosphereSettings;
		MaterialForgeSettings materialForgeSettings;
		SkinOpticsData skinOpticsData;
	};

	Texture2D<float4> DepthTexture : register(t17);

	// Get a int3 to be used as texture sample coord. [0,1] in uv space
	int3 ConvertUVToSampleCoord(float2 uv)
	{
		uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(uv);
		return int3(uv * BufferDim.xy, 0);
	}

	// Get a raw depth from the depth buffer. [0,1] in uv space
	float GetDepth(float2 uv)
	{
		return DepthTexture.Load(ConvertUVToSampleCoord(uv)).x;
	}

	float GetScreenDepth(float depth)
	{
		return (CameraData.w / (-depth * CameraData.z + CameraData.x));
	}

	float4 GetScreenDepths(float4 depths)
	{
		return (CameraData.w / (-depths * CameraData.z + CameraData.x));
	}

	float GetScreenDepth(float2 uv)
	{
		float depth = GetDepth(uv);
		return GetScreenDepth(depth);
	}

	// Returns water data for the tile containing worldPosition (camera-relative XY).
	float4 GetWaterData(float3 worldPosition)
	{
#ifndef USE_PIXL_STABLE_WATER_TILE_LOOKUP
#define USE_PIXL_STABLE_WATER_TILE_LOOKUP 1
#endif
#if USE_PIXL_STABLE_WATER_TILE_LOOKUP
		// WaterData is uploaded as a 5x5 grid centred on the camera cell. Resolve
		// both cell coordinates explicitly; the legacy fractional/rounding method
		// could jump to an adjacent tile as the camera crossed a half-cell boundary.
		int2 cameraCell = (int2)floor(FrameBuffer::CameraPosAdjust.xy / 4096.0f);
		int2 surfaceCell = (int2)floor((worldPosition.xy + FrameBuffer::CameraPosAdjust.xy) / 4096.0f);
		int2 cellInt = surfaceCell - cameraCell + int2(2, 2);
#else
		float2 cellF = (((worldPosition.xy + FrameBuffer::CameraPosAdjust.xy)) / 4096.0) + 64.0;  // always positive
		int2 cellInt;
		float2 cellFrac = modf(cellF, cellInt);

		cellF = worldPosition.xy / float2(4096.0, 4096.0);  // remap to cell scale
		cellF += 2.5;                                       // 5x5 cell grid
		cellF -= cellFrac;                                  // align to cell borders
		cellInt = round(cellF);
#endif

		uint waterTile = (uint)clamp(cellInt.x + (cellInt.y * 5), 0, 24);  // remap xy to 0-24

		float4 waterData = float4(1.0, 1.0, 1.0, -2147483648);

		[flatten] if (cellInt.x < 5 && cellInt.x >= 0 && cellInt.y < 5 && cellInt.y >= 0)
			waterData = WaterData[waterTile];

		return waterData;
	}

	float3 GetAmbient(float3 normal)
	{
		return SphericalHarmonics::Unproject(AmbientSHR, AmbientSHG, AmbientSHB, normal);
	}
}
#endif  // __SHARED_DATA_DEPENDENCY_HLSL__
