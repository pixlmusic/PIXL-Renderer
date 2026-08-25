# WindowLife curtain and occupant atlas provenance

Assets:

- `Kernels/WindowLife/CurtainAtlas.png`
- `Kernels/WindowLife/OccupantAtlas.png`

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
