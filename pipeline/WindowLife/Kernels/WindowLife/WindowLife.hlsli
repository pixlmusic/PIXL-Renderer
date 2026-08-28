#ifndef PIXL_WINDOW_LIFE_HLSLI
#define PIXL_WINDOW_LIFE_HLSLI

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

namespace WindowLife
{
    // Lighting already consumes the full D3D11 b0-b13 constant-buffer bank in
    // the live PIXL renderer. Keep WindowLife per-draw data in one structured
    // structured SRV instead; the installer audits and can relocate t127 if needed.
    struct PerDrawData
    {
        float4 Runtime0;
        float4 Optics0;
        float4 Surface0;
        float4 Runtime1;
        float4 Glass0;
        float4 Glass1;
        float4 Class0;
        float4 Eligibility0;
        float4 Interior0;
        float4 Geometry0;
        float4 Asset0;
		// x room contrast, y room emission, z occupant opacity,
		// w automatic room-art scale
		float4 Presentation0;
    };

    Texture2D<float4> WindowLifeOccupantAtlas : register(t123);
    Texture2D<float4> WindowLifeCurtainAtlas : register(t124);
    Texture2D<float4> WindowLifeAuthoredPaneMask : register(t125);
    Texture2D<float4> WindowLifeRoomAtlas : register(t126);
    StructuredBuffer<PerDrawData> WindowLifePerDraw : register(t127);

    float4 GetRuntime0() { return WindowLifePerDraw[0].Runtime0; }
    float4 GetOptics0() { return WindowLifePerDraw[0].Optics0; }
    float4 GetSurface0() { return WindowLifePerDraw[0].Surface0; }
    float4 GetRuntime1() { return WindowLifePerDraw[0].Runtime1; }
    float4 GetGlass0() { return WindowLifePerDraw[0].Glass0; }
    float4 GetGlass1() { return WindowLifePerDraw[0].Glass1; }
    float4 GetClass0() { return WindowLifePerDraw[0].Class0; }
    float4 GetEligibility0() { return WindowLifePerDraw[0].Eligibility0; }
    float4 GetInterior0() { return WindowLifePerDraw[0].Interior0; }
    float4 GetGeometry0() { return WindowLifePerDraw[0].Geometry0; }
    float4 GetAsset0() { return WindowLifePerDraw[0].Asset0; }
	float4 GetPresentation0() { return WindowLifePerDraw[0].Presentation0; }

    struct SurfaceResult
    {
        float paneMask;
        float glassWeight;
        float roughness;
        float f0;
        float transmission;
        float normalRetention;
        float2 normalWarp;
        float tier;
        float occupancyEligible;
        float debugMask;
    };

    struct Result
    {
        float occlusion;
        float paneMask;
        float debugMask;
        float authoredLayoutState;
        float activity;
        float tier;
        float glassWeight;
        float curtainOcclusion;
        float roomDepthOcclusion;
        float3 roomColor;
        float roomColorWeight;
    };

    bool IsCandidate() { return GetRuntime0().x > 0.5f && GetClass0().x > 0.5f; }
    bool HasNamedGlass() { return GetClass0().y > 0.5f; }
    uint GetPaneSourceFlags() { return (uint)floor(GetClass0().z + 0.5f); }
    bool HasGameGlowTexture() { return (GetPaneSourceFlags() & 1u) != 0u; }
    bool HasExternalAuthoredMask() { return (GetPaneSourceFlags() & 2u) != 0u; }
    bool HasAuthoredPaneTexture() { return GetPaneSourceFlags() != 0u; }
    bool IsExplicitWindow() { return GetClass0().w > 0.5f; }
    bool SuppressAutoPOM() { return GetGlass1().w > 0.5f; }

    float3 SampleAuthoredPaneTexture(float2 materialUV)
    {
        float3 sampleValue = 0.0f.xxx;
        if (HasExternalAuthoredMask())
            sampleValue = WindowLifeAuthoredPaneMask.Sample(SampGlowSampler, materialUV).xyz;
        else
            sampleValue = TexGlowSampler.Sample(SampGlowSampler, materialUV).xyz;
        return sampleValue;
    }

    float3 SampleAuthoredPaneTextureLevel(float2 materialUV, float mipLevel)
    {
        float3 sampleValue = 0.0f.xxx;
        if (HasExternalAuthoredMask())
            sampleValue = WindowLifeAuthoredPaneMask.SampleLevel(SampGlowSampler, materialUV, mipLevel).xyz;
        else
            sampleValue = TexGlowSampler.SampleLevel(SampGlowSampler, materialUV, mipLevel).xyz;
        return sampleValue;
    }

    void GetAuthoredPaneTextureDimensions(out uint width, out uint height, out uint mipCount)
    {
        width = 0u;
        height = 0u;
        mipCount = 0u;
        if (HasExternalAuthoredMask())
            WindowLifeAuthoredPaneMask.GetDimensions(0, width, height, mipCount);
        else
            TexGlowSampler.GetDimensions(0, width, height, mipCount);
    }

    float Hash11(float p)
    {
        p = frac(p * 0.1031f);
        p *= p + 33.33f;
        p *= p + p;
        return frac(p);
    }

    float Hash21(float2 p)
    {
        float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
        p3 += dot(p3, p3.yzx + 33.33f);
        return frac((p3.x + p3.y) * p3.z);
    }

    float ValueNoise2(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * (3.0f - 2.0f * f);
        float a = Hash21(i);
        float b = Hash21(i + float2(1.0f, 0.0f));
        float c = Hash21(i + float2(0.0f, 1.0f));
        float d = Hash21(i + float2(1.0f, 1.0f));
        return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    }

    float SegmentDistance(float2 p, float2 a, float2 b)
    {
        float2 pa = p - a;
        float2 ba = b - a;
        float denom = max(dot(ba, ba), 1.0e-5f);
        float h = saturate(dot(pa, ba) / denom);
        return length(pa - ba * h);
    }

    float EllipseDistance(float2 p, float2 radius)
    {
        return (length(p / max(radius, 1.0e-4f.xx)) - 1.0f) * min(radius.x, radius.y);
    }

    float ShapeMask(float distanceValue, float softness)
    {
        return 1.0f - smoothstep(-softness, softness, distanceValue);
    }

    float TaperedCapsuleDistance(float2 p, float2 a, float2 b, float radiusA, float radiusB)
    {
        float2 ba = b - a;
        float h = saturate(dot(p - a, ba) / max(dot(ba, ba), 1.0e-5f));
        return length(p - lerp(a, b, h)) - lerp(radiusA, radiusB, h);
    }

    // Articulated side/three-quarter profiles replace the former symmetric icon
    // figure. Gait now advances with travel, and seeded cloak, pack, robe and
    // carried-object variants stop neighbouring windows from repeating one shape.
    float PersonMask(
        float2 p,
        float pose,
        float motionPhase,
        float variant,
        float softness,
        float scale,
        float depthBlur,
        float direction)
    {
        p /= max(scale, 0.25f);

        float side = direction > 0.5f ? 1.0f : -1.0f;
        float gait = sin(motionPhase * 12.5663706f + pose * 5.7f);
        float gaitOpposite = sin(motionPhase * 12.5663706f + pose * 5.7f + 3.1415927f);
        float lean = side * ((pose - 0.5f) * 0.055f + gait * 0.012f);
        p.y += abs(gait) * 0.008f;

        float2 headCenter = float2(lean + side * 0.018f, 0.258f);
        float head = EllipseDistance(p - headCenter, float2(0.076f, 0.094f));
        float profile = EllipseDistance(
            p - (headCenter + float2(side * 0.066f, -0.006f)),
            float2(0.031f, 0.027f));
        float neck = TaperedCapsuleDistance(
            p, float2(lean * 0.55f, 0.185f), float2(lean * 0.35f, 0.140f), 0.045f, 0.054f);
        float torso = TaperedCapsuleDistance(
            p, float2(lean + side * 0.018f, 0.135f), float2(lean * 0.22f, -0.142f), 0.125f, 0.096f);

        float2 frontShoulder = float2(lean + side * 0.087f, 0.115f);
        float2 frontElbow = float2(side * (0.145f + gait * 0.055f), -0.010f + gaitOpposite * 0.025f);
        float2 frontHand = float2(side * (0.125f + gait * 0.125f), -0.150f + gaitOpposite * 0.030f);
        float frontArm = min(
            SegmentDistance(p, frontShoulder, frontElbow) - 0.042f,
            SegmentDistance(p, frontElbow, frontHand) - 0.036f);

        float2 backShoulder = float2(lean - side * 0.072f, 0.104f);
        float2 backElbow = float2(-side * (0.118f + gait * 0.045f), -0.005f - gaitOpposite * 0.018f);
        float2 backHand = float2(-side * (0.112f + gait * 0.108f), -0.145f - gaitOpposite * 0.022f);
        float backArm = min(
            SegmentDistance(p, backShoulder, backElbow) - 0.038f,
            SegmentDistance(p, backElbow, backHand) - 0.033f);

        float2 frontHip = float2(side * 0.052f, -0.125f);
        float2 frontKnee = float2(side * (0.055f + gait * 0.105f), -0.278f);
        float2 frontFoot = float2(side * (0.075f + gait * 0.185f), -0.432f + abs(gait) * 0.018f);
        float frontLeg = min(
            SegmentDistance(p, frontHip, frontKnee) - 0.052f,
            SegmentDistance(p, frontKnee, frontFoot) - 0.044f);

        float2 backHip = float2(-side * 0.046f, -0.130f);
        float2 backKnee = float2(-side * (0.050f + gaitOpposite * 0.092f), -0.282f);
        float2 backFoot = float2(-side * (0.070f + gaitOpposite * 0.168f), -0.432f + abs(gaitOpposite) * 0.016f);
        float backLeg = min(
            SegmentDistance(p, backHip, backKnee) - 0.049f,
            SegmentDistance(p, backKnee, backFoot) - 0.042f);

        float d = min(min(head, profile), min(neck, min(torso, min(frontArm, min(backArm, min(frontLeg, backLeg))))));

        if (variant < 0.25f) {
            // A loose cloak has an asymmetric trailing edge rather than the icon's
            // rigid oval torso.
            float cloak = TaperedCapsuleDistance(
                p, float2(-side * 0.035f, 0.095f), float2(-side * 0.085f, -0.245f), 0.118f, 0.165f);
            d = min(d, cloak);
        } else if (variant < 0.50f) {
            // Robed resident: broad lower garment, with feet still separating as
            // the pose changes so it does not become a restroom pictogram.
            float robe = TaperedCapsuleDistance(
                p, float2(lean * 0.4f, 0.090f), float2(-side * 0.018f, -0.315f), 0.120f, 0.178f);
            d = min(d, robe);
        } else if (variant < 0.75f) {
            float pack = EllipseDistance(
                p - float2(-side * 0.120f, 0.015f), float2(0.105f, 0.170f));
            d = min(d, pack);
        } else {
            float2 carriedCenter = float2(side * 0.205f, -0.040f);
            float carried = EllipseDistance(p - carriedCenter, float2(0.092f, 0.074f));
            float carryingArm = SegmentDistance(p, frontElbow, carriedCenter) - 0.039f;
            d = min(d, min(carried, carryingArm));
        }

        return ShapeMask(d, softness * depthBlur);
    }

    float ActivityEnvelope(float phase)
    {
        return smoothstep(0.04f, 0.16f, phase) * (1.0f - smoothstep(0.82f, 0.97f, phase));
    }

    float MotionX(float phase, float mode, float direction)
    {
        float eased = phase * phase * (3.0f - 2.0f * phase);
        if (mode < 0.55f)
            return lerp(-0.62f, 0.62f, direction > 0.5f ? eased : 1.0f - eased);
        if (mode < 0.80f) {
            float arrive = smoothstep(0.04f, 0.28f, phase);
            float leave = smoothstep(0.68f, 0.94f, phase);
            float side = direction > 0.5f ? 1.0f : -1.0f;
            return lerp(side * -0.62f, side * 0.04f, arrive) + side * 0.58f * leave;
        }
        return (direction > 0.5f ? 0.10f : -0.10f) + sin(phase * 6.2831853f) * 0.035f;
    }

    float PickRoomTile(float selector, float a, float b, float c, float d, float e)
    {
        float choice = floor(saturate(selector) * 4.999f);
        float result = e;
        if (choice < 3.5f) result = d;
        if (choice < 2.5f) result = c;
        if (choice < 1.5f) result = b;
        if (choice < 0.5f) result = a;
        return result;
    }

    float SelectRoomTile(float roomSeed, float family)
    {
        float selector = Hash11(roomSeed * 173.31f + 7.13f);
        // The generated atlas is curated by architectural/material family rather
        // than selecting all rooms uniformly. This keeps noble Solitude windows,
        // Riften timber, Windhelm stone, Markarth/Dwemer and trade interiors
        // plausible while preserving several stable variants inside each group.
        float roomTile = PickRoomTile(selector, 0.0f, 6.0f, 8.0f, 11.0f, 13.0f);
        if (family >= 0.5f)       // Solitude / castle / noble
            roomTile = PickRoomTile(selector, 1.0f, 2.0f, 7.0f, 10.0f, 15.0f);
        if (family >= 1.5f)       // Riften / canal timber
            roomTile = PickRoomTile(selector, 0.0f, 3.0f, 6.0f, 11.0f, 14.0f);
        if (family >= 2.5f)       // Windhelm / dark stone
            roomTile = PickRoomTile(selector, 2.0f, 5.0f, 7.0f, 9.0f, 12.0f);
        if (family >= 3.5f)       // Markarth / Dwemer
            roomTile = PickRoomTile(selector, 12.0f, 7.0f, 2.0f, 12.0f, 9.0f);
        if (family >= 4.5f)       // inn / shop / trade
            roomTile = PickRoomTile(selector, 1.0f, 3.0f, 4.0f, 5.0f, 14.0f);
        return roomTile;
    }

    float SelectFloorComponent(float4 values, float column)
    {
        float result = values.w;
        if (column < 2.5f) result = values.z;
        if (column < 1.5f) result = values.y;
        if (column < 0.5f) result = values.x;
        return result;
    }

    float RoomFloorSourceV(float roomTile)
    {
        // Each authored room has a different perspective horizon/floor boundary.
        // These normalized source-V anchors were measured from the bundled 4x4
        // atlas. They let every room present its floor at a consistent low window
        // height without editing, stretching or adding another metadata texture.
        const float4 row0 = float4(0.60f, 0.63f, 0.64f, 0.64f);
        const float4 row1 = float4(0.68f, 0.62f, 0.62f, 0.68f);
        const float4 row2 = float4(0.66f, 0.64f, 0.68f, 0.65f);
        const float4 row3 = float4(0.68f, 0.67f, 0.70f, 0.71f);

        float tile = clamp(floor(roomTile + 0.5f), 0.0f, 15.0f);
        float row = floor(tile * 0.25f);
        float column = tile - row * 4.0f;
        float4 values = row3;
        if (row < 2.5f) values = row2;
        if (row < 1.5f) values = row1;
        if (row < 0.5f) values = row0;
        return SelectFloorComponent(values, column);
    }

    float2 FrameAuthoredRoom(
        float2 roomLocal,
        float2 roomSize,
        float roomTile,
        out float floorLocalY)
    {
        float2 sourceUV = float2(roomLocal.x, 1.0f - roomLocal.y);
        float sourceFloorV = RoomFloorSourceV(roomTile);
        floorLocalY = 1.0f - sourceFloorV;

        // Manual mode remains the exact owner-preferred grid/crop path. Automatic
        // mode keeps its stable detected aperture but treats it as a viewport into
        // the room art rather than stretching the complete square tile to fit.
        if (GetInterior0().z < 0.5f)
            return sourceUV;

        float magnification = clamp(GetPresentation0().w, 1.0f, 1.80f);
        float baseSpan = rcp(magnification);
        float apertureAspect = clamp(
            roomSize.x / max(roomSize.y, 1.0f), 0.72f, 1.45f);
        float2 artSpan = baseSpan.xx;
        if (apertureAspect < 1.0f)
            artSpan.x *= apertureAspect;
        else
            artSpan.y /= apertureAspect;
        artSpan = clamp(artSpan, 0.42f.xx, 0.98f.xx);

        const float targetFloorV = 0.80f;
        float2 artCenter = float2(
            0.5f,
            sourceFloorV - (targetFloorV - 0.5f) * artSpan.y);
        float2 halfSpan = artSpan * 0.5f;
        artCenter = clamp(
            artCenter,
            halfSpan + 0.008f.xx,
            1.0f.xx - halfSpan - 0.008f.xx);

        float actualFloorV =
            0.5f + (sourceFloorV - artCenter.y) / max(artSpan.y, 1.0e-4f);
        floorLocalY = 1.0f - saturate(actualFloorV);
        return saturate(artCenter + (sourceUV - 0.5f.xx) * artSpan);
    }

    float Verticality(float3 N)
    {
        return 1.0f - smoothstep(0.48f, 0.90f, abs(N.z));
    }

    float ResolveTier(float3 N)
    {
        float tier = clamp(GetClass0().x, 1.0f, 3.0f);
        float radius = max(GetGeometry0().w, 0.0f);
        if (radius > 0.0f) {
            if (radius < GetEligibility0().x)
                tier = min(tier, 1.0f);
            else if (radius < GetEligibility0().y)
                tier = min(tier, 2.0f);
        }

        float verticality = Verticality(N);
        if (verticality < 0.18f)
            tier = min(tier, 1.0f);
        else if (verticality < GetEligibility0().z)
            tier = min(tier, 2.0f);
        return tier;
    }

    float PaneNormalEvidence(float4 normalSample)
    {
        // Architectural glass is locally flat even when the surrounding frame is
        // ornate or metallic. Combine tangent-normal slope with local continuity
        // so bright mullions and carved frames do not become pane pixels.
        float2 tangentSlope = normalSample.xy * 2.0f - 1.0f;
        float flatness = 1.0f - smoothstep(0.20f, 0.62f, length(tangentSlope));
        float normalVariation =
            length(ddx_coarse(normalSample.xy)) +
            length(ddy_coarse(normalSample.xy));
        float continuity = 1.0f - smoothstep(0.035f, 0.24f, normalVariation);
        return saturate(flatness * lerp(0.72f, 1.0f, continuity));
    }

    float PreParallaxPaneMask(float3 baseColor, float4 normalSample)
    {
        if (!IsCandidate())
            return 0.0f;

        // A real glow texture is a materially authored pane mask. Defer to it in
        // PaneMask instead of allowing bright diffuse masonry to become glass.
        if (HasAuthoredPaneTexture())
            return 0.0f;

        float3 safeColor = max(baseColor, 0.0f);
        float luma = dot(safeColor, float3(0.2126f, 0.7152f, 0.0722f));
        float diffusePane = smoothstep(GetSurface0().z, GetSurface0().z + max(GetSurface0().w, 0.01f), luma);
        float smoothEvidence = smoothstep(0.10f, 0.64f, saturate(normalSample.w));
        float flatEvidence = PaneNormalEvidence(normalSample);

        // Explicit window/glass materials can be non-emissive by day, but still
        // need flat-normal evidence. Brightness or gloss alone would select frames.
        float namedPane = (IsExplicitWindow() || HasNamedGlass())
            ? diffusePane * flatEvidence * lerp(0.58f, 1.0f, smoothEvidence)
            : 0.0f;
        return saturate(namedPane);
    }

    float SamplePaneGlowEvidence(float2 materialUV, float2 atlasTile)
    {
        // Keep the mask inside its authored UV tile. A wrapping sampler must not
        // borrow glow from the opposite edge of a facade atlas.
        if (any(abs(floor(materialUV) - atlasTile) > 0.5f))
            return 0.0f;

        float3 rawGlow = SampleAuthoredPaneTexture(materialUV);
        float rawLuma = dot(max(rawGlow, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
        return smoothstep(0.0030f, 0.055f, rawLuma);
    }

    float PaneMask(float3 baseColor, float4 normalSample, float glowLuma, float2 materialUV)
    {
        float pre = PreParallaxPaneMask(baseColor, normalSample);
        if (!HasAuthoredPaneTexture())
            return pre;

        // Solitude and the other authored architectural window glow maps already
        // encode glass as bright and frames/masonry as black. Treat that channel
        // as authoritative. Fixed conservative thresholds keep user diffuse-mask
        // tuning from expanding glass back onto frames while retaining small panes.
        float authoredLuma = max(glowLuma, 0.0f);
        if (HasExternalAuthoredMask()) {
            float3 authoredMask = SampleAuthoredPaneTexture(materialUV);
            authoredLuma = dot(max(authoredMask, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
        }
        float glowPane = smoothstep(0.028f, 0.145f, authoredLuma);

        // Erode only the outer two authored texels. This rejects luminous stone,
        // trim and window-frame halos while retaining the much larger glass
        // islands and every dark lead/mullion encoded by the glow map itself.
        uint glowWidth;
        uint glowHeight;
        uint glowMipCount;
        GetAuthoredPaneTextureDimensions(glowWidth, glowHeight, glowMipCount);
        float2 glowTexel = 1.0f / max(float2((float)glowWidth, (float)glowHeight), 1.0f.xx);
        float2 atlasTile = floor(materialUV);
        float neighborFloor = min(
            min(
                SamplePaneGlowEvidence(materialUV - float2(glowTexel.x * 2.0f, 0.0f), atlasTile),
                SamplePaneGlowEvidence(materialUV + float2(glowTexel.x * 2.0f, 0.0f), atlasTile)),
            min(
                SamplePaneGlowEvidence(materialUV - float2(0.0f, glowTexel.y * 2.0f), atlasTile),
                SamplePaneGlowEvidence(materialUV + float2(0.0f, glowTexel.y * 2.0f), atlasTile)));
        float authoredCore = smoothstep(0.10f, 0.68f, neighborFloor);

        // Some architectural glow atlases include luminous trim, frames, or even
        // broad facade texels. The glow map says where a pane may exist; the
        // locally flat tangent normal confirms that the texel is glass. This is
        // deliberately evaluated at full resolution so mullions remain excluded
        // even though the coarse layout guide bridges them below.
        // A separately authored *_mask atlas is already exact glass/frame data;
        // do not reject its panes because intentionally rippled old-glass normals
        // are non-flat. Native glow atlases still need the normal guard because
        // some of them contain luminous trim or facade texels.
        float glassSurface = HasExternalAuthoredMask()
            ? 1.0f
            : smoothstep(0.22f, 0.80f, PaneNormalEvidence(normalSample));
        float diffuseLuma = dot(max(baseColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
        float diffuseSupport = smoothstep(0.012f, 0.105f, diffuseLuma);
        return saturate(
            glowPane * authoredCore * glassSurface * lerp(0.42f, 1.0f, diffuseSupport));
    }

    float GlowFootprintMip(float2 materialUV, float2 textureSize, float mipCount)
    {
        float2 gradientX = ddx_coarse(materialUV) * textureSize;
        float2 gradientY = ddy_coarse(materialUV) * textureSize;
        float footprint = max(length(gradientX), length(gradientY));
        return clamp(log2(max(footprint, 1.0f)), 0.0f, max(mipCount - 1.0f, 0.0f));
    }

    float InteriorPaneMask(
        float3 baseColor,
        float4 normalSample,
        float glowLuma,
        float2 materialUV,
        float precisePane)
    {
        if (!HasAuthoredPaneTexture())
            return precisePane;

        // The external atlas is an exact glass/frame stencil. Keep every room,
        // curtain and occupant contribution inside that full-resolution result;
        // mip widening here was the source of the visible facade/frame halo.
        if (HasExternalAuthoredMask())
            return precisePane;

        uint textureWidth;
        uint textureHeight;
        uint mipCount;
        GetAuthoredPaneTextureDimensions(textureWidth, textureHeight, mipCount);
        if (textureWidth == 0u || textureHeight == 0u || mipCount == 0u)
            return precisePane;

        float2 textureSize = float2((float)textureWidth, (float)textureHeight);
        float footprintMip = GlowFootprintMip(materialUV, textureSize, (float)mipCount);
        float retainedMip = clamp(footprintMip - 1.20f, 0.0f, (float)(mipCount - 1u));
        float3 retainedGlow = SampleAuthoredPaneTextureLevel(materialUV, retainedMip);
        float retainedLuma = dot(
            max(retainedGlow, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));

        // Fine pane erosion is authoritative up close. Once a complete authored
        // window is only a few texels wide, preserve its filtered glass coverage
        // instead of letting black mullions erase the recessed room. Normal-map
        // evidence remains mandatory, so luminous facade or trim texels cannot
        // promote an entire building into a window.
        float minificationBlend = smoothstep(1.20f, 4.25f, footprintMip);
        float retainedCoverage = smoothstep(0.0050f, 0.060f, retainedLuma);
        float surfaceGuard = HasExternalAuthoredMask()
            ? 1.0f
            : smoothstep(0.075f, 0.68f, PaneNormalEvidence(normalSample));
        float distanceStablePane = retainedCoverage * surfaceGuard * 0.94f;
        return saturate(max(precisePane, distanceStablePane * minificationBlend));
    }

    struct PaneLayout
    {
        float2 local;
        float2 roomSize;
        float2 centerUV;
        float2 centerPlane;
        float backgroundConfidence;
        float confidence;
    };

    float SampleAuthoredPaneGuide(float2 materialUV, float mipLevel, float2 tile)
    {
        // Never let a wrapping material sampler join opposite sides of an atlas.
        // The actual pane mask remains full resolution; this deliberately coarse
        // guide only closes narrow mullion gaps into one logical window group.
        if (any(abs(floor(materialUV) - tile) > 0.5f))
            return 0.0f;

        // Raw samples are sufficient for binary layout evidence. Layout follows
        // the currently installed native glow texture; optional external masks
        // remain final glass clips and can never resize, seed or move a room.
        float3 glow = TexGlowSampler.SampleLevel(SampGlowSampler, materialUV, mipLevel).xyz;
        float luma = dot(max(glow, 0.0f), float3(0.2126f, 0.7152f, 0.0722f));
        return smoothstep(0.0040f, 0.060f, luma);
    }

    float FindAuthoredPaneEdge(
        float2 materialUV,
        float2 stepUV,
        float mipLevel,
        float2 tile,
        out float found)
    {
        const float guideThreshold = 0.50f;
        float insideUnits = 0.0f;
        float outsideUnits = 12.0f;
        float outsideRun = 0.0f;
        found = 0.0f;

        // Twelve fractional-mip guide texels span a complete repeated UV tile.
        // This is the important distinction between a pane and an aperture: a
        // sample that starts in the upper-left pane can cross the centre mullions
        // and still discover the opposite outer frame instead of fitting a room to
        // that single quadrant.
        [unroll]
        for (int i = 1; i <= 12; ++i) {
            if (found < 0.5f) {
                float guide = SampleAuthoredPaneGuide(
                    materialUV + stepUV * (float)i, mipLevel, tile);
                if (guide > guideThreshold) {
                    insideUnits = (float)i;
                    outsideRun = 0.0f;
                } else {
                    outsideRun += 1.0f;
                    // A single dark guide texel is normally a lead/wood mullion.
                    // Require a two-texel gutter before declaring the edge so all
                    // sub-panes of one authored window resolve to one room.
                    if (outsideRun >= 2.0f) {
                        outsideUnits = max((float)i - 1.0f, insideUnits + 0.25f);
                        found = 1.0f;
                    }
                }
            }
        }

        if (found < 0.5f)
            return outsideUnits * length(stepUV);

        // Two bounded refinements remove the coarse-mip stair stepping that would
        // otherwise make silhouettes change scale while crossing a pane.
        [unroll]
        for (int refine = 0; refine < 2; ++refine) {
            float middle = (insideUnits + outsideUnits) * 0.5f;
            float guide = SampleAuthoredPaneGuide(
                materialUV + stepUV * middle, mipLevel, tile);
            if (guide > guideThreshold)
                insideUnits = middle;
            else
                outsideUnits = middle;
        }
        return ((insideUnits + outsideUnits) * 0.5f) * length(stepUV);
    }

    float2 FindNearbyAuthoredPaneSeed(
        float2 materialUV,
        float2 stepUV,
        float mipLevel,
        float2 tile,
        out float bestGuide)
    {
        float2 bestSeed = materialUV;
        bestGuide = SampleAuthoredPaneGuide(materialUV, mipLevel, tile);

        // A narrow leaded pane can disappear at the coarse guide mip even though
        // its full-resolution glow is valid. Search only the immediately adjacent
        // guide texels so mask fitting remains local to this authored window and
        // cannot jump across the dark gutter into a neighbouring atlas entry.
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            [unroll]
            for (int x = -1; x <= 1; ++x) {
                float2 candidate = materialUV + float2((float)x, (float)y) * stepUV;
                float guide = SampleAuthoredPaneGuide(candidate, mipLevel, tile);
                float proximityBias = (abs((float)x) + abs((float)y)) * 0.006f;
                if (guide - proximityBias > bestGuide) {
                    bestSeed = candidate;
                    bestGuide = guide - proximityBias;
                }
            }
        }
        return bestSeed;
    }

    float2 UVDeltaToPlane(
        float2 deltaUV,
        float2 uvDx,
        float2 uvDy,
        float2 planeDx,
        float2 planeDy,
        out float valid)
    {
        float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        valid = abs(determinant) > 1.0e-9f ? 1.0f : 0.0f;
        float safeDeterminant = abs(determinant) > 1.0e-9f
            ? determinant
            : (determinant < 0.0f ? -1.0e-9f : 1.0e-9f);
        float screenX = (deltaUV.x * uvDy.y - deltaUV.y * uvDy.x) / safeDeterminant;
        float screenY = (uvDx.x * deltaUV.y - uvDx.y * deltaUV.x) / safeDeterminant;
        return planeDx * screenX + planeDy * screenY;
    }

    PaneLayout ResolveAuthoredPaneLayout(float2 materialUV, float2 plane)
    {
        PaneLayout result = (PaneLayout)0;
        if (!HasGameGlowTexture() || GetInterior0().z < 0.5f)
            return result;

        uint textureWidth;
        uint textureHeight;
        uint mipCount;
        TexGlowSampler.GetDimensions(0, textureWidth, textureHeight, mipCount);
        if (textureWidth < 8u || textureHeight < 8u || mipCount == 0u)
            return result;

        float2 textureSize = float2((float)textureWidth, (float)textureHeight);
        // Work at about twelve guide texels across the longest atlas dimension.
        // A fractional mip merges thin lead/cross mullions without losing the
        // broader dark frame that separates genuinely neighbouring windows.
        float desiredMip = log2(max(max(textureSize.x, textureSize.y) / 12.0f, 1.0f));
        float guideMip = clamp(desiredMip, 0.0f, (float)(mipCount - 1u));
        float2 stepUV = exp2(guideMip) / textureSize;
        float2 tile = floor(materialUV);
        float initialGuide = SampleAuthoredPaneGuide(materialUV, guideMip, tile);
        float2 initialSeed = materialUV;
        // Normal panes retain the original one-sample fast path. Only narrow
        // sub-panes lost by the coarse mip pay for the local recovery search.
        [branch] if (initialGuide <= 0.25f) {
            initialSeed = FindNearbyAuthoredPaneSeed(
                materialUV, stepUV, guideMip, tile, initialGuide);
        }
        if (initialGuide <= 0.25f)
            return result;

        float foundLeft;
        float foundRight;
        float foundDown;
        float foundUp;
        float left = FindAuthoredPaneEdge(initialSeed, float2(-stepUV.x, 0.0f), guideMip, tile, foundLeft);
        float right = FindAuthoredPaneEdge(initialSeed, float2(stepUV.x, 0.0f), guideMip, tile, foundRight);
        float down = FindAuthoredPaneEdge(initialSeed, float2(0.0f, -stepUV.y), guideMip, tile, foundDown);
        float up = FindAuthoredPaneEdge(initialSeed, float2(0.0f, stepUV.y), guideMip, tile, foundUp);

        float2 spanUV = float2(left + right, down + up);
        if (min(spanUV.x, spanUV.y) <= min(stepUV.x, stepUV.y) * 1.25f)
            return result;

        // The edge-pair midpoint is invariant for all sub-panes connected by the
        // coarse guide, so leaded sections share one aperture without a second
        // expensive search pass.
        result.centerUV = initialSeed + float2((right - left) * 0.5f, (up - down) * 0.5f);

        // Reconstruct an upright world-horizontal/world-vertical room from the UV
        // island. This handles flipped UVs and keeps people standing upright while
        // the full-resolution pane mask naturally hides them behind frames.
        float2 uvDx = ddx_coarse(materialUV);
        float2 uvDy = ddy_coarse(materialUV);
        float2 planeDx = ddx_coarse(plane);
        float2 planeDy = ddy_coarse(plane);
        float validCenter;
        float validU;
        float validV;
        float2 centerOffset = UVDeltaToPlane(result.centerUV - materialUV, uvDx, uvDy, planeDx, planeDy, validCenter);
        float2 spanU = UVDeltaToPlane(float2(spanUV.x, 0.0f), uvDx, uvDy, planeDx, planeDy, validU);
        float2 spanV = UVDeltaToPlane(float2(0.0f, spanUV.y), uvDx, uvDy, planeDx, planeDy, validV);
        float2 rawRoomSize = float2(
            abs(spanU.x) + abs(spanV.x),
            abs(spanU.y) + abs(spanV.y));

        float radius = max(GetGeometry0().w, 1.0f);
        // Native glow maps can contain lit stone, trim or a complete facade, so
        // reconstructed size remains tightly related to the draw bounds. External
        // exact masks deliberately do not relax any layout test: they clip only.
        float nativeSaneSize = max(rawRoomSize.x, rawRoomSize.y) < radius * 3.25f ? 1.0f : 0.0f;
        float saneSize = nativeSaneSize;
        float usefulSize = min(rawRoomSize.x, rawRoomSize.y) >= 8.0f ? 1.0f : 0.0f;
        // Authored-mask acceptance must not depend on the manual procedural room
        // sliders. Otherwise changing Room Cell Width/Height changes whether the
        // automatic fit is accepted and makes the toggle appear to be a no-op.
        // Geometry-relative limits retain the facade/helper-mesh safety guard,
        // while absolute ceilings admit genuinely tall cathedral windows.
        float2 maximumGroupSize = min(
            max(radius * float2(2.80f, 3.30f), float2(220.0f, 300.0f)),
            float2(560.0f, 720.0f));
        float nativeBoundedGroup = all(rawRoomSize <= maximumGroupSize) ? 1.0f : 0.0f;
        float boundedGroup = nativeBoundedGroup;
        float usefulAspect = min(rawRoomSize.x, rawRoomSize.y) /
            max(max(rawRoomSize.x, rawRoomSize.y), 1.0f) >= 0.14f ? 1.0f : 0.0f;

        // Arches and stained-glass mullions can hide one coarse edge even when
        // the reconstructed span is entirely usable as a recessed background.
        // Keep the strict four-edge confidence for people/curtains, but expose a
        // broader background confidence so irregular windows never fall back to
        // flat emissive paint.
        float nativeBackgroundSaneSize = max(rawRoomSize.x, rawRoomSize.y) < radius * 5.50f ? 1.0f : 0.0f;
        float backgroundSaneSize = nativeBackgroundSaneSize;
        float2 maximumBackgroundSize = min(
            max(radius * float2(4.75f, 5.25f), float2(400.0f, 520.0f)),
            float2(1024.0f, 1280.0f));
        float nativeBoundedBackground = all(rawRoomSize <= maximumBackgroundSize) ? 1.0f : 0.0f;
        float boundedBackground = nativeBoundedBackground;
        float backgroundAspect = min(rawRoomSize.x, rawRoomSize.y) /
            max(max(rawRoomSize.x, rawRoomSize.y), 1.0f) >= 0.08f ? 1.0f : 0.0f;
        float backgroundEdgeConfidence =
            saturate((foundLeft + foundRight + foundDown + foundUp - 2.5f) * 2.0f);
        result.backgroundConfidence =
            validCenter * validU * validV * usefulSize *
            backgroundSaneSize * boundedBackground * backgroundAspect * backgroundEdgeConfidence;
        result.confidence = foundLeft * foundRight * foundDown * foundUp *
            validCenter * validU * validV * saneSize * usefulSize * boundedGroup * usefulAspect;
        result.roomSize = max(rawRoomSize, float2(8.0f, 8.0f));
        float2 centerPlane = plane + centerOffset;
        result.centerPlane = centerPlane;
        result.local = (plane - centerPlane) / result.roomSize + 0.5f;
        return result;
    }

    float2 GetAdaptiveRoomSize()
    {
        float2 manualRoom = max(GetRuntime1().yz, float2(48.0f, 72.0f));
        if (GetInterior0().z < 0.5f)
            return manualRoom;

        // Automatic mode is deliberately independent of stale/manual UI values.
        // This is the owner-validated medium-window calibration and is the stable
        // basis from which dedicated geometry can scale up or down.
        const float2 referenceRoom = float2(110.0f, 140.0f);

        float radius = max(GetGeometry0().w, 0.0f);
        if (radius <= 0.0f)
            return referenceRoom;

        // Calibrate the geometry sphere against the reference aperture diagonal.
        // Unlike the old min(reference, radius) rule, this can enlarge a room for
        // a genuinely large window as well as shrink it for a small one.
        float referenceRadius = max(length(referenceRoom) * 0.50f, 1.0f);
        float geometryScale = clamp(radius / referenceRadius, 0.55f, 2.85f);
        float layoutHint = GetAsset0().w;

        // Large walls often contain many windows and must retain the regular grid.
        // Dedicated window/pane geometry keeps scaling even when its sphere is
        // unusually large; unknown geometry blends back to the grid conservatively.
        float facadeBlend = layoutHint < -0.5f
            ? 1.0f
            : (layoutHint > 0.5f
                ? 0.0f
                : smoothstep(referenceRadius * 2.75f, referenceRadius * 4.25f, radius));
        geometryScale = lerp(geometryScale, 1.0f, facadeBlend);
        return referenceRoom * geometryScale;
    }

    float2 ConstrainRoomLocal(float2 baseRoomLocal, float2 requestedOffset)
    {
        // Once a ray reaches a room side wall it should remain on that wall rather
        // than sampling outside the atlas and making the entire interior vanish.
        // The small inset also prevents bilinear atlas bleed at grazing angles.
        float2 safeBase = saturate(baseRoomLocal);
        const float roomInset = 0.018f;
        float2 minimumOffset = roomInset.xx - safeBase;
        float2 maximumOffset = (1.0f - roomInset).xx - safeBase;
        return safeBase + clamp(requestedOffset, minimumOffset, maximumOffset);
    }

    float2 StableRoomParallaxOffset(
        float2 viewPlane,
        float facing,
        float layerDepth,
        float2 refractionOffset,
        float2 roomSize,
        float2 maximumTravel)
    {
        // Perspective parallax is proportional to tan(view angle). The former
        // direct division by facing became unbounded at oblique views, then the
        // room clamp stretched one atlas edge across most of the pane. Preserve
        // normal front-facing travel, but progressively flatten depth near a
        // grazing view and compress the final offset into an elliptical budget.
        float depthRetention = smoothstep(0.16f, 0.48f, facing);
        float2 angularSlope = viewPlane / max(facing, 0.20f);
        float slopeLength = length(angularSlope);
        angularSlope *= min(1.0f, 1.45f / max(slopeLength, 1.0e-4f));

        float2 requestedOffset =
            (-angularSlope * (layerDepth * depthRetention) + refractionOffset) /
            max(roomSize, 1.0f.xx);
        float2 safeTravel = max(maximumTravel, 0.01f.xx);
        float2 ellipticalOffset = requestedOffset / safeTravel;
        return requestedOffset * rsqrt(1.0f + dot(ellipticalOffset, ellipticalOffset));
    }

    void BuildPlane(float3 worldPosition, float3 N, out float3 horizontalAxis, out float2 plane)
    {
        const float3 worldUp = float3(0.0f, 0.0f, 1.0f);
        horizontalAxis = cross(worldUp, N);
        float axisLengthSq = dot(horizontalAxis, horizontalAxis);
        if (axisLengthSq < 1.0e-5f)
            horizontalAxis = float3(1.0f, 0.0f, 0.0f);
        else
            horizontalAxis *= rsqrt(axisLengthSq);
        plane = float2(dot(worldPosition, horizontalAxis), worldPosition.z);
    }

    SurfaceResult EvaluateSurface(
        float3 cameraRelativePosition,
        float3 viewDirection,
        float3 geometricNormal,
        float3 baseColor,
        float4 normalSample,
        float glowLuma,
        float2 materialUV,
        float viewDepth)
    {
        SurfaceResult r = (SurfaceResult)0;
        if (!IsCandidate() || SharedData::InMapMenu)
            return r;

        float3 N = normalize(geometricNormal);
        float pane = PaneMask(baseColor, normalSample, glowLuma, materialUV);
        r.paneMask = pane;
        r.tier = ResolveTier(N);
        r.occupancyEligible = r.tier >= 2.5f ? 1.0f : 0.0f;

        float distanceAbs = abs(viewDepth);
        float distanceFade = 1.0f - smoothstep(GetSurface0().x, max(GetSurface0().y, GetSurface0().x + 1.0f), distanceAbs);
        float facing = saturate(abs(dot(normalize(viewDirection), N)));
        float fresnelVisibility = smoothstep(0.025f, 0.18f, facing);

        float glassEnabled = GetEligibility0().w > 0.5f ? 1.0f : 0.0f;
        r.glassWeight = pane * GetGlass0().x * distanceFade * glassEnabled;

        float3 worldPosition = cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;
        float3 horizontalAxis;
        float2 plane;
        BuildPlane(worldPosition, N, horizontalAxis, plane);

        float coarse = ValueNoise2(plane * 0.014f + GetRuntime0().yy * 17.0f);
        float fine = ValueNoise2(plane * 0.051f + float2(13.7f, 4.9f));
        float streak = 0.5f + 0.5f * sin(plane.y * 0.062f + coarse * 5.1f + fine * 1.7f);
        streak = pow(saturate(streak), 5.0f);
        float dirt = saturate(coarse * 0.58f + fine * 0.28f + streak * 0.14f);

        r.roughness = saturate(GetGlass0().z + (dirt - 0.35f) * GetGlass1().x * 0.34f);
        r.f0 = saturate(0.040f + GetGlass0().y * 0.034f);
        r.transmission = saturate(GetGlass0().w * (1.0f - dirt * GetGlass1().x * 0.075f));
        r.normalRetention = lerp(1.0f, GetGlass1().z, r.glassWeight);

        float warpX = ValueNoise2(plane * 0.022f + float2(41.3f, 9.1f)) - 0.5f;
        float warpY = ValueNoise2(plane * 0.027f + float2(6.7f, 31.9f)) - 0.5f;
        r.normalWarp = float2(warpX, warpY) * GetGlass1().y * r.glassWeight * fresnelVisibility;

        r.debugMask = distanceFade * pane;
        return r;
    }

    Result Evaluate(
        float3 cameraRelativePosition,
        float3 viewDirection,
        float3 geometricNormal,
        float3 baseColor,
        float4 normalSample,
        float glowLuma,
        float2 materialUV,
        float viewDepth)
    {
        Result result = (Result)0;
        if (!IsCandidate() || SharedData::InMapMenu ||
            (SharedData::InInterior && GetInterior0().w < 0.5f))
            return result;

        float3 N = normalize(geometricNormal);
        float verticalSurface = Verticality(N);
        float facing = saturate(abs(dot(normalize(viewDirection), N)));
        // The room must remain legible from an oblique street view. Only collapse
        // the illusion when the pane is genuinely edge-on.
        float grazingFade = smoothstep(0.035f, 0.20f, facing);
        float distanceAbs = abs(viewDepth);
        float distanceFade = 1.0f - smoothstep(GetSurface0().x, max(GetSurface0().y, GetSurface0().x + 1.0f), distanceAbs);

        float pane = PaneMask(baseColor, normalSample, glowLuma, materialUV);
        float interiorPane = InteriorPaneMask(
            baseColor, normalSample, glowLuma, materialUV, pane);
        result.paneMask = pane;
        result.tier = ResolveTier(N);
        result.glassWeight = pane * GetGlass0().x * distanceFade * (GetEligibility0().w > 0.5f ? 1.0f : 0.0f);
        result.debugMask = distanceFade * interiorPane;

        // Glass-only tier remains optical glass. Shallow and full tiers both get a
        // recessed room; only a full, confidently bounded window may draw people.
        if (result.tier < 1.5f || verticalSurface <= 1.0e-4f || grazingFade <= 1.0e-4f || distanceFade <= 1.0e-4f || interiorPane <= 1.0e-4f)
            return result;
        bool fullInteriorTier = result.tier >= 2.5f;

        float3 worldPosition = cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz;
        float3 horizontalAxis;
        float2 plane;
        BuildPlane(worldPosition, N, horizontalAxis, plane);

        // Begin with the conservative geometry/world-grid fallback. Automatic mode
        // can replace it below only when the currently installed native glow map
        // reconstructs a sane complete aperture. External masks remain clip-only.
        float3 geometryCenter = GetGeometry0().xyz;
        float2 centerPlane = float2(dot(geometryCenter, horizontalAxis), geometryCenter.z);
        float2 localPlane = plane - centerPlane;
        float2 roomSize = GetAdaptiveRoomSize();
        float2 baseRoomGrid = localPlane / roomSize + 0.5f;
        float2 sizingReference = GetInterior0().z > 0.5f
            ? float2(110.0f, 140.0f)
            : max(GetRuntime1().yz, float2(48.0f, 72.0f));
        float referenceRadius = max(length(sizingReference) * 0.50f, 1.0f);
        bool singleAperture = GetAsset0().w > 0.5f ||
            (GetAsset0().w > -0.5f && GetGeometry0().w > 0.0f && GetGeometry0().w <= referenceRadius * 2.75f);
        float2 roomCell = singleAperture ? 0.0f.xx : floor(baseRoomGrid);
        float2 baseRoomLocal = singleAperture
            ? saturate(baseRoomGrid)
            : baseRoomGrid - roomCell;
        float2 absoluteRoomCell = singleAperture
            ? floor(centerPlane / max(roomSize, 1.0f.xx))
            : floor(centerPlane / roomSize) + roomCell;
        float roomSeed = Hash21(absoluteRoomCell + GetRuntime0().yy * 41.0f);
        result.authoredLayoutState = 0.0f;
        bool roomLayoutSafe = true;
        bool allowOccupants = fullInteriorTier;
        bool allowCurtains = true;

        // A native texture-derived guide is not an authored dependency: it follows
        // whatever compatible window/glow texture is actually installed. At a
        // coarse mip, wood/lead mullions disappear into one connected aperture.
        // All room layers then share the same reconstructed world-space centre,
        // size and seed. If reconstruction is uncertain, the room keeps the safe
        // geometry fallback but moving foreground layers are disabled so they can
        // never expose a bad fit.
        PaneLayout paneLayout = ResolveAuthoredPaneLayout(materialUV, plane);
        bool attemptedNativeLayout = GetInterior0().z > 0.5f && HasGameGlowTexture();
        bool useNativeBackground = attemptedNativeLayout && paneLayout.backgroundConfidence > 0.5f;
        bool useNativeLayers = attemptedNativeLayout && paneLayout.confidence > 0.5f;
        if (attemptedNativeLayout) {
            result.authoredLayoutState = useNativeLayers ? 3.0f : (useNativeBackground ? 2.0f : 1.0f);
            if (useNativeBackground) {
                roomSize = paneLayout.roomSize;
                centerPlane = paneLayout.centerPlane;
                baseRoomLocal = saturate(paneLayout.local);
                singleAperture = true;

                // Quantize only the seed anchor, not the visible coordinates. This
                // absorbs sub-texel derivative noise while preserving a completely
                // world-anchored room. Every mullioned sub-pane in the aperture
                // therefore selects the same room, curtain and activity sequence.
                float2 stableApertureCell = floor(centerPlane / 32.0f + 0.5f.xx);
                float familySalt = GetAsset0().x * 37.0f;
                roomSeed = Hash21(stableApertureCell + float2(familySalt, familySalt * 1.73f));
            }
            allowOccupants = allowOccupants && useNativeLayers;
            allowCurtains = allowCurtains && useNativeLayers;
        }

        float3 V = normalize(viewDirection);
        float2 viewPlane = float2(dot(V, horizontalAxis), V.z);
        float minRoomExtent = min(roomSize.x, roomSize.y);
        // Depth remains visible but cannot become a camera-following slide across
        // a narrow pane. The room-size-relative cap is the maximum physical recess;
        // the per-layer projector below applies a second angular/travel bound.
        float maxUsefulDepth = max(minRoomExtent * 0.34f, 10.0f);
        float configuredDepth = min(GetOptics0().x, maxUsefulDepth);
        float refractScale = saturate(minRoomExtent / 72.0f);
        float2 refractVector =
            (normalSample.xy * 2.0f - 1.0f) *
            GetOptics0().z * refractScale;

        // Curtains occupy a shallow layer close to the glass. They therefore
        // parallax less than the occupant and establish a visible depth hierarchy.
        float curtainDepth = configuredDepth * 0.04f;
        float2 curtainLocal = ConstrainRoomLocal(
            baseRoomLocal,
            StableRoomParallaxOffset(
                viewPlane, facing, curtainDepth, refractVector * 0.18f,
                roomSize, float2(0.035f, 0.030f)));
        float curtainSeed = Hash11(roomSeed * 67.13f + 4.7f);
        float curtainPresent = curtainSeed > 0.20f ? 1.0f : 0.0f;

        // Prefer the authored 4x4 cloth atlas. Querying dimensions makes an
        // unbound optional texture deterministic (0x0), preserving the analytic
        // fallback without changing the per-draw data ABI.
        uint curtainAtlasWidth = 0u;
        uint curtainAtlasHeight = 0u;
        uint curtainAtlasMipCount = 0u;
        WindowLifeCurtainAtlas.GetDimensions(
            0, curtainAtlasWidth, curtainAtlasHeight, curtainAtlasMipCount);
        float curtainAtlasReady = curtainAtlasWidth > 0u && curtainAtlasHeight > 0u
            ? 1.0f
            : 0.0f;
        float curtainTile = floor(Hash11(roomSeed * 47.91f + 19.7f) * 15.999f);
        float curtainTileY = floor(curtainTile * 0.25f);
        float curtainTileX = curtainTile - curtainTileY * 4.0f;
        float curtainMaximumMip = min(
            max((float)curtainAtlasMipCount - 1.0f, 0.0f), 7.0f);
        float2 curtainGradientX = ddx_coarse(curtainLocal) * 0.25f;
        float2 curtainGradientY = ddy_coarse(curtainLocal) * 0.25f;
        float curtainFootprint = max(
            length(curtainGradientX * float2((float)curtainAtlasWidth, (float)curtainAtlasHeight)),
            length(curtainGradientY * float2((float)curtainAtlasWidth, (float)curtainAtlasHeight)));
        float curtainMip = clamp(
            log2(max(curtainFootprint, 1.0f)) - 0.30f,
            0.0f,
            curtainMaximumMip);
        float curtainInset = min(
            max(3.0f, exp2(curtainMip) * 1.20f) / 512.0f, 0.12f);
        float2 curtainTileLocal = float2(curtainLocal.x, 1.0f - curtainLocal.y);
        curtainTileLocal = lerp(
            curtainInset.xx,
            (1.0f - curtainInset).xx,
            saturate(curtainTileLocal));
        float2 curtainAtlasUV =
            (float2(curtainTileX, curtainTileY) + curtainTileLocal) * 0.25f;
        float4 curtainAtlasSample = WindowLifeCurtainAtlas.SampleLevel(
            SampGlowSampler, curtainAtlasUV, curtainMip);

        // The old low-frequency wedges remain only as a no-asset fallback.
        float curtainOpen = lerp(0.105f, 0.235f, Hash11(roomSeed * 43.7f + 8.3f));
        float curtainWave =
            sin(curtainLocal.y * 15.0f + roomSeed * 13.0f) * 0.018f;
        float leftCurtain =
            1.0f - smoothstep(
                curtainOpen + curtainWave,
                curtainOpen + curtainWave + 0.045f,
                curtainLocal.x);
        float rightCurtain =
            smoothstep(
                1.0f - curtainOpen + curtainWave - 0.045f,
                1.0f - curtainOpen + curtainWave,
                curtainLocal.x);
        float curtainVertical =
            smoothstep(-0.04f, 0.025f, curtainLocal.y) *
            (1.0f - smoothstep(0.975f, 1.045f, curtainLocal.y));
        float curtainPleat =
            lerp(0.68f, 1.0f,
                0.5f + 0.5f * sin(curtainLocal.x * 61.0f + roomSeed * 7.0f));
        float analyticCurtainMask =
            saturate(max(leftCurtain, rightCurtain)) *
            curtainVertical * curtainPleat * curtainPresent;
        float curtainMask = lerp(
            analyticCurtainMask,
            saturate(curtainAtlasSample.a) * curtainPresent,
            curtainAtlasReady);
        // An installed curtain atlas is composited as real colour/alpha below.
        // Applying its alpha again as transmission made the cutout look like a
        // second black shadow. Keep occlusion only for the no-asset fallback.
        result.curtainOcclusion =
            curtainMask * interiorPane * verticalSurface * grazingFade * distanceFade *
            GetInterior0().x * (allowCurtains ? 1.0f : 0.0f) *
            (SharedData::InInterior ? 0.0f : 1.0f) * (1.0f - curtainAtlasReady);

        // Anchor the reveal to the aperture itself. The old independently shifted
        // full-window vignette was the remaining soft duplicate layer and visibly
        // followed the camera after the authored room had already been composited.
        float2 depthEdgeCoord = abs(baseRoomLocal - 0.5f) * 2.0f;
        float roomEdge = smoothstep(0.78f, 1.02f, max(depthEdgeCoord.x, depthEdgeCoord.y));
        float paneGradient = max(fwidth(pane), 1.0e-4f);
        float paneInterior = smoothstep(paneGradient * 0.75f, paneGradient * 2.75f, pane);
        float paneReveal = pane * (1.0f - paneInterior);
        float authoredRoomAvailable =
            GetAsset0().y > 0.5f && GetAsset0().z > 1.0e-4f ? 1.0f : 0.0f;
        result.roomDepthOcclusion =
            interiorPane * verticalSurface * grazingFade * distanceFade *
            GetInterior0().y * (SharedData::InInterior ? 0.0f : 1.0f) *
            (roomLayoutSafe ? 1.0f : 0.0f) *
            saturate(roomEdge * 0.72f + paneReveal * 0.30f) *
            (1.0f - authoredRoomAvailable);

        // The authored atlas is a true recessed back plane behind procedural
        // curtains and occupants. A stronger depth than the reveal layer produces
        // visible camera parallax while mask clipping keeps it inside real glass.
        float roomTile = SelectRoomTile(roomSeed, GetAsset0().x);
        float roomFloorLocal = 0.12f;
        [branch] if (GetAsset0().y > 0.5f && GetAsset0().z > 1.0e-4f)
        {
            float2 authoredRoomLocal = ConstrainRoomLocal(
                baseRoomLocal,
                StableRoomParallaxOffset(
                    viewPlane, facing, configuredDepth, refractVector,
                    roomSize, float2(0.28f, 0.22f)));
            float insideRoom =
                smoothstep(-0.035f, 0.025f, authoredRoomLocal.x) *
                (1.0f - smoothstep(0.975f, 1.035f, authoredRoomLocal.x)) *
                smoothstep(-0.035f, 0.025f, authoredRoomLocal.y) *
                (1.0f - smoothstep(0.975f, 1.035f, authoredRoomLocal.y));

            float tileY = floor(roomTile * 0.25f);
            float tileX = roomTile - tileY * 4.0f;

            uint atlasWidth;
            uint atlasHeight;
            uint atlasMipCount;
            WindowLifeRoomAtlas.GetDimensions(0, atlasWidth, atlasHeight, atlasMipCount);
            float maximumAtlasMip = min(max((float)atlasMipCount - 1.0f, 0.0f), 7.0f);
            float2 framedTileLocal = FrameAuthoredRoom(
                authoredRoomLocal, roomSize, roomTile, roomFloorLocal);
            float2 roomGradientX = ddx_coarse(framedTileLocal) * 0.25f;
            float2 roomGradientY = ddy_coarse(framedTileLocal) * 0.25f;
            float atlasFootprint = max(
                length(roomGradientX * float2((float)atlasWidth, (float)atlasHeight)),
                length(roomGradientY * float2((float)atlasWidth, (float)atlasHeight)));
            // Slightly negative bias retains furniture silhouettes without forcing
            // the 512-square source into every distant pixel. Clamp before atlas
            // cells become too small to remain independent.
            float atlasMip = clamp(
                log2(max(atlasFootprint, 1.0f)) - 0.45f,
                0.0f,
                maximumAtlasMip);
            float atlasCellInset =
                min(max(2.5f, exp2(atlasMip) * 1.15f) / 512.0f, 0.12f);

            // World-up room coordinates become top-down texture V. The inset grows
            // with the selected mip so filtered samples cannot borrow a neighbouring
            // room from the 4x4 atlas.
            float2 tileLocal = lerp(
                atlasCellInset.xx,
                (1.0f - atlasCellInset).xx,
                framedTileLocal);
            float2 atlasUV = (float2(tileX, tileY) + tileLocal) * 0.25f;

            float4 roomSample = WindowLifeRoomAtlas.SampleLevel(
                SampGlowSampler, atlasUV, atlasMip);

            // RoomAtlas already contains coherent furniture and lighting. Do not
            // resample it at another depth: even scalar extraction preserved a
            // recognizable shifted copy on high-contrast interiors.
            float3 layeredRoomColor = roomSample.rgb;

            // The installed cloth atlas is a genuine RGBA cutout. Analytic colour
            // exists only for a missing-asset fallback; atlas alpha is the exact
            // user-facing Curtain Opacity coverage over the shared aperture fit.
            float curtainPalette = Hash11(roomSeed * 29.17f + 2.3f);
            float3 analyticCurtainTint = curtainPalette < 0.34f
                ? float3(0.34f, 0.055f, 0.035f)
                : (curtainPalette < 0.67f
                    ? float3(0.10f, 0.19f, 0.105f)
                    : float3(0.105f, 0.115f, 0.22f));
            float3 curtainTint = lerp(
                analyticCurtainTint,
                max(curtainAtlasSample.rgb, 0.0f.xxx),
                curtainAtlasReady);
            float roomIllumination = max(
                dot(max(layeredRoomColor, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)),
                0.035f);
            float curtainColorWeight = saturate(
                curtainMask * GetInterior0().x * (allowCurtains ? 1.0f : 0.0f));
            layeredRoomColor = lerp(
                layeredRoomColor,
                curtainTint * (roomIllumination * 1.20f + 0.035f),
                curtainColorWeight);

            // Darkening the ray-clamped room edge turns the parallax boundary into
            // a readable side reveal, completing the window-box depth cue.
            layeredRoomColor *= 1.0f - roomEdge * 0.18f;

			// User-facing presentation controls operate only on the recessed room.
			// Pane classification, masks, glass response and silhouettes remain
			// untouched. A neutral 1.0/1.0 exactly preserves the accepted baseline.
			float contrast = clamp(GetPresentation0().x, 0.50f, 2.0f);
			float emission = clamp(GetPresentation0().y, 0.0f, 3.0f);
			layeredRoomColor = max(
				(layeredRoomColor - 0.18f.xxx) * contrast + 0.18f.xxx,
				0.0f.xxx) * emission;
			result.roomColor = layeredRoomColor;
            result.roomColorWeight =
                roomSample.a * insideRoom * interiorPane * verticalSurface * grazingFade * distanceFade *
                GetAsset0().z * (roomLayoutSafe ? 1.0f : 0.0f) *
                (SharedData::InInterior ? 0.0f : 1.0f);
        }

        if (!allowOccupants)
            return result;

        float duration = SharedData::InInterior
            ? lerp(8.0f, 19.0f, Hash11(roomSeed * 79.13f + 11.7f))
            : lerp(21.0f, 49.0f, Hash11(roomSeed * 79.13f + 11.7f));
        duration /= max(GetRuntime1().w, 0.1f);
        float eventClock = (SharedData::Timer + roomSeed * 137.0f) / max(duration, 1.0f);
        float eventIndex = floor(eventClock);
        float phase = frac(eventClock);

        float eventSeed = Hash11(roomSeed * 113.0f + eventIndex * 17.17f + 0.31f);
        float activityProbability = GetRuntime1().x > 0.5f
            ? 1.0f
            : saturate(SharedData::InInterior ? max(GetRuntime0().w * 0.75f, 0.24f) : GetRuntime0().w);
        float active = eventSeed < activityProbability ? 1.0f : 0.0f;
        float envelope = GetRuntime1().x > 0.5f ? 1.0f : ActivityEnvelope(phase) * active;
        result.activity = envelope;
        if (envelope <= 1.0e-4f)
            return result;

        float depthSelector = Hash11(eventSeed * 91.7f + 3.1f);
        float simulatedDepth =
            configuredDepth * lerp(0.45f, 0.85f, depthSelector);
        // Interior refraction is separate from the glass surface normal. The person
        // therefore moves behind the pane while grime/reflection stays attached to it.
        float2 roomLocal = ConstrainRoomLocal(
            baseRoomLocal,
            StableRoomParallaxOffset(
                viewPlane, facing, simulatedDepth, refractVector * 0.72f,
                roomSize, float2(0.24f, 0.19f)));

        float mode = Hash11(eventSeed * 31.9f + 5.2f);
        if (SharedData::InInterior)
            mode *= 0.52f;
        float direction = Hash11(eventSeed * 53.1f + 9.7f);
        float pose = Hash11(eventSeed * 71.3f + eventIndex * 0.73f);
        float variant = Hash11(eventSeed * 157.7f + roomSeed * 23.9f);
        float personX = 0.5f + MotionX(phase, mode, direction) * 0.47f;
        // Anchor feet close to the authored lower boundary. The old centre-biased
        // value amplified small mask-bound errors and left people floating high.
        float personY = GetInterior0().z > 0.5f
            ? roomFloorLocal + max(GetOptics0().w, 0.50f) * 0.51f
            : 0.44f;
        personY += (Hash11(eventSeed * 19.7f) - 0.5f) * 0.035f;
        personY = clamp(personY, 0.36f, 0.76f);
        float2 p = roomLocal - float2(personX, personY);
        p.x *= clamp(roomSize.x / max(roomSize.y, 1.0f), 0.30f, 2.50f);

        float depthBlur = lerp(0.86f, 1.75f, depthSelector);
        float analyticPerson = PersonMask(
            p, pose, phase, variant,
            GetOptics0().y, GetOptics0().w, depthBlur, direction);

        // Authored semi-coloured people replace the repeated analytic icon when
        // the optional atlas is installed. Atlas alpha supplies the silhouette;
        // RGB is later mixed into the recessed room at a restrained strength.
        uint occupantAtlasWidth = 0u;
        uint occupantAtlasHeight = 0u;
        uint occupantAtlasMipCount = 0u;
        WindowLifeOccupantAtlas.GetDimensions(
            0, occupantAtlasWidth, occupantAtlasHeight, occupantAtlasMipCount);
        float occupantAtlasReady = occupantAtlasWidth > 0u && occupantAtlasHeight > 0u
            ? 1.0f
            : 0.0f;
        float occupantScale = max(GetOptics0().w, 0.50f);
        float2 occupantLocal = float2(
            p.x / (0.66f * occupantScale) + 0.5f,
            0.5f - p.y / (1.02f * occupantScale));
        if (direction < 0.5f)
            occupantLocal.x = 1.0f - occupantLocal.x;
        float occupantInside =
            step(0.0f, occupantLocal.x) * step(occupantLocal.x, 1.0f) *
            step(0.0f, occupantLocal.y) * step(occupantLocal.y, 1.0f);
        float occupantTile = floor(variant * 15.999f);
        float occupantTileY = floor(occupantTile * 0.25f);
        float occupantTileX = occupantTile - occupantTileY * 4.0f;
        float occupantMaximumMip = min(
            max((float)occupantAtlasMipCount - 1.0f, 0.0f), 7.0f);
        float2 occupantGradientX = ddx_coarse(occupantLocal) * 0.25f;
        float2 occupantGradientY = ddy_coarse(occupantLocal) * 0.25f;
        float occupantFootprint = max(
            length(occupantGradientX * float2((float)occupantAtlasWidth, (float)occupantAtlasHeight)),
            length(occupantGradientY * float2((float)occupantAtlasWidth, (float)occupantAtlasHeight)));
        float occupantMip = clamp(
            log2(max(occupantFootprint, 1.0f)) + 0.45f + GetOptics0().y * 4.0f,
            0.0f,
            occupantMaximumMip);
        float occupantInset = min(
            max(3.0f, exp2(occupantMip) * 1.25f) / 512.0f, 0.12f);
        float2 safeOccupantLocal = lerp(
            occupantInset.xx,
            (1.0f - occupantInset).xx,
            saturate(occupantLocal));
        float2 occupantAtlasUV =
            (float2(occupantTileX, occupantTileY) + safeOccupantLocal) * 0.25f;
        float4 occupantAtlasSample = WindowLifeOccupantAtlas.SampleLevel(
            SampGlowSampler, occupantAtlasUV, occupantMip);
        float authoredPerson = saturate(occupantAtlasSample.a) * occupantInside;
        float person = lerp(analyticPerson, authoredPerson, occupantAtlasReady);

        float pairSelector = Hash11(eventSeed * 143.1f + 1.9f);
        if (pairSelector > 0.88f && occupantAtlasReady < 0.5f) {
            float secondPhase = frac(phase + 0.31f + Hash11(eventSeed * 17.0f) * 0.22f);
            float secondX = 0.5f + MotionX(secondPhase, 0.34f, 1.0f - direction) * 0.42f;
            float2 p2 = roomLocal - float2(secondX, personY - 0.035f);
            p2.x *= clamp(roomSize.x / max(roomSize.y, 1.0f), 0.30f, 2.50f);
            float second = PersonMask(
                p2, 1.0f - pose, secondPhase, frac(variant + 0.37f),
                GetOptics0().y * 1.12f, GetOptics0().w * 0.94f,
                depthBlur * 1.18f, 1.0f - direction);
            person = max(person, second * 0.82f);
        }

        float depthTransmission = lerp(1.0f, 0.72f, depthSelector);
        float coverage = person * envelope * interiorPane * verticalSurface * grazingFade * distanceFade * depthTransmission;
        // Authored people are a real softly filtered RGBA layer. Occupant Opacity
        // controls coverage directly; it is not converted into black transmission
        // after the room composite, which was the source of the ghost appearance.
        float authoredOpacity =
            authoredPerson * occupantAtlasReady * envelope * interiorPane *
            verticalSurface * grazingFade * distanceFade *
            saturate(GetPresentation0().z);
        bool authoredComposited =
            authoredOpacity > 1.0e-4f && result.roomColorWeight > 1.0e-4f;
        if (authoredComposited) {
            float authoredLuma = max(
                dot(max(occupantAtlasSample.rgb, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f)),
                0.025f);
            float roomLuma = max(
                dot(max(result.roomColor, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f)),
                0.035f);
            float3 authoredTint = max(occupantAtlasSample.rgb, 0.0f.xxx) *
                clamp(roomLuma * 0.88f / authoredLuma, 0.65f, 3.0f);
            result.roomColor = lerp(
                result.roomColor,
                authoredTint,
                saturate(authoredOpacity));
        }
        float shadowStrength = SharedData::InInterior
            ? clamp(GetRuntime0().z * 0.42f + 0.08f, 0.10f, 0.40f)
            : GetRuntime0().z;
        float fallbackOpacity = lerp(1.0f, saturate(GetPresentation0().z), occupantAtlasReady);
        result.occlusion = authoredComposited
            ? 0.0f
            : saturate(coverage * fallbackOpacity * shadowStrength);
        return result;
    }
}

#endif  // PIXL_WINDOW_LIFE_HLSLI
