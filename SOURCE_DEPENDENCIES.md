# Source Dependencies

PIXL Renderer uses public Git submodules rather than committing generated build
trees or duplicate dependency checkouts. Clone the source revision recursively:

```powershell
git clone --recursive <PIXL-source-repository>
git submodule update --init --recursive
```

The authoritative URLs live in `.gitmodules`; the release commit pins exact
revisions. At the current development checkpoint they are:

- CommonLibSSE-NG / CommonLibVR: `8f4205da56f01cbe98557422c008a5719c180fb9`
- FidelityFX SDK DX11: `e65b2530631f2afb9a9ac753884926e49e20d608`
- NVIDIA Streamline: `a9ed1f58436864891f68b0458300464dc53d9a69`

PIXL carries one source patch for FidelityFX's Windows DX11 shader-generator
output path:

`cmake/patches/FidelityFX-DX11-Short-Output.patch`

Top-level CMake applies it idempotently before adding the FidelityFX SDK. This
keeps the submodule pinned to its public upstream revision while retaining the
source needed to reproduce PIXL's clean Release build.

FidelityFX's bundled shader generator also has a fixed-size Windows path buffer.
Normal source locations need no override. For an unusually long checkout, expose
the repository through a short local build root and configure with, for example,
`-DPIXL_FFX_SHORT_BINARY_ROOT=<short-local-build-root>/PIXL-12C`. This cache
variable changes only the generator's working-directory spelling; outputs still
belong to the normal CMake build tree.

Generated directories such as `build/`, package staging, shader caches, Python
environments, downloaded datasets, and trained model weights are not source and
are intentionally excluded.
