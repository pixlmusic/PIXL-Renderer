# PIXL extension panels

PIXL's advanced tuner has a deliberate **EXTENSIONS** button on the right. Opening
it does not change the selected renderer module or acquire another camera lease.
The initial panels expose the existing session style library and external
post-processing controls. There are no edge-hover flyouts.

## Integration contract

`engine/Menu/ExtensionPillar.h` exposes the internal registry. This is an
in-process C++ integration interface for code built with PIXL, **not a stable
cross-DLL plugin ABI**. It does not discover, load or download third-party DLLs.
An external SDK would need a separately versioned ABI and ownership contract.

All registration, unregistration and callbacks run on PIXL's UI thread. Do not
call the registry from worker threads. Register after UI initialization and
unregister before destroying the provider. Callbacks must own their captures or
use checked weak ownership; borrowing an object that can disappear is unsafe.

```cpp
#include "Menu/ExtensionPillar.h"

// Run once on the UI thread. The provider owns this handle.
PIXLUI::Extensions::Panel panel;
panel.identifier = "example.material-tools";
panel.displayName = "Material tools";
panel.category = "Materials";
panel.order = 20;
panel.icon = "M"; // Optional short text, not a borrowed GPU texture.
panel.availability = [weakProvider] {
    const auto provider = weakProvider.lock();
    return PIXLUI::Extensions::Availability{
        provider && provider->IsReady(), "Load a supported scene first." };
};
panel.draw = [weakProvider] {
    if (const auto provider = weakProvider.lock())
        provider->DrawSettings();
};
const auto handle = PIXLUI::Extensions::GetRegistry().Register(std::move(panel));
// handle == 0 means invalid/duplicate registration or registry shutdown.

// On the UI thread, before provider teardown:
PIXLUI::Extensions::GetRegistry().Unregister(handle);
```

Identifiers must be nonempty and contain only ASCII letters, digits, `.`, `_` or
`-`. Display names and draw callbacks are required. Duplicate identifiers are
rejected. Categories sort by name; ordering within a category is stable. Keep
identifiers stable across releases; do not use localized display names as IDs.

## Callback behavior

- Availability is optional. An unavailable panel displays its reason instead of
  drawing nonfunctional controls.
- Only the selected, open panel is evaluated. Closing PIXL, switching to its
  public pages or closing the extension drawer stops these callbacks.
- Callbacks must be short, nonblocking and allocation-conscious. Cache asset
  discovery and expensive state queries outside drawing. Do not perform file
  scanning, networking or resource creation each frame.
- Use the existing PIXL/ImGui controls. Keep Begin/End, Push/Pop and disabled
  scopes balanced. Do not end a window opened by the host, destroy the ImGui
  context, retain draw-list pointers between frames or change input/camera
  ownership.
- Own settings through the existing module/provider persistence system. The
  registry does not invent another configuration store.
- Snapshots keep registry entries alive during a draw. A callback may unregister
  itself or another entry, but that does not make borrowed provider data safe.
- The registry rejects registrations after shutdown. There is no restart of the
  native extension registry during the same process lifetime.

## Fault isolation and limits

The host catches C++ exceptions and uses the shipping ImGui recovery API to
recover ordinary unmatched child/style/ID/disabled scopes. It restores the host
style and recovery settings, stops the faulty entry and logs the fault once.
Other panels remain available.

This is **not a memory sandbox**. Access violations, corrupt native memory,
intentional stack underflow into host scopes and arbitrary native-code behavior
cannot be made safe by a UI wrapper. Providers still have to honor the contract.
Fix and unregister/re-register a provider to replace a faulted entry; do not
automatically retry a failing callback every frame.

## Validation

`tools/TestPixlGui.ps1` builds tests against the repository's actual installed
ImGui library. Tests cover registration rejection, ordering, unregistration,
snapshot lifetime, shutdown, scaled control layouts and a callback which throws
after leaving several ImGui scopes open. The resulting intentional ImGui error
messages are part of the recovery test, not failures of the main interface.

The library's error-recovery API is version-sensitive. Run these tests when
updating ImGui; do not silently substitute an unrelated recovery implementation.
