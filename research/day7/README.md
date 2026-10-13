# Day 7 — Judge execution research

> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Checklists and "to verify" items below are preserved as historical pre-contest working notes. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks).

> **Historical note:** This document records pre-contest assumptions as of
> 2026-08-06. Its estimated time and memory limits were superseded by the
> official problem statement. Use the [root README](../../README.md) for the
> confirmed contest limits and final results.

The Day 7 question was whether the judge could execute the scheduler within every
constraint. At the time, the algorithm was frozen as
`build/solution_evt_cap4_asort_frozen`; its worst measured CPU time was 0.58 s
against an estimated 5 s per-test budget (see §6). This document combined four
research notes into a verification plan.

Notes (read first):

- `research/day7/oj-execution-contract.md` — generic judge contract: time/memory measurement, I/O, flushing, signals, UB, determinism (10 sections, sources graded).
- `research/day7/icpc-domjudge-huawei-judge.md` — ICPC/DOMjudge + Huawei challenge platforms; **the 2026 challenge is Codeforces-hosted (contest 2251)**.
- `research/day7/codeforces-judge-system.md` — Codeforces internals (CPU-time measurement, per-test processes, Windows sandbox, verdict semantics).
- `research/day7/atcoder-judge-system.md` — AtCoder internals (max(wall, CPU) measurement, per-language multipliers, scoring-task aggregation).

---

## 1. Most probable contract (Codeforces-hosted ICPC Huawei challenge, per precedents)

| Property         | Likely value                                                                               | Evidence                                        |
| ---------------- | ------------------------------------------------------------------------------------------ | ----------------------------------------------- |
| Host             | Codeforces, contest 2251 (blog entry 155646)                                               | icpc-domjudge-huawei note §4                    |
| Execution        | one binary, stdin/stdout, per-test-case fresh process                                      | DOMjudge model, all CF-hosted Huawei challenges |
| Time limit       | per-test ~5 s (2025 Winter: 5 s; 2023 Buffer interactive: 15 s)                            | icpc-domjudge-huawei note §5–6                  |
| Memory           | per-test ~1024 MB (2025 Winter) / 2 GB typical DOMjudge                                    | same                                            |
| Measurement      | CPU time primarily, lax wall-clock backstop (DOMjudge); CF measures CPU time               | oj-execution-contract note §1, cf note §2       |
| Scoring          | special judge, sum of per-case points; TLE case = 0 for that case                          | §5, §7                                          |
| Hidden set       | re-generated same params different seed; final = re-test of **latest non-zero submission** | §5                                              |
| Aggregate regime | exists on sister platform (120 s suite) — keep `suite_gate` until spec says otherwise      | §6                                              |

**The single most important spec question on Aug 14**: per-test vs aggregate budget. Everything else is secondary.

## 2. Timing-regime matrix (fill in from the real spec on Aug 14)

| Question                         | Unknown until spec | Default assumption to verify                                               |
| -------------------------------- | ------------------ | -------------------------------------------------------------------------- |
| Per-case time limit?             | Possibly           | Yes, ~5 s (Codeforces-hosted)                                              |
| Aggregate suite limit?           | Possibly           | No, but keep `suite_gate --total-timeout 120`                              |
| Wall or CPU time?                | Possibly           | CPU primary + wall backstop (DOMjudge/CF)                                  |
| One process per test?            | Possibly           | Yes (fresh process per case)                                               |
| Memory per process / aggregate?  | Possibly           | ~1024 MB per case                                                          |
| Interactive (per-tick) protocol? | Possibly           | Yes — this is an online scheduling problem; per-tick reads, flush per tick |
| Custom checker / scoring judge?  | Definitely         | Special judge, sum of per-case points                                      |
| Output size caps?                | Possibly           | DOMjudge default 8 MB output cap                                           |
| Post-contest re-test?            | Likely             | Latest non-zero submission re-tested on hidden set                         |

## 3. Failure-mode red-team checklist (mechanism-based, not input-guessing)

For each mechanism, the tool/check that already exists or must be added:

| Mechanism (adversarial direction)            | Check today                                                                                      | Status                                                                                                         |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------- |
| Maximize active requests / N                 | `tools/stress_max.py --nmax 2000`                                                                | exists — max CPU 0.58 s zoo-wide                                                                               |
| Maximize decode rounds (DW re-entry)         | zoo decode-saturated case; watch dw_list growth (Day-3: ~1658 entries for ~300 live)             | exists — max_decode_chain 0.40 s CPU                                                                           |
| Maximize wall time (runtime adversary)       | `tools/runtime_stress.py` — hill-climbs params                                                   | exists (0.130 s max)                                                                                           |
| Maximize memory (history vs live state)      | audit: which structures grow with time, not live state? (dw_list, seen arrays, journals)         | **done (A2)** — peak 17.7 MB at N=2000/T=50000; no structure grows unbounded                                   |
| Maximize simulation work                     | `sim_work_used` ledger (cap 1.8e9) — deterministic, not wall-driven                              | exists                                                                                                         |
| Push right against time boundary             | `contest_runner.py` warn 0.5×/hard 0.83× of limit; target ≤ ~50% of limit                        | exists — max zoo CPU 0.58 s = 12% of 5 s                                                                       |
| Truncated/EOF/garbage input                  | EXP-29 fix (`int k=0; if(!st) break;`); rehearsal battery                                        | exists                                                                                                         |
| Signal-level death (SIGPIPE/SIGXCPU/SIGABRT) | rehearsal battery covers sigpipe; sanitizer sweep covers UB                                      | exists                                                                                                         |
| Determinism / rejudge sensitivity            | suite/gap/pool byte-score-identity; no clock-driven decisions                                    | exists (work ledger not clock)                                                                                 |
| Integer overflow in extreme params           | sanitizer sweep (UBSan) covers UB; **add explicit max-value audit for N\*T, score accumulators** | **done (A1)** — 8 latent sites, all conditional on spec bounds > thresholds; punch list in a1-integer-audit.md |
| Output protocol violation                    | assert build (ASSERT_STATE) + emission mirror                                                    | exists                                                                                                         |
| Hidden-set overfit (seed shift)              | SECRET pools (one-look each)                                                                     | exists — none burned since Day-4                                                                               |
| Latest-submission trap                       | never submit experimental builds; only `asort_frozen`                                            | exists — discipline                                                                                            |

## 4. Day-7 release-gate definition

`tools/contest_runner.py` verdicts (TLE/RE/SIGSEGV/SIGABRT/OOM/BAD_OUTPUT/TOO_MUCH_OUTPUT/EOF) + `suite_gate` + `crash_sweep` + ASan/UBSan + `runtime_stress` + `stress_max` + `metamorphic` + rehearsal battery. Release only if ALL pass, with wall ≤ ~0.5× of the (spec-confirmed) limit on every case including max-input.

Gate discipline (unchanged): freeze = `build/solution_evt_cap4_asort_frozen`; experimental builds may die; frozen branch is never casually edited; promote only via experiment→prove→promote.

## 5. Day-7 philosophy (see README.md)

> The judge is part of the problem. A scheduler that produces an excellent score but violates the execution contract is not a good solution. Runtime, memory, input/output behavior, termination, integer safety, undefined behavior, and scoring validity are first-class correctness properties. Optimize only after profiling. Never assume the local harness reproduces the official timing regime. Treat the official challenge specification as the source of truth and Codeforces/AtCoder as rehearsal environments. Maintain a frozen release candidate and promote changes only through differential, correctness, sanitizer, adversarial, and resource gates.

## 6. Day-7 release-gate baseline (recorded 2026-08-06, asort_frozen sha 37d09a8…)

**Key measurement correction**: the Python harness's WALL includes the per-tick feeder (Python sim) — on high-T cases feeder dominates wall (nmax_sequential: 9.5 s wall vs 0.58 s CPU; wall case: 1.56 s wall vs 0.43 s CPU). The official judge (CPU-primary per DOMjudge/CF) measures ~1/4 of our local wall. contest_runner now reports both; verdicts are CPU-primary (WARN_CPU/TLE_CPU at 0.5×/0.83× of limit) with wall demoted to a 10× hang backstop.
Measurement gotchas fixed: RUSAGE_CHILDREN ru_utime+ru_stime is CUMULATIVE across children — must delta around each spawn (fixed); ru_maxrss is a max (safe).

| Gate                                   | Result (2026-08-06)                                                |
| -------------------------------------- | ------------------------------------------------------------------ |
| suite 20 (contest_runner, CPU columns) | 20/20 PASS; max CPU 0.0072 s                                       |
| gap 20 (adversarial class)             | PASS (freeze gate, delta 0.000000)                                 |
| pool 1500 + bigpool 60                 | byte-score-identical (freeze gate)                                 |
| stress_max zoo 17 (N=2000, T=50000)    | 17/17 PASS; **max CPU 0.579 s** (nmax_sequential), max mem 17.7 MB |
| runtime adversary (200 iters)          | max wall 0.130 s, PASS                                             |
| scaling (N=10→1000, T=140→14000)       | N=1000: 0.40 s, RSS 17.5 MB, ratios 1.5–1.9                        |
| metamorphic (320 cases, shifts 5/37)   | time-shift neutrality + determinism hold                           |
| suite_gate aggregate (120 s hedge)     | 0.19 s / 120 s, valid                                              |
| rehearsal battery                      | all clean; sigpipe rc=-13 expected                                 |
| crash_sweep / ASan+UBSan+ASSERT        | 3000/3000, 500/500 (freeze gate)                                   |

**Conclusion**: every execution-contract dimension is green with ≥ 8× margin on CPU (0.58 s worst-case vs ~5 s), ≥ 55× on memory (17.7 MB vs ~1 GB), ≥ 80× on output (~100 KB vs 8 MB). The frozen binary is release-ready under the probable contract; Aug-14 spec check applies the A1 punch list (8 conditional UB sites) and fills the §2 matrix.
