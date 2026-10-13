# Edge-Case / Points-Extraction Sprint — Research Summary (archival)

> Post-contest archive: pre-freeze research notes written 2026-08-23. SB01–SB10 were
> completed 2026-08-23; the contest ended 2026-08-28 (final rank #56 worldwide,
> prelim 16,147.866 / 22,000). Prompt/operator instructions from the working session
> have been removed; only the gathered research is retained below.
> Author: Ntokozo Dlamini (assistant sped up gathering/summaries/checks).

## Verified scoring function

Each formula below is on its own line. Symbols use `code` and are defined underneath.

```text
clamp(x; b, t) = max(0, min(1, (x - b) / (t - b)))
```

```text
E = (last final-token time over all requests) - (earliest ARR)
```

- `E` — makespan denominator; immutable `earliest ARR`, movable `last final-token time`.

```text
tp = sum(Lout) / E
```

```text
tp_comp = clamp(tp; tp_base, tp_UB)
```

- `tp` — throughput; `tp_base` and `tp_UB` — clamp ramp endpoints.
- `tp_comp` — clamped throughput component.

```text
tdr = mean over ALL requests of (P POST completion - ARR)
```

- `tdr` — pooled mean; one request carries weight `1/R`.

```text
tpot = sum(e_last - e_first) / sum(Lout - 1)
```

- `e_last` and `e_first` — per-request last and first token times.
- Only each request's first-to-last token SPAN matters; cadence unmeasured.
- `Lout = 1` implies no gaps; `tpot = 0` if no gaps.

```text
excess_tdr = max(0, (tdr - SLO1) / SLO1)
```

```text
excess_tpot = max(0, (tpot - SLO2) / SLO2)
```

```text
dist = sqrt(eps1^2 + eps2^2)
```

- `eps1` — `excess_tdr`; `eps2` — `excess_tpot`.
- `SLO1` and `SLO2` — waiting thresholds.

```text
wait = dist_base > 0 ? max(0, 1 - dist / dist_base) : (dist == 0 ? 1 : 0)
```

- `dist_base` — ramp width; `dist_base = 0` is BINARY.
- `wait` — waiting component; `1` when within SLOs, `0` when breached on the cliff.

```text
Score = 1000 * (w_tp * tp_comp + w_c * wait)
```

- `w_tp + w_c = 1`; either may be `0`.
- Zero ONLY on protocol error / malformed output / stuck state / TLE > 15 s / MLE > 256 MB.
- SLO violations never zero a test.
- Means are pooled (one request = `1/R` weight).
- Final ranking = mean of 20 unseen frozen tests (harder than prelims historically; ONE frozen zero = −50 final-mean points).

## Where the missing prelim points were (archival ledger from real CF judgements)

- `#5 + #6` ≈ 1,198 missing (starve class: `tp_UB` sits 2.1–3.3x ABOVE physical ceiling — `tp` NEVER saturates, every token/ms is linear points; resource use is 96–100% of ceilings → mostly structural, but ORDER effects within decode rounds are proven winnable — see structural facts below).
- `#14` ≈ 585 (mostly tp-structural + ~71 waiting slice needing `tdr` −15%).
- `#3` ≈ 499 (pure-wait razor, `dist_base` ≈ 0-ish: needs `tdr` −37%; EXTREMELY dangerous — a 500→0 burn happened once; touch only with computed slack guarantees).
- `#1 / #2 / #11` ≈ 1,500 combined (below serial reference rates; likely link/compute-floored, largely dead).
- `#10 / #9 / #15 / #17 / #18 / #21` ≈ 630 combined TDR-cluster (SLOs below pipeline floors — order-sensitive tails).
- `#13 / #19 / #22` ≈ 250–350 combined linear-tp tails where wave/order mechanisms could pay.

Target arithmetic: every `+1` on 22 prelim tests = `+1` scoreboard.

16,148→17,000 needs ≈ `+852` TOTAL — that is: capture roughly HALF of {starve-order slices (`#5/#6/#22`), `#14` waiting slice, `#13/#19` linear tails} WITHOUT losing anything anywhere, or land one big structural unlock.

## What the solver did (archival)

Layered sources (`src/submission.cpp` is generated from them):

- `src/Solution.cpp` (main loop, alpha regime logic, cliff flip).
- `src/scheduler.cpp` (local/cloud task picking, `D_PRE` combine + dynamic cross-cloud cap ladder, accumulate holds, V11 admission cap, SPT prefill, TLE fast path at 1.2M frames).
- `src/policies.cpp` (`MarginalBatch::precompute` breakpoint argmin batch caps, `group_size` crossing tradeoff, `SloAwareDelay::hold_dpre`, chunking, cloud chooser: Tier-1 large-`Lin` isolation + EFT + PoTC band + x9 herd gate).
- `src/state.hpp/.cpp` (State machine, Oracle marginal-value functions, ready lists, `mega_regime` detector).
- `src/task_table.*` (piecewise-linear interpolation).
- `src/protocol.*` (I/O).

MATURE mechanisms already load-bearing (do not break):

- breakpoint-argmin caps.
- cheap-link UP-gate relaxation.
- traffic-aware cliff flip gated on `!cliff_feasible`.
- exhaust→unbounded caps.
- stderr minimalism.
- `pick_local` order `P_PRE`→`P_POST`→`D_PRE`→`D_POST` (reordering destroyed batching once).
- V-gate herd (x9: raw `d_last < d1` signature is LOAD-BEARING).

## Asset inventory (archival; verified components)

- `research/techniques/`:
  - SB01 (gradient calculus: `V_E` / `V_P` / `V_S` exact marginals, WSPT ordering, EOQ batch law, endgame switch).
  - SB02 (rollout/MPC design, budget ladder).
  - SB03 (VERIFIED exact forward simulator, 286 ns/event, Example-1 byte-identical; lean-integration spec).
  - SB04 (FD-verified exact score calculus to 2.9e-10; THREE PROVEN BUGS in live Oracle: zero-marginal at `dist == 0` knife edge, K=1 `V_tpot` ~1000x off scale, SLO2 cap uses wrong period — plus 14 magic constants dissolved into exact formulas with drop-in C++).
  - SB05 (survival-conditioned `Lout` posterior — 5x better MSE than deployed estimator; lull-vs-ended posterior; endgame trigger inequality).
  - SB06 (exact split DP: optimal cross-cloud batch partition, brute-force-verified 1200/1200; `D_POST` hold rule; lifetime-cost cloud picker).
  - SB07 (lot-streaming chunk sizes, phase-offset cohorts, EVENT-BACKED WAKE PROOF table for every hold).
  - SB08 (endgame drain policy, merge rule, `CliffMode` controller with ZERO violations on 300k adversarial trials + SAFE-mode exchange proof).
  - SB09 (alloc-free hot path proof 14→0 allocs/frame, parser/writer benches, size-block inventory of the artifact, minify patterns worth ~3.2K chars, rename list worth ~1.4K).
- `research/scoring/SYNTHESIS.md` (physics laws L1–L12 incl.: finish-line clock, span-only TPOT, dead zone [`P_POST`→first token] invisible to both waiting metrics, pooled means, iso-circle trade rate).
- `docs/HIDDEN_ATLAS.md` (recovered per-test parameter atlas).
- `docs/DECISIONS.md` D-entry history (wrong-sign lessons).

## Source note

The sprint plan (Phase A audit → Phase B mechanisms → Phase C self-verify) and its safety battery / definition-of-done checklist were working-session operator instructions and have been removed from this submission.

The safety battery (`make`, `zoo_gate`, crash sweep, hostile harness) is documented in `README.md` and `docs/README.md`.
