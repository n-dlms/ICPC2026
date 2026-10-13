# SA04 — The Waiting-Time Component: Exact Semantics, Geometry, and Strategic Implications

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA04 drafting). Read-only for src/ and tools/; experiments in
`experiments/sa04/` (harness copies) and `/tmp/sa04/` (run directory). Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56.

Claim tags: [FACT-statement] = problem.md (lines cited), [FACT-code] =
tools/interactor.py / tools/score_trace.py, [DERIVED] = algebra, [EXPERIMENTALLY
SHOWN] = live judge run with numbers. Ground truth re-verified this session:
problem.md:443-478 (scoring), tools/interactor.py:146-153,249,399-407.

All micro-experiments use `/tmp/sa04/mysolver.py` — a full-protocol scripted solver
(batch=1 decode, single P PROC chunk) with hold knobs
(`--hold-dpre=rid=R,round=K,ms=X`, `--hold-ppost=rid=R,ms=X`) driven through
`SA04_ARGS` env, plus crafted cases with dense `EMPTY` wake-up frames (every
0.001/0.005 ms, grid-aligned to durations so holds fire exactly). Runs:
`python3 tools/interactor.py --case <case> --solver /tmp/sa04/mysolver.py
--trace-file <trace>`; breakdowns recomputed from the judge trace (validated
against `tools/score_trace.py` — identical to 6 decimals on the smoke run).

Micro-case template: K=2, S=0, latency=1.0, bw=1e9, bpt=1, layers=1, table flat
(PRE=1.0, all others 0.001 [or 0.005 grid in Exp C]) → prefill = 3.002 ms,
decode round = 2.003 ms — fully predictable clocks.

---

## 1. Interval-measurement map — which clock each component sees

Per request i with arrival a_i, P POST completion p_i, token times
e_1<...<e_L (D POST completions):

```text
a_i ────────── p_i ──────────────── e_1 ───────────────────── e_L ─────────►
   [ARR→P POST]   [P POST→1st token]   [1st token→last token]
   tdr_i          DEAD ZONE for        tpot span_i = e_L − e_1
   (tdr)          tdr AND tpot         (tpot)
   elapsed sees:  last_finish = max_i e_{i,L} only (tp numerator ΣLout fixed)
```

- `a_i` — arrival time
- `p_i` — P POST completion
- `e_1` — first token time
- `e_L` — last token time
- `tdr_i` — per-request wait
- `tpot` — pooled gap mean

```text
tdr = Σ_i(p_i − a_i)/R
tpot = Σ_i(e_{i,L} − e_{i,1}) / Σ_i(Lout_i − 1)
tp = ΣLout / (max_i e_{i,last} − min_i a_i)
```

- `tdr` — mean wait over all requests
- `tpot` — pooled mean gap
- `tp` — output rate
- `R` — request count
- `Lout_i` — output length of request `i`

[FACT-statement problem.md:447-450; FACT-code
interactor.py:249,399].

**Experiment B (single request, Lout=2, 5 ms placed in each interval;
`/tmp/sa04/expB.txt`):**

| variant                 | tdr       | span/tpot | elapsed | tp     | dist  | c_w    | SCORE  |
| ----------------------- | --------- | --------- | ------- | ------ | ----- | ------ | ------ |
| B0 baseline             | 3.002     | 2.003     | 7.008   | 0.2854 | 2.239 | 0.7761 | 649.58 |
| B1 5ms in [P POST→tok1] | 3.002     | **2.003** | 12.008  | 0.1666 | 2.239 | 0.7761 | 517.55 |
| B2 5ms in [tok1→tok2]   | 3.002     | **7.003** | 12.008  | 0.1666 | 6.328 | 0.3672 | 313.11 |
| B3 5ms in [ARR→P POST]  | **8.002** | 2.003     | 12.008  | 0.1666 | 7.073 | 0.2927 | 275.83 |

[EXPERIMENTALLY SHOWN] All three variants have IDENTICAL elapsed (12.008) and
identical c_tp — the score differences are purely which component absorbs the
5 ms: B1 moves ONLY tp (−132.0 pts); B2 adds tpot (+5.000 exactly) and loses
−336.5; B3 adds tdr (+5.000 exactly) and loses −373.8. **The [P POST→first
token] window is invisible to BOTH tdr and tpot — it costs points only through
the makespan (last token), if at all.** (tpot sensitivity happened to be lower
than tdr here because SLO1=SLO2=1 with e_tdr>e_tpot; §4 gives the general rule.)

**Freedom probe (two requests, `/tmp/sa04/expB2.txt`):** rid0 (Lout=2, arr 0) is
not the last token holder (rid1, Lout=4, arr 6, finishes 17.014):

- B2.3: rid0's first decode held +5.000 ms → tdr=3.0020, tpot=2.0030,
  elapsed=17.0140, SCORE=762.9279 — **byte-identical to the 762.9279 baseline**.
  A dead-zone delay on a non-makespan-critical request is EXACTLY free [EXPERIMENTALLY
  SHOWN].

- B2.1: same hold +20 ms → rid0 becomes the last finisher → elapsed 17.014→27.008,
  waiting comp untouched (c_w 0.8328 both), score 762.93→627.00 (pure tp).

- B2.2: +20 ms in-span instead → same elapsed 27.008, c_w collapses 0.8328→0.0000
  (dist 1.003→6.003 > dist_base 6) → 210.58. Same elapsed budget, −416.4 pts vs B2.1.

- B2.4 caveat: at +7.9 ms (rid0 still not last) the score DID drop
  (762.93→725.84, tpot 2.003→2.227) — because rid0's delayed rounds re-entered
  the SHARED UP/DOWN FIFOs and stretched rid1's rounds. The dead zone is free
  only if the injected work does not contend with other requests' in-flight
  rounds (link/queue second-order effect) [EXPERIMENTALLY SHOWN].

---

## 2. Telescoping: internal cadence is irrelevant; only the span exists

[DERIVED] Σ_{j=1}^{L−1}(e_{j+1}−e_j) = e_L − e_1, so per-request gap patterns
never enter tpot except through their sum. The judge implements the span form
directly [FACT-code]. Consequences: (i) tokens may be produced arbitrarily
burstily inside the span at zero tpot cost; (ii) per-request "mean gap" =
span/(Lout−1) always — smoothness is never measured; (iii) the pool weights
requests by gap count (Lout−1), not equally.

**Experiment A (single request, Lout=3, `/tmp/sa04/expA.txt`):**

- A1 gaps (10.003, 2.003) vs A2 gaps (6.003, 6.003) — same span 12.006, same
  first token 5.005, same last token 17.011:
  A1: tdr=3.002000 tpot=6.003000 tp=0.176356 dist=5.388693 SCORE=87.7660;
  A2: tdr=3.002000 tpot=6.003000 tp=0.176356 dist=5.388693 SCORE=87.7660 —
  **identical to 4 decimals across every component** [EXPERIMENTALLY SHOWN].
  (A judge scoring max-gap, p99-gap, or gap-variance would separate these.)

**Experiment A2 (pooling weights, `/tmp/sa04/expA2.txt`):** rid0 Lout=10
(span 18.027), rid1 Lout=2 (span 32.003). Judge mean_tpot = 5.003000 =
(18.027+32.003)/(9+1) — the pooled-gap mean. The mean-of-per-request-means
reading would give (18.027/9 + 32.003/1)/2 = 17.003 — falsified. mean_tdr =
3.502 = (3.002+4.002)/2 — request-count mean, NOT Lout-weighted
[EXPERIMENTALLY SHOWN; confirms SA01 fallacy-lead #1 with a live probe].

---

## 3. Mean-pooling semantics and exchange rates

[FACT-statement problem.md:447-458] Both axes are POOL MEANS. There is no
per-request term anywhere: any per-request redistribution preserving the pool
sums is score-neutral. No per-request SLO exists; no fairness term exists; the
only hard constraint is that every request completes (problem.md:420-421).

Exact pool sensitivities (R = request count, G = Σ(Lout_i − 1) = ΣLout − R):

- Δmean_tdr = (Σ_i Δtdr_i)/R — 1 ms on ONE request = 1/R ms on the mean.
- Δmean_tpot = (Σ_i Δspan_i)/G — 1 ms added to ONE request's span (anywhere
  inside it, in any gap pattern) = 1/G ms on the mean.

- d(dist)/d(tdr_i) = e_tdr/(dist·SLO1·R), d(dist)/d(span_i) = e_tpot/(dist·SLO2·G)
  — identical per request: the formula itself gives NO request priority
  structure [DERIVED].

**Experiment C (R=20 dribble 40 ms apart, Lout=2, `/tmp/sa04/expC.txt`):**

- CA: one request's P POST held +200 ms → mean_tdr 3.01000→13.01025,
  elapsed UNCHANGED 767.040, tpot unchanged 2.015.

- CB: all 20 requests' P POST held +10 ms → mean_tdr 3.01000→13.01000 (the
  0.00025 residue on CA is EMPTY-grid quantization; the two deltas are the same
  to 2.5e-4 ms), elapsed 767.040→777.040.

- Same mean_tdr, same dist (5.5158 vs 5.5157), same c_w=0 — but CA scores
  200.93 vs CB 196.73: **concentrating TDR damage on few requests strictly
  dominates spreading it** (identical waiting cost, smaller makespan cost)
  [EXPERIMENTALLY SHOWN].

- Local derivative probes (Δmean = 0.025 ms): measured d(dist)/d(mean_tdr) =
  0.415079 vs formula e_tdr/(dist·SLO1) = 0.413489 (+0.4%); measured
  d(dist)/d(mean_tpot) = 0.380951 vs formula 0.374824 (+1.6%) — converging to
  the analytic gradient as Δ→0 (sqrt convexity inflates finite differences)
  [EXPERIMENTALLY SHOWN].

- Point prices at that operating point (w_c=0.5, dist_base=2):
  dScore/d(mean tdr 1 ms) = −5.17 pts; per single-request ms = −5.17/20 =
  −0.26 pts; dScore/d(mean tpot 1 ms) = −4.69 pts; per one-span ms = −0.23 pts.
  Measured from CL/CM: −10.489/2 ms and −9.956/2 ms on one request ✓.

---

## 4. dist geometry

[FACT-statement problem.md:459-478]

```text
e_tdr = max(0, (tdr−SLO1)/SLO1)
e_tpot = max(0, (tpot−SLO2)/SLO2)
dist = √(e_tdr²+e_tpot²)
c_w = max(0, 1−dist/dist_base) (binary if dist_base=0)
```

- `e_tdr` — relative TDR excess
- `e_tpot` — relative TPOT excess
- `tdr` — mean wait
- `tpot` — pooled gap mean
- `SLO1` — TDR target
- `SLO2` — TPOT target
- `dist` — Euclidean excess
- `dist_base` — waiting budget
- `c_w` — waiting component

(a) **Iso-score curves are quarter-circles** in (e_tdr, e_tpot) space. To hold
dist constant, de_tpot/de_tdr = −e_tdr/e_tpot: 1 relative unit of TDR excess
trades against e_tdr/e_tpot relative units of TPOT excess. In ABSOLUTE ms:

```text
d(mean_tpot)/d(mean_tdr)|_dist = −(e_tdr·SLO2)/(e_tpot·SLO1)
```

- `mean_tpot` — pooled gap mean
- `mean_tdr` — mean wait
- `e_tdr` — relative TDR excess
- `e_tpot` — relative TPOT excess
- `SLO1` — TDR target
- `SLO2` — TPOT target

[DERIVED].

Verified on live operating points: adv_degenerate V_tpot/V_tdr = 9.358/0.7051 =
13.27 = e_tpot·SLO1/(e_tdr·SLO2) = 1.46·50/(0.55·10) ✓; m17b
0.0250/0.0030 = 8.33 ≈ 71.044·50/(42.555·10) = 8.35 ✓; living_cliff_infeasible
4.21 ≈ 199.93/47.52 = 4.21 ✓ [EXPERIMENTALLY SHOWN].
(b) **Axis independence at the boundary**: if e_tpot=0 then dist = e_tdr
exactly (tpot below SLO2 is a FREE budget; growing it toward SLO2 costs
nothing) and vice versa. Live confirmations in the Exp D table: m3
e_tdr=1.603/dist=1.603; cf19_prefillsteep 4.606/4.606; m15 3.707/3.707;
conversely m5_order e_tdr=0, dist=e_tpot=1.540; tpot_sensitive dist=e_tpot.
(c) **SLO normalization**: 1 ms of mean lateness costs 1/SLO1 in e_tdr (only
above SLO1) — large SLO1 = cheap lateness (cf9_shape SLO1=553: 1 ms = 0.0018
relative; tdr_sensitive SLO1=40: 0.025 relative — 14× pricier per ms) [DERIVED
from the 95-case table].
(d) **dist_base scale**: gradient magnitude = 1000·w_c/dist_base. m5_order
dist_base=3 (dist 1.54 already eats half the budget — steep); m17
dist_base=1200 (flat — cheap violations); dist_base=0 → step function (10 of 95
local cases): any dist>0 zeroes c_w regardless of how small.
(e) **Clamp regions have ZERO gradient**: dist=0 (both SLOs met) or
dist≥dist_base → all waiting prices vanish; also per-axis when an excess is 0
below its SLO. Inside dist_base: prices (interactor.py:429-446 `_marginal_values`,
already judge-side) [FACT-code].

---

## 5. Per-case regime table (95 local cases, build/submission, 2026-08-23 tree)

Command per case: `python3 tools/interactor.py --case <c> --solver
build/submission --trace-file <tr>`; full table in
`experiments/sa04/expD.json` (recompute with `expD.py`). Regime census:
**40 SLO-met (c_w=1), 27 saturated-BAD (c_w=0), 18 linear, 10 binary-cliff.**

Representative rows (tdr/tpot in ms; e_* relative; regime → live prices in
pts per ms of the POOL mean):

| case                                                        | R        | tdr    | SLO1  | e_tdr | tpot   | SLO2 | e_tpot | dist  | dbase | c_w   | w_c  | regime       | V_tdr  | V_tpot |             | V_el |     |
| ----------------------------------------------------------- | -------- | ------ | ----- | ----- | ------ | ---- | ------ | ----- | ----- | ----- | ---- | ------------ | ------ | ------ | ----------- | ---- | --- |
| adv/adv_stuckbait                                           | 3        | 17.0   | 30    | 0     | 9.0    | 5    | 0.80   | 0.80  | 5     | 0.84  | 1.0  | linear       | 0      | 40.00  | ~0          |      |     |
| adv/adv_degenerate                                          | 10       | 77.5   | 50    | 0.55  | 24.6   | 10   | 1.46   | 1.56  | 5     | 0.69  | 0.5  | linear       | 0.705  | 9.358  | 0.0004      |      |     |
| mim/living_cliff_infeasible                                 | 160      | 206.1  | 199.9 | 0.031 | 49.0   | 47.5 | 0.031  | 0.044 | 10    | 0.996 | 0.5  | linear       | 0.177  | 0.744  | 0.0525      |      |     |
| mim/m5_order                                                | 1200     | 1173.1 | 2000  | 0     | 127.0  | 50   | 1.54   | 1.54  | 3     | 0.49  | 0.1  | linear       | 0      | 0.667  | 0.0243      |      |     |
| mim/cf10_shape                                              | 1800     | 2794.9 | 1262  | 1.22  | 197.4  | 8.8  | 21.4   | 21.5  | 389   | 0.945 | 0.65 | linear       | 0.0001 | 0.190  | ~0          |      |     |
| mim/m17 / m17b                                              | 300      | 2177.8 | 50    | 42.6  | 715.8  | 10   | 70.6   | 82.4  | 1200  | 0.93  | 0.35 | linear       | 0.003  | 0.025  | ~0          |      |     |
| mim/m17d                                                    | 200      | 9605.6 | 1e9   | 0     | 7118.2 | 100  | 70.2   | 70.2  | 76    | 0.08  | 0.35 | linear       | 0      | 0.046  | ~0          |      |     |
| mim/m3 / m3b                                                | 400      | 1301.8 | 500   | 1.60  | 19.3   | 20   | 0      | 1.60  | 50    | 0.97  | 0.7  | linear       | 0.028  | 0      | 0.018/0.000 |      |     |
| zoo/cf19_prefillsteep                                       | 1500     | 2803.2 | 500   | 4.61  | 12.1   | 20   | 0      | 4.61  | 50    | 0.91  | 0.7  | linear       | 0.028  | 0      | 0.0002      |      |     |
| mim/m15 (Lout=1)                                            | 200      | 9414.8 | 2000  | 3.71  | 0      | 1e9  | 0      | 3.71  | 34    | 0.89  | 0.55 | linear       | 0.008  | 0      | ~0          |      |     |
| mim/cf9_shape (Lout=1)                                      | 2000     | 6485.9 | 553   | 10.73 | 0      | 1e6  | 0      | 10.73 | 34    | 0.68  | 0.76 | linear       | 0.041  | 0      | ~0          |      |     |
| mim/m19                                                     | 500      | 1752.5 | 200   | 7.76  | 22.2   | 100  | 0      | 7.76  | 120   | 0.94  | 0.1  | linear       | 0.004  | 0      | ~0          |      |     |
| mim/m9/m9b/m9c                                              | 800-1500 | 10765+ | 50    | 214+  | 0      | 10   | 0      | 214+  | 117   | 0     | 0.95 | sat-BAD      | 0      | 0      | 0.5 (tp)    |      |     |
| mim/m_lat                                                   | 200      | 5080.5 | 1e9   | 0     | 800    | 300  | 1.67   | 1.67  | 0.5   | 0     | 0.9  | sat-BAD      | 0      | 0      | —           |      |     |
| zoo/transfer_bottleneck                                     | 10       | 208339 | 500   | 416   | 80250  | 100  | 801    | 903   | 0.1   | 0     | 0.5  | sat-BAD      | 0      | 0      | —           |      |     |
| adv/adv_cliff                                               | 200      | 601    | 5     | 119   | 62.7   | 10   | 5.3    | 119   | 0     | 0     | 0.5  | binary       | 0 a.e. | 0 a.e. | —           |      |     |
| zoo/example1                                                | 1        | 30     | 30    | 0     | 0      | 15   | 0      | 0     | 0     | 1     | 0.5  | binary (met) | 0      | 0      | —           |      |     |
| 40 SLO-met cases (zoo majority, adv_x* absurd-SLO extremes) |          |        |       | 0     |        |      | 0      | 0     |       | 1     |      | sat-GOOD     | 0      | 0      | V_tp only   |      |     |

Notes:

- **Zero-gap cases** (all-requests Lout=1: prefill_steep_lout1, cf9_shape, m15,
  lout1_*): G=0 → tpot ≡ 0 by definition → waiting component is PURE tdr; the
  entire decode stage of these tests is invisible to c_w.

- In 14 of 18 linear cases exactly one waiting axis is priced (the other excess
  is 0) — free-budget boundary operation.

- Saturated-BAD ≠ hopeless: the LUMP recovery (reach dist<dist_base) is
  1000·w_c pts (e.g. m9: 950 pts) — but the marginal at the current point is 0;
  progress pays only once the pool mean crosses under SLO1·(1+dist_base).

### tp-vs-waiting joint gradient (question 4)

```text
dScore/d(elapsed 1 ms) = −1000·w_tp·tp/((tp_UB−tp_base)·elapsed)
```

- `w_tp` — throughput weight
- `tp` — output rate
- `tp_UB` — high rate
- `tp_base` — reference rate
- `elapsed` — makespan `E`

The makespan price. Across the 18 linear cases `|V_elapsed|` ranges 0…0.0525 pts/ms
(max living_cliff_infeasible); in 12 of 18 it is < 0.001 pts/ms, i.e. **the
makespan is nearly free to spend while waiting points are live**. Exceptions:
m3 (0.018) and m5_order (0.0243) — tp-window-constrained with meaningful
makespan prices. Recoverable-points split for the top linear cases:
adv_degenerate rem_waiting 156 vs rem_tp 500; m17d 323 vs 650; m5_order 51 vs
169; cf9_shape 241 vs 0; cf10_shape 36 vs 303.

**Is the solver optimizing an unmeasured quantity?** At these operating
points the risk is not optimizing an imaginary term but MISPRICING real ones:
(i) in e_tpot=0 cases (m3/m15/cf9/prefill family — the TDR-recovery class),
ANY decode-cadence or span effort below SLO2 earns exactly 0 — only P POST
timing matters for c_w; (ii) in the 40 SLO-met cases all waiting effort past
dist=0 is waste; (iii) in the 27 saturated-BAD cases the waiting axis is dead
at the margin (lump-recovery only). The genuinely contested cases are the 18
linear ones, and there the current attack direction (interactor `attack=`
field, D-070) matches the dominant priced axis on each.

---

## 6. Fallacy-candidate mechanisms (docs audit — recorded for the completed program; program completed 2026-08-23)

1. **docs/DECISIONS.md:104 (D-008 mechanism note)**: "with dist_base=0 any
   single request crossing SLO1/SLO2 flips C_w" — **WRONG SEMANTICS**: dist is
   computed from the POOL MEANS; a single request crossing its SLO does nothing
   unless the mean crosses (shown §3: 1 request = 1/R weight). The −500
   collapses D-008 traced were mean-crossings, not per-request events. Any
   reasoning built on per-request cliff sensitivity is suspect.

2. **docs/DECISIONS.md:533 (D-023 triage)**: "TPOT => smooth decode for
   high-Lout only" — "smooth" is an unmeasured quantity: only per-request
   SPANS and the pooled mean exist (§2). Legitimate restatement: "minimize
   Σ spans of Lout>1 requests" (which favors high-Lout because they carry more
   gaps). Check whether any admission/holding logic regularizes gaps rather
   than shrinking spans.

3. **docs/ASSUMPTIONS.md:255 (§1.8)**: "TPOT: Mean gap between consecutive tokens for a
   request" — per-request phrasing, corrected at docs/ASSUMPTIONS.md:268; SA01 flagged;
   quantified here (Exp A2: pooled 5.003 vs per-request-mean 17.003).

4. **docs/DECISIONS.md D-021 (via SA01)**: "POOLED mean of per-request mean
   gaps, weighted by iter" — iter=Lout weighting would weight by Lout, correct
   is Lout−1 (gap count). Code is correct; doc wording hazardous.

5. **D-070 marginal values at dist_base=0** (DECISIONS.md:2507): "V_tpot =
   w_c/SLO2 when dist_base<=0" — the binary cliff has zero a.e. gradient; this
   is a reachability heuristic (points-if-dist-hits-0), not a gradient. Safe
   as steering, wrong as geometry near the step.

6. **Already-rejected items stay rejected** (docs/DECISIONS.md): Theorem-5
   λ_TDR "always urgent when λ>0" — under mean semantics urgency has no
   per-request standing beyond queueing order effects; Theorem-1 min-EFT
   choose() (this_lout=max(Lin,ewma_lout)) — orders of magnitude wrong.
   The live D-147 "TDR Urgency" (hold_dpre `tdr_critical` =
   excess_tdr>0.15·dist_base) is pool-mean-based and thus semantically sound.

7. **src grep (list-only)**: no live "cadence/smoosh" mechanism named in src/
   (only PSGO_F=1000 dump cadence, dev-only). Whether any logic EMBODIES
   cadence-shaping (e.g., admission spacing that regularizes gaps) is for the
   src-audit subagent; the two candidates to inspect are D PRE admission
   gates (max_inflight/hold_dpre) and D POST group assembly ordering.

---

## 7. Strategic implications

1. **Decode objective per request = minimize span (e_last − e_first), not
   cadence.** Front-load or back-load tokens freely inside the span; the only
   reason to spread rounds evenly is resource contention (B2.4), never the
   score.

2. **The pre-first-token window is a free parking lot.** Delaying a request's
   first decode to join a bigger batch, wait for link idle, or coalesce D PRE
   work costs 0 tdr, 0 tpot — and 0 tp unless that request's LAST token
   becomes the global makespan or its delayed rounds contend (§1 B2.3/B2.4).
   For requests that will finish well before the tail (most of a large pool),
   X is effectively unbounded; for the makespan-critical request, break-even:

```text
X* = V_waiting_gain/|V_elapsed|
```

- `X*` — break-even parking ms
- `V_waiting_gain` — waiting points per mean-ms
- `V_elapsed` — makespan price per ms

measured 14 ms (living_cliff) to >10⁶ ms
   (m17d) per mean-ms of tpot gain (§5 table). In 16 of 18 linear cases X* >
   2,000 ms: **first-decode admission delays are almost never priced**.

3. **Sacrifice the few, speed the herd.** Pool means make per-request damage
   cheap (1/R, 1/G) and lump gains on the mean expensive; concentrating
   unavoidable delay on few requests strictly dominates spreading (CA vs CB:
   +4.19 pts at identical c_w; generalizes: also concentrate span damage on
   low-gap (small Lout−1) requests — a Lout=1 request can absorb UNLIMITED
   decode delay at zero waiting cost, its decode leg is tp-only).

4. **Lout=1 requests are second-class waiting citizens** (0 gaps): their
   entire post-P POST pipeline is invisible to c_w. Conversely high-Lout
   requests carry G-weight: 1 ms of span on a Lout=100 request = 99 gap-ms.
   Span damage should be routed to low-Lout requests; span savings harvested
   on high-Lout ones.

5. **Work the priced axis only.** In the 14/18 single-priced linear cases,
   effort on the unpriced axis (tpot below SLO2, tdr below SLO1) is literally
   worth 0; in SLO-met cases only tp pays; in saturated-BAD cases nothing pays
   until the pool mean gets under the cliff (SLO1·(1+dist_base)) — a
   lump-recovery calculation, not marginalism.

6. **Relative-relief balancing**: per mean-ms, tdr relief pays
   e_tdr/(dist·SLO1), tpot relief e_tpot/(dist·SLO2) — push the axis with the
   larger e/SLO ratio first; at a convertibility rate r ms-of-tpot per
   ms-of-tdr, the optimum sits where r = e_tdr·SLO2/(e_tpot·SLO1).

## 8. Open questions recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026)

1. Whether the D PRE admission machinery (max_inflight/eff_cap/hold_dpre)
   implicitly priced first-decode delays that the score priced at zero — and
   whether relaxing them exclusively in the dead zone could have bought batches for free on
   the linear-regime cases. (Recorded candidate follow-up at close: src audit + A/B on m17/cf10.)

2. On CF (unknown params at the time), which regime classes dominated? The local census
   (42% SLO-met / 28% sat-BAD / 19% linear / 11% binary) was a mimic-based
   prior, not the CF distribution; the D-142..D-144 CF deltas suggested #5/#6
   (tp-window-squeezed, starve class) and #15 (m15-class) were live linear
   cases on CF too.

3. The B2.4 contention caveat: the remaining task at close was to quantify the link-FIFO externality of parking
   delay in the dead zone (when a parked round blocked in-flight rounds) — recorded as needing a queueing model of the shared UP/DOWN FIFOs (SA03-adjacent).

4. Lump-recovery feasibility for saturated-BAD big-R cases (m9: need mean tdr
   10765→<5,850 = SLO1(1+117)): the question at close was whether the m→∞ saturation bound (AGENTS D-144)
   was consistent with e_tdr=214>117 — i.e., structurally unreachable. If so,
   those cases were pure-tp, and every waiting-directed heuristic
   firing on them was waste.

5. tp_base anchoring interacted with the dead zone: since tp used earliest
   ARRIVAL→last token, first-decode parking on the makespan-critical request
   was the ONLY placement with tp cost — recorded as worth an explicit "critical request"
   notion in D PRE admission (absent from the documented levers at program close).
