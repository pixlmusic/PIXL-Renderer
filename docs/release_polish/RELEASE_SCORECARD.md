# Release scorecard — 1.0.8 local release candidate

| Area | Result | Evidence / remaining limit |
|---|---|---|
| Source snapshot | PASS | Local checkpoint is on branch `1.0.8-prerelease`; the release candidate source is committed locally. No push was performed. |
| Source/build inventory | PASS | `ACTIVE_BUILD_INVENTORY.md`, `FILE_REVIEW_MATRIX.md`, and `PER_FILE_STATIC_REVIEW.md` account for 886 enumerated PIXL files across engine, public include, shader, installer, and tooling roots; 45 shipping modules and one retired source-only module. |
| Release build | PASS | `cmake --build --preset PIXL-12C` completed and the integrated `PIXL-Audit` passed. |
| Shipped cloud defaults | PASS | All 26 `PIXL Volumetric Clouds` values in `SettingsDefault.json` match the installed `UserGraphics.json`; the private user config itself is not included in the package. Rain/fog visibility and lightning timing refinements are included in the candidate DLL. |
| Core package | PASS | Local `RELEASE-CANDIDATE` manifest validates 4,119 payload entries and 3,741 preloaded shader stages against the current ABI, revision, and module identities. The DLL hash is `BB981A8BDF3F2E39054081C2CAF8A5956D9EC79D680ACE3C9EA8CC3B3BAB5D09`. |
| FOMOD | PASS | Three-page 1.0.8 installer passed `TestPixlFomod.ps1`; regular defaults, preloaded shader cache, and no SurfaceTides payload were verified. 7-Zip archive integrity test passed. |
| Diff hygiene | PASS | `git diff --check` reports no whitespace errors; Git emits only expected LF-to-CRLF working-copy notices. |
| Automated and shader validation | PASS — prior audit | Previous release-polish run recorded 28/28 CTest cases and staged-root FXC matrices passing. Those results predate the current packaging-only cloud-default sync. |
| Steam deployment | PASS | 1.0.8 release candidate deployed to the configured Steam Skyrim SE Data directory. Two changed files were verified against the package; user settings and 3,741 live shader cache stages were preserved. Backup: `build/deployment-backups/Release-20261013-152421-8b21e1507bb64948a1acd7ad717b58f5`. |
| In-game behavior | MANUAL | Cloud motion/weather transitions, DOF, lightning/aurora, authored-mod compatibility, and live cache behavior still require gameplay testing. |
| Public release readiness | NOT CLAIMED | This is a local prerelease candidate. Manual Skyrim validation and final release approval remain outstanding. |

## Local artifacts

- Core staging: `dist/PIXL-Renderer-1.0.8-RC-Core-20261013-151247`
- FOMOD staging: `dist/PIXL-Renderer-1.0.8-FOMOD-20261013-151247`
- Archive: `dist/PIXL-Renderer-1.0.8-FOMOD-20261013-151247.zip`
- Archive SHA-256: `2140F088F9745DC0C69775F84B288B2E13367E75D217F3C1FB4491D4A2A54FF6`

Before public release, run the manual Skyrim checklist in `UX_AND_COMPATIBILITY_AUDIT.md` and record runtime, load order, visuals, and cache behavior. The release candidate is installed locally for testing; this does not represent a public release.
