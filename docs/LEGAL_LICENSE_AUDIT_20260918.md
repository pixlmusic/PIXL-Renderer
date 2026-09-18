# PIXL Renderer legal and licence audit

Audit date: 2026-09-18  
Audited revision: `67c034087d11562d67cfbbbf249ec6dc30dbeb4a` (`main`)  
Scope: tracked source, shader trees, submodule declarations, release/package
scripts, installer metadata, public notices, and locally available package
inputs.

This is an engineering provenance and licence checklist, not legal advice.
The follow-up safeguards below were added without deleting, relicensing, or
changing runtime source/shader behaviour.

## Executive result

The repository has a solid open-source notice foundation: GPLv3 text, the
Community Shaders linking/modding exception, an ancestry map, third-party
notices, pinned submodule declarations, and source/package provenance fields
are all present. The public-source exporter deliberately excludes known private
development material, generated output, shader caches, and the optional neural
runtime.

The audit did not find a missing root GPL notice or an obvious removal of
third-party copyright headers. Release publication is not yet legally
"cleared", however, because the following items require a deliberate owner or
qualified legal review.

## Findings requiring action or confirmation

### L1 — Proprietary/vendor runtime binaries are tracked in the source tree

The tracked `pipeline/ImageReconstruction/Kernels/ImageReconstruction` tree
contains NVIDIA Streamline/DLSS/DLSS-G binaries and AMD FidelityFX runtime
binaries. Adjacent licence files and third-party notices are present, but the
repository is publicly distributing the binary files themselves. NVIDIA's
included RTX/SDK terms contain restrictions beyond an ordinary open-source
licence, including limits on standalone SDK distribution and proprietary-notice
requirements.

Confirm, for each exact binary and version, that the relevant upstream licence
permits this repository and the intended Nexus package to redistribute it. If
the answer differs by channel, define a source-only/GitHub policy and a
release-only vendor-runtime policy. No binary was removed during this audit.

### L2 — NVIDIA SDK obligations are documented but not verified

`THIRD_PARTY_NOTICES.md` correctly calls out NVIDIA licence and notification
requirements, and the Streamline/DLSS licence files are retained. That notice is
not evidence that any required publisher notification, trademark approval, or
runtime distribution condition has been completed. Record the responsible
publisher decision and retain any required written permission or notification
evidence outside the public source tree.

### L3 — Package variants need a final notice-set check

`tools/StagePixlRendererStandalone.ps1` copies `COPYING`, `EXCEPTIONS.md`,
`ATTRIBUTION.md`, `THIRD_PARTY_NOTICES.md`, `SOURCE-AND-CREDITS.md`, and the
ReShade API notice into the staged package. `tools/BuildPixlFomod.ps1` copies
the base package and adds SurfaceTides notices when present, but its checks do
not independently assert that every FOMOD branch contains the complete PIXL
notice set. Verify the actual upload archive—not only the source scripts—has
the notices required by `SOURCE-AND-CREDITS.md`, plus every licence needed by
the selected vendor/runtime payload. The staging script now fails closed when
the core notice set is missing and when a bundled vendor DLL has no adjacent
licence/notice file. The FOMOD builder now requires the same core notice set and
refuses a SurfaceTides source tree with no detectable licence/third-party
notice set. These checks improve coverage but do not establish that a vendor
licence permits redistribution.

### L4 — PIXL-authored assets need explicit ownership records

The repository contains PIXL-specific shader systems, UI, branding, terrain
assets, snow microsurface data, and generated WindowLife artwork. Attribution
identifies PIXL-specific engineering work, but some asset provenance notes say
they are technical records rather than licence grants. Confirm that PIXL Studio
owns or has redistribution permission for every PIXL-authored texture, atlas,
font treatment, icon, and generated image shipped in a binary package. Keep
private generation prompts or source artifacts outside the public repository.

### L5 — SurfaceTides bridge provenance is dual-project work

The FOMOD contains a compatibility DLL/shader/configuration derived from the
SurfaceTides project. The package scripts preserve the supplied SurfaceTides
licence/third-party files when present, and the integration guide identifies
the bridge as a compatibility layer. Confirm the exact SurfaceTides source
licence, required notices, and permission to redistribute the modified binary
and shader in the Nexus package. PIXL attribution must not imply ownership of
SurfaceTides code.

### L6 — Historical upstream baseline should be archived or independently
verified

`ATTRIBUTION.md` identifies Community Shaders v1.8.3 and commit
`2f2919a71bed6132b125e41781304c8f6f73d002` as the comparison baseline, and the
repository keeps a read-only `upstream` remote. The baseline commit was not
present in this local object database during the audit, so the mapping was not
independently reconstructed here. Preserve the upstream URL, tag/commit, and
any required upstream notices with the release provenance record.

## Confirmed positive controls

- `COPYING` contains the GNU GPL version 3 text.
- `EXCEPTIONS.md` contains the retained modding/linking and corresponding-source
  additional permissions; upstream and PIXL authorship are not presented as
  identical.
- `ATTRIBUTION.md` maps renamed/extended Community Shaders systems and now
  distinguishes the retained Grass Collision field from PIXL Ground Response
  snow/mud deformation.
- `THIRD_PARTY_NOTICES.md` identifies CommonLib, Streamline, FidelityFX,
  ReShade, shader fragments, fonts, and the NVIDIA review requirement.
- `.gitmodules` pins CommonLibSSE-NG, FidelityFX-SDK, and Streamline-DX12 to
  explicit repositories/revisions or declared branches.
- `tools/ExportPixlPublicSource.ps1` requires the source commit, includes root
  licence/provenance files, and rejects known private/generated directories,
  neural model artifacts, caches, and development instruction files.
- `tools/StagePixlRendererStandalone.ps1` records the source commit and source
  URL in the package manifest, validates payload hashes, rejects reparse points
  and nested archives, and carries the principal notices into the package.
- ReShade API declarations have a dedicated BSD-3-Clause/MIT notice and are
  not confused with distributing the ReShade renderer.
- Bundled fonts retain family-level SIL Open Font License files.
- Individual adapted shader/source files retain visible upstream copyright and
  licence headers where the audit sampled them; no header-removal pattern was
  found in the reviewed active trees.

## Origin and authorship model

The current `origin` is the PIXL GitHub repository and the `upstream` push URL
is deliberately read-only. PIXL-specific modules are identified as PIXL work,
while Community Shaders-derived infrastructure, third-party snippets, SDKs,
fonts, and vendor binaries retain their own provenance. This is the correct
model for protecting PIXL work without erasing community credit or GPL rights.

The GPL applies to covered PIXL/Community-Shaders-derived source as stated by
the repository notices. It does not automatically relicense NVIDIA, AMD,
font, ReShade, SurfaceTides, or other separately licensed material. A package
must therefore provide the corresponding covered source and preserve each
component's own licence conditions.

## Recommended next review sequence

1. Obtain/record written confirmation for the exact NVIDIA/AMD binaries and
   intended distribution channel.
2. Build each FOMOD/release variant and inspect its extracted notice tree.
3. Confirm ownership/permission records for PIXL-authored and generated assets.
4. Confirm SurfaceTides bridge redistribution terms and source companion.
5. Preserve an upstream-baseline provenance bundle containing the exact source
   commit/tag and notices.

No removal or licence change is recommended solely from this report. The L1–L6
items should be resolved or consciously accepted before the next public binary
release.

## Follow-up safeguards implemented

- `tools/StagePixlRendererStandalone.ps1` now validates the complete PIXL
  notice set in the staged package and checks every bundled vendor DLL for an
  adjacent licence/notice file.
- `tools/BuildPixlFomod.ps1` now requires the complete PIXL notice set in the
  base package and fails closed if the SurfaceTides source has no detectable
  licence/third-party notice set.
- No runtime module, shader ABI, binary, or asset was removed or relicensed.
