# Owner-approved live settings promoted to defaults

Copied the owner's latest live UserGraphics settings into the authoritative distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json and installed Data/SKSE/Plugins/PIXL/Config/RendererDefaults.json. Preserved the entire existing ImageReconstruction section (upscaling, sharpening, frame generation and associated controls) and FirstTimeSetupCompleted=false for new users. Live UserGraphics itself is unchanged. No UserGraphics was added to distribution.

Semantic changes are confined to Ambient Probe, Atmosphere, Camera Suite, Distance Blend, Foliage Dynamics, Ground Response, Menu, Water Optics and Window Life. Other sections match prior defaults; numeric text formatting may differ from the old file. No DLL rebuild, shader/cache change, release ZIP refresh or source push was performed for this settings-only request.

## File review

distribution/SKSE/Plugins/PIXLRenderer/SettingsDefault.json: active shipping config; reviewed and modified. Packaging maps this to RendererDefaults.json. Fidelity: owner-tuned live values, not additional speculative tuning. Correctness: JSON readback equals the captured live object except the two explicit exceptions. Security: no executable content introduced; existing developer/debug and local paths inspected in the snapshot. Performance: quality choices copied as requested, not benchmarked. Maintainability: source remains authoritative, installed defaults hash matches it. Existing C++ fallback initializers are not retuned by this config-only promotion; absent/malformed config behavior is unchanged. Future: verify a fresh-install setup and repeat interior/exterior/weather checks against the staged next release.

Validation: semantic equality with expected JSON passed; upscaling defaults preserved; installed/source SHA equality passed; live user hash unchanged; git diff --check passed. Source and installed-default backups plus live snapshot are in build/OwnerDefaults-20260913. No exhaustive repository audit implied.
