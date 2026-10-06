# PIXL 1.0.6-dev test checkpoint

Public plugin/compatibility version remains 1.0.6. Branch is `1.0.6-dev`, never main.
Includes accepted Hybrid Reflection Hi-Z work, reconstruction/motion-blur safety,
ReactiveFX spawn/upload robustness, broad exposure metering, underwater optics,
water-reflection refinements and reconciled accepted live shader improvements.

The FOMOD is a clean-cache development build. It intentionally does not bundle
an old cache, user profile or manually installed proprietary Neural Rendering
runtime. Let the full rebuild complete before copying the new PipelineLibrary
into a public package. Do not manually spoof Library.ini fingerprints or edit
version/ABI keys to make an older cache appear valid.

Deployment uses the source-generated Core manifest, backs up overwritten runtime
files and leaves UserGraphics.json, profiles/themes, overrides and unrelated mods
alone. At the owner's explicit request the old live PipelineLibrary is moved to
a timestamped repository backup, leaving the live location absent for regeneration.
This is recoverable; it is not a recursive wipe of Data/PIXL or the game directory.

Launch via the existing Steam SKSE loader and verify fresh PIXL startup and cache
compilation progress. Keep game running on the preparation screen for the rebuild;
do not enter gameplay early if the goal is a complete startup permutation cache.

Morning checks: fire/frost/shock impacts and shouts; camera-motion blur; DLAA/DLSS
silhouettes/foliage and Neural Rendering; small candle in dark interior; bright
window/snow exposure; underwater near/far fog; repeated water exits; torches/magic
near water; water SSR at screen edges/reduced render scale. Crash cause remains
unconfirmed without the spell-session dump. Visual and performance certification
remain LIVE VALIDATION REQUIRED.
