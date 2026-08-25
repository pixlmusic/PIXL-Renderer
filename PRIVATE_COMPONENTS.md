# PIXL Private Component Boundary

This private development repository contains both the GPL-covered PIXL Renderer
work and independent internal research. Co-location in the private repository is
for backup and engineering convenience; it is not a blanket relicensing of
third-party or upstream work.

## PixDiT Photo Mode Enhance

`tools/PixDiTEnhance/` is currently an independent offline Python training,
export, validation, and bridge-design workspace. It is not compiled into,
dynamically linked with, loaded by, or packaged with `PIXLRenderer.dll`, and it
is excluded from the public source archive and beta plugin package.

The original PIXL-authored portions of that directory are presently intended as
private, all-rights-reserved research, subject to the separate notice in that
directory and every applicable third-party dependency/dataset licence. Model
weights and datasets are separate artifacts and are never committed here.

This boundary must be reviewed before any runtime integration. If proprietary
code is linked into the GPL-covered renderer, shares renderer data structures,
or otherwise becomes part of one combined program, labelling it private does not
remove the renderer's GPL obligations. Keep a future proprietary service/process
at arm's length, or license and publish the covered integration source as
required. Obtain qualified legal advice before commercial distribution.

## Public source export

`.gitattributes` marks internal ledgers, agent instructions, and
`tools/PixDiTEnhance/` as `export-ignore`. `tools/ExportPixlPublicSource.ps1`
creates and audits the public source archive from a committed revision. The
archive deliberately includes renderer C++, HLSL, module descriptors, build
scripts, licence/provenance documents, and the FidelityFX build patch; it omits
generated build output, shader caches, datasets, weights, and private research.
