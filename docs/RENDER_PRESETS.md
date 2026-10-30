# Renderer Presets

PIXL renderer presets save and share rendering settings as portable JSON files. They are separate from **Interface Themes**, which change menu colors, fonts and styling.

## Use a preset

1. Open PIXL Renderer with **Page Down** and choose **General → Renderer Presets**.
2. Select a bundled profile, a preset you saved, or one supplied by a mod.
3. Review its author, description and renderer version, then choose **Apply for next launch**.
4. Restart Skyrim through SKSE. PIXL keeps a timestamped copy of the previous `UserGraphics.json` before applying the profile.

The library overlays supported renderer values onto the current configuration. Unknown settings and values with incompatible JSON types are skipped. Your interface theme, language, hotkeys, compiler options and local capture paths remain yours. Presets do not execute scripts or load code.

## Save and share a look

Choose **Create a shareable preset**, enter a name and optional author/description, then select **Save current look**. PIXL writes a `.pixlpreset.json` file under:

```text
Data/SKSE/Plugins/PIXLRenderer/Presets/User/
```

The file contains the renderer settings and enable/disable state needed to reproduce the look. It does not include menu preferences, developer/compiler controls, version bookkeeping or the local screenshot destination. Duplicate names receive a numbered filename; an existing profile is never replaced.

Send the resulting file to another user, or package it as a small mod. In a Vortex/MO2 or manual Data mod, place it at:

```text
SKSE/Plugins/PIXLRenderer/Presets/<PresetName>.pixlpreset.json
```

The game does not download presets from Nexus or apply them automatically. Install through the mod manager as usual, open the Renderer Presets panel and choose **Refresh library**. Then explicitly select and apply the profile.

## File format

Current files use schema version 1:

```json
{
  "format": "PIXL.RenderPreset",
  "schemaVersion": 1,
  "rendererVersion": "1-0-9-0",
  "metadata": {
    "name": "Northern Dawn",
    "author": "Example Author",
    "description": "Cool daylight with restrained indirect lighting."
  },
  "settings": {
    "General": { "Enable Shaders": true },
    "Disable at Boot": { "HybridGI": false },
    "Example PIXL Module": { "Example setting": 1 }
  }
}
```

Replace the illustrative module data with settings captured by PIXL; module names and values must match the target renderer. The loader also recognizes the older bundled full-settings snapshots and filters out their non-renderer preferences. Files larger than 2 MiB, malformed JSON, unsafe/non-renderer fields and unsupported schema versions are ignored. Profiles from another renderer version can still be inspected, but unsupported settings are skipped during application.

## Compatibility and recovery

- A preset changes settings on disk; restarting is required so modules and boot-time enable states are initialized consistently.
- Applying a profile preserves the existing user configuration and writes a sibling backup such as `UserGraphics.json.bak.20261030-153000`.
- If a newer/older PIXL build no longer recognizes a setting, that value is skipped and reported. The remaining compatible values can still be applied.
- Revert by restoring the backup to `Data/SKSE/Plugins/PIXL/Config/UserGraphics.json` while Skyrim is closed.
- Profiles are local data only. PIXL has no Nexus API, remote downloader or automatic cloud sync for presets.
