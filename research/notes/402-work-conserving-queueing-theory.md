> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 402 — Work-Conserving Scheduling Is Throughput-Optimal (Queueing Theory for LLM Inference)

- arXiv: 2504.07347, "Throughput-Optimal Scheduling Algorithms for LLM Inference and AI Agents", (Tassiulas group, Yale).
- `research/papers/2504.07347.pdf` / `.txt` fetched OK.

## Claim

Formal queueing model of an LLM engine (iteration-level batch serving with token budget bmax and piecewise-constant batch time `tb = c + a·⌈b/b0⌉`, R² > 0.97 on real traces). Main result (Theorem 2): with load `λ(mp+md) < bmax/tbmax`, **every work-conserving K-FCFS scheduler is positive recurrent — i.e. throughput-optimal**; above that threshold every scheduler is unstable. Work-conserving = whenever supply ≥ bmax, the batch fills to the full token budget, mixing prefill+decode.
Orca and Sarathi-Serve are work-conserving (hence optimal); FasterTransformer and vanilla (non-chunked) vLLM are not and can blow up under moderate load. Section 6 (Theorem 10): under BOTH token-budget and batch-size (kmax) constraints the stability region becomes a 2-D convex hull where even work-conserving policies can fail at a vertex — Sarathi-1024 goes unstable while a non-work-conserving variant stays stable.

## Relevance

Two direct consequences for our judge model:

1. Our greedy (grant every feasible decode, then admit every feasible prefill, per tick) is work-conserving by construction. The theorem says: **no ordering or admission tweak can improve raw throughput beyond what a work-conserving policy achieves; the entire score gap vs optimal is (a) SLO attainment (ordering within the work-conserving class) and (b) the latency terms (1e-6·V)**. This refocuses effort: don't chase throughput via clever batching — chase deadline attainment and latency via ordering (note 403).
2. Our capacities are BOTH a token budget and a batch-size constraint simultaneously (Ecap = min(E,B) bound grants per tick, and prefill stages also consume the same budget). Section 6 shows this "vertex C" regime is exactly where work-conserving greedy can be strictly suboptimal — supporting the LOOKAHEAD architecture (simulation-based marginal selection, transactional apply) as the right safety mechanism, and warning that aggressive capacity-filling (e.g., admitting a prefill whose stage lands inside a decode's feasibility window) is a real loss mode.

Also gives the load-condition sanity bound: per tick, the max sustainable decode work is bounded by min(Ecap, Ccap)/per; the judge cases likely sit at or below this (admission control, doc 05).

## Algorithm sketch

```
Theorem 2:  λ(mp+md) < bmax/tbmax  ⇔  stable under any work-conserving K-FCFS
Theorem 10: with token budget bmax AND batch cap kmax, stability region =
            conv-hull in (λp, λd) space; vertex C ⇒ work-conserving can fail
K-FCFS:     a request that arrived K positions later cannot process a decode
            token while an older request processes nothing

```

## Port plan

- Justification, not code: keep the decode-first work-conserving skeleton; never introduce "deliberate idle" (the #1 way to break work-conservingness and lose throughput).
- Concrete guardrail for the LOOKAHEAD planner: never accept a candidate action-set that leaves a site idle while feasible work exists (a non-work-conserving sim step); the candidate filter in `select_actions` already implies this, make it explicit.
- The vertex-C insight maps to a testable variant: **prefill admission should consider the joint (edge, cloud) occupancy of decode windows**, i.e., reject a prefill if its cloud stage would occupy the exact window that a committed decode's DC stage needs (current code already books windows; the variant is about *ordering* prefill starts to avoid conflicts — see note 401 port plan #1).

## Weaknesses

- **Stability ≠ deadlines.** The theorem is about queue growth at infinite horizon with i.i.d. arrivals; our objective is on-time completion within finite horizon T on adversarial (known) arrivals with per-request absolute deadlines. A stable system can still miss every SLO.
- Batch time is a function of *token load only* (compute-bound GPU); our model has two sites with separate capacities and free COMM windows — the paper's single-server abstraction does not capture edge/cloud coupling or our fixed COMM latencies.
- The batch-size-constraint analysis (Section 6) is explicitly partial ("leaves a detailed treatment for future work").
- K-FCFS is an assumption on scheduling algorithms, not a theorem about EDD/EDF priority orders; the paper says nothing about which work-conserving policy minimizes deadline misses — that is the SLAI note.

## Sources

- arXiv:2504.07347; §2.3 Eq. (4) (batch time model), §3.2 Theorem 2 + Eq. (12) (load condition), §4 (per-algorithm work-conservingness table, Fig. 8), §6 Theorem 10 + Fig. 13 (2-D stability region, vertex C), §5 (networks: work-conserving can fail — Rybko–Stolyar example).
