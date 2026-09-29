# Directional SSS

Directional SSS is PIXL's sun and moon screen-space shadow refinement. It uses the existing Bend-inspired DX11 compute ray-march path (`ContactShadows`) and is presented separately from Material Forge local-light contact shadows.

## Source selection

At every eligible prepass PIXL reads `activeShadowSceneNode->sunLight` from Skyrim's active shadow scene. The current `NiDirectionalLight` direction is projected into the ray-march dispatch for that frame. This makes sun, moon, weather, and sky hand-offs safe without storing or reusing a stale directional source.

The cached source pointer is diagnostic state only. It is reset on renderer resource reset and never used as rendering input.

## Scope

- **Directional SSS:** active Skyrim sun or moon; full exterior sky mode; depth-aware directional ray march.
- **PBR Local Contact Shadows:** point and clustered lights only; configured under Material Forge.

The module keeps its established `ContactShadows` ID, config key, and shader define so existing profiles and shader-cache compatibility are preserved. Only the user-facing name changes to Directional SSS.

## Reference assessment

`_REFERENCE/PIXL-SS-Shadows` is a Unity camera post-process implementation. Its source selection, depth ray march, and optional blur informed this review. PIXL's existing compute path already provides the appropriate DX11 integration: native game depth, active Skyrim light acquisition, resolution-scaled dispatching, and edge-aware handling. Porting a second full-screen ray marcher would duplicate work and double-apply directional darkness.
