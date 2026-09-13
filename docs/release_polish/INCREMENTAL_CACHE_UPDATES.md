# Shipping incremental shader updates

PIXL now treats plugin version as provenance, not a shader compatibility key. A CPU/UI/config-only release reuses existing stages when Layout and ShaderABI match. Shared GPU ABI/layout changes still require full invalidation for safety. Do not change those identifiers for ordinary release version bumps.

For shader updates, bump the owning pipeline/Module.ini version. Runtime ValidateDiskCache uses each module's AffectsCachedShader(type, descriptor, stage) contract to remove only potentially affected stages. Unknown contracts conservatively invalidate wider families; correctness takes precedence over a cache hit. Retained stages in shared entry families are re-stamped so a newer Lighting.hlsl timestamp does not immediately defeat selective invalidation. New/unseen permutations compile on demand. Settings-only changes usually need no recompilation.

Module contracts must cover every changed shader path, including common includes and compile-time defines. Shared include changes need every dependent module version bumped, or a genuine shared ABI revision when applicable. The in-memory file-watcher dependency tracker is not a persistent per-permutation dependency manifest; do not rely on it alone for distributed include changes. Persistent content/dependency hashes are future work, not implemented here.

Release procedure:

1. Classify changed runtime files and validate ABI/resource bindings.
2. Record the old/new module versions and review AffectsCachedShader against actual HLSL guards.
3. Snapshot the newest compatible live cache with the game closed.
4. Export only unaffected entries; update snapshot metadata only after excluded stages have been accounted for. Never label stale binaries as current.
5. Package unaffected stages plus current sources. Do not clear users' libraries or ship UserGraphics.
6. Deploy sources, DLL and module metadata together. The new runtime removes affected stages once; subsequent starts reuse recompiled entries.

For the September 13 polish only, tools/ExportPixlPolishCache.ps1 recognizes WindowLife 0-7-3 -> 0-7-4 and MaterialLayers 1-3-2 -> 1-3-3. It exports the conservative union of their existing contracts: nonaffected vertex/compute/pixel families are kept; relevant Lighting pixel techniques are omitted. It refuses other starting versions and never writes to the source cache. It is deliberately not a generic update wizard.

Snapshot result: 3491 compiled stages, 360 omitted, 3131 reused. Those 360 are not recompiled offline; users compile the needed affected entries on first encounter. This is not a promise that every possible game permutation is preloaded. Mod-manager marker files are ignored by invalidation and excluded from the archive.

Future improvements: persistent per-stage include/define/compiler fingerprints, automatic dependency-map export from successful builds, and update-diff dry-run tooling with ABI regression fixtures.
