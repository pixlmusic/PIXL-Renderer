# WindowLife curtain and occupant atlas provenance

Assets:

- `Kernels/WindowLife/CurtainAtlas.png`
- `Kernels/WindowLife/CurtainAtlas_high-fidelity-2k.dds`
- `Kernels/WindowLife/OccupantAtlas.png`
- `Kernels/WindowLife/GlassGrime_1k.png`

Both assets were created for PIXL Renderer with OpenAI's image-generation tool
from original PIXL-directed prompts. No game screenshot, third-party texture,
artist image, named franchise asset, recognizable character, logo, or readable
text was supplied to the generator.

The occupant prompt requested sixteen indistinct cold-climate medieval-fantasy
townspeople in side and three-quarter activity poses, using subdued period cloth
and restrained transmitted rim light on an empty background. The resulting 4x4
concept sheet was converted to straight alpha from its black isolation
background, resized to a 2048-square runtime atlas, and stripped of metadata.

The curtain prompt requested sixteen orthographic open curtain pairs on an
isolated uniform background, with muted wool/linen cloth, varied northern
medieval-fantasy cuts, no frame, wall, glass, scenery, people, text, or watermark.
The generated straight-alpha 4x4 sheet was resized to a 2048-square runtime
atlas, its low-confidence matte was tightened, and metadata was stripped.

Both atlases are sampled only behind a separately established window-glass mask.
They do not contain or replace Skyrim window, facade, or architectural textures.
The original generation artifacts are retained in PIXL Studio's private
development records; machine-local source paths are intentionally excluded from
the public repository.

This record documents asset origin and processing. It does not replace the
project's normal release licensing and publisher review.

The project owner supplied the updated 2048-square BC3 curtain DDS derivative
on 2026-08-29. It preserves the 4x4 straight-alpha curtain layout, contains
twelve mip levels, and was imported byte-for-byte from the runtime-tested shader
directory. SHA-256:
`50BCC66F209B7508417B13E79C8CBCE48F978F1CC1F80C0729A8C3D49BBBA69D`.

The glass-grime texture was generated for PIXL Renderer on 2026-08-29 using
OpenAI's built-in image-generation model. The original prompt requested a
square, seamless, neutral-grayscale old architectural-glass surface containing
subtle dust, mineral haze, faint vertical rain marks, micro-scratches and broad
low-frequency variation, with no frame, scenery, text, logo, watermark or
recognizable third-party design. It was mechanically resized to `1024 x 1024`,
converted to 8-bit grayscale RGB-compatible PNG and stripped of unrelated
metadata. Runtime mip levels are generated once during module initialization.
SHA-256:
`06CA7B30F4C8E8EA9E1DEA8CDFB3503F016B7805629D273B97C795B8722D02A9`.
