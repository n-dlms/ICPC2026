> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 502 — CrossPipe: Optimal Pipeline Schedules for Cross-Datacenter Training

- arXiv: 2507.00217, "CrossPipe: Towards Optimal Pipeline Schedules for Cross-Datacenter Training", Chen, Kubicek, Huang, Hoefler (ETH Zürich, SPCL). USENIX ATC 2025.
- `research/papers/2507.00217.pdf` / `.txt` fetched OK. Code: https://github.com/spcl/crosspipe.

## Claim

A latency- and bandwidth-aware pipeline performance model for synchronous LLM training across geographically distributed datacenters, framed as a constraint-optimization (CO) job-scheduling problem over a DAG of `{F, D, W}` blocks and communication ops with alpha-beta delays.
CrossPipe generates **optimal** (CO-solver, Section 4.1) or **near-optimal greedy** (CrossUDSub, Section 4.2) cross-DC pipeline schedules, reducing training time by up to **33.6%** vs traditional 1F1B/ZBH1/ZBV under identical memory; under relaxed memory it approaches the efficiency of a zero-delay single-DC ZBV schedule (Section 7.1, Table 5). A **two-layer abstraction** (block scheduling vs communication arrangement, Section 6.3) decouples scheduling logic from the runtime, enabling hot-swappable dynamic schedules in Megatron-LM.

## Relevance

The contest's U-shaped edge–cloud split is a 2-site pipeline with cross-site communication on every prefill→cloud→decode round trip. CrossPipe is the closest published analogue of *exactly* that structure — a synchronous pipeline across low-bandwidth/high-latency links — and contributes three things directly reusable:

1. **The bubble-as-first-class-variable model.** CrossPipe measures bubble ratio = idle fraction per device and shows that under cross-DC delays the bubble grows linearly with `nmb` cross-DC hops on the critical path (Section 3.4, Figure 6: "A 1F1B path contains `O(nmb)` cross-DC communications"). This is the same quantity the contest calls "idle bubbles between edge and cloud" and is the explicit Day-7 anti-failure target. The performance model (Section 3.5) — `start(o) = max(prev-stage-end, dep-end + ℓ_c + T_bw)` where `T_bw` is bandwidth-aware via an occupancy model — is the formal version of the chain-sum feasibility estimate we already use (`est = t + pe + COMM + pc + COMM + Lout·per`), but generalized to a multi-op critical path with explicit bandwidth queuing. The chain-sum admission gate in `src/Solution.cpp:135` is the 2-op special case of this model; CrossPipe's bandwidth-occupancy `BW_model(Tready, src, dst, Tbw)` (earliest free window of length `Tbw` after `Tready`) is the next refinement when multiple in-flight requests contend for the same edge↔cloud link slot.

2. **The two-layer abstraction maps to a `strategy_engine.h` interface.** CrossPipe separates *block scheduling* (which computation runs when) from *communication arrangement* (non-blocking send/recv with delay-aware reordering). Our per-tick `select_actions` + `grant_decodes`/`admit_prefills` is already an instance of that split — the schedule logic picks admit/grant; the (future) communication layer decides arrival times. Making the seam explicit is the port-plan target, not a wholesale rewrite.

3. **The wave-vs-unidirectional crossover.** Section 5.1 / Figure 8: Wave schedules win at low latency (`Tlat/TF ≤ 0.5`), UD schedules dominate as delays grow ("UD schedules become superior as delays grow"). Loop schedules have the highest delay sensitivity (6 cross-DC comms/microbatch vs 4 for Wave, 2 for UD). Translated to the contest: when COMM is small relative to per-stage compute, a bidirectional "ping-pong" admit/grant pattern is fine; under the high-latency edge↔cloud regime (the expected contest regime, per `comm_bound` in Engine 0), a unidirectional ordering that minimizes cross-DC round trips should be preferred. The ZeRO-style "WGrad split vs unified backward" finding — finer block granularity reduces bubbles with negligible overhead — encourages sub-token or sub-stage scheduling when the protocol permits (currently quanta are fixed).

## Algorithm sketch

### Optimal (CO formulation, Section 4.1)

```
Sets:       M (microbatches), S (stages), T={F,D,W} (block types), K (chunks)
            P (compute ops (s,k,t,m)), C (comm ops), O = P ∪ C
Inputs:     d_o (duration; bandwidth-time for c∈C, compute otherwise)
            ℓ_c (latency for c∈C), m_p (net mem change after p), M_s^max (mem cap)
            Pred(o) (immediate data-dependency predecessors)
Decision:   t_o ≥ 0 (start of o), x_{o,o'} ∈ {0,1} (order on shared device/link)
Objective:  min  t_last(0) + d_last(0) − t_first(0)         [makespan on stage 0]

Constraints:
 (DataDep)  ∀o∈O, ∀p∈Pred(o):
              t_o ≥ t_p + d_p + ℓ_p           if p ∈ C
              t_o ≥ t_p + d_p                  if p ∈ P
 (NonOverlap) ∀o,o' sharing device/link:
              t_o + d_o ≤ t_{o'} + H(1 − x_{o,o'})
              t_{o'} + d_{o'} ≤ t_o + H·x_{o,o'}        (H = horizon bound)
 (Memory)    ∀s∈S, ∀q∈P on s:  Σ_{p∈P, dev(p)=s} m_p · u_{p,q} ≤ M_s^max
              where u_{p,q}=1 iff p completes before q starts
 (μbOrder)   ∀o,o'∈P same stage s & type t, mb(o)<mb(o'):
              t_o + d_o ≤ t_{o'}             [cuts search space]

```

Solved with CPLEX (CO) — scales to 64 stages; MILP (Gurobi) OOMs at 32 (Appendix D.1, Figure 14). The order variables `x` implicitly resolve link contention and the scheduling order of cross-DC comms — no separate scheduler needed.

### Greedy `CrossUDSub` (Section 4.2, Algorithm 1)

```
Input: T_F,T_D,T_W per stage; M_F,M_D,M_W; M_L per stage; α,β comm matrices
Block-splitting: each F/D/W block divided into n_sub sub-blocks (finer granularity
                 absorbs bubbles; cost negligible vs iteration time)

Init:  for i in [n_mb]: add F_i (stage 0) to S_0's schedulable set
loop:
  cur ← next_stage_to_schedule()    # stage with earliest schedulable time
                                     # = max(last-scheduled-end, earliest-dep-avail)
  if no stage schedulable: break
  p_cur ← highest-priority schedulable op on stage cur   # warm: F; steady: F⇄D; cool: D>W
                                                         # if mem blocks F/D, schedule W
  schedule the NEXT SUB-BLOCK of p_cur on cur
  if p_cur.type == D and no remaining sub-blocks:
      add W(p_cur) to S_cur's schedulable set
  let p_next be the op depending on p_cur (across the DC boundary)
  if p_next exists and p_cur has no remaining sub-blocks:
      T_lat ← α[cur, next]
      T_bw  ← β[cur, next] · Msg_Size
      E_bw  ← BW_model(p_cur.T_end, cur, next, T_bw)   # earliest free window of
                                                       # length T_bw on link cur→next
      p_next.T_avail ← E_bw + T_lat
      add p_next to S_next's schedulable set

```

Complexity: `O(n_mb² · n_sub² · n_PP · log(n_mb·n_sub))`; in practice ≈ `O(c · n_mb · n_sub · n_PP)` with small `c` (schedulable set per stage stays small). CrossUDSub matches CrossUD (the solver optimum) in most delay regimes (Section 5.1).

### Schedule regime table (Section 5.1, Table 3 + Figure 8)

| Schedule   | WGrad   | Cross-DC comms/μb | Best regime | Memory | DP overlap |
| ---------- | ------- | ----------------- | ----------- | ------ | ---------- |
| 1F1B       | unified | 2 (UD)            | low-delay   | medium | medium     |
| IV1F1B     | unified | (Loop)            | low-delay   | med+   | medium     |
| ZBH1       | split   | 2 (UD)            | low-delay   | medium | low        |
| ZBV (Wave) | split   | 4 (Wave)          | low-delay   | med+   | low        |
| CrossUD    | split   | 2                 | high-delay  | medium | (CO gen)   |
| CrossUDSub | split   | 2 + sub-block     | high-delay  | medium | (greedy)   |
| CrossWave  | split   | 4                 | low-delay   | med+   | (CO gen)   |
| CrossLoop  | split   | 6                 | never-best  | med+   | (CO gen)   |

**Headline regime result:** UD-schedules win as delay grows (fewer cross-DC hops per microbatch = less critical-path amplification); Wave wins at low delay (better overlap); Loop is strictly dominated under delay. WGrad-split (B=D+W) strictly improves unified-B because finer blocking granularity fills sub-block bubbles with negligible cost.

## Port plan

Write-up only (per spec-not-public policy). Concrete shape if a cross-DC-aware engine is built for the contest:

- **Map the two-layer abstraction to a `strategy_engine.h` interface** (no code now):
  - `StrategyEngine::select_schedule(arrivals, occupancy, system_params)` returns an ordered admit/grant plan for the tick;
  - `CommArranger::arrival_times(plan, link_state)` applies the alpha-beta occupancy model (`BW_model`) to turn the plan into actual completion times. This is the C++ mirror of CrossPipe Section 6.3 and isolates the link-contention modeling (note 406 port plan #2 — chain-sum becomes path-sum with occupancy).

- **Greedy `CrossUDSub` variant for Engine 2 (LNS init seed):** the sub-block split maps onto quanta. Quanta are protocol-fixed today, so the *direct* sub-block win is unavailable. The *structural* win — *unidirectional* ordering under high `COMM` (the contest's expected regime per `comm_bound`) rather than ping-pong — does transfer: prefer admit-then-batch-grant over interleaved admit/grant when `COMM > per_stage`. Scheme:
  - At parse, classify by `comm_bound = total_comm / total_compute > 0.5` (Engine 0 already computes this).
  - High `comm_bound` ⇒ admit.ordering = SJF by `pe+COMM+pc` (minimize path length), grant ordering batches decodes to amortize one cross-DC round trip across multiple tokens; this is the UD analog.
  - Low `comm_bound` ⇒ Wave-like interleave is acceptable — current frozen behavior.
  - Implement behind `-DCROSS_UD` and A/B test via `python3 tools/ab_test.py --baseline build/solution --candidate build/solution_cross_ud cases/dev/burst_*.txt`. The DEV burst cell (`--arrival clusters`, T 150-200, E 3, rho 1.0-2.2) and the comm-displacement cell (`--COMM 6-8 --T 1000 --n 260 --arrival spread --lout mid`) are exactly the regimes where CrossPipe's UD-vs-Wave crossover predicts a difference.

- **Bandwidth-occupancy `BW_model` for admission:** the current admission gate uses a *serial* lower bound (`pe + COMM + pc + COMM + Lout·per`). When multiple in-flight requests share the edge↔cloud link, this underestimates real completion because their COMM slots queue. Port the range-based occupancy model (`BW_model` finds the earliest free window of length `T_bw` after `T_ready` on the link) into `apply_action_set` to make admission and LOOKAHEAD simulation dispatch against a contended link instead of an ideal one. This is the missing second-order term the chain-sum test ignores; on `comm_bound` cases it is the predicted loss mechanism (per the EXP-37 record that displacement, not starvation, drives bigpool loss).

- **CO-solver variant NOT to be ported:** the solver path is offline-batched and pre-schedules `O(nmb)` microbatches ahead. The contest is online, single-tick decision, adversarial arrivals. The greedy path captures everything portable; the solver path is a research reference for "what optimal looks like" on a known batch, useful only for `tools/opt_gap.py` oracle comparisons on tiny cases.

## Weaknesses

- **Training ≠ inference.** Synchronous SGD on a fixed `nmb`-microbatch batch with a fully-known DAG is the opposite regime of online inference: arrivals are adversarial/hidden, requests have absolute deadlines (SLOs, not throughput), and there is no gradient to synchronize. CrossPipe's whole model is one training iteration; our horizon is `T` ticks with stream arrivals inside it. The CO objective (min makespan on a known batch) is not our objective (max on-time throughput under SLO + 1e-6·V latency).
- **Static DAG vs. dynamic decode length.** CrossPipe schedules `nmb` known microbatches with fixed `{F, D, W}` block times per chunk. Our decode length (`Lout`) is known at admit (good) but the *interaction pattern* — repeated edge↔cloud hops per decode token — has no CrossPipe analog (a microbatch in CrossPipe crosses each DC boundary exactly twice: F-activation forward and Dgradient backward). CrossPipe's loop/wave/UD families differ in *how many* boundary crossings per microbatch; ours is fixed at 2·Lout round trips. The crossover result (UD wins at high delay) is qualitative guidance, not a quantitative schedule we can lift.
- **No SLO / no deadlines.** CrossPipe minimizes makespan; SLO attainment and per-request latency are not in the objective. The 402 work-conserving theorem is closer to our throughput question; CrossPipe is closer to our *bubble* question but not the *deadline* question.
- **No admission control.** All `nmb` microbatches are admitted; the question is order, not reject. Our reject lever (admission control) is upstream of CrossPipe's modeling.
- **Solver path is offline.** Per Section 4.1 + Appendix D.1, the CO solver runs once per (system params, batch size) change — minutes to hours. Our judge budget is CPU-time-bounded and per-tick. Only the greedy path is even theoretically portable, and even it assumes the full batch is known upfront.
- **Bandwidth model is link-by-link, not multi-link.** `BW_model` contends comms one link at a time; our single edge↔cloud link is the trivial special case, but CrossPipe never models a 2-site system explicitly (the smallest evaluation is 2 DCs × 2 stages = 4 stages, with one DC boundary crossing). The "single link" generality of our problem is actually *simpler* than CrossPipe's setting, so the WGrad-split win and the wave/UD crossover should hold a fortiori, but the absolute numbers (33.6% reduction) are not transferable.
- **Memory constraint has no analog.** CrossPipe's `M_s^max` activation-memory budget gates how many in-flight F blocks a stage can hold — the lever that lets dynamic schedules exploit "extra memory budget" (Section 7.2, Case 3). The contest has a *capacity* (tokens/tick per site), not a *memory* (in-flight requests per device). The closest analog is `Ecap`/`Ccap`, but those bound dispatch per tick, not accumulate activations across the horizon. The whole "relax memory → approach zero-delay" result (the second headline claim) does not apply.
- **Decouple-schedule-from-comm assumes non-blocking send/recv.** NCCL rendezvous with delay-aware recv-posting (Section 6.3) is the implementation win. The contest protocol is interactive tick-by-tick stdin/stdout — there is no async communication to reorder; the COMM latency is a fixed time debit, not a steerable operation. The two-layer abstraction's *runtime* half has no contest analog; only the *modeling* half transfers.

## Sources

- arXiv:2507.00217; §2.2 (alpha-beta + Figure 1 bandwidth-occupancy), §2.3 (cross-DC infra classes, latency/bandwidth ranges), §3.3 (1F1B DAG construction, Figure 4), §3.4 (static-schedule problems: critical path `O(nmb)` cross-DC comms, Figure 6 bubble strides), §3.5 (performance model: `start(o)=max(prev, dep+ℓ+T_bw)`), §4.1 (CO formulation: sets, decision vars, constraints, makespan objective), §4.2 (CrossUDSub greedy: Algorithm 1, complexity `O(n²·nsub²·nPP·log)`, §4.2.4 §4.2.5 analysis), §5.1 Table 3 + Figure 8 (WGrad-split wins; UD > Wave as delay grows; Loop most delay-sensitive), §5.2 Figure 9 (cross-DC PP > DP by 3.05× at 4 GB/s; bandwidth-primary bottleneck when TF large), §6.3 (two-layer abstraction: block schedule vs comm orchestration), §7.1 Figure 11 + Table 5 (33.6% reduction vs original / 21.9% vs optimized; up to 1.33× from extra memory+GBS), §7.2 Table 6 (4-DC scaling), §8.1 (heterogeneous DCs: stage-specific params already supported), Appendix D.1 Figure 14 (MILP OOMs at 32 stages; CO scales to 64), Appendix E Figure 9 derivation (cross-DC DP cost 2α+2Nβ).
