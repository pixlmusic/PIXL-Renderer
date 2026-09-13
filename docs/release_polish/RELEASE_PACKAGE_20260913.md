# Release packaging follow-up

Scope: package current renderer and live pipeline library; update both GitHub
and packaged installation guides; commit/push accumulated release work.

## File accounting

| File | Purpose / findings / changes / validation |
| --- | --- |
| README.md | Active GitHub guide; updated installation, controls, DLAA/FG, optional proxy link, defaults and cache behaviour; removed stale beta/startup descriptions. Documentation-only. |
| distribution/PIXL-RENDERER-README.md | Active shipping guide; same installation instructions, Data-root placement, explicit optional proxy distinction and no game-save promise on exit. |
| tools/StagePixlRendererStandalone.ps1 | Active packaging; group notices under SKSE/Plugins/PIXL/Documentation, omit development reports, retain required runtime paths. Validate reproducible dependency patch separately from unknown worktree changes. |
| tools/AuditPixlRenderer.ps1 | Active validation; require relocated notices with source-hash checks; permit installation guide to name incompatible renderers without false retired-identity errors. |

All four reviewed for security, correctness, maintainability, fidelity and
performance implications; no GPU changes. Upstream optional proxy documentation
checked; no proxy download or bundling performed. Existing third-party patch
matches committed reverse-apply check and is preserved, not recommitted upstream.

Preflight: 3,475 live compiled stages; all module identities compatible; 3,807
payload hashes verified; repository package audit passed with 37 active modules
and one source-only retired module. No UserGraphics, logs, PDBs, backup files or
mod-manager markers in preflight. No all-permutation rebuild or cache deletion.

Prior rendering and shader validation is recorded in the scoped reports. This
packaging audit is not an exhaustive security certification or hardware matrix.
Future: automatic archive round-trip tests, per-stage source fingerprints,
transactional settings-save result UI, broader FG hardware validation and
fresh-install automated UI testing. Existing output archives are preserved;
new dated release artifacts avoid destructive replacement.
