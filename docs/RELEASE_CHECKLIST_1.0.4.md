# PIXL Renderer 1.0.4 release gates

Status values: PASS, FAIL, MANUAL TEST REQUIRED, IN PROGRESS.

| Gate | Status | Evidence / next action |
|---|---|---|
| Baseline Release build | PASS | `PIXL-12C` built and `PIXL-Audit` passed before edits. |
| Final Release build | PASS | `PIXL-12C` generated version 1.0.4.0 DLL; `PIXL-Audit` passed with 37 shipping modules. |
| Exhaustive per-file C++/HLSL review | IN PROGRESS | The 735-file inventory is an automated scan, not a semantic review. The 421 `.cpp`, `.h`, `.hlsl`, and `.hlsli` files in `engine`, `pipeline`, and `distribution` have not all been individually traced and signed off. Do not claim this release gate passed. |
| Required shader cases | PASS (representative) | Strict FXC: 32 material, 32 grass, 72 landscape/water, 9 WindowLife, 588 compute cases. Full in-game permutation cache remains a manual gate. |
| Full runtime shader cache | MANUAL TEST REQUIRED | Candidate uses compile-on-device mode; `PIXL.Shaders.20260925.2` invalidates prior stages. Verify a complete in-game rebuild. |
| GUI code test | PASS | Registry/lifetime, scaled layouts, disabled controls, callback isolation. |
| GUI visual and input test | MANUAL TEST REQUIRED | Skyrim 720p–4K, input, extension state. |
| CPU/HLSL structure parity | MANUAL TEST REQUIRED | No new shared ABI in this pass; pre-existing WindowLife/material edits passed selected FXC suites, not live binding. |
| Resource/register runtime audit | MANUAL TEST REQUIRED | D3D debug layer and live draw inspection. |
| Photo Mode lifecycle | MANUAL TEST REQUIRED | Enter, capture, abnormal exit, restoration. |
| Video Mode lifecycle | MANUAL TEST REQUIRED | Video-to-Photo stops preview playback; verify Shift movement, POI selection/update, smooth speed ramp, input isolation, both mode switches and camera restoration in Skyrim. |
| SurfaceTides absent/present | MANUAL TEST REQUIRED | Verify V1 handshake log on river/lake draw. |
| Configuration migration | MANUAL TEST REQUIRED | Existing keys retained; default and five preset JSON files parse, but a 1.0.3a user profile still needs live load. |
| Debug features off by default | PASS (static) | Default JSON has zero for `DebugView`, `LegacyPhysicalDebugMode`, and `TerrainHeightDebugMode`; inspect live UI too. |
| Core ZIP | PASS | 341 archive entries; 340 manifest payloads verified, version 1.0.4. |
| Source ZIP | PASS | 1,016 entries; current commit and working-tree snapshot, required files present, generated/private files excluded. |
| FOMOD ZIP | PASS | 364 archive entries; XML schema, optional SurfaceTides bridge and notices validated. |
| SurfaceTides source companion | PASS | Matching 1.0.2 modified source archive built and hash verified. |
| Main-game deployment | PASS | Restaged DLL 1.0.4.0 hash `80DA4F9FA1263AD39996F9FEEEB6D123EC413D16A7BF2897CE5AA9D1734149CC` deployed with rollback backup; all 340 payloads hash-match the manifest. |
| Steam-game deployment | PASS | The same restaged DLL was deployed with a separate rollback backup; all 340 payloads hash-match the manifest. |
| In-game first/third person, cells, fast travel, weather | MANUAL TEST REQUIRED | Requires owner live session. |
