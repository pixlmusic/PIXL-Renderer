# Typed pipeline migration checkpoint — 2026-10-05

This is a source/build checkpoint, not a release certification or live GPU profile. The existing Skyrim SE DX11 hooks, vanilla fallback, TAA/FSR/DLSS paths and fixed b5/b6 CPU/HLSL ABI remain authoritative. The pipeline map is the base architecture reference.

| Requested phase | Status | Implemented/verified | Remaining gate |
|---|---|---|---|
| 1. Frame/view/depth/resource safety | Partial | Typed tokens, epochs, extents, depth publications and pass declarations; scoped targeted DX11 copy unbinding; legacy hook ordering preserved; reconstruction contributor execution now requires a current MainWorld publication extent; legacy records carry module quality/resolution metadata | Most legacy module bindings are not yet declared/tracked; live debug-layer validation |
| 2. Unified temporal | Partial | MainWorld-only typed context; packed opaque history and two-channel confidence/disocclusion GPU guide; reconstruction, Atmosphere and HybridGI now require current MainWorld temporal continuity before reusing history | DOF/camera, water, reflections and atmosphere sub-histories still need explicit confidence/disocclusion weighting and live tuning |
| 3. GPU annotations | Partial | Compact typed UAV written with temporal guide; known opaque signals only; availability is logged on state transitions | Forward/window/foliage classes require explicit trusted contributors; avoid guessing from RGB |
| 4. G-buffer | Audit complete, optimization deferred | Confirmed 10-bit glossiness in NormalRoughness RGB, 2-bit alpha is coverage; no unsafe repacking | Demand-driven MRT writes need fixed-slot shader/output changes and measured benefit |
| 5. Optical replay | Partial | Bounded same-frame queue used by Contained Liquids; original draw retained; queue aggregates requirement bits and logs changes without owning passes | Scene crop still liquid-owned; WindowLife and other optics not migrated |
| 6. Reconstruction contributors | Partial | Typed frame/backend publication; liquid and Reactive FX contributors retain max composition; all contributor execution paths validate MainWorld token/epoch/extent; names/types are exposed for diagnostics | Explicit water/glass/window/foliage/precipitation contributors where baseline masks are insufficient |
| 7. Resource efficiency | Partial | Existing bounded transient pool retained and idle replacement fixed; typed light/probe metadata, typed GI light acquisition; shader source/include content fingerprint added to cache metadata with legacy-cache compatibility | Other consumers still legacy; fingerprint needs to be written by a cache-producing run before strict validation can be enabled |
| 8. Adaptive workload | Partial | Existing opt-in hysteretic controller now ignores absent GPU timings; reflection rays and World Probes use separate reflection cadence; SkyBounce probe updates and Volume Occlusion use bounded 1–2 frame cadence when budgeted; existing quality presets retained | Remaining domains require measured timing and temporal tests; controller remains opt-in |

The new temporal/annotation pass adds one full-active-region compute dispatch and persistent small-format textures. Since downstream consumers are not migrated, its production cost/benefit is unproven. Before packaging, use GPU captures to decide whether to connect trusted consumers or demand-gate this pass; do not ship an unconditional pass on an unmeasured assumption.

The shader cache already keys by stage/descriptor/defines and validates layout, shared ABI, shared shader revision and module revisions. It now also records a content fingerprint over the deployed shader/include/module metadata tree when cache metadata is written. Older validated caches without this optional field remain usable; strict fingerprint enforcement begins once a cache-producing run has written the field.

Validation at this checkpoint: Release DLL build; 22 portable CTest tests including WARP handoff/binding and shader-cache metadata contracts; strict FXC `/WX /Ges /O3` compilation of `TemporalValidityCS.hlsl`; `git diff --check`. No Skyrim runtime, DX11 debug-layer, visual regression, VR/stereo, performance or upscaler certification has been performed. No DLL or shader payload was deployed by this migration.

## Follow-up implementation checkpoint — 2026-10-05

The source-only follow-up added transition-only diagnostics for scheduler records, TemporalValidity/annotation availability, optical requirement changes, adaptive mode, and reconstruction contributor registration. The compatibility contributor overload now refuses to write without a current typed MainWorld publication. These changes do not certify GPU behaviour.

**LIVE VALIDATION REQUIRED:** confidence-weighted shader consumption by HybridGI/reflections/Atmosphere/DOF/water; authored forward annotation producers; shared optical scene-copy ownership; actual G-buffer demand elimination; DX11 debug-layer hazard checks; temporal/upscaler visual captures; and measured GPU timings for adaptive decisions.
