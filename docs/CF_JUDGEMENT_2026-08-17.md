# Codeforces Real-Judge Results — 2026-08-17 (run 3: D-085) (archived runs that led to #56; contest ended Aug 28 2026)

**Total**: 14,775.143 points / 22,000 (67.2%)

**Artifact judged**: `src/submission.cpp` (D-085: tpot-only batch ratio + UP-tail load balancing + unconditional adaptive chunking; SJF P_PRE from D-081)

**Baseline**: 14,620.683 (D-082) → **+154.460** (D-085)

**Previous runs**: 13,577.064 (pre-D3), 13,783.929 (D-075), 13,792.389 (D-076, saved in `CF_JUDGEMENT_2026-08-16.md`), 14,620.683 (D-082 — rows NOT saved to disk; only the delta table in DECISIONS D-082 survives)

## Raw Judgement Protocol

```
#1: OK [0ms, 0MB]: points 500.0000027586 tp=0.022222 mean_tdr=30.000000 mean_tpot=0.000000 dist=0.000000 norm_tp=0.000000 norm_c=1.000000 normalized_score=0.500000 points=500.000003
#2: OK [15ms, 0MB]: points 500.0 tp=0.005755 mean_tdr=126.158679 mean_tpot=0.000000 dist=0.000000 norm_tp=0.000000 norm_c=1.000000 normalized_score=0.500000 points=500.000000
#3: OK [0ms, 0MB]: points 500.5679669212 tp=0.004545 mean_tdr=1329.849832 mean_tpot=61.537533 dist=0.577735 norm_tp=0.670259 norm_c=0.500568 normalized_score=0.500568 points=500.567967
#4: OK [0ms, 0MB]: points 775.6401125541 tp=0.049534 mean_tdr=466.388357 mean_tpot=125.211194 dist=1.525110 norm_tp=0.383224 norm_c=0.943818 normalized_score=0.775640 points=775.640113
#5: OK [125ms, 0MB]: points 381.9166573329 tp=0.775569 mean_tdr=1497.254452 mean_tpot=99.674581 dist=3.835809 norm_tp=0.227962 norm_c=0.997736 normalized_score=0.381917 points=381.916657
#6: OK [62ms, 0MB]: points 268.3712203509 tp=0.415114 mean_tdr=3585.784857 mean_tpot=85.296612 dist=6.108524 norm_tp=0.188128 norm_c=0.990558 normalized_score=0.268371 points=268.371220
#7: OK [0ms, 0MB]: points 845.1281998163 tp=0.009090 mean_tdr=978.307361 mean_tpot=77.704716 dist=0.622234 norm_tp=0.219392 norm_c=0.845128 normalized_score=0.845128 points=845.128200
#8: OK [15ms, 0MB]: points 810.7331527868 tp=0.012364 mean_tdr=1086.885839 mean_tpot=150.767532 dist=1.660186 norm_tp=0.700503 norm_c=0.847477 normalized_score=0.810733 points=810.733153
#9: OK [15ms, 0MB]: points 661.0733837161 tp=0.004274 mean_tdr=7169.709784 mean_tpot=0.000000 dist=11.940996 norm_tp=0.923495 norm_c=0.647262 normalized_score=0.661073 points=661.073384
#10: OK [31ms, 0MB]: points 684.0946788407 tp=0.007627 mean_tdr=182697.727218 mean_tpot=99.220654 dist=144.124192 norm_tp=0.994098 norm_c=0.629388 normalized_score=0.684095 points=684.094679
#11: OK [15ms, 0MB]: points 500.185382213 tp=0.000007 mean_tdr=32780482.884393 mean_tpot=16199.089335 dist=0.000000 norm_tp=0.000371 norm_c=1.000000 normalized_score=0.500185 points=500.185382
#12: OK [0ms, 0MB]: points 805.4426189366 tp=0.000024 mean_tdr=1057070.901985 mean_tpot=400.111705 dist=2.634796 norm_tp=0.809404 norm_c=0.413225 normalized_score=0.805443 points=805.442619
#13: OK [31ms, 0MB]: points 631.6410307525 tp=0.023515 mean_tdr=1683.762674 mean_tpot=128.163305 dist=2.889353 norm_tp=0.565883 norm_c=0.828916 normalized_score=0.631641 points=631.641031
#14: OK [31ms, 0MB]: points 415.2668658781 tp=0.003564 mean_tdr=192.489397 mean_tpot=186.962310 dist=0.176642 norm_tp=0.210323 norm_c=0.795876 normalized_score=0.415267 points=415.266866
#15: OK [46ms, 0MB]: points 882.6651154351 tp=0.000009 mean_tdr=7741874.160363 mean_tpot=0.000000 dist=35.874840 norm_tp=0.982404 norm_c=0.801060 normalized_score=0.882665 points=882.665115
#16: OK [31ms, 0MB]: points 971.0892871876 tp=0.029519 mean_tdr=41824.030033 mean_tpot=125.153831 dist=35.322575 norm_tp=0.972299 norm_c=0.911792 normalized_score=0.971089 points=971.089287
#17: OK [281ms, 0MB]: points 944.2298115604 tp=0.000520 mean_tdr=8526718.975648 mean_tpot=24334.698783 dist=462.463224 norm_tp=0.986650 norm_c=0.858103 normalized_score=0.944230 points=944.229812
#18: OK [15ms, 0MB]: points 915.6758282673 tp=0.000009 mean_tdr=17713632.685967 mean_tpot=0.000000 dist=139.586719 norm_tp=0.991026 norm_c=0.811621 normalized_score=0.915676 points=915.675828
#19: OK [906ms, 0MB]: points 324.9691092403 tp=0.251834 mean_tdr=175.145187 mean_tpot=2564.479889 dist=49.818075 norm_tp=0.324969 norm_c=0.998796 normalized_score=0.324969 points=324.969109
#20: OK [140ms, 0MB]: points 997.7030747913 tp=0.005607 mean_tdr=1244.765293 mean_tpot=284.639134 dist=0.550547 norm_tp=0.995271 norm_c=0.999194 normalized_score=0.997703 points=997.703075
#21: OK [62ms, 0MB]: points 961.6476562069 tp=0.012495 mean_tdr=49660.494631 mean_tpot=0.000000 dist=208.370627 norm_tp=0.994706 norm_c=0.928589 normalized_score=0.961648 points=961.647656
#22: OK [2281ms, 2MB]: points 497.1021550873 tp=0.248843 mean_tdr=2782.000000 mean_tpot=3708.000000 dist=616.473280 norm_tp=0.001910 norm_c=0.992295 normalized_score=0.497102 points=497.102155
```

## D-085 Attribution (vs D-082 baseline 14,620.683)

- **#3 +158.30** (342.26 → 500.57): THE D-085 WIN.

  mean_tpot 95.30 → 61.54, dist 0.745 → 0.578, norm_tp 0.542 → 0.670, norm_c 0.356 → 0.501.

  Both norms up; TDR neutral (1331.5 → 1329.8). Textbook death-spiral fix: batches stayed large, S amortized, TPOT improved.

- **#20 +0.40** (997.30 → 997.70): tp 0.0056 → 0.00561, tpot 366 → 285.

- **Losers (small, net −3.84 across the other 21 tests)**:

  - #22 −4.25 (tp 0.495 → 0.249, mean_tpot 935 → 3708 — TPOT collapsed, TDR identical; likely the tpot-ratio shrinking batches when e_tpot dominates, see D-086 note).
  - #9 −0.26, #10 −0.04, #11 −0.05. All other 18 tests byte-identical.

- **NOT moved by D-085**: every test that moved in D-082 (#4, #6, #12, #13, #15, #17, #18, #21) is byte-identical to the D-082 run — D-085 touched only the 6 tests above.

  SJF gains remain fully intact.

## Flags

- The 14,620.683 (D-082) rows were never saved to disk — only deltas survive in DECISIONS D-082.

  Policy I (Ntokozo Dlamini) adopted after this run and completed for #56: saved EVERY subsequent run's full protocol to a dated `docs/CF_JUDGEMENT_*.md`.

## Run 4: D-087 verdict (14,778.987, +3.844 vs D-085)

```text
#22: 501.3480552532 tp=0.494860 mean_tdr=2782.000000 mean_tpot=934.994586 dist=389.924727 norm_tp=0.007570 norm_c=0.995126
#20: 997.3011372539
```

- #22 fully recovered (was 497.102 in D-085).
- #20 lost the D-085 +0.402. All other tests byte-identical to D-085. Total 14,778.987.
