# PIXL Auto-DOF

PIXL's Auto-DOF path is implemented inside Camera Suite's native
DX11 presentation composite. It consumes PIXL's existing scene colour and depth
resources and writes through the existing HDR output buffer; it does not load or
depend on CinematicDoFStandalone at runtime.

## Reference and credit

The local reference implementation is:

`_REFERENCE/CinematicDoFStandalone-0.8.31-source`

It was studied for actor/head targeting, dialogue focus behaviour, depth guards,
and bokeh-oriented controls. Credit remains with the CinematicDoFStandalone
authors and their upstream shader contributors for that inspiration and learning
reference. Credit for the reference work belongs to kota, Jiaye, and the
upstream CinematicDOF contributors including Frans Bouma (Otis / Infuse
Project). PIXL's implementation is a separate integration and shader path.

## PIXL approach

- Native Camera Suite scene/depth buffer integration rather than a second plugin
  presentation hook.
- Smoothed dialogue-speaker head tracking with visibility and loaded-3D guards.
- Stable screen-depth autofocus outside dialogue, with temporal easing to reduce
  focus pumping.
- Native Director integration: the existing photo-mode focus reticle is the
  authoritative target, with slower focus while the camera is moving and a
  quicker settle when the frame is held for capture.
- Interior-aware sky protection. PIXL uses the runtime cell context to reduce
  blur on cleared depth/portal pixels indoors, while allowing a stronger,
  physically distant sky response outdoors.
- A wider near-focus tolerance and smooth circle-of-confusion response to avoid
  constant foreground blur and the aquarium/claustrophobic presentation.
- Depth-continuity and cross-sign rejection to reduce terrain, foliage, actor,
  and snow silhouette halos.
- Separate half-resolution near/far gathers with an approximate six-sided
  aperture, anamorphic scaling, cat-eye falloff, and HDR highlight lift.
- Thin-lens CoC evaluation using focal length, aperture, sensor height, and
  Skyrim's game-unit scale, stabilized by the user focus range.
- Reprojected near/far history using the previous camera matrices and depth;
  camera motion, invalid depth, and disocclusion-like samples reduce history
  weight instead of smearing the frame.

The feature is opt-in through Camera Suite's `Enable PIXL Auto-DOF` control. If
CinematicDoFStandalone is installed and selected as owner,
PIXL disables its own path to prevent double application.

## Validation status

The C++ Release target builds successfully and the modified CoC and near/far
blur compute shaders pass FXC Shader Model 5 validation. Runtime visual
validation remains necessary in third-person gameplay, dialogue, interiors,
exteriors, first person, menus, photo mode, rapid camera movement, and
frame-generation paths. The aperture is shader-generated rather than a custom
artist texture, and focus targeting still uses actor/depth projection rather
than a gameplay ray-cast API.
