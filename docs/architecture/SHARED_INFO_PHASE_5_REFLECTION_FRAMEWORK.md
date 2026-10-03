# Shared Information Upgrade — Phase 5: Reflection Framework

## Audit result

HybridGI already implements the requested DX11 hybrid pipeline: stochastic GGX candidate generation, hierarchical depth traversal, four-step hit refinement, normal/thin-silhouette rejection, roughness-aware radiance filtering, geometry-remapped temporal reuse, bilateral spatial reconstruction, world-cache miss fill and final environment/probe fallback in Deferred. Replacing it would duplicate resources and risk the tested visual path.

## Implementation

`ReflectionContext` makes that existing result authoritative. HybridGI publishes its RGBA reflection target (RGB incident radiance, A confidence), dimensions and trace/filter/fallback metadata. Deferred is the first consumer and retains the legacy tuple as a safe fallback. Stale publication is explicitly cleared when HybridGI is unavailable.

WaterOptics remains specialized and unchanged. It is not replaced by generic SSR.

## Cost and compatibility

- No new trace, texture, history, shader permutation, register or GPU work.
- Existing reflection settings, GUI, output format and composite behavior are unchanged.
- The framework provides one discoverable result for later metal, wet-material and glass consumers.

## Validation

Release build completed. Contract validation checks screen trace, refinement, geometry rejection, temporal accumulation, world fallback and WaterOptics retention. Live reflection validation remains pending.
