> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Research below was completed 2026-08-05 and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 2026-08-05 — Optimization-techniques research (for EVT rework)

Assistant-supported research (directed by Ntokozo Dlamini), sources fetched live. All URLs verified during research.

## Contest map corrections

- 2022 = CF 1724 (VM placement, interactive per-request, **15 s**/512 MB — not 4 s),
  2023 Spring = 1813, 2023 Fall XR = **1885 (2 s/test, offline)**, 2024 = 2015
  (**10 s/test**), 2025 Winter = 2177 (**5 s/test**, batched-interactive), 2026 = CF 2251
  (announcement https://codeforces.com/blog/entry/155646, statement unpublished).

- 2026 UCup **Finals** challenge (frankthinker/huawei-llm-scheduling-challenge,
  GitHub): LLM scheduling, V = Σ(Sᵢ−Aᵢ) + max(Sᵢ+L_outᵢ) + max(Sᵢ−Aᵢ) — CONFIRMED
  sibling of our problem. **All tests share 120 s total, 4 GB** (README). USA1 1st,
  Dvoe protiv vetra 2nd, Afterlife 3rd. Repo = independent reconstruction, NOT
  winner source; best-valid artifact V133 = 374.98; final V146 = `-1 / Run timeout`.

## Winner-style architecture (USA1 reconstruction, README §1)

- Rolling online planner, **75-tick window**, key `L_out·(L_in+L_out)^1.594982`
  (matches our USA1 key), commit only next-tick starts, never reads future
  arrivals; SPT switch under low-B pressure; full-info phase once all requests
  known; **transactional accept**: candidate replaces incumbent only if a full,
  exactly-validated legal plan with strictly lower V.

- Postmortem lesson (the #1 killer, V146): clock()-selected work + appended-not-
  exchanged stages + per-case testing instead of **aggregate Σ-CPU < 120 s − 10–20%
  margin**, under GCC AND Clang (layout changes runtime distribution). Use
  **semantic-work ledger** (element-visits, rollouts, validator cells), clock only
  as kill-switch. Aggregate-budget gate = our suite_gate --total-timeout shape.

- Research findings worth stealing:
  - **89.8% of the latest-wave gap is locked in earlier committed prefix** — early
    admission/owner decisions dominate; admission is the strategic lever, not tail.

  - Owner choice matters more than key order; flexible short request on large GPU
    once cost 18 units of committed-prefix regret.

  - Full-info portfolio phase gained only ~1 pt (373.62→374.56) — the big win is
    online admission structure.

  - Tiny exact counterexamples often need ≥4 requests changed at once; fixed
    pair/triple repairs cover only part of the residual.

  - Identical observable prefixes can have opposite realized futures → no
    deterministic prefix-only rule dominates (validates "never force").

  - Beam width 16 global serial-SGS ≈ pattern oracle (15/16 of exact gap) with
    parent envelope as unconditional fallback.

## Scheduling rules (deadline/capacity)

- EDF optimal only preemptive single-machine; **non-preemptive/multi-resource EDF
  provably suboptimal** (skedbooks.com non-optimality of EDF and LST) → ordering is
  heuristic, feasibility is not; feasibility test per dispatch required.

- **Slack test = standard safe admission control**: admit iff slack = D − now −
  remaining ≥ 0; never displace an incumbent below slack 0.

- **Moore-Hodgson** (arXiv:2104.06210): optimal max on-time set for 1 machine,
  scan EDD, evict longest when over-committed; O(n log n). Candidate per-GPU decode
  admission rule; drop-longest-first recovers optimal on-time set. (Single-machine,
  no release times, unweighted — limitations noted.)

- SJF/SLAI unbounded approximation in LLM serving (arXiv:2508.06133; Sorted-F ≤48,
  ~3 at high concurrency) → gating validated (our IMBAL_GATE is the right shape).

- LST/LLF: min-slack first; tie thrashing → ELLF (EDD inside ties); "works only on
  current state, can be suboptimal in brief overload" — matches our SLAI evidence.

## Lookahead horizon

- Fixed large horizons can lose to greedy (Yue & Al Kontar, AISTATS 2020); rolling
  horizon without terminal feedback is myopic; the fix = base-policy-generated
  simulated futures + **base policy as budget-exhausted fallback** (Bertsekas
  rollout). Our adaptive evt_depth (work-budgeted, [2,256]) is the right shape;
  published winner data point = 75 ticks.

- Horizon must adapt to instance size, never a constant (our fixed-100 cap cost −7).

## Interactive/submission engineering (CF-specific)

- Flush per message; cin.tie(0) + explicit flush; read-all-first + emit wrapper
  (UCup judge is offline validation of stdout, per-tick only in local harness);
  never block on stdin mid-run; **SIGPIPE on judge disconnect → set SIG_IGN and
  exit cleanly, never abort()**; long long everywhere for t×size products and V
  accumulators; no allocations in per-tick loops (flat arrays, vector reuse,
  active lists, arena); deterministic tie-breaks on id, never pointer addresses.

- Lazy deletion for time-dependent PQ keys (keys monotone-improving in t; push
  (key,id,version), skip stale on pop).

- **PieceTimeline-style segment/bit-parallel feasibility** (frankthinker README §2):
  segment-compressed timeline, forbidden-interval queries, prefix sums for O(1)
  duration integrals — the machinery that makes exact per-GPU×time checks cheap.
  Protocol-agnostic; candidate for our occupancy arrays.

- "Multiple cuts from one instance do not create independent cases"; case-geometry
  axes: full burst / sparse waves / microbursts / gaps, capacity-1 bottlenecks,
  identical×N masses, huge T empty tail — our degenerate zoo covers these.

## Implementation implications for our solver

1. Keep EDD+USA1 greedy base + IMBAL_GATE; lookahead depth adaptive (done, evt_depth).
2. Transactional accept + aggregate suite gate + semantic work ledger (never
   clock-driven portfolios) — already our discipline; V146 is the cautionary tale.

3. Port active sets (Day 3) to kill the last per-tick full-N scan; consider
   segment-compressed occupancy (PieceTimeline-style) for capacity scans.

4. Moore-Hodgson-style decode admission = candidate experiment AFTER spec (needs
   case evidence; check rejected-ideas before promoting).

5. SIGPIPE_IGN + graceful exit path; long long hygiene audit of V accumulators.
6. Admission/owner-choice quality is the strategic lever (89.8% lock-in), not
   deeper lookahead or 2-request local repairs.
