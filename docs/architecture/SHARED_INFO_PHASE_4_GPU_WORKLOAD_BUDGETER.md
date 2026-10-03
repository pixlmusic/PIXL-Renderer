# Shared Information Upgrade — Phase 4: GPU Workload Budgeter

## Implementation

The renderer now aggregates existing named profiler passes into fixed workload domains. It uses an exponential moving average, asymmetric upper/lower thresholds, four discrete levels and a 120-frame hold before another transition. No individual spike changes quality.

Adaptive control is experimental and defaults off. The Developer UI exposes its state, target frame time, measured domain averages, level caps and transition reason. Existing Low/Medium/High/Cinematic contracts remain authoritative; scaling is multiplicative and can never exceed the user's configured workload.

HybridGI is the first consumer. Only runtime ray directions/steps are scaled; saved settings, artistic controls, ABI and permutations are unchanged. With adaptive control disabled, the scale is exactly 1.0 and behavior is identical.

## Cost and safety

- Reuses the existing non-blocking D3D11 timestamp profiler.
- No GPU resource, query, pass or readback was added.
- Fixed eight-domain state; no unbounded registry.
- No experimental behavior is active by default.

## Deferred

Other domains report timing but do not yet alter workload. They require module-specific cadence/resolution adapters and live visual testing before activation. Persistent user-facing Adaptive mode is intentionally deferred until that validation exists.

## Validation

Release build completed. CTest contracts and PIXL-Audit are required at the phase gate. Live profiling remains pending; no timing improvement is claimed.
