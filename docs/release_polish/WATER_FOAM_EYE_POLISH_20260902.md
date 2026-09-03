# Water Foam and Eye Optics Release Correction — 2026-09-02

## Outcome

This controlled release correction removes player-projected foam, eliminates the coarse water-cell tile artifact, introduces a high-resolution flow/contact stencil, and makes eye lighting more readable and physically coherent in dark interiors.

## Water Architecture

The active path is:

```text
opaque scene depth + water pixel depth
                    |
                    v
        continuous contact separation
                    +
     flow-map convergence / surface activity
                    +
  2048² linear foam stencil, two rotated layers
                    |
                    v
       water-surface coverage composite
                    |
                    v
             atmospheric fog
```

The contact mask is evaluated per pixel from the opaque depth immediately behind the water surface. It no longer consumes the refracted receiver distance, which could be constant across a coarse water quad and reveal Skyrim's water mesh as large rectangles.

The foam artwork is sampled twice in absolute world space with different scale/orientation and explicit derivatives. Water flow advects the sampling coordinates. Neither player position nor camera rotation participates in coverage.

### Texture generation

- Final asset: `pipeline/Water Optics/Kernels/WaterOptics/FoamStencil2K.png`
- Generator: built-in image generation tool
- Runtime format: 2048x2048, 8-bit linear grayscale PNG loaded by DirectXTK/WIC
- Runtime mip policy: complete mip chain generated on upload
- Binding: PS `t66`; WaterOptics caustics remain at `t65`

Final prompt:

> Use case: stylized-concept. Asset type: seamless tileable game texture / grayscale shader mask for physically simulated river whitewater foam. Create a high-resolution seamless grayscale foam stencil texture for a real-time water shader: pure black background with organic white and gray lace-like filaments, broken bubbles, thin branching ridges, irregular cellular loops, and sparse turbulent clusters at multiple scales. Keep it direction-neutral, orthographic, free of baked lighting, seamless on all edges, and suitable for BC4-style compression and mipmapping. Avoid square tiles, checkerboards, rectangles, radial composition, directional bias, perspective water, blue colour, baked highlights, hard clipping, and uniform sine bands.

## Eye Optics Architecture

The eye remains a normal Skyrim eye material with PIXL's corneal shell. This correction changes three bounded behaviours:

1. Maximum dark-adapted pupil radius is reduced so interior pupils do not consume an implausibly large fraction of the iris.
2. Corneal roughness is broadened to avoid tiny mirror-like highlights and unstable close-up reflections.
3. Authored/local environment reflection is multiplied by dielectric Fresnel, not by diffuse illumination. Iris colour and corneal reflection therefore no longer collapse or flare together as exposure changes.

A small ambient-proportional ocular-scatter term improves sclera/iris readability. It cannot emit light because it is derived exclusively from existing ambient irradiance. The final release follow-up also wraps only the low-frequency sclera-edge ambient lookup toward the viewer at grazing angles, preventing eyelid-adjacent pixels from collapsing to charcoal while leaving direct light, geometry and the corneal specular normal unchanged. Far-eye corneal roughness is widened to reduce subpixel mirror/glint aliasing.

## Modified Files

| File | Purpose | Change | Validation |
| --- | --- | --- | --- |
| `distribution/Shaders/Water.hlsl` | Water surface PS | Per-pixel contact depth, two-layer stencil, no player wake | FXC refractive flow-map PS passed |
| `engine/Modules/WaterOptics.cpp` | Resource/UI/config owner | WIC texture load/mips, `t65-t66` bind, wake UI removed, compatibility lane forced to zero | PIXL native target passed |
| `engine/Modules/WaterOptics.h` | CPU settings/resource ABI | Added SRV only; 64-byte settings ABI preserved | C++ static assertion and native build passed |
| `pipeline/Water Optics/Module.ini` | Selective cache version | `1-2-2` to `1-3-0` | Limits invalidation to owned PS families |
| `distribution/Shaders/EyeRendering/EyeRendering.hlsli` | Eye optical constants | Pupil and roughness calibration | FXC Eye PS passed |
| `distribution/Shaders/Lighting.hlsl` | Eye BRDF/integration | Ambient ocular scatter and Fresnel-correct environment reflection | FXC Eye PS passed |
| `pipeline/SkinOptics/Module.ini` | Selective cache version | `0-3-1` to `0-3-3` | Invalidates affected Lighting PS entries, including the release sclera-edge follow-up |

## Fidelity, Performance, Security, and Stability

- Fidelity: foam is a water-surface coverage layer rather than visible mesh-shaped patches; eyes retain authored colour with less mirror-like reflection.
- Temporal stability: absolute world mapping, explicit texture derivatives, generated mips, and no camera/player projection.
- Performance: the visible foam path adds two mask samples; no simulation texture, particle system, CPU trail, or per-player work is introduced.
- Memory: a single 2048² R8-class texture plus mips when WIC preserves grayscale (approximately 5.3 MiB); driver fallback conversion may use a wider compatible format.
- Security: the asset is local and immutable after load; no network, telemetry, process, registry, or unrelated filesystem access.
- ABI: shared FeatureData and `WaterOptics::Settings` sizes/field order are unchanged.

## Validation

- `git diff --check`: passed (line-ending notices only).
- Strict FXC Water refractive/flow-map and refractive/vertex-colour pixel permutations: passed.
- Strict FXC Lighting Eye/environment-map/world-probe/SkyBounce/dialogue pixel permutation: passed.
- Direct PIXLRenderer Release target with project references disabled: passed and linked `PIXLRenderer.dll`.
- Clean-cache release stage: passed; 310 files, generated asset and module descriptors present in the manifest.
- Live deployment: passed; all seven affected files match staged SHA-256 hashes.
- Full dependency build: blocked before PIXL compilation by the pre-existing FidelityFX DX11 permutation generator process exit `0xC0000409`; serial retry reproduced it. Existing generated FidelityFX libraries allowed the direct PIXL target to validate successfully.
- Runtime visual validation remains required for Whiterun river contact edges, bridge pilings, moving flow, dark interiors, dialogue close-up, TAA/DLSS/DLAA and frame generation.

## 5 Future Visual Improvements

1. Persistent aeration lifetime in a low-resolution world clipmap.
2. Vertical waterfall/rapid foam classification.
3. Material-aware shore foam colour and persistence.
4. Eye tear-film meniscus highlight near eyelid contact.
5. Iris depth/parallax calibration by texture family.

## 5 Future Performance Improvements

1. Share flow-neighbour samples with the normal-flow path.
2. Skip stencil sampling when depth/contact and convergence bounds are empty.
3. Use a BC4 DDS asset when the release asset converter is available.
4. Reduce the second stencil octave beyond a projected-size threshold.
5. Cache eye region weights in an existing interpolant only if ABI-safe evidence supports it.

## 5 Future Feature / Research Ideas

1. Multi-emitter boat and creature wake system with temporal vectors.
2. Foam contribution to PIXL reconstruction reactive confidence.
3. Temperature-dependent foam and ice-edge behaviour.
4. Per-eye asymmetric temporal pupil adaptation.
5. Optional corneal reflection debug view separating local cubemap and world probes.
