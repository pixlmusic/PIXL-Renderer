# UI and Accessibility Release Report

## Outcome

PIXL's three public product pages retain the existing visual theme and now use subtle page-entry motion, scroll safely when display scaling reduces available height, and keep ordinary renderer setup out of the engineering workspace.

The Camera page exposes reconstruction method and quality, DLSS model/sharpening, FSR sharpening, Neural Rendering, NR look/intensity, frame generation, V-Sync, frame limiting and supported latency controls. Engineering-only NR conditioning remains under **Advanced image controls**. Every added control writes an existing serialized setting and is consumed by Image Reconstruction; changes which alter device/swap-chain provisioning show an explicit restart message.

Photo Mode retains keyboard and controller navigation. Its neural rows report `RTX 30+ ONLY`, `OFF`, `PHOTO READY` or `UNAVAILABLE` instead of implying that a saved preference guarantees a working backend. Capture remains transactional: camera and input stay locked until sampling, resolve, encode and save finish, and a second capture cannot start concurrently.

## Compatibility and accessibility decisions

- NR controls are disabled on AMD, Intel and RTX 20-series adapters.
- TAA is the hardware-neutral default; no vendor feature is required to boot.
- Advanced controls are collapsed by default.
- Existing PIXL colours, spacing, controller focus and tooltips are preserved.
- Motion is restrained to short fades/slides; no continuous decorative animation consumes gameplay GPU time.

## Remaining validation

Physical AMD and Intel testing is still required to confirm the disabled-state copy and focus order. Controller testing should cover 100%, 125% and 150% UI scale plus ultrawide and 16:9 displays.
