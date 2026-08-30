#pragma once

#include <atomic>

struct SkyVeil;
struct WorldProbes;
struct VolumeOcclusion;
struct MaterialLayers;
struct GroundResponse;
struct ActorSurfaceEffects;
struct FoliageDynamics;
struct StrandShading;
struct HorizonBlend;
struct AmbientProbe;
struct RadiantGrid;
struct LinearLightCore;
struct DistanceBlend;
struct InteriorDaylight;
struct NaturalLighting;
struct HybridGI;
struct ContactShadows;
struct SkyBounce;
struct TerrainDetail;
struct SkyContinuity;
struct TissueDiffusion;
struct TerrainSeam;
struct TerrainField;
struct TerrainOcclusion;
struct Waterbody;
struct LightVolumes;
struct WaterOptics;
struct PulseProfiler;
struct RainResponse;
struct ThinSurface;
struct ImageReconstruction;
class Profiler;
struct Atmosphere;
struct CameraSuite;
struct PixelCapture;
struct SkinOptics;
struct WindowLife;

class State;
class Deferred;
struct MaterialForge;
class Menu;
class WeatherManager;
class SceneSettingsManager;

namespace SIE
{
	class ShaderCache;
	class ShaderFileDependencyTracker;
}

/**
 * @brief Initializes core singletons (ShaderCache, State, Menu, Deferred).
 */
void OnInit();

/**
 * @brief Resolves runtime game pointers, RTTI relocations, and D3D device references.
 */
void ReInit();

/**
 * @brief Caches late-binding game singletons (player, sky, INI settings).
 */
void OnDataLoaded();

/**
 * @brief Signals shader compilation to stop.
 */
void OnGameWindowClose();

/**
 * @brief Installs Detours hooks on the device context's Map/Unmap vtable slots to capture per-frame constant buffer data.
 * @param a_context The D3D11 device context to hook.
 */
void InstallD3DHooks(ID3D11DeviceContext* a_context);
namespace globals
{
	namespace d3d
	{
		extern ID3D11Device* device;
		extern ID3D11DeviceContext* context;
		extern IDXGISwapChain* swapChain;
	}

	namespace pipeline
	{
		extern SkyVeil skyVeil;
		extern WorldProbes worldProbes;
		extern VolumeOcclusion volumeOcclusion;
		extern MaterialLayers materialLayers;
		extern GroundResponse groundResponse;
		extern ActorSurfaceEffects actorSurfaceEffects;
		extern FoliageDynamics foliageDynamics;
		extern StrandShading strandShading;
		extern HorizonBlend horizonBlend;
		extern AmbientProbe ambientProbe;
		extern RadiantGrid radiantGrid;
		extern LinearLightCore linearLightCore;
		extern DistanceBlend distanceBlend;
		extern InteriorDaylight interiorDaylight;
		extern NaturalLighting naturalLighting;
		extern HybridGI hybridGI;
		extern ContactShadows contactShadows;
		extern SkyBounce skyBounce;
		extern TerrainDetail terrainDetail;
		extern SkyContinuity skyContinuity;
		extern TissueDiffusion tissueDiffusion;
		extern TerrainSeam terrainSeam;
		extern TerrainField terrainField;
		extern TerrainOcclusion terrainOcclusion;
		extern Waterbody waterbody;
		extern LightVolumes lightVolumes;
		extern WaterOptics waterOptics;
		extern PulseProfiler pulseProfiler;
		extern RainResponse rainResponse;
		extern ThinSurface thinSurface;
		extern ImageReconstruction imageReconstruction;
		extern CameraSuite cameraSuite;
		extern PixelCapture pixelCapture;
		extern Atmosphere atmosphere;
		extern MaterialForge materialForge;
		extern SkinOptics skinOptics;
		extern WindowLife windowLife;

	}

	/** @brief GPU constant buffer layout matching Skyrim's per-frame camera data. */
	struct FrameBuffer
	{
		Matrix CameraView;
		Matrix CameraProj;
		Matrix CameraViewProj;
		Matrix CameraViewProjUnjittered;
		Matrix CameraPreviousViewProjUnjittered;
		Matrix CameraProjUnjittered;
		Matrix CameraProjUnjitteredInverse;
		Matrix CameraViewInverse;
		Matrix CameraViewProjInverse;
		Matrix CameraProjInverse;
		float4 CameraPosAdjust;
		float4 CameraPreviousPosAdjust;
		float4 FrameParams;
		float4 DynamicResolutionParams1;
		float4 DynamicResolutionParams2;
	};

	/** @brief Cached snapshot of per-frame camera data, captured between Map/Unmap of the per-frame constant buffer. */
	struct FrameBufferCache
	{
		FrameBuffer data;

		/** Gets the camera view matrix. */
		const Matrix& GetCameraView() const { return data.CameraView; }
		/** Gets the camera projection matrix. */
		const Matrix& GetCameraProj() const { return data.CameraProj; }
		/** Gets the combined camera view-projection matrix. */
		const Matrix& GetCameraViewProj() const { return data.CameraViewProj; }
		/** Gets the unjittered camera view-projection matrix. */
		const Matrix& GetCameraViewProjUnjittered() const { return data.CameraViewProjUnjittered; }
		/** Gets the previous frame's unjittered view-projection matrix. */
		const Matrix& GetCameraPreviousViewProjUnjittered() const { return data.CameraPreviousViewProjUnjittered; }
		/** Gets the unjittered camera projection matrix. */
		const Matrix& GetCameraProjUnjittered() const { return data.CameraProjUnjittered; }
		/** Gets the inverse of the unjittered camera projection matrix. */
		const Matrix& GetCameraProjUnjitteredInverse() const { return data.CameraProjUnjitteredInverse; }
		/** Gets the inverse camera view matrix. */
		const Matrix& GetCameraViewInverse() const { return data.CameraViewInverse; }
		/** Gets the inverse camera view-projection matrix. */
		const Matrix& GetCameraViewProjInverse() const { return data.CameraViewProjInverse; }
		/** Gets the inverse camera projection matrix. */
		const Matrix& GetCameraProjInverse() const { return data.CameraProjInverse; }
		/** Gets the camera position adjustment vector. */
		const float4& GetCameraPosAdjust() const { return data.CameraPosAdjust; }
		/** Gets the previous frame's camera position adjustment vector. */
		const float4& GetCameraPreviousPosAdjust() const { return data.CameraPreviousPosAdjust; }
		/** Gets the frame parameters (timer, frame count, etc.). */
		const float4& GetFrameParams() const { return data.FrameParams; }
		/** Gets the first set of dynamic resolution parameters. */
		const float4& GetDynamicResolutionParams1() const { return data.DynamicResolutionParams1; }
		/** Gets the second set of dynamic resolution parameters. */
		const float4& GetDynamicResolutionParams2() const { return data.DynamicResolutionParams2; }
	};

	namespace game
	{
		extern RE::BSGraphics::RendererShadowState* shadowState;
		extern RE::BSGraphics::State* graphicsState;
		extern RE::BSGraphics::Renderer* renderer;
		extern RE::BSShaderManager::State* smState;
		extern RE::TES* tes;
		extern RE::MemoryManager* memoryManager;
		extern RE::INISettingCollection* iniSettingCollection;
		extern RE::INIPrefSettingCollection* iniPrefSettingCollection;
		extern RE::GameSettingCollection* gameSettingCollection;
		extern float* cameraNear;
		extern float* cameraFar;
		extern float* deltaTime;
		extern RE::BSUtilityShader* utilityShader;
		extern RE::PlayerCharacter* player;
		extern RE::Sky* sky;
		extern RE::UI* ui;
		extern RE::Calendar* calendar;
		extern RE::ImageSpaceManager* imageSpaceManager;
		extern bool* bEnableVolumetricLighting;
		extern std::atomic<bool> quitGame;

		extern RE::BSGraphics::PixelShader** currentPixelShader;
		extern RE::BSGraphics::VertexShader** currentVertexShader;
		extern REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags;

		extern RE::Setting* bEnableLandFade;
		extern RE::Setting* bShadowsOnGrass;
		extern RE::Setting* shadowMaskQuarter;
		extern REL::Relocation<ID3D11Buffer**> perFrame;
		extern REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		extern D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer;
		extern FrameBufferCache frameBufferCached;
	}

	namespace rtti
	{
		extern REL::Relocation<const RE::NiRTTI*> NiIntegerExtraDataRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSEffectShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> BSWaterShaderPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiParticleSystemRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiBillboardNodeRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiAlphaPropertyRTTI;
		extern REL::Relocation<const RE::NiRTTI*> NiSourceTextureRTTI;
	}

	extern State* state;
	extern Deferred* deferred;
	extern Menu* menu;
	extern SIE::ShaderCache* shaderCache;
	extern Profiler* profiler;
	extern WeatherManager* weatherManager;
	extern SceneSettingsManager* sceneSettingsManager;

	/** @brief Initializes core singletons (ShaderCache, State, Menu, Deferred). Called once at plugin load. */
	void OnInit();
	/** @brief Resolves runtime game pointers, RTTI relocations, and D3D device references. Called when the renderer is ready. */
	void ReInit();
	/** @brief Caches late-binding game singletons (player, sky, INI settings) after Skyrim's data files are loaded. */
	void OnDataLoaded();
	/** @brief Signals shader compilation to stop when the game window is closing. */
	void OnGameWindowClose();
	/**
	 * @brief Installs Detours hooks on the device context's Map/Unmap vtable slots to capture per-frame constant buffer data.
	 * @param a_context The D3D11 device context to hook.
	 */
	void InstallD3DHooks(ID3D11DeviceContext* a_context);
}
