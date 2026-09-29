# Source and credits

PIXL Renderer v1.0 is a substantially modified work derived in part from the
GPL-3.0-or-later Community Shaders project and incorporates work by its
contributors and feature authors. The project records Community Shaders v1.8.3
(`2f2919a71bed6132b125e41781304c8f6f73d002`) as its historical comparison
baseline. PIXL Renderer is not endorsed by or affiliated with Bethesda Game
Studios or the Community Shaders team.

Corresponding source for a public binary must be distributed with, or made
available alongside, that release under GPL-3.0-or-later and the additional
permissions in `EXCEPTIONS.md`. Keep this notice, `COPYING`, `EXCEPTIONS.md`,
`NOTICE.md`, `ATTRIBUTION.md`, `TRADEMARKS.md`, and
`THIRD_PARTY_NOTICES.md` with redistributed packages. The public download
should identify the exact PIXL source tag or commit used to build its binary.

Skyrim and related marks belong to their respective owners. Source-code rights
are separate from project identity; see `TRADEMARKS.md`. Upstream and
third-party work retains its original provenance and applicable licence.

## PIXL-specific contributions

Git history supports PIXL-specific attribution for the following work. A module
may still be a mixed work when it uses or extends Community Shaders
infrastructure; file-local notices remain authoritative.

| Module or contribution | Provenance summary |
|---|---|
| WindowLife | PIXL-specific module and extensions |
| DistantLife | PIXL-specific module |
| Contained Liquids | PIXL-specific module |
| Curved Surface Mapping | PIXL-specific module |
| Render Origin | PIXL-specific renderer service and integrations |
| Ground Response snow/mud terrain deformation | PIXL contribution; grass collision remains upstream-derived |
| Director camera paths and Video integrations | PIXL additions to the mixed CameraSuite/UI system |
| CameraSuite physical camera and Cinematic DOF 2.0 | PIXL additions to the upstream-derived HDR Display system |

MaterialForge, CameraSuite, Directional SSS/Contact Shadows, Rain Response,
Water Optics, Waterbody/Flowmap, SkyBounce, Image Reconstruction, and the main
GUI/framework are upstream-derived or mixed systems with PIXL modifications;
they are not claimed as wholly original PIXL work.

This attribution does not restrict GPL-covered redistribution, forking, or
modification. Files containing Community Shaders or third-party material retain
their authors' notices and terms. Full provenance and dependency details are in
the repository-level notice files.

Project source and release provenance: https://github.com/pixlmusic/PIXL-Renderer
