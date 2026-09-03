# Final Release UI and Eye Pass — 2026-09-03

## Outcome

This pass promotes the owner's current live-tested graphics configuration, corrects dark sclera edges, and reduces the normal-play UI to a clear Quality → Camera workflow.

## User experience

- Four real PIXL reference captures are packaged for Enhanced, Balanced, High and Ultra profile hover previews.
- The Ultra+ capture appears when Neural Rendering is hovered.
- Camera finishing/viewfinder controls are followed directly by reconstruction, DLSS/FSR, NR, frame generation and pacing.
- Engineering controls remain available but sit behind `ADVANCED / SUPPORT`.
- First launch explains the PIXL menu and Director keys and permits menu-key rebinding.
- Later launches show a short bottom-right, non-interactive control reminder that fades away automatically.
- `Alt+N` toggles Neural Rendering only on a supported NVIDIA/DLSS session and returns an honest live/restart/unsupported HUD message.

## Eye correction

The sclera-edge defect was caused by sharply curved legacy eye normals sampling very little directional ambient at the limbus. PIXL now wraps only the eye's ambient lookup toward the view at exposed grazing sclera pixels. Direct light, corneal reflection direction and the mesh remain unchanged. Ambient ocular scatter is colour-preserving, and far-eye corneal roughness is increased to reduce unstable subpixel reflections.

## Presentation safety

Exclusive-fullscreen sidecar support was withdrawn after a repeatable Alt-Tab access violation in `dxgi.dll`. Ordinary exclusive PIXL/DLSS remains available, but NR/FG requires borderless presentation for this release.

## Validation

- Native `PIXLRenderer` Release build: passed.
- Strict FXC `Lighting.hlsl` Eye pixel permutation: passed.
- Default/preset JSON parsing and obsolete-key audit: passed.
- Clean-cache staging and 317-file manifest/package audit: passed.
- Live DLL/default/profile/shader/preview hash verification: passed.
- Final archive SHA-256: `F92D5E2A56F4449492C6C0B770C595E2A495CB37635D4C252A1823CBE469E70C`.
