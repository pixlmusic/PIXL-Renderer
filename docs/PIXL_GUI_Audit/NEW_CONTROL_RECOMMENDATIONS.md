# PIXL Renderer New Control Recommendations

These are deliberately limited to parameters that provide useful fidelity/performance/behaviour choices. They are **recommendations**, not fabricated controls. None should be exposed until its complete settings, serialization, resource-lifetime, C++ upload and shader path is implemented and validated. Existing defaults below reproduce current behaviour.

| Module / effect | Parameter to expose | Current internal value | Proposed GUI label | Range | Default | Primary impact | Wiring required |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Natural Lighting | Inverse-square light radius scale | `NaturalLighting::InverseSquareRangeScale = 2.4` | Local Light Range | 1.0–4.0 | 2.4 | Fidelity and performance | Add serialized `NaturalLighting::Settings`; replace the header constant in `CalculateLightRadius`; feed profile contract only if a measured light-count budget is also enforced. |
| Natural Lighting | Visible light cutoff, with a separate shadow-caster-safe floor | `DefaultCutoff = 0.05`; `DefaultShadowCasterCutoff = 0.022` | Local Light Fade Threshold | 0.01–0.10 | 0.05 | Fidelity and performance | Settings/JSON/UI plus `CalculateLightRadius`; retain the lower shadow-caster cutoff internally or expose it only in Developer Mode. |
| Volume Occlusion | Shadow-copy resolution | 512x512 mip 0, 256x256 mip 1 | Volumetric Shadow Quality | Low 256 / High 512 / Ultra 1024 | High (512) | Fidelity and performance | Serialized enum; resource recreation in `VolumeOcclusion`; dispatch dimensions; memory/error fallback. Shader ABI need not change if dimensions are queried. |
| Volume Occlusion | Separable blur radius | Fixed 11-tap (radius 5) kernel | Volumetric Shadow Softness | 2–7 texels | 5 | Fidelity and performance | Add kernel radius/weights via a small CB or compile safe fixed variants; update both horizontal/vertical kernels and profile mapping. |
| Terrain Occlusion | Update budget | One 128-pixel stripe per frame | Terrain Shadow Update Rate | 1–4 stripes/frame | 1 | Temporal response and performance | Serialized integer; update scheduler/dispatch bounds; preserve deterministic wrap and avoid writing overlapping stripes. |
| Terrain Occlusion | Penumbra scale | Derived internally; no user setting | Terrain Shadow Softness | 0.5–2.0x | 1.0x | Fidelity | Add a CB scalar on both C++ and `TerrainOcclusion.hlsli` sides; do not reorder the existing ABI fields. |
| Terrain Seam | Depth-capture eligibility distance | `terrainDepthCaptureDistance = 2048` | Terrain Blend Distance | 1024–4096 game units | 2048 | Fidelity and performance | Serialized setting; replace the local constant in `TerrainSeam.cpp`; quality profile may scale it after live seam testing. |
| World Probes | Irradiance update work per frame | Fixed three-stage split beginning at `kIrradianceSplit = 2` | Environment Update Responsiveness | Efficient / Balanced / Responsive | Balanced (current split) | Temporal response and performance | Add enum and a scheduler table; maintain full mip coverage and reset semantics. This should not expose raw mip indices. |
| Radiant Grid | Clustered particle-light budget | `MAX_LIGHTS = 1024`, `CLUSTER_MAX_LIGHTS = 128` compile-time capacities | Particle Light Budget | 256 / 512 / 1024 visible lights | Ultra (1024) | Fidelity and performance | Add CPU culling budget below the fixed ABI capacity, profile mapping and diagnostics. Do not resize the structured buffers at runtime for the first implementation. |
| WindowLife | Near/medium/far interior representation | Current single interior path plus distance fade | Interior Detail | Performance / Balanced / Cinematic | Balanced matching current behaviour | Fidelity and performance | Add a serialized enum; select depth-reconstructed/plane/atlas work without changing detection; bind optional room depth/normal assets safely and retain current fallback. |
| WindowLife | Optional building-specific room-art overrides | Automatic layout now reconstructs window shape from the installed native glow texture and falls back to a 110x140 geometry calibration; room-family selection is still hash/location-family based | Interior Art Override | Automatic / Building Profile | Automatic | Fidelity | Add a non-invasive building/material profile lookup that selects approved atlas families without changing the 192-byte draw ABI. Keep automatic shape reconstruction authoritative and require live coverage across segmented, large and shuttered windows. |
| Pixel Capture | Tile/temporal memory ceiling | Derived from scale, sample count and output resolution | Photo Capture Memory Budget | 512 MB–8 GB or Auto | Auto | Performance and stability | Add preflight VRAM estimation, clamped plan and user warning; no shader ABI change. |

## Reviewed modules where no new public control is recommended

- **Horizon Blend and Terrain Field:** automatic correctness/data-provider modules. A toggle would mostly let users break cross-module assumptions.
- **Sky Bounce:** the three useful artistic limits are already exposed; reset/rebuild is now automatic when required.
- **Light Volumes:** enable, quality and explicit custom dimensions already cover fidelity/performance needs.
- **Hybrid GI:** already has a very large advanced surface. Further raw controls would reduce coherence; future work should favour safer presets and better tooltips.
- **Ground Response:** its public surface is already extensive. Remaining work is spell/interaction coverage and live behaviour, not more sliders.
- **Foliage Dynamics:** lighting, material, coverage and multi-scale wind parameters already exist. The remaining issue is visual/compatibility validation, not missing UI.
- **Water Optics:** SSR and caustic fidelity/performance controls are already present, and Water quality now changes the actual trace budget.
- **Camera Suite:** exposure, bloom, DOF, lens, motion and bodycam controls are already exposed. Current DOF concerns require runtime correction rather than another control.
- **Image Reconstruction:** backend, quality, sharpening, frame generation, frame pacing and latency choices are already exposed. Hardware validation is more valuable than additional switches.
- **Skin Optics, Tissue Diffusion and Strand Shading:** character-material controls are comprehensive; quality profiles already provide the simplified public path.

## Release policy for new controls

1. Defaults must reproduce the accepted Ultra baseline exactly.
2. A quality-profile mapping must change measurable cost before a parameter is marketed as a performance control.
3. Resource dimensions or ABI-affecting values require safe recreation/versioning on both C++ and HLSL sides.
4. Raw debug views, cache regeneration and authoring tools remain Developer-only.
5. Any control whose result is scene-, vendor- or mod-dependent remains marked for live validation until tested in Skyrim.
