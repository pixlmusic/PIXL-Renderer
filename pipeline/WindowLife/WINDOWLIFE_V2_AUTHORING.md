# Authoring interiors for WindowLife V2

This describes the current PIXL DX11 runtime. It does not imply support for a second mesh UV, depth texture, or metadata file that the renderer does not yet load.

## Current layers and coordinates

From the viewer inward: Skyrim's window glass and pane mask; PIXL's world-anchored grime/optical warp; optional curtain cutout; optional occupant cutout; authored room atlas. The material's primary UV locates the real glass and optional `Data/Textures/masks/<diffuse-stem>_mask.dds` stencil. A world-plane coordinate locates the room and holds its identity stable across the window. Glass refraction bends only the transmitted room/foreground ray, never the opaque frame. There is no reliable mesh UV2 in the current Lighting vertex stream; authoring a UV2 channel alone will not affect WindowLife.

The exterior room can now intersect a bounded box: back wall, left/right wall, floor and ceiling. Side hits reuse strips of the selected *same* room tile, so a room must have suitable wall and floor content near its edges. The Room Volume control blends this with the original planar projector; Window Recess places the box behind the pane. At distance the original mapping is used. Existing assets remain valid without changes.

When the player views an authored window from indoors, WindowLife instead uses the outdoor day/night atlas in a wider, deeper volume behind the glass. The window aperture and frame remain fixed, and the transmitted image responds to camera movement and glass refraction. This is still a single color atlas, so it cannot reproduce the independent depth of real outdoor objects. Check lateral motion, nearby windows, and grazing angles in game before judging an authored outdoor view.

Dry old-glass waviness uses stable world-plane slopes to bend only the transmitted artwork. Refraction and Glass Distortion set its strength; Interior Softness also softens the outdoor atlas. Indoors, the former curtain resource slot holds the outdoor night atlas, so curtain artwork is not drawn over the exterior view.

## Texture artist checklist

1. Create a square, straight-on view of a medieval room. Keep the outer left/right strips useful as side-wall material and the lower strip useful as floor material. Put the most recognizable furniture in the central 70%; avoid a high-contrast object bisected by the tile edge.
2. Do not paint glass, frames, mullions, outdoor reflections, weather, silhouettes, or curtains into the room tile. Those are separate layers. Provide diffuse room color with believable local light and shadow; avoid an all-white emissive panel.
3. Pack sixteen 512-square cells into a 2048-square 4x4 sRGB atlas. Keep each cell opaque and leave safe edge content for mip filtering. The release loader prefers `RoomAtlas_2k.dds`, a compressed 2048-square 2D texture with authored mips; `RoomAtlas.png` is a compatibility fallback.
4. For exact glass boundaries, provide a DDS mask whose filename matches the *diffuse* texture stem with `_mask` suffix in `Data/Textures/masks`. Black is frame/opaque facade and bright is glass. Match the diffuse UV layout exactly. The mask is an optional clip, not a room placement map. Avoid reusing an unrelated diffuse stem.
5. Curtains use their own 4x4 straight-alpha atlas and a central opening. Occupants use the shipped 4x4 atlas with a shared foot line. Replacing `OccupantAtlas.png` currently requires updating `OccupantAtlasRow` for the new sheet's row/foot positions. Glass grime should be neutral grayscale and mip-filtered.

## Capture of a real Skyrim interior

1. Choose a room and a plausible exterior window opening. Measure or estimate the opening width/height and the actual wall-to-rear-wall distance; record those values with the asset source, even though per-room metadata is not yet loaded.
2. Place the capture camera just inside the opening, looking normal to the rear wall. Use a consistent moderate field of view (roughly 50–70 degrees horizontal) and keep the optical axis level. Avoid including a physical window frame in the capture.
3. Capture a clean beauty pass with the room's intended practical lights. Keep furniture within the central region, preserve side walls/floor/ceiling, and avoid UI, bloom-heavy effects, glass, or exterior reflections. Obtain the right to redistribute any captured asset before packaging it.
4. Crop to a square view, align the visible floor across related tiles, color-grade in sRGB, and atlas-pack without blending adjacent rooms. Verify the 2048-square DDS mips independently; inspect the far mips for cross-tile color bleed.
5. If depth, normal, emissive, occupancy or material maps are also captured, keep them as source assets for a future opt-in runtime. The current shader samples only room color/alpha at t126; naming extra maps will not make it consume them.
6. Validate in Skyrim from straight-on, oblique left/right, above/below, near/far, day/night and rain. Toggle Room Volume between zero and its intended value to isolate the new box mapping. Check that glass refraction affects the room but not the frame.

## Optional future metadata proposal (not loaded by 1.0.4)

An eventual per-room manifest could declare schema version, tile index, room width/height/depth, floor line, exposure target, practical-light positions, room category, and optional depth/normal/emissive map names. It should be validated off the rendering hot path, optional for legacy assets, and never silently redefine existing atlas tile order. No current GUI control writes this format.

## Runtime compatibility

Missing room atlas: original WindowLife procedural transmission and glass remain. Missing exact mask: native glow or conservative procedural pane detection remains. Missing curtain/occupant art: analytic fallback remains. No UV2: the existing material UV and world-plane mapping continue to work. Authored Complex Material parallax keeps precedence on generic glass; named windows can retain their room if their pane evidence is valid.
