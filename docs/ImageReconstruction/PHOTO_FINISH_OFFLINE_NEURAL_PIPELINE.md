# PIXL Photo Finish Offline Neural Pipeline

## Purpose

Photo Finish is a bounded offline-quality still renderer inside the running game. Real-time frame rate is intentionally not a constraint while the transaction is active: Skyrim simulation is already paused by Director, and PIXL owns the camera and input until the final file is written.

## Final pipeline

```text
User requests capture
  -> reject if another capture/encode/save is active
  -> snapshot and freeze exact TFC translation + rotation
  -> swallow keyboard, mouse, controller and PIXL-menu input
  -> hide Director/full PIXL UI from clean capture frames
  -> when Photo NR is requested, temporarily enable Feature 18 and switch DLSS to DLAA/native input
  -> otherwise raise internal render scale (current / >=85% / native 100%)
  -> reset affected temporal histories
  -> render 0 / 4 / 8 / 16 lighting-and-post convergence frames
  -> begin deterministic low-discrepancy projection jitter
  -> render 8 / 16 / 24 fresh final-composite samples when NR is active
       -> each Feature 18 evaluation consumes a newly rendered PIXL/DLAA frame
       -> depth, motion, jitter and exposure remain aligned to that real frame
  -> copy only newly completed, race-free neural outputs
  -> jitter-aware temporal resolve
  -> final Native / 2x / 4x reconstruction and detail polish
  -> optional processed motion finish
  -> encode and save
  -> restore gameplay render scale/history, camera input and controls
```

## Controls

- **Capture internal render**: current gameplay scale, at least 85%, or native 100%. Neural Photo Finish always uses native DLAA input; the selector continues to control non-neural captures.
- **Lighting/post convergence**: off, 4, 8, or 16 fully rendered settling frames. Default is 8.
- **Capture Neural Final Composite**: optional and capability-gated. TAA, FSR, non-DLSS and unavailable Feature 18 sessions retain universal Photo Finish.
- **Neural temporal convergence**: 8, 16, or 24 independent Feature 18 outputs rendered from fresh DLAA/native-input frames and combined by the offline resolve. Recursive output feedback is deliberately not used because Feature 18 is a temporal reconstruction model and repeated invocation against unchanged guides progressively blurs detail.
- **Final output resolution**: Native, 2x, or 4x. The existing 48-megapixel safety ceiling may reduce 4x on very large inputs.

The same high-value controls are available in the Insert quick panel through numpad navigation.

## Resolution contract

Feature 18 remains at the vendor-validated display output extent. A prior attempt to invent an independent smaller output extent was rejected by the installed 310.8 runtime with `0xBAD00005`, so PIXL does not advertise a fake transformer-resolution selector.

Photo Finish increases quality in two supported domains instead:

1. It raises the real game render subrect before DLSS/Feature 18, up to native display resolution.
2. It reconstructs the saved still to Native/2x/4x using real deterministic sub-pixel samples after neural evaluation.

## Photo-only Feature 18 ownership

DLSS sessions provision PIXL's DX12 sidecar and Feature 18 resources at startup even when the real-time Neural Rendering master is off. A photo transaction then activates the model only for the frozen capture, selects effective DLAA quality without changing the serialized gameplay option, resets temporal histories, and restores the previous gameplay path after save completion.

Recursive model feedback was removed. Runtime testing showed that feeding a completed Feature 18 image back through the same temporal model with unchanged world guides progressively blurred the image. The replacement spends the same offline-time budget on independent, correctly guided neural frames and combines those results in PIXL's jitter-aware still resolve.

## Input and camera ownership

The capture lock begins before the clean-frame delay and ends only after reconstruction, encoding and file save report completion. During that interval:

- TFC translation and pitch/yaw are restored exactly every Director frame;
- camera smoothing velocity is zeroed;
- Skyrim and PIXL menu input are swallowed;
- HOME/END/Insert/numpad controls cannot alter or start another transaction;
- a second backend request is rejected;
- the full PIXL workspace closes before a capture requested from its Photo page.

## Validation status

- Release C++ build: PASS.
- Integrated `PIXL-Audit`: PASS, 38 modules.
- Shader/permutation impact: none; no HLSL or shader-cache version changed.
- Runtime still-image validation: pending.

Highest-value runtime checks:

1. Start with DLSS Quality/Balanced and real-time NR disabled. Enable only Photo Neural Rendering, use 8 convergence frames and 2x output, then verify the saved still uses NR.
2. Verify all camera/input controls remain inert until the save notification appears, then immediately recover.
3. Compare 8/16/24 fresh-frame convergence for detail stability, tonal drift and capture time; none should develop the cumulative blur of recursive feedback.
4. Confirm the saved file contains the neural final composite and no PIXL/Director UI.
5. Confirm DLSS quality and the real-time NR toggle return to their pre-capture behavior immediately after save.
6. Repeat with neural capture disabled and with TAA/FSR to confirm the universal path is unchanged.
