# PIXL Image Reconstruction Input Audit

Date: 2026-09-01

## Actual frame boundary

```text
Skyrim opaque/forward rendering + PIXL material/lighting modules
    -> Camera Suite scene-linear post processing
    -> EncodeTexturesCS at the current dynamic render extent
         depth: Skyrim non-reversed device depth
         motion: jitter-free UV displacement, camera + object motion
         reactive: TAA/detail hint with bounded contribution
         transparency: water/transparency classification
    -> DLSS/FSR reconstruction to display resolution
    -> display-resolution depth reconstruction
    -> HDR/output transform and renderer UI separation
    -> optional DX11/DX12 interop neural Feature 18 pass
    -> optional FidelityFX frame interpolation/presentation
    -> Present
```

The neural pass is deliberately downstream of spatial reconstruction. It consumes the
display-resolution scene plus the current encoded depth/motion guides. Feature 18's
output extent remains the display extent because the installed 310.8 runtime rejects an
independent scaled output contract (`0xBAD00005`). The supported workload selector is
therefore the real DLSS/scene input resolution, now exposed as **NR / DLSS Input
Resolution**.

## Guide contract

| Input | Producer | Encoding / units | Extent | Lifetime | Status |
|---|---|---|---|---|---|
| Scene colour | final pre-reconstruction PIXL scene | reconstruction input colour | dynamic render extent | current evaluation | Correct |
| Depth | Skyrim depth target | non-reversed device depth; smaller is nearer | dynamic render extent | through Present for Streamline | Correct |
| Motion | `EncodeTexturesCS` | current-to-previous UV displacement; jitter removed; camera motion included | dynamic render extent | through Present | Correct |
| Reactive hint | `EncodeTexturesCS` | bounded TAA/detail response | dynamic render extent | through Present | Correct; intentionally selective |
| Transparency hint | normal/water classification | scalar transparency confidence | dynamic render extent | through Present | Correct |
| Jitter | `ImageReconstruction::jitter` | negative current projection jitter supplied once | per frame | constants | Correct |
| Camera | cached unjittered view/projection | current and derived previous matrices | per frame | constants | Correct |
| Neural guides | DX11 shared depth/motion copies | same frame as display-resolution neural source | guide extent tracked explicitly | fenced through D3D12 evaluation | Correct |
| UI | separate SDR RGBA8 interop surface | independently brightness-corrected for HDR/FG | display extent | through Present | Correct |

## Motion and temporal notes

- The motion field uses the closest/longest valid representative from a 5x5
  neighbourhood. Streamline is told that the vectors are already dilated, are not
  jittered, are two-dimensional, and contain camera motion.
- History is reset for camera cuts/large translations, FOV changes, dynamic-resolution
  changes, reconstruction mode transitions and explicit module resets.
- Invalid or unreliable transparent detail is represented through the reactive and
  transparency hints rather than fabricating undocumented neural inputs.
- Hybrid GI now preserves valid reprojection history during rapid camera rotation;
  motion magnitude no longer shortens otherwise valid accumulation merely because the
  camera moved quickly.
- Radiant Grid particle emitters retain a short bounded submission history so a candle
  or fire cannot abruptly stop contributing local light solely because its billboard
  left the immediate submission set during a turn.

## Resolution control

The new GUI selector maps directly to PIXL's established reconstruction quality modes:

| Selection | Scene input scale | Intended use |
|---|---:|---|
| DLAA / Native 100% | 100% | maximum real input detail |
| Quality | DLSS quality contract | high-quality gameplay |
| Balanced | DLSS balanced contract | lower GPU load |
| Performance | DLSS performance contract | 4K / constrained GPUs |
| Ultra Performance | DLSS ultra-performance contract | diagnostic / extreme constraint |

Changing it resets DLSS, neural and FidelityFX temporal histories together. This is a
real geometry/lighting/model-guide workload change, not an unsupported rescale of only
the neural output.

## Remaining runtime validation

- A/B fast 180-degree turns with DLSS and with FSR3 frame generation.
- Thin foliage, precipitation, fire/smoke and water disocclusion at Quality/Balanced.
- Camera cut, loading transition, exterior/interior transition and photo-mode exit.
- Verify the displayed input dimensions match the selected quality contract.
- Validate that neural + frame generation maintains UI separation and presents the
  latest completed neural result rather than a prior-frame resource.

No claim of visual completion is made from static/build validation alone.
