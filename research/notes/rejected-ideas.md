> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). This registry is preserved as the historical record of ideas measured and rejected pre-contest. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Rejected-Ideas Registry

Why this exists: the #1 documented failure mode in this project is re-proposing an
idea already measured and rejected (a fresh session re-derives "decode-first" from
first principles and burns a day validating it). Before proposing any heuristic or
ordering change, grep this file. If the idea is here, the burden of proof is on NEW
case evidence, not re-justification.

Rules:

- Only fully-measured negative results are listed (measured on a real pool, not a hunch).
- Each entry: the idea, the exact build/flag, the measurement, the mechanism (why it lost), the reopen condition.
- A reopen needs concrete new cases where the mechanism no longer applies — not a re-argument.

---

## 1. DECODE_FIRST: decode-ready before WAIT prefills in lookahead ordering

- **Build**: `-DLOOKAHEAD -DDECODE_FIRST` (candidate sort: DW-before-WAIT).
- **Measurement**: 10k pool (seed 7): +1883.8 net vs baseline, vs +3800.7 for the winner; fails 331 vs 164; new −9 regressions on burst/mixed classes; suite +0.0.
- **Mechanism**: fixes the tick-39 corner (DW req loses to WAIT prefills, `3P 4P` vs `1D 3P`) but starves SJF prefill packing — decode-first hoists single-token decode actions over prefill starts, so short-job prefills slip later and throughput drops. The DW-vs-WAIT tie-break is a real corner but the global reorder is the wrong lever.
- **Reopen condition**: an action-candidate mechanism that breaks the DW-vs-WAIT tie LOCALLY (same capacity class, no global reorder) with a measured subset, or new cases where prefill packing is unaffected (e.g., decode-starved edge graphs).

## 2. Containment displacement (CONTAIN)

- **Build**: `-DLOOKAHEAD -DCONTAIN`.
- **Measurement**: +2.0/+3.0 initial gains were ARTIFACTS of a state-leak bug (simulated bookings leaked into real containment decisions via global owner maps). After snapshot/restore fix (`sim_scope`): +1.0 == plain LOOKAHEAD (neutral).
- **Mechanism**: containment displacement buys nothing once simulation is honest; lookahead already captures the value.
- **Reopen condition**: evidence that displacement of an owner creates slack that greedy admission misses, demonstrated on a case where CONTAIN beats LOOKAHEAD by ≥ 1 point in the ASSERT_STATE build.

## 3. Pure SJF admission alone

- **Build**: `-DADMIT_SJF` without keys/lookahead.
- **Measurement**: loses 1 on gen_burst_03 (suite).
- **Mechanism**: SPT admission alone misorders when runtimes are equal-ish; needs the SLAI decode key + lookahead context to pay off.
- **Reopen condition**: new cases with heavy short-job concentration where baseline SPT-invariant loss is reproduced.

## 4. SLAI decode ordering alone

- **Build**: `-DKEY_SLAI` without lookahead/admit-SJF.
- **Measurement**: neutral (suite).
- **Mechanism**: SLAI needs the lookahead context to be informative; alone it's a no-op reorder.
- **Reopen condition**: none currently — part of the winner stack, only standalone use is rejected.

## 5. rho-gates other than 1.0

- **Build**: `-DLOOKAHEAD -DRHO_GATE=0.98/0.995/1.02` variants.
- **Measurement**: all lose on the 20-case suite with the current keys.
- **Reopen condition**: recalibrate only after a pool shows a rho-boundary failure cluster.

## 6. Blanket IMBAL_GATE (disable LOOKAHEAD, not just keys, on imbalanced graphs)

- **Build**: `-DIMBAL_GATE` gating the whole strategy on 3E<C.
- **Measurement**: −18 on gap class, +52 vs +76 on dev — gating keys only is strictly better.
- **Reopen condition**: gap-class evidence that the lookahead core itself (not the keys) loses on imbalanced graphs.

## 7. Ungated SLAI/SJF keys (no imbalance gate)

- **Build**: `-DLOOKAHEAD -DKEY_SLAI -DADMIT_SJF` (previous winner).
- **Measurement**: +2.0 suite but −22 on the adversarial gap 20-set; keys gamble on runtime info that backfires on ≥3x-imbalanced graphs.
- **Mechanism**: on starved graphs, SLAI/SJF keys misorder decode vs prefill against the greedy-feasible alternative.
- **Reopen condition**: the gate trade is documented (keys net-positive even on starved graphs); only a confirmed spec change to runtime visibility or a new failure cluster reopens it.

## 8. Time-shift-equality as a metamorphic invariant

- **Build**: naive equality `score_shifted == score_orig` in tools/metamorphic.py.
- **Measurement**: EVERY solver fails it — because the score's max(completion) term is absolute, not because solvers are translation-sensitive. Correct invariant is `score_shifted == score_orig - s*1e-6`.
- **Mechanism**: score model subtlety, not a solver defect.
- **Reopen condition**: if the real spec's V turns out relative-only, the invariant goes back to plain equality (and the score model changes).

## 2026-08-05 — candidate prioritization for the lookahead loop (EVT_CAND_POLICY)

- Hypothesis: with limited sim budget, ordering candidates by urgency/contention/impact before the greedy-sequential accept should beat the mixed SLAI/SJF/cmp_key ordering.
- Data: urgency-first +13 dev but −57 burst (pool1500, 90 regressions), −7 gap, −11 big; contention-first −58/−100; impact-first −129; hybrid −29/−73. All policies equalized budget (~equal episodes/sim_work).
- Mechanism note: the accept loop commits accepted actions to occupancy, so candidate order biases which decisions get made — the control ordering already balances SLAI (DW-side) vs SJF (WAIT-side) vs cmp_key; urgency-first over-prioritizes the WAIT-side gamble and damages burst clusters.
- Reopen only with new case evidence showing a class where generic ordering fails (e.g., a dedicated burst-heavy pool with >1 regressions of the control vs urgency).

## 2026-08-05 — sim-side copy elimination (sim_reqs = base_reqs per candidate)

- Hypothesis: the lookahead loop's per-candidate `sim_reqs = base_reqs` copy dominates select() wall time; eliminating it via shared mutable sim + journal rollback wins big on wall.
- Measurement (EVT_REPORT_STATS, cap4, n1000_t14000): 566 candidate sims, 5.8 MiB total copies = ~3-5 ms ≈ 1% of select 414 ms. Sim machinery (3×O(n) full-scan passes per tick in sim mode, 35696 ticks, 249 ms = 60%) is the real cost. Rollback already negligible (0.2 ms).
- Reopen condition: ONLY with new measurement showing copies >5% of select (e.g., if the real judge's cases have far larger N or if the sim were converted to active lists, making scans cheap and the copy relatively large). A "shared sim + journal rollback" project without copy-dominance evidence is Day-2-risk with ~1% payoff.

## 2026-08-06 — Drop the `Ent` struct, sort `cand` in place via `cmp_admit_key` (sim path)

- Hypothesis: the sim-path admit sort builds a `vector<Ent>` per tick (`ents.reserve; for cand: ents.push_back({id,sjf,D,usa1,A}); sort(ents, ent_cmp); cand.clear(); for ents: cand.push_back(e.id)`); replacing with a single in-place `sort(cand.begin(), cand.end(), cmp_admit_key)` eliminates the struct-build + write-back passes.
- Parity: byte-identical on suite + 5 gap cases (keys/D/usa1/A all immutable post-parse → reqs[a] == sim_reqs[a] for the admit-key fields). The behavior IS identical — this was a pure-speed test.
- Timing (n1000_t14000, interleaved min-of-6): asort_frozen min 1.557 s / median 1.630 s / mean 1.623 s; ents-skip min 1.579 s / median 1.655 s / mean 1.643 s. **ents-skip is ~20 ms SLOWER** (mean +20, median +25, min +22). The pack-of-Ent sort has better data locality (contiguous {id,sjf,D,usa1,A}) than the cmp-driven `reqs[id].sjf_key` indirection through random reqs[] positions. Random access in `cmp_admit_key` blows the cache that the packed Ent loop keeps warm.
- Reopen condition: ONLY if reqs[] becomes sorted-by-id-then-key (i.e., cache-friendly) OR if the field-set grows enough that the Ent struct stops fitting a cache line AND the sim admit sort remains the dominant cost. Otherwise the packed-Ent variant IS the optimization; do not strip the struct without cache measurement.

## 2026-08-06 — Phase 3 (sim incremental sort / sim-owned active lists) — DEFERRED

- Day-6 profile of asort+inflight shows: sim admit full sort (a_sort) = 76 ms = 28% of select on n1000_t14000 (still the single biggest remaining cost). Real-path admit sort already eliminated by asort; real-path advance_all already swept by inflight. The only remaining lever is converting the SIM path's machinery (admit sort, grant scan, advance no-op visits) to a sim-owned active-list structure (a `sim_wait_sorted` with O(log n) insert/remove), backed by journal/rollback so per-candidate simulation doesn't leak.
- Decision: **DEFERRED** — freeze stands at 1.51 s min vs 3.0 s official (2× margin); the ~7% wall gain (~100 ms incl. sim scans) is small, and Phase 3 inherits the full DW re-entry / `seen` dedup / journal-rollback class risk that produced the project's worst bugs on Day-2/3 (mixed-WAIT-DW non-transitivity, C_ED → DW re-entry, phase=DE 10.9992 duplicate-emission bug). Estimated payoff (~100 ms) doesn't justify the risk profile until the real spec's judge budget is known.
- Per EXP-31 decomposition retained (cycle numbers, @2.6 GHz, n1000_t14000, post-asort+inflight profile): rtp_all 200 ms (75% of select), a_disp 146 ms (54%, a_sort 76 ms + ents-build+dispatch ~70 ms), sel_total real fallback 103 ms (38%, cand avg 27 — already small), adv_all 32 ms (12%, inflight sweep pays off), g_sort 8.9 ms. Note: 100% of remaining sim cost is bounded by sim_work cap (1.8e9) — episodes exhausted early; subsequent ticks route to the cheap real fallback. Sim episodes run only whiles sim_work remains → that's where a_sort lives.
- Reopen condition: (a) the real spec shows the judge budget contracts (≤ 1.5 s for n>1000 cases) OR (b) Day-2/3 active-list infrastructure is reused from real path → sim rollback can be a pure wait_sorted deltas on top of the journal with low marginal risk. Otherwise the freeze is the exit point for the wall-time arc.

## 2026-08-13 — SLO-MAEL aging tiebreak (AGING, opencode port) — REJECTED with measurement

- Idea: opencode's SLO-MAEL aging = `age = sla + AGING_W*A` as a tiebreak in the decode comparator (oldest-first among equal-sla candidates), per the paper's argument that cold-queue requests starve under pure urgency scheduling.
- Measurement (suite, vs dev base): **−1.0 on gen_burst_02** (13.999398 → 12.999466), +0.000044 on gen_burst_00, all other 18 cases 0 → class_fail=['burst'], invalid gate. Sanitizer/crash-clean but score-losing.
- Mechanism: the frozen comparator already falls through sla → D → usa1 → A; interposing the age key (≡ A-component) BEFORE D means the deadline can no longer resolve equal-sla decode ties, and burst_02's decode-critical dispatch pays 1.0. The paper's aging presumes a homogeneous decay model without our per-request deadline/urgency stack — it is subsumed by the existing D/usa1 keys.
- Reopen condition: new case evidence of a cell where equal-sla decode ties systematically starve old requests AND the D-first tiebreak demonstrably causes it (e.g., a decode_saturated cell where sla ties are pervasive and age-differentiated). Any reopening must place aging AFTER D/usa1 (last-resort tiebreak), where it degenerates to the existing A key — so realistically: reopen only if a regression case shows A-ascending itself failing.

## 2026-08-13 — Per-episode candidate-order SA (ENGINE_LNS, opencode port) — neutral, kept dormant

- Idea: opencode's LNS-style SA over the per-episode candidate order to escape the greedy rho-gate's local ordering.
- Measurement: byte-identical everywhere (suite 20 / dev 201 / gap 20 / large-cell 40: ±0.0000..0.000017, one 1e-6-level delta), deterministic, valid, budget-gated runtime, sanitizer/crash-clean. First version (−6.999635) was a genuine SA bug (final-not-best perm + T0=4n²) — fixed to best_perm + T0=4n.
- Mechanism: cand sets ≤ 8 (often ≤ 1) and already key-sorted; the greedy rho-pass is order-flattening in practice. The SA has no reachable improvement surface at this granularity — it would need to perturb the full schedule (admit/deny + dispatch decisions over the horizon), not per-episode order.
- Status: NOT promoted (no gain), KEPT as compile-time portfolio flag for spec-day objective retargeting (score_probe-driven; if the real score function is relative/baseline, the SA objective can be re-targeted to the gap without touching the search machinery).
- Reopen condition: real-spec cells where per-tick candidate sets are large (K≫8) and the greedy order is provably suboptimal — OR convert to schedule-level perturbation (much larger change, only with a confirmed multi-objective score).

## 2026-08-13 — SLO-MAEL admission priority (ENGINE_MAEL, opencode 4.4) — REJECTED with measurement

- Idea (faithful paper port): `effective_priority = 1/max(1, D-A-min_lat) + VIOL_W*max(0, t+min_lat-D) + AGE_W*age`, WAIT-side admission ordering only (decode kept SLAI — aging-on-decode tiebreak already rejected above); age += 1 per tick not admitted, reset on P dispatch. Defaults AGE_W=0.1, VIOL_W=1.0 (opencode 4.4), muted variant AGE_W=0.01/VIOL_W=0.1.
- Measurement (vs dev base): suite 20 **−9.999764** (gen_burst_02 −8.0, gen_burst_00 −2.0, class_fail=['burst']); zoo 14 −8.0 (reverse_sorted −6.0, decode very close); dev tight 32 −2.0. Muted weights still −8.0 (the 1/slack base term alone inverts SJF within burst clusters — long-chain requests win admission and block the cluster; age/viol terms are second-order).
- Mechanism: the frozen admit ordering (SJF → D → usa1 → A) is the structural winner on this problem's burst/contention cells (consistent with the Day-5 finding: "urgency-first −57 burst" — control's SJF-first balances the WAIT-side gamble). SLO-MAEL's base-priority term is anti-SJF (longer chain → tighter relative slack → higher priority) and the lookahead greedy commits those choices; on reverse-sorted arrivals the t-dependent violation term injects tick-sensitivity (−6.0). No cell showed a win — not even decode_saturated (the greedy already prevents starvation via SLAI urgency + doom-marking; aging buys nothing there).
- Reopen condition: real-spec evidence of a decode_saturated cell where long requests STARVE under the frozen solver (measured SLO violations concentrated on max-Lout requests) — then reintroduce aging limited to the decode side with the D-first key stack intact, or as a pure last-resort tiebreak after A. Without starvation evidence in a real case, SLO-MAEL is dead: its base term demonstrably fights the burst regime our pool is built around.

## 9. ENGINE_SLAI_DYNAMIC — full SLAI port (criticality + dynamic offset + token budget)

- **Build**: `src/Solution_slai_dynamic_candidate.cpp` (frozen line + gated code),
  `-DENGINE_SLAI_DYNAMIC -DSLAI_DYNAMIC_THETA={0,1,2,5}` (alpha 0.3, beta cap
  parameterized, default no-op). Port per SLAI paper (2508.01002 §6):
  lst = s_{L-1} + tbt - Θ·t_batch_avg with per-request EMA of inter-dispatch
  gaps (the only observable congestion proxy in a per-tick protocol), tbt =
  (D-A)/(Lin+Lout), critical iff t >= lst, crit-first ordering, β cap.

- **Measurement (vs frozen, ab_test)**: suite +0.000000/-1e-6; gap +0.0001;
  big_scale +1.0; decode_saturated +23.0; prefill_heavy +7.0; tight ~0;
  burst_displacement −6.0; dribble −48.99; comm_displacement −506.87.
  class_fail=[burst, dribble, comm]. Determinism 3/3; ASan/UBSan+ASSERT 300/300.

- **Mechanism**: the margin is STRUCTURALLY INERT — t_batch_avg is a global
  EMA, so Θ·t_batch_avg shifts every lst by the same constant and cannot
  change any ordering (th0 ≡ th2 ≡ th5 byte-identical on every pool). The
  β token budget can never bind: per-tick dispatch is already capped by
  min(E,C)=3, the protocol's own token-budget equivalent. The entire delta
  comes from the key change lst = s+tbt (per-token deadline) vs frozen
  D − tokens_left·per (latest-start EDD): the per-token spread key
  misorders decode under communication pressure — same failure family as
  estfix's comm displacement (EXP-41) and dribble (EXP-48): deadline-spread
  keys destroy throughput when COMM dominates.

- **Paper-correct notation (do not reopen on the wrong premise)**: the SLAI
  offset is Θ (a constant; the paper's "dynamic offset" variant is a 2-level
  memory-driven switch Θ∈{5,10}, not an EMA-adapted margin); δ in the paper
  is the GeMV output vector; β is the key-projection output. The paper's
  τ̄_batch (GPU batch execution time) has no observable counterpart in a
  discrete-tick protocol — the inter-dispatch-gap mapping is itself an
  assumption, and the experiment shows the margin cannot act anyway.

- **Reopen condition**: spec evidence of variable per-token decode costs
  (context-length-dependent hop times) AND a margin mechanism that changes
  per-request order (per-request τ̄, not a global EMA), validated on a
  fresh pool. Absent that, KEY_SLAI's static lst remains the winner.

## 10. ENGINE_REFUSE_EARLY — queue-aware admission refusal (reviewer Engine 7) — REJECTED with measurement (EXP-53)

- Idea: replace the queue-blind admission est with est + queue_delay (sum of in-flight remaining work / drain capacity), refuse early when queue-inclusive completion > D. Claimed the frozen solver "admits chain-feasible requests then strands them" (the estfix mechanism).
- Measurement (EXP-53, 2026-08-13): control (accurate est alone on the frozen line) is byte-identical to frozen on all 7 matrix cells (+0.000000) — the estfix stranding was never caused by the admission est. The queue term alone (refuseE − refuseC) loses EVERY cell: big_scale −33, burst −91, comm −772, decode_saturated −10, dribble −220, prefill_heavy −43, tight −9; gap 20 −22; suite ≈ frozen (−2e-6).
- Mechanism: frozen's conservative est (~1/3 overestimate over the phase-pipelined chain) ALREADY contains the queue bound — that overestimate IS why frozen wins dribble (24 on_time vs estfix 12, EXP-48). An explicit queue term double-counts the backlog → over-refusal, worst on COMM-heavy cells (max bubble ⇒ max backlog sum ⇒ max qdelay). The reviewer's "sum of remaining processing times" is the wrong queue model for this pipeline.
- Reopen condition: (a) an occupancy-walk chain-fit (exact earliest-completion against current edge/cloud windows — NOT a backlog sum), AND (b) a measured case class where frozen strands under its own est (none found in 7 cells × 40 + suite + gap), AND (c) fresh-pool revalidation. Without (b) the premise itself is unobserved.

## §11 — ENGINE_8' tuning variants rejected with measurement (EXP-55, 2026-08-13)

- **EST_SEL_ADMIT_ONLY (gate flips admission est only, doom stays accurate):** comm_displacement −84.998, dribble −61.0 — the doom/decode est is the driver of BOTH estfix veto failures, not the admission est. Doom sites must flip with admission sites.
- **ESTSEL_DOOM_MARGIN (pre-fire accurate doom + margin m∈{2,4,6}):** dribble −24..−25 (worse than base −10) — margin costs the pre-fire accurate wins without removing the strands.
- **ESTSEL_ADMIT_RUN1 (naive admission while trickle run ≥ 1, pre-gate):** suite −1.0, tight +1033 (vs +1093), decode_saturated +73 (vs +160), dribble −11 — accurate admission is the value engine; constraining it on any run≥1 moment (which occurs accidentally in spread/cluster cells) poisons every win cell. Reopen only with a detector that fires only on the full 15-arrival evidence AND a win-cell case that strands under accurate admission.
- **Trickle W window below 15 (10/12/13/14):** dribble −15/−12/−18/−19 (knee at 15) — non-monotone evidence-window tradeoff; nothing to gain.
- **Any-multi-tick reset (W=25, k>1 resets):** false-fires on gen_burst_02 (suite −5.0) — k≥3-only reset + MAXGAP=2 is the safe combination (early-k2 −0.0001 vs −48).

## §12 — ENGINE 9 interference batching: dormant-by-decision (EXP-56, 2026-08-13)

Rejected as an ACTIVE engine pre-spec: no interference in protocol/protocol.yaml or the mock statement (cost model = fixed per-request constants). The reviewer's cost(prefill|decode-mixed) = pe·(1+α·n_decode) formula is literature-motivated, not contract-traceable. Kept as a dormant hook (`-DENGINE_INTERFERENCE`, flat IF_ALPHA/IF_BETA, default 0 = byte-identical): Day-1 triggers = spec text with coefficients (→ calibrate, re-gate) or spec text with composition dependence (→ extend signatures with spec'd state). Reopen ONLY with spec evidence; parity-proven dormant (suite 196.9916 exact).

## §13 — ENGINE 10 time-budget batching: rejected as active engine (EXP-57, 2026-08-13)

FairBatching-style time budget (a + b·new_tokens + c·context) rejected: (1) the lookahead work ledger it would replace is a solver-CPU safety budget, not a token/batch budget — replacing it breaks the timing-regime discipline; (2) the protocol has no context/new-token dimensions and no attention model — coefficients untraceable to contract.yaml; (3) batch cost in this model is token-capacity-exact per tick. Reopen: Day-1 ledger-charge refit (rdtsc-measured) as a coverage experiment AFTER official limits are known; never before.

## §14 — Temporal disaggregation (EcoServe, reviewer INSIGHT 2): refuted with measurement (EXP-61, 2026-08-13)

-DTEMPORAL_DISAGG (decode dispatch gated while any prefill work pending): loses every measured cell (suite −4, tight −9, decode_sat −57, prefill_heavy −50, dribble −32, burst −10). Premise absent: no interference term in the protocol, so nothing to eliminate; only decode starvation remains. Reopen only with a spec-declared interference model (then TOOL 7 + ENGINE 9 hook apply, and TD becomes a policy option ON TOP of calibrated α/β — not instead of nothing).

## §15 — T-clamp gate variants rejected in EXP-64 (2026-08-13)

- **[0,6]-hardcoded-window clamp gate:** count requests with D==T and A+dur-T in [0,6] at arrival; fire on first match. Fires 15/40 tight cases (estfix's deep-Lout late admissions are the +1093 mechanism!), prefill -63, big -49, burst -4: net -173 vs +14 on the clamp cell. The window bound is Lout-dependent (chain_remain savings = 4+3*Lout; dur - savings), a fixed 6 both misses deep-Lout strand cases (loose_01: no fire, -5 remains) and over-fires shallow cells. Reopen only with a per-request chain-remain-exact window AND a per-case threshold calibrated on capacity pressure, not just presence.
- **0.5 clamp-fraction gate (fraction of D==T requests):** burst cell has 17/40 cases at 0.50-0.69 final fraction — would flip 17 burst cases, estfix wins there (+18.994 aggregate). 0.75 threshold adopted; separation evidence: burst max 0.69, clamp cell min 0.93.
- **Post-parse fraction check:** gate decision after the tick loop is TOO LATE — the protocol is online (per-tick reads fused with scheduling), so the first T/3 of the case already ran with accurate est. Any gate must decide at arrival time (the [0,6] gate survived only because it fired at arrival).
- **Reviewer's own detector (D < A + dur*0.95):** targets rho < 0.95 = the TIGHT cell, not clamp; would kill +1093. Never built.

## §16 — Burst-tail strand detectors refuted in EXP-66 (2026-08-13)

- **Blanket late-cluster gate ("overfull in an 8-tick span >= 3"):** overfull = D==T && A+dur>T. Fires on 28/40 tight, 21/40 big, 25/40 prefill cases — the late overfull admits ARE the accurate-est WINNING mechanism in those cells (they complete: spare pipeline). 91/280 regressed (tight_000: 34.999 -> 32.999). NET disaster; reverted (byte-identical rebuild vs g9 verified).
- **Conjunction gate (max-overfull-4 >= 2 && decode_load >= 0.85):** fires 120/514 cases — ~100 winning burst/bpool cases (burst_000: load 1.06, max4 2, estfix +2.001; pool_003: 0.94/3, +7.000) + 5 gap. Feature-identical winners exist for every loser; decode_load does not separate. REFUTED by fire-list enumeration over 514 cases (matrix 280 + bpool 120 + t_clamp 20 + pat/pat2 20 + clamp_mid/high 24 + gap 20 + suite 20 + decode_veto...).
- **General lesson:** the burst-tail strand class (3/514 cases, -8..-12) is prediction-blind at parse: clamp frac, overfull clusters, decode load, arrival span, T all fail independently and jointly. The class is documented (EXP-66) with a day-1 contingency (portfolio re-run on the real sample distribution when burst-tail patterns appear at >= ~2%); no parse-time detector may be added without a reproducing generator cell (regime-reproduction rule) — the two hand-built clone cells (b010_cell, b250_cell) did NOT reproduce the loss (+35.995/+15.995), demonstrating the failure signal needs the full matrix_gen bursts distribution.
