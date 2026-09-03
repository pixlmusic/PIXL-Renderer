# PIXL Renderer Active Build Inventory

Baseline commit: `78df570e55a3772e355327cfd1653327654e1d47`

Canonical source: repository root.
Temporary build alias: an optional short `subst` mapping was used only for long-path tool compatibility.
Build: Visual Studio 17 2022, x64, C++23, Release/LTCG, `x64-windows-static-md-release`.
Renderer target: `PIXLRenderer.dll`; 38 integrated module descriptors.

## Active File Counts

| Category | Files | Final status |
| --- | ---: | --- |
| Native C/C++ and resource source | 240 | REVIEWED |
| HLSL shader source/includes | 180 | REVIEWED |
| Build, configuration, licence and release text | 100 | REVIEWED |
| Runtime binary/image/mesh/font assets | 63 | REVIEWED |
| Pinned third-party submodule boundaries | 3 | REVIEWED |
| **Total** | **586** | **REVIEWED** |

The exact path/subsystem accounting remains in `FILE_REVIEW_MATRIX.md`. The final
static pass opened 583 repository files, verified three exact submodule pins, and
reported zero missing/unreadable entries or malformed JSON/binary signatures.

## Source Roots

- `engine/` and `include/`: recursively included by `cmake/AddCXXFiles.cmake`.
- `distribution/Shaders/` and every `pipeline/*/Kernels/`: included in the generated project and assembled into the runtime shader namespace.
- `pipeline/*/Module.ini`: compiled into module-version metadata and staged into `Shaders/PIXL/Modules`.
- Explicit pipeline asset roots and distribution UI/configuration roots copied by `tools/StagePixlRendererStandalone.ps1`.
- Root/CMake/vcpkg/build scripts and release audit/staging/export tooling.

## Generated Files - Never Edit Directly

- `build/PIXL-12C/cmake/ModuleVersions.h`, `Plugin.h`, `ThemePresets.h`, and `version.rc`.
- CMake-generated precompiled-header wrappers under `build/PIXL-12C/CMakeFiles/PIXLRenderer.dir/`.
- Their canonical templates and generators are reviewed; generated output is checked for correctness and drift.

## Third-party Boundary

- CommonLibSSE-NG, FidelityFX SDK, and NVIDIA Streamline are pinned submodules. They are audited at the pin/license/build-interface/binary boundary; vendor internals are not restyled or represented as PIXL-owned code.
- The only known vendor-tree modification is the reproducible FidelityFX DX11 short-output-path patch, retained as a committed patch file and verified independently.

## Traced Non-active Utilities

- `tools/install-worktree-alias.ps1`, `new-worktree.ps1`, `update-cmake-version.ps1`, and `verify-shader-refactor.*` are development conveniences, not compiled, loaded, staged, or required by the public runtime. They remain subject to security/path scanning but are outside the active per-file matrix.
- Ignored `PIXL_AUTONOMOUS/`, PixDiT research, build outputs, local Data mirrors, caches, and browser state are not public renderer inputs.

The complete exact-path accounting is in `FILE_REVIEW_MATRIX.md` and the machine-readable `build/release-polish-work/active_file_inventory.csv`.
