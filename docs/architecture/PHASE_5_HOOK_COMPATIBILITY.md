# Phase 5 — Hook and Runtime Compatibility

## Outcome

PIXL now has a central hook result registry and runtime capability view. The change adds observability around existing hooks without relocating patches, changing thunk ABIs or weakening failures from required renderer interception.

## Architecture

- `HookRegistry` records hook/group name, owner, relocation description, feature impact, patch size, required state, resolved address, result and diagnostic detail.
- Result states distinguish `VALIDATED`, `INSTALLED`, `DISABLED`, `UNSUPPORTED`, `SIGNATURE_MISMATCH` and `RELOCATION_MISSING`.
- Runtime reporting identifies SE, AE or VR and the exact executable version selected by CommonLib.
- Optional byte-signature validation accepts only caller-supplied, runtime-verified signatures. It guards the memory range with `VirtualQuery` before comparison and supports masked bytes.
- Developer UI exposes the capability matrix and hook results with ownership and impact details.

## Instrumented Hooks

- The required core renderer hook set reports validation start and successful installation.
- Early D3D11 and DXGI IAT interception report independent results. D3D11 device interception is explicitly `DISABLED` when Image Reconstruction owns that path; this is intentional ownership, not a failure.
- Foliage Optimizer reports unavailable resources as `DISABLED` and its quarantined SE hook ABI as `UNSUPPORTED`, retaining vanilla grass.

Individual core call sites are still installed exactly as before. They are represented as a required group because verified per-runtime byte signatures do not yet exist for every site. Inventing signatures or treating addresses from another executable as equivalent would be less safe. Fine-grained records should be added only as each site's bytes are verified.

## Failure Policy

Required core renderer hooks retain their established fail-fast behavior. Continuing after a partial required patch set would leave an incoherent renderer.

Optional systems are capability-gated before mutation where currently possible. Foliage Optimizer remains fail-closed on the unsupported SE ABI and preserves vanilla rendering. A partially installed multi-patch hook set cannot be safely rolled back; transactional preflight for every relocation is deferred until verified signatures are available.

## Runtime and ABI Impact

- No relocation IDs or offsets changed.
- No thunk signature, vtable index, patch byte or hook ordering changed.
- No public module API, settings key, shader register, shader entry point or cache ABI changed.
- UI work is developer-only and read-only.

## Validation

- `tools/TestHookRegistry.ps1` validates metadata, statuses, guarded signature checks, ownership handoff and fallback contracts.
- Release `PIXLRenderer.dll`: built successfully after regenerating the CMake source glob for the new registry implementation.
- `PIXL-Audit`: passed; all 42 shipping modules retained.
- `git diff --check`: no whitespace errors (repository line-ending conversion notices remain).
- Live Skyrim hook inspection across supported runtimes: pending.

## Deferred Work

- Author executable-specific signatures only from verified binaries.
- Split the core aggregate record as individual hook sites gain reliable metadata.
- Add a preflight transaction for optional multi-patch sets before any write occurs.
- Validate Wine/CrossOver protection behavior on an actual supported environment rather than importing an untested upstream workaround.
