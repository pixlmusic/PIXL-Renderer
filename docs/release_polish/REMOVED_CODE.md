# Removed Code

No active renderer code was removed in this checkpoint. Release polish favored compatibility-preserving defaults, capability gates and UI integration over risky cleanup.

Proven generated/ignored clutter was removed: loose byte-identical root build copies,
stale generated build/checkpoint directories, temporary browser state and empty
runtime asset directories. These were not source-controlled product files and can
only be recovered by rebuilding or recreating the relevant temporary workspace.
