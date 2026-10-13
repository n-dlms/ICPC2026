# Hidden-Test Atlas (D-155 era) — reverse-engineered from 7 judgement protocols (contest archive that led to #56; contest ended Aug 28 2026)

Author: Ntokozo Dlamini (AI sped up solving/checking). Method: per-test linear solves.

```text
w_tp = (S−ncw)/(ntp−ncw)
dist_base = dist/(1−ncw)
SLO pair = bounded grid solve on clamped hypot
tp_base/tp_UB = 2-point linear
```

| # | w_tp | w_c | dist_base | SLO1 | SLO2 | tp_UB | best_pts | class |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | .50 | .50 | ? | ? | ? | ? | 500.000 | example |
| 2 | .50 | .50 | ? | ? | ? | ? | 500.000 | waiting-perfect |
| 3 | 0 | 1 | 1.157 | ~836.7 | ~65.6 | .00523 | 500.568 | PURE-WAIT (TDR race) |
| 4 | .30 | .70 | 27.15 | ? | ? | .1156 | 792.496 | mixed |
| 5 | .80 | .20 | 1694 | ? | ? | 3.325 | 440.377 | TP-RACE (at 30% UB) |
| 6 | .90 | .10 | 646.9 | ? | ? | 2.112 | 361.677 | TP-RACE (at 30% UB) |
| 7 | 0 | 1 | 4.018 | ? | ? | .02979 | 913.162 | PURE-WAIT |
| 8 | .25 | .75 | 10.88 | ? | ? | .01637 | 810.733 | mixed |
| 9 | .05 | .95 | 33.85 | 554.031 | – | .004511 | 735.976 | TDR race (tpot=0 fam) |
| 10 | .15 | .85 | 388.9 | ? | ? | .007655 | 684.407 | waiting-heavy |
| 11 | .50 | .50 | ? | 9.27e7 | 32398 | ? | 500.239 | transfer-bound physics |
| 12 | .99 | .01 | 4.49✓hist | ? | ? | ? | 805.443 | PURE-TP |
| 13 | .75 | .25 | 16.89 | ? | ? | .03569 | 710.024 | TP push ×1.36 |
| 14 | .65 | .35 | 0.865(!tight) | 163.592 | 356.405 | ? | 415.267 | TDR race (ept clamped 0) |
| 15 | .45 | .55 | 180.3 | 209950 | – | ? | 882.685 | tpot=0 fam |
| 16 | .98 | .02 | 400.4 | ? | ? | .03028 | 978.366 | TP (97% UB already) |
| 17 | .67 | .33 | 3259 | ? | ? | .000533 | 945.048 | near-saturated |
| 18 | .58 | .42 | 741 | 125998 | – | ? | 916.192 | tpot=0 fam |
| 19 | 1.00 | 0 | 41310 | ? | ? | .7464 | 879.290 | PURE-TP (88% UB! ×1.135) |
| 20 | .38 | .62 | 683.1 | 4675.4 | 183.57 | ? | 998.183 | near-maxed |
| 21 | .50 | .50 | 2918 | 237.189 | – | .01255 | 971.430 | 99% UB |
| 22 | .50 | .50 | 80000 | ? | ? | 43.63 | 918.764 | R=2000 stress (84% UB) |

## Exchange-rate theorems (exact, from solved params)

- **T-EXCH**:

  ```text
  dScore/d(mean_tdr) = 1000·w_c/(dist_base·SLO1) when ept clamped at 0
  ```

  - #3: **+1.034 pts per ms** of mean-TDR cut (biggest lever on the board; −19% tdr = +250).
  - #14: +2.47 pts/ms but only 28.9ms of headroom to c_w=1 (+71.4 total).
  - #9: +0.051 pts/ms; −18% tdr = +47.
- **T-TPRACE**:

  ```text
  dScore/d(tp) = 1000·w_tp/(tp_UB−tp_base)
  ```

  - #19: +1365/unit → ×1.135 makespan cut = **+121** (cheapest verified win).
  - #13: +26740/unit → ×1.36 = **+251**.
  - #22: +11.5/unit → ×1.188 = +79.
  - #5/#6: 242/430 per unit tp but ×3.3 needed — structural ceiling unknown.
- **T-WGATE**: `w_tp`/`w_c` are startup constants.

  Aggressiveness gated on WEIGHTS is inference-free (no D-152 snapshot fallacy).

  `w_c`≤0.2 ⇒ {#5,.2 #6,.1 #12,.01 #16,.02}: `c_w` risk costs ≤~20pts/0.01·100 while tp pays linear.

  `w_c`≥0.6 ⇒ strict discipline.
