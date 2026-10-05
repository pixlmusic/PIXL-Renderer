// PIXL Renderer - contained-liquid runtime interface and GPU ABI.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#pragma once
#include "RenderModule.h"
#include <array>

// Renderer-only contained-liquid optics for supported potion and beverage bottles.
// No engine object, gameplay record, inventory state, or save data is modified.
struct ContainedLiquids : RenderModule
{
    static constexpr std::size_t kProfileSamples = 16;
    std::string GetName() override { return "Contained Liquids"; }
    std::string GetShortName() override { return "ContainedLiquids"; }
    std::string_view GetCategory() const override { return ModuleGroups::kMaterials; }
    std::string_view GetShaderDefineName() override { return "PIXL_CONTAINED_LIQUIDS"; }
    bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; }
    bool AffectsCachedShader(RE::BSShader::Type t, std::uint32_t, CachedShaderStage s) override
    { return HasShaderDefine(t) && s == CachedShaderStage::Pixel; }
    std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
    { return {"Experimental contained-liquid optics for potions and verified clear-container beverages.", {"Effect-aware potion colours", "Clear-glass alcohol profiles", "Gravity-relative fill", "Inertial slosh"}}; }
    struct Settings {
        bool Enabled = true;
        float Fill = 0.70f, SloshStrength = 1.40f, Damping = 0.25f;
        float Absorption = 1.0f, Refraction = 1.0f;
        float BubbleStrength = 0.20f, InternalReflection = 1.0f;
        float SubsurfaceScattering = 0.55f;
        float Emission = 2.40f;          // Magical potion emission.
        float OrdinaryEmission = 1.20f;  // Alcohol/ordinary liquid art control.
        float LabelPreservation = 0.90f;
        bool Freeze = false;
        int Debug = 0;
    } settings;
    void SetupResources() override;
    void Reset() override;
    void PostPostLoad() override;
    void DrawSettings() override;
    void LoadSettings(json&) override;
    void SaveSettings(json&) override;
    void RestoreDefaultSettings() override { settings = {}; ClearHistory(); }
    void SetupGeometry(RE::BSRenderPass*);
    void ReplayAfterDeferred();
    void MarkReconstructionReactive(ID3D11UnorderedAccessView*,std::uint32_t,std::uint32_t);
private:
    void ClearHistory();
    // PS t120/t121 are private to the Lighting path and rebound on every draw.
    struct GPUData {
        float4 center{}, axisX{}, axisY{}, axisZ{}, plane{}, optics{}, crop{}, dynamics{};
        float4 profile0{}, profile1{}, profile2{}, profile3{}, detail{}, appearance{};
        float4 opticalColor{}, liquidColor{};
    };
    static_assert(sizeof(GPUData) == 256);
    struct ObjectState {
        std::uintptr_t key{};
        std::uint32_t reference{}, frame{};
        float time{}, animationTime{}, bubbleTime{}, agitation{};
        RE::NiPoint3 position{}, velocity{}, angularVelocity{};
        std::array<RE::NiPoint3,3> basis{};
        float tilt[2]{}, speed[2]{};
        std::array<float,kProfileSamples> radialProfile{};
        std::uint64_t profileKey{};
        float profileMinZ{}, profileMaxZ{}, profileRadius{};
        bool profileValid{};
        bool sleeping = true;
        float sleepTime{};
    };
    struct ProfileCacheEntry {
        std::uint64_t key{};
        std::array<float,kProfileSamples> radialProfile{};
        float profileMinZ{},profileMaxZ{},profileRadius{};
        std::uint32_t lastUse{};
        bool attempted{},valid{};
    };
    std::array<ObjectState, 32> objects{};
    std::array<ProfileCacheEntry,64> profileCache{};
    std::uint32_t profileCacheClock{};
    winrt::com_ptr<ID3D11Buffer> buffer;
    winrt::com_ptr<ID3D11ShaderResourceView> dataSRV, sceneSRV;
    winrt::com_ptr<ID3D11Texture2D> scene;
    DXGI_FORMAT sceneFormat = DXGI_FORMAT_UNKNOWN;
    std::uint32_t sceneCaptureFrame = ~0u;
    std::uintptr_t sceneCaptureSource{};
    std::uint32_t sceneCaptureLeft{},sceneCaptureTop{},sceneCaptureWidth{},sceneCaptureHeight{};
    struct ReactiveRegion {
        std::uint32_t left{},top{},right{},bottom{},sourceWidth{},sourceHeight{};
    };
    std::array<ReactiveRegion,32> reactiveRegions{};
    std::size_t reactiveRegionCount{};
    std::uint32_t reactiveRegionFrame=~0u;
    ReactiveRegion pendingReactiveRegion{};
    std::uint32_t lastFrame = ~0u, contextID{}, matches{};
    bool matchedLogged{}, failureLogged{}, captureFailureLogged{}, replayLogged{};
    bool replaying=false;
    RE::BSRenderPass* preparedPass=nullptr;
    static void ReplayQueuedDraw(void*,RE::BSRenderPass*,std::uint32_t,bool,std::uint32_t);
    bool CaptureScene(GPUData&);
    struct Hook {
        static void thunk(RE::BSShader*, RE::BSRenderPass*, std::uint32_t);
        static inline REL::Relocation<decltype(thunk)> func;
    };
    struct DrawHook {
        static void thunk(RE::BSRenderPass*,std::uint32_t,bool,std::uint32_t);
        static inline REL::Relocation<decltype(thunk)> func;
    };
};
