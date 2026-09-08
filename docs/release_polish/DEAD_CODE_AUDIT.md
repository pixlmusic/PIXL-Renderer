# Dead Code Audit

No code was removed solely because it appeared unused. The shipping Strand Shading path remains authoritative and the retired Hair Reconstruction ABI stub remains source-only for cached-layout auditability. The release staging script continues to exclude descriptors explicitly marked `Pipeline = Retired`.

The duplicated engineering controls in the Tuning Workspace remain intentionally available for diagnostics; the public Camera page is now the supported user path. Removing those diagnostics during final polish would add regression risk without runtime benefit.

No new dead settings were introduced: every public reconstruction/NR/FG control maps to serialized Image Reconstruction state and a current runtime consumer.

## 2026-09-07 disposition

Hair Reconstruction is the only descriptor marked `Pipeline = Retired`. It is absent from `RenderModule::GetModuleList`, generated shipping module metadata and the packaged module catalog. Its 128-byte CPU/HLSL reservation remains because deleting or resizing it could invalidate existing shared-buffer/cache layouts. Its dormant kernel is NOT included in the dated RC. The package include checker recognizes only the exact disabled include guard in Lighting.hlsl; an unguarded missing include still fails. An earlier undated staging experiment included the kernel and is superseded.

No other module was proven dead. Debug/profiler/benchmark paths remain intentionally gated and are not removed merely because normal gameplay does not exercise them.

### Proven unused buffer helper removal

Removed the unused global StructuredBuffer class and both StructuredBufferDesc template overloads from engine/Buffer.h. Native/header/tool/build/distribution reference searches found definitions only, no instantiations, subclasses, construction, serialized factories or runtime registrations. HLSL StructuredBuffer declarations are a different language construct and were not changed. The private engine include directory does not expose this as an installed inter-plugin API. A full affected native rebuild/audit passed after removal. Active ConstantBuffer, Buffer and Texture wrappers remain. See REMOVED_CODE.md for defects, validation and recovery. This does not declare any rendering module dead.
