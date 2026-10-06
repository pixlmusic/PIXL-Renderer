# Scoped reconstruction, camera and ReactiveFX safety review

Source: current `1.0.7-safe-performance` worktree, safe 1.0.6 baseline.
This is not a whole-repository release certification. Existing Hi-Z changes and
the user's FidelityFX dependency modifications are preserved.

## Reconstruction (controlled improvement; live validation required)

Traced motion generation, EncodeTextures, Streamline tagging/evaluation,
reconstruction publication, sharpening, neural guide copies and proxy Present.
Vectors already use normalized current-to-previous UV and unjittered matrices;
changing their sign/scale would be a regression. Encoding now chooses one real
surface vector instead of blending velocities across a depth edge, and rejects
non-finite vectors. Soft coverage remains a reactive hint, not geometric
disocclusion. Shared reconstruction publication names the encoded DLSS vectors.

Streamline returns evaluation success and aborts on failed frame tagging.
Failed evaluation does not certify history, resolve stale sharpening output or
publish neural guides; reset requests remain pending for the next valid dispatch.
Neural guides carry a frame stamp and require a successful DLSS frame at proxy
Present. That proxy explicitly bypasses the native Present/State::Reset hook.
No new GPU buffers, ABI/register reassignment or hook reorder was introduced.

Private neural Feature 18 parameters are not guessed. User neural strength,
enhancement settings and backend selection remain unchanged. DLSS/DLAA foliage,
silhouettes, fast motion, failure recovery and neural presentation require live
comparison; lower ghosting is expected, not measured or certified.

## Camera / metering (controlled improvement; live validation required)

Traced GUI settings, HDRData, presentation dispatch, shared depth and camera
matrices. Motion blur explicitly binds/restores b5/b12, validates dependencies,
and compares unjittered current/previous projections. Existing blur taps, limits,
photo-mode exclusions and all settings remain intact. This is camera-motion
blur, not independently moving-object motion blur.

The histogram was full-frame but centre-weighted up to 4:1. It is now uniformly
weighted across its sampled cells. Existing low/high percentile controls still
trim the log-luminance mean. Highlight protection uses the 95th rather than 98th
percentile to reserve protection for broader highlights rather than small
emissives. This approximates broad evaluative/matrix metering, not a literal
DSLR sensor implementation or nine separate exposure controllers. No new pass,
attachment, cbuffer field or user configuration edit. Adaptation times, EV
bounds, photographic compensation, manual exposure and photo hold unchanged.
Test dark interior with distant candle, bright windows, sky/snow and traversal;
upstream Skyrim adaptation and local exposure remain separate influences.

## ReactiveFX (release-safe robustness; crash root cause unconfirmed)

Traced projectile impact producer in GroundResponse, POD queue, recipes, upload,
GPU Spawn/Simulate/BuildMask/Composite and reactive-mask contribution. CPU retains
Skyrim object access; GPU particles remain in a persistent bounded structured
buffer with batched spawn commands. This baseline uses compute rasterization,
not per-particle game objects or newly introduced instanced mesh rendering.

Seed generation now runs under the event queue lock; dropped-event counting is
atomic. Disabled or invalid projectile events are rejected before classification.
Failed spawn Map drops the burst instead of throwing or dispatching stale data;
failed impulse upload skips simulation. Optional resource/tuning failures are
contained and log once. Invalid dynamic extents are rejected before integer casts.
Spawn upload compacts duplicate ring slots, preserving the last command, preventing
parallel threads racing on a particle slot after a large burst wraps the debris
pool. Counts, recipes, collision, colour, lifetimes and quality limits unchanged.
No new shader, instancing rewrite or added particle population.

CrashLogger points to redirected H:/UserData/Documents/.../SKSE; newest available
dump is 2026-10-06 13:19:13 (startup), not the reported spell-casting session.
Therefore no claim that these changes fix the observed access violation.
Need a matching dump and repeated fire/frost/shock/shout live test.

## Validation / future work

### Water follow-up and exact checkpoint reconciliation

The live shader comparison exposed accepted fixes absent from this source
baseline: world-cache neighbour/cascade continuity and restrained additive
irradiance, plus short-reach world-stable directional/local caustic bounces.
Reviewed and retained these in the checkpoint source, rather than deploying an
older shader over them. This is checkpoint reconciliation, not a new GI rewrite.

Local and directional caustic bounces remain coloured by actual light data and
separate from receiver direct NoL. Local footprints now require the same known
water plane. The coarse tile grid does not prove exact shoreline geometry or
water-point occlusion; this remains an approximation, not ray-traced caustics.
Strict validation exposed implicit texture gradients inside variable local-light
loops. Capture conservative receiver gradients before the loops and use SampleGrad.
Live confirmation needed for torches, held spells, magic particle lights, bridge
undersides and shoreline exclusions. No glow is fabricated for spells without a
real lighting contribution.

Submerged presentation previously only desaturated/tinted colour. It now uses
existing presentation depth for bounded Beer-Lambert RGB extinction/scattering;
zero/invalid authored fog (common for ENB-targeted records) uses a conservative
fallback while the opt-in strength still disables the effect at zero.
Camera warp remains unchanged. Stormglass shader is prewarmed during setup,
not first water exit, and shared b5/b12 are bound/restored for finishing passes.
This removes an identifiable first-use compilation hitch; it does not certify
that every recurring water-exit stutter is solved without a live timing capture.

Water reflection uses a normalized blended normal, active-view (not backing-UV)
SSR edge confidence, and lets valid SSR hits have authority instead of an
exponential ~58% ceiling at default strength. Roughness filtering, environment
cubes, colour limits, user controls and miss fallback retained. No ray tracing or
probe-box parallax is claimed: cubemap bounds are not available to infer it safely.

Release build and 24/24 CTests passed. Strict 72-case landscape/water VS/PS matrix,
four legacy/clustered forward/deferred WaterOptics lighting integrations and four
camera compute shaders passed. Full 684-case compute matrix passed with zero failures.
Earlier five ReactiveFX entries and four Encode variants also passed strict FXC.
Expected image changes require morning Skyrim testing; no GPU/CPU gain is measured.

Release DLL build; portable/structural tests; strict FXC HDR output, all four
Encode variants, histogram/exposure and five ReactiveFX compute entries.
Spawn tests execute production compaction for empty/duplicate/invalid/wrapped
capacity cases; source contracts cover upload failure and frame-success wiring.
Meter reference verifies 1% bright coverage rejection and broad highlight response.
Tests do not simulate D3D device removal or prove in-game image quality.

Future: injected Map/device failure harness; object-motion blur with compatible
display-space vectors; DLSS/reactive edge captures; neural guide visualization;
performance comparison for dense particle bursts. Rejected broad GPU instancing
rewrite, private NGX parameter guesses, cache resets, quality increases and
unsolicited changes to user's neural settings. No measured performance claims.
