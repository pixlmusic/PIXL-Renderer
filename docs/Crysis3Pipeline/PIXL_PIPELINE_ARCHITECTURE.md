# Crysis-Informed PIXL Pipeline Architecture

This architecture preserves the working Skyrim/DX11 pipeline. It uses Crysis 3 as evidence for shared renderer data and coherent consumers, not as an implementation template.

```text
Skyrim geometry and engine lighting
        |
        +--> PIXL thin scene buffers
        |      albedo / specular / reflectance
        |      normal + roughness / semantic masks
        |      depth / motion
        |
        +--> specialized forward paths
               skin / hair / foliage / particles / water

PIXL thin scene buffers
        |
        +--> Directional Visibility (HybridGI-owned)
        |      bent direction / hemisphere visibility / confidence
        |
        +--> HybridGI + world cache
        +--> Contact Shadows / Terrain / Volume visibility
        +--> WorldProbes + AmbientProbe + SkyBounce
        |
        v
Deferred composite
        indirect diffuse + probe/specular visibility + reflection hierarchy
        |
        +--> rain runoff / later forward effects
        +--> shadowed froxel atmosphere
        +--> water optics
        +--> CameraSuite optics and exposure
        +--> reconstruction / frame generation / presentation
```

## Shared contracts

### 1. PIXL Directional Visibility

Canonical owner: `HybridGI`.

```text
RG: octahedral world-space bent direction
B : directional/hemisphere visibility
A : observation confidence
```

Consumers must fade to their existing fallback when confidence falls. Direct shadow authorities must not be multiplied blindly.

### 2. PIXL Scene Description

The existing Deferred MRT set is the scene-description foundation. Any proposed new material state must first answer:

1. Is an existing channel semantically available?
2. Can the value be reconstructed from current buffers?
3. Does it need full resolution and every frame?
4. Which passes need it, and on which side of reconstruction?

No new full-resolution MRT is justified merely to resemble the Crysis thin G-buffer.

### 3. PIXL Reflection Query

```text
validated screen/world observation
             |
             miss / low confidence
             v
HybridGI world-cache rough fallback
             |
             miss / low confidence
             v
World/Ambient probe and sky environment
             |
             v
directional specular visibility for fallback energy
```

### 4. PIXL World Forcing

The release-safe first contract is a deterministic world-space wind sampler with current/previous-time parity. Foliage, hair and particles may consume it without each inventing camera-relative noise. A writable 3D force grid and constraint chains are a later compute prototype.

### 5. PIXL Extended Emitters

The first prototype should preserve `RadiantGrid::LightData` and interpret existing radius/size information as a bounded effective source radius for diffuse/specular broadening. Full rectangle/disc/tube/portal types require an append-only light ABI, automatic orientation/shape inference, LTC lookup resources and separate performance validation.

## Pipeline policies

- Runtime constants and standalone compute passes are preferred over global Lighting permutation defines.
- Module descriptors must scope cache invalidation narrowly.
- Temporal modules consume central camera-cut, FOV and resolution invalidation signals.
- Particle/fire energy must be clustered before becoming local lights; never create one shadowed light per particle.
- Water caustics should reuse the existing receiver and add a bounded low-resolution live focus source.
- Atmosphere keeps the existing froxel implementation; Crysis's half-resolution interleaving is historical context, not a replacement target.
