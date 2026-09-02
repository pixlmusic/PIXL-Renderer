# PIXL Renderer v1.0

PIXL Renderer is a standalone PBR rendering engine for Skyrim Special Edition. Install one ZIP with a mod manager, enable the included `PIXL-TerrainField.esp`, launch through SKSE, and use `End` to open the renderer.

Choose a Quality profile, make any preferred Camera adjustments, then press **SAVE LOOK**. The coordinated Enhanced profile with native TAA is the safe default; frame generation and Neural Rendering start off. Reconstruction, NR/FG and latency controls are available on the normal Camera page, while Photo Mode can temporarily use Neural Rendering on supported NVIDIA RTX 30-series-or-newer hardware even when real-time NR is disabled.

Do not combine PIXL Renderer with another engine-level shader-hook renderer, ENB, ReShade, Kreate, or a second copy of PIXL Renderer. The clean-cache archive compiles pipelines for the machine on first launch. Let that first compilation complete; do not repeatedly delete a healthy cache, because ordinary shader changes invalidate only the affected permutations.

Licensing, source availability, third-party notices and attribution are provided in `SOURCE-AND-CREDITS.md` and `COPYING`.
