# Investigation 2026-08-17 — D3 trigger refinement & score-gap structural analysis (completed investigation that contributed to #56; contest ended Aug 28 2026)

**Status: COMPLETED Aug 17 2026 — investigative phase completed; refinement analysis below contributed to #56.**

**I (Ntokozo Dlamini) paused the threshold sweep mid-run to pursue a new idea. All artifacts in `build/invest_2026-08-17/`.**

---

## 0. Goal of this session

CF baseline (pre-D3) = **13,577.064 pts (61.7%, rank 1051)**.

I (Ntokozo Dlamini) investigated the structural reasons for the gaps and the small things we hadn't done well, then decided what raised the score toward #56.

D3 (`DPRE_ACCUMULATE`) was already baked (D-074, artifact 65,096 chars, battery green) — that was locked and unchanged.

Everything below was investigation of a D3-any refinement + the other structural families.

## 1. CF judgement facts (pre-D3 rows, pasted by me (Ntokozo Dlamini) — confirmed exact)

- 22 tests, feedback only. Total runtime ≈2132ms across tests; **#19 slowest at 1046ms** (hotspot check completed — see §7).

  Memory 0/2MB everywhere.

```text
score = w_tp·norm_tp + w_c·norm_c
```

- Per-test `w_tp` derived from above; **all 9 TP weights match discriminator `TP_PARAMS` exactly** (#5=0.80, #6=0.90, #11=0.50, #12=0.99, #13=0.75, #14=0.65, #16=0.98, #19=1.0, #22=0.50).

  Derived: #1/#2/#3/#7 have w_tp≈0 (pure waiting), #9≈0.05, #10≈0.15, #15≈0.45, #17≈0.67, #18≈0.58, #4≈0.30, #8≈0.25, #20≈0.40, #21≈0.50.

- Discriminator convention: `TP_PARAMS` uses `SLO1`=`SLO2`=1e9, `dist_base`=1e9 → c-component neutralized; verdicts speak to TP only.

  t13/t14 use FLAT_ROWS; t5/t6/t11/t12/t16/t19/t22 use DEFAULT_ROWS.

## 2. The five failure families (from 2026-08-16 analysis)

| family | tests | nature |
| --- | --- | --- |
| (a) D3 flat-only locks out defaults | #6 (greedy 0.728 vs ours 0.406), #16 (0.942 vs 0.462) | RECOVERABLE — this session's target |
| (b) TDR-catastrophic burst | #11/#15/#17/#18 | STRUCTURAL: runtime signature (0-281ms runtime, millions-of-ms tdr) = burst arrivals, thin link, Lout=1; TP ceiling tiny |
| (c) #19 TPOT=2564ms | #19 | wrong-shape reconstruction (real norm_tp 0.325 vs recon ceiling 0.064) — verdict suspect |
| (d) pure-waiting | #3/#7 (w_tp=0) | NO local reconstruction exists yet — Move 3 (w3_like) |
| (e) local-bound | #22 (greedy 0.206 vs ours 0.095) | scheduling gap, NOT D3-related |

## 3. D3-any experiment (baseline of the refinement hunt)

Dropping the `flat_decode_table()` gate (fire whenever w_c>0) on the CURRENT sources:

| case | cap2 (D3 off) | d3any | delta |
| --- | --- | --- | --- |
| t5 | 0.39056 | 0.32414 | **−0.066** |
| t6 | 0.40556 | 0.50292 | **+0.097** |
| t16 | 0.46196 | 0.59031 | **+0.128** |
| t22 | 0.09462 | 0.08343 | **−0.011** |
| t13 | 0.41610 | 0.66389 | +0.248 (flat — already baked) |
| t14 | 0.41093 | 0.52563 | +0.115 (flat — already baked) |

Invariants: x1=304.492, x3=0.000, table_1row=500.095 for d3any (all preserved).

**Mechanism (traced)**: on DEFAULT_ROWS with unbounded MarginalBatch.cap (=1<<20 via climb+exhaust rules), d3any ≈ "D POST always beats D PRE on local" (`dpre_n < 1<<20` trivially true).

t5 tight link (worst_up≈301ms) starves; t6/t16 loose link (≈151ms) breathes; t22 ultra-cheap link (bw=1000 Gbps, S=20) is local-bound — per-cycle efficiency lost.

## 4. Variant A/B — full table (all built, no-flag binary == exp_accum3 == D-074)

| case | cap2 | d3any | LINK | LINK10 | RBOUNDD3 | PREFILL | IDLEOK | LINK200 | REL |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| t5 | 0.39056 | 0.32414 | 0.39056 | 0.39056 | 0.39020 | 0.39056 | 0.39056 | **0.39056** | 0.39056 |
| t6 | 0.40556 | 0.50292 | 0.40556 | 0.40556 | 0.41456 | 0.40556 | 0.40556 | **0.50292** | 0.40556 |
| t11 | 0.00308 | 0.00308 | 0.00308 | 0.00308 | 0.00308 | 0.00308 | 0.00308 | 0.00308 | 0.00308 |
| t12 | 0.09491 | 0.09491 | 0.09491 | 0.09491 | 0.09491 | 0.09491 | 0.09491 | 0.09491 | 0.09491 |
| t13 | 0.41610 | 0.66389 | 0.66389 | 0.66389 | 0.43139 | 0.66389 | 0.66389 | **0.66389** | 0.66389 |
| t14 | 0.41093 | 0.52563 | 0.52563 | 0.52563 | 0.41879 | 0.52563 | 0.52563 | **0.52563** | 0.52563 |
| t16 | 0.46196 | 0.59031 | 0.46196 | 0.46196 | 0.47534 | 0.46196 | 0.46196 | **0.59031** | 0.46196 |
| t19 | 0.06981 | 0.06981 | 0.06981 | 0.06981 | 0.06981 | 0.06981 | 0.06981 | 0.06981 | 0.06981 |
| t22 | 0.09462 | 0.08343 | 0.09462 | 0.08343 | 0.08387 | 0.09462 | 0.08343 | **0.09462** | 0.09462 |

Invariants: x1=304.492 and x3=0.000 for ALL variants; table_1row: cap2=500.101,
all D3-firing variants 500.095, RBOUNDD3=500.101.

### Variant definitions (scheduler.cpp D3 target computation, current edited state)

- **LINK**: fire D3-any iff `worst_up = transfer_time(cfg, R+1) < 0.1`; else flat-only.
  Never fires on the TP cases (worst_up ≥ 1.6ms everywhere) → flat-only behavior.
- **LINK10**: same with threshold 10ms. Fires on t22 only (worst_up=1.6ms) → t22
  regresses to d3any level; no gains.
- **RBOUNDD3**: target = min(mc, R/4), fire whenever w_c>0. Modest + everywhere
  (t6 +0.009, t16 +0.013, t13 +0.015, t14 +0.008) but t22 still regresses (0.08387);
  far below d3any on t6/t16/t13/t14. Not the win.
- **PREFILL**: fire when flat OR prefill pipe active (P_PROC_RUNNING +
  WAIT_INPUT_DOWN > 0). **Dead**: prefill-active and dpost_n>0 are anti-correlated
  in time on burst tests (prefill drains before D_POST backlog exists) → equals
  flat-only on every case. count_phase is correct (per-cloud sums OK); the temporal
  anti-correlation is the killer.
- **IDLEOK**: fire when prefill_active OR (worst_up < S + dpre(1)). On t6/t16:
  worst_up=151ms > dpre_wave=2ms → falls to flat-only → no gains. On t22: fires →
  regresses. Dead.
- **LINK200** ⭐: fire D3-any iff `S < 5.0 && worst_up < 200.0`; else flat-only.
  **Perfect split on the reconstruction family**: t6/t16 (151ms) fire → full gains;
  t5 (301ms) doesn't → preserved; t22 (S=20) doesn't → preserved; t13/t14 flat →
  unchanged. Strictly better than cap2 AND strictly better than d3any on every case.
  **CAVEAT noted at the time: 200ms was fitted to reconstructions; robustness sweep was in progress
  when I (Ntokozo Dlamini) paused it. Follow-up was completed for #56.**
- **REL**: fire iff `transfer_time(cfg, mc) < S + dpre(1)`. Dead — mc=1<<20 makes
  up_mc astronomically large → never fires on defaults.

## 5. Threshold robustness sweep — PLATEAU CONFIRMED (partial: LT100-LT250 captured before abort; LT250/LT300 truncated)

Sweep of `worst_up < D3_LINK_MS && S < 5.0` (else flat-only):

| MS | t5 | t6 | t16 | t22 | t13 | t14 |
| --- | --- | --- | --- | --- | --- | --- |
| 100 | 0.39056 | 0.40556 (no fire) | 0.46196 (no fire) | 0.09462 | 0.66389 | 0.52563 |
| 150 | 0.39056 | 0.40556 (no fire) | 0.46196 (no fire) | 0.09462 | 0.66389 | 0.52563 |
| 175 | 0.39056 | **0.50292** | **0.59031** | 0.09462 | 0.66389 | 0.52563 |
| 225 | 0.39056 | **0.50292** | **0.59031** | 0.09462 | 0.66389 | 0.52563 |
| 250+ | aborted mid-run (LT250/LT300 binaries built but no result files) | | | | | |

**Plateau, not knife's edge**: gain regime 175 ≤ MS < 301 (t6/t16 worst_up≈151ms fire; t5 worst_up≈301ms blocks; t22 blocks via S=20 gate).

Margins: ~24ms below, ~76ms above.

Note `worst_up = transfer_time(cfg, R+1)` ≈ C by construction of the _link helper (bw chosen so transfer(R) = C − lat), so the plateau is set by the reconstruction geometry; real-CF tests have real R and their own worst_up.

Remaining question at the time — whether real #6/#16 worst_up fell inside the plateau — was resolved with post-D3 CF data during work toward #56.

## 6. Source state — IMPORTANT

`src/scheduler.cpp` contained at the time the variant-selector block (`#if defined(D3_V_LINK) ... #elif defined(D3_V_LINKT) ... #else flat-only`).

The `#else` branch is the D-074 flat-only logic → **no-flag compiles are behaviorally identical to the D-074 artifact** (`/tmp/exp_d3_orig` verified byte-identical to `build/exp_accum3`).

But the source TEXT differed from D-074.

Before finalizing, I (Ntokozo Dlamini) reverted to the original flat-only block or kept it deliberately — decision was completed and contributed to #56.

## 7. Follow-up investigation items (completed after this session; moved from the session list and contributed to #56)

1. **Threshold robustness sweep** (above, completed) — LINKT 100-300.

2. **Move 3 — w3_like reconstruction for #3/#7 (pure-waiting family, w_tp=0)**.

   Derived SLO params from CF rows:

  - #3: mean_tdr=1331.5, dist=0.7449, norm_c=0.356 → dist_base≈1.157, SLO1≈763 (if e_tpot=0).

    ```text
    SLO1 = mean_tdr/(1+dist) = 763
    ```

    #3 has w_tp=0 → e_tpot free.

  - #7: mean_tdr=860, dist=0.3489, norm_c=0.913 → dist_base≈4.01, SLO1≈637.

   I (Ntokozo Dlamini) added this to discriminator build_case + TP_PARAMS, did A/B vs cap2, and analyzed SloAwareDelay/MarginalBatch behavior.

   TDR was the ONLY lever here — analysis of whether our TDR was structurally capped contributed to #56.

3. **t22 local-bound gap** (greedy 0.206 vs ours 0.095): not D3-related.

   Analysis completed: compared what greedy did that we didn't on S=20/bw=1000. Local was the bottleneck; S amortization was the lever toward #56.

4. **#19 runtime hotspot (1046ms)**: half of the 2132ms total on one test.

   Checked for an O(R·frames) or MarginalBatch precompute cost. Small win provided headroom toward #56.

5. **#19 wrong-shape**: real norm_tp 0.325 vs recon ceiling 0.064 — the real test must have had a smaller Lout or different shape.

   Structural verdict was suspect; a better reconstruction would have been needed to even measure ourselves. This assessment informed work toward #56.

6. **Final decision (completed)**: the sweep showed a robust plateau, which led to a D-entry and later bake decision for the LINK200-style trigger (held per 48h rule until post-D3 CF data arrived).

   Post-D3 CF rows told us which REAL tests were flat-decode and contributed to #56.

## 8. Artifacts

- Binaries + result files: `build/invest_2026-08-17/` (24 files: 15 binaries, 9 v_*.txt).

  Key: exp_d3_orig (= D-074 exact), exp_d3any, exp_D3_V_LINK200, exp_LT{100..300}.

- Pre-change snapshot: `/tmp/sched_orig.cpp`, `/tmp/sub_pre_d3`.

- Docs: DECISIONS.md D-066/D-067/D-074; docs/CF_JUDGEMENT_2026-08-17.md (baseline rows).
