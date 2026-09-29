# Ground Response snow follow-up — 26 September 2026

## Symptom and evidence

Steam `ScreenShot12.png` and `ScreenShot13.png` show a nearly black, jagged trail where the player has walked over snow. The owner confirmed the snow itself deforms correctly and setting Snow Compaction Darkening to zero does not remove the dark trail. The optional `SnowMicro.png` is installed. Neither missing microtexture nor the visible darkening slider explains the report.

## 1.0.2 to 1.0.4 source comparison

Baseline: 1.0.2 release commit `113b2c78` (the older `v1.0.2` tag does not contain this later distribution tree). The following active Ground Response owners and immediate consumers were compared by file diff and relevant call paths:

| Files | Finding relevant to the trail |
| --- | --- |
| `engine/Modules/GroundResponse.h/.cpp` | Added environmental state, hard/soft classification, visual marks, and derived fields. The six-layer flags and per-pass b13 binding were traced to the terrain and lighting shaders. No isolated CPU sign change explains a black track. |
| `pipeline/Ground Response/Kernels/GroundResponse/Runtime.hlsli` and `GroundResponseSharedConstants.inl` | Added shared classification and environment constants. Both terrain and Lighting consume the same snow/mud activation helper. |
| `SurfaceDeformationUpdateCS.hlsl`, `SurfaceDerivedUpdateCS.hlsl`, and `TerrainSurface.hlsl` | Persistent deformation remains the geometry source. The new derived response/gradient path added track-normal reconstruction after the existing terrain TBN sign compensation. |
| `GroundMarkUpdateCS.hlsl` and the Lighting t111 consumer | Added visual footprint/blood/elemental marks. Ordinary tread multiplies colour by at most about 5.5% on snow; it cannot by itself explain a near-black trail. Blood and scorch use separate mark channels. |
| `distribution/Shaders/Lighting.hlsl` | Snow/mud darkening, deferred albedo override, front-face TBN reversal, and Ground Response shadow path were reviewed against 1.0.2. The front-face reversal already existed in 1.0.2 and is still required for the terrain replay. |
| `distribution/Shaders/Common/ShadowSampling.hlsli`, `LightingEval.hlsli`, `LightingLandscape.hlsli` | Shadow sampling and the relevant terrain material path were checked. No change to these files is required for the identified basis mismatch. |
| `DeformableGround.hlsli`, `GroundResponse.hlsli`, `CollisionUpdateCS.hlsl`, `PIXLAdvancedSnowMaterial.hlsli` | Checked as sampling, collision, and material dependencies. No additional change was made. |

## Code-level cause and correction

The terrain domain shader first negates the stored landscape TBN. The Ground Response front-face branch in `Lighting.hlsl` negates it again, restoring an upward lighting normal. The newer derived track-normal reconstruction wrote an upward normal into that already-negated basis. Lighting then reversed the reconstructed normal downward on walked pixels. This occurs when the track gradient is present and remains visible when the Snow Compaction Darkening slider is zero.

`TerrainSurface.hlsl` now stores the reconstructed normal in the same negated convention as the rest of the domain shader's terrain basis. The existing Lighting correction then restores the intended upward normal. Tangents, bitangents, snow/mud material controls, geometry depth, texture assets, and other renderer systems are unchanged by this correction. Ground Response module version `3-3-6` invalidates its own affected cache stages.

The preceding `3-3-5` patch capped geometric compression and slump at the configured absolute depth. That was a separate correctness change; it is not claimed as the cause or cure for this black shading. The owner reports that geometric deformation is working.

## Validation and deployment

- Corrected `TerrainSurface.hlsl`: clockwise HS, counterclockwise HS, and DS compile with `fxc /WX /Ges /O3`.
- `BuildRelease.bat PIXL-12C` and integrated `PIXL-Audit`: pass, 37 shipping modules.
- Core RELEASE-CANDIDATE package manifest: pass, 340 payloads. ZIP integrity: pass. Archive SHA-256: `7330554EA88AABCEAD71134158282F85FF536EEF58346297A825C1B241BAD6D1`.
- Steam deployment: DLL, `TerrainSurface.hlsl`, and `GroundResponse.ini` match the package by SHA-256. Rollback: `build/deployment-backups/RC-DayNight-20260926-014723-61f5011f84c94b628a1bc1771fac5da1`.
- Existing Steam user configuration and cache were not overwritten. The Core ZIP excludes the stale compiled cache, so standalone installation compiles shaders on device.

## Manual test required

Revisit the exact snow scene and walk the same route with Geometric Snow and Ground Response enabled. Test Snow Compaction Darkening at zero and at the user's normal value, then rotate the camera and observe whether the trail stays lit without losing its shape. Also inspect a mixed snow/rock transition and a mud track. This correction has not yet been visually verified in Skyrim.
