# Shared buffer wrappers — September 7

Full source: engine/Buffer.h. Internal native header, included through renderer/module infrastructure; no installed public SDK. Active ConstantBuffer/Buffer/Texture1D/Texture2D/Texture3D wrap D3D11 resources and views with winrt::com_ptr, device/context from Globals and DX failure throwing. Construction/upload/view creation feeds many passes, not a standalone visual pass. Callers own dimensions, formats, binding, resize and shutdown order. No shared shader ABI changed.

## Five investigations

1. **Proven uninstantiated StructuredBuffer wrapper — removed, LOW.** Searches across engine/include/tools/cmake/distribution found only definitions for global StructuredBuffer and its two descriptor helpers: no instance, subclass, factory or runtime string registry. It is not the HLSL type. Removed wrapper included invalid usage/CPU-write combinations, an ignored data_size and incorrect object-address list upload. Active Buffer class remains. Full affected production rebuild/link/audit passed. No runtime performance/VRAM improvement is claimed for removal of uninstantiated code. Original source recoverable from Git HEAD.
2. **COM ownership — retained.** Views/resources use com_ptr; its put() releases old values. Adding manual reset calls is redundant. Texture2D(raw pointer) adopts a reference with attach; changing to AddRef without auditing callers could leak. Caller ownership inventory remains future work (HIGH risk).
3. **Constant upload bounds — open.** Dynamic Update copies caller size without checking ByteWidth; default UpdateSubresource ignores size. Rounded allocation may exceed a small typed source. No direct ConstantBufferDesc(...false) calls found in initial search, not exhaustive indirect proof. Need full caller-size tracing and failure policy before hot-path behavioral change; MEDIUM/HIGH if a bad caller exists.
4. **Alignment/format costs — retained.** 64-byte rounding is more conservative than minimum constant-buffer alignment; reducing it without confirming engine/vendor assumptions yields little demonstrable benefit. Tiny overhead per resource, no measured aggregate. Generic texture formats/mips belong to module descriptors, not wrapper defaults.
5. **Error/device lifecycle — open.** Constructors throw on D3D failure; module callers must gracefully disable optional features. A global rewrite to nullable wrappers would silently break callers. Test allocation failure and resize ownership per module first. Resource names guard null/empty values; no per-frame logging added.

## File accounting and impact

| File | Purpose/participation/dependencies | Quality/findings | Implemented/rejected | Visual/performance/security/risk | Validation/future |
| --- | --- | --- | --- | --- | --- |
| engine/Buffer.h | Shared D3D11 resource/constant/view wrappers; Globals device/context, DX errors, winrt and resource naming | Full read; RAII mostly sound; unchecked upload size and adopting-pointer contract require callers | Removed only proven unused StructuredBuffer and descriptor overloads; retained live wrappers and alignment | No scene/GPU ABI changes; no measured timing or allocation savings; removes dormant unsafe helper surface, LOW cleanup | build/final-release-records/unused-buffer-cleanup-build-20260907.log exit 0; map active allocation sizes/lifetimes and add bounds/ownership tests |

No files, runtime assets or game content deleted. See REMOVED_CODE.md and DEAD_CODE_AUDIT.md for search scope and inherited-cleanup provenance.

## 5 Future Visual Improvements

1. Resource-format reference captures; 2. resize history validation; 3. initialization-content tests; 4. debug resource naming in captures; 5. neutral optional-resource fallbacks.

## 5 Future Performance Improvements

1. Persistent resource inventory; 2. allocation frequency counters; 3. descriptor reuse analysis; 4. constant upload byte accounting; 5. module-by-module lifetime/VRAM overlap analysis.

## 5 Future Feature / Research Ideas

1. Explicit adopt/addref tags; 2. checked typed constant uploads; 3. device-failure injection; 4. dynamic resource budget diagnostics; 5. resize/lifecycle tests with WARP.
