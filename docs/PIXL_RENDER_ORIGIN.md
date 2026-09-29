# PIXL RenderOrigin experiment

Status: **EXPERIMENTAL — compiled and suitable for controlled testing, not validated for normal use.**
Default OFF, session-only controls. No live Skyrim visual test or GPU timing has
been performed. This is a staged coordinate foundation with limited consumers,
not a renderer-wide conversion or a production floating-origin claim.

## Checkpoint and safety boundary

Before edits, the existing source including working-tree changes was exported:
`dist/PIXL-Renderer-1.0.4-Before-RenderOrigin-20260926-Source.zip`.
SHA256: `03CCF5973550751D0C8D0C0AB3D3E664269487F42F10C98830D6AFCF13709686`.
Archive validation passed. Dependencies follow SOURCE_DEPENDENCIES.md and
submodule metadata; the export includes the reproducible FidelityFX patch.
Experimental branch: `experiment/render-origin-20260926`, based on
`37202f4ada17007b16d0b0f704cdf021f473f93b` plus the existing uncommitted work.
No remote branch/source publication is part of this experiment.

No actor, reference, Havok, navmesh, collision, cell, water-object, animation-root,
save or engine camera transform is written. The stable Steam installation is
not updated by this experiment. Its last WindowLife test remains separate.

## Why and existing architecture

Skyrim already rasterizes using camera-adjusted coordinates. Native b12 holds
current and previous camera offsets and matrices. Blindly subtracting another
origin from positions fed to those matrices would introduce exactly the motion
and shadow artifacts this work aims to prevent.

The valuable first changes remove large absolute intermediate values from
precision-sensitive subtraction while exposing a consistent snapped coordinate
space for future modules. CPU doubles cannot recover precision already lost in
Skyrim's float transforms; no such recovery is claimed.

| Space | Meaning / current owners | Contract |
|---|---|---|
| Absolute Skyrim | Engine objects, terrain cells, world probe identity, flowmaps | Persistent; never modify |
| Native engine-relative | Lighting/Water/Grass positions, FrameBuffer camera matrices | P minus native camera adjustment E |
| PIXL render-relative | New core API | P minus snapped origin O |
| View | GI/depth rays, camera-space normals | Engine view transform |
| Clip/screen | Projection, dynamic resolution, motion vectors | Homogeneous divide and viewport convention retained |
| Previous native | PreviousWorld, previous camera matrix/adjustment | P(previous) minus E(previous) |
| Previous PIXL | New temporal helpers | P(previous) minus O(previous) |
| Light | Shadow projections, utility depth, occlusion camera | Retain each producer's matrix/input contract |
| Grid/probe | SkyBounce toroidal index, Ground Response field, World Probes | Persistent world identity; origin epochs must not reindex |

## Origin lifecycle and frame ordering

`State::UpdateSharedData` samples the cached engine camera at the first shared
upload per PIXL frame. `Globals::CacheFramebuffer` remains the authoritative
Map/Unmap snapshot of native b12. Absolute camera is native E plus the translation
from the cached inverse view (transposed to SimpleMath's CPU convention), rather
than player position. This covers free camera and first-person camera offsets.

The manager updates once per frame ID. Subsequent shared uploads reuse its
current/previous origins, but calculate bridge offsets against that upload's
current/previous cached native matrices. Thus alternate/native camera offsets
are not mistaken for a new PIXL epoch. Origins, UI and updates are render-thread
owned; no new synchronization, hooks, scene traversal or per-object allocations.
The developer UI queues settings for the next frame. No GPU readback is added.

XYZ snaps to the nearest multiple of 4096. A shift occurs only when an axis is
more than 0.75 grid widths from the held origin. The 0.25-grid hysteresis beyond
the nearest-cell boundary prevents chatter. Interiors use the same algorithm;
small interiors normally need no shift. Debug mode uses 256 units. The Force
button deliberately changes origin with a stationary camera for testing.

First frame, frame-counter rewind, invalid input, exterior worldspace change,
interior-cell identity change, or camera movement above 32768 units marks a
coordinate discontinuity. Exterior cell boundaries do not count as worldspace
changes. Mode/grid changes also invalidate the manager's coordinate continuity.
Invalid/nonfinite or implausibly large (>1e12) camera input disables the active
experimental path. On discontinuities previous origin initializes to current.

`HistoryValid` describes coordinate continuity only: it does not replace each
module's existing camera-cut, FOV, resolution, loading and resource checks.
Save/load in the same context at the same position does not itself invalidate
this stateless coordinate mapping. Existing module history lifecycles still own
their resources. The service neither clears histories nor moves spatial caches.

## CPU and GPU APIs

`engine/Renderer/RenderOrigin.h` owns Position (three doubles), Manager, and
GPUData. It exposes absolute camera, current/previous origin, delta, epoch,
WorldToRender, RenderToWorld and CurrentToPrevious. Epoch is 64-bit on CPU;
GPU diagnostics expose the low 32 bits. Never persist GPU epoch as a world ID.

Nine float4/uint4 registers (144 bytes) append to SharedData b5, beginning at
byte 704. Total size is 848 bytes. Existing fields keep their offsets. Data:
current/previous high+low origins, delta/grid size, current/previous E-O offsets,
E-current minus E-previous calculated in double, flags. C++ size/offset assertions
and FXC reflection check the binary contract. No new b/t/u/s register is consumed.

The shared shader ABI changes to `PIXL.SharedBuffers.20260926.RenderOrigin1`.
This requires a fresh cache for the experimental binary even when disabled.
Never use the old ABI's preloaded cache or mix this DLL with old SharedData.
Disabling the option restores the legacy algorithms, not an older binary ABI.

`Common/PIXLRenderOrigin.hlsli` provides explicit engine/render/absolute bridges,
current-to-previous conversion, current and previous projection, and depth-to-
render reconstruction. Projection remains factored through native matrices:

    clip = NativeViewProjection * float4(renderPosition - (E - O), 1)

This is the PIXL-relative matrix contract without mutating or duplicating native
matrices. The depth reconstruction helper expects normalized non-DR UV and raw
native depth; callers must reject invalid/background homogeneous positions as
their pass requires. Do not feed render-relative points directly to native b12.

## Temporal sign and precision

For a static absolute P:

    currentRender = P - currentOrigin
    previousRender = currentRender + (currentOrigin - previousOrigin)
    previousEngine = previousRender - (previousEngineOrigin - previousOrigin)

The optimized native-to-previous helper algebraically reduces this to:

    previousEngine = currentEngine + (currentEngineOrigin - previousEngineOrigin)

The offset is subtracted in double on CPU, then converted to float once. This
avoids `(small + largeCurrent) - largePrevious` in the pixel/compute shader.
Snapped-origin shifts cancel exactly in the mathematical transform. Actual camera
motion remains present. Static objects with a moving camera should not have zero
screen velocity; the artificial origin motion alone must contribute zero.

Native MotionBlur::GetSSMotionVector already projects separately transformed
current and previous engine-relative vertices. DLSS/DLAA/FSR/FG retain these
inputs and native camera constants. No RenderOrigin-based reset is wired into
upscalers, GI, exposure, fog, or shadow histories.

## Integrations and deliberate boundaries

| System | Result |
|---|---|
| State / shared constants / developer UI | New core service, snapshot, packing and session controls |
| Atmosphere scattering history | Uses shared stable native-to-previous conversion; density/noise/height and existing history rejection retained |
| CameraSuite HDR camera motion reconstruction | Uses the same conversion; exposure, lens and camera history ownership retained |
| RainResponse roof runoff reprojection | Uses the same conversion; world seed and precipitation/emitter storage remain absolute |
| WindowLife | Geometry-local subtraction avoids large absolute intermediate values when enabled; authored world seed and glass phase unchanged |
| SkyBounce | CPU grid rounding and relative grid offset computed in double when enabled; toroidal absolute identity/storage retained |
| Shadows / contact shadows | Audited native/light-space contracts; no conversion of cascades or engine shadow matrices yet |
| HybridGI / World Probes | View-space tracing and absolute spatial caches retained; broad migration deferred |
| Ground Response | Existing tiled absolute addressing and native-relative deformation retained; no field/event rebase |
| Water / flowmaps / caustics | Absolute world phase and engine-relative rendering retained |
| Rain/snow/particles/wind | Existing world identity retained; only runoff temporal bridge changed |
| Main depth, geometry and motion buffers | Native producers and consumers retained; explicit helper API is available for future modules |

The live integrations intentionally use algebraically simplified coordinate
bridges. They do not need to add and subtract O simply to exercise the snap.
This limits risk and extra GPU arithmetic. No increased shadow resolution or
universal far-world improvement is claimed.

## Audit ledger

`tools/WriteRenderOriginCoordinateInventory.ps1` repeats the final search across
engine, pipeline and distribution source. It records every matching path, line,
snippet and lexical classification in
`docs/release_polish/RENDER_ORIGIN_COORDINATE_OCCURRENCES.csv`.
That CSV is an occurrence index, not a claim of complete semantic verification.
Unconverted paths remain candidates for staged review before conversion.

Semantic traces used for this patch: Globals frame cache -> State shared upload
-> SharedData; native FrameBuffer/MotionBlur -> Lighting/Grass/Effect/Water motion
outputs -> ImageReconstruction/FidelityFX/Streamline; Atmosphere current/previous
reconstruction; WindowLife geometry/mask/projector; SkyBounce CPU cell/offset
-> probe Sample/UpdateProbes; CameraSuite depth-to-camera velocity; RainResponse
roof seed -> previous UV. Ground Response field, WaterCaustics, HybridGI absolute
cache injection, VolumeOcclusion absolute light-space and WorldProbes previous
capture transforms are explicitly retained rather than mechanically rebased.

## Adding a new PIXL module

Use render-relative positions for precision-sensitive local calculations, and
stable absolute or integer-cell/local coordinates for persistent identity.
Subtract absolute anchors on the CPU in double where possible. The optional
power-of-two periodic helper is only appropriate for binary periods; arbitrary
periods require a double CPU phase/remainder contract. Never seed noise with
OriginEpoch or a snapped-relative coordinate. High/low storage cannot recover
precision already lost in an absolute float input.

Never cache a render-relative position across epochs without conversion. Never
modify engine transforms. Label matrix inputs explicitly. Use the previous
origin with previous matrices, not the current origin. Do not clear temporal
history for an ordinary shift. Treat real teleports/loading separately.

## Validation and manual torture matrix

Automated checks include CPU lifecycle/negative coordinates/hysteresis,
10,000 simulated frame transforms with repeated forced shifts, invalid input,
context changes, legacy toggle, HLSL helper compile and reflected buffer offsets.
These are numerical/compiler tests, not a live GPU motion-vector test.

For live testing, open Advanced Tuner > Experimental > World > Render Origin.
Enable the module, then use the 256-unit grid and Force button. Observe near origin and far
world coordinates; compare disabled/enabled in the same scene. Test static
camera, slow boundary crossings in both directions, sharp camera rotations,
first/third person, Photo/Director mode, interiors, large custom interiors,
worldspace changes, fast travel, load/reload and resolution/FOV changes.

Check shadows, fog, SkyBounce, water, wind, rain/snow, roof runoff, WindowLife,
moving actors, persistent footprints and DLSS/FSR/FG. Require no shift-induced
pop, phase change, artificial motion-vector spike, flash or cache reset. Measure
GPU timings in matching scenes before claiming any performance change. Existing
engine float precision and absolute persistent caches remain limitations.

Expected cost: fixed CPU arithmetic once per frame, 144 bytes extra shared data,
uniform branches in limited consumers; no new draw/dispatch, texture, readback,
allocation per object or GPU history. Cost and benefit remain unmeasured.

Recommended next target: validate the temporal bridges live, then convert a
single HybridGI spatial-cache lookup to integer cell plus local position.
Shadow cascade rebasing should follow only with captures proving an absolute
precision bottleneck; native camera-relative shadows should not be rebased twice.

## Final build evidence and changed-file ledger

Final Release rebuild: PASS using `BuildRelease.bat PIXL-12C`; integrated audit
passed for 37 shipping modules. No compiler warning appeared in the captured
final build output. This was an incremental rebuild of all affected objects,
not a claim of a fresh dependency/toolchain build.

DLL: `build/PIXL-12C/Release/PIXLRenderer.dll`, 20,184,064 bytes,
UTC timestamp 2026-09-25 18:33:24.
SHA256: `5FB774FF7C54D1EBE96472195CBAEE478A85AB7E0123A6B053B230466D36EB58`.

Validation PASS: 10,000 CPU frame/shift cases; reflected 848-byte shared buffer;
588 compute cases; 14 WindowLife cases; 32 material, 32 grass and 72 selected
landscape/water cases; focused CameraSuite HDR and roof-runoff compute shaders.
These suites cover selected permutations, not every possible renderer permutation.
Shader checks use FXC with warnings treated as errors. Legacy and enabled paths
coexist in the same compiled runtime branch. No live GPU execution was tested.
Final search indexed 756 coordinate occurrences across 90 files, not 90 complete
semantic reviews. Git diff whitespace check passed (line-ending notices only).

Local test artifact:
`dist/PIXL-Renderer-1.0.4-RenderOrigin-Experimental-20260926-Core.zip`
SHA256: `FA74CECE84C4387FC17B74C76869E195A87B4D306D9CEE1032A3A20431822DC5`.
Package manifest validated 341 payloads; ZIP integrity passed, 342 files including
manifest. Packaged DLL hash matches the final build. No compiled pipeline cache
is bundled. Steam was not deployed; nothing was pushed publicly.

| Changed file | Purpose |
|---|---|
| engine/Renderer/RenderOrigin.h (new) | Double CPU state, snap lifecycle, temporal and GPU bridge |
| engine/Renderer/RenderOrigin.cpp (new) | Singleton and developer diagnostics |
| engine/State.h | Shared ABI append and static assertions |
| engine/State.cpp | Once-per-frame camera/context snapshot and upload |
| engine/ShaderCache.cpp | Invalidate incompatible shared-buffer cache |
| engine/Menu/RuntimeSettingsRenderer.cpp | Developer-only session panel |
| engine/Modules/SkyBounce.cpp | Double cell-to-camera offset; preserve toroidal identity |
| distribution/Shaders/Common/SharedData.hlsli | Matching GPU fields |
| distribution/Shaders/Common/PIXLRenderOrigin.hlsli (new) | Explicit space conversions and temporal helpers |
| pipeline/Atmosphere/Kernels/Atmosphere/VolumetricFogLightScatteringCS.hlsl | Stable previous-camera conversion |
| pipeline/Camera Suite/Kernels/CameraSuite/HDROutputCS.hlsl | Stable camera-motion reconstruction |
| pipeline/Rain Response/Kernels/RainResponse/RoofRunoffResolveCS.hlsl | Stable history projection; retain absolute seed |
| pipeline/WindowLife/Kernels/WindowLife/WindowLife.hlsli | Local pane difference without large absolute intermediate |
| tools/TestRenderOrigin.cpp (new) | Numerical lifecycle/temporal tests |
| tools/TestRenderOrigin.ps1 (new) | Standalone MSVC test runner |
| tools/RenderOriginValidation.hlsl (new) | GPU API compile/reflection fixture |
| tools/TestRenderOriginShaders.ps1 (new) | FXC and ABI validation |
| tools/WriteRenderOriginCoordinateInventory.ps1 (new) | Reproducible occurrence index |
| docs/PIXL_RENDER_ORIGIN.md (new) | Coordinate contract, limitations and integration guide |
| docs/release_polish/FILE_REVIEW_MATRIX.md | Focused changed-path review supplement |
| docs/release_polish/RENDER_ORIGIN_COORDINATE_OCCURRENCES.csv (generated) | Search evidence |

Existing unrelated working-tree changes predate this experiment and are preserved.
The checkpoint includes them. Remaining release-polish reviews are not asserted
complete by this focused coordinate experiment.
