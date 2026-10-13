> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Work items below were executed pre-contest and are preserved as historical working notes. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 8 — Optimality Lab (workplan)

Day 8 question: **how close are our decisions to the best decisions possible?**
The oracle (`tools/exact_solver.py` + `tools/opt_gap.py`, built 2026-08-05) is a
scientific instrument, not another contestant. Frozen winner untouched
(`build/solution_evt_cap4_asort_frozen`); everything below operates outside it.

Hard architectural rule (Essay 8): the oracle consumes fast_judge's model
functions (apply_action/step/compute_score) — same semantics as the judge.
No duplicated rules. At contest start (Aug 14): reconciled once, then judge+solver+oracle all
followed the actual spec.

## O1 — Oracle trust (validate before measuring)

- Hand-solved suite: ~5 tiny cases (n=2–4) with FULL optimal schedules derived
  by hand; assert oracle reproduces value AND schedule exactly. Extends the
  single 2-request case from 08-05. Also serves as oracle regression protection.

- Budget-stability: same case solved at 2M / 20M / 200M nodes → same OPT
  (spot-check; guards against prune-order-dependent bugs).

- Fidelity note: solve_exact prunes decode actions completing after min(D,T) —
  sound for on_time optimality (useless decodes only consume windows); the
  self-check OPT ≥ every solver guards the direction. Recorded, not a bug.

## O2 — solve_exact returns the optimal action trace

- Record (t, id, op) along the best DFS path; return it.
- Precondition for O3.

## O3 — First-divergence analysis (Essay 3, the headline output)

- Replay solver-binary actions vs optimal trace in lockstep through the SAME
  fast_judge model; first tick where action sets differ = divergence point.

- Report: tick, request, our action vs optimal action, downstream consequence
  (which requests end up on-time in each trajectory, score delta).

- Classify divergence mechanism: admit-vs-not, admit-which-id, decode-order,
  timing-only (same actions, different tick).

## O4 — Failure-mechanism classification (Essay 5)

- On cand < OPT cases: histogram of FIRST-divergence mechanisms; cross-tab by
  class (bottleneck/starved/contend) and rho looseness. Expected shape:
  admission-dominant (matches the Day-5 lost-request finding).

## O5 — Regime analysis (Essay 7)

- Per-case ρE = edge demand/(E·T), ρC = cloud demand/(C·T) (rho_mean already
  computed but unused). Bucket gap by (ρE vs ρC) imbalance and deadline
  looseness; find where the winner's gap concentrates (expect: the
  must-admit-now-or-miss corner, high ρE asymmetry).

## O6 — Scale to N ≤ 7 (Essay 12: "exact on N <= 7")

- Current: N ≤ 5, T ≤ 30, budget 2M, 18% UNKNOWN. Upgrade path:
  decision-search formulation ("can we land k on-time?", descend k) with
  success-cut; better admissible bound (contention-aware remaining work);
  per-case budget scaling. UNKNOWN policy unchanged (never approximated).

## O7 — Oracle-driven adversarial mining (Essay 4)

- Upgrade the mine objective: hunt `cand ≪ OPT`, not `cand < baseline`.
- Cheap prefilter (ms): keep cases where cand on_time < # individually-feasible
  requests (A + ideal ≤ min(D,T)) — the upper bound that both-bad cases must
  violate; oracle-confirm only the survivors. 50k generated, oracle on the
  filtered tail.

## O8 — Lookahead depth vs optimality (Essay 6)

- Depth-variant builds (fixed evt depth 4/8/16/32/64/256) → opt_gap --extra.
- Question: what depth captures ~all of the useful decision information, and
  does high contention demand more depth (adaptive depth policy)?

## O9 — AI-assisted mechanism analysis (Essay 9)

- For each clustered divergence family: dossier = case + our schedule +
  optimal schedule + first divergence. Feed the dossier (not "give me a
  scheduler") → mechanism hypothesis → candidate policy → machine A/B via the
  existing war-room loop (fail_mine → cluster → replay → A/B).

## O10 — Day-8 report (Essay 12 format)

- Oracle validated; generated N; solver % exact-optimal, mean/max gap; largest
  failure class; first-divergence shares; key discovery; candidate hypothesis
  marked HYPOTHESIS ONLY; frozen winner unchanged.

# Day-8 status update (2026-08-06, EOD)

- **O1 DONE**: chaining-aware remaining_ideal (fix: fast_judge.step() chains
  C_PE→PC→C_EP and DE→C_DE→DC→C_ED in one tick) + decode-hop on-time prune.
  Hand suite cases/tiny_hand/h1-h5 (5 cases, hand-derived traces) ALL PASS via
  tools/oracle_validate.py (exact on_time + score + trace; budget-stability
  2M vs 200M). The pre-fix oracle UNDERSTATED OPT on the starved corner —
  Day-5 gap numbers (12.1%/7.6%) are obsolete.

- **O2 DONE**: solve_exact returns (on_time, score, nodes, trace).
- **Oracle speedup (enables O6)**: two-phase search — descending decision
  passes (k=n..1, first k-solution exits) + V-min pass at k=OPT with an
  admissible V lower bound + on-time decode prune. h1: 45k→77 nodes,
  h2: 340k→144, h5: 571k→540. opt_gap now: 57/60 solved exactly at budget 2M
  (3 UNKNOWN excluded; was 18%+ UNKNOWN before). GOTCHA: score LB
  (k − 1e-6·V_LB) is an UPPER bound on score — prune on lb <= found, not >=.

- **O3/O4 DONE (tiny pool seed 5, 60 cases)**: frozen winner exact-optimal on
  7/57 (12.3%); 50 gap cases; first-divergence histogram: admit_missed 40,
  admit_missed_x2 3, admit_missed_x3 1, admit_reorder 3, mixed 2,
  decode_reorder 2, admit_extra 1, decode_extra 1 → 88% admission-side.

- **DISCOVERY #1 → estfix candidate (EXP-33)**: admission gate est is
  chaining-blind (src/Solution.cpp:408 family: t + pe + COMM + pc + COMM +
  Lout·per overestimates → false-doom). chain_remain_admit/chain_remain_decode
  applied at all 7 admit + 5 decode sites (keys untouched). build/
  solution_evt_cap4_asort_estfix: suite +42.998 (freeze gate PASSES, class_fail
  =[]), gap +21.996, dev +607.958, bigpool +663.975, tiny 96.5% exact-optimal,
  hand 5/5. BUT dev-burst −7.004 FAIL (12/13 share the "flip" pattern:
  loose-D Lout≥7 requests now admitted under E=3 contention displace tighter
  requests — burst_134 14→6 on-time, none of its 14 flipped ids on-time), and
  big-N comm/mixed/burst losses (−106/−40/−95). Isolation: admit-only = burst
  −29/imbalance −14; decode-only = +16 valid but gap −2. VERDICT: high-variance,
  NOT freezeable; frozen winner untouched. Validity: assert byte-identical,
  ASan 500/500, metamorphic clean; runtime 2.01 s vs 1.52 s (wall case).

- **O5 PENDING**: rho/regime cross-tab of the residual gaps (2 tiny + burst
  family).

- **O6 DONE (functional)**: N ≤ 7, T ≤ 40 pool runs at 2M budget, 95%
  solved.

- **O7 PENDING**: mine cand ≪ OPT on the survivor set (prefilter exists).
- **O8 PENDING**: sim depth vs optimality is now secondary — the estfix
  discovery superseded it (depth-bounded displacement is the bottleneck).

- **O9 DONE (partial)**: dossiers built for burst_134 (flip cascade) and
  burst_009 (sim-shift variant); next-session target = displacement-aware
  admission control (gate the policy, not the est; tiny-optimal 96.5% proves
  the est is right on low contention).

- **O10 PENDING**: report after the next experiment.

## Discipline

- Frozen binary never rebuilt, never A/B'd against itself from this stream's
  builds. All oracle outputs land in research/day8/.

- Oracle changes gated by O1 suite + OPT ≥ solver self-check before use.
- UNKNOWN is a verdict, not a data point.
