# PIXL Reflection Hierarchy Audit

Date: 2026-08-31
Status: active pipeline verified; no source retune implemented; bounded local-probe extension remains future work

## Outcome

PIXL already implements the important Crysis-style principle of combining screen observations with environment fallback. The current renderer is not a single global cubemap path:

```text
opaque material reflectance / roughness
        |
        +--> HybridGI stochastic screen-space ray hit
        |        depth crossing + hit-normal facing confidence
        |        neighborhood variance clamp
        |        motion/depth/normal validated temporal history
        |        miss
        |          v
        |    HybridGI world-cache rough fallback
        |
        +--> WorldProbes dynamic environment + reflection cubemaps
        |        mip-filtered by roughness
        |        AmbientProbe/DALC and SkyBounce calibration
        |        PIXL directional specular visibility on fallback energy
        |
        v
deferred reflectance composite
```

Water retains Skyrim's raw/blurred Water SSR pair and blends it with the PIXL WorldProbes fallback using surface orientation, distance, edge confidence, roughness and luminance guards.

The missing Crysis-like capability is not SSR itself. It is **multiple bounded, parallax-correct local probe volumes**. Adding those volumes is a B/C architectural feature because PIXL needs stable ownership, capture/update policy, room/worldspace invalidation, probe selection and a compact shader-visible descriptor before box correction is meaningful.

## Actual PIXL paths

### Hybrid reflections

- `HybridGI/hybridReflection.cs.hlsl` samples a GGX VNDF direction from the receiver's normal, view and roughness.
- Screen tracing uses the existing HybridGI depth hierarchy, refines the crossing, validates receiver thickness and rejects strongly back-facing hit normals.
- Confidence contains screen-edge, depth-crossing and facing terms. Smith geometry is used as confidence metadata rather than multiplying radiance twice.
- A missing/weak screen hit may use the persistent HybridGI world cache only over a bounded roughness range.
- Observation radiance is normalized by its confidence weight. Confidence controls temporal authority rather than making a 30% reliable sample 30% as bright.
- Reprojected history has depth/normal validation, a current-hit neighborhood clamp, a firefly shoulder and a short confidence-decaying miss tail.

### WorldProbes

- `WorldProbes` captures camera-local environment and reflection cube faces, infers missing information, filters seven roughness mip levels, and compresses to BC6H.
- Work is split across three frames for the environment capture and another three when the reflection capture is active.
- Large game-time jumps reset both capture families and restart the split pipeline, preventing pre-jump mips from being compressed as current state.
- `WorldProbes.hlsli` combines environment and sky components with AmbientProbe/DALC calibration and SkyBounce visibility/specular response.
- Deferred HybridGI applies bent-normal directional specular visibility to the cubemap/probe fallback before blending toward a validated Hybrid Reflection observation.

### Water

- `Water.hlsl` begins with dynamic WorldProbes cubemap irradiance.
- Skyrim raw and blurred SSR textures are chosen by surface roughness and pointing confidence.
- Dynamic-resolution UV correction, screen-edge confidence, distance support, water roughness and a luminance ceiling suppress invalid/unstable samples.
- SSR confidence replaces rather than adds to the probe fallback, preventing double reflection energy.

## Correctness decisions

### No duplicate global SSR pass

PIXL's Hybrid Reflections are already the modern opaque SSR/world-cache path. Re-enabling a historical global SSR shader would duplicate reflected energy, temporal history and GPU tracing work.

### No unconditional bent-normal multiplier on screen hits

A validated on-screen ray can legitimately observe a bright object through the receiver's reflected direction. Broad hemisphere visibility remains authoritative for environment/probe fallback, but multiplying every high-confidence screen hit by it would erase valid reflections and double-occlude against hit validation. Cave/eave A/B tests remain required; only low-confidence or off-screen fallback energy should receive extra suppression.

### No camera-centred box correction without a probe volume

Box projection needs a probe capture position and stable volume bounds for the receiver. Applying it to PIXL's single camera-following WorldProbe would make reflections swim with the camera and would not provide local room containment.

## Bounded local-probe extension

A future implementation should add a small CPU-managed pool (for example 8-16 active probes), not thousands of per-object cubemaps:

```text
game-thread room/cell candidates
        -> immutable probe snapshots
        -> importance queue (player room, visible, recently dirty)
        -> amortized cube-face capture/filter
        -> StructuredBuffer<ProbeVolume>
        -> receiver selects 1-2 volumes
        -> box-correct reflection ray
        -> confidence blend with current WorldProbe
```

Each descriptor needs capture position, AABB/OBB bounds, fade distance, cubemap slice, generation and validity. A texture-cube array plus append-only structured buffer is preferable to new Lighting permutations. Selection should occur per tile/cluster where possible. Interior portals/rooms are useful evidence, not permission to assume all modded architecture is sealed.

## Performance

Current Hybrid Reflection cost is one compute trace/resolve path gated by reflectance and maximum roughness. WorldProbes amortizes capture/filter/compression across frames. No additional work was added by this audit.

The future local system must cap capture faces per frame, sleep static probes, invalidate only on cell/lighting/time discontinuities, and fall back to the existing WorldProbe hierarchy when the pool is exhausted.

## Runtime validation queue

1. mirror/glossy/rough materials in caves, under eaves and in open sky;
2. slow orbit and fast pan across screen-hit to probe-fallback boundaries;
3. interiors with bright exterior sky behind walls;
4. water SSR at shores, screen edges and dynamic-resolution modes;
5. TAA and DLSS on thin geometry and changing local lights;
6. wait/time skip and weather transition during the split WorldProbe update;
7. Hybrid Reflections, WorldProbes, AmbientProbe and SkyBounce toggled independently.

## Classification

- Existing reflection hierarchy: **A — essential and working; runtime regression validation required after related changes.**
- Additional confidence tuning: **B — only with captured A/B evidence.**
- Multiple bounded parallax-correct probes: **B/C — high-value architecture prototype, not a release-polish hot patch.**
