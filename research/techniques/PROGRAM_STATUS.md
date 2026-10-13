# Computation modules (SB01–SB10): status and integration record

**Authorship note (post-contest):** former "Author: subagent SBxx" lines meant assistant drafting under Ntokozo Dlamini's direction (now reframed as "Prepared by Ntokozo Dlamini with assistant support").

All ideas, selections, and decisions are Ntokozo Dlamini's.

All ten computation modules were completed on 2026-08-23. The entries below retain the original technical findings, measurements, size estimates, and verification results.

Later sections record integration work on the post-contest working tree; they do not describe the sealed contest submission.

## Module results

### SB01 — online scheduling theory

- Status: DONE 2026-08-23.
- Content: gradient calculus D1–D8 (per-milestone constant value coefficients), three-regime batch law, WSPT value-rate ordering, endgame switch.
- Doc: `SB01_online_scheduling_theory.md`.

Key laws:

```text
q* = sqrt(2 * lambda * f0)
```

- `q*` — thin-pool optimal batch; `lambda` — ready rate; `f0` — fixed overhead.
- `V_E`, `V_P`, `V_S` — exact marginals (see SB04).

### SB02 — deterministic forward-model + rollout design

- Status: DONE 2026-08-23.
- Content: ε-exact `O(K)` min-scan sim, undo-journal, Bertsekas rollout over ≤7 candidates with current heuristics as base policy, budget ladder L0–L3.
- Doc: `SB02_forward_model_rollout.md`.

Commit rule:

```text
V_maximin(a) = min_f V(rollout(a, f))
```

- `a` — candidate; `f` — sampled future in `{F0, F1, F2}`.
- Commit only on maximin win over heuristic with margin `eps_v`.

### SB03 — forward simulator audited, fixed, verified

- Status: DONE 2026-08-23.
- Defects fixed (7): journal restore-width corruption; transfers enqueued at apply instead of TDN time — cross-cloud DOWN FIFO order was wrong; `D_PROC` / `D_POST` dup-id checks; missing `cstdio`; `-Werror` blockers; `FIN` / `TDN` emission order; unjournaled `nev`.
- Build: `-Werror` clean.
- Replay: Example 1 byte-identical (34 lines, 11 frames, file-path replay) + Example 2 timestamp-exact.
- Self-checks (a)–(e) PASS incl. bit-for-bit 50-action rollback x3 seeds.
- Stress: 1.62M events / 1.01M frames at 286 ns/event sim-core (2e6-frame extrapolation 0.92 s of 15 s; peak RSS 20.6 MB of 256 MB).
- Sizing: reference core 12,627 / minimal 10,287 NEW minified chars vs 7,470 net deletable (M2..M5) + 70 free ⇒ full mirror DOES NOT FIT, lean integrated engine (~8.0–8.7 KB) spec'd instead.
- Doc: `SB03_simulator_implementation.md` (7 sections incl. integration spec + funding order).
- All tooling bans honored (`interactor.py` cross-check removed from `SB03_sim_test.py`).

### SB04 — runtime score calculus

- Status: DONE 2026-08-23.
- Calculus: exact `V_E` / `V_P` / `V_S` / exchange / finite-delay forms FD-proven on 1065 random states (max rel 2.9e-10, zeros exact, all clamp edges + `dist_base = 0`).
- Oracle audit (3 headline wrongnesses):
  - zero marginals on the `dist == 0` knife edge + unpriced finite SLO crossings;
  - K=1 `V_tpot` 1000x unit error + mispriced cliff jump;
  - SLO2 cap uses the pipelined period `P_pipe` where the member gap binds, transfer leg missing `(k-1)*lat`.
- Member-gap law:

```text
P_gap(m, pool) = max(chain, ceil(pool / m) * P_pipe)
```

- `P_pipe` — pipelined period; `chain` — serial chain latency; `pool` — ready pool.
- Constants mapped (14): `0.15*db`, `0.5*db`, `ewma < 2.5`, `SLO1*0.8`, `0.05/0.3/1/5` ladder, `Lin >= 256`, `alpha 0.8`, `1.05`, inflight bases, `tdr_urgent` FD.
- Per-segment EOQ law + regime detector verified vs brute force 120/120.
- Size: fragment 9,916 / 9,304 B minified (A 4,974 / B 874 / C 3,345) vs 3,013 B deletable constant blocks (Tier-1 net ~break-even).
- Docs: `research/techniques/SB04_score_calculus.md` + `SB04_score_calculus.cpp` + `SB04_check.py`.

### SB05 — statistical inference layer

- Status: DONE 2026-08-23.
- Posterior: survival-conditioned `Lout` posterior (24 geometric-`Lin` buckets x 512-level completed histograms, EB shrinkage `kappa = 4` to global truncated mean, inverse-Mills fallback, coherence floor `E[L|L >= iter+1] >= iter+1` — deployed `predict_lout` can return `< iter+1` today).
- Tail: hard-EM self-consistent tail injection (Lynden-Bell-style: actives inject `max(cut, T_glob(cut))`, fixes length-biased early tails).
- Work: total-work estimate with SE (predictive spread + parameter error).
- Rate: generation-decayed arrival rate (median rel err 8.4%, tracks 4x jumps).
- Lull: nonparametric lull-vs-ended posterior on raw gap survival (decisive: 0.96 within-support vs 0.058 post-end on low-variance gaps).
- Trigger: endgame hold trigger derived as optimal stopping (no finite optimal hold exists).

Hold threshold:

```text
HOLD iff p_alive * lambda_dr * C1 > p_crit + rho_S * n_mt
```

- `p_alive` — lull-vs-ended posterior; `lambda_dr` — decode-ready rate; `C1` — singleton round chain time.
- `p_crit` — held-work share of global-last probability; `rho_S = V_S / V_E`; `n_mt` — held emitting count.
- 10000/10000 closed-form matches; degenerate cases no-arrivals / single-request / unknown-`Lout` resolved by the formula itself, documented §4.
- Audit re-verified 2026-08-23 (2nd pass): ALL checks re-pass incl. NEW Check G determinism (3 fresh driver runs, 3000-event script, byte-identical — previously claimed but untested).
- Validation ALL PASS: C++/Python bit-match ~11k values <1e-12; posterior MSE beats plain-EWMA / `max(Lin,ewma)` / deployed-bucket-mean in ALL 4 scenarios (bimodal 17.2k vs 84.1k/87.6k); CI calibration limited by documented pacing-blindness bias (survivor pools over-predicted HIGH — irreducible without pacing features; consumers use ratios).
- Bugs found and fixed during derivation: decayed-sum-vs-mean rate (~50x), missing factor 2 in inverse Mills ratio, variance blending violating total-variance law, EM point-mass second moment faking confidence, harness event stream not time-sorted.
- Size ledger: fragment header 14,239 B minified vs 968 B deletable machinery = NET +13,271 B — DOES NOT FIT artifact budget as-is; fundable tiers spec'd (core ~8.7 KB w/o EM+total_work, recommended ~11 KB w/ EM via `_RENAME` pool); `sizeof(Infer)` = 543,928 B fixed static (memory fine, source bytes binding).
- Files: `SB05_inference.{md,cpp}` + `SB05_check.py`.

### SB06 — batch composition and cloud partition

- Status: DONE 2026-08-23.
- Round period:

```text
P(m, split) = max(2*S + dpre(m) + dpost(m), max_c(S + dproc(c_j)), k*lat + beta*m)
```

- `m` — group size; `split` — per-cloud partition `c_j`; `k` — distinct clouds touched.
- `S` — setup; `beta = 8*bpt/(bw*1e6)` — per-token link cost; `lat` — latency.
- Link term is `k`-dependent.
- Split: DP-optimal split via `T`-threshold partition (binary search over `S+dproc` thresholds + `K x m` DP, balanced-for-monotone shortcut).
- Shapes: 6 table-shape cases (linear / concave / convex / flat / V / mixed) with herd-vs-spread condition including `(k-1)*lat` header and prefill-serialization.
- Hold: `D_POST` hold rule SB04-calibrated.

Hold inequality:

```text
hold iff V_E * G_loc > p_crit * V_E * D + n_span * V_S * D
```

- `G_loc = S + dpost(m0) + dpost(m1) - dpost(merged)` — local time saved.
- `D` — wait; `p_crit` — critical share; `n_span` — span-sensitive count.
- With SLO2 chain vs pipe cap.
- Cloud pick via lifetime cost `T_prefill + Lhat*p(m) + Dc` crowding externality (dominates EFT/JSQ/PoTC per shape).
- Verification: exhaustive 1200/1200 (100% on 6 families incl. V 6,6 herd→balanced transition) + shape + hold + pick sanity.
- Size: fragment 11,342 B minified (shipped 6,750 with `_RENAME`) vs 8,987 B deletable (M2 2,549 + M2b 2,848 + M3 2,174 + M4 623 + M5 793) net +1,500 free with tiers; Tier1 `optimal_split` alone funds `P PRE` cloud choice via M3.
- Files: `SB06_partition.{cpp,hpp}` + `SB06_check.py` + `SB06_partition_math.md` (7 sections, proofs, case table, ledger).

### SB07 — prefill lot-streaming, pipelining

- Status: DONE 2026-08-23.
- Chunking:

```text
k* = ceil(p / (G - S))
```

```text
k_value = sqrt(Vd * p / (Vs * S))
```

- `p` — remaining prefill time; `G` — stall budget; `S` — setup.
- `Vd` — decode value rate; `Vs` — self delay price.
- Combined stall-budget + value optimum.
- Split inequality:

```text
split iff Vd * p / 2 > Vs * S
```

- Cohorts: phase-offset cohorts with 1F1B `D ≈ P_pipe/2` and `r = 1/(1+q)` geometric + grid search (1200/1200 brute PASS).
- Wake: event-backed wake proof for all 5 holds (each cites `cloud_busy` / `down_inflight` / `pipe > 0` with fallback issue-now, invariant I proven).
- Size: fragment 11,793 minified (tiers: prefill-only ~2.7 KB, full ~9.3 KB vs 3,230 deletable `AdaptiveChunk`+accumulate+ladder).
- Artifacts: `SB07_pipelining.cpp/.md/.py` DONE and `-Werror` clean.

### SB08 — endgame / tail + binary-cliff

- Status: DONE 2026-08-23.
- Drain: CPM drain (LRP star, maximal `m_drain` capped by `m_tp`, break-to-singletons).
- Merge rule:

```text
hold iff V_E * G_loc > p_crit * V_E * D + n_span * V_S * D
```

- FD-checked 1000/1000.
- Slack:

```text
s_tdr = (SLO1 * R - sum_tdr_done - sum_est) / n_undone
```

```text
s_tpot = (SLO2 * G_total - sum_spans_done - sum_est) / est_gaps
```

- Reserves: Hoeffding / Bennett reserves with `CliffMode` SAFE classifier (zero violations on 300k adversarial trials: Hoeffding `δ = 0.01/0.001` 0/200k, Bennett 0/100k).
- SAFE contract (prefill absolute, `m_tp` cap, no holds/chunking, SRPT) proven non-increasing via exchange.
- Size: fragment 8,980 minified (7,630 `_RENAME`) vs 2,237 deletable cliff gates — tiered funding spec'd.
- Artifacts: `.cpp/.md/.py` DONE and `-Werror` clean.

### SB09 — artifact engineering

- Status: DONE 2026-08-23.
- Parser: integer-mantissa 9-decimal parser PASS 0 mism > 1ulp on 1.2M (35.8 ns, 5x `strtod`).
- Writer: 1.61x faster (95 ns vs 153 ns, integer-only verified, flush 1.2 us/syscall → 2.6 s for 2M).
- Allocation: audit 19 sites → 0 allocs/frame (14→0, 14 s→0 via harness).
- Size: analyzer 91 blocks top-20 46,567/71% (`State` 7,371) + 10 minify patterns (~3.2K free) + 46 rename candidates (~1.4K).
- Governor: 25 ns/read 0.05 ms/2M (0.0003%) admission `k*c*F < 0.8*B_left` proves <13 s.
- Artifacts: docs + bench + size + harness DONE.

### SB10 — integration blueprint

- Status: DONE 2026-08-23.
- Content: `research/techniques/SB10_integration_blueprint.md` (exec summary + unified 12,678 B deletion ledger w/ overlap reconciliation + phases #0/P0–P8 with per-phase edit lists, byte ladders, V1–V7 verification, CF attribution tables from `HIDDEN_ATLAS` rows, kill/revert rules, calendar to Aug-28 freeze, file map, risk register R1–R14, 6 human decision points; capacity verdict: engine+calculus saturate the artifact ⇒ exactly ONE of {P7 cliff-controller (recommended), P6, P5a-trimmed, P8} fits post-P4).
- Harness: statement-derived harness `SB10_fuzz.py` (zero repo judging code): Example-1 replay byte-identical PASS, det 100/100, loop smoke 500/500, FULL 10000/10000 PASS.
- Stress: density 22.7k frames + NEW `--hard` forced-singleton trap 477,730 legal frames — MEASURED baseline ~10 µs/frame ⇒ ≈20 s extrapolated at the 2e6-frame cap = live TLE exposure the shipped D-161 guard does NOT cover (forced-threshold probe: no change; segment timers decide 4.2 / write 3.4 / apply 1.7 µs/frame).
- Ablation: behavior-neutral `cost()`-memoization validated −17% CPU with transcript-identical output on fuzz corpus → heads P0 edit list with a fast-mode extension for the >1.2M-frame tail.
- Evidence verbatim: `SB10_fuzz_evidence.txt`.

### P4 — exact score calculus landed

- Status: DONE 2026-08-26.
- Scope: per SB10 §1.7 (subagent pair, working tree).
- Oracle trio swapped to SB04-exact forms (`state.hpp`: `marginal_tp_v` one-sided-full at `tp == tp_base` + zero at/above `tp_UB`; `marginal_tdr_v` / `tpot_v` interior now INCLUDES `dist == dist_base` recovery edge).
- NEW exact finite `delay_cost_ppost` / `delay_cost_span` (pooled-mean shift `n*delta/R_hat` resp. `/G_hat`, kink-exact, `dist_base == 0` cliff priced as `p_violation*1000*w_c`).
- `rem_tp` / `rem_c` dead bodies deleted.
- D11 dissolved at `scheduler.cpp` V11 block (`max(2,K)` / `worst_up_lin < 1` → 100 / `2+2K` / half+1 / `0.5*dist_base` → wide `A* = (P_round-K*lat)/be` clamp[1,2000] + narrow tdr-slack budget `max(0,SLO1-tdr_hat)*R_hat/(S+lat)` floor K — kc floor is LOAD-BEARING: without it `adv_living_cliff` self-reinforces admission starvation 179.433→111.147, restored byte-parity with floor).
- D12 (K=1 `w_c/SLO2` unit error + cliff `1000*w_c` jump) verified ALREADY GONE (subsumed by P2 `group_size` rewrite; grep: zero such literals left in `src/`).
- Hold rewritten to SB08 §2 exact inequality:

```text
hold iff v_e_hat * G_loc * util_e > util_c * v_e_hat * D + delayCostSpan(ready, D)
```

- `D = DOWN-tail + S + ppost(ewma_lin)`, replacing the `{8,min(tot,8) >= 2}` target ladder and the `dist_base == 0` `ready == 0` no-op branch (`ready < 1` returns false — wake safety unchanged: hold requires `pipe > 0` ⇒ TDN/XDN pending).
- Terminal R-hat-aware `v_e_hat` added (`fwsim.cpp`: `N_hat = tokens_out + sum_r max(0,predict_lout_r-iter_r)`, `E_hat = (t-earliest)+tail*(3S+dpre1+dproc1+dpost1+2*xfer(1))`, frame-keyed 64-frame cache ⇒ deterministic, `O(R)/64` frames).
- Dead P2 EOQ statics `q_star_thin` / `q_crit_fn` removed; `*RENAME` pool extended (+26 entries `q84..q9k`).
- Verification:
  - FD standalone `research/techniques/P4_fd_check.{cpp,py}` drives the REAL `State::Oracle` — 1075 randomized states (clamp edges + `dist_base = 0` cliff + knife edges + `SLO = 1e9` corners + `G = 0`): max rel err 2.959e-10 (<1e-8 gate), zeros exact (743/921/724/200/267 exact-0 checks incl. cliff-flip semantics), 185 ill-conditioned skips documented.
  - V1 all five targets `-Werror` clean incl. standalone artchk.
  - V2 Example-1 replay PASS byte-identical 11 frames.
  - V3 loop 2000/2000, det 200/200 identical, stress `--hard` extrapolation 17.02 s (pre-P4 16.16 s; standing R2 exposure, raw child CPU improved 4.90→4.04 s).
  - V5 zoo 27/27, verdict sweep 74/74 ok (`[ORACLE FINAL]` MISSING is expected — retired behind `LOCAL_DIAG` by P0 stderr diet), crash 500/500, hostile ALL PASS, san/debug harsh (x3/x5/x6/finstorm) verdict ok clean.
  - Extremes vs `/tmp/sub_pre_P4`: x1 304.492==, x9 388.794==, x5 1000==, x6 508.247==, `living_cliff` 179.433== (post-kc-floor; NOTE pre-existing P1–P3 tree level 179 < the D-154 733.884 discriminator — flagged for coordinator, NOT a P4 delta).
  - V7 margin 689 (64,846 chars; ledger: −330 `rem**`, −230 EOQ statics, +510 delay costs, +470 horizon/`v_e_hat`, +140 hold rewrite, +90 D11 net, renames +~740 back).
- Signature stability: `projected_score(...)` call sites untouched, `marginal_*` signatures unchanged, additive-only methods, deterministic (frame-keyed cache, no RNG/unordered iteration), doubles audited for 1e9-SLO/1e12-time scale (squares ≤1e24).
- CF attribution when probed: watch #3/#14/#21 (knife-edge + tight `dist_base`), #19/#22 via terminal horizon only; KILL/REVERT total ≤ −5 or any row −40 or #3+#7+#14 combined −20 ⇒ revert to pre-P4 snapshot (`/tmp/p4_pre_snapshot` = P3 tree; git HEAD remains `a40d6f3`).

---

## P7 — computed binary-cliff controller (2026-08-26, final phase)

Status: LANDED on the working tree (artifact `src/submission.cpp` 65,480 B, margin 55).

Replaces D8 (traffic-aware cliff flip + `!cliff_feasible`) and D9 (`compute_alpha` `dist_base == 0` min-TDR early-out) with the SB08 §3–§4 computed slack controller (`fwsim.cpp cliff_refresh`, `δ = 0.01` Hoeffding reserve, doom valve) + SAFE behavioral contract (prefill priority structural, holds off via `g_force_no_hold`, single-piece chunking, SRPT pop via `cliff_srpt`, decode gap-feasibility threshold swap in `MarginalBatch::group_size` via `g_cliff_sr`).

Trims fired (byte ladder):

1. tpot-side SAFE _trigger_ dropped (tdr-only latching); tpot protection kept continuously through the `g_cliff_sr` threshold, gated behind its own Hoeffding thinness line (`pC < rf*1.517427/sqrt(gl)`) after mechanism tracing on `adv_living_cliff` showed an ungated `pC < SLO2` cap tightens batches every frame.
2. Not fired: fold-SAFE-into-rollout, SRPT drop (not needed at margin 55).

Verification (all on final artifact):

- V1 `-Werror` matrix + c++23 standalone PASS.
- V2 Example-1 replay PASS byte-identical 11 frames.
- V3 loop 1000/1000, det 200/200, stress-hard extrap 17.65 s (standing R2 baseline 18.58 s ±10%).
- V5 zoo 27/27 verdict-only, crash 500/500, hostile ALL PASS, san/debug harsh (x3_supertrap/x5_void/x6_horizon/finstorm) verdict ok clean.
- V7 POSIX grep 0.
- Property harness `P7_property.cpp` ALL PASS (A slack-boundary FD 500/500 <1e-9; B1 tdr exchange dominance 600/600; B2i pick-time gap-feasibility invariant 19,812/19,812; B2ii identity with uncapped argmin when cap non-binding 19,812/19,812; C doom valve 200/200).

Behavior A/B vs `/tmp/p7_pre_snapshot` (transcript sha, NOT scores):

- 92/95 byte-identical across zoo+adv+mimics; the 3 differing transcripts are exactly the designed blast radius (`dist_base == 0` ∧ `w_c > 0` rows: `living_cliff` bounded ~90-frame SAFE episode; `falsehope` / `x2_falsehope` already-zero rows, same 0.000 outcome).
- No scoring evidence used anywhere (pre-freeze §0.8).

CF attribution plan recorded pre-freeze: part of a subsequent single human submission as planned at the time; kill rule — any `w_c > 0` row −30 or total Δ≤−5 ⇒ revert P7 alone: `cp -r /tmp/p7_pre_snapshot/src . && make submission`.
