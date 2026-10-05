# Typed pipeline migration checkpoint — 2026-10-05

This is a source/build checkpoint, not a release certification or live GPU profile. The existing Skyrim SE DX11 hooks, vanilla fallback, TAA/FSR/DLSS paths and fixed b5/b6 CPU/HLSL ABI remain authoritative. The pipeline map is the base architecture reference.

| Requested phase | Status | Implemented/verified | Remaining gate |
|---|---|---|---|
| 1. Frame/view/depth/resource safety | Partial | Typed tokens, epochs, extents, depth publications and pass declarations; scoped targeted DX11 copy unbinding; legacy hook ordering preserved; reconstruction contributor application now rejects stale token/extent publication | Most legacy module bindings are not yet declared/tracked; live debug-layer validation |
| 2. Unified temporal | Partial | MainWorld-only typed context; packed opaque history and two-channel confidence/disocclusion GPU guide; Atmosphere and HybridGI now require current MainWorld temporal continuity before reusing history | No broad consumer migration; camera-motion depth comparison needs live tuning and full matrix-based validation |
| 3. GPU annotations | Partial | Compact typed UAV written with temporal guide; known opaque signals only | Forward/window/foliage classes require explicit trusted contributors; avoid guessing from RGB |
| 4. G-buffer | Audit complete, optimization deferred | Confirmed 10-bit glossiness in NormalRoughness RGB, 2-bit alpha is coverage; no unsafe repacking | Demand-driven MRT writes need fixed-slot shader/output changes and measured benefit |
| 5. Optical replay | Partial | Bounded same-frame queue used by Contained Liquids; original draw retained | Scene crop still liquid-owned; WindowLife and other optics not migrated |
| 6. Reconstruction contributors | Partial | Typed frame/backend publication; liquid and Reactive FX contributors retain max composition; contributor writes are now token/extent validated at the live reconstruction boundary | Explicit water/glass/window/foliage/precipitation contributors where baseline masks are insufficient |
| 7. Resource efficiency | Partial | Existing bounded transient pool retained and idle replacement fixed; typed light/probe metadata, typed GI light acquisition | Other consumers still legacy; source/include fingerprinting not added to prebuilt shader cache |
| 8. Adaptive workload | Partial | Existing opt-in hysteretic controller now ignores absent GPU timings; reflection rays use separate timing/domain; SkyBounce probe updates use a bounded 1–2 frame cadence when budgeted; existing quality presets retained | Volumetric cadence and other domains require measured timing and temporal tests |

The new temporal/annotation pass adds one full-active-region compute dispatch and persistent small-format textures. Since downstream consumers are not migrated, its production cost/benefit is unproven. Before packaging, use GPU captures to decide whether to connect trusted consumers or demand-gate this pass; do not ship an unconditional pass on an unmeasured assumption.

The shader cache already keys by stage/descriptor/defines and validates layout, shared ABI, shared shader revision and module revisions. A new global revision for this standalone shader would invalidate the full prebuilt cache without changing existing permutation semantics, so it was deliberately not added. Persistent include fingerprints remain future work.

Validation at this checkpoint: Release DLL build; 21 portable CTest tests including WARP handoff/binding tests; strict FXC `/WX /Ges /O3` compilation of `TemporalValidityCS.hlsl`; `git diff --check`. No Skyrim runtime, DX11 debug-layer, visual regression, VR/stereo, performance or upscaler certification has been performed. No DLL or shader payload was deployed by this migration.
