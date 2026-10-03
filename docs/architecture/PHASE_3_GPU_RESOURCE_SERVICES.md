# Phase 3 — Shared DX11 GPU Resource Services

## Outcome

PIXL now has a renderer-owned DX11 service for descriptor-matched transient textures and buffers, centralized dynamic-buffer uploads, and temporal-history registration. It is layered over the existing immediate-context renderer and does not require DX11.1.

## Architecture

- `GPUResourceServices::AcquireTexture/ReleaseTexture` provides bounded descriptor-exact texture reuse.
- `AcquireBuffer/ReleaseBuffer` provides bounded descriptor-exact temporary-buffer reuse.
- Generation-tagged handles reject stale releases.
- Idle resources are retired after 180 frames without moving live slots.
- Both pools are capped at 64 slots and fail safely when every slot is active.
- `UploadDiscard` validates buffer usage, access flags and byte bounds before using the established D3D11 `WRITE_DISCARD` path.
- `HistoryResourceRegistry` behavior is provided by named history registration, validity state and reset callbacks.
- Scheduler history, resolution and resource-recreation events feed the service.
- Developer diagnostics expose pool occupancy, create/reuse counts, upload traffic and history validity.

## Initial Migration

Distant Life emitter uploads now use the shared guarded upload path. Its GPU buffer, SRV, register contract, candidate selection and draw ownership remain unchanged.

Persistent history resources and special multi-view targets remain module-owned. Camera Suite and Hybrid GI resources are not transient merely because they are intermediate; migrating them without exact lifetime declarations would risk aliasing active history. DX11.1 range-bound constant-buffer suballocation is also deferred until a consumer can prove capability and fallback parity.

## Compatibility and Safety

- No shader, register, constant-buffer layout or cache revision changed.
- No blocking readback was introduced.
- No per-frame filesystem work was introduced.
- Pool matching compares every relevant DX11 descriptor field.
- Resolution changes retire only idle transient textures.
- Device/resource recreation clears pooled resources and invalidates registered histories.
- Optional allocation failure returns an empty handle for caller fallback.

## Validation

- Release `PIXLRenderer.dll`: built successfully.
- `PIXL-Audit`: passed; 42 shipping modules retained.
- `tools/TestGPUResourceServices.ps1`: static lifecycle, bounds, migration and stale-handle contracts.
- Changed HLSL: none; FXC validation not applicable.
- Package layout: unchanged.
- Live Skyrim validation and measured VRAM/resource reuse: pending.
