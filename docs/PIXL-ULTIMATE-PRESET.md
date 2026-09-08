# PIXL Renderer Ultimate preset

`distribution/SKSE/Plugins/PIXLRenderer/Presets/PIXL-Renderer-Ultimate.json` is the highest validated real-time fidelity profile.

It combines the source-defined Ultra contracts across lighting, materials, atmosphere, water, terrain, characters and camera systems. It enables the production-ready visual modules together, uses native DLAA when the DLSS sidecar is available, and falls back to TAA when it is not.

Frame generation and real-time Neural Rendering are disabled for deterministic output and stability. Photo Finish may still use its separate 4x/24-sample neural capture path. The preset preserves the softened `SnowCoverageFeather=0.35` transition requested for Ground Response.

This profile is intentionally expensive: first-use shader compilation and cell transitions can be lengthy, and GPU/VRAM headroom is required. It is an opt-in preset, not the shipped default.
