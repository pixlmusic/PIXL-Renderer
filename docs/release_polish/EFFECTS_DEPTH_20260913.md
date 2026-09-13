# Effects-only virtual depth: controlled live-test implementation

## Black-preview / weak-object follow-up

Owner reports black preview and weak object POM; Markarth remains fixed.
The absolute UV Jacobian threshold (1e-8) rejected valid small UV derivatives.
Replaced it with a relative conditioning check and finite guard. Offset output
now transforms world displacement as a vector rather than subtracting two
nonlinear depth conversions. Effects-only object offsets may be signed (the
previous no-protrusion clamp discarded the positive relief half of centered
synthetic POM). Hardware depth remains off. Preview uses a square-root response
so sub-unit offsets are visible; colour does not represent literal displacement
scale. These are identified code-level loss mechanisms; actual live resolution
is not yet confirmed.

Scoped active-file review: Lighting.hlsl (math/finite guards/visibility safety),
DeferredCompositeCS.hlsl (diagnostic sensitivity), MaterialLayers Module.ini
(1-3-6 targeted cache invalidation). All three modified and reviewed for
correctness, security, fidelity and performance; no extra resource/ABI changes.
Future: automated GPU offset readback, scale-conditioned Jacobian tests, and
direct ray-travel output to further reduce world-coordinate cancellation.

Five live-only tuning fields changed, with all other JSON verified identical:
ObjectAutoHeightScale .0051 -> .01; ObjectMaxTexelShift 4 -> 10;
AutoMinTexelShift .4 -> .2; ObjectVirtualDepthStrength missing/off -> .5;
ObjectVirtualDepthMaxWorld missing -> 4 (same as compiled fallback).
This can increase auto-POM marching work/relief, requiring a motion/performance
test. Release defaults were not promoted. Source/live shader hashes verified;
no DLL change was needed, and no manual cache deletion occurred.

Six strict FXC cases passed (auto/authored/terrain/alpha/GroundResponse PS and
full composite CS); disassembly rejects any SV_Depth in the tested PS paths.
Evidence: build/effects-depth-fix-2ea1b4f596664d40873575b101ff8969.
UV scale-invariance checked for four scales and zero determinant rejection.
Four live files deployed with backups at
build/deployment-backups/Effects-Depth-Fix-20260913-131738-24a3adbdb45244f395f1fe4c89c89401.
User config hash was checked before replacement; fog/upscaling/unrelated values
are unchanged. Release ZIP remains unchanged. Live visual validation pending.

Owner confirmed disabling authored hardware-depth reconstruction fixed the
Markarth wall. That fix remains: neither authored nor synthetic parallax in
this implementation writes SV_Depth or changes the engine DSV.

## Data flow and bounds

Lighting POM UV intersection -> bounded ray displacement -> signed linear-view
offset in Masks2.y -> post-geometry compute resolve -> separate R32_FLOAT depth
-> HybridGI depth prefilter and deferred composite. Raster/decal visibility,
motion vectors, forward water, fog, upscaling guides and collision keep their
original depth paths. This is not universally shared scene depth or silhouette
displacement. Model-space-normal, skinned, alpha-tested and LOD draws are
excluded. Zero offsets preserve the exact source depth in the resolve.

Masks2 changes from R16_UNORM to R16G16_FLOAT: x remains 1-vertexAO, y is
signed relief. Lighting writes the offset; grass/effect shaders already write
zero to y. DeferredComposite is the only existing sampled Masks2 consumer and
now declares float2. No additional MRT/register slot is allocated. The resolve
uses CS t0/t1/u0 and b5 after RTVs/DSV are unbound, then unbinds its resources.
Only its two explicit consumers receive virtual depth; the global scene-depth
helper is deliberately unchanged.

PS b9 remains 304 bytes. The final two formerly unused floats become object
strength/max-world; static assertions enforce offsets 296/300. Version advances
2 -> 3, invalid old/new shader-buffer pairs fail the validity check. Old pad3/4
JSON fields are ignored; missing new fields default to strength 0 and limit 4.
Existing terrain settings are retained. MaterialLayers 1-3-5 selectively
invalidates affected Lighting PS stages; no full shader-library deletion.

The resolve clamps relief to 64 world units and 10% of view distance, rejects
nonfinite/invalid projected depth, and preserves sky. Its texture is created
on demand/resized to the input dimensions. Missing resources/compile/allocation
failures fall back to normal depth, with a latched warning instead of frame
spam. All controls off skips the dispatch. GI temporal history is invalidated
when depth parameters change. A session-only debug checkbox uses reserved
composite mode 100 (not HybridGI settings): blue recess/orange protrusion/black
zero. It displays the generated offset before the resolve's near-camera cap.

## File accounting

All following active files reviewed and modified in this scope. Security,
fidelity, performance and maintainability checks covered the changed paths;
this is not a claim of a new exhaustive whole-repository audit.

| File | Purpose/dependencies, findings and validation |
| --- | --- |
| engine/Modules/MaterialLayers.h | Runtime resources/settings. 304-byte ABI and final field offsets asserted; COM ownership; reset/fallback state. |
| engine/Modules/MaterialLayers.cpp | Settings serialization/UI -> PS b9; effects CS creation/dispatch, guarded resource recreation, explicit fallback, GI history reset. No external access beyond local shader loading. |
| pipeline/Material Layers/Kernels/MaterialLayers/MaterialLayersTuning.hlsli | Version-3 b9 mirror/getters; object controls in former padding. Strict FXC fixtures cover the new reads. |
| distribution/Shaders/Lighting.hlsl | Computes bounded effects offset without changing raster visibility/motion. Numeric finite output guard; excluded cutouts/actors/LOD. |
| pipeline/Material Layers/Kernels/MaterialLayers/EffectsDepth.hlsl | New bounded full-screen CS; t0/t1/u0 isolated to its pass, b5 camera conversion. Strict FXC, depth round-trip/zero tests. |
| engine/Deferred.cpp | RG16 masks allocation; post-geometry resolve before GI/composite; reserved diagnostic selection. Forward/engine DSV unchanged. |
| distribution/Shaders/DeferredCompositeCS.hlsl | float2 masks reader, virtual-depth world reconstruction, diagnostic colour. Full exterior/interior/minimal CS fixtures compile. |
| engine/Modules/HybridGI.cpp | Only depth prefilter input changes, native helper fallback preserved; existing SRV cleanup verified. |
| pipeline/Material Layers/Module.ini | Narrow cache invalidation version 1-3-5; no global ABI invalidation. |

Additional read-only review: engine/Utils/D3D.cpp (global depth selection left
unchanged; compiler injects COMPUTESHADER), Common/SharedData.hlsli (projection
inverse), Common/GBuffer.hlsli, existing grass/effect Masks2 writers and deferred
blend state. Generalized hardware-depth rewrite was rejected after the confirmed
Markarth regression. Global depth replacement was rejected to limit exposure.

## Validation and remaining risks

Shader evidence: build/effects-depth-b6503d6fca0241f49bb4186d0185d5f0.
Seven PS cases: auto, authored, terrain, alpha, forward, skin, model-space.
Ten extra VS/PS cases: GroundResponse terrain, windows/glow, environment, PBR,
LOD. Three composite CS variants: exterior, interior, minimal. One resolve CS.
Authored/auto/terrain disassembly has no SV_Depth. Twenty numeric projection
round-trip cases cover positive/negative/zero offsets and near-distance limits.
Initial fixture errors (missing COMPUTESHADER define, HLSL reserved identifier,
PowerShell framework lacking Math.Clamp) were corrected before final checks.

No GPU visual/performance result is claimed. Added storage is two bytes/pixel
for Masks2 plus four bytes/pixel when the effects texture exists. An enabled
resolve adds a full-screen compute dispatch. Remaining live checks: Markarth,
terrain/GroundResponse, camera motion/teleport, GI history and light leaks,
interior/exterior, dynamic resolution, water boundaries and effects disabled.
Small detail can be attenuated by GI's depth filtering; strength is not a
promise of visibly larger geometry. Keep this as a test build, not a repackaged
public release, until owner comparison confirms quality/performance.

## 5 Future Visual Improvements

Final validation/deployment: canonical PIXL-12C Release build passed after the
last UI/history changes. Six live files deployed with SHA256 verification while
Skyrim was closed. Backup:
build/deployment-backups/Effects-Depth-20260913-100825-25d31529cf3942ef89758f4532086dc9.
DLL SHA256: 0C91CD0CE4F5855CD954016DB6CB4DE9ADBD213A1AD6E22DE04E33F12FB9501B.
Configuration and cache files were not modified, and the release ZIP was not
updated. Visual confirmation is still required.

Depth-boundary confidence; authored-height metric calibration; temporal relief
confidence; contact-aware GI sampling; normal-aware depth reconstruction.

## 5 Future Performance Improvements

Profile resolve bandwidth; skip unchanged/offscreen tiles; pack offsets more
tightly if precision permits; share depth-prefilter work; release idle textures.

## 5 Future Feature / Research Ideas

Dedicated effect-depth API; opt-in AO consumers; material exclusions; validated
relief motion vectors; depth-prepass-compatible true displacement.
