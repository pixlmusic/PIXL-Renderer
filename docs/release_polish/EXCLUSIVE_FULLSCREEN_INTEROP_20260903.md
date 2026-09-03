# Exclusive Fullscreen Interop — 2026-09-03

## Finding

PIXL explicitly disabled its D3D11-to-D3D12 presentation sidecar whenever Skyrim's startup swap-chain descriptor requested exclusive fullscreen. DLSS Super Resolution could still use the ordinary D3D11 Streamline path, but Neural Rendering and FidelityFX Frame Generation could not start because both depend on PIXL's DX12 sidecar.

The bundled FidelityFX frame-interpolation swap-chain implementation supports creation with a `DXGI_SWAP_CHAIN_FULLSCREEN_DESC`, forwards fullscreen state, detects exclusive presentation, and omits `DXGI_PRESENT_ALLOW_TEARING` in exclusive mode. PIXL had been passing a null fullscreen descriptor and rejecting exclusive mode before reaching this supported SDK path.

## Runtime result

The experiment successfully created the sidecar and rendered in exclusive fullscreen, but an Alt-Tab caused a repeatable access violation inside `dxgi.dll` and could leave Skyrim owning the display above Task Manager. PIXL logging stopped before a recoverable module fault, confirming the failure occurred in the native DXGI mode/ownership transition rather than an ordinary optional-feature error.

## Release decision

- Borderless/windowed flip-model presentation remains the only NR/FG sidecar path.
- Exclusive fullscreen continues to run ordinary PIXL/DLSS rendering, but Neural Rendering and Frame Generation report unavailable until the user selects borderless and restarts.
- Sidecar creation no longer passes an exclusive fullscreen descriptor.
- The release UI retains a concise, truthful borderless requirement instead of exposing an unsafe path.

## Constraint

PIXL's shared D3D11/D3D12 resources, swap-chain buffers and vendor registrations are established at startup. Switching presentation ownership live would require a substantially broader resource/state rebuild and is deferred until it can be proven safe across focus loss, device loss, VRR and vendor overlays.

## Performance expectation

No frame-time improvement is claimed. On modern Windows, flip-model borderless presentation can already receive independent-flip behavior, while the tested exclusive path adds a severe reliability regression.

## Validation

1. Exclusive sidecar creation: passed.
2. Neural Rendering output before focus loss: passed.
3. Alt-Tab/focus transition: failed repeatably in `dxgi.dll`.
4. Release rollback to borderless-only sidecar provisioning: native build passed; live runtime smoke test remains required.
