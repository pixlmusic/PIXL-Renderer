# Phase 7 — Unified CTest Validation

## Outcome

PIXL now has a CTest-based validation layer that can run independently of Skyrim, SKSE, CommonLib and the full plugin dependency graph. Existing focused PowerShell tests remain intact and are registered rather than replaced.

## Test Architecture

`cmake/PIXLTests.cmake` defines five warning-clean native tests:

- RenderOrigin lifecycle, typed domains and ABI;
- Contained Liquids profile-volume and slosh math;
- HybridGI spherical-harmonic math;
- atmosphere/weather policy;
- Phase 2 camera, surface-classification and temporal policies.

It also registers contract tests for the pass scheduler, GPU resource services and hook registry, plus strict FXC tests for RenderOrigin and shared visibility. The full repository audit is registered when the renderer target exists and is labelled separately from portable checks.

Assertions are explicitly retained in Release test executables. This avoids the false-green behavior caused by `NDEBUG` without adding conflicting compiler switches.

## Developer Commands

Portable configuration and validation:

```powershell
cmake -S . -B build/portable-validation -DPIXL_PORTABLE_TESTS_ONLY=ON -DBUILD_TESTING=ON
cmake --build build/portable-validation --config Release --target PIXL-Portable-Tests
```

After a normal full build:

```powershell
ctest --test-dir build/PIXL-12C -C Release --output-on-failure
```

Labels separate `portable`, `cpp`, `contract`, `shader`, `fxc`, `full-build` and `audit` checks.

## CI

`.github/workflows/portable-validation.yml` runs the dependency-light suite on Windows for pushes and pull requests. It intentionally does not claim to build or live-test the SKSE plugin. Full developer builds and live Skyrim validation remain separate gates.

## Runtime and Build Impact

- Normal renderer source, runtime behavior and packaging are unchanged.
- Tests are `EXCLUDE_FROM_ALL`; they are built only when their target is requested.
- No game SDK, local game installation or proprietary asset is required by portable configuration.
- Existing PowerShell tests remain directly runnable.

## Validation

- Portable CTest configure succeeded from a fresh build directory.
- All 10 portable tests passed: five C++, three contracts and two strict FXC checks.
- The same 10 portable tests passed from the normal `PIXL-12C` build tree.
- Release `PIXLRenderer.dll`: built successfully.
- Full-build CTest repository audit passed; `PIXL-Audit` retained all 42 shipping modules.
- `git diff --check`: no whitespace errors (repository line-ending conversion notices remain).
- Hosted CI execution: pending the first remote workflow run.

## Deferred Work

- Add pure quality-profile and settings-migration fixtures as those policies are extracted from runtime owners.
- Add shader-reflection ABI tests in Phase 8.
- Keep game-dependent hooks, cell streaming and visual captures outside portable unit tests.
