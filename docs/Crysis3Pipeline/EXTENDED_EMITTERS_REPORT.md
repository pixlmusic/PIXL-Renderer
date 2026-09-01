# PIXL Extended Emitters: Finite-Sphere Audit

## Release decision

**Classification: C — controlled prototype for a later isolated cache cycle.**

PIXL already transports a bounded finite-source quantity from a Skyrim light form to every relevant local-light consumer. The missing feature is angular emitter extent in the BRDF: inverse-square attenuation uses `sizeBias`, but direct-light diffuse and specular evaluation still receives the direction to a mathematical point.

No renderer code was changed in this pass. Implementing the useful approximation requires changing `distribution/Shaders/Lighting.hlsl` and normally `Common/LightingEval.hlsli`. Those files feed the broad Lighting pixel-shader family. PIXL's dependency tracker correctly associates shared includes with compiled descriptors, so deploying that change would invalidate a large set of opaque material permutations while the release cache is already being stabilized. It would also alter every eligible inverse-square local-light highlight without an in-game A/B validation cycle.

This is a deliberate release-safety decision, not a DX11 limitation. The design below requires no new pass, resource, LUT, `LightData` field, register, global setting, or automatic light-shape inference.

## Existing data path

```text
TESObjectLIGH::data.fov
    |
    | NaturalLighting::SetExtLightData
    v
RuntimeLightDataExt::size (clamped 0.01 .. 50)
    |
    | NaturalLighting::ProcessLight
    v
LightData::sizeBias = 0.5 * 0.8 * 70^2 * size^2
    |
    +--> strict lights, b3 (menus/non-world)
    +--> clustered lights, t35 (world)
             |
             +--> attenuation denominator
             +--> cluster contact-light importance
             +--> proposed angular emitter radius
```

`NaturalLighting::SetExtLightData` treats authored `fov >= 50` as the historical default and maps it to `sqrt(2)`; otherwise it uses the authored value. `ProcessLight` writes the squared finite-source softening term into the existing ABI. Both the strict-light setup and the world clustered-light upload call the same function, so the representation is consistent.

The CPU and HLSL layouts agree:

```text
float3 color; float fade;
float radius; float invRadius; float fadeZone; float sizeBias;
float4 positionWS;
uint4 roomFlags;
uint lightFlags; uint shadowMaskIndex; uint pad0; uint pad1;
```

The C++ struct remains `alignas(16)`, is used as the structured-buffer stride, and is mirrored by `RadiantGrid/Common.hlsli`. No ABI change is needed.

## Diagnosis

### What works now

- `NaturalLighting.cpp:64` computes `sizeBias` from the authored source size.
- `LightingEval.hlsli:54-65` evaluates finite-emitter inverse-square attenuation as `k / (distance^2 + max(sizeBias, minimumDistance^2))`, followed by a smooth radius cutoff.
- `Lighting.hlsl:4428-4438` uses that response for strict and clustered world lights, blended by the existing physical-falloff control.
- `ClusterCullingCS.hlsl:58-64` includes `sizeBias` in the cluster-wide importance score used to choose bounded local contact-shadow candidates.
- Cluster intersection continues to use the light influence radius, which is correct: emitter radius must not expand an already-derived retirement radius a second time.
- Diffuse, specular, transmission, coat, skin, hair, wetness, and legacy paths all converge through `CreateDirectLightingContext` and `PhysicalLighting::EvaluateDirect`.

### What remains point-like

`Lighting.hlsl:4419-4421` constructs the centre vector and distance. At `4456` it normalizes that vector, and at `4521-4523` it creates the direct-light context from that one direction. `CreateDirectLightingContext` then constructs `halfVector = normalize(viewDir + lightDir)`.

Consequently `sizeBias` prevents a singular near-field attenuation spike, but it does not widen a highlight. A fireplace, brazier, or broad practical light can illuminate with a softened inverse-square response while polished armour still shows a point-source-sized GGX highlight.

Particle representative lights currently have `sizeBias == 0` and are marked `Simple`; this proposal intentionally does not infer shapes for them. Particle aggregation and particle-light policy are outside this report.

## Bounded implementation design

The safest first implementation is an **effective-sphere roughness convolution**, not a full LTC area-light system and not closest-point direction substitution.

For an inverse-square light only:

```hlsl
float sourceRadius = sqrt(max(light.sizeBias, 0.0f));
float sinAngularRadius = saturate(sourceRadius / max(lightDist, sourceRadius + 1.0f));
float sourceSlope = min(0.5f * sinAngularRadius, 0.35f);

float alpha = material.Roughness * material.Roughness;
float broadenedAlpha = sqrt(alpha * alpha + sourceSlope * sourceSlope);
float broadenedRoughness = sqrt(saturate(broadenedAlpha));
```

Use a local copy of `MaterialProperties` for the current light and change only its specular roughness fields:

- `Roughness` for legacy physical, Material Forge base, skin, and hair adapters;
- `CoatRoughness` for Material Forge's clear coat;
- the wetness roughness passed to `EvaluateWetnessLighting`.

Do **not** alter:

- centre `lightDir` or `NdotL`;
- diffuse irradiance;
- transmission direction;
- shadow lookup direction;
- contact-shadow ray direction;
- POM self-shadow direction;
- attenuation, intensity, clustering, or room containment.

This keeps the GGX distribution normalized while convolving in a bounded estimate of source angular variance. It avoids the common closest-point-sphere shortcut, which broadens the lobe but retains an unrealistically high peak and can move diffuse/shadow direction near a large emitter.

### Required gates

Apply the convolution only when all are true:

1. rendering a world/reflection scene, not inventory/loading/menu previews;
2. `NATURAL_LIGHTING` is compiled;
3. the light has `LightFlags::InverseSquare`;
4. the existing physical-falloff blend is greater than zero;
5. `sizeBias` exceeds the existing minimum-distance floor;
6. the source subtends a non-trivial but bounded angle.

Blend the broadened result by `GetPhysicalLocalLightFalloffStrength()` so current profiles with physical falloff disabled remain byte-for-byte equivalent at runtime.

### Why no new user control

The authored light size already is the physically meaningful control, and the existing physical local-light falloff blend is the compatibility gate. A second global "area-light strength" slider would let attenuation and angular extent drift apart and would add UI/config debt. If individual sources need correction, they should be corrected in authored light data or an eventual PIXL-owned light profile—not by global shape inference.

## Rejected alternatives

### Full rectangular/disc/tube LTC evaluation

Rejected for this release pass. It needs orientation, shape dimensions, and usually LUT resources. `LightData` currently contains none of those. Inferring shapes automatically from nearby emissive geometry would be a new tracking/classification subsystem with broad compatibility risk.

### Closest point on sphere to the mirror ray

Deferred. It is cheap, but changing `lightDir` affects diffuse, transmission, POM and shadow assumptions. Changing only `halfVector` requires auditing every specialized material path and does not lower the integrated highlight energy. Roughness convolution is the more controlled first prototype.

### Reusing `radius` as emitter size

Rejected. `radius` is the smooth retirement/influence radius, not physical source size. It is deliberately scaled beyond the perceptible inverse-square tail and would create huge, camera-filling highlights.

### Encoding a new field into padding

Unnecessary. `sizeBias` already carries the required scalar. Repurposing padding would also complicate strict/structured ABI validation and future compatibility.

## Cache and baseline impact

The implementation point is the shared Skyrim `Lighting.hlsl` pixel path. `ShaderCache.cpp:1438-1458` composes global module defines with per-descriptor defines, while `1485-1494` records include dependencies. A change to `LightingEval.hlsli` therefore invalidates all Lighting descriptors that included it; placing the helper only in `Lighting.hlsl` still changes the same broad shader source timestamp and compiled family.

This cannot be made descriptor-local with the current data contract because all eligible local lights are evaluated inside the shared Lighting shader. A source-only compile-time define disabled by default would still change source deployment and force cache work without delivering a testable feature.

Expected shader cost when eventually enabled is small per evaluated inverse-square light: one square root for source radius, bounded scalar math, and one extra local `MaterialProperties` copy that the compiler should scalarize. Actual GPU time must be measured; no timing claim is made here.

## Source/live authority check

At audit time the canonical and live copies matched byte-for-byte for:

- `distribution/Shaders/Common/LightingEval.hlsli`
- `distribution/Shaders/Common/LightingCommon.hlsli`
- `distribution/Shaders/Lighting.hlsl`
- `pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli`

No live shader or staged runtime file was modified.

## Strict shader validation

FXC 10.0.26100.0 compiled the current source with `/Ges /WX /O3`, entry `main`, target `ps_5_0`, against the active live include tree:

| Permutation | Result |
| --- | --- |
| `PSHADER RADIANT_GRID NATURAL_LIGHTING` | PASS |
| `PSHADER RADIANT_GRID NATURAL_LIGHTING DEFERRED MATERIAL_FORGE WORLD_PROBES` | PASS |
| `PSHADER RADIANT_GRID NATURAL_LIGHTING SKINNED HAIR STRAND_SHADING` | PASS |
| `PSHADER RADIANT_GRID NATURAL_LIGHTING SKINNED FACEGEN PIXL_SKIN DEFERRED` | PASS |

Artifacts are under `build/shader-validation/crysis-extended-emitters-audit/` and are build output, not distribution files.

## Per-file review

### `engine/Modules/NaturalLighting.cpp`

- **Role / active status:** Hooks point-light creation and luminance selection; registered as a core module and called by both RadiantGrid upload paths.
- **Dependencies:** Skyrim `TESObjectLIGH`/`NiLight` runtime layout, `RuntimeLightDataExt`, `RadiantGrid::LightData`.
- **Quality:** Existing size clamp, safe radius radicand, minimum radius, and smooth cutoff are robust. `sizeBias` is exactly the reusable finite-emitter signal.
- **Fidelity/performance:** No extra CPU work is required for the proposed sphere approximation.
- **Correctness/security:** Null checks and numeric clamps are present; no unrelated system access or security concern found.
- **Change:** None. Preserved the current baseline.
- **Future:** Clarify/document the authored `fov`-to-size convention using verified Skyrim light-form semantics before exposing per-source tooling.

### `engine/Modules/NaturalLighting.h`

- **Role / active status:** Module contract and physical attenuation constants.
- **Quality:** Constants are centralized; inverse-square tail extension is clearly documented.
- **Correctness/security:** No ABI or security issue found.
- **Change:** None.
- **Future:** Add unit tests for monotonic radius/attenuation if a host-side test target is introduced.

### `engine/Modules/NaturalLighting/Common.h`

- **Role / active status:** Overlay of Skyrim light runtime storage used by the creation hook and upload.
- **Quality/risk:** High ABI sensitivity because it reinterprets engine memory. This pass deliberately did not add or reorder fields.
- **Security:** Expected game-runtime integration only; no suspicious behavior.
- **Change:** None.
- **Future:** Add offset/size assertions where CommonLib exposes stable layout facts.

### `engine/Modules/RadiantGrid.cpp`

- **Role / active status:** Builds strict and world light records and uploads the structured buffer.
- **Quality:** Both routes call `NaturalLighting::ProcessLight`, so finite-source data does not diverge by scene type. World clustering uses the same record.
- **Performance:** No per-frame emitter classification should be added here for the first prototype.
- **Correctness/security:** Existing bounds checks and 1024-light cap are appropriate. No security issue found.
- **Change:** None by this audit; pre-existing particle aggregation edits were preserved.
- **Future:** Profile light-count/cluster occupancy before any shape-light expansion.

### `engine/Modules/RadiantGrid.h`

- **Role / active status:** CPU light ABI, flags, buffers, settings, and hooks.
- **Quality/risk:** The current 80-byte `LightData` layout has all data needed for a scalar sphere approximation. No ABI extension is justified.
- **Change:** None.
- **Future:** Shape/orientation fields belong only in a versioned future area-light buffer, not hidden in current padding.

### `pipeline/Natural Lighting/Kernels/NaturalLighting/NaturalLighting.hlsli`

- **Role / active status:** Legacy-compatible selection between regular and inverse-square attenuation for RadiantGrid lights.
- **Quality:** Numerically guarded and consistent with the CPU formula.
- **Change:** None.
- **Future:** Consolidate duplicate attenuation math with `LightingEval.hlsli` only if compiler output and all call sites can be proven equivalent.

### `pipeline/Radiant Grid/Kernels/RadiantGrid/Common.hlsli`

- **Role / active status:** HLSL mirror of `LightData`, cluster records, and flags.
- **Quality:** CPU/HLSL order matches; `sizeBias` is already in a stable scalar slot.
- **Change:** None.
- **Future:** None required for effective spheres.

### `pipeline/Radiant Grid/Kernels/RadiantGrid/RadiantGrid.hlsli`

- **Role / active status:** Binds strict/clustered records and resolves portal/room eligibility.
- **Quality:** The emitter proposal does not bypass containment or shadow masks.
- **Change:** None.
- **Future:** Preserve room filtering in every future shape-light path.

### `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterBuildingCS.hlsl`

- **Role / active status:** Builds logarithmic view-space cluster AABBs from the current projection contract.
- **Quality:** Emitter angular extent does not require cluster geometry changes because `radius` already bounds influence.
- **Change:** None.
- **Future:** None specific to sphere highlights.

### `pipeline/Radiant Grid/Kernels/RadiantGrid/ClusterCullingCS.hlsl`

- **Role / active status:** Assigns lights and selects two contact-shadow candidates per cluster.
- **Quality:** `sizeBias` already reduces near-source score singularities. Expanding culling by emitter radius again would over-cover clusters.
- **Change:** None.
- **Future:** Validate candidate stability for very large authored light sizes.

### `distribution/Shaders/Common/LightingCommon.hlsli`

- **Role / active status:** Shared direct-light context/material structures used by all Lighting material families.
- **Quality:** Adding emitter fields here would fan out across specialized models. The proposed local material copy avoids that churn.
- **Change:** None.
- **Future:** A versioned `DirectEmitterContext` is appropriate only if real shape lights are adopted.

### `distribution/Shaders/Common/LightingEval.hlsli`

- **Role / active status:** Shared attenuation and material-family direct-light evaluation.
- **Quality:** Attenuation is already finite and safely blended. Specular lacks angular extent.
- **Change:** None; unrelated existing fur changes were preserved.
- **Future:** Host the small roughness-convolution helper after a scheduled cache cycle.

### `distribution/Shaders/Lighting.hlsl`

- **Role / active status:** Actual Skyrim lighting raster shader and the only coherent place to apply per-light emitter extent before every material adapter.
- **Quality:** The local-light loop correctly keeps attenuation, shadowing, POM and material evaluation ordered. It provides distance, flags, `sizeBias`, and a material copy point.
- **Change:** None; unrelated existing hair, accumulation, parallax, and fur work was preserved.
- **Future:** Prototype the gated local material convolution immediately before `PhysicalLighting::EvaluateDirect`, then A/B metal, wet, skin, hair and coat materials.

### `engine/ShaderCache.cpp` (scoped cache-impact review)

- **Role / active status:** The inspected path composes Lighting defines, compiles source, tracks include dependencies, and invalidates dependent descriptors. This was not a whole-file release/security review, so the master matrix remains unmarked for this file.
- **Quality:** Dependency tracking explains why this otherwise small shader edit is a broad cache event. Descriptor-scoping cannot isolate a feature used by every local-light material.
- **Change:** None.
- **Future:** A separately versioned shader-library cache could reduce future shared-include rebuild pain, but that is release-risk architecture work.

## Acceptance test for the future prototype

1. Capture a fixed exposure/material setup with a candle, torch, brazier, fireplace, and ordinary point light.
2. On polished metal, verify increasing authored emitter size widens and lowers the highlight rather than merely changing brightness.
3. Verify diffuse gradients, light cutoff, shadows, contact shadows, POM shadows, and room containment do not move.
4. Verify inverse-square-off and physical-falloff-strength-zero configurations reproduce the previous image.
5. Verify menu/inventory/loading previews are unchanged.
6. Test skin, hair, wet surfaces, clear coat, glass-adjacent materials, and snow for energy spikes.
7. Orbit and translate with TAA, DLSS, and FSR; the highlight must not pop when crossing cluster boundaries.
8. Profile a dense interior before/after; reject the implementation if register pressure or per-light math causes disproportionate cost.

## Five future visual improvements

1. Implement and A/B the bounded sphere/GGX convolution during a scheduled Lighting cache cycle.
2. Add explicit disc/rectangle/tube records only for PIXL-owned or reliably authored emitters.
3. Give broad emitters matching penumbra control without changing Skyrim's authoritative shadow maps.
4. Connect WindowLife portal luminance to explicit, bounded window emitter records rather than guessed nearby points.
5. Let aggregate fire sources provide one coherent emitter extent so armour receives a broad flame-shaped highlight without per-particle lights.

## Five future performance improvements

1. Compute the scalar angular spread once per local-light loop iteration and reuse it across base, coat, wetness, skin, and hair lobes.
2. Early-out source convolution when `sizeBias` is below the existing minimum-distance floor.
3. Preserve current cluster bounds and lists; never add separate shape-light cluster walks for the sphere tier.
4. Measure compiler scalarization of the local material copy and replace it with two roughness scalars if register pressure rises.
5. Introduce an explicit shape-light budget and LOD only if later LTC lights are adopted.

## Five future feature / research ideas

1. Versioned LTC rectangle/disc evaluation for explicitly tagged fireplaces, windows, and forges.
2. A PIXL-owned light-profile format containing shape, orientation, dimensions, and compatibility fallback.
3. Emissive-geometry-to-light association performed at load/classification time, never through per-frame inference.
4. Portal-aware emitter clipping that reuses RadiantGrid room masks for elongated sources.
5. Temporal representative-shape fitting for dynamic fire clusters, with stable identity and conservative energy bounds.

## Conclusion

PIXL is already closer to an extended-emitter architecture than the feature list implied: source size is captured, physically softened, uploaded, and consumed consistently. The remaining sphere-emitter specular improvement is technically straightforward and does not require a new data ABI. Its risk is deployment breadth, not algorithmic complexity. Schedule it for a deliberate Lighting cache cycle with metal/fireplace A/B captures rather than forcing it into the current release test build.
