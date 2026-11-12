# Local-light and shadow coverage

## Current path

`RadiantGrid::UpdateLights` reads the engine's owning `activeLights` and
`activeShadowLights` lists, plus PIXL synthetic emitters. The CPU buffer accepts
up to 1024 lights. Cluster culling accepts up to 256 intersecting lights per
cluster. Compatible PIXL lighting shaders then read this list independently of
Skyrim's small per-shape forward-light list. This extends *shading* coverage for
lights PIXL has discovered; it does not create missing engine light objects or
force every pass and material through PIXL's clustered path.

Skyrim's native shadow mask still has four channels. RadiantGrid can select up
to three local native shadow maps with the sun present, or four without it.
Its optional local-shadow cache can retain more maps across frames, but capture
still depends on a live `activeShadowLights` entry and a rendered engine map.
The cache is disabled by default while the scheduling path is being validated.
Promotion of non-shadow lights is also disabled by default. Neither setting
should be enabled globally as a shortcut around native rendering budgets.

The screen-space point-light shadow is already a ten-step view-space depth ray.
Cluster culling selects four candidates, and the lighting shader selects among
them per pixel. The pass can add contact occlusion to selected lights; it cannot
see off-screen, hidden, or transparent blockers absent from the depth buffer.
Increasing the candidate count or ray steps across every pixel would raise
cost substantially and would still not solve those missing-geometry cases.

## Diagnostic interpretation

The latest available interior capture (`PIXL-HybridGI-20261111-100926`) predates
the subsequent cache-first shader change. It showed one shadowing candle at a
time, while previous captures showed a fireplace only after approaching it.
The actual Steam installation is
`H:/SteamLibrary/steamapps/common/Skyrim Special Edition`. It had a November 9
`PIXLRenderer.dll` when that capture was made, so it did not test the newly
built RadiantGrid collector. A similarly named development game folder outside
Steam was initially mistaken for the Steam installation; that deployment was
corrected after checking Steam's app manifest and the running game's process
path. The Steam DLL and shader were backed up under
`build/PIXL-12C/live-backups/steam-20261111-113951` before deployment.
That pattern does not prove one global light cap: discovery, engine selection,
map residency, cluster ranking, and screen-space visibility can each exclude a
different source. The source-only follow-up has no comparable in-game capture
yet, so it is not verified as a complete fix.

`UpdateLights` previously appended the same light twice if both owning lists
contained it. The collector now keeps one clustered entry per light identity.
This prevents duplicate entries from wasting cluster and contact-candidate
capacity in overlapping-list scenes. The cited capture had zero `activeLights`
entries, so this correction alone does not explain that particular candle.

The screen-space ray now skips non-finite positions/depth, exits at 4096 game
units of receiver view distance, and fades from 1536 to 4096. This bounds the
outdoor cost and avoids distant screen-depth silhouettes while preserving
nearby indoor rays. The directional contact pass and local six-tap contact ray
already have separate distance fading; their algorithms were left intact.

The November 11 Steam shader reconciliation found 322/323 matching canonical
files, with only `RadiantGrid/RadiantGrid.hlsli` differing. After compiling
three lighting permutations, cluster culling, and directional contact compute,
the revised shader and Release DLL were deployed to Steam. The deployed DLL
SHA-256 is `2DD7A31EE64BEBFE7C386545E7D1E9D5D01ECF4C465534A40B0ACD0D55829E1F`.
The other development game folder had 173 differing shaders and was synchronized
separately; those counts must not be attributed to Steam. This is
source/build/deployment validation, not in-game proof that every emitter casts
a correct shadow.

## Next engineering stages

1. **Release-safe:** capture a repeatable interior immediately on entry and
   after approaching a source. Record engine owning-list counts, distinct PIXL
   lights, four shadow candidates, active native map owners, cache residency,
   and per-light rejection reasons. Compare the same emitters by identity over
   time. Keep normal logging bounded.
2. **Controlled:** if a source exists in a safe owning list but misses the
   shortlist, improve selection with stable receiver influence and hysteresis.
   Preserve the current four-ray performance ceiling until GPU timing supports
   a different budget. If a source has no fresh map, keep its light contribution
   and use screen-space visibility only where geometry data supports it.
3. **Experimental:** a broader simulated ray-traced shadow system needs an
   independent scene representation: at minimum a temporally maintained
   light-centric depth/voxel/SDF proxy for off-screen occluders, with clear
   coverage and invalidation rules for moving objects and cell transitions.
   A depth-only ray marcher, even with more steps or a Hi-Z pyramid, cannot
   recover geometry that was never captured. Prototype this in a bounded DX11
   module and measure it before replacing native maps or changing shader ABI.

Do not describe clustered lighting, the native cache, and screen-space contact
shadows as one unlimited shadow system. They address different limits.
