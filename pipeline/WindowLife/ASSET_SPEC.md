# WindowLife authored interior assets

Current V2 geometry and capture guidance is in `WINDOWLIFE_V2_AUTHORING.md`.
This file retains the original art brief; the current runtime contract below
uses the bundled 2048-square atlases, not a 4096-square upload.

WindowLife keeps glass/pane detection tied to authored window masks. It first uses an optional loose `Data/Textures/masks/<diffuse-stem>_mask.dds` atlas when one is already installed, then uses the material's bound glow texture, and only uses PIXL's conservative procedural pane fallback when neither source exists. The optional assets described here supply only what appears behind that glass: a recessed room, a nearer curtain plane, and softly coloured occupants. They must not contain window frames or mullions.

## Delivery format

Author room variations as separate lossless source images. The current runtime packs sixteen `512 x 512` cells into a `4 x 4`, `2048 x 2048` atlas. Higher-resolution source art can be retained for future derivatives.

- sRGB colour, 8 bits per channel;
- square, straight-on camera with no perspective-skewed window frame;
- no baked exterior reflection, rain, dirt, glass, frame, mullion, or facade;
- keep important furniture inside the central 90% of the tile;
- place the visible floor transition around 72-80% down from the top;
- use dark, low-contrast medieval/Nordic interiors with restrained warm illumination;
- avoid readable text, recognizable copyrighted characters, modern objects, and hard white highlights;
- retain some neutral wall/floor area so shader parallax does not look like a flat collage.

The release runtime prefers the precompressed, pre-mipped `RoomAtlas_2k.dds`
(2048-square BC1 sRGB). `RoomAtlas.png` remains a compatibility fallback only.
Keeping the DDS authored mip chain avoids startup mip generation and uses about
2.67 MiB of GPU memory for the room layer.

## Curtains

Curtains are a separate nearer parallax layer. Supply up to sixteen `512 x 512` straight-alpha PNG/TGA variations; a `4 x 4`, `2048 x 2048` BC7 sRGB atlas is sufficient.

- RGB contains a muted fabric colour and fold shading;
- alpha contains coverage; do not premultiply RGB by alpha;
- background outside the cloth is fully transparent;
- curtain pairs should remain open through the centre and must not include a window frame;
- leave at least 16 transparent pixels around each source edge for atlas padding/mips.

The release runtime prefers `Kernels/WindowLife/CurtainAtlas_high-fidelity-2k.dds`,
a 2048-square 4x4 BC3 sRGB/alpha atlas with authored mips. `CurtainAtlas.png`
remains the compatibility fallback and the analytic curtain remains the
no-asset fallback.

## Occupants

Occupants are another independent layer between the curtains and room background. Supply `512 x 512` straight-alpha sprites on a consistent floor baseline. A `4 x 4`, `2048 x 2048` atlas supports sixteen variants.

- full body with head and feet inside the tile;
- side or three-quarter walking/working poses, never symmetric signage poses;
- indistinct faces and softly blurred edges;
- dark desaturated umber, charcoal, burgundy, moss, and blue-grey clothing;
- restrained warm transmitted rim light, not a pure black ghost;
- alpha is coverage, not brightness; do not premultiply;
- no floor/contact shadow, window, scenery, text, or watermark.

WindowLife v0.6 ships `Kernels/WindowLife/OccupantAtlas.png`, a 2048-square
4x4 straight-alpha atlas. WindowLife seeds room, curtain, and occupant indices
independently per logical authored window. Their parallax depths remain
independent so camera movement produces real layer separation instead of sliding
one combined decal.

## Runtime sampling contract

An installed `*_mask.dds` pane atlas is bound only to the matching real window material at Lighting PS `t125`; helper/proxy geometry that directly uses a mask texture remains rejected. The mask is sampled in the real material's UV space and is never copied into the PIXL package. This keeps third-party asset provenance separate while allowing exact pane/frame/mullion clipping when the user has supplied compatible masks. `t122` contains PIXL's mip-filtered glass-grime texture, `t123` and `t124` contain PIXL's occupant and curtain atlases, `t126` remains the PIXL room atlas, and `t127` is the 256-byte per-draw structured payload.

Room and curtain atlases use a fixed `4 x 4` grid. Their shader UVs are clamped inside the selected cell with a mip-dependent inset.

The bundled occupant PNG is a 1254-square concept sheet, normalized to 2048 at
load time, with four unevenly spaced sprite rows. `OccupantAtlasRow` in
`WindowLife.hlsli` holds its normalized row/foot anchors; replacing that artwork
requires updating those coordinates. Do not assume resizing repacks the rows.
Occupant alpha supplies a dark silhouette; RGB is not treated as emitted light.
The same room-floor anchor applies to manual and automatic room sizing.

Active v0.6 layer order from glass inward:

1. old-glass reflection, grime, and refraction on the pane;
2. curtain atlas at `0.04` of configured room depth;
3. occupant atlas at a seeded `0.45-0.85` of configured depth;
4. authored room atlas at the configured depth, with an optional bounded box at close range;
5. recessed-room edge/reveal fallback.

Authored room colour replaces most of the flat source emission only where a trusted glass mask and recessed room are active. Missing or failed assets leave the procedural WindowLife result intact.

## Integrated room atlas

WindowLife ships a `2048 x 2048` atlas containing sixteen `512 x 512` room
cells. Runtime loading prefers `Data/Shaders/WindowLife/RoomAtlas_2k.dds`, then
falls back to `RoomAtlas.png`. It is bound to Lighting PS `t126` and sampled as
a recessed back plane. WindowLife's structured per-draw payload remains at
`t127`; the optional per-material pane mask occupies `t125`.

The generated rooms are curated into regional selection families rather than selected uniformly: general Nordic/Whiterun, Solitude/castle/noble, Riften/canal timber, Windhelm/dark stone, Markarth/Dwemer, and inn/shop/trade. Selection is deterministic per resolved logical window. The full-resolution external pane mask or native glow mask still clips the room to authored glass and mullions.

The two preferred DDS atlases contain complete mip chains and are validated by
the loader for 2048-square, compressed, single-texture 2D layout. Invalid or
missing DDS files degrade safely to the PNG path without changing shader slots
or the per-draw ABI.
