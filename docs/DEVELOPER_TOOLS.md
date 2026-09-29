# PIXL Renderer developer tools

The root batch files are interactive launchers for `tools/PIXLDeveloperTools.ps1`.
They keep the repository's CMake presets and release scripts as the build and
packaging source of truth.

## Entry points

- `BuildRelease.bat`: choose a fast incremental DLL, audited Release, or clean audited Release.
- `BuildDeployAll.bat`: build, optionally create Core/FOMOD archives, then deploy a verified package.
- `DeployPIXL.bat`: deploy the latest manifest-backed Core to one configured Skyrim installation or all of them.
- `CommitPushPIXL.bat`: guarded staging, commit, and current-branch push workflow.

Failed interactive operations leave the command window open. All deployments
require Skyrim to be closed, verify the package manifest, back up changed PIXL
files, preserve user configuration and shader caches, hash deployed files, and
roll back the current operation after a copy failure.

## Local installation configuration

The tool discovers the repository's ancestor game installation, Steam library
folders, and `PIXL_SKYRIM_ROOT_1` through `PIXL_SKYRIM_ROOT_3`. A directory added
through the menu is saved to the ignored local file:

`build/PIXLDevTools.local.json`

No personal game path is written into tracked source.

## Packages

Development packages use fast ZIP compression and compile shaders on the test
machine. Production packages use maximum ZIP compression and require selecting
a live `Data/PIXL/PipelineLibrary`; its ABI and module versions are validated
before it can enter the archive. Public Release packages never include a user
configuration.

Core and FOMOD output remains under `dist/`. FOMOD assembly calls the existing
`BuildPixlFomod.ps1` validator and requires a valid SurfaceTides source tree.

## Targeted iteration

Deployment can select the full package, only `PIXLRenderer.dll`, all shader
payloads, or a manifest wildcard such as `Shaders/WindowLife/*`. Only files
listed in the verified package manifest can be copied.

For scripted use, call the PowerShell tool directly. Example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/PIXLDeveloperTools.ps1 `
  -Action BuildDeploy -BuildMode Release -Package Core `
  -PackageMode Development -GameIndex 2 -NonInteractive
```
