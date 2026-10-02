# Foliage Optimizer comparison

Date: 2026-10-02

## Scope

The reference reviewed was the local Vegetation Studio Pro runtime tree under
`H:\PIXLSTUDIOS\ISLAND BOY\Assets\AwesomeTechnologies\VegetationStudioPro\Runtime\Systems`.
It is Unity C# runtime code. No source or assets from that tree were copied into
PIXL Renderer. The inspected subtree did not provide a project-level licence
grant for redistribution, so the comparison is architectural only.

## Useful principles identified

- Maintain culling/render state per active camera, rather than assuming the
  gameplay camera is always the render camera.
- Compact vegetation into spatial cells and reuse persistent instance buffers.
- Perform frustum, distance and LOD classification in a bounded GPU pass before
  indirect rendering.
- Use predictive loading and expanded retention bands to reduce pop-in during
  camera movement.
- Keep shadow visibility policy separate from the main view, with stronger
  distance/importance rules for large vegetation.
- Grow reusable buffers conservatively instead of allocating per frame.

## PIXL coverage

PIXL's Foliage Optimizer already provides bucketed grass storage, dirty/pending
resource application, coarse slice rejection, GPU per-instance frustum/distance
culling, Hi-Z occlusion, indirect draws, mesh LOD and temporary photo/video
range-density-fade boosts. Complex grass is folded into the same bucket path.

The current Skyrim integration is not a Unity-style vegetation cell streamer;
grass ownership and engine streaming remain controlled by Skyrim. Replacing that
with a second cell/streaming system would be high risk and is not part of this
release-polish change.

## Implemented PIXL-native improvement

Foliage Optimizer now treats camera ownership as a culling-history boundary. It
detects gameplay-to-photo/video camera changes, camera replacement, mode changes
and large camera jumps. It invalidates the old Hi-Z pyramid and gives the new
view two settling frames of frustum/distance culling before occlusion results
are trusted again. This prevents stale camera occlusion from making vegetation
disappear when turning a capture camera or entering a cinematic mode.

The existing guard-band settings remain the retention mechanism for vegetation
near the view edge; no saved setting or constant-buffer layout was changed.

## Deliberately deferred

- A Skyrim-wide tree/shrub cell streamer: engine ownership and draw paths differ
  from grass and require separate validation.
- Predictive cell loading: useful for future distant-tree/plant streaming, but
  should be integrated with Skyrim's loaded-cell lifecycle rather than creating
  a competing loader.
- Separate shadow-camera instance lists: potentially valuable, but requires
  tracing every shadow pass and would be unsafe during this release pass.
- Direct reuse of reference scripts: not performed because their redistribution
  rights were not established.
