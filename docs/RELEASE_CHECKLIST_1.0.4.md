# PIXL Renderer 1.0.4 release gates

Status values: PASS, FAIL, MANUAL TEST REQUIRED, IN PROGRESS.

| Gate | Status | Evidence / next action |
|---|---|---|
| Baseline Release build | PASS | `PIXL-12C` built and `PIXL-Audit` passed before edits. |
| Final Release build | PASS | `PIXL-12C` generated version 1.0.4.0 DLL; `PIXL-Audit` passed with 37 shipping modules. |
| Required shader cases | PASS (representative) | Strict FXC: 32 material, 32 grass, 72 landscape/water, 9 WindowLife, 588 compute cases. Full in-game permutation cache remains a manual gate. |
| Full runtime shader cache | MANUAL TEST REQUIRED | Candidate uses compile-on-device mode; new `ShaderRevision` invalidates 1.0.3a stages. Verify a complete in-game rebuild. |
| GUI code test | PASS | Registry/lifetime, scaled layouts, disabled controls, callback isolation. |
| GUI visual and input test | MANUAL TEST REQUIRED | Skyrim 720p–4K, input, extension state. |
| CPU/HLSL structure parity | MANUAL TEST REQUIRED | No new shared ABI in this pass; pre-existing WindowLife/material edits passed selected FXC suites, not live binding. |
| Resource/register runtime audit | MANUAL TEST REQUIRED | D3D debug layer and live draw inspection. |
| Photo Mode lifecycle | MANUAL TEST REQUIRED | Enter, capture, abnormal exit, restoration. |
| Video Mode lifecycle | MANUAL TEST REQUIRED | POIs, playback, capture abort, camera restoration. |
| SurfaceTides absent/present | MANUAL TEST REQUIRED | Verify V1 handshake log on river/lake draw. |
| Configuration migration | MANUAL TEST REQUIRED | Existing keys retained; default and five preset JSON files parse, but a 1.0.3a user profile still needs live load. |
| Debug features off by default | PASS (static) | Default JSON has zero for `DebugView`, `LegacyPhysicalDebugMode`, and `TerrainHeightDebugMode`; inspect live UI too. |
| Core ZIP | PASS | 341 archive entries; 340 manifest payloads verified, version 1.0.4. |
| Source ZIP | IN PROGRESS | Re-export final current working tree after documentation update. |
| FOMOD ZIP | PASS | 364 archive entries; XML schema, optional SurfaceTides bridge and notices validated. |
| SurfaceTides source companion | PASS | Matching 1.0.2 modified source archive built and hash verified. |
| Main-game deployment | PASS | 28 changed files copied with backup; all 340 live payloads hash-match Core. DLL 1.0.4.0 hash `D1C83A3F887122EE78D79DD8F83EDE0696CD08A2B1A491165C7D473424DC1ED9`. |
| Steam-game deployment | MANUAL TEST REQUIRED | Current request targets the main game root; do not imply both installations were updated. |
| In-game first/third person, cells, fast travel, weather | MANUAL TEST REQUIRED | Requires owner live session. |
