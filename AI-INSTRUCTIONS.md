# PIXL Renderer assistant instructions

Use `AGENTS.md` as the active engineering contract.

Work against `engine/`, `pipeline/`, and `distribution/`. Keep changes DirectX 11 / Shader Model 5 compatible, preserve runtime ABI and settings migration, validate affected HLSL with FXC `/WX`, build the host DLL, and run the PIXL audit before packaging.

The product is a standalone PIXL renderer. Retired modular product identities may appear only in required legal/provenance documentation or in narrowly scoped legacy migration/conflict detection code that does not expose those identities to users.
