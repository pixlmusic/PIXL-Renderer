# PIXL Renderer v1.0

PIXL Renderer is a standalone PBR rendering engine for Skyrim Special Edition. Install one ZIP with a mod manager, enable the included `PIXL-TerrainField.esp`, and launch through SKSE. The one-time welcome card shows the shipped controls: `Page Down` opens PIXL Renderer and `Insert` opens PIXL Director.

Choose a Quality profile, make any preferred Camera adjustments, then press **SAVE LOOK**. The current live-tested Balanced profile with FSR 3.1 Native AA is the safe default; frame generation and Neural Rendering start off. Reconstruction, NR/FG and latency controls are available below the Camera viewfinder, while Photo Mode can temporarily use Neural Rendering on supported NVIDIA RTX 30-series-or-newer hardware even when real-time NR is disabled. `Alt+N` toggles NR after a DLSS sidecar session has been provisioned. NR/FG use borderless presentation; exclusive fullscreen is intentionally excluded because Alt-Tab is not stable across the DX11/DX12 ownership boundary.

Do not combine PIXL Renderer with another engine-level shader-hook renderer, ENB, ReShade, Kreate, or a second copy of PIXL Renderer. The clean-cache archive compiles pipelines for the machine on first launch. Let that first compilation complete; do not repeatedly delete a healthy cache, because ordinary shader changes invalidate only the affected permutations.

Licensing, source availability, third-party notices and attribution are provided in `SOURCE-AND-CREDITS.md` and `COPYING`.
