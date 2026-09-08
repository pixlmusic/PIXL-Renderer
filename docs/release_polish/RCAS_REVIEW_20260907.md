# DLSS RCAS sharpening — current-pass review

Scope: complete RCAS C++/header/HLSL, complete ImageReconstruction header, and the reconstruction implementation's sharpening UI, allocation, initialization and post-processing callsites. The rest of ImageReconstruction.cpp is not yet fully reviewed.

## Purpose and pipeline position

```text
Current-frame DLSS -> sharpenerTexture (full output resolution, kMAIN format)
  -> RCAS available + enabled + strength > 0: compute -> kMAIN UAV
  -> otherwise: unchanged copy -> kMAIN texture
  -> original Skyrim post-processing / Camera Suite framebuffer redirection
```

Optional inputs: current reconstruction reactive mask, transparency mask, copied motion vectors and dynamic input rectangle. RCAS owns a compute shader and constant buffer, not a history texture. Its configuration is 16 bytes (float attenuation at 0, uint confidence at 4, float2 dimensions at 8), rounded to 64 bytes by the existing generic constant-buffer allocator. Bindings remain b0, t0–t3, u0. Compute group is 8x8. No register, history, motion-vector generation, reconstruction timing or shader equation changed.

## Five substantial investigations

1. **Lost reconstructed output on shader failure — MEDIUM, fixed.** Previously ApplySharpen returned early if compilation failed, but its caller skipped the fallback copy. RCAS now returns whether it dispatched, and the caller resolves unchanged output when false. Expected visual result: a valid unsharpened image during this optional failure instead of unresolved kMAIN content. Normal successful output is unchanged. No extra normal-path copy; failed path costs the same full-resolution copy as disabled sharpening. In-game failure injection remains pending.
2. **Per-frame failure logging — LOW, fixed.** The missing-shader warning ran every attempted frame. It now runs during shader creation and explains the fallback. Reduced failure-path logging/I/O; no normal image impact. A failed shader remains unavailable until normal resource/reinitialization handling; this does not add automatic retries or hot-reload support.
3. **Unsafe caller attenuation — LOW/MEDIUM, fixed.** Out-of-range finite attenuation could make the resolve denominator approach or cross zero. Clamp the CPU payload to [0,1]; reject nonfinite attenuation so the caller copies instead. UI slider values already map into this range, so supported settings keep their behavior. Tests include infinity/NaN and bounds under MSVC /O2 /fp:fast. No HDR color clamp was introduced.
4. **Optional confidence validity — LOW, fixed.** Missing required views, constant buffer or shader now returns false. Nonfinite dynamic dimensions disable the optional confidence branch, matching its existing missing-input behavior and avoiding an undefined float-to-uint shader conversion. This does not independently validate mask resource extents, motion conventions or stale-frame data; those remain reconstruction-wide review items.
5. **Shader numerical/ABI regression evidence — verification implemented; new visual algorithm rejected for this pass.** The existing shader already guards reciprocal limiters, clamps neighbor coordinates, attenuates unstable surfaces/motion and bounds dispatch writes. Compiled the exact unchanged HLSL with strictness, optimization level 3 and warnings-as-errors, reflected layout/registers/group size, then executed 20 D3D11 WARP cases. Flat black, gray, white and HDR (2 and 128), zero attenuation, full reactive/transparency rejection, large motion and odd 17x9 dimensions passed. No speculative lobe/HDR remapping rewrite is justified by these tests. WARP is software execution, not RTX performance or real scene temporal validation.

## File-by-file record

| File | Purpose / dependencies / quality | Findings and changes | Fidelity / performance / security / risk / future | Validation |
| --- | --- | --- | --- | --- |
| engine/Modules/ImageReconstruction/RCAS/RCAS.h | Full; ownership and dispatch interface, Buffer/State/D3D11 | Documented nodiscard bool result so caller cannot silently ignore failure | No image/math change. Raw owning CB means copying RCAS would be unsafe; current static instance is not copied. Future unique ownership after lifecycle tests. MEDIUM integration change. | Production build and exact-header unit test |
| engine/Modules/ImageReconstruction/RCAS/RCAS.cpp | Full; compile/init/upload/dispatch/unbind | Fallback signal, bounded attenuation, finite dimensions, initialization-only warning | No new external access; existing local shader compile. One full-resolution dispatch, no per-dispatch heap allocation. D3D Map/creation exceptions still rely on surrounding renderer handling; this change only handles missing resources. | Native control-flow test and Release build |
| pipeline/ImageReconstruction/Kernels/ImageReconstruction/RCAS/RCAS.hlsl | Full; HDR spatial filter plus optional confidence loads | No change; existing reciprocal/edge guards retained | Approximately five source loads plus three optional confidence loads/output pixel. Center/source size equality relies on caller. Alpha forced to 1 remains existing contract. No temporal history. Future mixed-sign HDR and real mask/motion tests. | Actual HLSL compilation, reflection, 20 WARP cases |
| engine/Modules/ImageReconstruction.h | Full declaration review; backend state, settings, textures, hooks | No source change. Old ApplySharpening comment incorrectly says only called for strength > 0; implementation also resolves disabled output | Default sharpening off. Raw resource ownership, reset atomics and backend session state require implementation review. No static timing/ABI certification from declarations. | Header compiled with changed RCAS interface |
| engine/Modules/ImageReconstruction.cpp | Partial; named caller/UI/setup regions only | Fallback copy after failed sharpening | MEDIUM controlled failure-path fix; copy source/destination are allocated from matching kMAIN descriptor. No frame-generation order change. Full resource/resize/hook/settings review pending. | Exact caller test, native build; no Skyrim launch |
| tools/TestPixlRCAS.ps1 | Full; new exact-source RCAS/header/caller test with recording stubs | Tests failure fallback, no log spam, success dispatch, disabled/zero/no-UAV/nonfinite paths, bounds and confidence validity | Offline/local, unique ignored build directory; no live configuration/network/process injection. Compiler command uses supplied local VS environment. No D3D execution in these unit stubs. | /W4 /WX /O2 /fp:fast passed |
| tools/TestPixlRCASShader.ps1 | Full; new actual D3D11 software shader test | ABI reflection and 20 shader executions with readback | Offline local WARP, no game access or performance claims. Shader loads are read-only; generated C++/EXE stored below unique build directory. Extend formats/size/precision tests in future. | /W4 /WX native build; HLSL warnings-as-errors; all assertions passed |

## Evidence

- Production build/audit: `build/final-release-records/rcas-fallback-build-20260907.log`, exit 0; no compiler/link warning/error matches.
- DLL: 19,669,504 bytes, SHA256 `A194CA429EEE2342FCB648183AFFFAAD2673B74A7404A54F9620BDE225DBE031`. This is newer than staged RC-03 and is not deployed.
- Exact-source test: `build/rcas-tests-9ac5c26b3f9642f7b391bb6bdcf99be5`.
- WARP test: `build/rcas-shader-tests-ce5b67b4505f4e2798928a3a5b464d14`; shader SHA256 `F72BAB0ABCDCDFD2DCD0C3661FE3C36D461B4894F32467F39EDE531359BC596C`.
- Initial test-harness compilation failed because its Windows includes needed NOMINMAX and an explicit string header. Those harness issues were fixed; the final run passed without suppressing warnings. Production shader/native compilation did not fail.

## 5 Future Visual Improvements

1. Mixed-sign HDR stress cases; 2. actual reconstruction guide validation; 3. foliage temporal capture comparisons; 4. highlight/halo reference images; 5. source/output size mismatch diagnostics.

## 5 Future Performance Improvements

1. GPU timestamp measurements at 1080p/1440p/4K; 2. bandwidth profiling for optional masks; 3. assess confidence exp2 cost; 4. avoid unnecessary RCAS compilation when disabled only if hot enabling remains reliable; 5. benchmark supported lower-precision resource formats without modifying the current HDR contract.

## 5 Future Feature / Research Ideas

1. Device-loss harness; 2. shader reload lifecycle test; 3. resize/quality-switch tests; 4. failure status in UI; 5. camera-cut/disocclusion sequence test fixtures.
