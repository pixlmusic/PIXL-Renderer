# Quick Setup: DLAA and frame generation

## Scoped file review

`engine/Menu/LaunchExperienceRenderer.cpp` — active; reviewed/modified;
security, fidelity, performance and maintainability reviewed. Provides first-run
and repeat Quick Setup modal, persists Menu/ImageReconstruction through State.
Dependencies: ImGui, Menu, State, Globals, QualityProfiles, ImageReconstruction,
existing DXGI swapchain and hooked WM_CLOSE shutdown path.

Previously setup offered DLSS Quality but omitted native DLAA, and had no FG
choice. Added DLAA immediately after DLSS Quality, using the existing DLSS
method (3) with qualityMode=0, not a new method enum. Reopening setup restores
the DLAA selection. Both NVIDIA choices retain runtime capability gating.

FG checkbox initializes from current state and writes frameGenerationMode on
Save/Enter; enabling also writes frameGenerationForceEnable=1 in the same save
to avoid a second low-refresh restart. Existing backend selection is preserved.
Escape on the settings page continues to skip pending choices. Quality profiles
are applied only when changed; fog/POM defaults and live user config were not
edited by this task.

Runtime RestartRequired leads to a follow-up inside the same input-owning modal.
Continue for now completes setup; exiting requires both an explicit unsaved-game
acknowledgement checkbox and an exit button. Settings save uses the existing
State save path before posting WM_CLOSE to the swapchain window, after verifying
that it belongs to this process. No forced termination, shell execution,
auto-relaunch, downloads or shader ABI/cache changes. Exclusive fullscreen gets
an explicit warning that restart alone is insufficient.

## Validation and limitations

Release DLL compilation passed. Reviewed mappings for Off/TAA/FSR/DLSS/DLAA,
reopen selection, unsupported NVIDIA paths, pending FG activation/deactivation,
and confirmation/input lifetime. No in-game UI automation or exit test performed.
Build is not deployment: Skyrim was running, so its DLL was not replaced.

Class A UI wiring, Class B confirmed graceful shutdown. Live validation required
for short displays, checkbox/combos, continuing without restart, confirmed exit,
first FG boot, and unavailable runtimes. The existing void State::Save reports
I/O failures through logging; a transactional save-result UI remains future work.
No rendering math or shader changes; no GPU performance claim. UI-only work
executes only while setup is displayed. Existing backend runtime faults and
requirements still apply; selecting FG does not guarantee hardware support.

Rejected: automatic relaunch, because it could bypass SKSE or the owner's mod
manager; hot-creating a replacement presentation sidecar during gameplay.

Future: shared quality-choice table; transactional settings-save feedback;
automated modal interaction tests; small-display layout snapshots; centralized
backend-specific restart/availability explanation.

## Deployment follow-up

Deployed the validated Release DLL on 2026-09-13 at 20:14 local time with
Skyrim closed. Live `Data/SKSE/Plugins/PIXLRenderer.dll` SHA256 matches build:
`7CB4686E802AF291ADD912E5CEDFFA247AAB6A74F8186B2961AC69A943327634`.
Previous DLL moved to recoverable backup (avoiding in-place hardlink writes):
`build/deployment-backups/Startup-DLAA-FG-20260913-201430-6644968cdbe04f82b2d1de2899f9c08f/PIXLRenderer.dll`.
No settings, shader cache, shaders or release ZIP changed. Live UI validation
remains pending; reopen Quick Setup to test without deleting user settings.
