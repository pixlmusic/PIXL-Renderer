# Release Scorecard

Current pass is INCOMPLETE. Rows describing defaults/UI/Photo Finish below are inherited baseline assessments, not new runtime test results. `CURRENT_PASS_STATUS.md` takes precedence over historical claims in the report set.

| Area | Status | Evidence / remaining risk |
| --- | --- | --- |
| Build | PASS | Visual Studio 2022 Release/LTCG target and exact 37-shipping-module audit pass; one Hair ABI record is explicitly retired/source-only. |
| Defaults | PASS | Current live-tested Balanced + FSR 3.1 Native AA; FG and real-time NR off; first-run card reset only in shipped defaults. |
| Neural hardware gate | PASS | NVIDIA RTX 30-series-or-newer policy; unsupported controls disabled and runtime fails closed. |
| Public UI | PASS | Camera page owns reconstruction/NR/FG/latency below the viewfinder; real profile and NR hover previews are packaged; Photo Mode supports keyboard/controller. |
| Photo Finish | PASS (code) | Locked transaction, fresh neural convergence, robust non-recursive resolve; final runtime A/B remains owner validation. |
| Shader cache | PASS | Current log uses disk-cache reuse; changed sources invalidate affected fingerprints; release package intentionally ships clean-cache. |
| Security | INCOMPLETE | NeuralRendering.cpp changes a vendor import temporarily to substitute a module identity path. Explicit compatibility/security review remains necessary; no blanket security sign-off. |
| Packaging | STAGED, NOT DEPLOYED | Dated RC has 315 payloads plus manifest; manifest integrity passed. Package audit and shader validation are separate gates. |
| File accounting | INCOMPLETE | 586 inherited ledger entries are not proof of a fresh exhaustive semantic review. New validation tools must also be accounted for. |
| Physical compatibility | PENDING | AMD/Intel disabled-state and RTX 3060 Ti timing require hardware runs. |

No visual or performance timing is claimed without live measurement.
