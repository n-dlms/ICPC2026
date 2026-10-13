> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 406 — SplitLLM: Edge–Cloud Split Inference as Constrained Placement (model family match)

- arXiv: 2410.10759, "SplitLLM: Collaborative Inference of LLMs for Model Placement and Throughput Optimization", Mudvari, Jiang, Tassiulas (Yale / NEC Labs), Oct 2024.
- `research/papers/2410.10759.pdf` / `.txt` fetched OK.
- Note: the task brief asked for "Amphisbaena (Huawei)" — no such public paper is findable (searched; closest Huawei-adjacent edge-cloud LLM work is this and the EdgeFM'24 SLM/LLM hybrid, ACM 10.1145/3662006.3662067). SplitLLM is used as the verified representative of the 2024–2026 edge-cloud split-inference line.

## Claim

Edge-cloud split inference of LLMs = choose, per transformer layer, whether it runs on client (edge) or server (cloud), minimizing server-side FLOP (throughput load) subject to an end-to-end latency budget and communication costs.
Formulated as a constrained shortest-path problem over the (layer, location) graph (NP-hard in general), solved by an **O(L·w) pseudo-polynomial DP** where w is the integerized latency budget: tables C(k,w), S(k,w) = optimal cost with k layers done and w time remaining, ending at client/server respectively; four transitions c2c, c2s, s2c, s2s with per-link compute+transfer costs. Average 28.9% reduction in server computational load (and sharply lower server queue wait) vs greedy splitting at equal latency.

## Relevance

- **Model family match:** this is the closest published problem shape to the contest: split transformer inference across edge and cloud with COMM cost and a per-request latency budget. The judge abstracts it to fixed stages (ALPHA-fixed split, uniform per-token quanta) — so SplitLLM validates the problem framing, and its DP is the offline *placement* half we cannot touch (pe/pc fixed).
- **Feasibility accounting transfers:** the DP's core discipline — every (placement, order) choice must keep the *sum* of compute + transfer along the chain under the budget — is exactly our `est = t + pe + COMM + pc + COMM + Lout·per ≤ min(D,T)` admission test (src/Solution.cpp:135). SplitLLM proves this chain-sum model is the right abstraction for split LLM inference; our serial-lower-bound estimate is its deterministic special case (equal per-token quanta).
- **Load-reduction objective:** SplitLLM minimizes server work to raise throughput; our pe/pc is fixed, so the only remaining load lever is *order* (which request's stages occupy shared windows when) — again pointing at ordering/criticality (note 403) rather than placement.

## Algorithm sketch

```
DP over layers k=1..L, budget w=1..W (integerized latency):
  C(k,w) = best cost ending at client after k layers with w budget left
  S(k,w) = best cost ending at server
  transitions: c2c (compute only), c2s (+upload), s2c (+download+compute), s2s (compute only)
  infeasible transitions → ε (pruned)
  backtrack π[k] = layer placement; complexity O(L·W)

```

## Port plan

- No code port (split is fixed). One justified reuse: keep the chain-sum feasibility test as-is; it is the exact analog of the DP's budget constraint, and it is already the admission gate in both `admit_prefills` and `apply_action_set`. If the real spec ever makes ALPHA per-request or per-case, SplitLLM's O(L·w) DP is the day-2 algorithm to implement for choosing the split (write-up only, not code, per spec-not-public policy).

## Weaknesses

- Solves *where to compute each layer* — a decision the protocol fixes; the contest's scheduling problem (which request, when) is downstream of placement and largely absent from the paper (FCFS-style service assumed at the server; single-request latency budget, no SLO deadlines, no concurrency-aware scheduling theory).
- No decode-stage distinction: the paper models one forward pass per layer; the token-by-token decode with repeated edge↔cloud round trips (the bubble source in our judge) is not modeled.
- The DP is per-request offline; nothing about online admission under contended capacity or interactive per-tick decisions.
- "Amphisbaena" as cited in the brief could not be located — treat SplitLLM as the verified representative of the family; do not assume the real judge's mechanics beyond the U-shaped split it already documents.

## Sources

- arXiv:2410.10759; §III-A Eq. (1)-(2) (formulation: latency budget + server FLOP), §III-B Algorithm 1/2 (O(L·w) DP, C/S tables), §IV-C (28.9% load reduction, queue-wait eval).
