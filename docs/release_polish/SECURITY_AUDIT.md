# Security Audit

Current September 7 status: incomplete. The opening sections are historical baseline assertions, not a fresh security certificate. In particular, the preset generator's post-write baseline check is not a transactional overwrite guarantee; see BUILD_INPUT_REVIEW_20260907.md. The current scope/counts and unresolved neural-runtime risks supersede any broad earlier completion wording.

Current diagnostic/filesystem slice: fixed malformed module names resolving to the shader root, unchecked cached cleanup paths, redirected-target acceptance, Workshop restore paths and loss of failed recovery metadata. JSON override cleanup retains a separate exact-path boundary. Filename sanitation now handles reserved device basenames with extensions. Tests record cleanup calls against isolated fixtures, never run live deletion. See DIAGNOSTIC_FILESYSTEM_REVIEW_20260907.md for evidence and explicit TOCTOU/ownership limits. Font locale ranges now outlive their atlas use; see FONT_THEME_REVIEW_20260907.md. These findings further demonstrate why the historical broad safety assertions below are not current certification.

The final UI/default/photo changes introduce no networking, telemetry, updater, shell execution, registry persistence, credential handling or new dynamic library search path. NVIDIA feature availability is derived from the DXGI adapter description/vendor and the already-loaded Streamline feature contract. Unsupported hardware fails closed.

Photo Finish continues to write only through its configured screenshot path. Existing `ShellExecuteA` use opens that user-selected folder from an explicit button; it is not invoked by the new capture pipeline. Release configuration generation reads the explicitly supplied baseline and verifies its hash after generation so it cannot silently overwrite the live file.

Third-party FidelityFX and Streamline code remains at the pinned dependency boundary. The existing reproducible FidelityFX build patch is preserved; the dirty submodule state is not newly authored vendor code.

The final 586-entry accounting pass opened every active file (or verified the
exact clean Git pin for each of the three submodules) and scanned active text for
network clients, telemetry transport, process execution, registry persistence,
credentials and hard-coded machine paths. No secrets, tokens or credentials are
present. The two developer preset generators no longer default to this
workstation's Skyrim path; callers must supply `-BaselinePath` explicitly.

Intentional findings were retained and classified:

- `LoadLibrary`/`GetProcAddress` calls load known SKSE compatibility modules,
  pinned Streamline/NGX/FidelityFX interfaces and optional season APIs.
- `ShellExecute` calls occur only behind explicit user buttons that open a
  configured folder, log, feature-mod page or support path.
- The renderer contains no updater, downloader, remote command channel or
  automatic browser launch.

## 2026-09-07 continuation: security review not complete

The preceding text records baseline assertions. Pattern searches alone do not substantiate an exhaustive security sign-off, and the earlier continuation overstated their coverage.

- `engine/Modules/ImageReconstruction/NeuralRendering.cpp`: inspected in full. `SignedRuntimePathScope` rewrites the loaded vendor runtime's `GetModuleFileNameW` import temporarily, returning a different `nvngx.dll` path for the PIXL caller module. This is not an ordinary DLL-load operation. The comment identifies it as an NGX validation workaround. Vendor support/redistribution acceptability has not been established by this pass.
- HIGH reliability risk: the scope uses process-global mutable callback state, without synchronization; import restoration can fail, but the destructor still clears the saved callback. Vendor callback concurrency and restore-failure behavior are unvalidated. Do not describe this code as security-reviewed merely because DLL loading is intentional. No change to the workaround has been made here.
- The loader accepts private Feature 18 ABI only for version 310.8.x. Version gating is not signature verification or evidence of vendor approval.
- Streamline requests DLSS, Reflex and PCL. The extra live `sl.dlss_nr.dll` and `sl.dlss_g.dll` are not required by the loader paths inspected; they have not been copied into the RC.
- New offline manifest validation rejects traversal/ADS/rooted paths, duplicate entries, links, missing/extra files and mismatched payload hashes. Integrity does not prove binary trust or redistribution rights.
- Public release still requires the publisher review already recorded in `THIRD_PARTY_NOTICES.md`. No public release, commit or push is authorized or performed.
