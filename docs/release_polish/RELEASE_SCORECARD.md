# Release Scorecard

Current pass is a RELEASE CANDIDATE, not a final public release. `CONSUMER_RELEASE_READINESS_20260911.md` records the new first-run/package work and the remaining cache/runtime gates. `CURRENT_PASS_STATUS.md` still takes precedence over historical claims in the report set.

| Area | Status | Evidence / remaining risk |
| --- | --- | --- |
| Build | PASS | Visual Studio 2022 Release/LTCG target and exact 37-shipping-module audit pass; one Hair ABI record is explicitly retired/source-only. |
| Defaults | PASS (code) | Fresh install now defaults to Enhanced + native TAA and opens onboarding; FSR 3.1 Quality is explicit opt-in. In-game first-run validation remains open. |
| Neural hardware gate | PASS | NVIDIA RTX 30-series-or-newer policy; unsupported controls disabled and runtime fails closed. |
| Public UI | PASS | Camera page owns reconstruction/NR/FG/latency below the viewfinder; real profile and NR hover previews are packaged; Photo Mode supports keyboard/controller. |
| Photo Finish | PASS (code) | Locked transaction, fresh neural convergence, robust non-recursive resolve; final runtime A/B remains owner validation. |
| Shader cache | CONTROLLED | Loader ABI/module validation remains active; release staging now requires a validated preloaded library and stamps cache mtimes after shader staging. No complete current library was available locally, so final cache-hit behavior remains open. |
| Security | INCOMPLETE | NeuralRendering.cpp changes a vendor import temporarily to substitute a module identity path. Explicit compatibility/security review remains necessary; no blanket security sign-off. |
| Packaging | STAGED, NOT DEPLOYED | Clean RC archive has 327 files plus manifest; manifest integrity and 14 negative/positive cases passed. `RELEASE` is guarded until a complete cache is supplied. |
| File accounting | INCOMPLETE | 586 inherited ledger entries are not proof of a fresh exhaustive semantic review. New validation tools must also be accounted for. |
| Physical compatibility | PENDING | AMD/Intel disabled-state and RTX 3060 Ti timing require hardware runs. |

No visual or performance timing is claimed without live measurement.
