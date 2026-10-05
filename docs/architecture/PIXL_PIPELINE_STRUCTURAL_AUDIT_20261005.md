# PIXL Pipeline Structural Audit — 2026-10-05

Scope: backend correctness and visual-stability review of the documented Skyrim SE/DX11 frame graph. This audit intentionally avoids replacing the renderer architecture or changing artistic recipes.

## Confirmed issues fixed

### 1. Contained Liquids owner lifetime/classification guard

`engine/Modules/ContainedLiquids.cpp::SetupGeometry` accepted broad geometry-name hints before resolving the owning reference. A candidate with no `GetUserData()` owner could reach `owner->GetFormID()` and crash the render thread.

Fix: fail closed immediately when no owner is found. Vanilla/original rendering continues because the capture hook never suppresses the original draw.

### 2. Pre-water depth-copy compatibility guard

`Deferred::Main_RenderWorld_BlendedDecals` copied the main depth resource into `kPOST_ZPREPASS_COPY` without checking that the two resources still matched in dimensions, mip/array shape, format, and MSAA state. During loading, recreation, or an unsupported target transition this could create a D3D11 error or corrupt the depth boundary used by water, particles, and reconstruction.

Fix: validate both resources and copy only when compatible and non-aliased. Preserve/unbind/restore the exact MRT/DSV set around the copy. A mismatch is logged once and leaves the existing destination untouched; that destination is not thereby guaranteed current or safe for temporal use. Unsupported-depth fallback remains a live-validation item.

### 3. Reactive mask no longer masquerades as disocclusion

`ImageReconstruction::Upscale` previously called `TemporalContext::PublishDisocclusion` with the reactive mask. Reactive pixels describe temporal responsiveness; they do not prove that geometry became newly visible. The inspected source does not demonstrate an active temporal shader consumer of this publication, so no current visible improvement is attributed to removing it. This corrects the contract and prevents misleading diagnostics/future misuse.

Fix: remove the false publication. `TemporalContext` now reports disocclusion only when a real producer supplies one.

### 4. Reconstruction publication cannot remain stale

When the renderer changed from DLSS/FSR to native/TAA, the external reconstruction pass could be skipped while `ReconstructionContext` retained the previous frame’s SRVs, dimensions, backend, and validity state.

Fix: `ReconstructionContext::BeginFrame` clears only the frame publication at the authoritative `State::UpdateSharedData` frame-snapshot boundary; the bounded contributor registry remains intact. Native/TAA transitions now fail closed instead of exposing previous-frame reconstruction data, while the current publication remains available through final presentation.

### 5. Liquid replay target guard

Contained Liquids replay now aborts safely if the restored forward color target or main depth view is unavailable. This prevents a cosmetic replay path from issuing a draw with an invalid output set during renderer transitions.

## Structural findings reviewed and intentionally preserved

### Deferred target ownership

The deferred path explicitly unbinds output targets before compute work and restores Skyrim’s dirty-render-target state. The reviewed paths clear the temporary CS SRV/UAV ranges after dispatch. No broad state-cache rewrite was introduced because Skyrim owns the surrounding render-state cache and the existing dirty notifications are part of compatibility behavior.

### Depth boundaries

The graph contains deliberate depth epochs: live main depth, terrain-seam depth, effects-only depth, pre-water depth, reconstruction depth copies, and optional refraction/underwater resources. Consumers continue to use `Util::GetCurrentSceneDepthSRV` where the intended pre-water/terrain seam boundary is required. No consumer was redirected blindly to a different depth copy.

### Temporal frame ownership

`TemporalContext` deduplicates repeated same-frame snapshots and invalidates history for world changes, render-origin shifts, resolution changes, camera cuts, teleports, settings/module resets, and device resets. The review found no justification for adding a second private camera-history service.

### Annotation and reconstruction cost

Native/TAA mode does not allocate the DLSS/FSR reactive/transparency mask pair merely for diagnostics. This preserves the default path’s bandwidth budget. External reconstruction paths still publish base annotations and apply registered reactive contributors.

### Scheduler failure policy

Legacy module callbacks remain optional scheduler adapters with a direct-loop fallback. The scheduler’s pass registry remains setup-time bounded. A broader transient-exception retry policy was not introduced because repeatedly retrying an unsafe pass could create per-frame exception/log churn; resource recreation already rebuilds the registry.

### Raw render-pass pointers

Contained Liquids retains only same-frame pending draw pointers and replays them before the frame’s accumulator objects can become stale. No cross-frame render-pass retention was added.

## Residual risks requiring live or GPU-assisted validation

- Exact Skyrim render-target descriptor compatibility across loading screens, world transitions, MSAA settings, and device recreation still requires a live D3D debug-layer run.
- The native/TAA path intentionally does not create external reactive masks. If a future native temporal feature needs them, it should add an explicit bounded path rather than assuming DLSS/FSR resources exist.
- The scheduler still models most legacy module resource usage coarsely as `ModuleOwnedResources`; migrating individual passes to precise declarations is future work.
- The temporal service remains single-view and is not a per-eye VR contract.
- Visual validation is still required for terrain displacement edges, water boundaries, reactive mask dilation, DLSS/FSR transitions, and menu/loading transitions.

## Validation performed

- Source/config path audit completed against the canonical repository.
- 43 module manifests discovered; 42 runtime modules registered, with Hair Reconstruction retained as ABI-only reserved storage.
- `git diff --check` passed before and after the fixes.
- Initial Release target rebuilt successfully; that initial pass did not change shaders. Follow-up shader and build validation is recorded below.

## Follow-up: frame handoff hardening

The follow-up traces depth/motion/mask ownership across deferred lighting, late effects, liquid replay, external reconstruction and resource recreation. It is not a claim that every shader/module or every runtime permutation was visually certified.

### Implemented fixes and expected image benefit

1. **Encoder owns CS b5.** DistantLife, ReactiveFX and runoff clear shared compute bindings. `EncodeTexturesCS` needs `SharedData::CameraData` to compare linear depth at silhouettes. Explicit binding removes a confirmed inherited-state dependency that could invalidate motion conditioning. Expected benefit: more consistent silhouette/occlusion-edge reconstruction.
2. **Liquid reactive coordinates match active pixels.** Capture bounds already include viewport projection/dynamic resolution. Removed a second backing-texture scale, which displaced/shrank liquid reactive regions under upscaling. Expected benefit: less stale liquid/refraction history at the actual bottle silhouette.
3. **Reconstruction failure is observable.** DLSS tag/evaluation and FSR dispatch now return success through `Upscale`/`PerformUpscaling`. Failed dispatch does not validate history, upscale depth, disable dynamic resolution, sharpen stale DLSS output or copy stale NR guides. Pending reset is preserved/rearmed. This does not guarantee recovery of a main texture partially modified by a failed SDK dispatch; no expensive backup copy was added.
4. **Same-backend recreation rebuilds guides.** Target/device recreation explicitly discards masks and intermediate textures even without a backend change. Old CB wrappers are released before replacement. FSR context/interface descriptions are value-initialized; nonexistent contexts are not destroyed/dispatched.
5. **Publication epoch safety.** Device/target reset clears annotation, reconstruction and owned motion publications. Base-mask refresh clears old reactive/transparency references; mismatched annotation extents are rejected rather than rewriting base dimensions.
6. **Temporal continuity.** External invalidations survive the next snapshot without duplicate history callbacks. Nonconsecutive frame indices reject reuse. Mismatched disocclusion publication cannot overwrite the motion extent. Previous-frame transform metadata no longer exposes borrowed guide pointers after their owning frame retires (no inspected callers used those pointers).
7. **Replay cleanup.** Missing liquid targets are rejected before `OMGetRenderTargets` acquires references. Clearing liquid history also clears reactive regions; disabled/no-context contributors do no work.
8. **Shader robustness and small cost reduction.** Nonfinite DLSS motion is replaced with zero and full reactivity; invalid neighbor vectors cannot win dilation. Material-neighborhood traversal reuses the already-read centre and fetches each neighbor once in source. No new textures, passes, sample footprint or resource slots.

### ABI and flow review

- GPU layouts/register assignments unchanged. Shared b5 remains 848 bytes / 53 registers; CPU asserts remain in force.
- Encode b0 remains 16 bytes (`float2` extent + `float2` padding); explicit CPU size/offset assertions added. Depth-upscale b0 remains 16 bytes.
- Only internal C++ return contracts changed: Streamline/FidelityFX dispatch, reconstruction preparation and its hook caller.
- Corrected map/graph: `kPostPostLoad`, actual material-mask format, late forward water/transparency before reconstruction, and pre-water copy after `EndDeferred` (not a producer for earlier same-invocation particle/deferred work).
- Existing per-pixel shader masks remain authoritative. `PixelAnnotations::Classify` is not silently promoted to an active image producer.

### Validation and remaining boundaries

- New executable `PIXLTestPipelineHandoffs` exercises the actual temporal/annotation/reconstruction implementations with a DX11 WARP device and real SRV: invalidation carry, callback count, frame gaps, guide-extent rejection, device-reset publication release, same/new-frame lifetime and bounded contributor behavior.
- FXC `/WX /Ges /O3` compiled encoder DLSS, FSR+DEPTH_OUTPUT and native variants successfully.
- Focused CTest run: handoff executable plus five existing architecture contracts passed (6/6).
- Final Release build (`cmake --build build/PIXL-12C --config Release --target PIXLRenderer PIXL-Portable-Tests --parallel 12`) succeeded after the final runtime edits. All 21 portable tests passed, including the rebuilt WARP handoff executable. No compiler warnings/errors appeared in these build outputs; this is not a historical warning-baseline comparison. No hardware GPU timing is available.
- Encoder DXBC reflection confirms b0/b5 binding and `CameraData` offset 528, matching CPU SharedData layout. DLSS variant reports approximately 682 instruction slots with the selected FXC/O3 compiler; this is complexity evidence, not a measured performance gain or comparison with the old shader.
- Remaining live checks: 50/67/100% render scale bottle-mask alignment; moving terrain/actors at silhouette edges; NONE/TAA/FSR/DLSS transitions; loading/target recreation; failed SDK dispatch; native output and HDR/UI; water/depth boundaries.
- Auxiliary-camera snapshot ordering and unsupported-copy fallback are documented, not reordered without a captured engine-frame trace. Constant-resource binding is fixed where demonstrated, not assumed fixed for all modules.
- Changes are source/build-only: nothing was copied to Steam, staged into a release package, committed or pushed by this follow-up. Existing third-party submodule modifications were preserved. The review matrix was updated locally; `docs/release_polish/` is ignored by the current repository rules.

### 5 Future Visual Improvements

1. Capture main/auxiliary camera epochs and establish a verified main-view-only temporal snapshot.
2. Add an explicitly produced geometric disocclusion guide only when a real consumer warrants it.
3. Validate activity masks at displaced terrain silhouettes against DLSS/FSR captures.
4. Measure reactive bottle coverage and reduce marking only where motion/refraction permits.
5. Compare native/TAA versus external-upscaler water/transparent-layer history behavior.

### 5 Future Performance Improvements

1. Inspect encoder DXBC/register count and GPU timing before changing dilation radius.
2. Narrow scheduler resource declarations per pass without changing hook order.
3. Cache active/backend resource descriptors for inexpensive epoch assertions.
4. Profile target-copy bandwidth before changing depth boundary representation.
5. Avoid snapshot COM copies in diagnostics only when actual CPU profiling justifies it.

### 5 Future Feature / Research Ideas

1. Optional developer frame-epoch overlay for all published guides.
2. A debug-layer binding-hazard regression harness for late effect/reconstruction chains.
3. Explicit depth-epoch validity metadata and unsupported-format fallback routing.
4. Per-view temporal contexts as a prerequisite for validated VR support.
5. SDK failure injection tests with bounded output recovery, without a permanent full-frame backup.
