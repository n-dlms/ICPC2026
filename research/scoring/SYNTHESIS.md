# Scoring & Physics Research Synthesis — 2026-08-23 (archived post-contest)

Produced by research program SA01–SA10 (drafted with assistant support under Ntokozo Dlamini's direction, docs in this directory).
Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56 worldwide.
Purpose: recover the TRUE scoring function from authoritative artifacts, separate proven
facts from inference, and identify the structural mechanisms in the solver that cap points.
Standing rule for this program: docs/ASSUMPTIONS.md / docs/DECISIONS.md claims are leads, not authority;
the local judge is a reimplementation that must be verified, not trusted.

---

## 1. The authoritative scoring function (PROVEN — SA01, SA02)

Source of truth: `problem.md` (full CF statement scrape; live page re-verified identical
by SA02). Local judge (`tools/interactor.py:393-408`, `tools/score_trace.py:127-138`)
matches on **17/17 audit items** and on 12 real + 5 crafted edge probes. The formula is:

```text
clamp(x; b, t)   = max(0, min(1, (x−b)/(t−b)))
tp               = ΣLout / E          where E = (last final-token time) − (earliest ARR)
tp_component     = clamp(tp; tp_base, tp_UB)                    (linear ramp)
tdr              = mean over ALL requests of (P POST done − ARR)
tpot             = Σᵢ(e_lastᵢ − e_firstᵢ) / Σᵢ(Loutᵢ − 1)       (telescoped form)
dist             = sqrt(max(0,(tdr−SLO1)/SLO1)² + max(0,(tpot−SLO2)/SLO2)²)
wait_component   = dist_base>0 ? max(0, 1 − dist/dist_base) : (dist==0 ? 1 : 0)
Score            = 1000 · ( w_tp·tp_component + w_c·wait_component )
```

- `clamp` — conversion primitive, 0 at base, 1 at target, clamped outside
- `tp` — output rate in tokens/ms, `ΣLout` over elapsed `E`
- `E` — last final-token time minus earliest arrival
- `tp_base` — one-request-at-a-time reference rate
- `tp_UB` — estimated high rate
- `tdr` — mean P POST completion minus arrival over all requests
- `tpot` — pooled mean output gap, span over gaps
- `SLO1` — target average wait to decode ready
- `SLO2` — target average output gap
- `dist` — Euclidean excess over SLOs
- `dist_base` — reference scheduler amount above targets, 0 gives binary cliff
- `w_tp` — throughput weight, `w_c` — waiting weight
- `Score` — final 0–1000 points

Answers to the standing question list:

- **Throughput meaning**: total tokens / global elapsed (arrival of first request to the
  LAST token of the LAST request). Not requests/time, not SLO-filtered, no goodput.

- **SLO violations**: NEVER zero a completed test. Partial, continuous, SLO-relative
  penalty through `dist`; binary only when `dist_base=0`. Zero only on protocol error /
  stuck state / TLE(15 s) / MLE(256 MB), and only on that test.

- **Measurement interval**: earliest ARR → last final token. No warm-up, no horizon,
  no drain credit. Everything between the anchors is charged to tp.

- **Aggregation**: scoreboard = SUM over 22 prelim tests (feedback only, max 22,000).
  FINAL ranking = arithmetic MEAN of 20 frozen system tests (after Aug 28 11:00 UTC).

- **Feasibility gate**: none. No per-request deadlines. tdr/tpot are pooled MEANS.
- **Request value**: all tokens equal in the numerator; requests differ only in cost.
- **Unfinished requests**: not a scoring concept — they must all finish (END only after
  all FIN). Abandonment ⇒ stuck state ⇒ 0 for the test.

- **Admission control**: no reject primitive exists. Never abandon; delaying prefill
  never paid in any experiment (SA06).

- **Latency continuity**: fully continuous (linear ramps + circular dist geometry).

## 2. The physics laws that actually matter (SA03, SA04, SA07)

Full card L1–L14 in `SA07_control_restatement.md`. The load-bearing ones:

- **L2 Finish line**: the clock ends at the LAST token. One straggler reprices every
  token (demonstrated 3.2× tp loss on adv_straggler; 69.7-pt swing in one crafted pair).

- **L3 Span-only TPOT**: internal token cadence is UNMEASURED — only e_first→e_last per
  request enters tpot (telescoping verified live; judge accumulates span).

- **L4 Dead zone**: [P POST → first token] is invisible to BOTH waiting metrics; it only
  costs tp if it delays the makespan. First-decode parking is almost free.

- **L5 Pooled means**: 1 ms on one request = 1/R ms on tdr. Sacrificing a few requests'
  waiting time to speed the herd is legal and cheap.

- **L6 SLO-relative pricing**: 1 ms late costs 1/SLO1 per request on excess_tdr.
- **L8/L9 No abandonment; prefill delay never pays; dead-zone parking dominates.
- **L12 Everything reduces to steering three aggregates**: E (elapsed), mean tdr, mean
  span. Any mechanism steering anything else is steering a dead quantity.

- **L10 Endgame = race**: at the drain, always finish; lull-parking lost 121–234 pts
  and one twin hit stuck=0 (SA06).

## 3. Where the points are (SA05 ledger + SA09 ceilings)

- Sealed leader 16,147.866 / 22,000. Missing 5,852. **~4,900–5,200 were structural**
  (below rigorous resource floors: link bytes, cloud-ms/K, local-ms, per-request
  dependency chains — four bounds, validated against 43 probe attacks).

- Realistic contested pool at program close: **~50–150 prelim points rigorous, ≤580 optimistic**
  (if starve-class tests were order/V-table structured — wrong-sign history weighted low).

- Scoreboard context (SA02): prelim rank 174/3,498; top-100 cutoff was +123 total; leader +572.
  Final = mean of 20 frozen system tests (after Aug 28 11:00 UTC; contest has since ended with final rank #56).

- **One frozen zero = −50 final mean points.** TLE audit: ~10.5 µs/frame ⇒ a
  2·10⁶-frame frozen test ≈ 21 s > 15 s ⇒ 0. TLE guard was recorded as change #0.

- Component split of missing: 71% tp-side / 29% waiting.

## 4. The two proven structural caps (SA09 probe defeats, SA10 line-level)

A protocol-correct throwaway Python solver BEAT the artifact on two classes:

1. **x9 class (V-shaped decode tables, pure-tp, local-bound)**: 388.6 → 736.8 with
   single-cloud mega groups. Root causes at line level (SA10, run-verified):

   - `src/policies.cpp:56-61` — the EFT cloud-chooser's climb rule breaks at m=1 on the
     table's rising arm, never sees the fall → load-linear choice → perfect 250×8 spread.

   - `src/scheduler.cpp:275-284` — eager first-frame D_POST grabs cloud 0's 250 sub-batch
     (188.5 ms vs 106 unified) → permanent cohort phase-split.

   - `src/scheduler.cpp:174-176` — DPRE_ACCUMULATE target is `w_c>0 ? group_size : 0`:
     accumulation is compiled OUT on pure-tp tests (proof: w_c 0→0.05 in a case copy ⇒
     D_PRE=2000 for all 100 rounds, +40 pts).

2. **m5_order class (mixed-Lout/order pools)**: 779.9 → 979.0 with trivial lockstep mega.
   No cap binds — it is readiness churn: 647 D_PREs, 89 singletons, ragged 66/44/26 tail;
   local decode occupancy 14,333 ms vs mega 6,804. Three mechanisms refuse herd-sync:
   NoDelay at alpha≥0.8 (`src/Solution.cpp:189-190`), hold_dpre's multi-token kill-switch
   (`src/policies.cpp:287`), and the accumulate skip's narrow conditions
   (`src/scheduler.cpp:170-190`, never waits for in-flight members).

## 5. Fallacies found in the existing docs (correcting the record)

- D-008 claim "any single request crossing SLO flips the waiting component" — FALSE:
  means only (SA04).

- D-023 "smooth decode" objective — cadence is unmeasured (SA04; judge accumulates span).
- docs/ASSUMPTIONS.md §1.8 per-request TPOT phrasing — imprecise; code was correct (SA01).
- D-143's starve-class saturation model is confirmed as a LOCAL bound, but tp_UB sits
  3.3× above the physical ceiling on #5/#6/#22 — those tests never saturate tp; every
  token/ms is linear points (SA03). The "structural ~1,100 pts" verdict for #5/#6 is
  mostly reaffirmed by SA09's floors, but the m5_order probe shows order/mixed-Lout
  structure could unlock a slice of it.

- The "K-scaled inflight / dist_headroom" family steers real quantities but is
  prelim-tuned; SA05 lists the overfit-risk constants.

## 6. Ranked engineering plan recorded at program close (SA10; all changes gated, battery per docs/DECISIONS.md; program completed 2026-08-23)

0. **TLE guard** for >1.2M-frame tests (10.5 µs/frame × 2M = 21 s > 15 s). Highest
   expected value at close: protected the final mean from a single frozen zero (−50 pts).

1. **x9 cohort-integrity pack** — A2 (D_POST wait-for-unified), A1 gated (single-cloud
   re-merge only when per-token cost minimized at m≈R ∧ local-bound ∧ w_tp-dominant),
   A3 (fix the m=1 climb-rule blindness on V-tables). Expected at close: +250–340 local on the
   x9 class. Ungated A1 was FALSIFIED (m19b +5.2% E, x4 +20.6% E) — the gate was mandatory.

2. **Gated herd-sync hold** for m5_order class (NoDelay/kill-switch/accumulate-skip
   revision). Recorded guidance: one CF probe maximum — wrong-sign family historically.

3. **Span-aware dead-zone parking depth** (SA04/SA07; V_tpot>0, V_tp≈0 gated).
4. **Wave pipelining / phase-offset cohorts** (SA08 lot-streaming, 1F1B alternation,
   geometric sublot sizing) — biggest theoretical lever (m19b E 2.28× floor at 27%
   local occupancy) but highest complexity; every staggered-wave gate needs an
   event-backed wake proof (D-140-P0 deadlock class; Rybko–Stolyar instability warning).

5. **Drain-tail de-fragmentation + survival-conditioned Lout estimate** max(μ, iter+1)
   for tail priority (SA06; crafted-lull case 323.7 vs 451.0).

## 7. Document inventory

| Doc                            | Topic                                                               |
| ------------------------------ | ------------------------------------------------------------------- |
| SA01_authoritative_formula.md  | formula recovery, judge audit 17/17, Example-1 anchor               |
| SA02_official_artifacts_web.md | live statement check, aggregation, standings, prior-year analogue   |
| SA03_throughput_interval.md    | clock semantics, tp_base reconstruction, CF tp_UB fit               |
| SA04_waiting_component.md      | telescoping, dead zone, pooled means, 95-case regime census         |
| SA05_aggregation_losses.md     | 22-test ledger, loss decomposition, overfit register, TLE audit     |
| SA06_admission_value.md        | admission/abandonment, value keys, hidden-Lout, endgame             |
| SA07_control_restatement.md    | physics card L1–L14, scorecard harness (experiments/sa07/)          |
| SA08_literature.md             | 7 papers added to research/papers/, lot-streaming application notes |
| SA09_ceiling_analysis.md       | 4 resource bounds, ceilings, probe defeats (experiments/sa09/)      |
| SA10_structural_audit.md       | 52-mechanism census, line-level defeats, falsification designs      |

Harnesses: `experiments/sa01/independent_scorer.py`, `experiments/sa03/tp_analysis.py`,
`experiments/sa07/scorecard.py` (+ CSV over 95 cases), `experiments/sa09/ceiling.py`,
`experiments/sa09/probe.py` (the solver that beat the artifact).
