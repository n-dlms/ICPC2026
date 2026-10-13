> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Trap catalog below is preserved as a historical working note; defenses landed are recorded in `research/notes/experiments-log.md` and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 600 — ICPC Invisible Traps (A–H) — Vibe-Coder Failure Modes

Synthesis of opencode Part 1 (`src/opencode.md` lines 2-44). These are the structural subtleties that AI-assisted competitors (vibe coders) will miss because they pattern-match on "scheduling problem" without understanding the U-shape's domain physics. This note is the **red-team catalog** for our solver and test-generation.

## Trap A — Phase Interference

When a single prefill job enters a batch of decode operations, it substantially degrades performance for both. Naive batching of prefill + decode creates catastrophic interference. **Defense:** the frozen solver keeps phases separated (admit on C_EP only after prefill COMPLETE; admit on C_ED only after cloud round-trip COMPLETE — see `next_stage()` in `src/Solution_asort_frozen.cpp`). `tools/case_gen.py` must generate a `prefill_in_decode_batch` case (arrivals timed so a prefill enters mid-decode-batch of another request) — adversarial zoo entry to add.

## Trap B — Bubble is the Variable

The bubble (idle time between edge and cloud phases) is the *primary* optimization target, not a side-effect. Most competitors optimize makespan/throughput directly and leave bubble on the table. **Defense:** see `research/notes/510-bubble-modeling.md` (instrumentation in `#ifdef BUBBLE_STATS`; existing `wait_list`/`dw_list` already model it). Adding the explicit counter cross-validates that LOOKAHEAD+EVT is actually doing what it claims.

## Trap C — Degenerate Communication Regime

The spec (UNKNOWN until Aug 14) will likely test the COMM boundaries:

- **Zero comm latency:** U-shaped split becomes pointless; all-cloud or all-edge might be optimal. The frozen solver must detect this and degenerate gracefully — verify with `tools/case_gen.py --force-adversarial zero_comm` (in cases/zoo/).
- **Communication >> compute:** edge-cloud split becomes the bottleneck; sequential processing is forced; optimize for least-bad ordering. Verify with a `comm_dominant` cell.
- **Symmetric vs asymmetric delays:** per Rayward-Smith 1992 (`research/notes/507-*.md`), open shop with uniform delays is NP-complete even with 2 machines; asymmetric edge→cloud vs cloud→edge breaks Johnson's rule and simple heuristics. Probe the spec for symmetric vs asymmetric — `tools/reconcile_spec.py --blast communication_model`.

**Defense:** `tools/engine0_probe.py` extracts `comm_bound = total_comm_time / total_compute_time > 0.5` cell label; route to Engine 3 (Flow) if the spec is comm-heavy and pipeline-friendly, else Engine 4 (frozen). The frozen solver's `zero_comm` zoo case (see cases/zoo/) is the regression guard.

## Trap D — SLO Boundary Exactness

Tight SLO (SLO = min_possible_latency) and impossible SLO (SLO < min_possible) are explicit test families. Most AI-generated solvers either:

- Violate the tight SLO by epsilon (floating-point, off-by-one), OR
- Crash or produce invalid output on infeasible requests.

**Defense:** `tools/fast_judge.py --feasibility-check` independently re-derives completion vs deadline (does NOT trust the solver's score) — the archived working discipline (see README.md). The `impossible_slo` zoo case uses `judge_min_completion()` (walks actual judge semantics, not the naive `durations()` sum) so it is genuinely infeasible — solver must not crash or assume feasibility. **Use integer keys throughout** (`usa1_key`, `sjf_key` are integers; see `src/Solution_asort_frozen.cpp` for the static-key pattern — float keys are forbidden for determinism).
`tools/fast_judge.py --overflow-check` is the integer-overflow scan.

## Trap E — Memory / KV-Cache Constraint

If the spec includes an edge memory limit (how many requests' KV caches can fit on edge simultaneously), it becomes a bin-packing constraint on top of scheduling. Most competitors won't model it.

**Defense:** UNKNOWN until Aug 14 — do NOT model it pre-spec. After spec reconciliation, if `tools/reconcile_spec.py --blast memory_constraint` shows an edge memory budget, add to the contract and consider bin-packing as an admission-control dimension (a request is admitted only if there is both edge compute AND edge memory budget). Frozen solver currently assumes no memory budget; verify before submitting if the spec adds it.

## Trap F — Timing Regime Pivot

Per-case, aggregate, or per-phase time budgets completely change the optimization:

- **PER_CASE:** each test independent; optimize locally.
- **TOTAL:** all cases share a budget; meta-scheduler allocates time across cases.
- **PER_PHASE:** edge prefill, cloud compute, edge decode each have separate budgets — sacrifice edge time to save cloud time.

**Defense:** `research/day7/` README documents the 2026 ICPC Huawei challenge as Codeforces-hosted (contest 2251) → per-test limits most probable (~5s / 1024MB). The Day-7 finding is that the harness `for r in reqs: step(...)` loop is the wall-clock dominator on high-T cases (nmax_sequential: 9.5s wall vs 0.58s CPU); CPU-time is the budget, wall is 10× official hang backstop only. The archived working discipline (see README.md) was explicit: NEVER let wall-clock drive value decisions — budget counted work. The V146 postmortem is the canonical timeout-driven failure.

## Trap G — Score Function Shape

Absolute (raw throughput) vs baseline-relative (gap over naive) vs relative-to-best (gap over other teams) changes everything. **Defense:** `tools/score_probe.py` + docs/ASSUMPTIONS.md — block Day-1 checklist on `score_function` being KNOWN, not ASSUMED. opencode Trap 4 ("Objective Function Mirage") inverts the entire optimization strategy; a CONFIRMED relative formula means "optimize the gap", not raw on_time.

## Trap H — Batch Granularity Assumption

If the spec uses **chunked prefill** (breaking prefill into smaller segments that interleave with decode), the problem becomes fine-grained pipeline scheduling with micro-batches; a monolithic-prefill/decode solver leaves massive bubbles on the table.

**Defense:** UNKNOWN until Aug 14. After spec reconciliation, `tools/reconcile_spec.py --blast decode_granularity` and `--blast prefill_chunking`. If chunked: the frozen solver's monolithic-prefill assumption regresses; a chunk-aware Engine 3 (Flow) variant (built from CrossPipe greedy, paper 502 — the sub-block scheduling) is the path. CrossPipe `CrossUDSub` is the algorithm (sub-block incremental earliest-schedulable ready-op scheduler).

## Sources

- `src/opencode.md` lines 2-44 (Part 1 — invisible traps).
- cases/zoo/, src/ARCHITECTURE.md, docs/ASSUMPTIONS.md, docs/DECISIONS.md (the archived defenses).
