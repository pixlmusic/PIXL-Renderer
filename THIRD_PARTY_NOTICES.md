# PIXL Renderer third-party notices

PIXL Renderer includes or builds against third-party software and shader
material. This summary does not replace the complete licence text stored beside
each component. Copyright and licence headers in individual files remain
authoritative and must not be removed.

## Bundled build dependencies

| Component | Licence/notice | Source location |
|---|---|---|
| CommonLibSSE-NG / CommonLibVR | MIT; copyright notice retained in its licence | `extern/CommonLibSSE-NG/LICENSE` |
| NVIDIA Streamline | MIT for covered Streamline material, plus NVIDIA SDK/supplement terms and bundled third-party notices | `extern/Streamline-DX12/license.txt`, `extern/Streamline-DX12/3rd-party-licenses.md`, and SDK licence files under the component |
| AMD FidelityFX SDK | MIT for AMD code plus component-specific third-party terms | `extern/FidelityFX-SDK/LICENSE.txt`, `extern/FidelityFX-SDK/Third_party_notices.txt` |
| DirectX Shader Compiler / LLVM material | Apache-2.0 with LLVM exception and bundled notices | `extern/DirectXShaderCompiler/v1.9.2607/inc/hlsl/LICENCE.txt` and files in the compiler archive |
| sk_hdr_png | Unlicense/public-domain dedication; source and author information retained | `extern/sk_hdr_png/LICENSE` |

The `vcpkg.json` manifest also resolves libraries including BS thread pool,
CLibUtil, C++/WinRT, DirectX Headers, DirectXTK/DirectXTex, EASTL, efsw, ImGui,
magic_enum, Microsoft Detours, nlohmann/json, pystring, stb, Tracy,
unordered_dense, and Xbyak. Those packages remain governed by their individual
vcpkg port metadata and upstream licences; listing them here does not relicense
them under the renderer's GPL.

## Included shader and source material

- Sony Interactive Entertainment/Bend Studio screen-space shadow code is
  retained under Apache-2.0 notices in
  `engine/Modules/ContactShadows/bend_sss_cpu.h` and
  `pipeline/Contact Shadows/Kernels/ContactShadows/bend_sss_gpu.hlsli`.
- Intel XeGTAO-derived material is retained under MIT notices in the HybridGI
  kernels.
- AMD FidelityFX RCAS material retains its MIT notice in the
  ImageReconstruction kernels.
- NVIDIA Streamline, Reflex, and DLSS licence texts are retained in
  `pipeline/ImageReconstruction/Kernels/ImageReconstruction/Streamline`.
  The NVIDIA SDK texts include distribution and release-notification
  requirements that the publisher must review before a public binary release.
- GPURealTimeBC6H code by Krzysztof Narkowicz and irradiance code by Michal
  Siejak retain their notices in the WorldProbes kernels.
- Separable SSS code by Jorge Jimenez and Diego Gutierrez retains its
  redistribution conditions in
  `pipeline/Tissue Diffusion/Kernels/TissueDiffusion/SeparableSSS.hlsli`.
  Binary documentation must reproduce the acknowledgement required there.
- LegitProfiler-derived UI code, Unrimp-inspired background blur, shader math,
  colour mapping, spherical-harmonics code, rain/splash references, and other
  adapted snippets retain their in-file credits and licence notices.
- The base shader bundle contains MIT-licensed material credited to Ilya
  Perapechka; its licence is `distribution/Shaders/LICENSE`.

## Fonts

Bundled fonts retain SIL Open Font License texts alongside each family under
`distribution/Interface/PIXLRenderer/Fonts`. These include Jost, Sanguis,
Sovngarde, Rubik, Roboto, Inter, OpenDyslexic, and IBM Plex variants present in
the source tree. Reserved font-name restrictions in those licence files still
apply.

## Release packaging rule

A binary package must include `COPYING`, `EXCEPTIONS.md`, `ATTRIBUTION.md`, this
notice, and every component licence copied with a distributed third-party
binary or asset. The release publisher must separately confirm compliance with
the NVIDIA SDK/DLSS terms and any required pre-release notification; this is a
publisher/legal review item, not a claim that those terms have been satisfied
by source inspection alone.
# Optional ReShade API bridge

The API declarations in `extern/ReShade/include` are Copyright (C) Patrick Mours,
licensed BSD-3-Clause OR MIT. See `extern/ReShade/LICENSE.md` and provenance in
`extern/ReShade/README.md`. Release packages include `ReShade-API-LICENSE.md`.
The ReShade renderer and third-party presets are not distributed with PIXL.
