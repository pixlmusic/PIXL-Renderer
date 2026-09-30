// PIXL Renderer - contained-liquid classification, fitting and rendering.
// Copyright (C) 2026 PIXL Studio
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional permissions are described in the repository EXCEPTIONS.md.

#include "ContainedLiquids.h"
#include "ContainedLiquidMath.h"
#include "Globals.h"
#include "State.h"
#include "Util.h"
#include "Deferred.h"
#include <d3d11_1.h>
#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <vector>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ContainedLiquids::Settings,
    Enabled, Fill, SloshStrength, Damping, Absorption, Refraction, BubbleStrength, InternalReflection,
    SubsurfaceScattering, Emission, OrdinaryEmission, LabelPreservation);

namespace {
    enum class LiquidFamily : std::uint32_t { Health, Magicka, Stamina, Poison, Magic, RedWine, PaleAlcohol, Ordinary };
    struct LiquidStyle {
        float3 absorption;
        float3 tint;
        LiquidFamily family;
        bool magical;
        float bubbleScale;
        float viscosity;
        float liquidIOR;
    };

    struct ClassificationCacheEntry {
        RE::FormID formID{};
        std::uint64_t modelHash{};
        std::uint32_t lastUse{};
        LiquidStyle style{};
        bool valid{};
    };
    std::array<ClassificationCacheEntry,128> classificationCache{};
    std::uint32_t classificationCacheClock{};

    std::uint64_t HashText(std::uint64_t hash,const char* text)
    {
        constexpr std::uint64_t prime=1099511628211ull;
        if (!text) return (hash^0xFFu)*prime;
        while (*text) {
            hash^=static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*text++)));
            hash*=prime;
        }
        return hash;
    }

    bool ContainsNoCase(const char* text, std::string_view needle)
    {
        if (!text || needle.empty()) return false;
        const std::string_view value{text};
        return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
            [](char lhs, char rhs) { return std::tolower(static_cast<unsigned char>(lhs)) ==
                std::tolower(static_cast<unsigned char>(rhs)); }) != value.end();
    }

    bool HasAny(const char* value, std::initializer_list<std::string_view> needles)
    {
        return std::ranges::any_of(needles, [value](auto needle) { return ContainsNoCase(value, needle); });
    }

    bool IsSupportedContainerModel(const char* model)
    {
        return model && (ContainsNoCase(model, "\\potions\\") ||
            HasAny(model, { "potion", "bottle", "wine", "mead", "ale", "beer", "brandy", "rum",
                "skooma", "sujamma", "flin", "shein", "matze", "waterflask" }));
    }

    bool IsAlcoholModel(const char* model, const char* itemName)
    {
        return HasAny(model, { "wine", "mead", "ale", "beer", "brandy", "rum", "sujamma", "flin", "shein", "matze" }) ||
            HasAny(itemName, { "wine", "mead", "ale", "beer", "brandy", "rum", "sujamma", "flin", "shein", "matze" });
    }

    bool IsLikelyLiquidGeometry(const char* geometry)
    {
        if (!geometry || !*geometry) return false;
        if (HasAny(geometry, { "cork", "stopper", "cap", "label", "tag", "rope" })) return false;
        // The draw hook runs before expensive owner/model classification, so this
        // positive gate prevents unrelated opaque geometry from consuming the
        // fixed 32-pass replay budget.
        return HasAny(geometry, { "potion", "bottle", "wine", "mead", "ale", "beer", "skooma", "liquid" });
    }

    float ActorValueScore(const RE::AlchemyItem* item, RE::ActorValue actorValue)
    {
        if (!item) return 0.0f;
        float score=0.0f;
        for (const auto* effect : item->effects) {
            if (!effect || !effect->baseEffect) continue;
            const auto& data = effect->baseEffect->data;
            if (data.primaryAV != actorValue && data.secondaryAV != actorValue) continue;
            const float magnitude=std::max(std::abs(effect->effectItem.magnitude),1.0f);
            const float duration=std::max(static_cast<float>(effect->effectItem.duration),1.0f);
            const float areaWeight=effect->effectItem.area>0?1.1f:1.0f;
            score=std::max(score,magnitude*std::sqrt(duration)*areaWeight);
        }
        return score;
    }

    LiquidStyle ClassifyLiquidUncached(const RE::AlchemyItem* item, const char* model)
    {
        const char* name = item ? item->GetFullName() : nullptr;
        if (item && !item->IsFood()) {
			// Poison is an item-level semantic and must win over restorative actor
			// values that may appear in multi-effect or modded poison records.
            if (item->IsPoison())
                return {{0.42f,0.055f,0.62f},{0.08f,0.30f,0.025f},LiquidFamily::Poison,true,0.55f,1.35f,1.39f};

			// Classify real potion families from the strongest meaningful effect.
			// Names and model paths are intentionally only fallback evidence.
            const float health=ActorValueScore(item,RE::ActorValue::kHealth);
            const float magicka=ActorValueScore(item,RE::ActorValue::kMagicka);
            const float stamina=ActorValueScore(item,RE::ActorValue::kStamina);
            const float dominant=std::max({health,magicka,stamina});
            if (dominant>0.0f) {
                if (health>=magicka && health>=stamina)
                    return {{0.055f,0.55f,0.82f},{0.34f,0.024f,0.012f},LiquidFamily::Health,true,0.70f,1.05f,1.36f};
                if (magicka>=stamina)
                    return {{0.72f,0.18f,0.045f},{0.018f,0.10f,0.48f},LiquidFamily::Magicka,true,0.85f,0.82f,1.37f};
                return {{0.58f,0.07f,0.52f},{0.018f,0.32f,0.055f},LiquidFamily::Stamina,true,0.80f,0.92f,1.35f};
            }
            if (ContainsNoCase(model,"poison") || ContainsNoCase(name,"poison"))
                return {{0.42f,0.055f,0.62f},{0.08f,0.30f,0.025f},LiquidFamily::Poison,true,0.55f,1.35f,1.39f};
            if (HasAny(model,{"magicka","mana"}) || HasAny(name,{"magicka","mana"}))
                return {{0.72f,0.18f,0.045f},{0.018f,0.10f,0.48f},LiquidFamily::Magicka,true,0.85f,0.82f,1.37f};
            if (HasAny(model,{"stamina","vigor"}) || HasAny(name,{"stamina","vigor"}))
                return {{0.58f,0.07f,0.52f},{0.018f,0.32f,0.055f},LiquidFamily::Stamina,true,0.80f,0.92f,1.35f};
            if (ContainsNoCase(model,"health") || ContainsNoCase(name,"health"))
                return {{0.055f,0.55f,0.82f},{0.34f,0.024f,0.012f},LiquidFamily::Health,true,0.70f,1.05f,1.36f};
            return {{0.30f,0.10f,0.36f},{0.16f,0.045f,0.24f},LiquidFamily::Magic,true,0.65f,1.0f,1.38f};
        }

        if (HasAny(model, { "redwine", "winebottle01", "winebottle02" }) || HasAny(name, { "red wine", "alto wine" }))
            return {{0.08f,0.68f,0.82f},{0.22f,0.018f,0.025f},LiquidFamily::RedWine,false,0.28f,1.22f,1.36f};
        if (HasAny(model, { "wine", "mead", "ale", "beer", "brandy", "rum", "sujamma", "flin", "shein", "matze" }) ||
            HasAny(name, { "wine", "mead", "ale", "beer", "brandy", "rum", "sujamma", "flin", "shein", "matze" }))
            return {{0.08f,0.24f,0.72f},{0.26f,0.105f,0.018f},LiquidFamily::PaleAlcohol,false,0.38f,1.12f,1.36f};
        return {{0.08f,0.055f,0.035f},{0.025f,0.045f,0.055f},LiquidFamily::Ordinary,false,0.12f,1.0f,1.333f};
    }

    LiquidStyle ClassifyLiquid(const RE::AlchemyItem* item,const char* model)
    {
        const auto formID=item?item->GetFormID():0;
        const auto modelHash=HashText(1469598103934665603ull,model);
        ClassificationCacheEntry* replacement=&classificationCache[0];
        for (auto& entry:classificationCache) {
            if (entry.valid && entry.formID==formID && entry.modelHash==modelHash) {
                entry.lastUse=++classificationCacheClock;
                return entry.style;
            }
            if (!entry.valid || entry.lastUse<replacement->lastUse) replacement=&entry;
        }
        replacement->formID=formID;
        replacement->modelHash=modelHash;
        replacement->lastUse=++classificationCacheClock;
        replacement->style=ClassifyLiquidUncached(item,model);
        replacement->valid=true;
        return replacement->style;
    }
    float Safe(float x, float lo, float hi, float fallback)
    { return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback; }
    bool Finite(RE::NiPoint3 p)
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
    float Length(RE::NiPoint3 p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); }

    bool BuildRadialProfile(RE::BSGeometry* geometry,
        std::array<float,ContainedLiquids::kProfileSamples>& profile,
        float& minZ, float& maxZ, float& maxRadius)
    {
        auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
        auto* rendererData = geometry ? geometry->GetGeometryRuntimeData().rendererData : nullptr;
        if (!triShape || !rendererData || !rendererData->rawVertexData ||
            !rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_VERTEX)) return false;
        const auto stride = rendererData->vertexDesc.GetSize();
        const auto count = static_cast<std::uint32_t>(triShape->GetTrishapeRuntimeData().vertexCount);
        if (stride < sizeof(float) * 3 || count < 8 || count > 65535) return false;

        std::vector<float> heights;
        heights.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto* position = reinterpret_cast<const float*>(rendererData->rawVertexData + std::size_t(index) * stride);
            if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return false;
            heights.push_back(position[2]);
        }
        const auto percentile=[&](float fraction) {
            const auto index=std::min(static_cast<std::size_t>(fraction*(heights.size()-1)),heights.size()-1);
            std::nth_element(heights.begin(),heights.begin()+index,heights.end());
            return heights[index];
        };
        const float meshMinZ=percentile(0.01f);
        const float meshMaxZ=percentile(0.99f);
        if (!(meshMaxZ > meshMinZ + 0.1f) || meshMaxZ - meshMinZ > 128.0f) return false;

        // Bottle families have different neck heights. Fit only the lower 70%
        // of the live mesh and leave a glass floor; neck/cork geometry therefore
        // retains its authored material even for modded containers.
        const float span = meshMaxZ - meshMinZ;
        minZ = meshMinZ + span * 0.03f;
        maxZ = meshMinZ + span * 0.74f;
        if (!(maxZ > minZ + 0.5f)) return false;

        std::array<std::vector<float>,ContainedLiquids::kProfileSamples> radialSamples;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto* position = reinterpret_cast<const float*>(rendererData->rawVertexData + std::size_t(index) * stride);
            if (position[2] < minZ - 0.4f || position[2] > maxZ + 0.4f) continue;
            const float normalized = std::clamp((position[2] - minZ) / (maxZ - minZ), 0.0f, 1.0f);
            const auto slice = static_cast<std::size_t>(std::clamp(std::lround(normalized *
                static_cast<float>(ContainedLiquids::kProfileSamples-1)),0l,
                static_cast<long>(ContainedLiquids::kProfileSamples-1)));
            const float radial = std::hypot(position[0], position[1]);
            if (std::isfinite(radial) && radial < 64.0f) radialSamples[slice].push_back(radial);
        }
        std::array<float,ContainedLiquids::kProfileSamples> outer{};
        std::array<bool,ContainedLiquids::kProfileSamples> populated{};
        for (std::size_t i=0;i<outer.size();++i) {
            auto& samples=radialSamples[i];
            if (samples.empty()) continue;
            const auto robustIndex=std::min(static_cast<std::size_t>(samples.size()*0.85f),samples.size()-1);
            std::nth_element(samples.begin(),samples.begin()+robustIndex,samples.end());
            outer[i]=samples[robustIndex];
            populated[i]=true;
        }
        for (std::size_t i = 0; i < outer.size(); ++i) {
            if (populated[i]) continue;
            std::size_t lo = i, hi = i;
            while (lo > 0 && !populated[lo]) --lo;
            while (hi + 1 < outer.size() && !populated[hi]) ++hi;
            if (!populated[lo] && !populated[hi]) return false;
            outer[i] = populated[lo] && populated[hi] ? std::lerp(outer[lo],outer[hi],
                static_cast<float>(i-lo)/std::max(static_cast<float>(hi-lo),1.0f)) :
                (populated[lo] ? outer[lo] : outer[hi]);
        }
        maxRadius = *std::max_element(outer.begin(), outer.end());
        if (!(maxRadius > 0.5f) || maxRadius > 32.0f) return false;
        for (std::size_t i = 0; i < profile.size(); ++i) {
            const float smoothed=i>0&&i+1<outer.size()?
                (outer[i-1]+outer[i]*2.0f+outer[i+1])*0.25f:outer[i];
            // The bottle mesh still supplies the final raster silhouette. Keeping
            // the proxy close to the fitted inner wall avoids grazing-angle holes
            // without allowing liquid to escape beyond the authored geometry.
            profile[i] = std::clamp(smoothed / maxRadius * 0.89f, 0.08f, 0.96f);
        }
        // Keep a real glass floor and soften the shoulder rather than drawing a
        // circular proxy through the original bottle wall.
        profile.front() *= 0.72f;
        profile.back() *= 0.72f;
        return true;
    }

    RE::NiPoint3 AngularVelocity(const std::array<RE::NiPoint3,3>& previous,
        const RE::NiPoint3 (&current)[3],float dt)
    {
        if (!(dt>1.0e-5f)) return {};
        RE::NiPoint3 sineAxis{};
        float dotSum=0.0f;
        for (int i=0;i<3;++i) {
            sineAxis+=previous[i].Cross(current[i]);
            dotSum+=previous[i].Dot(current[i]);
        }
        sineAxis=sineAxis*0.5f;
        const float sine=Length(sineAxis);
        const float cosine=std::clamp((dotSum-1.0f)*0.5f,-1.0f,1.0f);
        if (sine<1.0e-6f) return {};
        const float angle=std::atan2(sine,cosine);
        return sineAxis*(angle/(sine*dt));
    }
}

void ContainedLiquids::LoadSettings(json& j)
{
    settings = j;
    settings.Fill=Safe(settings.Fill,0.02f,0.98f,0.75f);
    settings.SloshStrength=Safe(settings.SloshStrength,0,2,1);
    settings.Damping=Safe(settings.Damping,0.25f,0.95f,0.55f);
    settings.Absorption=Safe(settings.Absorption,0,3,1);
    settings.Refraction=Safe(settings.Refraction,0,2,0.65f);
    settings.BubbleStrength=Safe(settings.BubbleStrength,0,1,0.22f);
    settings.InternalReflection=Safe(settings.InternalReflection,0,1.5f,0.45f);
    settings.SubsurfaceScattering=Safe(settings.SubsurfaceScattering,0,2,0.55f);
    settings.Emission=Safe(settings.Emission,0,3,0.18f);
    settings.OrdinaryEmission=Safe(settings.OrdinaryEmission,0,3,0.0f);
    settings.LabelPreservation=Safe(settings.LabelPreservation,0,1,0.90f);
    settings.Debug=0; settings.Freeze=false;
    ClearHistory();
}
void ContainedLiquids::SaveSettings(json& j) { j=settings; }
void ContainedLiquids::ClearHistory()
{
    objects={};lastFrame=~0u;matches=0;pendingCount=0;preparedPass=nullptr;
    sceneCaptureFrame=~0u;sceneCaptureSource=0;sceneCaptureWidth=sceneCaptureHeight=0;
}
void ContainedLiquids::Reset()
{
    // RenderModule::Reset is called EVERY present, despite its old API comment.
    // Preserve spring state across ordinary frames; discard only frame-owned passes.
    pendingCount=0;preparedPass=nullptr;matches=0;
    if (!settings.Enabled || (globals::game::ui && (globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME)))) ClearHistory();
}
void ContainedLiquids::DrawSettings()
{
    ImGui::TextWrapped("Experimental. Supports live world potions and clear beverage bottles whose mesh can be fitted safely. Opaque, wicker and wrapped containers retain their authored appearance. Magicka is blue, stamina green, health red; alcohol is non-emissive by default.");
    if (ImGui::Checkbox("Enable contained liquids", &settings.Enabled)) ClearHistory();
    ImGui::BeginDisabled(!settings.Enabled);
    ImGui::SliderFloat("Fill level", &settings.Fill,0.02f,0.98f,"%.2f");
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Sets the fraction of the internal proxy volume filled with liquid.");
    ImGui::SliderFloat("Slosh strength", &settings.SloshStrength,0,2);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Controls the liquid's lag and settling after bottle movement or rotation.");
    ImGui::SliderFloat("Slosh damping", &settings.Damping,0.25f,0.95f);
    ImGui::SliderFloat("Absorption strength", &settings.Absorption,0,3);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Controls how rapidly light is absorbed along a ray through the liquid.");
    ImGui::SliderFloat("Refraction strength", &settings.Refraction,0,2);
    ImGui::SliderFloat("Bubbles", &settings.BubbleStrength,0,1);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Adds restrained, stable bubbles inside the liquid. Higher values add shader work only to matched bottles.");
    ImGui::SliderFloat("Internal reflection", &settings.InternalReflection,0,1.5f);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Controls reflected light at the liquid and inner-glass boundaries.");
    ImGui::SliderFloat("Subsurface scattering", &settings.SubsurfaceScattering,0,2);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Lets back-lit liquid carry warm light through its depth. This affects the liquid volume, not the bottle label.");
    ImGui::SliderFloat("Magic emission", &settings.Emission,0,3);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Controls glow for magical potions. Their effect-derived colour also controls the emitted tint.");
    ImGui::SliderFloat("Alcohol / ordinary emission", &settings.OrdinaryEmission,0,3);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Defaults to zero. Raise deliberately to fake restrained bounced light from wine, alcohol, water, or other non-magical liquids.");
    ImGui::SliderFloat("Label preservation", &settings.LabelPreservation,0,1);
    if (auto t=Util::HoverTooltipWrapper()) ImGui::TextUnformatted("Keeps opaque painted label and stopper detail in front of the liquid during the replay pass.");
    if (ImGui::TreeNode("Debug / contained liquids")) {
        ImGui::Combo("Visualisation", &settings.Debug,"Off\0Proxy volume\0Liquid surface\0Optical thickness\0Surface normal\0");
        ImGui::Checkbox("Freeze slosh", &settings.Freeze);
        ImGui::Text("Matched draws this frame: %u / fixed history capacity: 32", matches);
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
}
void ContainedLiquids::SetupResources()
{
    buffer=nullptr; dataSRV=nullptr; scene=nullptr; sceneSRV=nullptr;
    sceneFormat=DXGI_FORMAT_UNKNOWN;sceneCaptureFrame=~0u;sceneCaptureSource=0;ClearHistory();
    if (!globals::d3d::device) return;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth=sizeof(GPUData); d.Usage=D3D11_USAGE_DYNAMIC;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride=sizeof(GPUData);
    if (FAILED(globals::d3d::device->CreateBuffer(&d,nullptr,buffer.put())) ||
        FAILED(globals::d3d::device->CreateShaderResourceView(buffer.get(),nullptr,dataSRV.put()))) {
        buffer=nullptr; dataSRV=nullptr;
        logger::error("[ContainedLiquids] GPU data unavailable; vanilla fallback active");
    }
}

bool ContainedLiquids::CaptureScene(GPUData& data)
{
    auto* ctx=globals::d3d::context;
    ID3D11RenderTargetView* raw[8]{};
    ID3D11DepthStencilView* rawDepth=nullptr;
    ctx->OMGetRenderTargets(8,raw,&rawDepth);
    std::array<winrt::com_ptr<ID3D11RenderTargetView>,8> targets;
    for (size_t i=0;i<8;++i) targets[i].attach(raw[i]);
    winrt::com_ptr<ID3D11DepthStencilView> targetDepth;
    targetDepth.attach(rawDepth);
    if (!targets[0]) return false;
    if (globals::deferred->deferredPass) return false;
    winrt::com_ptr<ID3D11Resource> resource;
    targets[0]->GetResource(resource.put());
    auto source=resource.try_as<ID3D11Texture2D>();
    if (!source) return false;
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    D3D11_RENDER_TARGET_VIEW_DESC view{}; targets[0]->GetDesc(&view);
    if (desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice!=0 ||
        (desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
    D3D11_VIEWPORT vp{}; UINT count=1; ctx->RSGetViewports(&count,&vp);
    if (count!=1 || vp.Width<1 || vp.Height<1) return false;
    const auto matrix=globals::game::frameBufferCached.GetCameraViewProj().Transpose();
    const auto clip=DirectX::SimpleMath::Vector4::Transform(float4{data.center.x,data.center.y,data.center.z,1},matrix);
    if (!std::isfinite(clip.w) || clip.w<=0.01f) return false;
    const float px=vp.TopLeftX+(clip.x/clip.w*0.5f+0.5f)*vp.Width;
    const float py=vp.TopLeftY+(-clip.y/clip.w*0.5f+0.5f)*vp.Height;
    if (!std::isfinite(px)||!std::isfinite(py)||px<0||py<0||px>=desc.Width||py>=desc.Height) return false;
    constexpr UINT cropSize=512;
    // Conservative projected sphere bound, including homogeneous W variation.
    // Copy only the bottle rectangle, not a 512-square tile for every tiny potion.
    const float radius=std::max({data.axisX.w,data.axisY.w,data.axisZ.w});
    const float wGradient=std::sqrt(matrix._14*matrix._14+matrix._24*matrix._24+matrix._34*matrix._34);
    const float nearW=clip.w-radius*wGradient;
    if (nearW<=0.01f) return false;
    const float xGradient=std::sqrt(matrix._11*matrix._11+matrix._21*matrix._21+matrix._31*matrix._31);
    const float yGradient=std::sqrt(matrix._12*matrix._12+matrix._22*matrix._22+matrix._32*matrix._32);
    const float rx=radius*(xGradient+std::abs(clip.x/clip.w)*wGradient)/nearW*vp.Width*0.5f;
    const float ry=radius*(yGradient+std::abs(clip.y/clip.w)*wGradient)/nearW*vp.Height*0.5f;
    if (!std::isfinite(rx)||!std::isfinite(ry)||std::max(rx,ry)<1.5f||std::max(rx,ry)>246.0f) return false;
    const UINT width=std::min(static_cast<UINT>(std::ceil(rx*2+16)),desc.Width);
    const UINT height=std::min(static_cast<UINT>(std::ceil(ry*2+16)),desc.Height);
    if (std::max(rx,ry)<8.0f) {data.optics.y=0;data.dynamics.y=0;}
    const UINT requestedLeft=static_cast<UINT>(std::clamp(px-width*0.5f,0.0f,static_cast<float>(desc.Width-width)));
    const UINT requestedTop=static_cast<UINT>(std::clamp(py-height*0.5f,0.0f,static_cast<float>(desc.Height-height)));
    pendingReactiveRegion={requestedLeft,requestedTop,requestedLeft+width,requestedTop+height,
        desc.Width,desc.Height};
    const auto sourceID=reinterpret_cast<std::uintptr_t>(source.get());
    const auto frame=globals::state?globals::state->frameCount:~0u;
    const bool cachedTileContainsRequest=scene && sceneFormat==desc.Format &&
        sceneCaptureFrame==frame && sceneCaptureSource==sourceID &&
        requestedLeft>=sceneCaptureLeft && requestedTop>=sceneCaptureTop &&
        requestedLeft+width<=sceneCaptureLeft+sceneCaptureWidth &&
        requestedTop+height<=sceneCaptureTop+sceneCaptureHeight;
    if (cachedTileContainsRequest) {
        data.crop={static_cast<float>(sceneCaptureLeft),static_cast<float>(sceneCaptureTop),
            static_cast<float>(sceneCaptureWidth),static_cast<float>(sceneCaptureHeight)};
        return true;
    }
    if (!scene || sceneFormat!=desc.Format) {
        scene=nullptr; sceneSRV=nullptr;
        D3D11_TEXTURE2D_DESC copy{};
        copy.Width=cropSize; copy.Height=cropSize; copy.MipLevels=1; copy.ArraySize=1;
        copy.Format=desc.Format; copy.SampleDesc.Count=1; copy.Usage=D3D11_USAGE_DEFAULT;
        copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(globals::d3d::device->CreateTexture2D(&copy,nullptr,scene.put())) ||
            FAILED(globals::d3d::device->CreateShaderResourceView(scene.get(),nullptr,sceneSRV.put()))) return false;
        sceneFormat=desc.Format;
    }
    // Cache one generous frame-local tile. Nearby bottles reuse this immutable
    // pre-liquid scene region instead of copying the same render target again.
    const UINT tileWidth=std::min(cropSize,desc.Width);
    const UINT tileHeight=std::min(cropSize,desc.Height);
    const UINT left=static_cast<UINT>(std::clamp(px-tileWidth*0.5f,0.0f,
        static_cast<float>(desc.Width-tileWidth)));
    const UINT top=static_cast<UINT>(std::clamp(py-tileHeight*0.5f,0.0f,
        static_cast<float>(desc.Height-tileHeight)));
    D3D11_BOX box{left,top,0,left+tileWidth,top+tileHeight,1};
    // DX11 forbids copying a subresource while it remains bound for output.
    // Release the binding only for this copy, then restore the exact MRT/DSV set.
    ctx->OMSetRenderTargets(0,nullptr,nullptr);
    ctx->CopySubresourceRegion(scene.get(),0,0,0,0,source.get(),0,&box);
    ID3D11RenderTargetView* restore[8]{};
    for (size_t i=0;i<8;++i) restore[i]=targets[i].get();
    ctx->OMSetRenderTargets(8,restore,targetDepth.get());
    sceneCaptureFrame=frame;sceneCaptureSource=sourceID;
    sceneCaptureLeft=left;sceneCaptureTop=top;
    sceneCaptureWidth=tileWidth;sceneCaptureHeight=tileHeight;
    data.crop={static_cast<float>(left),static_cast<float>(top),
        static_cast<float>(tileWidth),static_cast<float>(tileHeight)};
    return true;
}

void ContainedLiquids::SetupGeometry(RE::BSRenderPass* pass)
{
    auto* ctx=globals::d3d::context;
    if (!ctx) return;
    if (replaying && preparedPass==pass && pass) {
        ID3D11ShaderResourceView* views[2]{dataSRV.get(),sceneSRV.get()};ctx->PSSetShaderResources(120,2,views);return;
    }
    ID3D11ShaderResourceView* empty[2]{}; ctx->PSSetShaderResources(120,2,empty);
    if (!settings.Enabled || !buffer || !dataSRV || !globals::state || !globals::state->inWorld ||
        globals::state->activeReflections || globals::deferred->deferredPass || globals::state->isMapMenuOpen || !pass || !pass->geometry || !pass->shaderProperty) return;
    const auto frame=globals::state->frameCount;
    auto* player=globals::game::player;
    auto* cell=player?player->GetParentCell():nullptr;
    if (!cell) return;
    if (contextID!=cell->GetFormID()) { ClearHistory(); contextID=cell->GetFormID(); }
    if (lastFrame!=frame) { matches=0; lastFrame=frame; }
    auto* geometry=pass->geometry;
    if (!IsLikelyLiquidGeometry(geometry->name.c_str())) return;
    RE::TESObjectREFR* owner=nullptr;
    auto* node=static_cast<RE::NiAVObject*>(geometry);
    for (int depth=0;node && depth<64;++depth,node=node->parent) {
        if ((owner=node->GetUserData())) break;
    }
    auto* potion=owner&&owner->GetBaseObject()?owner->GetBaseObject()->As<RE::AlchemyItem>():nullptr;
    const char* model=potion?potion->GetModel():nullptr;
    if (!potion || !IsSupportedContainerModel(model)) return;
    const auto style=ClassifyLiquid(potion,model);
    // Generic wine/mead meshes frequently combine glass with wicker, wood or an
    // opaque sleeve in one draw. Without authored submesh metadata there is no
    // stable way to put a procedural volume behind only the glass. Fail closed
    // for those containers; explicit clear/glass/crystal assets remain eligible.
    if (HasAny(model,{"wicker","basket","woven","wood","leather","wrapped","wrap"}) ||
        HasAny(geometry->name.c_str(),{"wicker","basket","woven","wood","leather","wrapped","wrap"})) return;
    // Alcohol meshes are often named only winebottle/meadbottle and do not
    // contain a "glass" token. Admit those known beverage families, while
    // retaining the clear/glass gate for otherwise generic ordinary liquids.
    if (!style.magical && !HasAny(model,{"clear","glass","crystal","waterflask"}) &&
        !IsAlcoholModel(model, potion->GetFullName())) return;
    auto* material=static_cast<RE::BSLightingShaderMaterialBase*>(pass->shaderProperty->material);
    if (!material || !material->textureSet) return;
    const auto& tr=geometry->world;
    if (!Finite(tr.translate)||!std::isfinite(tr.scale)||tr.scale<0.01f||tr.scale>20.0f) return;
    const float localBound=geometry->worldBound.radius/tr.scale;
    if (!std::isfinite(localBound) || localBound<0.5f || localBound>128.0f) return;
    RE::NiPoint3 axes[3];
    for (int i=0;i<3;++i) {
        axes[i]={tr.rotate.entry[0][i],tr.rotate.entry[1][i],tr.rotate.entry[2][i]};
        if (!Finite(axes[i])||std::abs(Length(axes[i])-1.0f)>0.02f) return;
    }
    const auto center=tr.translate;
    const auto& adjust=globals::game::frameBufferCached.GetCameraPosAdjust();
    const RE::NiPoint3 relative{center.x-adjust.x,center.y-adjust.y,center.z-adjust.z};
    const float distance=Length(relative);
    if (!std::isfinite(distance)||distance>1600.0f || distance<10.0f*tr.scale) return;
    auto key=reinterpret_cast<std::uintptr_t>(geometry);
    auto* state=&objects[0];
    for (auto& candidate:objects) {
        if (candidate.key==key && candidate.reference==owner->GetFormID()) {state=&candidate;break;}
        if (!candidate.key || candidate.time<state->time) state=&candidate;
    }
    const float now=globals::state->timer;
    float dt=now-state->time;
    if (state->key!=key || state->reference!=owner->GetFormID() || dt<0 || dt>0.25f || Length(tr.translate-state->position)>128) {
        *state={}; state->key=key; state->reference=owner->GetFormID();
        state->position=tr.translate;state->time=now;state->frame=~0u;dt=0;
        for (int i=0;i<3;++i) state->basis[i]=axes[i];
    }
    std::uint64_t profileKey=HashText(1469598103934665603ull,model);
    profileKey=HashText(profileKey,geometry->name.c_str());
    profileKey^=static_cast<std::uint64_t>(std::lround(localBound*1024.0f))*0x9E3779B185EBCA87ull;
    if (!state->profileValid || state->profileKey!=profileKey) {
        ProfileCacheEntry* cached=nullptr;
        ProfileCacheEntry* replacement=&profileCache[0];
        for (auto& entry:profileCache) {
            if (entry.attempted && entry.key==profileKey) {cached=&entry;break;}
            if (!entry.attempted || entry.lastUse<replacement->lastUse) replacement=&entry;
        }
        if (!cached) {
            cached=replacement;
            *cached={};cached->key=profileKey;cached->attempted=true;
            cached->valid=BuildRadialProfile(geometry,cached->radialProfile,
                cached->profileMinZ,cached->profileMaxZ,cached->profileRadius);
        }
        cached->lastUse=++profileCacheClock;
        // Ambiguous/non-readable geometry keeps Skyrim's original rendering.
        // A generic replacement profile can leak through labels or solid props.
        if (!cached->valid) return;
        state->radialProfile=cached->radialProfile;
        state->profileMinZ=cached->profileMinZ;
        state->profileMaxZ=cached->profileMaxZ;
        state->profileRadius=cached->profileRadius;
        state->profileKey=profileKey;
        state->profileValid=true;
    }
    if (state->frame!=frame) {
        if (!settings.Freeze && dt>0.0001f) {
            const auto rawVelocity=(tr.translate-state->position)*(1.0f/dt);
            // Render transforms contain small animation/interpolation jitter.
            // Differentiating the raw position twice amplified that jitter into
            // a permanent slosh impulse, especially at high frame rates. Track
            // a frame-rate-independent filtered velocity before deriving the
            // acceleration that drives the liquid plane.
            const float velocityBlend=1.0f-std::exp(-dt*14.0f);
            const auto filteredVelocity=state->velocity+(
                rawVelocity-state->velocity)*velocityBlend;
            auto accel=(filteredVelocity-state->velocity)*(1.0f/dt);
            if (Length(accel)<8.0f) accel={};

            const auto rawAngular=AngularVelocity(state->basis,axes,dt);
            const float angularBlend=1.0f-std::exp(-dt*16.0f);
            auto angular=state->angularVelocity+(
                rawAngular-state->angularVelocity)*angularBlend;
            if (Length(angular)<0.01f) angular={};
            const float forcing=Length(accel)/784.0f+Length(angular)*0.08f;
            if (state->sleeping && forcing>0.012f) {state->sleeping=false;state->sleepTime=0.0f;}
            if (!state->sleeping) {
                const float targets[2]={
                    std::clamp(accel.x/784.0f+angular.x*0.055f,-0.18f,0.18f)*settings.SloshStrength,
                    std::clamp(accel.y/784.0f+angular.y*0.055f,-0.18f,0.18f)*settings.SloshStrength};
                const float damping=std::clamp(settings.Damping+(style.viscosity-1.0f)*0.16f,0.25f,0.95f);
                const float frequency=8.0f/std::sqrt(std::max(style.viscosity,0.5f));
                for (int i=0;i<2;++i)
                    PIXL::ContainedLiquidMath::Spring(state->tilt[i],state->speed[i],targets[i],damping,dt,frequency);
                const float energy=std::abs(state->tilt[0])+std::abs(state->tilt[1])+
                    std::abs(state->speed[0])+std::abs(state->speed[1]);
                state->sleepTime=forcing<0.003f && energy<0.0015f?state->sleepTime+dt:0.0f;
                if (state->sleepTime>0.35f) {
                    state->sleeping=true;state->sleepTime=0.0f;
                    state->tilt[0]=state->tilt[1]=state->speed[0]=state->speed[1]=0.0f;
                }
            }
            state->velocity=filteredVelocity;
            state->angularVelocity=angular;
        }
        state->position=tr.translate;state->time=now;state->frame=frame;
        for (int i=0;i<3;++i) state->basis[i]=axes[i];
    }
    const float profileCenterZ=(state->profileMinZ+state->profileMaxZ)*0.5f;
    const auto profileCenter=tr.translate+axes[2]*(profileCenterZ*tr.scale);
    const RE::NiPoint3 profileRelative{profileCenter.x-adjust.x,profileCenter.y-adjust.y,profileCenter.z-adjust.z};
    GPUData data{};
    data.center={profileRelative.x,profileRelative.y,profileRelative.z,1};
    const float radii[3]={state->profileRadius,state->profileRadius,(state->profileMaxZ-state->profileMinZ)*0.5f};
    float4* packed[3]={&data.axisX,&data.axisY,&data.axisZ};
    for (int i=0;i<3;++i) *packed[i]={axes[i].x,axes[i].y,axes[i].z,radii[i]*tr.scale};
    RE::NiPoint3 n{state->tilt[0],state->tilt[1],1}; n=n*(1.0f/Length(n));
    const std::array<float,3> profilePlane={
        n.Dot(axes[0])*radii[0]*tr.scale,
        n.Dot(axes[1])*radii[1]*tr.scale,
        n.Dot(axes[2])*radii[2]*tr.scale};
    const float fillOffset=PIXL::ContainedLiquidMath::ProfileFillOffset(
        state->radialProfile,profilePlane,settings.Fill);
    data.plane={n.x,n.y,n.z,fillOffset};
    data.optics={settings.Absorption,settings.Refraction,static_cast<float>(settings.Debug),tr.scale};
    data.dynamics={now,std::min(1.0f,std::abs(state->speed[0])+std::abs(state->speed[1])),replaying?1.0f:0.0f,std::clamp((1600.0f-distance)/400.0f,0.0f,1.0f)};
    data.profile0={state->radialProfile[0],state->radialProfile[1],state->radialProfile[2],state->radialProfile[3]};
    data.profile1={state->radialProfile[4],state->radialProfile[5],state->radialProfile[6],state->radialProfile[7]};
    data.profile2={state->radialProfile[8],state->radialProfile[9],state->radialProfile[10],state->radialProfile[11]};
    data.profile3={state->radialProfile[12],state->radialProfile[13],state->radialProfile[14],state->radialProfile[15]};
    const float seed=std::fmod(static_cast<float>((owner->GetFormID()*1664525u+1013904223u)&0x00FFFFFFu)/16777216.0f,1.0f);
    data.detail={settings.BubbleStrength*style.bubbleScale,settings.InternalReflection,seed,1.52f};
    const float emission=style.magical?settings.Emission:settings.OrdinaryEmission;
    data.appearance={settings.SubsurfaceScattering,emission,settings.LabelPreservation,static_cast<float>(style.family)};
    data.opticalColor={style.absorption.x,style.absorption.y,style.absorption.z,style.magical?1.0f:0.0f};
    data.liquidColor={style.tint.x,style.tint.y,style.tint.z,style.liquidIOR};
    if (!CaptureScene(data)) {
        if (!captureFailureLogged) {
            logger::warn("[ContainedLiquids] Matched bottle but scene capture was rejected; vanilla draw retained");
            captureFailureLogged=true;
        }
        return;
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(buffer.get(),0,D3D11_MAP_WRITE_DISCARD,0,&map))) {
        if (!failureLogged) {logger::warn("[ContainedLiquids] Upload failed; using vanilla draw");failureLogged=true;} return;
    }
    std::memcpy(map.pData,&data,sizeof(data));ctx->Unmap(buffer.get(),0);
    ID3D11ShaderResourceView* views[2]{dataSRV.get(),sceneSRV.get()};ctx->PSSetShaderResources(120,2,views);
    if (reactiveRegionFrame!=frame) {reactiveRegionFrame=frame;reactiveRegionCount=0;}
    if (reactiveRegionCount<reactiveRegions.size())
        reactiveRegions[reactiveRegionCount++]=pendingReactiveRegion;
    ++matches;
    if (replaying) preparedPass=pass;
    if (!matchedLogged) {logger::info("[ContainedLiquids] Supported potion/beverage bottle matched; effect-aware optics active");matchedLogged=true;}
}
void ContainedLiquids::Hook::thunk(RE::BSShader* shader,RE::BSRenderPass* pass,std::uint32_t flags)
{ func(shader,pass,flags); if (globals::pipeline::containedLiquids.loaded) globals::pipeline::containedLiquids.SetupGeometry(pass); }
void ContainedLiquids::PostPostLoad()
{
    stl::write_vfunc<0x6,Hook>(RE::VTABLE_BSLightingShader[0]);
    // Same verified RenderPassImmediately call site used by TerrainSeam; chain it.
    stl::write_thunk_call<DrawHook>(REL::RelocationID(100852,107642).address()+REL::Relocate(0x29E,0x28F));
    logger::info("[ContainedLiquids] Clear-container hook installed; opaque/wrapped beverages fail closed");
}
void ContainedLiquids::DrawHook::thunk(RE::BSRenderPass* pass,std::uint32_t technique,bool alphaTest,std::uint32_t flags)
{
    auto& module=globals::pipeline::containedLiquids;
    if (module.loaded && module.settings.Enabled && !module.replaying && globals::state &&
        globals::state->inWorld && !globals::state->activeReflections && globals::deferred->deferredPass &&
        pass && pass->geometry && pass->shader && pass->shader->shaderType.get()==RE::BSShader::Type::Lighting &&
        IsLikelyLiquidGeometry(pass->geometry->name.c_str())) {
        if (module.pendingFrame!=globals::state->frameCount) {module.pendingCount=0;module.pendingFrame=globals::state->frameCount;}
        bool duplicate=false;
        for (size_t i=0;i<module.pendingCount;++i) if (module.pending[i].pass->geometry==pass->geometry) duplicate=true;
        if (!duplicate && module.pendingCount<module.pending.size()) module.pending[module.pendingCount++]={pass,technique,flags,alphaTest};
    }
    func(pass,technique,alphaTest,flags); // Original opaque/depth/shadow behavior is never skipped.
}
void ContainedLiquids::ReplayAfterDeferred()
{
    if (!settings.Enabled || !pendingCount || pendingFrame!=globals::state->frameCount) {pendingCount=0;return;}
    auto* ctx=globals::d3d::context;
    auto* renderer=globals::game::renderer;
    auto& shadow=globals::game::shadowState->GetRuntimeData();
    ID3D11RenderTargetView* saved[8]{}; ID3D11DepthStencilView* savedDepth=nullptr;
    ctx->OMGetRenderTargets(8,saved,&savedDepth);
    ID3D11RenderTargetView* forward[4]{};
    for (int i=0;i<4;++i) {
        auto id=shadow.renderTargets[i];
        if (id!=RE::RENDER_TARGET::kNONE) forward[i]=renderer->GetRuntimeData().renderTargets[id].RTV;
    }
    auto* depth=renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0];
    ctx->OMSetRenderTargets(4,forward,depth);
    replaying=true;
    const auto count=pendingCount;
    // Passes belong to this frame's live accumulator, as in TerrainSeam replay.
    // No pointer is retained for a later frame or dereferenced after unload.
    for (size_t i=0;i<count;++i) {
        const auto draw=pending[i];
        preparedPass=nullptr;
        SetupGeometry(draw.pass);
        if (preparedPass) DrawHook::func(draw.pass,draw.technique,draw.alphaTest,draw.flags);
        preparedPass=nullptr;
    }
    replaying=false;pendingCount=0;
    if (count && !replayLogged) {
        logger::info("[ContainedLiquids] Replayed %zu captured bottle draw(s) after deferred lighting", count);
        replayLogged=true;
    }
    ID3D11ShaderResourceView* empty[2]{};ctx->PSSetShaderResources(120,2,empty);
    ctx->OMSetRenderTargets(8,saved,savedDepth);
    for (auto* rtv:saved) if (rtv) rtv->Release();
    if (savedDepth) savedDepth->Release();
    shadow.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
}

void ContainedLiquids::MarkReconstructionReactive(ID3D11UnorderedAccessView* target,
    std::uint32_t width,std::uint32_t height)
{
    if (!target || !globals::state || reactiveRegionFrame!=globals::state->frameCount ||
        !reactiveRegionCount || !width || !height) return;
    winrt::com_ptr<ID3D11DeviceContext1> context1;
    if (FAILED(globals::d3d::context->QueryInterface(IID_PPV_ARGS(context1.put()))) || !context1) return;
    std::array<D3D11_RECT,32> rectangles{};
    UINT count=0;
    for (std::size_t i=0;i<reactiveRegionCount && count<rectangles.size();++i) {
        const auto& source=reactiveRegions[i];
        if (!source.sourceWidth || !source.sourceHeight) continue;
        const float scaleX=static_cast<float>(width)/source.sourceWidth;
        const float scaleY=static_cast<float>(height)/source.sourceHeight;
        D3D11_RECT rectangle{
            static_cast<LONG>(std::floor(source.left*scaleX)),
            static_cast<LONG>(std::floor(source.top*scaleY)),
            static_cast<LONG>(std::ceil(source.right*scaleX)),
            static_cast<LONG>(std::ceil(source.bottom*scaleY))};
        rectangle.left=std::clamp<LONG>(rectangle.left,0,width);
        rectangle.top=std::clamp<LONG>(rectangle.top,0,height);
        rectangle.right=std::clamp<LONG>(rectangle.right,0,width);
        rectangle.bottom=std::clamp<LONG>(rectangle.bottom,0,height);
        if (rectangle.right>rectangle.left && rectangle.bottom>rectangle.top)
            rectangles[count++]=rectangle;
    }
    if (count) {
        const float reactive[4]{1.0f,1.0f,1.0f,1.0f};
        context1->ClearView(target,reactive,rectangles.data(),count);
    }
}
