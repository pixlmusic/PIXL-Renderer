# Image reconstruction and sidecar validation — September 9

Status: work in progress; not a release certificate. Do not infer visual,
performance, or all-GPU support from a successful compilation or menu launch.

## Confirmed findings

- The live DX12 directory combined Streamline 2.10.3 core/interposer/Reflex/PCL
  with sl.dlss_g 2.13.0. An isolated executable reproduced the failure at
  CreateSwapChainForHwnd with the installed SM86 proxy and this runtime.
- The identical executable passed with the coherent official 2.10.3 runtime.
  A second test also initialized and bound the independent D3D11 Streamline
  instance successfully, then created/presented/shut down the DX12 instance.
- The older first-present dump records C0000005 in nvwgf2umx.dll. The module
  location alone does not establish which caller violated a driver contract.
- The matched live runtime and renderer reached successful first Present at
  21:43:15 and remained responsive. A subsequent owner launch also reached
  first Present at 21:45:28. The owner identified a corrupt save separately;
  do not attribute that load failure to reconstruction without further evidence.
- The SM86 backend logged RTX 3060 Ti, actual_sm=86, active=true, and PTX SM86
  selection. Installation is not proof that interpolation kernels executed.

## Changes and scope

| File | Purpose and review scope | Result / remaining validation |
| --- | --- | --- |
| engine/Modules/ImageReconstruction/Streamline.cpp | Runtime loading, tags, latency and frame tokens; selected-path review | Reject mixed DX12 DLL versions before loading; return tag failures; reject zero extents/nonfinite scale; restore required DX12 PCL markers. Full file audit remains open. |
| engine/Modules/ImageReconstruction/Streamline.h | Backend declarations | Updated tag return contract; no shader ABI change. |
| engine/Modules/ImageReconstruction/DX12SwapChain.cpp | DX11/DX12 resources, queues and presentation; selected-path review | Separate native device from wrapper, require factory hook, bridge DLSS-G input completion, bound CPU allocator waits. Resize and COM lifetime audit remains open. |
| engine/Modules/ImageReconstruction/DX12SwapChain.h | Sidecar ownership and synchronization declarations | Added wrapper and allocator fence bookkeeping; no shader ABI change. |
| engine/Modules/ImageReconstruction.cpp | Backend selection, neural provisioning and UI state; selected-path review | Retain native device, distinguish configured interpolation from capability. Broad upscaler/transition review remains open. |
| engine/Modules/ImageReconstruction/NeuralRendering.h | NGX bridge interface; read | Existing unsupported/missing/fault status handling; runtime version gate retained. |
| engine/Modules/ImageReconstruction/NeuralRendering.cpp | NGX neural bridge; partial review | Third-party calls must receive native device; independent complete security/lifetime review remains open. |
| tools/SidecarSmoke.cpp | Standalone device/factory/present diagnostic; full read | Explicit external paths, checked exports/results; no game or shader cache access. Not an interpolation benchmark. |
| tools/BuildSidecarSmoke.cmd | Diagnostic build wrapper; full read | Finds Visual Studio with vswhere; links installed Windows libraries. |
| tools/TestPixlSidecarRuntime.ps1 | Version/hash package validator; full read | Matching set accepted; known mixed set rejected. No binary execution. |
| tools/StagePixlRendererStandalone.ps1 | Packaging; changed-boundary review | Validates DX12 runtime after shader/runtime copy. Existing packaging behavior retained. |
| pipeline/ImageReconstruction/Kernels/ImageReconstruction/StreamlineDX12/* | Official runtime package | Unmodified official 2.10.3 DLLs, NGX and license notices; binary internals not audited. |

Existing FidelityFX and shader implementations were not redesigned. No cache
contents were cleared. RCAS tests compile temporary test shaders, not the live
Skyrim cache. No speedup, image-quality improvement, or lower-GPU support is
claimed from these correctness changes.

## Evidence

- Official SDK: https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.3
- Manual hooking: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- DLSS-G input lifetime: vendored include/sl_dlss_g.h, DLSSGState fence contract.
- build/sidecar-dual-2.10.3.log: dual-runtime startup/present test.
- build/sidecar-dual-native-device.log: native/proxy ownership variant test.
- build/sidecar-present-fence.log: dual-runtime startup, three present-thread
  state queries and three input-completion queue waits all succeeded, exit 0.
- build/rcas-tests-ec9db7e3e81e4f9a8557525ae0996965: exact-source fallback tests PASS.
- build/rcas-shader-tests-0911557d3fb64be6bbd5705c83501d42: 20 WARP cases PASS,
  reflected bindings/ABI and edge dispatch; includes HDR/black/white/motion masks.
- checkpoints/sidecar-matched-runtime-20260909-214249: prior live DLL/PDB,
  DX12 runtime and saved graphics configuration.

Pre-overlay candidate DLL SHA-256:
`EE712408C38CEB60C4553283A7F49581E1CF4F4ECA67F2FFC4A92C4580443D5F`.
It includes native/proxy device separation and the input-completion fence bridge.
The live startup-tested DLL SHA-256 is
`F0E08ED44C3A1E709D58662DA5B2535010F23B98EB794573E150A8D203D66DC0`.
The final overlay follow-up includes those synchronization changes and was
deployed after Skyrim exited, with DLL/PDB hash verification. Live DLL SHA-256:
`CB93FBED930F81C878DA2679A4D332FE5913F2817168DB3504101EE21F596E63`.
Rollback: `checkpoints/sidecar-ui-before-20260909-215615`.
The saved menu-FG option remains false. Visual verification is pending.

## Open release gates

### PIXL settings overlay follow-up

The owner supplied an image with a blurred window rectangle but missing
controls. The late sidecar overlay bound Skyrim's old kFRAMEBUFFER while the
blur wrote to the shared presentation target. CameraSuite now binds that final
target and its full-resolution viewport for the overlay. The sidecar blur uses
the final image and no longer composites/clears the separate UI input again.
The Frame Generation in Menus policy now includes PIXL's IsEnabled window;
the owner's saved option was already false. Live neural evaluation also pauses
for that window except explicit photo processing. Shader sources/cache remain
unchanged. Build validation does not replace an owner visual retest.

Selected-path follow-up review: engine/Menu/BackgroundBlur.cpp (blur target/UI
ownership), engine/Modules/CameraSuite.cpp (late ImGui target/viewport), and
engine/Modules/ImageReconstruction.cpp (settings-window suspension policy).

1. Known-good save with TAA, FSR, DLSS and DLAA; menu/world/loading transitions.
2. Actual DLSS-G output, on/off transitions, frame pacing and long-session stability.
3. FSR3 regression and neural-only regression after synchronization changes.
4. DLSS-G + neural rendering together, including failure fallback and memory cost.
5. Resize/alt-tab, HDR, first/third person, fast travel and scene discontinuities.
6. Wider hardware testing: only RTX 3060 Ti startup was tested here. Do not widen
   the existing neural GPU/version gate based on this test.
7. Third-party runtime offline/privacy behavior: the stock SDK was observed
   invoking its NGX updater despite optional OTA flags being omitted. NGX logs
   also mention telemetry. This is vendor behavior, not new PIXL networking;
   the package must not be advertised as fully audited/offline until resolved.
8. Exhaustive per-file review and supported shader permutation coverage remain open.

## DLSS-G multiplier and HUD follow-up

Added saved `dlssgGeneratedFrames` (default 1 additional frame) with 2x/3x/4x
controls in both reconstruction settings views. Options are capped by the
present-thread SDK capability query; changing the multiplier also invalidates
the options cache. The SM86 dual-device smoke test reported a maximum of three
generated frames (4x output). This verifies capability reporting, not actual
interpolation performance or image quality.

Corrected CameraSuite's HUD-composite suppression to apply only to the FidelityFX
presenter, which owns its UI composite. DLSS-G now receives the complete real
frame and the separate UI-color/alpha guide. Removed the incorrect HUD-less tag
that pointed to the complete backbuffer. This targets disappearing crosshair and
compass pixels without changing the FSR present path. Classification: B,
controlled correctness improvement requiring owner visual validation.

Reviewed the settings serialization, both UI controls, Streamline options/tag
contract, DX12 caller/state query and CameraSuite composite ownership together.
No CPU/shader layout, HLSL, cache or third-party source changes were needed.
Release build succeeded; the rebuilt dual-device startup/FG-off smoke test passed.
Generated output at each multiplier, crosshair/compass visibility, FSR regression
and menu transitions still require in-game testing. Existing vendor NGX loader
warnings remain in the smoke log; this is not a full neural-runtime certification.

## 5 Future Visual Improvements

- Validate guide extents against runtime resolution changes.
- Measure HUD-edge interpolation artifacts.
- Compare neural results under disocclusion and camera cuts.
- Tune sharpness using motion/temporal confidence with captured comparisons.
- Validate HDR neural/tone-map ordering before enabling unsupported combinations.

## 5 Future Performance Improvements

- Measure input-completion waits and queue overlap.
- Profile neural cost at each supported render scale.
- Measure frame pacing rather than relying on overlay FPS.
- Evaluate safe buffering of interpolation input textures.
- Budget neural memory against frame-generation working sets on 8 GB GPUs.

## 5 Future Feature / Research Ideas

- Automated scene replay across reconstruction backends.
- Repeatable device-loss/fallback tests.
- A supported hardware/runtime compatibility manifest.
- An in-menu distinction between armed FG and measured generated output.
- A vendor-runtime privacy/offline validation procedure.
