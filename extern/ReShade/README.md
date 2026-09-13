# ReShade API declarations

Unmodified upstream API headers from https://github.com/crosire/reshade,
commit `eeb2c76aea8e00200d88b479c9036c5ea4d06d5e` (API 20), retrieved 2026-09-14.
Licensed BSD-3-Clause OR MIT; see LICENSE.md and individual header notices.

PIXL uses only the event and runtime declarations. It resolves registration
exports from an already loaded ReShade module, which must accept API 20.
The ReShade implementation, injector, effects and presets are not bundled.
PIXL's own ImGui is not connected to ReShade's ImGui function table.
