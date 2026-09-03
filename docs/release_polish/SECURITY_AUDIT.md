# Security Audit

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
