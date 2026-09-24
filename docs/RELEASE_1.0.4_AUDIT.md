# PIXL Renderer 1.0.4 release audit

## Scope and evidence

The canonical source is this repository. The baseline `PIXL-12C` Release build passed before 1.0.4 edits. The source tree contained substantial intentional uncommitted renderer, GUI, Director, material, and WindowLife work; none was reset. `docs/release_polish/ACTIVE_BUILD_INVENTORY.md` enumerates 734 PIXL-owned build/runtime candidates, including one retired module. The accompanying matrix records a complete automated text/pattern scan. Deep semantic and in-game review is still tracked separately; a static scan does not certify visual correctness.

## Changes in this pass

- Set the active product, executable resource, package, and FOMOD version to 1.0.4. Custom theme metadata now uses the generated product version.
- Added a shared shader revision in pipeline-library metadata. A 1.0.3a cache with no matching revision is invalidated on first 1.0.4 launch; unchanged CPU/HLSL buffer ABI retains its own separate key. The stage tool reads all three cache keys from `ShaderCache.cpp` and refuses a stale preloaded cache.
- Added a SurfaceTides entry to the existing PIXL Extensions pillar. It checks the loaded DLL without loading it or inventing an ABI. The UI reports **Detected** until SurfaceTides confirms the V1 water-draw handshake in its own log.
- Retained the existing optional SurfaceTides 1.0.2 bridge and its exact-version FOMOD requirement. The local bridge DLL matches the main game's installed DLL by SHA-256.
- Built the matching modified SurfaceTides 1.0.2 source companion beside the FOMOD to keep the optional bridge's source and notices available.
- Removed one machine-specific path default from a development-only Discord maintenance script.
- Reused the existing release staging, manifest verification, deployment, source-export, and FOMOD tools.

## Architecture decisions

PIXL's native water remains authoritative unless the optional SurfaceTides bridge positively matches a live draw. `PIXL_QueryWaterDrawV1` remains the existing render-thread contract. Ground Response and WindowLife were not redesigned in this pass. The PIXL GUI remains the only user-facing framework.

## Risks and required manual tests

- A loaded SurfaceTides DLL does not prove its simulation, geometry hook, or V1 handshake is active. Inspect `SurfaceTides.log` while viewing a river or lake.
- Photo/Video Mode, shader permutations, material visuals, startup, fast travel, resize, and all supported Skyrim runtimes require live validation. Compilation alone cannot clear these gates.
- The public 1.0.4 candidate uses compile-on-device shader cache mode. First launch will discard an incompatible old library and rebuild. A fully preloaded release cache must be built from this exact source and descriptor set before labeling that package `RELEASE`.

## Validation record

The 1.0.4.0 Release DLL and integrated audit passed. Strict FXC coverage passed 733 selected cases. Core and FOMOD manifests/archives passed. The Core was deployed to `H:\The Elder Scrolls - Skyrim - Special Edition\Data` with a rollback backup under `build/deployment-backups`; all 340 deployed manifest payloads match by SHA-256. The existing installed SurfaceTides DLL matches the local 1.0.2 source build. See `RELEASE_CHECKLIST_1.0.4.md` for live tests still required. Test output is retained under ignored `build/` paths.
