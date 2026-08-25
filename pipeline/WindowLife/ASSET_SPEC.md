# WindowLife authored interior assets

WindowLife keeps glass/pane detection tied to authored window masks. It first uses an optional loose `Data/Textures/masks/<diffuse-stem>_mask.dds` atlas when one is already installed, then uses the material's bound glow texture, and only uses PIXL's conservative procedural pane fallback when neither source exists. The optional assets described here supply only what appears behind that glass: a recessed room, a nearer curtain plane, and softly coloured occupants. They must not contain window frames or mullions.

## Delivery format

Author room variations as separate lossless `1024 x 1024` PNG or TGA files. Ten rooms are enough for the first set. PIXL will pack them into a `4 x 4`, `4096 x 4096` runtime atlas, leaving six cells for later variations.

- sRGB colour, 8 bits per channel;
- square, straight-on camera with no perspective-skewed window frame;
- no baked exterior reflection, rain, dirt, glass, frame, mullion, or facade;
- keep important furniture inside the central 90% of the tile;
- place the visible floor transition around 72-80% down from the top;
- use dark, low-contrast medieval/Nordic interiors with restrained warm illumination;
- avoid readable text, recognizable copyrighted characters, modern objects, and hard white highlights;
- retain some neutral wall/floor area so shader parallax does not look like a flat collage.

PIXL will build a full mip chain and encode the runtime atlas as BC7 sRGB. A 4096-square BC7 atlas with mips is about 21 MiB. A 2048-square atlas (512 pixels per room cell) is the performance package option, but 1024-pixel room sources preserve enough detail for close promo shots and future higher-quality presets.

## Curtains

Curtains are a separate nearer parallax layer. Supply up to sixteen `512 x 512` straight-alpha PNG/TGA variations; a `4 x 4`, `2048 x 2048` BC7 sRGB atlas is sufficient.

- RGB contains a muted fabric colour and fold shading;
- alpha contains coverage; do not premultiply RGB by alpha;
- background outside the cloth is fully transparent;
- curtain pairs should remain open through the centre and must not include a window frame;
- leave at least 16 transparent pixels around each source edge for atlas padding/mips.

The existing analytic curtain remains the no-asset fallback.

## Occupants

Occupants are another independent layer between the curtains and room background. Supply `512 x 512` straight-alpha sprites on a consistent floor baseline. A `4 x 4`, `2048 x 2048` atlas supports sixteen variants.

- full body with head and feet inside the tile;
- side or three-quarter walking/working poses, never symmetric signage poses;
- indistinct faces and softly blurred edges;
- dark desaturated umber, charcoal, burgundy, moss, and blue-grey clothing;
- restrained warm transmitted rim light, not a pure black ghost;
- alpha is coverage, not brightness; do not premultiply;
- no floor/contact shadow, window, scenery, text, or watermark.

WindowLife will seed room, curtain, and occupant indices independently per logical authored window. Their parallax depths must also remain independent so camera movement produces real layer separation instead of sliding one combined decal.

## Runtime sampling contract

An installed `*_mask.dds` pane atlas is bound only to the matching real window material at Lighting PS `t125`; helper/proxy geometry that directly uses a mask texture remains rejected. The mask is sampled in the real material's UV space and is never copied into the PIXL package. This keeps third-party asset provenance separate while allowing exact pane/frame/mullion clipping when the user has supplied compatible masks. `t126` remains the PIXL room atlas and `t127` remains the 176-byte per-draw structured payload.

The packed atlases will use a fixed `4 x 4` grid. Each cell must be padded by duplicating its outer pixels into a 16-pixel gutter before mip generation. Shader UVs will be clamped inside the selected cell so neighbouring rooms never bleed at distance.

Proposed layer order from glass inward:

1. old-glass reflection, grime, and refraction on the pane;
2. curtain atlas at roughly `0.18-0.25` of configured room depth;
3. occupant atlas at a seeded `0.55-1.05` depth;
4. room atlas at roughly `1.10-1.35` depth;
5. recessed-room edge/reveal fallback.

Authored room colour should gently modulate existing window emission rather than replace Skyrim's glow colour. Missing or failed assets must leave the current procedural WindowLife result intact.

## Integrated v0.5 room atlas

WindowLife v0.5 ships `Kernels/WindowLife/RoomAtlas.png`, a `2048 x 2048` sRGB atlas containing sixteen `512 x 512` room cells. It is loaded at runtime from `Data/Shaders/WindowLife/RoomAtlas.png`, bound to Lighting PS `t126`, and sampled as a recessed level-0 back plane. WindowLife's structured per-draw payload remains immediately adjacent at `t127`; the optional per-material pane mask occupies `t125`.

The generated rooms are curated into regional selection families rather than selected uniformly: general Nordic/Whiterun, Solitude/castle/noble, Riften/canal timber, Windhelm/dark stone, Markarth/Dwemer, and inn/shop/trade. Selection is deterministic per resolved logical window. The full-resolution external pane mask or native glow mask still clips the room to authored glass and mullions.

This 2048 atlas is the active first-party PIXL asset. A later 4096 BC7 replacement can preserve the exact 4x4 layout and shader contract when independently authored 1024x1024 source rooms are available. Any replacement must retain opaque alpha and be validated against the loader contract.
