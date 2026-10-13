# Dead-Code Audit — 1.0.8 Prerelease

## Result

The build inventory identifies 45 shipping modules and one descriptor explicitly marked `Pipeline=Retired`. The retired module remains source-only and excluded from runtime staging. No additional code was removed: broad text search cannot prove unused when PIXL uses module factories, shader selectors, string-based settings, and generated build globs.

The staged release candidate contains the shipping shader/module catalog and the integrated repository audit passed. No unreferenced shader directory was deleted as part of this pass.

## 5 Future Visual Improvements

1. Add machine-readable module-to-shader dependency metadata.
2. Report dormant material branches separately from dead code.
3. Build representative authored-content presets for difficult legacy branches.
4. Preserve shader debug paths that can diagnose visual regressions.
5. Keep retired module documentation discoverable for migration.

## 5 Future Performance Improvements

1. Use compiler/build manifests to identify unreachable permutations.
2. Compare cached shader keys against actually requested runtime keys.
3. Remove only permutations confirmed unreachable under all supported defines.
4. Measure archive and cache-size changes before deleting assets.
5. Track generated files independently from source ownership.

## 5 Future Feature / Research Ideas

1. CI report of source-to-module-to-shader reachability.
2. Automated stale asset detection with runtime string/reflection exemptions.
3. Build-time checks for registered modules without UI/config metadata.
4. Explicit retired-module catalog and compatibility notes.
5. Periodic removal review after one release cycle of telemetry-free evidence.
