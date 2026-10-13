# SA05 — Contest Aggregation + Per-Test Loss Accounting

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA05 drafting). Read-only for src/tools/cases; scratch in /tmp/sa05/
(scripts copied to experiments/sa05/). Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Binary measured: **sealed D-148 leader**
(git `a40d6f3:src/submission.cpp`, 65,342 chars, compiled standalone to
/tmp/sa05/sub_d148) — the CF 16,147.866 tree; the working tree held a newer
(D-158/D-159-era) candidate, so the sealed leader was rebuilt from git to keep
measurements faithful.

**Correction to the task brief**: missing points = 22,000 − 16,147.866 =
**5,852.134** (the brief's "852.134" dropped the leading 5). The ledger below
sums to 5,851.9 ± 0.3 (reconstruction residual 0.24, see §1.3).

Sources: docs/CF_JUDGEMENT_2026-08-16.md (full 22-row protocol, 13,577.064),
docs/CF_JUDGEMENT_2026-08-17.md (D-085 full protocol + D-087 note, 14,778.987),
DECISIONS.md D-104 (full protocol, 15,590.128), D-122 verdict (full protocol,
16,036.543), D-127/D-140/D-142/D-144/D-146/D-147 verdict deltas, D-152/D-153
postmortems, docs/HIDDEN_ATLAS.md (per-test w_tp/w_c/dist_base/SLO/tp_UB table
solved from 7 protocols by tools/atlas_solve.py), SA01 authoritative formula,
SA03 throughput-interval, SA04 waiting-component.

---

## 1. Per-test ledger — the 16,147.866 leader (D-147/D-148 tree)

### 1.1 Reconstruction chain

Full per-test protocols exist for 16,036.543 (D-122, DECISIONS line 4078) and
earlier. Later verdicts record only deltas, chained:

- D-127 (+23.049 → 16,059.592): per-test unattributed; #5/#6/#14 confirmed
  unchanged (416.5/361.4/415.3).

- D-140 (+74.494 → 16,134.086): Theorem-B pick_local; #3 500.567, #19 879.290,
  #14 415.267, #22 918.764 quoted as kept/moved-to; #5 implied 434.071 (from
  D-142's "434.071→440.377"); remaining gains unattributed (landed in the
  TDR/tp cluster, incl. #9 → 735.976, confirmed by D-152's v7 protocol).

- D-142 (+6.633 → 16,140.719): #5 +6.306 (→440.377), #16 +0.641, #6 +0.183,
  #17 +0.056, #4 +0.022 (→788.766), #12 +0.022, #15 −0.595, #18 −0.002;
  #1/#2/#3/#7/#8/#9/#10/#11/#13/#14/#19/#20/#21/#22 unchanged.

- D-144 (+0.595 → 16,141.314): #15 recovered to 882.685; others unchanged.
- D-146 (+1.453 → 16,142.767): #16 978.366 (+0.641), #17 944.959, #15 882.685,
  #21 966.180, #22 918.764, #4 788.766, #20 998.183 quoted; +0.812 unattributed.

- D-147 (+5.099 → **16,147.866**): #21 966.180→970.252, #13 709.056→710.024,
  #17 944.959→945.048, #6 361.704→361.674.

- Post-leader probes: v7 16,119.188 (−28.678, reverted), D-152 16,141.3−595.55
  (reverted) — **16,147.866 remains the best CF submission** (rank 174/3498).

### 1.2 The 22-test ledger (sorted by missing-from-1000, descending)

| rank | test  | leader pts   | missing     | basis                                | class (atlas)                     |
| ---- | ----- | ------------ | ----------- | ------------------------------------ | --------------------------------- |
| 1    | #6    | 361.674      | **638.326** | pinned                               | TP-RACE w=(.90,.10), norm_tp .292 |
| 2    | #14   | 415.267      | **584.733** | pinned (stable since 08-16)          | both-excess razor, dbase 0.865    |
| 3    | #5    | 440.377      | **559.623** | pinned                               | TP-RACE w=(.80,.20), norm_tp .301 |
| 4    | #1    | 500.000      | **500.000** | pinned (identical in every protocol) | example, norm_tp=0 forever        |
| 5    | #2    | 500.000      | **500.000** | pinned                               | serial/small-R, norm_tp=0         |
| 6    | #11   | ~500.19      | **~499.81** | est¹                                 | transfer-bound, SLO1=9.27e7       |
| 7    | #3    | 500.568      | **499.432** | pinned                               | PURE-WAIT w_tp=0, dbase 1.157     |
| 8    | #10   | ~684.13      | ~315.87     | est¹                                 | waiting-heavy w=(.15,.85)         |
| 9    | #13   | 710.024      | 289.976     | pinned                               | TP push ×1.36                     |
| 10   | #9    | 735.976      | ~264.02     | est²                                 | TDR race, Lout=1 fam, w=(.05,.95) |
| 11   | #4    | 788.766      | 211.234     | pinned                               | mixed                             |
| 12   | #12   | ~804.10      | ~195.90     | est¹                                 | PURE-TP w=(.99,.01), ntp .809     |
| 13   | #8    | ~810.73      | ~189.27     | est¹                                 | mixed                             |
| 14   | #19   | 879.290      | 120.710     | pinned (stable since D-104)          | PURE-TP w=1, 88% UB               |
| 15   | #15   | 882.685      | 117.315     | pinned                               | wait, Lout=1 fam                  |
| 16   | #7    | ~907.53      | ~92.47      | est¹                                 | pure wait                         |
| 17   | #18   | ~916.19      | ~83.81      | est¹                                 | wait, Lout=1 fam                  |
| 18   | #22   | 918.764      | 81.236      | pinned                               | R=2000 stress, 84% UB             |
| 19   | #17   | 945.048      | 54.952      | pinned                               | near-saturated                    |
| 20   | #21   | 970.252      | 29.748      | pinned                               | 99% UB                            |
| 21   | #16   | 978.366      | 21.634      | pinned                               | TP 97% UB                         |
| 22   | #20   | 998.183      | 1.817       | pinned                               | near-maxed                        |
|      | **Σ** | **16,147.9** | **5,851.9** |                                      |                                   |

¹ est = D-122 value carried through unchanged-lists; the D-127/D-140/D-146
unattributed gains (+23.05/+74.49/+0.81) plus the −0.24 reconstruction residual
sit inside {#7,#8,#9,#10,#11,#12,#18} — each ±0.5, ranking unaffected.
² #9=735.976 cross-confirmed by the v7 protocol (D-152) and D-153 ("score 786.9
vs 736.2" on cf9_shape).

**Σ = 16,147.866 official; my point estimates sum to 16,148.11 (residual −0.24
unattributable — the D-146 +0.812 and D-127/D-140 spreads were never recorded
per-test). Confidence: pinned values exact to 3 d.p.; est values ±0.5.**

### 1.3 Component decomposition (tp vs waiting)

```text
score = 1000·(w_tp·norm_tp + w_c·norm_c)
miss_tp = 1000·w_tp·(1−norm_tp)
miss_w = 1000·w_c·(1−norm_c)
```

- `score` — per-test 0–1000
- `w_tp` — throughput weight
- `w_c` — waiting weight
- `norm_tp` — normalized throughput
- `norm_c` — normalized waiting
- `miss_tp` — missing tp points
- `miss_w` — missing waiting points

Weights from HIDDEN_ATLAS (solved, exact);
norms from the newest protocol quoting that test (D-085/D-122/v7/D-146/D-147).

| test  | w_tp | w_c | miss_tp          | miss_w           | dominant loss                       |
| ----- | ---- | --- | ---------------- | ---------------- | ----------------------------------- |
| #1    | .50  | .50 | 500.0            | 0                | tp ≤ tp_base (reference unbeatable) |
| #2    | .50  | .50 | 500.0            | 0                | same                                |
| #3    | 0    | 1   | 0                | 499.4            | waiting (dist .578/1.157)           |
| #4    | .30  | .70 | ~185             | ~27              | mixed                               |
| #5    | .80  | .20 | ~558             | ~1.6             | tp (norm_tp .301, tp_UB 3.3× tp)    |
| #6    | .90  | .10 | ~637             | ~1.2             | tp (norm_tp .292, tp_UB 2.1× tp)    |
| #7    | 0    | 1   | 0                | ~92              | waiting                             |
| #8    | .25  | .75 | ~75              | ~115             | mixed                               |
| #9    | .05  | .95 | ~4               | ~260             | waiting (e_tdr only, tpot=0)        |
| #10   | .15  | .85 | ~1               | ~315             | waiting                             |
| #11   | .50  | .50 | ~500             | ~0               | tp = 7e-6 ≈ tp_base                 |
| #12   | .99  | .01 | ~190             | ~6               | tp                                  |
| #13   | .75  | .25 | ~265             | ~25              | tp                                  |
| #14   | .65  | .35 | ~513             | ~71              | both (ntp .210, ncw .796)           |
| #15   | .45  | .55 | ~8               | ~109             | waiting (Lout=1)                    |
| #16   | .98  | .02 | ~20              | ~2               | tp (97% UB)                         |
| #17   | .67  | .33 | ~9               | ~46              | waiting                             |
| #18   | .58  | .42 | ~5               | ~79              | waiting (Lout=1)                    |
| #19   | 1    | 0   | 120.7            | 0                | tp (88% UB)                         |
| #20   | .38  | .62 | ~1.9             | ~0.6             | —                                   |
| #21   | .50  | .50 | ~2.5             | ~27              | —                                   |
| #22   | .50  | .50 | ~80              | ~1               | tp (84% UB)                         |
| **Σ** |      |     | **~4,175 (71%)** | **~1,678 (29%)** |                                     |

Headline: **the missing mass is 71% tp-side**, but ~2,700 of the 4,175 tp-miss
is provably structural (below). The waiting-side 1,678 is dominated by
#3+#10+#9+#15+#18 (1,262 of it).

---

## 2. Local loss decomposition (mimics × sealed D-148 leader)

Method: tools/interactor.py --trace-file on each mimic, components recomputed
from the trace with the exact Sim.score math (script: experiments/sa05/decompose.py,
traces /tmp/sa05/tr_*.txt). All verdicts ok.

| case              | tag        | score   | tp     | c_tp | tdr   | tpot  | e_tdr | e_tpot | c_w   | miss_tp | miss_w |
| ----------------- | ---------- | ------- | ------ | ---- | ----- | ----- | ----- | ------ | ----- | ------- | ------ |
| cf9_shape         | #9         | 701.53  | .0048  | 1.00 | 7915  | 0     | 13.31 | 0      | .607  | 0       | 298.5  |
| cf10_shape        | #10        | 650.45  | .0012  | .135 | 2812  | 254.6 | 1.23  | 27.93  | .928  | 302.9   | 46.7   |
| cf14_shape        | #14        | 1000.00 | .8779  | 1.00 | 12.7  | 18.5  | 0     | 0      | 1.000 | 0       | 0      |
| cf17_shape        | #17        | 1000.00 | .1988  | 1.00 | 1756  | 12.5  | 0     | 0      | 1.000 | 0       | 0      |
| cf19_prefillsteep | #19        | 639.27  | .2503  | .013 | 2803  | 12.1  | 4.61  | 0      | .908  | 296.2   | 64.5   |
| m3                | #3         | 719.06  | 2.7676 | .138 | 1302  | 19.3  | 1.60  | 0      | .968  | 258.5   | 22.4   |
| m6                | #5/#6 fast | 270.84  | 3.7965 | .190 | 18002 | 40.2  | 0     | 0      | 1.000 | 729.2   | 0      |
| m6b               | #5/#6 slow | 255.84  | 3.4632 | .173 | 4802  | 31.0  | 0     | 0      | 1.000 | 744.2   | 0      |
| m9                | TDR        | 0.00    | .0516  | 0    | 10765 | 0     | 214.3 | 0      | 0     | 50.0    | 950.0  |
| m9b               | TDR        | 0.01    | .0516  | 0    | 10764 | 0     | 214.3 | 0      | 0     | 150.0   | 850.0  |
| m9c               | TDR        | 0.00    | .0931  | 0    | 11253 | 0     | 224.1 | 0      | 0     | 50.0    | 950.0  |
| m15               | #15        | 488.70  | .0056  | 0    | 9579  | 0     | 3.79  | 0      | .889  | 450.0   | 61.3   |
| m17               | #17        | 326.19  | .3526  | 0    | 2178  | 715.8 | 42.6  | 70.6   | .931  | 649.8   | 24.0   |
| m17c              | #17 fat    | 310.87  | .0267  | 0    | 9606  | 7035  | 3.80  | 0      | .888  | 650.0   | 39.1   |
| m19               | #19        | 993.53  | 3.1446 | 1.00 | 1752  | 22.2  | 7.76  | 0      | .935  | 0       | 6.5    |
| m19b              | #19        | 248.71  | 2.4871 | .249 | 331   | 73.4  | 0     | 0      | 1.000 | 751.3   | 0      |
| m19c              | #19        | 723.29  | 3.6164 | .723 | 586   | 50.1  | 0     | 0      | 1.000 | 276.7   | 0      |
| m5_order          | #5         | 751.40  | 2.0613 | .789 | 1176  | 137.7 | 0     | 1.75   | .415  | 190.1   | 58.5   |
| m_lat             | abyss      | 20.74   | .1037  | .207 | 5081  | 1300  | 0     | 3.33   | 0     | 79.3    | 900.0  |

Faithfulness audit (important):

- **cf9_shape is component-faithful** for #9: waiting-only loss (miss_tp=0,
  miss_w 298.5 at local w_c=.76 vs CF ~260 at w_c=.95); tdr 7915 vs CF 5518-5725.

- **m6/m6b reproduce #5/#6's shape**: 100% of loss is tp-side, c_w=1.0,
  c_tp .17-.19 vs CF .29 — the starve class is exactly "tp_UB above the
  delivered rate with waiting safe".

- **cf14_shape and cf17_shape are NOT score-faithful** (local 1000 vs CF
  415.3/945.0): their SLO/tp windows are looser than the real tests (D-153
  already flagged cf14 calibration as deferred). They must not be used as
  recovery evidence.

- cf10_shape inverts the decomposition locally (miss mostly tp because its
  tp_UB=.00807 is far tighter than the real #10, where norm_tp=.994): the
  waiting side (miss_w 46.7, c_w .928) is the only transferable reading.

- m19 tp=3.14/c_tp=1.0 vs CF #19 tp=.2518/norm_tp .325 — m19's window is
  looser; cf19_prefillsteep matches CF #19's tp almost exactly (.2503 vs .2518)
  and shows the miss as tp-side (296 of 361 local missing) — consistent with
  CF #19's miss being 100% tp (w_c=0).

---

## 3. The big pools, ranked, with physical-feasibility analysis

### Pool 1 — #5 + #6 starve/TP-RACE class: 1,198 missing (tp-side ~1,195)

Facts: w_tp .80/.90, norm_tp .301/.292, tp_UB ≈ 3.30×/2.09× achieved tp (SA03
bands), dist ratio <0.01 (waiting safe, c_w ≈ .99). D-143/D-144's saturation
program: on faithful analogues max(E, UP, C/K)/mk ∈ [0.975, 1.000] with the
LOCAL edge binding (m6 1.000, m6b 0.976); the throttle's damage was extra
decode rounds, not idle, and D-142's relaxation already harvested the
admission-depth slice (+6.306 on #5). **Recovery requires beating the local
computer's serial work rate by ~3.3× — physically impossible (tp_UB was set
above the m→∞ bound). Verdict: ~95% structural; residual = the 2.4% E-tail and
wave-size trims, worth ~10-40 pts, already heavily mined.**

### Pool 2 — #14 both-excess razor: 585 missing (513 tp + 71 wait)

Waiting side: T-EXCH gives +2.47 pts per ms of mean-TDR cut, but only 28.9 ms
of headroom to c_w=1 (tdr 192.489 → SLO1 163.592, a −15% cut; e_tpot clamped
at 0). tp side: norm_tp .210 at tp .0036 → full recovery needs ~4.7× tp.
D-153: "#3/#14 are the only policy-recoverable pools left (+500/+580) but
demand tdr cuts of 30-40% on E-serialized streams — no known mechanism; three
composite submissions and four wrong-sign residents show local certifiability
is exhausted". **Verdict: the 71-pt waiting slice is the only concrete,
quantified target; mechanism unknown, and the local razor (cf14_shape) cannot
certify it. High effort, cliff-shaped downside.**

### Pool 3 — #3 PURE-WAIT: 499 missing (100% waiting)

w_tp=0, dist_base 1.157, dist .578. +1.034 pts per ms of mean-TDR (biggest
verified exchange rate on the board); c_w=1 needs tdr 1329.8 → 836.7 (−37%).
The E-serialization analysis (cf9-analog, D-153) says at rho_E≈1 mean TDR is
table-fixed; a 37% cut is below the span/2 floor on the analogue class.
**Verdict: recoverable in principle, no known mechanism; asymmetry was brutal
(D-152 lost this test 500.567→0 in ONE submission via snapshot-inference;
D-154 built adv_living_cliff specifically to catch this). The recorded guidance was to attack only with a
mechanism provably slack-guarded.**

### Pool 4 — #1 + #2 + #11 tp-below-reference: ~1,500 missing (all tp)

#1/#2: norm_tp=0 with dist=0/norm_c=1 in every protocol since 08-16 — tiny-R /
serial tests where the one-at-a-time reference cannot be beaten (CFUPGRADE
Class I-a; SA03 open-q4: tp=1/45 of window on #1). Structural write-off (~1,000).
#11: tp=7e-6 ≈ tp_base, SLO1=9.27e7 (trivial), tpot 16,199 < SLO2 32,398.
CFUPGRADE called #11 "the single most important test" IF the abyss link can be
pipelined; but the D-142/D-152 meter program found every local abyss analogue
(x1/x6/blackhole/transfer/m15/m17c/d) LINK-SAT at 96-100% of the physical
ceiling, and m_lat locally shows c_tp .207 with c_w=0 — the same family that
resists everywhere else. **Verdict: #1/#2 dead; #11 would have needed the
link-ceiling discriminator on a faithful resident (not run at program close); prior = mostly structural
(~400-490 of 499).**

### Pool 5 — the TDR/waiting cluster #10+#9+#15+#17+#18+#21: ~629 missing (waiting)

#9: PROVEN structural (D-153: tdr 5725 vs SLO1 553 needed — below span/2;
resident reproduces CF ±0.29). #10: SLO1 ≈ 1,269 back-solved vs tdr 182,690 —
c_w=1 needs a 144× cut; only the linear tail pays (~2.6 pts per 1% dist cut).
#15/#17/#18/#21: dist_base 180-3,259 slack, e_tdr-dominated, Lout=1 family;
T-EXCH rates are tiny (#18: 1000·.42/(741·125,998) ≈ 4.5e-6 pts/ms — a 10⁶-ms
TDR cut buys 4.5 pts). D-104's SJF already banked the recoverable part
(#15 544→883, #18 653→916, #17 725→945). **Verdict: structurally saturated;
remaining upside single-digit points per test.**

### Residual linear-region tp tests (not "big pools" but the cheapest wins)

Per T-TPRACE (atlas): #13 ×1.36 makespan cut = +251, #19 ×1.135 = +121, #22
×1.188 = +79 — IF such makespan cuts exist. Evidence against: #22 is E-bound
(D-153); #19's D-141-era estimate (+450-560 for #5/#6/#19/#22 if tables peak
mid-range) delivered only +6.6 total; #13 is the one test that still moves
with policy (v7 #13 +1.68 was the only real v7 win). Realistic combined
upside ~50-150, not +451.

**Bottom line on the 5,852: ~4,900-5,200 points are structural (link/edge
saturation, tp_UB above ceiling, SLO below pipeline floor, unbeatable
reference). Genuinely contested: #3 (499), #14 (585), #13/#19/#22 tails
(~250), starve tails (~50) — with negative-EV tails documented on #3-class
cliffs.**

---

## 4. Aggregation strategy — 20 frozen tests decide everything

Facts (SA02): final ranking = mean of 20 frozen system tests run after Aug 28
11:00 UTC; the 22 prelims were feedback-only. Winter-2025 analogue: frozen tests
were harder — a finalist post-mortem reported losing 10-16k (of ~126k, i.e.
5-13%) on frozen tests 4-11; the 2023 edition's prelim #1 finished final #7.
Scoreboard at program close: rank 174/3498 at 16,147.866; top-100 cutoff 16,270.824
(+123 = prelim-side, irrelevant to the final); leader 16,720.06.

### (a) Robustness vs prelim-specialization

A frozen-test zero (crash/stuck/TLE) costs **50 final-mean points**; a frozen
cliff-collapse (c_w 1→0) costs up to ~500 per test = 25 mean points. The
largest realistic prelim upside left (~150-350 pts, §3) is worth ~0-17 mean
points IF frozen tests resembled prelims — but they are "typically harder",
i.e. drawn toward the razor/starve/cliff classes where our losses are anyway
structural and where our tuned gates face out-of-distribution parameters. The
D-117/D-152/D-153 record (local +135 → CF −29; local +8.4 → CF −595) shows the
calibration error bars on any new mechanism exceed its expected gain.
**Robustness therefore dominated: the recorded recommendation was to prefer physics-general mechanisms
(marginal-cost batching, P_POST fall-through, saturation-aware caps) over
prelim-fitted thresholds; to freeze early unless a candidate passed the living-
cliff discriminator AND was inference-free.**

### (b) Overfit-risk inventory (prelim-specialization hazards in the leader)

- `ewma_lout() < 2.5` low-Lout regime with `SLO1*0.8` threshold (D-147 Fix 3B)
  — tuned to prelim Lout distributions; a frozen test with mixed Lout near 2.5
  gets a discontinuous policy.

- `dist_headroom = excess_dist < 0.5*dist_base` + `max_inflight = 2+K` +
  `w_tp >= w_c` tp_dominant split (D-142/D-144) — keyed to prelim dist ratios;
  #12's fragility is documented in-repo; misfires cost TDR on tight-cliff
  frozen tests.

- `tdr_urgent` threshold 0.3*dist_base (D-146) — magic constant.
- Tier-1 straggler isolation `Lin >= 256` (D-147) — Lin-scale magic.
- xfer_ratio ladder cutoffs 0.05/0.3/1.0/5.0 (D-147 Fix 1) — link-ratio magic.
- D-152's lost-waiting override — the canonical failure (killed #3 500→0);
  reverted; "means dilute" is now a standing theorem (D-153).

- Physics-general / low-risk: MarginalBatch breakpoint-argmin + unbounded
  rules (D-057/D-119 stack), Theorem-B pick_local arbitration (D-140),
  work-conserving core, V12 mirror, weight-gated modes (D-158/D-159 are
  explicitly inference-free per T-WGATE — the right pattern, +0.002 local).

- The suite now contains adv_living_cliff (D-154) — the only local guard
  against the binary-cliff frozen failure mode; any new aggressiveness must
  pass it (standing rule).

### (c) Zero-tail risk audit

Record: every CF protocol of the leader lineage = 22/22 OK; crash sweep
500/500, hostile harness ALL PASS, zoo 27/27, oracle 74/74 (D-159 era);
sanitizer clean; no open crash modes (the burst_100 debug-build assert is
-O0-FP-only, release+san clean). Residual zero-risks:

1. **TLE on a maximal-frame frozen test** (see §5: 1M frames = 10.5s solver
   CPU locally; statement allows 2·10⁶ frames ⇒ ~21s ⇒ 0). The prelim tests
   never came close (worst 296ms-2.3s), but "harder frozen set" could include
   a frames monster. This is the single largest identified zero-tail.

2. **Cliff collapse** (score 500+→~0, not a crash) — guarded by
   adv_living_cliff + weight-gating, but only as well as the local razor
   calibrates (D-153: local certifiability exhausted).

3. Process risk (D-152a stale-binary masking) — mitigated by the standing
   standalone-compile rule.

---

## 5. TL/ML budget audit (15 s / 256 MB / 2·10⁶ frames / R≤2000 / ΣLout≤2·10⁵)

CF-recorded (real judge): worst runtime 296 ms (#17, D-104 protocol, current
generation), 906 ms (#19) and 2,281 ms (#22, obsolete D-085 build); max RSS
2 MB. Headroom ≥ 6.6× on TL, 128× on ML.

Local, sealed D-148 leader (this session):

- scaling_bench 6/6 ok: R=2000 → 0.94 s wall (13,432 frames); growth ~linear.
- case_2m (1,001,516 frames): solver-only **6.63 s user + 3.90 s sys = 10.5 s
  CPU, RSS 8.0 MB** (the 40-44 s interactor "runtime" is Python-judge
  overhead; measured via /usr/bin/time wrapper).

- adv_x3_supertrap (205k frames): 7.9 s wall incl. judge. adv_finstorm 10k
  frames: 0.42 s. burst_100/mixed/dribble: ≤0.07 s.

⇒ per-frame solver cost ≈ **10.5 µs/frame**; the 15 s TL is exhausted at
≈1.4·10⁶ frames on this box. A frozen test at the statement's 2·10⁶-frame
bound with case_2m-like per-frame density would **TLE ⇒ 0 ⇒ −50 final mean**.
Memory is a non-issue everywhere (≤8.2 MB vs 256 MB). No adaptive
frame-budget/early-simplification guard existed in the artifact at program close;
hardening the per-frame path (or degrading gracefully at high frame counts)
was the highest-value robustness item found by this audit.

---

## 6. Bottom line — expected value of attacking each pool (as assessed at program close; program completed 2026-08-23)

| pool                        | size    | structurally recoverable?                                  | EV of attack now                                                                                                     |
| --------------------------- | ------- | ---------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| #5+#6 starve                | 1,198   | No — tp_UB ≈ 3.3×/2.1× physical ceiling; E-bound 97.5-100% | ~0-40 pts; mined out                                                                                                 |
| #14 razor                   | 585     | Wait slice (71) maybe; tp slice (513) needs 4.7×           | Low; no mechanism; bad local razor                                                                                   |
| #3 pure-wait                | 499     | In principle; −37% tdr on E-serialized stream              | Negative without new theory (cliff −500 tail)                                                                        |
| #1/#2/#11                   | ~1,500  | No (#1/#2); probably no (#11, link-sat)                    | ~0; only the link-ceiling discriminator is worth running                                                             |
| #10/#9/#15/#17/#18/#21      | ~629    | No — SLO below pipeline floor / tiny exchange rates        | ~0-15                                                                                                                |
| #13/#19/#22 tp-linear       | ~250    | Partially (#13 policy-movable)                             | ~50-150 if makespan structure found; #22 E-bound                                                                     |
| **Robustness (frozen-set)** | 20×1000 | n/a                                                        | **Dominates**: TLE-hardening protects up to 50 mean-pts per prevented zero; weight-gated inference-free changes only |

The 16,147.866 leader already sat at ~92% of the realistically attainable
prelim mass; the remaining prelim pools were mostly physics-bounded. Given the
frozen-set aggregation, marginal prelim points were worth far less than avoided
zeros/cliffs: the recorded allocation was (1) frame-count/TLE hardening,
(2) inference-free robustness only, (3) at most one CF probe on the #13-class
linear-tp upside before the ~Aug-26 freeze.
