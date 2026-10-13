> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 501 — Zero Bubble Pipeline Parallelism

- arXiv: 2401.10241, "Zero Bubble Pipeline Parallelism", Penghui Qi, Xinyi Wan, Guangxing Huang, Min Lin (Sea AI Lab), v1 30 Nov 2023.
- `research/papers/2401.10241.pdf` / `.txt` fetched OK.

## Claim

Splitting the layer backward into **B** (gradient w.r.t. input) and **W** (gradient w.r.t. parameters) decouples a previously sequential dependency (`B_{i-1}` depended on `W_i`) and lets `W` be slid anywhere after its `B`, so it can fill pipeline bubbles.
On top of this, handcrafted schedules ZB-H1 (same peak memory as 1F1B, bubble ≈ (p−1)(T_F+T_B−T_W), a third of 1F1B) and ZB-H2 (zero-bubble at 2p·M_B memory), an **automatic schedule search** (heuristic + ILP) that takes (p, m, M_limit, T_F, T_B, T_W, T_comm) and minimizes the longest-stage completion time under a memory cap, and **ZB-V** (V-shaped 2-chunks-per-worker assignment achieving zero bubble at 1F1B's p·M_B memory). Up to +23% throughput over 1F1B at equal memory (+31% relaxed); ZB-2p bubble rate < 1% on profiled timings.

## Relevance

The contest is the **inference-side analog of the bubble problem this paper solves for training**: a U-shaped split (edge first-layers → cloud middle → edge last-layers) means each request does edge-prefill → COMM → cloud-compute → COMM → edge-decode, and *every decode token* repeats the cloud round trip. With sequential per-tick processing, edge sits idle while cloud computes and vice versa — exactly the pipeline bubble ZB targets. Three transferable ideas:

1. **Split-the-stage to break the dependency chain.** ZB's central move is separating B from W so the non-critical half can be retimed to fill bubbles. Our analog: the edge-decode stage is `W`-like — for a request whose cloud-compute (the `B`-like critical chain) has finished, the edge-decode quanta can be issued *whenever* edge has a free slot within the request's feasibility window, not necessarily immediately. The current greedy admits/grants decode in arrival order; a retiming that *deliberately* slips a decode grant to fill an edge-idle slot later in the tick (without missing the request's per) is a ZB-style bubble-fill. The bubble variable — explicitly modeled idle time between edge and cloud — is what ZB-H2 drives to zero; our `idle_edge(t)` + `idle_cloud(t)` time series is the exact quantity to minimize.
2. **Warm-up + steady-state + cooldown structure.** 1F-1B-1W is the steady state that keeps both ends busy once the pipeline is full. Our prefill (F) → cloud-compute (B) → decode (W) triad is structurally identical; the warm-up phase ("fill the pipe with prefills before the first decode must happen") is exactly the admission-control question of *how many concurrent in-flight requests to carry* before decode starts draining. ZB's rule "the first worker needs (p−1)·(T_B+2T_comm)+p·T_F worth of warm-up F-passes before the first B can return" is the same chain-sum lower bound our `est = t + pe + COMM + pc + COMM + Lout·per ≤ D` admission test computes (note 406) — ZB gives the *theoretical justification* for why chain-sum is the right feasibility test.
3. **Automatic schedule search via ILP over (i,j,c) pass timings.** The contest's offline-per-instance variant (if the spec ever admits an offline plan mode or a per-case budget for global re-optimization) is directly attackable with the same ILP shape: variables E(i,j,c) = ending time of pass c for request j at stage i, ordering indicators O, dependency constraints across stages, memory→capacity constraints. The objective `min max_i E(i,m,W)−E(i,1,F)+T(i,1,F)` is "minimize makespan of the longest stage" = minimize T_last_completion, which is the V term in our score (Σ 1e-6·max completion). The structural match is the closest of any paper in the corpus to a hypothetical offline-optimal solver.

## Algorithm sketch

```
B/W split:      Backward_i split into B_i (∇_x, depends on B_{i+1}) and W_i (∇_W,
                independent of B_{i-1}, can be retimed). Replaces one sequential
                link B_{i-1} → W_i with two parallelizable ops.
ZB-H1:         1F1B layout, but W starts depend on warm-up count. Bubble:
                (p-1)(T_F + T_B - T_W).  Peak memory: p·M_B. (1/3 of 1F1B bubble.)
ZB-H2:         Extra F in warm-up to fill the pre-first-B bubble; tail W reordered
                trapezoid→parallelogram. Bubble: (p-1)(T_F + T_B - 2T_W). Memory
                (2p-1)M_B.  Near-zero bubble if m large.  Requires dropping the
                cross-stage optimizer sync (Section 4: post-update validation +
                in-place AdamW rollback, Appendix C).

Heuristic auto-schedule (§3.1):
  warm-up:     while memory < M_limit: schedule F at stage i. (#F_i - #F_{i+1} ≥ 1
                always; 2 binary hyperparams control (a) almost-fill vs. delay-next-B
                and (b) skip-one-F when diff>1 doesn't add bubbles. Grid search.)
  steady:      1F-1B-1W. Insert W when gap > T_W, or gap < T_W but the cumulative
                bubble so would exceed the largest cumulative at another stage, or
                when memory limit hit (recycle). Drains to F=0,B=0,then all W.

ILP (Appendix G):
  vars:        E(i,j,c) end time of pass c∈{F,B,W} at stage i, microbatch j
               O(i,j,c)→(i,j',c') ∈ {0,1}  ordering indicator
  min          max_i  E(i,m,W) - E(i,1,F) + T(i,1,F)        (makespan of longest stage)
  s.t.         E(i,j,F) ≥ E(i-1,j,F) + T_comm + T(i,j,F)    (forward chain)
               E(i,j,B) ≥ E(i+1,j,B) + T_comm + T(i,j,B)    (backward chain)
               E(i,j,c) ≥ E(i,j',c') + T(i,j,c) - ∞·O(...|...)   (chosen order)
               M_limit ≥ Σ ΔM(i,j',c') + Σ ΔM(i,j,c)·O            (memory cap)
  ΔM(F) = +M_B ,  ΔM(B) = M_W - M_B ,  ΔM(W) = -M_W
  solved by CBC (Forrest & Lougee-Heimer 2005) at small scale; heuristic seeds it.

ZB-V (§6):     2p chunks, assign 2 chunks per worker in a V: worker i gets
                chunk i (forward) and chunk 2p-i+1 (forward), so F and B for a
                microbatch originate at the SAME worker → B can start without
                waiting for last worker. Warm-up: worker i does (2p-i) F's on
                chunk 1 + (i-1) F's on chunk 2 = 2p-1 F's. Steady: 1F-1B-1W,
                alternating chunk 1 / chunk 2 groups. Tail: B prioritized, W fills.
                Zero bubble at p·M_B memory (same as 1F1B) when T_F=T_B=T_W.

Bubble rate:   (cost - m·(T_F+T_B+T_W)) / cost,  cost = max_i stage execution time.
Memory floor:  M_limit ≥ k·M_B,  β ≥ (p-1)(T_B+2T_comm)+(p-k)T_F  (Formula 1-2,
                Appendix B). Zero-bubble needs k = ⌊((p-1)(T_B+2T_comm)+pT_F)/T_F⌋
                → ~2p·M_B memory at tight comm, ~p·M_B for ZB-V.

```

## Port plan

No code today — this is a research note. The candidate variant, conditional on the spec exposing **explicit stage timings and per-stage capacities** (i.e., the protocol lets us observe `pe/pc/COMM` as quanta we can interleave, not just fixed ALFA):

- **New engine `-DENGINE_FLOW`** in `src/Solution.cpp`, sibling to the current greedy/lookahead engines. Shape: at parse, classify the case as "pipeline-friendly" via `tools/case_classify.py` — i.e. `comm_bound=false`, decodable stages, Lout ≥ 2 (genuine decode loop). On those cases, switch from per-tick greedy admit/grant to a **ZB-V-style plan**: maintain an in-flight set with two "chunks" per request (the first-edge layers and the last-edge layers share the same edge worker, so the request's prefill and decode are co-located on edge — already true in our U-shape; ZB-V's V-shape is structurally what the U-shape already is). The plan emits, per tick: as many prefill-starts as edge capacity allows (warm-up rule: at least one more F in flight at the edge than at the cloud), interleaves cloud-compute grants and edge-decode grants in 1F-1B-1W fashion, and fills edge-idle ticks with slipped decodes (the W-pass analog) whose feasibility windows still permit them.
- **A/B hypothesis:** `python3 tools/ab_test.py --baseline build/solution --candidate build/solution_flow cases/*.txt` — primary hypothesis is that ZB-style decode retiming reduces the V term (1e-6·max completion) AND the on_time count on comm-heavy / long-Lout cases by filling the bubble the greedy leaves between a cloud finish and the edge's next free decode slot. **Expected win region:** `matrix_gen.py --arrival spread --lout deep --rho loose --n 96 --T 1000` (the cells where the Day-4 big-scale gain +76 showed up — exactly the regime where the pipeline is deep enough for warm-up to matter). **Expected loss region:** `impossible_slo` / `tight_slo` cases where deliberate slippage of a decode grant risks missing D — the flow engine MUST keep the existing chain-sum feasibility gate (`est ≤ D`) as a hard constraint on every retimed grant, never bubble-fill at the cost of a deadline.
- **ILP offline mode (deferred):** if the spec ever allows a per-case budget for global re-optimization (the playbook's "full-information re-optimization" phase), port the Appendix G ILP directly with CBC (already allowed — single-file, no third-party libs; CBC is a .a link, but if unavailable, the heuristic seeds a greedy solution and the ILP is optional polish). Variables: E(req, stage) with stage ∈ {E_pre, C_mid, E_dec, C_dec_per_token...}; constraints identical to (4)-(7); objective `min max completion` = minimize V. This is the only paper in the corpus with an explicit ILP for the exact objective shape we have.
- **Sanity guardrails:** the bubble-rate formula `(cost - m·ΣT) / cost` is a *diagnostic*, never an objective — our score is on_time + 1e-6·V, not bubble rate. Use it in `tools/case_classify.py` to detect "comm-bound" cells (high bubble rate ⟹ retiming helps; low bubble rate ⟹ greedy already near-zero-bubble ⟹ flow engine adds nothing, fall back to greedy). Never run the flow engine on `comm_bound=true` cells (the protocol's COMM is a fixed-latency wall with no overlap possible — ZB's T_comm assumption is overlap-with-compute, which the contest may not allow).

## Weaknesses

- **Training, not inference.** The whole paper is synchronous SGD over a fixed microbatch DAG: deterministic m microbatches, F→B→W chain per microbatch, all known at schedule time. Our problem is online inference with adversary-chosen arrivals, per-request SLO deadlines, and unknown future. The ZB schedule is computed offline from a known (p, m, T_*); the contest solver must commit per-tick with no future knowledge. The ILP is offline and not directly runnable per-tick.
- **No deadlines.** ZB minimizes makespan with a *memory* constraint; it has no notion of per-microbatch deadlines. ZB-H2's "potentially infinite W retiming" would happily miss every SLO if W-slippage delays a request's completion past D. Transferring the schedule requires bolting on a hard `completion ≤ D` gate per request — which is exactly the feasibility test we already have, but ZB's automatic search has no such constraint and would need to be rewritten.
- **B/W split is a training artifact.** Inference has no backward pass; there is literally no `W` to retime. The structural analog (cloud-compute = B-critical-chain, edge-decode = W-slippable) is an *interpretation*, not a theorem — it holds only to the extent that edge-decode quanta are genuinely slack (i.e., the request's per is loose enough to absorb a slipped grant). On `tight_slo` cells the analog breaks: edge-decode is on the critical path and cannot be retimed at all. A variant that wins on `deep_lout_loose_rho` may regress on `tight_slo`, classic portfolio cell-dependence — use the freeze gate's structural-cell regression check.
- **1F-1B-1W steady state assumes homogeneous stage timings.** The contest's pe/pc may differ across requests ( heterogeneous Lout, per-request per) — the steady-state pattern breaks when stage time is request-dependent. The automatic-schedule search handles this via profiling, but it profiles once per (p,m) configuration; we'd need per-request profiling, and the ILP grows with the request count.
- **Optimizer sync bypass (Section 4) is irrelevant to inference.** No optimizer, no gradient norm clipping, no NAN check. Half the paper's mechanism (the post-validation + in-place AdamW rollback) does not transfer at all.
- **Memory ↔ capacity analogy is imperfect.** ZB's M_limit bounds activations stored *across stages* to enable retiming. Our Ecap/Ccap bound grants *per tick*, not in-flight activations; a slipped decode still occupies an edge slot in its rescheduled tick. The slack ZB exploits (W uses less memory than B) has no clean analog — we cannot "store a decode for later" without consuming the window. The analogy is loose; do not treat the bubble-rate bound `(p-1)(T_F+T_B-2T_W)` as a quantitative prediction for our setting.

## Sources

- arXiv:2401.10241; §1 (B/W split motivation, Figure 1), §2 (ZB-H1 §2.1, ZB-H2 §2.2, quantitative analysis §2.3 — Table 1 FLOPs/memory per pass, Table 2 bubble vs memory), §3 (heuristic §3.1, ILP formulation Appendix G Eq. 3-7, memory floor Appendix B Eq. 1-2), §4 (optimizer sync bypass, Figure 4; in-place AdamW rollback Appendix C Algorithm 1), §5 (experiments — Figure 5 throughput, Table 5 bubble rates, Figure 7 memory-vs-bubble, Appendix H small-m regime), §6 (ZB-V §6 — Figure 8 schedule, Tables 6-8, Appendix F same-memory comparison, Figure 9, Appendix D), Appendix G (ILP variables/constraints/objective).
