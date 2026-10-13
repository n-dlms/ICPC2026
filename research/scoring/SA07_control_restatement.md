# SA07 — The True Objective as Scheduling Physics: Consolidated Control Restatement + Standing Scorecard

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA07 drafting). READ-ONLY on src/, tools/, cases/; artifacts in
`experiments/sa07/` (scorecard.py, scorecard_full.csv, scorecard_table.txt) and traces in
`/tmp/sa07/traces/`. Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Solver measured: `build/submission` (2026-08-23 tree,
117,016 B). Consolidates SA01 (formula audit), SA03 (tp interval), SA04 (waiting
geometry), SA05 (loss ledger), SA06 (admission value); every reused fact is cited, every
new number is from this session's harness.

---

## 1. The objective in scheduler variables

The solver controls, per decision frame at time t, which legal tasks start on which
machines. Nothing else is controllable: transfers are auto-queued at task completion,
Lout is hidden until FIN, there is no timer, no rejection, no abandonment.

**Score-relevant aggregates** (the ONLY quantities the score reads; SA01 §1, judge-exact):

```text
E     = last final-token time − earliest ARR            (tp denominator; N_tok/E = tp)
T     = Σ_i (ppost_i − a_i)   → tdr = T/R               (R = request count, ALL requests)
S     = Σ_i (e_i,last − e_i,first) → tpot = S/G         (G = Σ(Lout_i − 1); tpot := 0 if G=0)
N_tok = Σ Lout_i                                        (fixed by the test file — no lever)
```

- `E` — elapsed, tp denominator window
- `T` — total prefill wait, `tdr` is `T/R`
- `S` — total decode span, `tpot` is `S/G`
- `N_tok` — total tokens, fixed by test
- `R` — request count
- `G` — total gaps, `Σ(Lout_i − 1)`

```text
Score = 1000 · [ w_tp · clamp((N_tok/E − tp_base)/(tp_UB − tp_base); 0, 1)
               + w_c  · C_w(dist),  dist = √(e_tdr² + e_tpot²),
                 e_tdr = max(0,(tdr−SLO1)/SLO1), e_tpot = max(0,(tpot−SLO2)/SLO2),
                 C_w = max(0, 1−dist/dist_base)  (binary 1{dist=0} if dist_base=0) ]
```

- `w_tp` — throughput weight
- `w_c` — waiting weight
- `tp_base` — reference rate
- `tp_UB` — high rate
- `e_tdr` — relative TDR excess
- `e_tpot` — relative TPOT excess
- `dist` — Euclidean excess
- `dist_base` — waiting budget
- `C_w` — waiting component
- `Score` — final 0–1000

So the entire game reduces to steering **three aggregates** — E (makespan), T (total
prefill-wait), S (total decode span) — against per-test clamps. Every mechanism either
moves E, T/R, or S/G, or it moves nothing.

### 1.1 The physics card (L1–L14)

Each law: statement (equality/inequality), 1–3 line derivation, strategic consequence.
L1–L11 are consolidated from SA03/SA04/SA05/SA06 (verified there); L12–L14 are new from
this session's scorecard.

| #       | Law                                 | Statement                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Derivation                                                                                      | Consequence                                                                                                                                                                                                 |                                                                                                                           |
| ------- | ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| L1      | Clock starts at first ARR           | `E ≥ a_max − a_min`; pre-first-ARR time unpriced                                                                                                                                                                                                                                                                                                                                                                                                                                              | window is [min ARR, last token] (problem.md:433-435; SA03 shift-invariance run)                 | no warm-up value; all idle INSIDE the window is paid at full rate                                                                                                                                           |                                                                                                                           |
| L2      | Clock ends at LAST token            | `E ≥ a_i + X_i + chain_i(m)` ∀i; 1 ms on the eventual last finisher = +1 ms E; tp = N_tok/E dilutes EVERY token                                                                                                                                                                                                                                                                                                                                                                               | single denominator; SA03 tail-owner pair: 69.7 pts swing for identical work                     | finish-order at the tail matters iff finish times are spread; straggler slack column of the scorecard measures the exposed tail                                                                             |                                                                                                                           |
| L3      | Only spans exist                    | `Σ_{j}(e_{j+1}−e_j) = e_last − e_first` (telescoping)                                                                                                                                                                                                                                                                                                                                                                                                                                         | SA04 Exp A: bursty vs even gaps score identically                                               | never spend on cadence/smoothness; front/back-load tokens freely inside the span                                                                                                                            |                                                                                                                           |
| L4      | Dead zone [ppost_i → first token]   | invisible to tdr AND tpot; costs only E (iff makespan-critical) or via FIFO contention                                                                                                                                                                                                                                                                                                                                                                                                        | SA04 Exp B1/B2.3: byte-identical scores; B2.4 contention caveat                                 | free parking lot for batch formation; the ONLY priced placement is the makespan-critical request                                                                                                            |                                                                                                                           |
| L5      | Pooled means                        | Δtdr = ΔT/R; Δtpot = ΔS/G; no per-request term anywhere                                                                                                                                                                                                                                                                                                                                                                                                                                       | SA04 Exp A2/C: pooled 5.003 vs per-request 17.003; CA vs CB +4.2 pts                            | sacrifice the few (1/R, 1/G weights), speed the herd; concentrate damage on low-gap requests (Lout=1 absorbs unlimited decode delay)                                                                        |                                                                                                                           |
| L6      | SLO-relative pricing                | 1 ms of pool-mean lateness = 1/SLO1 (tdr) or 1/SLO2·(R/G scaling, tpot) in e-units; free below SLO                                                                                                                                                                                                                                                                                                                                                                                            | clamp definition                                                                                | large SLO = cheap lateness (cf9_shape 1 ms = 0.0018 rel; tdr_sensitive 0.025 rel, 14× pricier); work only priced axes                                                                                       |                                                                                                                           |
| L7      | Iso-score circles                   | dist const ⟺ de_tpot/de_tdr = −e_tdr/e_tpot; absolute: d(tpot)/d(tdr)\                                                                                                                                                                                                                                                                                                                                                                                                                        | _dist = −e_tdr·SLO2/(e_tpot·SLO1)                                                               | dist = ‖(e_tdr,e_tpot)‖; verified SA04 §4a on 3 live points                                                                                                                                                 | at a convertibility rate r the optimum sits where r = e_tdr·SLO2/(e_tpot·SLO1); push the axis with the larger e/SLO first |
| L8      | Completion is hard                  | done≠R → 0; stuck → 0 (interactor.py:395; problem.md:420,489)                                                                                                                                                                                                                                                                                                                                                                                                                                 | gate before scoring                                                                             | never abandon; every wait must keep an in-flight event (SA06 §2.3 park-no-wake stuck)                                                                                                                       |                                                                                                                           |
| L9      | Prefill delay never pays            | P PRE delay taxes tdr (X/R) AND E; dead-zone parking taxes neither                                                                                                                                                                                                                                                                                                                                                                                                                            | SA06 §1.2: holdppre always lost on w_tp=1; park1_50 dominated on every mix                      | admission delays belong only in the dead zone; P_PRE throttles are queue-valves, not value plays                                                                                                            |                                                                                                                           |
| L10     | Endgame = race                      | parking at a lull: −121..−234 pts + stuck risk (SA06 §6.2); idle-capacity argument                                                                                                                                                                                                                                                                                                                                                                                                            | lull resources are free; delay only shifts tail ownership                                       | race always; the tail lever is round-REGROUPING (this session: x9), never waiting                                                                                                                           |                                                                                                                           |
| L11     | Equal token value, unequal cost     | tokens per binding-resource ms: Lout_i/A_i (local), Lout_i/(2(Lin_i+Lout_i)) (link), Lout_i/B_i (cloud)                                                                                                                                                                                                                                                                                                                                                                                       | SA06 §3.1 charge model                                                                          | priority keys should be regime-matched; the link-bound key exists nowhere in src today                                                                                                                      |                                                                                                                           |
| **L12** | **Gradients vanish in clamps**      | dScore/dE = −1000·w_tp·N_tok/(E²·(tp_UB−tp_base)) only for tp_base<tp<tp_UB; dScore/dT = −1000·w_c·e_tdr/(dist_base·dist·SLO1·R) only for w_c>0, 0<dist<dist_base, e_tdr>0; dScore/dS = −1000·w_c·e_tpot/(dist_base·dist·SLO2·G) same gate; else 0                                                                                                                                                                                                                                            | clamp derivatives; scorecard column dE/dtdr/dspan is 0 on 24/95 cases despite active components | points are either linear-rate (interior) or step (dist crossing dist_base or 0; tp crossing tp_UB) — a sat0 case pays NOTHING until the pool mean crosses under SLO·(1+dist_base), then pays lump 1000·w_c  |                                                                                                                           |
| **L13** | **Resource serialization floors**   | E ≥ max(arrival span + solo(last ARR), UP-serial, DOWN-serial, local-serial, cloud/K-serial)                                                                                                                                                                                                                                                                                                                                                                                                  | every resource's total occupancy lies inside the window and is serial                           | the binding floor names the physics of the test (scorecard `bind` column): up = transfer-order world, local/cloud = compute-order world; efforts that don't attack the binding resource are dead on arrival |                                                                                                                           |
| **L14** | **Where E-headroom actually lives** | solver is at 96–100% of the binding floor on contended cases (m6 1.04×, m6b 1.11×, finstorm 1.00×, m15/x1/abyss link 97–100% — SA03 §3 + this scorecard); recoverable E-gap exists ONLY where (a) local occupancy is LOW (27–46%) while E ≫ cloud floor — a dependency-chain pipelining gap (m19b 2.28×, m19c 2.36×, m5_order 3.93×, living_cliffs 2.2×) — or (b) cohorts fragment and the small cohort pays the small-m table penalty (x9: 251+1751 lockstep, local 93.1% busy, 10.0× floor) | trace anatomy, §4.3 below                                                                       | E-compression attacks must target wave pipelining / cohort re-merge, not "less idle" (there is none) and not the link-saturated family                                                                      |                                                                                                                           |

---

## 2. The standing scorecard (95 cases)

Tool: **`experiments/sa07/scorecard.py`** (self-contained; runs any case through
`tools/interactor.py --trace-file`, recomputes every quantity from the trace, prints
regime labels, marginal gradients, and what-if levers; `--csv`/`--table` outputs).
Full run 2026-08-23:

```
python3 experiments/sa07/scorecard.py --all \
    --csv experiments/sa07/scorecard_full.csv \
    --table experiments/sa07/scorecard_table.txt --jobs 10

```

Outputs: `experiments/sa07/scorecard_full.csv` (95 rows × 60 cols — the standing
scoreboard), `experiments/sa07/scorecard_table.txt` (formatted), traces in
`/tmp/sa07/traces/` (recreate with the trace-dir flag). Spot-validation: mixed 500.25,
adv_degenerate 344.08, m15 tdr 9414.80 — byte-match SA01/SA04 (independent scorer).

### 2.1 Regime census (95 cases, current tree)

| axis | regime                          | count                            |
| ---- | ------------------------------- | -------------------------------- |
| tp   | tp-linear                       | 84                               |
| tp   | tp-saturated                    | 10                               |
| tp   | tp-below-base                   | 1 (adv_floor)                    |
| wait | wait-met (c_w=1)                | 40                               |
| wait | wait-linear (priced)            | 18 (13 of 18 single-axis priced) |
| wait | wait-sat0 (c_w=0, dist≥db)      | 27                               |
| wait | cliff-met / cliff-broken (db=0) | 3 / 7                            |

Live marginal gradient on any axis: 71/95 cases. Both tdr AND span priced
simultaneously: only 5 (cf10_shape, living_cliff_infeasible, m17, m17b, adv_degenerate).

### 2.2 Marginal gradients (pts/ms, sign = dScore/dquantity; 0 in saturated regions)

Marginal forms (L12, clamp-gated):

```text
dScore/dE = −1000·w_tp·N_tok/(E²·(tp_UB−tp_base))
dScore/dT = −1000·w_c·e_tdr/(dist_base·dist·SLO1·R)
dScore/dS = −1000·w_c·e_tpot/(dist_base·dist·SLO2·G)
```

- `E` — elapsed
- `T` — total wait
- `S` — total span
- `w_tp` — throughput weight
- `w_c` — waiting weight
- `N_tok` — total tokens
- `e_tdr` — relative TDR excess
- `e_tpot` — relative TPOT excess
- `dist` — Euclidean excess
- `dist_base` — waiting budget
- `SLO1` — TDR target
- `SLO2` — TPOT target
- `R` — request count
- `G` — total gaps

Representative priced rows (full table in the appendix / CSV):

| case                    | regime           | dScore/dE                       | dScore/d(mean tdr) | dScore/d(mean span) |
| ----------------------- | ---------------- | ------------------------------- | ------------------ | ------------------- |
| adv_degenerate          | tp-lin/wait-lin  | −0.0004                         | −0.705             | −9.358              |
| m5_order                | tp-lin/wait-lin  | −0.0243                         | 0                  | −0.667              |
| cf9_shape               | tp-sat/wait-lin  | 0                               | −0.041             | 0                   |
| m6 (starve)             | tp-lin/wait-met  | −0.0068                         | 0                  | 0                   |
| living_cliff_infeasible | tp-lin/wait-lin  | −0.0525                         | −0.177             | −0.744              |
| m17                     | tp-lin/wait-lin  | ~0                              | −0.003             | −0.025              |
| example1                | tp-lin/cliff-met | −6.13 (unreachable: E=floor=45) | 0                  | 0                   |

Gradient ≠ recoverability: example1 has the steepest dE on the board and zero headroom
(E already at its solo floor).

---

## 3. Points on the table (what-if analysis)

Levers, each re-scored with all clamps respected, other aggregates held fixed (E lever
conservative: span/tdr side-effects of speeding up are not credited):

| lever                         | suite Σ (95 cases)                        | realistic reading                                                                                               |
| ----------------------------- | ----------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| E −10% (tp-linear only)       | **+658.9**                                | upper bound of a genuine 10% makespan cut                                                                       |
| E → floor2 (contention-blind) | +1,962                                    | fantasy on contended cases; real only where occupancy is low (§4.3)                                             |
| tdr → solo prefill floor      | +8,768 (8,250 of it on 34 sat0 cases)     | fantasy: floors are queue-free solo; sat0 lump recovery needs tdr −45%+ on m9 family (SA05: below span/2 floor) |
| span −25%                     | **+266.9**                                | realistic tpot compression bound                                                                                |
| span → solo m=1 chain floor   | +1,433 (900 of it = m_lat, latency-bound) | upper bound; real on adv_degenerate/m17 family                                                                  |

Per-case attribution (top of each lever):

- **E lever** (tp-linear, w_tp high): m5_order +83.2, m19c +80.4, adv_abyss +68.3,
  x6 +56.5, x7 +52.3, x9 +51.7, x1 +33.8, example1 +30.7 (unreachable), m19b +27.6,
  adv_living_cliff +26.6, finstorm +26.4, m6 +19.0, m6b +17.3 (10% each).

- **tdr lever**: +500 lumps on compute_bottleneck/x8/cloud_imbalance/lout1_*/k8
  (w_c-typ 0.5, sat0→met), +950 m9 family, +1000 tdr_sensitive, +240.5 cf9_shape,
  +64.5 cf19_prefillsteep, +60 m15, +39 m17c. All require tdr cuts of 60–99% —
  **structurally unreachable** on every one (binding floors 2–10× below achieved tdr;
  cf9 SA05: below span/2 floor; m15 link-sat 97.8%).

- **span lever**: m_lat +900 (fantasy), m17d +323, adv_degenerate +101 (floor),
  +55 @25%, m5_order +51, cf10_shape +34, m17/m17b +12, stuckbait +90 @25%.

**Net reading**: the genuinely contested local mass is ≈ E-10% + span-25% ≈ **930 pts
across 95 cases**, concentrated in ~15 cases (§4.3). The 8,768 tdr->floor number is the
siren song — the scorecard's floor columns prove it is solo-timing fantasy on every
sat0 case.

---

## 4. Mechanism → score-quantity map (audit-level; deep audit is SA10's)

Two-column map from named mechanisms in `src/policies.cpp`, `src/scheduler.cpp`,
`src/state.cpp` to the aggregate they actually influence. "DIRECT" = moves the
aggregate the score prices; "PROXY" = steers an intermediate that correlates with one;
"NONE" = no score channel.

| mechanism (site)                                                                    | what it steers                                                     | score quantity                                                                                                                                            |
| ----------------------------------------------------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Oracle marginal_tp_v/tdr_v/tpot_v (state.hpp:276-292)                               | exact judge gradients, zeroed in clamps                            | E, T, S — DIRECT, judge-exact (matches interactor `_marginal_values`)                                                                                     |
| MarginalBatch::precompute breakpoint-argmin max_m_ (policies.cpp:161-188)           | per-token amortized batch size                                     | E (local work per token), S — DIRECT                                                                                                                      |
| group_size SLO2 crossing + Twist-1 tradeoff (policies.cpp:190-275)                  | batch cap vs cost(m)≤SLO2 with V_tp/V_tpot/V_tdr finite-difference | S (span period), E (fewer S payments), T (local occupancy) — DIRECT                                                                                       |
| low_lout_regime = ewma_lout<2.5 → SLO1*0.8 threshold (policies.cpp:215-216)         | batch cap under small-Lout prior                                   | E+T (correct: G=0 → tpot dead); the GATE keys on ewma, not on the true dead condition G=Σ(Lout−1)=0 — PROXY with a mismatch (bimodal pools misclassified) |
| ewma_lout / predict_lout (state.cpp:284, 603-612)                                   | hidden-Lout prior for cloud EFT decode debt                        | E/T estimates — PROXY (no fairness term exists; iter-based censoring bound unused, SA06 §4.3)                                                             |
| SloAwareDelay::hold_dpre Theorem-6 marginal wait (policies.cpp:283-360)             | dead-zone park-for-batch vs V_tp·ΔE + V_tpot·Δspan + V_tdr·wait    | S vs E/T — DIRECT (correct placement per L4/L9)                                                                                                           |
| tdr_critical override >0.15·dist_base (policies.cpp:294-296)                        | suppress holds when tdr bleeding                                   | T — DIRECT (pool-mean keyed)                                                                                                                              |
| binary-cliff imminent-P_POST pairing (policies.cpp:299-311)                         | pair before P POST under db=0                                      | T — DIRECT but step-priced (lump, not gradient; SA01 fallacy #4 applies to its V_tpot fallback)                                                           |
| V11 up_full P_PRE throttle, max_inflight_eff (scheduler.cpp:94-105)                 | P_PRE admission depth vs UP occupancy                              | E (UP serialization) + T — DIRECT effect, but the constants (2, /2+1, 2+2K) are unpriced heuristics on link occupancy, not score quantities               |
| dist_headroom < 0.5·dist_base relaxation (scheduler.cpp:98-103)                     | widen throttle when waiting safe                                   | same — PROXY for "waiting points live" (keyed on the true dist)                                                                                           |
| run_ppost arbitration q_j<a_i + tdr_urgent mv_tdr>0 (scheduler.cpp:118-144)         | P POST vs P PRE ordering                                           | T (mean ppost timing) + E — DIRECT, pool-priced (D-151 fixed the per-request-era 0.3 threshold)                                                           |
| SJF-by-Lin ppre_pop buckets (state.cpp:458-485)                                     | prefill admission order                                            | T (Smith-optimal) — DIRECT; side-effect: straggler generation under Lin↔Lout correlation → E — INDIRECT harm (SA06 §3.2)                                  |
| Straggler balancer Tier-1 Lin≥256 isolation (policies.cpp:19-27)                    | isolate fat prefills                                               | T of co-located + E tail — DIRECT-ish; 256 is a magic scale                                                                                               |
| EFT cloud choose + T9 alignment floor + power-of-two near-ties (policies.cpp:29-90) | cloud assignment                                                   | T + E — DIRECT                                                                                                                                            |
| AdaptiveChunk k_opt (policies.cpp:120-152)                                          | prefill chunking                                                   | T (earlier first token), E — DIRECT                                                                                                                       |
| DPRE_ACCUMULATE target wait (scheduler.cpp:170-190)                                 | dead-zone batch accumulation                                       | S + E — DIRECT (event-backed per L8)                                                                                                                      |
| dynamic cross-cloud cap xfer_ratio ladder 0.05/0.3/1/5 (scheduler.cpp:210-235)      | group cloud-spread                                                 | S/E via transfer amortization — DIRECT effect, magic-ratio constants                                                                                      |
| multi_token_dpre_count (state.hpp:45, buckets)                                      | iter>0 cohort for hold suppression                                 | S of started requests — DIRECT use of a real signal                                                                                                       |
| alpha_ (policies.cpp:191)                                                           | —                                                                  | NONE — comment says "Alpha is dead"; confirmed dead knob                                                                                                  |

**Dead quantities confirmed absent or vestigial**: (1) cadence/gap-smoothness — no
mechanism steers it (grep + SA04); the D-023 "smooth decode" phrasing never became
code. (2) per-request SLO compliance counting — all live urgency keys are pool-mean
derivatives (the D-151 fix removed the last threshold-flavored one). (3) ewma_lout as
fairness — it is only a hidden-Lout prior; its regime gate mismatches G=0. (4)
max_inflight as an end in itself — it is a link-occupancy valve with unpriced
constants; the quantity it protects (UP FIFO serialization → E/T) is real.
(5) `alpha_` — dead parameter. The audit-level risk is therefore NOT imaginary
objectives but MISPRICED constants (magic thresholds 0.15, 0.5, 256, 2.5, 0.05/0.3/1/5)
on otherwise sound channels — SA10's job to price-test them.

---

## 5. Structural physics blocking — ranked change candidates recorded at program close (local analogues; program completed 2026-08-23)

Ranked by expected LOCAL points (scorecard floors + trace anatomy, as assessed at program close). All would have had to respect
L8 (event-backed waits) and pass adv_living_cliff (standing rule, SA05 §4b).

### #1 — Wave pipelining / cohort interleaving on uniform dense pools ("E-gap where local occupancy is low")

- **Diagnosis**: m19b E=5,147 vs cloud-serial floor 2,258 (2.28×) with LOCAL occupancy
  only 26.9%; m19c 2.36× at 43.8%; m5_order 3.93× at 45.8%; living_cliff_infeasible
  2.20× at 36.5%. Groups are stable (m19b: 486 of 494 D POST groups = 26 members,
  lockstep) — the loss is the per-round dependency chain (D PRE→UP→D PROC→DOWN→D POST
  serializes every wave; clouds idle while the local machine runs D PRE/D POST and
  vice versa). Not idle-filling (L14): there IS no idle-free restructuring; the fix is
  2–4 INTERLEAVED cohorts so cohort A's cloud leg overlaps cohort B's local legs.

- **Affected regime/cases**: tp-linear, local/cloud-bound, dense uniform-Lout:
  m19b (+318 floor2; realistic +150–250), m19c (+277; +140–250), m5_order (+169;
  +80–160), living_cliff_infeasible (+166), adv_living_cliff (+141). CF analogs: the
  #13/#19/#22 tp-linear residual pool (SA05 §3 "residual linear-region": +250 raw,
  ~50–150 realistic — this is its mechanism candidate).

- **Falsification**: probe-solver experiment — split m19b's pool into 2 fixed cohorts,
  offset round phase by half a wave; if E does not drop ≥25%, the dependency-chain
  model is wrong. Secondary: if E drops but score doesn't (window arithmetic), the
  what-if overpriced it.

- **Risk**: cohort splitting shrinks groups (26→13) raising per-round table cost on
  steep tables — must only fire when the table is sub-linear at halved m (marginal
  check already exists in precompute).

### #2 — Cohort re-merge / anti-fragmentation for the drain tail AND mid-run ("x9's two-speed pool")

- **Diagnosis**: adv_x9_above runs 99 waves of a 251-cohort + 99 of a 1,751-cohort
  (after one initial 2,001 group); local occupancy 93.1% — E IS local-serial work, but
  the small cohort pays dpre(251)=188.5 vs dpre(1751)=116.9 per round on the V-shaped
  table. Re-merged 2,000-groups: local work 61.5k→~26k; lockstep E≈40k (+303 pts);
  with pipelining up to the floor +611 (tp would cross tp_UB → c_tp=1). This is SA06's
  §6.2 singleton-fragmentation soft spot, but persistent mid-run, not drain-tail.

- **Affected regime/cases**: tp-linear, w_tp≈1, sub-linear/V tables, staggered cloud
  readiness: x9 (+611 floor, +303 conservative), x4 (+39), cf10_shape (+34 span/E mix).

- **Falsification**: probe holding the fast 251-cohort one round (dead zone, event-
  backed) to re-merge with the 1,751; if local busy does not drop ≥35% or E does not
  drop correspondingly, dead. Battery guard: x1/x6 (link-bound) must not move.

- **Risk**: holding for re-merge costs tpot-span on the parked cohort (V_tpot>0 cases)
  — gate on V_tp-dominance (w_tp·V_tp > V_tpot·span-loss), exactly the D-144
  tp_dominant split pattern.

### #3 — Dead-zone parking depth for span compression (the +136..+180 crafted win, industrialized)

- **Diagnosis**: 18 wait-linear cases; where e_tpot>0 the span lever is priced
  (adv_degenerate −9.36 pts/ms of pool span). DPRE_ACCUMULATE already parks, but the
  artifact reaped less compression than an aggressive batcher on SA06's crafted case
  (tpot 28.9 vs 23.9 — group caps bind). Scorecard: adv_degenerate +55 @25% span,
  +101 at solo floor; m17/m17b +4.3 @25%; cf10_shape +9.4 @25%/+34 floor; stuckbait
  +90 @25%.

- **Affected regime/cases**: wait-linear e_tpot>0: adv_degenerate, m17 family,
  cf10_shape. CF analogs: #17/#18/#21 waiting tails (small, single-digit each, SA05)
  and #14's 71-pt waiting slice.

- **Falsification**: A/B a deeper accumulate target (target+1 group) gated on
  V_tpot>0 && V_tp≈0 on adv_degenerate/m17b/cf10_shape; if span does not shrink or
  T/E side loses more than the span gain, dead. Must pass the B2.4 contention caveat
  (parked rounds must not block in-flight FIFO traffic — check tdr/E side effects).

### #4 — Straggler early-start overlap at the finish line

- **Diagnosis**: straggler slack (last FIN − 2nd-last): teeter/ambush 3,494 (51% of E),
  straggler 4,599, purgatory 4,491. SA03 proved the tail itself is at the dependency
  floor (6.988 vs 7.002 ms/round) — the lever is starting the straggler's decode
  EARLIER (overlap with the herd), not speeding the tail. dE->floor2 +31–34 each
  (floor2 contention-blind, so realistic ~+10–30).

- **Falsification**: trace-measure straggler first-decode start vs herd-drain time; if
  the gap is already ≈0 (straggler decodes as early as its prefill allows), the lever
  is exhausted — the loss is arrival-order physics (L2 tail-owner), not policy.

### #5 — (Negative result, do NOT spend) sat0 tdr lump recovery + link-bound admission ordering

- The 8,250 sat0 tdr->floor points require tdr cuts of 45–99% into queue-free-solo
  fantasy (binding floors 2–10× away; m9 family needs mean tdr <5,900 vs solo floor
  27 and cloud/local floors ≥14,400 — the lump is physically unreachable). The
  scorecard CONFIRMS SA05's structural verdict with floor arithmetic.

- Link-bound family (m15, m17c/d, abyss, x1, x6, blackhole, transfer_burst: bind=up,
  97–100% link-saturated): the tokens-per-byte admission key (L11) has no local
  headroom to prove itself on — only worth SA05's never-run link-ceiling discriminator
  on a faithful #11 resident, not local A/Bs.

---

## 6. Open questions recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026)

1. Whether the #1 pipelining mechanism would have survived on CF windows. m19b/c mimicked #19 whose real
   band was tighter (m19 was loose: c_tp=1.0); the recorded prerequisite was a faithful #13/#22 resident (tight band,
   dense uniform arrivals) to have been authored before implementing (SA03's fit_cf_tp
   bands: #13 0.0076–0.036, #22 0.166–43.6).

2. Whether x9's two-cohort split was caused by the home-cloud-first fill order (scheduler.cpp
   'FIRST fill from the home cloud') or by arrival skew into READY_D_PRE. The trace answer
   would have decided whether the fix was ordering or a re-merge hold.

3. The L12 step-events (tp crossing tp_UB) were unscored by marginal machinery by
   construction — the question at close was whether there was a case where a LUMP tp jump (crossing tp_UB) was 1 achievable
   wave away. (x9 WAS that case: E 40k → tp 5.0 < 7; E 28.6k → tp 7.0 = UB exactly.)

4. cf19_prefillsteep's local dtdr->floor (+64.5) contradicted SA05's "CF #19 miss was
   100% tp (w_c=0)" — the mimic's w_c=0.7 differed from the real test's w_c=0; recorded at close as a mimic
   calibration issue, flagged for the D-143 mimic program (same family as m6's tp_UB=20
   vs real 3.3).

5. m_lat's +900 span->floor: latency×rounds bound — the remaining task at close was to quantify the true floor (2·lat per
   round was irreducible per request) before believing any of it.

---

## Appendix A — full scorecard table

See `experiments/sa07/scorecard_table.txt` (formatted, both blocks) and
`experiments/sa07/scorecard_full.csv` (machine-readable, all columns incl. floors,
binding resource, straggler slack, arrival span, tail). Key columns: E / fl2 ratio and
`bind` (L13); `dE`/`dtdr`/`dspan` (L12); `dE-1/5/10%`, `dE->floor2`, `dtdr->floor`,
`dspan-10/25%`, `dspan->floor` (§3 levers); `slack` (L2 exposure).

## Appendix B — reproduction

```bash
# standing scoreboard (95 cases, ~6 min at --jobs 10)
python3 experiments/sa07/scorecard.py --all --csv experiments/sa07/scorecard_full.csv \
    --table experiments/sa07/scorecard_table.txt --jobs 10
# single case card
python3 experiments/sa07/scorecard.py --case cases/adv/adv_x9_above.txt
# custom what-if percentages
python3 experiments/sa07/scorecard.py --all --es 2,10 --spans 20

```

Method notes: aggregates recomputed from the judge trace (D POST TDN completions =
token times; validated against SA01's independent scorer and interactor Sim.score);
floors from SA03's validated solo reconstruction (1e-8 ms agreement) and resource
serial bounds; scores re-derived with full clamps. floor2 is a valid but LOOSE lower
bound where the optimal-m ≠ R (x9-class) — the dE->floor2 column is an upper bound on
headroom, cross-checked against trace occupancy in §5 before being believed.
