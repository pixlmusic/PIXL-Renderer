# Optional DLSSG selector

Scope: release-safe UI clarification; no renderer/shader behaviour changes.

Both public and advanced backend selectors share one implementation. DLSSG is
visible but disabled without a confirmed runtime or local SM86 installation
files. FSR 3 remains selectable even when generation is off, allowing recovery
from imported DLSSG settings. Existing settings are not silently rewritten.

Detection checks the executable directory for dlssg_sm86.ini and a supported
proxy filename once per process. No DLL is loaded, downloaded or authenticated
by this check. Matching files are only an installation hint: unrelated proxies,
invalid files or unsupported hardware can still fail the existing runtime
capability checks. A confirmed native runtime bypasses the file requirement.
Installation/removal requires a restart. Guidance warns against overwriting
existing proxies and points to the separately distributed upstream mod.

## File accounting

All three files are active, reviewed and modified in this scope. Security,
fidelity and performance were reviewed; future suggestions are recorded below.

| File | Purpose/dependencies | Findings and changes | Validation/future |
| --- | --- | --- | --- |
| engine/Modules/ImageReconstruction.cpp | Runtime settings UI; ImGui, Windows executable path, filesystem, existing DLSSG capability | Replaces unconditional selector with shared disabled choice and honest installation/runtime messages. Read-only non-throwing file-status calls; bounded executable path buffer; cached lookup, no per-frame I/O. No lighting or GPU changes. | Release build; pending live absent/present mod tests. Future: authoritative proxy identity handshake if upstream provides one. |
| engine/Modules/ImageReconstruction.h | Module interface used by both UI surfaces | Declares selector method; no settings layout or CPU/GPU ABI change. | Compile callers. Future: separate shared UI presentation if more backend controls are unified. |
| engine/Menu/PIXLRendererPage.cpp | Public reconstruction controls | Uses shared selector; backend remains editable with FG off. Preserves restart flag and user settings. | Release build; pending visual navigation test. Future: localized installation guidance. |

Rejected: bundling/downloading the proxy, assuming any version.dll proves
compatibility, clearing caches, changing runtime capability gates, or silently
resetting imported settings. Files plus INI still cannot prove proxy identity;
the UI explicitly distinguishes file detection from working runtime support.

No exhaustive repository-wide audit or live GPU validation is claimed.

Validation result: canonical PIXL-12C Release target built successfully after
these changes; git diff --check passed. No shader compilation/cache invalidation
was requested. Built DLL only: live deployment and RC ZIP are not updated by
this scoped UI follow-up.
