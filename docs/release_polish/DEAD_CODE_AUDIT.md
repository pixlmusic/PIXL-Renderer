# Dead Code Audit

No code was removed solely because it appeared unused. The shipping Strand Shading path remains authoritative and the retired Hair Reconstruction ABI stub remains source-only for cached-layout auditability. The release staging script continues to exclude descriptors explicitly marked `Pipeline = Retired`.

The duplicated engineering controls in the Tuning Workspace remain intentionally available for diagnostics; the public Camera page is now the supported user path. Removing those diagnostics during final polish would add regression risk without runtime benefit.

No new dead settings were introduced: every public reconstruction/NR/FG control maps to serialized Image Reconstruction state and a current runtime consumer.
