# SurfaceTides foam and PIXL water

## Current finding

SurfaceTides' existing `HideAttachedFoam` feature identifies a small set of
original attached effect meshes and culls only their foam child shapes. The
PIXL/SurfaceTides water bridge replaces water geometry while retaining PIXL's
water pixel shader. These are different draw families, so the water shader
handshake cannot automatically move or depth-test an attached foam mesh.

PIXL already renders dynamic water foam in its water pixel shader. That foam is
evaluated after the tessellated water displacement and therefore follows the
same surface. The rapid-water path now also loads the vanilla
`Textures/effects/fxwhitewater01noalpha.dds` artwork and projects two animated,
flow-advected samples on that same surface. This preserves the recognizable
Skyrim rapid-water breakup without leaving a flat legacy overlay behind.

The reviewed SurfaceTides foam filter remains the safe fallback for duplicate
legacy foam: it can hide the legacy attached foam while PIXL owns the visible
surface response. It is opt-in and does not alter meshes or collision.

## Reference technique

The supplied KriptoFX decal implementation uses the correct general model for
a future foam path:

1. use a sufficiently subdivided projected XZ plane;
2. set its base height to the water plane;
3. apply the simulated water displacement in the vertex stage; and
4. reject fragments against scene depth.

That is a good model for new SurfaceTides foam decals, but it cannot be safely
applied to the existing Skyrim effect meshes without a new vertex/draw path.

## Required upstream contract for true legacy-foam conforming

A future SurfaceTides/PIXL integration should add a versioned API that exposes
the current water field and coordinate contract, for example:

- world-space water level and local displacement lookup;
- validity and world/cell epoch;
- optional screen/depth conform mode;
- explicit ownership flag so legacy foam is either conformed or culled, never
  rendered twice.

SurfaceTides would then render reviewed foam as a projected, subdivided decal
with the same displacement sample and a shallow scene-depth fade. The API must
be optional and fail closed when PIXL is absent, the field is invalid, or the
water epoch changes. This is separate from `PIXL_QueryWaterDrawV1`, which only
describes the currently bound water shader draw.

## Release decision

Do not replace arbitrary Skyrim effect shaders or move unknown foam meshes in
1.0.2. Keep the reviewed opt-in culling path, PIXL-native dynamic foam, and the
new authored rapid-water projection. Implement the versioned water-field/decal
contract as a subsequent SurfaceTides compatibility update after it has a
dedicated in-game validation scene.
