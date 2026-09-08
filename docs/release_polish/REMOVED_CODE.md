# Removed Code

## September 7 current-pass removal

Removed only the unused StructuredBuffer class and its two unused StructuredBufferDesc template overloads from `engine/Buffer.h`. No source file or runtime asset was deleted. The class copied the entire allocated ByteWidth regardless of supplied data_size; its UpdateList API also took an arbitrary object's address. The descriptor overloads could combine default/UAV usage with CPU write access. No current caller/instantiation/subclass or registration was found in native/public headers, build scripts, tools or distribution, so removal prevents future reuse of an unsafe duplicate API without changing active rendering. Generic Buffer/ConstantBuffer/Texture wrappers remain intact.

Validation: all affected native translation units rebuilt, linked and passed audit in `build/final-release-records/unused-buffer-cleanup-build-20260907.log` (exit 0). No warning suppression or module disabling. No GPU speed or memory saving is claimed because the removed class was not instantiated. Original definitions remain recoverable from baseline Git history / the working diff; do not reset unrelated changes to restore them.

The earlier cleanup account below is historical, not evidence that this continuation deleted those directories. Current generated cleanup was the safe CMake clean target; later diagnostic fixtures/checkpoints were retained under ignored directories.

## Historical baseline account

No active renderer code was removed in this checkpoint. Release polish favored compatibility-preserving defaults, capability gates and UI integration over risky cleanup.

Proven generated/ignored clutter was removed: loose byte-identical root build copies,
stale generated build/checkpoint directories, temporary browser state and empty
runtime asset directories. These were not source-controlled product files and can
only be recovered by rebuilding or recreating the relevant temporary workspace.
