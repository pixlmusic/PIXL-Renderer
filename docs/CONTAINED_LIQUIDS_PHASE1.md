# Contained Liquids Phase 1

> Superseded scope note, 2026-09-26: Phase 1's exact minor-healing-bottle gate
> has been expanded into a conservative bottle-family prototype. The bounded
> replay/crop design remains, but live potion effects now select health (red),
> magicka (blue), stamina (green), poison, and generic-magic profiles; wine,
> alcohol, and ordinary liquids use non-emissive defaults with a separate optional
> emission control. Current architecture and validation are in
> `docs/release_polish/DISTANT_LIFE_AND_CONTAINED_LIQUIDS_20260926.md`.

## Implementation plan and asset evidence

Read CODEX_START, README and the full phase prompt before changes. Reference
sources were inspected for conceptual volume/force flow only; none are copied
or distributed. No redistribution rights were assumed.

The actual Steam Skyrim Meshes0 archive contains
`meshes/clutter/potions/potionhealthlesser.nif`. Its single BSTriShape is
`PotionHealthLesser:0`, with diffuse `textures/clutter/GenericPotion01.dds`.
Local vertex bounds are approximately (-4.591,-4.240,0) to
(4.591,4.240,16.093). The neck begins around z=8; the proxy deliberately occupies
only the bulb, centered at z=3.7, radii (3.25,3.0,3.4). Extracted assets remain in
ignored build scratch for inspection; no Bethesda asset is shipped.

Hook plan: chain BSLightingShader SetupGeometry after existing modules, upload
private t120 structured data and t121 scene crop; evaluate volume only in forward
Lighting PS permutations. Keep native mesh, transforms, depth, blending, motion
vectors and all shader resource conventions intact. Every Lighting draw clears
the two private slots before classification. No new fullscreen pass. Existing
water refraction and CameraSuite late frame copies do not have a guaranteed
fresh pre-bottle lifetime here, so use one reusable bounded 512-square crop of
the current colour target. Reject MSAA, reflection, deferred and unsupported
target formats. Never sample a bound output resource.

World up is Skyrim +Z (existing terrain/water height contracts), not the Unity
reference's +Y. CPU uses absolute object transforms for derivatives; camera
motion never enters dynamics. GPU center is native camera-relative and works
with the existing FrameBuffer matrix contract, independently of RenderOrigin.

Status: EXPERIMENTAL, default disabled. Live visual verification required.

## Final architecture and exact matching

The original NIF is alpha-tested opaque geometry, not ordinary transparent glass.
A forward-only hook would never shade its normal deferred submission. The final
implementation therefore also chains the already-verified RenderPassImmediately
call site used by TerrainSeam. It collects at most 32 matching geometry submissions
without skipping the original draw. After EndDeferred has composited opaque
lighting and restored forward blend state, it replays the selected geometry in
forward mode. The liquid shader discards replay fragments outside the liquid;
original depth testing preserves occlusion. Cork/neck fragments above local z=7.8
are explicitly excluded, even when their view rays would enter liquid behind them.
The bottle mesh, material, textures, transforms and simulation are never modified.

The single registry entry is **RestoreHealth01 / Skyrim.esm 0003EADD** (verified
by reading the local master), AND the exact model path, geometry name and diffuse
path listed above. Geometry bound radius/scale must match 9.0064 within 0.1 units.
Other potions using the same model are excluded. This is conservative compatibility;
a replacement preserving all identifiers and bounds is not fingerprint-verified.

Hooks:
- BSLightingShader SetupGeometry vfunc 0x6, chained after WindowLife.
- RenderPassImmediately call site REL 100852/107642 + 0x29E/0x28F, already used
  by TerrainSeam; chain preserved and original draw always executed.
- One explicit invocation from Deferred::EndDeferred after ResetBlendStates.

## Proxy, dynamics and optical model

One ellipsoid follows the exact bottle geometry's rotation, translation and
uniform positive scale. Rays use native camera-relative coordinates. Closest-
approach quadratic roots avoid large discriminant cancellation. Ray intervals
are rejected behind the camera and clipped against the world-gravity fill plane.
Optical thickness is the positive remaining interval, normalized by bottle scale.

Fill defaults to 75% **volume**, not 75% height. Inverting the sphere cap relation
F(h)=(2+3h-h^3)/4 and multiplying by the rotated ellipsoid's support radius
preserves volume for sideways bottles. +Z is world up. Acceleration and change
of bottle up-axis drive two bounded spring coordinates. CPU dynamics use
absolute object transforms, so camera movement adds no force. The exact damped
spring solution is stable across varying frame times; derivative smoothing,
impulse clamps and a 0.22 per-axis tilt limit constrain energy. Large translation
jumps, >0.25-second gaps and time rewind reset the object state.

Tiny two-direction normal ripples scale with spring velocity. They disappear
at rest. Beer-Lambert RGB absorption uses coefficients (0.055,0.55,0.82) times
user strength and normalized optical length. Lost transmission contributes a
small lighting-dependent red scatter term, not emission. The surface uses the
existing PIXL BRDF::F_Schlick, D_GGX and Vis_SmithJoint helpers, IOR 1.33 and
roughness 0.22. Lighting inputs are existing colour-space-correct directional
ambient, directional light colour/direction and shadow visibility. Dedicated
local-light integration remains deferred; the preserved bottle coating still
contains its normal lighting response.

## Scene colour, bindings and safety

Refraction uses a crop of forward colour target 0 immediately before the liquid
replay, after opaque/deferred composition. A single reusable 512x512 texture is
allocated lazily, in the source R16G16B16A16_FLOAT or R8G8B8A8_UNORM format.
Only the projected bottle rectangle plus an eight-pixel margin is copied, not
the whole texture. No fullscreen copy, blur, extra sampler, per-potion texture
or compute dispatch is introduced. Offscreen, behind-camera, <3-pixel, >492-pixel
and unsupported resource cases retain the original rendering. Large close-ups
are intentionally unsupported pending a wider crop/tiled strategy.

Refraction offsets are at most two pixels, fade with thickness, and clamp inside
the valid copied rectangle. Four Load operations provide bilinear interpolation.
The crop is never an output target. Owned PS t120 (128-byte structured data) and
t121 (crop SRV) were searched across active shader and CPU bindings before use;
WindowLife keeps t122..127 unchanged. CPU static size assertion and shader
reflection agree: center0, axes16/32/48, plane64, optics80, crop96, dynamics112,
stride128. No shared-buffer ABI change was needed for this module.

Every Lighting draw clears the private slots before classification. Replay
restores prior OM targets/DSV, releases getter references, clears private SRVs
and marks engine render-target state dirty. Native engine draw setup owns its
normal shader/material/blend/depth updates. No manual shader-state cache is
introduced. Shader compilation failure/async fallback during first warm-up must
be checked live; do not assess the effect before required permutations finish.

## Critical Phase 1 limitation

Opaque vanilla rendering has already culled the scene behind the bottle. The
scene crop therefore contains the original bottle, not hidden background
geometry. This prototype offers **scene-colour distortion and volume optics over
the opaque bottle**, not physically correct through-glass background visibility.
It cannot guarantee the full perceptual success criterion until tested. True
transmission needs a separately validated exclusion/background or dual-layer
path, not a fabricated claim that hidden geometry is available. No full fluid
simulation, pouring, arbitrary meshes, foam, bubbles or smoke is implemented.

## Lifecycle and performance

Fixed 32-object history and 32-draw replay arrays; no per-frame container heap
allocation and no retained engine smart/raw objects between frames. Numeric
pointer keys are never dereferenced after collection. Frame-owned pass pointers
are consumed in the same live accumulator frame then cleared. Disabled/loading/
main-menu/cell transitions clear history; ordinary RenderModule::Reset preserves
springs because that callback is actually called every present. Device/resource
setup releases and recreates owned resources. Sleeping states skip spring work.
The original world and physics remain unchanged.

Distance fades from 1200 to 1600 game units; farther objects skip. Very small
projections omit refraction and ripples. Cost is one additional original mesh
draw plus one bounded crop for each eligible visible target. No GPU timings were
measured. A scene with many nearby supported potions costs more; the fixed cap
prevents unbounded work but is not a performance guarantee.

## GUI and serialization

Native module registry/category: Materials > Contained Liquids (Alpha).
Controls: enable, prototype description, fill, slosh strength/damping, absorption,
refraction. Debug modes: proxy, liquid surface, optical thickness, surface normal;
freeze slosh and matched-draw count. Normal settings use existing JSON module
load/save/reset. Numeric ranges and nonfinite values are sanitized. Debug/freeze
are session-only and never saved as release defaults. Default enabled=false.

## Files added

- engine/Modules/ContainedLiquids.h: module, settings, fixed state and GPU layout.
- engine/Modules/ContainedLiquids.cpp: exact registry, hooks, crop, lifecycle/UI.
- engine/Modules/ContainedLiquidMath.h: independently derived spring and fill math.
- pipeline/Contained Liquids/Module.ini and CORE: integrated alpha module metadata.
- pipeline/Contained Liquids/Kernels/ContainedLiquids/ContainedLiquids.hlsli:
  analytic interval, fill clipping, optics, debug and replay mask.
- tools/TestContainedLiquidMath.cpp: production-math tests.
- tools/TestContainedLiquidsShaders.ps1: shader permutation checks.
- docs/CONTAINED_LIQUIDS_PHASE1.md: implementation and test report.

## Existing files modified by this task

- engine/Globals.h and Globals.cpp: authoritative module instance.
- engine/RenderModule.cpp: registration and hook ordering.
- engine/Deferred.cpp: post-opaque replay invocation.
- distribution/Shaders/Lighting.hlsl: guarded forward-only liquid inclusion and
  final colour integration. Existing WindowLife/RenderOrigin changes preserved.
- docs/release_polish/FILE_REVIEW_MATRIX.md: focused review supplement.

Reference files, extracted Bethesda assets and build scratch are not shipped.
New pipeline files have local intent-to-add entries because the existing staging
and audit tools deliberately enumerate Git-indexed paths. No commit or push.

## Validation and test checklist

Automated: standalone production math tests passed for fill fractions 2..98%,
24/30/60/120/240 FPS response equivalence, settling, and 100000 variable-step
bounded impulses. Nine FXC /WX cases passed, including exact ENVMAP+alpha-test,
WindowLife coexistence, deferred no-op, skinned no-op and MaterialForge/Material
Layers/VolumeOcclusion permutations. Buffer reflection matches CPU stride.
Final release build and package results are recorded below.

Live validation remains required:
1. Install the experimental build with its matching shaders; finish compilation.
   Leave RenderOrigin OFF initially to isolate this prototype.
2. Enable Contained Liquids in the Materials module panel. In a disposable test
   save, `player.additem 0003EADD 1`, then drop that potion into the world.
   Inventory preview rendering is deliberately unsupported.
3. Debug Proxy: confirm only the bulb is cyan and the cork never changes from
   side, above, below, and sideways views. Check count/log if nothing matches.
4. Switch debug off. Test fill 0.25/0.75/0.95 upright and sideways. Check that
   the plane remains horizontal and liquid remains inside the bulb.
5. Grab, rotate, drop, stop abruptly; observe lag/overshoot/settling. Repeat
   at 30/60/high FPS. Move only the camera; no new slosh should be introduced.
6. Daylight, shadow, dark interior: no emission; check highlight strength and
   the acknowledged opaque-background approximation. Reject a solid-red-insert look.
7. Put another object in front; verify original depth occludes the liquid.
   Test several overlapping potions, screen edges, close-up cutoff and distance fade.
8. Toggle off/on, load a save, change cells, change resolution, enter Photo/
   Director mode, and reload shaders. Check no stale draw, state leak or crash.
9. Compare disabled against pre-experiment screenshots and matching GPU timings.
   Test DLSS/TAA and FG only after the baseline motion is visually stable.

Next phase should prioritize true background visibility and live proxy fitting,
then object-aware temporal surface motion and local lights. Do not expand the
container registry until the one supported bottle passes the visual checklist.

## Final build and package evidence

Release build: PASS with `BuildRelease.bat PIXL-12C`. Integrated pipeline audit:
PASS, 38 shipping modules and one retired source-only module. Final DLL:
`build/PIXL-12C/Release/PIXLRenderer.dll`, 20,210,176 bytes, SHA-256
`CD72036CD276DEDBF86C440DE40ADB246A38EF9434BDAB167A298219CF63E918`.

Experimental Core:
`dist/PIXL-Renderer-1.0.4-Contained-Liquids-Phase1-Experimental-Core.zip`,
125,169,715 bytes, SHA-256
`0AD52DD345CA66826ED607BFF17A250D735D3B6A86EECAEE808E6272A1570BD1`.
Package manifest verified 343 payloads. Independent 7-Zip test passed: 344
files including the manifest. The packaged DLL hash matches the final build;
Lighting.hlsl and ContainedLiquids.hlsli are present. Nothing was deployed to
Steam and nothing was pushed publicly.

PHASE 1 PROTOTYPE STATUS
COMPILES: YES
BUILDS: YES
READY FOR IN-GAME TEST: YES
VISUAL VALIDATION: PENDING USER TEST
