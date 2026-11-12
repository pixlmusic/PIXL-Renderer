# PGLite source adapter for PIXL Renderer

The source-adapter installer choice provides a source patch and compatibility
notes. It does **not** install `PGLite.dll` by itself. Some PIXL FOMOD builds
also offer a separate PIXL-aware PGLite runtime choice; if present, that choice
installs a modified DLL and cubemap. It preserves an existing `PGLite.ini` and
provides a sample INI in the archive. If the original PGLite mod is installed,
let the PIXL-aware `PGLite.dll` win that file conflict in your mod manager.
The original PGLite DLL detects Community Shaders or ENB, not PIXL Renderer.
PIXL and Community Shaders should not be loaded together.

To build a PIXL-aware PGLite runtime, obtain the original PGPatcher source and
its declared submodules/dependencies, then apply `PGLite-PIXLRenderer.patch`
to that source. From the PIXL source repository root, the supplied reference
checkout can be patched with `git apply compatibility/PGLite-PIXLRenderer.patch`.
From an original PGPatcher checkout, use `git apply -p4` with the path to this
patch. Follow PGLite's upstream standalone CMake build instructions, then
install its built `PGLite.dll` as a separate SKSE plugin.

PIXL's MaterialForge reads the TruePBR mesh flags and texture slots PGLite
writes. The adapter changes PGLite's renderer detection and option gates. A
Steam startup has confirmed PIXL detection and PGLite initialization. Test a
representative TruePBR mesh and hair flow map before public release.
See `PGLITE.md` for the exact contract and validation checks.
