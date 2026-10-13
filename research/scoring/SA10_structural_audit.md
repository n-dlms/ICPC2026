# SA10 — Structural Audit of the Solver: Why the Artifact Loses Winnable Points

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA10 drafting, final). READ-ONLY on src/, tools/, cases/; scratch in
`/tmp/sa10/`, scripts in `experiments/sa10/` (`trace_groups.py`, `mkvariants.py`).
Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Solver under audit: `build/submission` (2026-08-23 tree). All numbers below are from this
session's runs through `tools/interactor.py --trace-file` unless cited to a predecessor
doc (SA01–SA09 in this directory).

---

## 1. The two defeats, explained at line level

### 1.1 x9-class defeat (V-shaped table, single-cloud mega optimal): 388.6 vs 736.8

Case `cases/adv/adv_x9_above.txt`: K=8, S=1, lat=0.001, bw=1000, w_tp=1.0, **w_c=0.0**,
tp_UB=7; table dpre/dproc/dpost = 10 @m=1, **200 @m=10, 5 @m=4096** (V-shape: per-token
cost 10 → 20 → 0.0012, minimized at m=R=2000). 2000 requests, Lin=4, Lout=100, all t=0.

Artifact trace (`/tmp/sa10/tr_x9.txt`): P_PRE assigns **exactly 250 per cloud**; decode =
**1 group of 2000, then 99 rounds of a 250-cohort + 99 rounds of a 1,751-cohort**;
800 D_PROCs of exactly 250. E = 66,090.9 (score 388.636). SA09's probe (all-on-cloud-0
lockstep): E = 37,811 (score 736.8).

The per-round cycle decomposes exactly (all numbers from the trace / table
interpolation; dpre(250)=dpost(250)=188.55, dpre(1750)=116.9, dpre(2000)=105.0):

| regime                              | round anatomy                                                                                                   | ms/round    |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------- | ----------- |
| artifact (split cohorts)            | D_PRE(250)+D_PRE(1750) 188.5+116.9 (serial local) + 8∥ D_PROC(250) 188.5 + D_POST(250)+D_POST(1750) 188.5+116.9 | **615.2**   |
| + D_PRE re-merge (measured, §1.1.3) | D_PRE(2000) 106 + 8∥ D_PROC(250) 188.5 + D_POST(250)+D_POST(1750) 306.4                                         | **603.1**   |
| + unified D_POST (predicted)        | + D_POST(2000) 106                                                                                              | **401.7**   |
| + single-cloud D_PROC (probe)       | D_PRE(2000)+D_PROC(2000)+D_POST(2000) = 3×106 + transfers                                                       | **317–337** |

99 rounds × (615.2−317) ≈ 29.5k of the 28.3k E gap — the decomposition is complete.
Three independent mechanisms, each with its line and its flip predicate:

**(1) The EFT cloud balancer spreads 250/cloud — `policies.cpp:37-83`, root at :56-61.**
`PrefillLoadBalanceCloud::choose` models decode debt as:

```text
t_decode = this_lout × max(batches·dproc(m_actual), dproc(1)·active)
climb rule: if (dproc(m+1) − dproc(m) > dproc(m)/m) break
```

- `t_decode` — predicted decode debt
- `this_lout` — predicted output length
- `dproc` — decode proc time
- `m_actual` — capped batch size
- `active` — active request count

where `m_actual`
(policies.cpp:59). On the V-table the first marginal step is +21.1 > 10 = avg → the
climb breaks at **m=1** and never sees the falling arm to 5@4096. t_decode is then
perfectly linear in `active`, so the argmin EFT is an even spread (trace: 250×8).
Note the inconsistency: `MarginalBatch::precompute` computes the cap with an
**argmin over listed breakpoints** (policies.cpp:178-183) which correctly finds 4096 on
this table — `choose()` uses the naive climb instead. Spread costs 84.5 ms/round
(8∥ D_PROC(250)=188.5 vs 1× D_PROC(2000)=105).
**Predicate to flip**:

```text
argmin_{m∈listed∪{R}} dproc(m)/m ≥ pool-size
```

- `dproc` — decode proc time
- `m` — batch size
- `R` — pool size

(per-token cost
still falling at the pool scale) ∧ local-bound (L13/L14) ∧ w_tp-dominant ∧ tp-linear
→ prefer consolidation (or at least evaluate t_decode at the argmin batch size, not
m_cap=1).

**(2) The eager first-frame D_POST — `scheduler.cpp:275-284`.** After the round-1
D_PRE(2000), the 8 per-cloud D_PROCs finish together but their DOWN transfers serialize
on the shared FIFO (~1 ms each here). At the first frame after cloud 0's sub-batch
lands, `pick_local` sees `n = rl_size(READY_D_POST) = 250` and issues **D_POST(250)
immediately** (dpost(250)=188.5 instead of a 106 unified D_POST(2000) had it waited
~8 ms for the sibling sub-batches). This both taxes every round (+200 ms vs unified,
33% of the cycle) and — worse — puts cloud 0's members out of phase, which creates the
permanent 250/1751 cohort split. Verified in the x9_wc run below: even with the D_PRE
re-merged, D_POST stays {250:100, 1750:100} all run.
**Predicate to flip**: members of the *same in-flight round* still transferring
(WAIT_DEC_DOWN > 0 ⇒ an event is guaranteed — L8-safe wake) ∧ dpost marginal saving
(dpost(m)+dpost(m′)+2S vs dpost(m+m′)+S) exceeds V_tp-priced wait cost ∧ !tdr_critical.

**(3) No cohort re-merge when w_c=0 — `scheduler.cpp:174-176`.** The DPRE_ACCUMULATE
target is `target = (cfg_.w_c > 0.0) ? group_size(...) : 0` — on pure-tp tests
(the starve/race class, exactly where E is everything) **accumulation is compiled
out**. Without the `dpost_n > 0 → dpre_acc_skip` recycling rule, the fast cohort's
D_PRE fires the instant it is ready and the two cohorts never co-meet again.
**Experiment (this session)**: copy of x9 with only the scoring line changed to
w_tp=0.95/w_c=0.05 (`/tmp/sa10/x9_wc.txt`) — the ONLY behavioral difference is this
gate (alpha stays 0.95 ≥ 0.8 → NoDelay; SLOs=1e9 keep all caps unbounded; verified
group_size path unchanged) — trace: **D_PRE = 2000 for all 100 rounds**, E
66,090.9 → 64,708.4 (−2.1%), score 388.6 → 428.7 (+40.0). The gate flip alone
re-merges the cohort; the remaining gap is components (1)+(2).
**Predicate to flip**: unconditional target, but gated by the same x9-class condition
as (1) — see §5 Change A and the falsification below (the naive unconditional version
LOSES on the m19 family).

**Counter-test**: K=1 variant (`/tmp/sa10/x9_k1.txt`) scores **378.2 (worse)** — with
the cloud dimension gone, the pipeline-fill race fragments differently (~104/42
stragglers pay the V-table's rising arm, dpre(42)=198.5) and with w_c=0 nothing
re-merges. Single-cloud assignment alone is NOT the fix; cohort integrity is.

### 1.2 m5_order-class defeat (mixed-Lout pool): 779.9 vs 979.0

Case `cases/mimics/m5_order.txt`: K=8, w_tp=0.9/w_c=0.1 (alpha=0.9), SLO1=2000,
SLO2=50, tp_UB=2.6; 1200 requests, Lin ∈ {1..1024} (pproc varies 1.05→1024.8), Lout ∈
{4,8,24,64,200}, arrivals staggered over 1000 ms. Artifact: score 779.93, E = 30,862
(Trace: `/tmp/sa10/tr_m5.txt`). SA09's trivial mega-lockstep probe: E = 22,189, score
979.0 (spans explode but tp saturates; the entire +199 is E-side).

**No cap binds here.** MarginalBatch's cap on this table is unbounded
(argmin over listed dpre = 4096 → 1<<20) and the Twist-1 crossing never fires
(V_tpot = 0 at run time: tpot under SLO2 early, e_tdr = 0). group_size returns
`n_ready` everywhere. **The fragmentation is pure readiness churn**: the trace shows
647 D_PREs of sizes 1..463 (89 singletons; mid-run groups 65/104/123/135/158/170 do
form when the pool co-collsides), 1,280 S-payments, and a ragged drain tail
(66×5 then 44/18/26/18/44). Local decode occupancy is **14,332.8 ms for the artifact
vs 6,803.5 for the mega counterfactual** (2.1× table-time+S tax), the rest of the E
gap is dependency-chain bubbles (mega also waits, but pays for fewer, bigger rounds).

Three mechanisms refuse to sync the herd, with lines:

1. **NoDelay whenever alpha ≥ 0.8 — `Solution.cpp:189-190`.** m5_order's alpha=0.9
   selects `NoDelay`: there is no hold policy at all on any w_tp-dominant test.

2. **`SloAwareDelay::hold_dpre` is first-round-only — `policies.cpp:287`**:
   `if (s.multi_token_dpre_count() > 0) return false;` — once ANY member has emitted a
   token, holds are structurally impossible. This is the D-116-era protection against
   decode-batch destruction, but it also forbids mid-run round synchronization.

3. **DPRE_ACCUMULATE's skip set is too narrow — `scheduler.cpp:170-190`**: the only
   wait conditions are "a D_POST is pending" (recycling) and "first-pass pipeline
   fill". There is no "wait for in-flight round members (WAIT_DEC_UP/D_PROC/
   WAIT_DEC_DOWN) to rejoin" condition — the exact wait that makes mega win. Every
   mid-run moment with local free, D_POST queue empty, and 1–2 members ready fires a
   singleton D_PRE at S+dpre(1)=1.8 ms/token.
   **Counter-test (this session)**: m5_order with w_c=0 (`/tmp/sa10/m5_wc0.txt`)
   removes even the recycling hold: E 30,862 → 30,936 (neutral; D_PRE count 647→1,049,
   singletons 89→194 — it fragments more but each fragment races sooner; net zero).
   So the recycling valve is E-neutral on mixed pools: it neither syncs nor
   systematically hurts — the +200 mega win needs a true herd-sync hold that no
   current mechanism provides.

**Predicate a fix needs**: hold the D_PRE head while in-flight decode members exist
(event-backed wake by L8: any WAIT_DEC_* transfer/task will frame) ∧ expected
`(S+dpre(m))+ (S+dpre(m')) − (S+dpre(m+m'))` saving > V_tp·(wait × tp/E) + V_tpot·
span-cost + V_tdr·tdr-cost (the Theorem-6 functional, policies.cpp:311-359, already
exists) ∧ gate on w_tp-dominant ∧ tp-linear ∧ table sub-linear at the merged size.
Both defeat mechanisms FAIL adv_living_cliff ungated (SA09 §3.3) — the gates above are
mandatory, not optional.

---

## 2. Full mechanism census

Verdicts: **ALIGNED** = steers a priced quantity with sound physics; **SUSPECT** =
sound channel, wrong/miskeyed condition; **DEAD** = steers nothing on some or all
regimes; **OVERFIT-RISK** = prelim-tuned magic constant on a real channel. L1–L14 =
SA07 physics card.

| #   | mechanism                                                                 | file:line                               | steers (E/T/S/none)   | physics                                        | verdict                                                                                                                       |     |                       |
| --- | ------------------------------------------------------------------------- | --------------------------------------- | --------------------- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | --- | --------------------- |
| 1   | compute_alpha dist_base=0 min-TDR/TPOT unattainable → alpha=1             | Solution.cpp:81-98                      | regime                | L12                                            | ALIGNED                                                                                                                       |     |                       |
| 2   | worst-case Rmax=2000 serialized-monster SLO check → alpha=1               | Solution.cpp:111-127                    | regime (waiting→free) | L12                                            | ALIGNED (constants are spec bounds)                                                                                           |     |                       |
| 3   | alpha = w_tp/(w_tp+w_c)                                                   | Solution.cpp:128-134                    | regime                | L6/L12                                         | ALIGNED                                                                                                                       |     |                       |
| 4   | AdaptiveChunk gate S<1.0 && layers>8                                      | Solution.cpp:180-182                    | T/E                   | L4                                             | ALIGNED (D-150 falsified always-on)                                                                                           |     |                       |
| 5   | **NoDelay iff alpha ≥ 0.8**                                               | Solution.cpp:189-190                    | none (disables holds) | L4/L9                                          | **SUSPECT — m5_order culprit (1); magic 0.8; blocks herd-sync exactly on tp-dominant tests**                                  |     |                       |
| 6   | combine_dpre = alpha > 0.2                                                | Solution.cpp:193                        | E                     | L13                                            | ALIGNED effect, OVERFIT-RISK constant                                                                                         |     |                       |
| 7   | xremote_cap = 1+⌊alpha·(K−1)⌋                                             | Solution.cpp:194                        | E                     | L13                                            | OVERFIT-RISK (only used in ladder branch 1.0≤ratio<5)                                                                         |     |                       |
| 8   | traffic-aware cliff flip (dist_base=0, all-arrivals infeasible → NoDelay) | Solution.cpp:255-280                    | T                     | L12 step                                       | ALIGNED (D-050/D-115)                                                                                                         |     |                       |
| 9   | MAX_FRAMES=10M emergency guard                                            | Solution.cpp:244-252                    | safety                | L8                                             | ALIGNED                                                                                                                       |     |                       |
| 10  | cheap-link max_inflight=100 iff worst_up_lin<1.0                          | scheduler.cpp:93-95                     | E/T (UP valve)        | L13                                            | ALIGNED effect (D-057), magic 100                                                                                             |     |                       |
| 11  | base_inflight = max(2,K)                                                  | scheduler.cpp:94                        | E/T                   | L13                                            | OVERFIT-RISK                                                                                                                  |     |                       |
| 12  | dist_headroom = excess_dist < 0.5·dist_base                               | scheduler.cpp:96-97                     | E/T                   | L12 (true dist)                                | OVERFIT-RISK (0.5; #12's documented fragility: dist 3.19 vs 0.5·4.49)                                                         |     |                       |
| 13  | eff_cap tighten/widen split (!headroom && w_c>0)                          | scheduler.cpp:98-104                    | E/T                   | L13                                            | OVERFIT-RISK (AGENTS describes a w_tp≥w_c variant not present in this tree — stale doc)                                       |     |                       |
| 14  | up_full → P_PRE throttle                                                  | scheduler.cpp:104,135-137               | E/T                   | L9 queue-valve                                 | ALIGNED (D-142/D-144 evidence)                                                                                                |     |                       |
| 15  | tdr_urgent = mv_tdr>0 (score-delta urgency)                               | scheduler.cpp:116-120                   | T                     | L5/L12 pool-mean                               | ALIGNED (D-151)                                                                                                               |     |                       |
| 16  | run_ppost: q_j < a_i && (!cloud_starved \                                 | \                                       | link_busy)            | scheduler.cpp:121-133                          | T+E                                                                                                                           | L5  | ALIGNED (D-140 +74.5) |
| 17  | D_PRE UP gate (up_inflight_pre < eff)                                     | scheduler.cpp:193-195                   | E/T                   | L13                                            | OVERFIT-RISK constants, ALIGNED channel                                                                                       |     |                       |
| 18  | hc = cloud with most READY_D_PRE; FIFO in bucket                          | scheduler.cpp:161-166                   | E (fill)              | neutral; **no L11 value key**                  | ALIGNED (but see §3/§4.4)                                                                                                     |     |                       |
| 19  | **DPRE_ACCUMULATE target keyed on w_c>0**                                 | scheduler.cpp:174-176                   | E (accumulation)      | L4/L14                                         | **SUSPECT/DEAD — x9 culprit (3): accumulation compiled out on every w_tp-dominant test**                                      |     |                       |
| 20  | dpost_n>0 → recycling skip                                                | scheduler.cpp:178-179                   | E                     | partial herd-sync                              | SUSPECT (accidental re-merge on x9 +40; E-neutral m5; naive unconditional version LOSES m19b +5.2%, x4 +20.6% — this session) |     |                       |
| 21  | first-pass fill skip (iter=0 && pipe≥target)                              | scheduler.cpp:180-189                   | E                     | L4                                             | ALIGNED but first-round-only                                                                                                  |     |                       |
| 22  | dynamic cross-cloud cap ladder xfer_ratio 0.05/0.3/1.0/5.0                | scheduler.cpp:210-226                   | E/S                   | L13                                            | OVERFIT-RISK (magic ratios; NOT the x9 spreader — ratio 1e-4 → cap=K permits the 2000-group)                                  |     |                       |
| 23  | link-saturation override (in_flight>2 && head>t+2·xfer₁ → cap=1)          | scheduler.cpp:228-231                   | E                     | L13                                            | OVERFIT-RISK                                                                                                                  |     |                       |
| 24  | FIRST-fill-from-home-cloud, then most-ready others                        | scheduler.cpp:232-259                   | E                     | header amortization                            | ALIGNED                                                                                                                       |     |                       |
| 25  | **eager first-frame D_POST assembly**                                     | scheduler.cpp:275-284                   | S/E                   | L14                                            | **SUSPECT — x9 culprit (2): grabs first-landed sub-batch; no wait-for-siblings**                                              |     |                       |
| 26  | pick_cloud SPT-by-Lin (Smith)                                             | scheduler.cpp:299-315                   | T                     | L5 Smith-optimal                               | ALIGNED; straggler-generator under Lin↔Lout correlation (SA06 §3.2); **only unbounded per-frame loop (O(queue))**             |     |                       |
| 27  | D_PROC group_size per cloud                                               | scheduler.cpp:329-337                   | E                     | L13                                            | ALIGNED                                                                                                                       |     |                       |
| 28  | Straggler balancer Tier-1 Lin≥256 isolation                               | policies.cpp:20-31                      | T/E                   | L5                                             | OVERFIT-RISK (Lin-scale magic)                                                                                                |     |                       |
| 29  | this_lout = max(Lin, predict_lout) in EFT                                 | policies.cpp:43-44                      | T/E proxy             | L11                                            | ALIGNED (D-151-vetted form)                                                                                                   |     |                       |
| 30  | **m_cap climb rule in EFT (breaks at m=1 on V-tables)**                   | policies.cpp:56-61                      | E estimate            | **contradicts L13/L14 on non-monotone tables** | **SUSPECT — x9 culprit (1): inconsistent with precompute's argmin (:178-183)**                                                |     |                       |
| 31  | T9 alignment floor max(batches·dproc(m), dproc(1)·active)                 | policies.cpp:67-68                      | E estimate            | L13                                            | ALIGNED (D-124)                                                                                                               |     |                       |
| 32  | PoTC near-tie power-of-two (1.05 / −0.5 active)                           | policies.cpp:73-81                      | E estimate            | robustness                                     | OVERFIT-RISK (mild)                                                                                                           |     |                       |
| 33  | AdaptiveChunk k≤2, k_opt=ceil(full/local_avg)                             | policies.cpp:129-152                    | T/E                   | L4                                             | ALIGNED (gated), magic 2                                                                                                      |     |                       |
| 34  | precompute climb rule marg ≤ avg                                          | policies.cpp:170-177                    | E                     | L13                                            | ALIGNED (D-057)                                                                                                               |     |                       |
| 35  | precompute argmin over listed breakpoints                                 | policies.cpp:178-183                    | E                     | L13                                            | ALIGNED — and the model choose() should reuse                                                                                 |     |                       |
| 36  | cap ≥ max_table_m → unbounded (1<<20)                                     | policies.cpp:184-186                    | E                     | L13                                            | ALIGNED (D-051)                                                                                                               |     |                       |
| 37  | **low_lout_regime ewma<2.5 → SLO1·0.8 threshold**                         | policies.cpp:215-216                    | T/S proxy             | **mismatches G=Σ(Lout−1)=0 (SA06 §4.3)**       | SUSPECT (bimodal pools misclassified; magic 2.5/0.8)                                                                          |     |                       |
| 38  | max_m_slo binary search cost(m) ≤ slo_thresh                              | policies.cpp:202-233                    | S                     | L12                                            | ALIGNED                                                                                                                       |     |                       |
| 39  | Twist-1 crossing Δtp·V_tp ≤ Δtpot·V_tpot + tdr_loss                       | policies.cpp:239-274                    | S vs E/T              | L7/L12                                         | ALIGNED (D-119/D-122); V_tpot=1000·w_c binary-cliff fallback (:248-249) is reachability steering, not geometry (SA04 §6.5)    |     |                       |
| 40  | **hold_dpre multi_token kill**                                            | policies.cpp:287                        | none (disables holds) | L4                                             | **SUSPECT — m5 culprit (2): forbids herd-sync after round 1**                                                                 |     |                       |
| 41  | tdr_critical excess_tdr > 0.15·dist_base                                  | policies.cpp:290-292                    | T                     | L12                                            | OVERFIT-RISK (magic 0.15)                                                                                                     |     |                       |
| 42  | batch-ready override ready_now ≥ 2                                        | policies.cpp:294-296                    | E                     | L4                                             | ALIGNED-ish (magic 2)                                                                                                         |     |                       |
| 43  | binary-cliff imminent-P_POST pairing (ready=0 ∧ imminent=1)               | policies.cpp:298-310                    | T                     | L12 step                                       | ALIGNED (D-115)                                                                                                               |     |                       |
| 44  | Theorem-6 marginal hold gain > loss                                       | policies.cpp:311-359                    | S vs E/T              | L4/L7/L12                                      | ALIGNED (exact oracle marginals; `V_tpot≤ε ∧ V_tdr≤ε → hold` at :345 is dead-zone-consistent)                                 |     |                       |
| 45  | ppre_pop SJF-by-Lin buckets                                               | state.cpp:458-476                       | T                     | L5 Smith                                       | ALIGNED; straggler side-channel                                                                                               |     |                       |
| 46  | predict_lout per-Lin-bucket mean + ewma(0.05) prior 10                    | state.cpp:284,603-613; state.hpp:353    | T/E proxy             | L11                                            | ALIGNED-ish; **iter-censoring bound max(μ, iter+1) unused — free signal dead** (SA06 §4.3)                                    |     |                       |
| 47  | D_POST optimistic re-push, FIN cancels                                    | state.cpp:190-217                       | E                     | L8/L10                                         | ALIGNED (work-conserving, event-safe)                                                                                         |     |                       |
| 48  | up_inflight_pre issue-time projection                                     | state.cpp:363-377                       | E/T valve             | L13                                            | ALIGNED                                                                                                                       |     |                       |
| 49  | dynamic_max_batch_ / PSGO engine                                          | scheduler.cpp:32-57                     | —                     | —                                              | DEAD in artifact (PSGO_STATIC, deliberate D-049/D-062)                                                                        |     |                       |
| 50  | alpha_ knob                                                               | policies.cpp:190                        | none                  | —                                              | DEAD (comment: "Alpha is dead")                                                                                               |     |                       |
| 51  | Oracle marginal_tp/tdr/tpot_v, excess, projections                        | state.hpp:227-306                       | E/T/S                 | L12 judge-exact                                | ALIGNED (the scorecard's ground truth)                                                                                        |     |                       |
| 52  | flat_decode_table_ flag / OD-9                                            | state.hpp:224-225, Solution.cpp:219-227 | —                     | —                                              | DEAD everywhere (setter only inside `#ifndef PSGO_STATIC`; getter `flat_decode_table()` has zero callers in src/)             |     |                       |

## 3. Dead / unsupported mechanisms

- **Cadence / gap-smoothness**: absent (grep + SA04 Exp A byte-identical scores). The
  D-023 "smooth decode" idea never became code. Clean.

- **Per-request SLO counting / fairness**: all live urgency keys are pool-mean
  marginals (L5); D-151 removed the last per-request threshold. Clean.

- **Beyond-saturation effort**: (a) SloAwareDelay's Theorem-6 machinery is inert on the
  27 sat-BAD + 40 SLO-met cases by construction (V=0 gates), and is switched OFF
  entirely (NoDelay) whenever alpha ≥ 0.8 — so no wasted effort, but also no span
  compression where e_tpot > 0 IS priced with alpha < 0.8 (SA07 #3: adv_degenerate
  −9.36 pts/ms). (b) `low_lout_regime` burns SLO2 protection on ewma misreads. (c)
  OD-9/flat_decode_table (#52) is dead code in the artifact. (d) alpha_ (#50) dead.

- **The recycling valve (#20) steers an unintended quantity on mixed pools**: it is a
  local-machine arbitration rule (alternate D_POST/D_PRE) that accidentally re-merges
  uniform pools (x9) and does nothing on churning pools (m5 E-neutral). Its CF effect
  on the #19 family is measurable and negative when made unconditional (§1.1).

## 4. Missing mechanisms — the blocking-physics list and insertion map

| missing mechanism (source)                                                                                          | partial code today                                                                            | minimal insertion point                                                                                                                                                                                                  | gate predicate (keeps adv_living_cliff + 68-battery safe)                                                                                                                                                                               |
| ------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **M1 wave pipelining / phase-offset cohorts** (SA07 #1, SA08 AN-1/AN-2; m19b 2.28×/27% local, m19c 2.36×, m5 3.93×) | none — the solver is strictly single-wave lockstep per cohort                                 | `pick_local` D_PRE assembly (scheduler.cpp:192-269): partition READY_D_PRE into W=2 fixed sub-cohorts at first-token epoch; issue head cohort only (issue-order stagger, never idle-park — Botsali no-idling, SA08 §5.2) | table sub-linear at m/W (precompute marginal), local-bound (L14), tp-linear, W×(D_PRE+D_POST) ≤ cohort round budget; **event-backed wake proof per SA08 AN-4 / D-140-P0: every skipped issue must leave an in-flight task or transfer** |
| **M2 drain-tail / mid-run de-fragmentation (herd sync)** (SA06 §6.3, SA09 §3.1; m5 +199)                            | dpre_acc_skip recycling (scheduler.cpp:178-179) — first-round + D_POST-pending only           | extend the `dpre_acc_skip` condition block: skip while (in-flight decode members exist) ∧ marginal test of Theorem-6 form                                                                                                | in-flight WAIT_DEC_* > 0 (event-guaranteed wake), Δtable+ΔS saving > V_tp·wait-cost + V_tpot·span + V_tdr·tdr, w_tp-dominant ∧ tp-linear; relax #40's kill-switch only under this compound gate                                         |
| **M3 dead-zone parking depth for span compression** (SA07 #3; adv_degenerate +55@25%)                               | Theorem-6 hold (policies.cpp:311-359), disabled at alpha ≥ 0.8                                | Solution.cpp:189-190 switch: alpha ≥ 0.8 ⇒ a span-aware delay (not NoDelay) when oracle V_tpot > 0                                                                                                                       | V_tpot > 0 ∧ V_tp-priced E cost of the park < span gain; never when pipe == 0 (stuck, SA06 §2.3)                                                                                                                                        |
| **M4 link-bound value ordering Lout/(2(Lin+Lout))** (SA06 §3.2, L11)                                                | none — ppre buckets order by Lin only (state.cpp:458)                                         | `ppre_pop` bucket priority or `choose()` tie-break, keyed on predict_lout                                                                                                                                                | only in UP-bound regime (util_up > 0.9); LOW CF EV (link-sat family has no headroom — SA09 §2)                                                                                                                                          |
| **M5 survival-conditioned Lout max(μ, iter+1)** (SA06 §4.3, SA08 AN-5)                                              | iter tracked (state.hpp:45), used nowhere                                                     | `predict_lout` (state.cpp:610-613), one line: `max(bucket_mean, iter+1)`                                                                                                                                                 | always-on (strictly better estimate); consumers unchanged                                                                                                                                                                               |
| **M6 single-cloud mega re-merge (x9-class)** (SA09 §3.2; +348 local)                                                | cross-cloud combine exists (scheduler.cpp:206-259); cloud choice spreads (policies.cpp:37-83) | (a) `choose()` m_cap := argmin-over-listed (reuse precompute logic); (b) D_POST wait-for-siblings (scheduler.cpp:275-284); (c) accumulation target live when the x9 predicate holds                                      | x9 predicate: per-token cost minimized at m≈R ∧ local-bound ∧ w_tp-dominant ∧ tp-linear; PLUS adv_living_cliff + x1/x6 byte-identical (link-bound must not move)                                                                        |
| **M7 finish-line E-gradient endgame** (SA07 #4; teeter/ambush slack 51% of E)                                       | oracle last_finish tracked; no last-finisher identity anywhere                                | tail-priority key in D_PRE fill order (scheduler.cpp:232-259): among near-ready, prefer max(remaining tokens via M5)                                                                                                     | only when V_tp > 0 (tp-linear); no hold added (race always — L10)                                                                                                                                                                       |

## 5. Falsification experiment designs recorded at program close (top changes; program completed 2026-08-23)

All were to follow docs/DECISIONS.md battery (zoo 22/22, oracle 68/68 + mimics, crash 500/500,
hostile ALL, extremes byte-checks, A/B vs snapshot) — treated as the gate, with the
D-121 rule: A/B on `build/submission`, never the dev build.

**Change #0 — TLE/frame-count hardening (SA05 §5; survives this audit's check).**

Static per-frame cost: `pick_cloud`'s SPT scan (scheduler.cpp:300-308) is O(|READY_P_PROC| of cloud) — the only unbounded per-frame loop; `choose()` walks the same lists per
P_PRE issue (policies.cpp:49-53). SA05 measured 10.5 µs/frame average on case_2m
(1M frames) ⇒ 2·10⁶-frame statement bound ≈ 21 s > 15 s TL ⇒ 0 ⇒ −50 final-mean pts.
Fix: after F_max ≈ 1.2M frames (above every observed test; case_2m ≈ 1M, worst CF
2.3 s ≪), degenerate to O(1)-per-frame mode: SPT scan → list head, `others` sort
skipped, group_size → constant. *A/B*: byte-identical on all 95 local cases (trigger
unreachable locally); case_2m timing re-measured; kill if any verdict flips. Rollback:
git checkout of src/.

**Change A — x9-class cohort integrity (M6), three separable sub-changes.**
A1 (accumulate target under x9 predicate — NOT unconditional; unconditional was
falsified this session: m19b E +5.2%, m19c +4.0%, x4 +20.6%, x7/x1/x6/straggler/
finstorm E unchanged, x9 −2.1%). *Cases*: x9 (+40 expected), m19b/c (must be
byte-identical — predicate false there), x4/x7/x1/x6 (byte-check), adv_living_cliff
(w_c=0.5 — unaffected path), full battery. Kill: any byte-delta outside the gated
population or any regression in it.
A2 (D_POST wait-for-siblings). *Cases*: x9 (−200 ms/round expected ⇒ ~+120 pts),
m19b/c/m6/m6b/m15/m17c/d (must hold within ±0.1), adv_living_cliff (razor: dist_base=0
— the wait must be gated !tdr_critical ∧ dist-safe), zoo 22/22. Kill: any cliff case
drops > 0.5, or suite net < +20.
A3 (choose() m_cap := argmin; consolidation preference under x9 predicate).
*Cases*: x9 (probe endpoint 736.8 minus A1/A2 parts), the 18 CF-shape mimics + 22 zoo
(cloud-choice touches everything — strictest A/B of the three), extremes x1/x6 with
peak-UP-depth ≤ 2 trace-verified. Kill: ANY zoo/adv regression beyond noise; this is
D-146/D-147/D-150 minefield (Theorem-2 KKT was falsified here).
Expected A-total on x9: +348 ceiling; realistic +250–340 (fill imperfections).

**Change B — herd-sync hold for mixed pools (M2/M5).** *Cases*: m5_order (+50–150
target; mega endpoint +199), m3/m3b, living_cliff_infeasible/adv_living_cliff (gate
safety), m6/m6b/m9* (starve residents — byte-check: the D-152/D-153 wrong-sign family),
m19b/c (probes could NOT beat artifact there — must not regress), full battery +
san/debug asserts on x3/x5/x6/finstorm (new wait paths = new deadlock surface: verify
event-backed wake by the SA06 §2.3 stuck battery). Kill: m19b/c net negative, any
stuck/timeout in hostile harness, or local net < +50 given the family's CF
wrong-sign record. **CF rule: at most ONE probe submission, retire on loss** (SA09
priority order).

## 6. Risk register (change → known CF exposure → rollback)

| change                              | CF tests at risk (SA05 ledger)                                                                                                                                            | local guard                                                                   | rollback                                              |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ----------------------------------------------------- |
| #0 TLE guard                        | none behaviorally (trigger > 1.2M frames; worst CF 2.3 s ≈ 13k frames); risk is a bug in the degenerate path itself                                                       | byte-identical 95/95 + crash sweep 500                                        | git checkout src/ (single commit)                     |
| A1 gated accumulate                 | #19 (m19b/c residents — unconditional variant already measured −4..−5% E there), #13, #12 tail                                                                            | m19b/c byte-identical required                                                | revert commit; x9_wc case kept as the regression trap |
| A2 D_POST wait                      | #3/#14 (binary/razor cliffs — waiting TDR), #15/#17 (link-sat: the wait could serialize behind DOWN FIFO), every K>1 decode test                                          | adv_living_cliff + dist-gate; m15/m17c/d within ±0.1                          | revert; the wait is additive-gated (one bool)         |
| A3 cloud-choice m_cap/consolidation | #11 (x1-class link-bound: spread is currently load-bearing for UP depth ≤ 2), #4/#8 mixed, EVERYTHING touches cloud choice (D-146 +1.45/D-147 +5.1 were won in this file) | strictest battery + x1/x6 UP-depth trace; ship only with byte-tight predicate | revert to D-148 leader lineage                        |
| B herd-sync                         | #5/#6 starve (wrong-sign family: every local starve win failed to transfer; D-152 lost #3 500→0 in ONE submission via exactly a waiting override), #3 cliff               | weight-gated (w_tp-dominant ∧ tp-linear), m9* byte-check, stuck battery       | revert; standing rule: retire after one CF loss       |
| (M5 Lout censoring)                 | near-zero (strictly better estimate; consumers are estimates only)                                                                                                        | full A/B byte-tolerance ±small on estimate-driven cases                       | one-line revert                                       |

## 7. Ranked engineering plan recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026)

1. **#0 TLE/frame hardening** — protected up to 50 final-mean points per prevented
   zero on the frozen set; strictly behavior-neutral locally; recorded as do-first (SA05 §4:
   robustness dominated all prelim upside).

2. **Change A (x9-class cohort integrity), in order A2 → A1-gated → A3** — the only
   lever where a trivial policy beat the artifact by >300 local pts AND the ceiling
   permitted it (SA09). A2 was the single biggest per-round component (−200 of −298
   ms/round) and was locally falsifiable in one battery run. CF upside at close: #22 (≤81),
   #13/#19 tails.

3. **Change B (herd-sync hold, M2 + M5 censoring)** — +199 local endpoint on m5_order,
   but the documented wrong-sign family: recorded guidance was to implement gated, verify against the full
   battery, at most one CF probe, retire on loss.

4. **M3 span-aware delay at alpha ≥ 0.8** — only if the suite had shown adv_degenerate/
   m17-family gains without cliff exposure (SA07 #3 quantified +55..101 local).

5. **M1 wave pipelining** — largest theoretical pool (m19b/c ≈ +150–250 local each)
   but every probe LOST to the artifact there (SA09 §3.3) and it multiplied cyclic
   dependencies (SA08 AN-4 warning): recorded as only-after-1–3-banked, with the D-140-P0
   event-back proof to have been written before code.

6. Do NOT spend: M4 (link-bound ordering — saturated family), sat0 tdr lumps
   (SA09 §5), anything on #1/#2 (unbeatable reference).

## Reproduction

```bash
python3 experiments/sa10/mkvariants.py          # writes /tmp/sa10 variants
python3 tools/interactor.py --case cases/adv/adv_x9_above.txt \
  --solver build/submission --trace-file /tmp/sa10/tr_x9.txt
python3 experiments/sa10/trace_groups.py /tmp/sa10/tr_x9.txt /tmp/sa10/tr_x9_wc.txt
# x9_wc: D_PRE {2000:100} — the w_c-gate re-merge proof

```

Traces kept: /tmp/sa10/tr_{x9,x9_wc,x9_k1,m5,m5wc0}.txt, b_/v_ pairs for the 8
w_c=0 residents. Scripts: experiments/sa10/{trace_groups.py,mkvariants.py}.
