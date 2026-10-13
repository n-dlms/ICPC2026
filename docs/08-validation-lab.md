# 08 — Validation Lab: adversarial testing and measurement infrastructure

> **Historical note (post-contest).** I built this lab before Aug 14 and used it throughout the contest to reach **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) originated the design and decisions; AI sped up implementation and checks under my direction.

Purpose as I used it: eliminate the two dominant risks in my preparation — (a) overfitting the solver to the
20-case regression suite, and (b) solver/judge state divergence (the CONTAIN bug class). This
document was the build plan for the pre-Aug-14 tooling I completed. I kept Tier 3 items (multi-hop displacement,
min-cost flow, preemption, more papers) parked until the real spec justified them.

## Principles I enforced

1. **Regression suite ≠ selection suite.** The 20 cases in `cases/` were the freeze gate only.
   I made all development decisions on a generated DEV pool; I used a held-out SECRET pool
   once per freeze as a final check. I never tuned against SECRET.
2. **Robust dominance, not average dominance.** A candidate shipped only if: valid on all cases,
   sum ≥ baseline, AND no class-level regression beyond tolerance (c1–c4: hard 0; generated
   families: small tolerance, default −0.5). I rejected a candidate that won burst by +20 but lost
   tight-SLO by −20 even when the average looked good.
3. **Budget discipline was a correctness rule, locally too.** Per-case runtime guard (target
   < 1 s local), DEV-pool runs stayed well under 30 s so the tooling itself could never
   approach the 120 s wall. I tracked worst-case, not just mean.
4. **Every accepted change answered "why".** Score decomposition and per-tick replay were
   part of my A/B workflow, not optional diagnostics.
5. **The simulator/judge boundary was a first-class citizen.** State invariants and strict
   judge behavior ran on every build in the pipeline. The CONTAIN +3.0 contamination was the
   canonical warning: an invalid state manipulation could look like a free point.

## Components

### P1. Case generator — `tools/case_gen.py`

Parameter space (all seeded, deterministic):

- arrival pattern: poisson / burst / sequential / mixed
- load: arrival rate λ, ρ (deadline-pressure multiplier vs EDD-feasible optimum)
- shape: pe/pc ranges, Lout distribution (variance), SLO multiplier
- topology stress: edge/cloud imbalance, COMM cost, horizon T

Outputs `cases/dev/` (500) and `cases/secret/` (1000+). Seeds and manifest logged.

### P2. Adversarial generator — `tools/adversarial_gen.py`

Hill-climbing mutation loop over the P1 parameter space:

- objective A (regression hunting): maximize `score_baseline − score_candidate`
- objective B (weakness hunting): minimize candidate score within realistic parameter bounds
- keep top-N into `cases/adversarial/`; every found regression class becomes a regression test
- LIMITATION (known, written): objective B plateaus at the score floor (all candidates score
  ≈ 5 on saturated cases) — the weak mode cannot separate candidates there; do not use weak
  mode as a proxy for candidate quality.

### P2b. Failure-location pipeline — `tools/fail_mine.py` (IMPLEMENTED, pre-spec)

The "generate 10,000 → run both → biggest regressions → cluster" loop:

- pool: `--generate N` (all 7 families, seeded) or `--pool` glob; per case both solvers run
  through the shared judge model (fast_judge) under `-j` worker processes
- per-case feature vector: hdr params, n_reqs, admit_frac, edge/cloud/comm util, peak_cloud,
  slack_min/p95, arrival burstiness (pstdev/mean of per-tick arrivals)
- clustering: greedy decision-stump conjunction mining over the failing subset, reporting
  rules like "E > 3 AND slack_min <= 4" with purity/gain/coverage; also prints worst-10 and
  best-5 deltas, saves failing cases with `--out-dir`
- measured: 10,000 cases × 2 solvers in 110 s wall on 4 cores; full loop (mine → replay →
  hypothesis → A/B) ≈ 4 min per iteration
- found so far: imbg winner fails 1.6% of a 10k pool (worst −4); classes = cloud-starved
  E>3 (42%) and B==2+bursty-bottleneck (35%, distinct cluster). One hypothesis tested
  (DECODE_FIRST) and rejected by the same loop — see experiments-log.
- interaction tooling (2026-08-05): ratio-bin heat table (C/E bins, `<-` flags ≥2x base
  rate), per-feature quantile-bin heat tables, pairwise conjunction table (top-4 features
  by univariate gain), and manual `--interact "B == 2 & arr_burst >= 2.5 & slack_min <= 4"`
  queries (no eval). Example run (2k pool): residual fails cluster to `E > 3 & C <= 1`
  (edge-starved), purity 0.10 vs base 0.014.

### P2c. Exact tiny-instance oracle — `tools/exact_solver.py` + `tools/opt_gap.py` (IMPLEMENTED, pre-spec)

- branch-and-bound DFS over per-tick action subsets; reuses fast_judge's apply_action/step/
  compute_score so optimality is defined by the same state machine as all other tools
- admissible bound = completed + #unfinished whose zero-contention remaining fits by min(D,T);
  `doomed[]` filter; node budget → honest UNKNOWN (exit 2), never approximated
- `opt_gap.py` wraps it: generates a tiny pool (n ≤ 5, Lout ≤ 3, cap 1–2, family-stratified),
  runs baseline + candidate + extra solvers, reports OPT/base/cand sums, per-class gap%,
  and the request types the candidate loses (mechanism). Self-checks OPT ≥ every solver.
- measured (150 tiny, seed 3): 123 solved exactly, 27 UNKNOWN; OPT 157 / base 138 / cand 145
  → gaps 12.1% / 7.6%. bottleneck (E=1,C=1) is the hard class (24.5%/16.3%); contend (E=2,C=2)
  is solved optimally by both (0%). Lost requests are tight-deadline (rho 0.89–1.0).

### P2d. Metamorphic testing — `tools/metamorphic.py` (IMPLEMENTED, pre-spec)

- time-shift probe: rebuilt case with A/D/T all +s (identical problem); asserted
  on_time equal AND score_shifted == score_orig − s·1e-6 — NOT plain equality, because
  fast_judge's V included absolute max(completion) (protocol item
  objective/score_translation_sensitive). Verified: every solver was translation-invariant;
  naive equality failed on every solver for the wrong reason.
- determinism probe (reps), id-reorder probe (warning-only tie-sensitivity signal).
- ran clean on dev, adversarial/gap, and secret pools; I re-ran it before every freeze.

### P2e. Contest-runtime layer — `tools/contest_runner.py` + `limits.yaml` + stress tools (IMPLEMENTED, pre-spec)

Separate timing regime from research mode: ONE solver, ONE input, hard deadline +
memory + verdicts. Limits live in `limits.yaml` (placeholders until Aug 14; warn
0.5×, hard 0.83× of official — never test at the official limit itself).

- `contest_runner.py` — interactive protocol with verdicts
  PASS/WARN/TLE/RE/SIGSEGV/SIGABRT/OOM/BAD_OUTPUT/TOO_MUCH_OUTPUT/EOF; per-case wall
  time + peak RSS; `--cpuset N` for conservative single-core timing; judge overhead
  itself measured (0.42 s at T=50k/n=300) so solver-side walls aren't confused with
  harness cost.
- `stress_max.py` — degenerate zoo (empty, 1 req, identical×1000, reverse-sorted,
  duplicate timestamps, cap-1 bottlenecks, heterogeneous, impossible, huge T).
- `scaling_bench.py` — progressive N (10→1000) with runtime + RSS + superlinear
  ratio flags; 10,000 instances ≠ 10,000 requests in one instance.
- `runtime_stress.py` — hill-climbs instance parameters to maximize wall time
  (finds I* = argmax T(I) before the judge does).
- `crash_sweep.py` + `make san` — verdict histogram over pools; ASan/UBSan
  correctness pass (timings never representative).
- MEASURED WALL (2026-08-05): winner TLEs at N=1000/T=14,000 under the 3 s
  placeholder (0.25s@N=100 → 1.0s@N=250 → 1.9s@N=500 → 3.3s@N=1000). N=500 fixed,
  T=500→20,000: 0.24→3.31 s ⇒ ~O(N·T) per-tick-scan floor + lookahead K×horizon;
  LOOKAHEAD_H=100 only −25%. RSS flat 16–17 MB (no state leak). Spec-day action:
  confirm real max N/T; if large T, make per-tick work active-only and budget-cap
  lookahead. Runtime adversary separately found a 3.3 s case in 120 iters
  (n=143, T≈22.6k, E=2/C=4, Lout≤16).

### P3. State invariants — `src/Solution.cpp` with `-DASSERT_STATE` (always on in validation builds)

- no occupancy overlap within edge / cloud / comm (and across resources where the model forbids it)
- no action emitted twice; no action modified after emission (the CONTAIN class)
- phase transitions legal: WAIT→PE→C_PE→C_DEC→E_DEC→DONE
- est ≤ D and est ≤ T at every admission and apply step
- start ≥ arrival; timestamps monotone
- end-state: re-run the judge's occupancy math in-process and require exact equality with solver state
- `tools/fast_judge.py` made strict by default: illegal action ⇒ case fail (assumed protocol)

### P4. Score decomposition — `tools/decompose.py` (+ judge `--decompose`)

Per case and per class: throughput, on-time count, rejections, slack min/mean/p95,
latency mean/p95, edge/cloud/comm utilization, idle/bubble time, score components
(on_time and V) separately. `tools/decompose_ab.py` prints baseline→candidate metric
deltas so every score change is explained by mechanism.

### P5. Replay debugger — `-DREPLAY_TRACE` + `tools/replay_diff.py`

Compact per-tick decision log (t, emitted actions, key occupancy/SLO state). Diff two runs
on the same case; report the first divergence tick and the downstream consequence:
"at t=43 candidate emitted X instead of Y ⇒ cloud idle until t=51". Build infra now,
use heavily once the real spec lands.

### P6. Robustness A/B — extend `tools/ab_test.py`

Accept condition becomes: all valid ∧ sum ≥ baseline ∧ no class regression beyond tolerance.
Add `--pool dev|regression` (default regression for freeze, dev for development).
Class = profile family (burst/mixed/sequential/tight) ∪ c1–c4 individually.

### P7. Heuristic tournament — `tools/tournament.py`

Build all Makefile variants, run on DEV pool, emit leaderboard (score, validity, per-class
breakdown); challenger vs current-winner pairwise gate on demand. Replaces the manual
make-and-ab_test loop.

### P8. Runtime profiling — `-DPROFILE` + `tools/profile.py`

Per-phase timers (parse/sim/emit) and worst-case per case; the per-case < 1 s and
DEV-pool < 30 s guards were checked by the pipeline. Every new mechanism I accepted passed
before-and-after A/B.

### P9. AI code-audit workflow (process I used)

Before every freeze I ran a dedicated auditor pass (as done for CONTAIN) checking occupancy
desync, emission safety, UB/overflow, and phase legality. Findings went to
`research/notes/experiments-log.md`; every finding ⇒ fix ⇒ full re-A/B before freeze.

### P10. Doc updates

- `docs/05`: label theoretical anchors (throughput ceiling, EDD-optimal) as model-specific
  intuition, not theorems; keep them as hypothesis generators, not constraints.
- Day-1 cadence (docs/05 + docs/DECISIONS.md): reconcile assumptions → establish valid baseline →
  characterize the scoring landscape (decompose on real cases) → freeze safe version →
  experiment aggressively from copies, never destroying the safe baseline.

## Execution order I followed

1. P3 invariants + strict judge (I hardened the measurement substrate first)
2. P1 generator + DEV/SECRET pools
3. P4 decomposition (needed to interpret everything after)
4. P6 robustness A/B (new selection rule)
5. P2 adversarial sweep (used P1 + P4; fed new regression cases)
6. P5 replay (infra built early, heavy use after spec arrived)
7. P7 tournament, P8 profiling (wrappers around existing tooling)
8. P2c exact oracle + P2d metamorphic (both implemented; I re-ran before every freeze)
9. P2e contest-runtime layer (contest_runner + stress_max + scaling_bench +
   runtime_stress + crash_sweep; limits.yaml filled on Aug 14; the scaling wall
   it found was the #1 pre-spec performance action item I resolved)
10. P10 doc/process updates

## Acceptance criteria for the lab itself (all met during the run to #56)

- Current winner passed the pipeline: valid, robust-dominant on DEV, no SECRET surprise
  (one look per freeze).
- Adversarial sweep found ≥ 1 scenario class where the winner regressed vs baseline, or
  I recorded a written negative result ("no class found in N attempts under these bounds").
- Every score claim in `experiments-log.md` was backed by a decompose_ab.py output.
