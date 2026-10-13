# TESTING_FORTRESS_DESIGN.md — Local judge, zoo cells, gates (design archive that led to #56)

Author: Ntokozo Dlamini (AI sped up implementation/checks). Scope: DESIGN ARCHIVE. Tool implementations were completed in Phase 5 after the solver baseline and contributed to #56. Contest ended Aug 28 2026.

---

## 1. tools/interactor.py — the local judge (core piece completed for #56)

Replicated the statement's interactor exactly. Consumes: a case file + a solver binary.
Runs the solver as a subprocess, drives the frame protocol, validates every response,
simulates FIFO transfers, scores, and reports verdicts.

### 1.1 Case file format (`.txt`, as completed for #56)

```
K S latency bandwidth bytes_per_token num_layers
SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c
N
batch_size prefill_pre prefill_proc prefill_post decode_pre decode_proc decode_post   (N rows)
R
arrival_t0 Lin0 Lout0
arrival_t1 Lin1 Lout1
...
```

- Arrivals pre-generated (deterministic per seed). R ≥ 1, ΣLout ≤ 2·10⁵.
- Interactor simulates: per-request lifecycle, S occupancy [t, t+S+dur], FIFO UP/DOWN (independent, both active), transfer time formula, TDN/XDN/FIN emission at exact times, coalescing same-timestamp events into one frame, END after final response.

### 1.2 Validation rules (any violation → verdict 0 + reason)

1. Parse errors (count not int, unknown step, bad server, m mismatch, ids).

2. n > K+1, or two tasks on the same resource in one response.

3. Task on a busy resource (server occupied; local or cloud busy).

4. Predecessor not delivered: ARR missing; input UP XDN missing for first P PROC piece; previous piece TDN missing; input DOWN XDN missing for P POST; previous final-step TDN missing for D PRE; current-iteration UP XDN missing for D PROC member; current-iteration DOWN XDN missing for D POST member.

5. Request wrong step / in-flight / finished (FIN seen); duplicate ids in group; m < 1.

6. Cloud mismatch: P PRE remote ∉ [0,K); P PROC/P POST remote ≠ assigned; D PROC member assigned elsewhere.

7. Illegal piece: empty, out of [0,num_layers], not ascending gap-free from 0.

8. Stuck state detection: unfinished requests exist AND no future event possible (no pending task/transfer/arrival) → 0.

9. TLE/MLE (wall-clock budget; CPU measured; generous factor for local runs).

10. EOF without END after all requests finished → treated per interactor rules.

### 1.3 Scoring

```text
Score = 1000·(w_tp·C_tp + w_c·C_w)
```

- `tp`, `μ_tdr`, `μ_tpot`, `dist`, `C_tp`, `C_w` — exact statement formula.

Also emits a per-request trace (TDR, first/last token, completion) for debugging.

### 1.4 CLI (as completed for #56)

```
python3 tools/interactor.py --case cases/zoo/single_k1.txt --solver build/solution [--seed N] [--trace] [--timeout 5]
→ VERDICT ok|violation(<reason>)|stuck|tle|re | score 500.000 | runtime 0.02s
```

## 2. Zoo cell definitions (statement-aligned; concrete params)

### 2.1 Correctness zoo (14 cells)

| Case | K | S | latency | bw | bpt | layers | R | pattern | checks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| single_k1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | immediate | basic lifecycle; example-1-shaped |
| single_k8 | 8 | 1 | 1 | 1 | 1 | 1 | 1 | immediate | cloud choice (7 idle) |
| burst_100 | 8 | 1 | 1 | 1 | 1 | 10 | 100 | all t=0 | queue explosion, balance |
| dribble | 8 | 1 | 1 | 1 | 1 | 10 | 100 | 1/frame | overhead amortization |
| zero_S | 2 | 0 | 1 | 1 | 1 | 10 | 10 | immediate | chunking free (num_layers≥1; S min 1 per constraints — S=1 and chunk-vs-1 comparison was used instead) |
| huge_S | 2 | 10 | 1 | 1 | 1 | 10 | 10 | immediate | chunking disabled by S |
| one_layer | 2 | 1 | 1 | 1 | 1 | 1 | 5 | immediate | no chunking possible |
| many_layers | 2 | 1 | 1 | 1 | 1 | 64 | 5 | immediate | chunking tradeoff |
| table_1row | 2 | 1 | 1 | 1 | 1 | 5 | 5 | immediate | clamp below/above |
| table_gaps | 2 | 1 | 1 | 1 | 1 | 5 | 5 | immediate | -1 skipping |
| table_unordered | 2 | 1 | 1 | 1 | 1 | 5 | 5 | immediate | sorting |
| empty_frame | 2 | 1 | 1 | 1 | 1 | 5 | 1 | delayed | e=0 (arrivals only after idle frames) |
| precision | 2 | 1 | 0.001 | 100 | 1 | 1 | 1 | immediate | double precision boundary |
| transfer_bottleneck | 2 | 1 | 50 | 0.001 | 10⁶ | 5 | 10 | immediate | UP/DOWN queue saturation |
| compute_bottleneck | 2 | 1 | 0.001 | 100 | 1 | 5 | 10 | immediate | compute-bound |
| prefill_only | 2 | 1 | 1 | 1 | 1 | 5 | 5 | immediate | large Lin, Lout=1 |
| decode_only | 2 | 1 | 1 | 1 | 1 | 5 | 5 | immediate | Lin=1, large Lout |
| mixed | 8 | 1 | 1 | 1 | 1 | 10 | 50 | mixed | phase separation |

### 2.2 Performance zoo

| Case | R | K | ΣLout | purpose |
| --- | --- | --- | --- | --- |
| scale_n_10 | 10 | 8 | ~500 | baseline |
| scale_n_100 | 100 | 8 | ~5e3 | linear check |
| scale_n_1000 | 1000 | 8 | ~5e4 | scaling wall (frames ~1e6) |
| scale_k_1 | 100 | 1 | ~5e3 | single cloud |
| scale_k_8 | 100 | 8 | ~5e3 | medium |
| scale_k_16→8 | 100 | 8 | ~5e3 | K capped at 8 by constraints |

### 2.3 Adversarial zoo

| Case | trap |
| --- | --- |
| cloud_imbalance | arrivals timed so min-busy_until misassigns unless queue-aware |
| batching_trap | table where decode dur is linear (batching no benefit) |
| chunking_trap | S=10, layers=64, small prefill_proc → chunking loses |
| transfer_burst | all P PRE finish together → UP explodes |
| tdr_sensitive | w_c=1, tight SLO1 → mean TDR matters |
| tpot_sensitive | w_tp=1, tight SLO2 → TPOT span matters |
| dist_base_zero | dist_base=0 → binary waiting component |
| stuck_risk | last request, no arrivals remain, must finish (stuck detection) |

### 2.4 Protocol torture

Truncated stream mid-frame; EOF after config; EOF after table; malformed event line (in solver-side: our parser must not crash); extra whitespace; missing trailing newline.

e=1000 single frame; n=K+1 max response; END immediately after config; END after first frame.

## 3. Gates (reuse kept generic harnesses)

1. `zoo_gate.py` — rerun with interactor backend: all correctness cells must VERDICT ok; score recorded.
2. `crash_sweep.py` — 3000 seeded random cases (generator in interactor or zoo_gen), zero RE/TLE/stuck/violation.
3. `scaling_bench.py` — perf zoo; budget analysis vs statement bounds (2·10⁶ frames, ~5 s).
4. Sanitizer build through the same interactor (correctness only).
5. Rebuild identity: sha256 of source(s) + merged submission + binary; `verify_frozen`-style target in Makefile.
6. A/B: `ab_test.py` (already generic) with interactor scoring backend.

## 4. Ordering (Phase 5 execution completed for #56)

1. interactor.py (validated against Example 1 transcript: reproduced 0→4→10→21→27→30→32→35→40→43→45 and score 500).
2. zoo_gen.py + zoo cells + zoo_gate.py backend swap — completed.
3. merge_submission.py + PROVENANCE.json + Makefile targets — completed.
4. crash_sweep, scaling_bench, sanitizer, torture — completed.
