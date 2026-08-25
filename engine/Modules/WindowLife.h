#pragma once

#include "Buffer.h"
#include "I18n/I18n.h"
#include "RenderModule.h"

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * @brief Procedural inhabited-window and architectural-glass renderer.
 *
 * Phase 2A carries per-draw architectural-glass/window data through a tiny
 * structured-buffer SRV rather than a constant-buffer register. Lighting's
 * b0-b13 bank is already saturated in the live PIXL tree, so this avoids all
 * cbuffer collisions without changing FeatureData b6.
 */
struct WindowLife : RenderModule
{
    virtual inline std::string GetName() override { return "Window Life"; }
    virtual inline std::string GetDisplayName() override { return T("feature.window_life.name", "Window Life"); }
    virtual inline std::string GetShortName() override { return "WindowLife"; }
    virtual inline std::string_view GetShaderDefineName() override { return "PIXL_WINDOW_LIFE"; }
    virtual std::string_view GetCategory() const override { return ModuleGroups::kMaterials; }

    virtual std::pair<std::string, std::vector<std::string>> GetModuleSummary() override
    {
        return {
            T("feature.window_life.description", "Upgrades architectural glass and adds subtle procedural life on both sides of suitable inhabited windows."),
            {
                T("feature.window_life.key_feature_1", "Old-glass Fresnel response, stable roughness/grime variation and restrained optical waviness"),
                T("feature.window_life.key_feature_2", "Window classes keep silhouettes out of tiny, roof and awning panes"),
                T("feature.window_life.key_feature_3", "Mask-fitted, asynchronous occupants with varied articulated motion and depth parallax"),
                T("feature.window_life.key_feature_4", "Window glass selectively overrides synthetic Auto-POM while retaining relief on frames and surrounding architecture")
            }
        };
    }

    virtual inline bool HasShaderDefine(RE::BSShader::Type t) override
    {
        return t == RE::BSShader::Type::Lighting;
    }

    virtual bool AffectsCachedShader(RE::BSShader::Type t, std::uint32_t descriptor, CachedShaderStage stage) override
    {
        if (t != RE::BSShader::Type::Lighting || stage != CachedShaderStage::Pixel)
            return false;

        const auto technique = (descriptor >> 24u) & 0x3Fu;
        const bool staticTechnique =
            technique == 0u || technique == 1u || technique == 2u ||
            technique == 3u || technique == 7u || technique == 11u;
        const bool skinned = (descriptor & (1u << 1u)) != 0u;
        const bool worldMap = (descriptor & (1u << 18u)) != 0u;

        // Mirrors PIXL_WINDOW_LIFE_ACTIVE in Lighting.hlsl: only static/object
        // pixel permutations can bind or execute the architectural-window path.
        return staticTechnique && !skinned && !worldMap;
    }

    struct Settings
    {
        bool EnableWindowLife = true;
        bool EnableArchitecturalGlass = true;

        // Occupant transmission/occlusion. Day remains subtle; night is readable.
        float DayShadowStrength = 0.12f;
        float NightShadowStrength = 0.58f;
        float DayActivity = 0.32f;
        float EveningActivity = 0.62f;
        float LateNightActivity = 0.18f;

        // Interior optical illusion.
        float ParallaxDepth = 28.0f;
        float Refraction = 2.5f;
        float SilhouetteSoftness = 0.055f;
        float HumanScale = 0.95f;
        float CurtainStrength = 0.22f;
        float RoomDepthStrength = 0.14f;
        bool EnableAuthoredRooms = true;
        float AuthoredRoomStrength = 0.78f;
        bool UseAuthoredMaskLayout = true;
        bool EnableInteriorPassers = true;

        // Distance LOD and pane discrimination.
        float DistanceFadeStart = 2400.0f;
        float DistanceFadeEnd = 7600.0f;
        float PaneThreshold = 0.16f;
        float PaneSoftness = 0.20f;

        // Stable procedural room grid and event cadence.
        // Owner-validated medium-window baseline. The shader derives small,
        // large, and grand aperture families from geometry bounds around it.
        float RoomWidth = 110.0f;
        float RoomHeight = 140.0f;
        float MotionSpeed = 1.0f;

        // Phase 2A architectural glass.
        float GlassStrength = 0.86f;
        float GlassReflectionBoost = 0.62f;
        float GlassRoughness = 0.30f;
        float GlassTransmission = 0.93f;
        float GlassDirtStrength = 0.42f;
        float GlassDistortion = 0.034f;
        float GlassNormalRetention = 0.26f;
        bool SuppressWindowAutoPOM = true;

        // First-stage physical eligibility guard. Geometry radius catches dedicated
        // tiny window meshes; shader normal orientation catches roof/awning panes.
        float MinShallowWindowRadius = 22.0f;
        float MinFullWindowRadius = 42.0f;
        float FullWindowVerticality = 0.62f;

        bool DebugWindowDetection = false;
    } settings;

    struct PerGeometryData
    {
        // c0: x enabled, y salt, z current shadow strength, w activity probability
        float4 Runtime0{};
        // c1: x parallax depth, y silhouette softness, z refraction units, w human scale
        float4 Optics0{};
        // c2: x fade start, y fade end, z pane threshold, w pane softness
        float4 Surface0{};
        // c3: x debug overlay, y room width, z room height, w motion speed
        float4 Runtime1{};
        // c4: x glass strength, y reflection boost, z target roughness, w transmission
        float4 Glass0{};
        // c5: x dirt, y distortion, z normal retention, w suppress Auto-POM
        float4 Glass1{};
        // c6: x material tier [1..3], y named glass token, z pane-source flags (1 game glow, 2 external authored mask), w explicit window token
        float4 Class0{};
        // c7: x min shallow radius, y min full radius, z full-window verticality, w architectural glass enabled
        float4 Eligibility0{};
        // c8: x curtain shadow, y recessed-room shadow, z authored-mask layout, w interior passers
        float4 Interior0{};
        // c9: xyz absolute geometry-bound centre, w geometry radius
        float4 Geometry0{};
        // c10: x regional room family, y atlas ready/enabled, z authored-room blend, w tile count
        float4 Asset0{};
    };
    static_assert(sizeof(PerGeometryData) == 176, "WindowLife per-draw payload must be exactly 176 bytes.");
    static_assert(sizeof(PerGeometryData) % 16 == 0, "WindowLife constant-buffer payload must remain 16-byte sized.");

    virtual void DrawSettings() override;
    virtual void LoadSettings(json& o_json) override;
    virtual void SaveSettings(json& o_json) override;
    virtual void RestoreDefaultSettings() override;
    virtual void SetupResources() override;
    virtual void PostPostLoad() override;

    void BSLightingShader_SetupGeometry(RE::BSRenderPass* a_pass);

private:
    struct Classification
    {
        bool isWindow = false;
        bool hasGlowTexture = false;
        bool hasAuthoredMask = false;
        bool explicitWindow = false;
        bool namedGlass = false;
        int materialTier = 0;  // 1 glass only, 2 shallow interior, 3 full candidate
        int roomFamily = 0;    // 0 Nordic, 1 noble, 2 Riften, 3 Windhelm, 4 Dwemer, 5 trade
        int score = 0;
        std::string authoredMaskKey;
        std::string evidence;
    };

    Classification ClassifyMaterial(const RE::BSLightingShaderMaterialBase* a_material);
    const Classification& GetClassification(const RE::BSLightingShaderMaterialBase* a_material);
    void RefreshFrameBaseData();
    void UpdateAndBindActive(const Classification& a_classification, const RE::BSGeometry* a_geometry);
    void UploadData(ID3D11Buffer* a_buffer, const PerGeometryData& a_data) const;
    void BindNeutral() const;
    void BindActive(const Classification& a_classification) const;
    ID3D11ShaderResourceView* GetAuthoredMaskSRV(const Classification& a_classification) const;
    static float GetDayNightBlend(float a_hour);
    static float GetActivityForHour(float a_hour, const Settings& a_settings);

    // Keep WindowLife's five private resources contiguous at the top of the
    // D3D11 pixel-SRV range. The live binding audit reserves t123..t127 for this
    // module; no existing PIXL or game resource occupies these slots.
    static constexpr UINT kOccupantAtlasSRVSlot = 123;
    static constexpr UINT kCurtainAtlasSRVSlot = 124;
    static constexpr UINT kAuthoredMaskSRVSlot = 125;
    static constexpr UINT kRoomAtlasSRVSlot = 126;
    static constexpr UINT kPerDrawSRVSlot = 127;

    winrt::com_ptr<ID3D11Buffer> activeBuffer;
    winrt::com_ptr<ID3D11Buffer> neutralBuffer;
    winrt::com_ptr<ID3D11ShaderResourceView> activeSRV;
    winrt::com_ptr<ID3D11ShaderResourceView> neutralSRV;
    winrt::com_ptr<ID3D11ShaderResourceView> occupantAtlasSRV;
    winrt::com_ptr<ID3D11ShaderResourceView> curtainAtlasSRV;
    winrt::com_ptr<ID3D11ShaderResourceView> roomAtlasSRV;
    PerGeometryData frameBaseData{};
    PerGeometryData currentActiveData{};
    bool activeDataValid = false;
    std::uint32_t activeDataFrame = ~0u;

    std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> authoredMaskSRVs;
    std::unordered_map<std::uintptr_t, Classification> classificationCache;

    struct Hooks
    {
        struct BSLightingShader_SetupGeometry
        {
            static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags);
            static inline REL::Relocation<decltype(thunk)> func;
        };

        static void Install()
        {
            stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
            logger::info(
                "[WindowLife] Installed BSLightingShader geometry hook on PS t{} occupant atlas + t{} curtain atlas + t{} authored pane mask + t{} room atlas + t{} structured SRV.",
                kOccupantAtlasSRVSlot,
                kCurtainAtlasSRVSlot,
                kAuthoredMaskSRVSlot,
                kRoomAtlasSRVSlot,
                kPerDrawSRVSlot);
        }
    };
};
