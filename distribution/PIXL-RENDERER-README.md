# PIXL Renderer v1.0

September 11 release candidate: includes Hybrid GI's zero-safe denoising basis, corrected temporal-history scaling, guided first-run setup, and clean package staging. The exact corresponding source revision and URL are recorded in `PIXL-RENDERER.manifest.json`; a matching source archive is supplied separately.

Validation limits: the GI correction passed shader compilation and reference-math tests, but still needs broad in-game visual regression testing. Optional DLSS-G SM86 remains experimental, with limited hardware and long-session coverage. Frame generation and real-time Neural Rendering remain off by default. A RELEASE package label does not imply an exhaustive security, compatibility or performance certification.

PIXL Renderer is a standalone PBR rendering engine for Skyrim Special Edition. Install one ZIP with a mod manager, enable the included `PIXL-TerrainField.esp`, and launch through SKSE. The one-time welcome card shows the shipped controls: `Page Down` opens PIXL Renderer and `Insert` opens PIXL Director.

On first launch, PIXL opens a guided setup card. The safe universal default is Enhanced quality with native TAA; FSR 3.1 Quality is available when its bundled runtime is present. The card can be reopened from **QUICK SETUP** in the PIXL Renderer control centre. Choose a Quality profile, make any preferred Camera adjustments, then press **SAVE LOOK**. Frame generation and Neural Rendering start off. Reconstruction, NR/FG and latency controls are available below the Camera viewfinder, while Photo Mode can temporarily use Neural Rendering on supported NVIDIA RTX 30-series-or-newer hardware even when real-time NR is disabled. `Alt+N` toggles NR after a DLSS sidecar session has been provisioned. Changing FSR, DLSS, frame generation, or a sidecar-backed option requires closing and relaunching Skyrim through your mod manager after saving. NR/FG use borderless presentation; exclusive fullscreen is intentionally excluded because Alt-Tab is not stable across the DX11/DX12 ownership boundary.

Do not combine PIXL Renderer with another engine-level shader-hook renderer, ENB, ReShade, Kreate, or a second copy of PIXL Renderer. A RELEASE archive includes a validated preloaded pipeline library; a RELEASE-CANDIDATE archive may compile pipelines on the first launch. Let any required first compilation complete and do not repeatedly delete a healthy cache, because ordinary shader changes invalidate only the affected permutations.

DLSS-G offers 2x, 3x and 4x output (subject to the runtime limit). It automatically requests Reflex during generation. Camera's advanced latency controls now use the active DLSS-G DX12 runtime: Boost and the Reflex limiter remain available even when the optional always-on Reflex switch is off. The limiter caps rendered frames, not generated output; frame generation does not increase simulation or input-update speed. FSR3 retains its own pacing path.

For RTX 30-series DLSS-G, install the upstream `sdli1995/dlssg_for_sm86` proxy (`version.dll` and `dlssg_sm86.ini`) beside SkyrimSE.exe, not inside Data. These proxy files are not bundled or overwritten by this package. The matched Streamline DX12 runtime is bundled. See `DLSSG_SM86_INTEGRATION.md` for setup and known validation limits.

The September 9 atmosphere look is now the shipped and Restore Defaults baseline. Existing saved graphics settings are preserved; choose Restore Defaults for Atmosphere if you want the new baseline in an existing installation. Named historical quality/look profiles remain independent.

Licensing, source availability, third-party notices and attribution are provided in `SOURCE-AND-CREDITS.md` and `COPYING`.
