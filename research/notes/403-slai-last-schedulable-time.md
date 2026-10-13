> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 403 — SLAI: Last-Schedulable-Time (Criticality) Ordering for Decode Iterations

- arXiv: 2508.01002, "Optimal Scheduling Algorithms for LLM Inference: Theory and Practice", Bari, Hegde, de Veciana (UT Austin; Proc. ACM Meas. Anal. Comput. Syst. 9(3), Dec 2025).
- `research/papers/2508.01002.pdf` / `.txt` fetched OK.

## Claim

Two halves. (1) Theory: builds a GPU-tiling-accurate cost model and proves a throughput-optimality upper bound (Theorem 1: instability once `x·d̄a > g`); the RAD scheduler (Prefill Mode/Decode Mode alternation, FCFS prefills, optimally-tiled decode batches) attains it (Theorem 2) — i.e., "optimal tiling + dynamic prefill/decode resource allocation" is the complete picture, and Sarathi-Serve/DistServe are corners of one tradeoff. (2) Practice (SLAI scheduler): per-decode-iteration **last schedulable time** `T_L,q = s_{q−1} + TBT_y − Θ·mean_batch_time`; an iteration is *critical* when now ≥ T_L.
Batch construction: (1) critical decode iterations first, in increasing T_L; (2) prefill iterations (active prefills > new, SPF or FCFS order); (3) non-critical decode iterations only if token budget remains. Result: delaying non-critical decodes frees batch budget for prefills earlier, lowering TTFT, while P99 TBT still met — better SLO attainment than Sarathi-Serve's uniform "all decodes are equally urgent" policy, especially with heterogeneous per-request SLOs.

## Relevance

This is the SLO-aware *ordering* theory for exactly our decision: **which decode token to grant when contended, under deadlines**. In our model the decode token of request r at tick t has an implicit per-token operational due date: token k must start its DE stage by `D − (tokens_left−k)·per − per`; sorting grants by that due date ascending = sorting by `D − tokens_left·per` (constant shift, same order).
That is precisely SLAI's criticality: the request whose remaining tokens cannot absorb more delay is granted first — NOT necessarily the request with the earliest absolute D (EDD), which is what our `cmp_key` does today. When two DW requests have different tokens_left, EDD-on-D and EDD-on-operational-due-date differ.

Second transferable piece: **deferring non-critical work is the prefill-admission analog.** SLAI packs prefills into the budget *left over by critical decodes* — the same rule as Sarathi (note 401) but with the criticality filter added. For our judge: a prefill admission should be timed against the decode-criticality frontier, and this is exactly what the LOOKAHEAD simulation marginalizes over.

## Algorithm sketch

```
T_L(q-th decode iter of req y) = end_of_prev_batch(y) + TBT_y − Θ·mean_batch_time
critical(now)  ⇔  now ≥ T_L
batch:  [critical decodes by T_L asc]
        + [prefill chunks: active > new; SPF or FCFS]
        + [non-critical decodes by T_L asc, if budget remains]
our model:   per-token op-due-date(y) = D_y − tokens_left_y · per        # per = PEDGE+COMM+PCLOUD+COMM
             grant order: sort DW by op-due-date asc, tiebreak USA1 desc, then A

```

## Port plan

- **Primary port (1 line):** in `grant_decodes` (src/Solution.cpp:103), replace the comparator `cmp_key` with a decode-specific key: primary `D − tokens_left·per` asc; secondary USA1 desc (keep); tertiary A. Same change in the LOOKAHEAD candidate sort (`select_actions`, src/Solution.cpp:276). Ship as `-DKEY_SLAI`; A/B with `tools/ab_test.py` vs default greedy. Complexity O(1), no new state, tranche untouched.
- **Secondary port:** WAIT-phase analog for admission ordering: key = slack `D − est` (est already computed in `admit_prefills`, src/Solution.cpp:135) — admits the request whose finish estimate is tightest first under admission contention. A/B separately.
- **Not ported (risky):** SLAI's explicit *deferral* of non-critical decodes. In our model a deferred decode frees a window for prefill, but the decode still needs `per·tokens_left` eventually; a naive deferral can force it past D. Our LOOKAHEAD already evaluates this tradeoff per-candidate with transactional acceptance — leave deferral to the simulator, not to a hard rule.

## Weaknesses

- SLAI's batch is a *soft* composition problem (fill a token budget); our grants are atomic bookings of edge+cloud windows — the "pack leftovers with prefills" step has no direct equivalent when the leftover capacity is on one site only.
- `TBT_y` per request is given in the paper; our D is end-to-end, so we must derive per-token slack ourselves (`D − tokens_left·per`) — the derivation is exact only under the serial-lower-bound model, which ignores capacity contention (the honest estimate is what LOOKAHEAD simulates).
- The Θ offset absorbs *batch-time variability* — there is no variability in our deterministic tick model; Θ has no analog.
- Theory half assumes Poisson arrivals, stationary regimes, memory-capacity Assumptions 3-4 — none transfer to the adversarial per-case judge; the practical half is validated only on Mistral-7B / ShareGPT traces.

## Sources

- arXiv:2508.01002; §4.2 Algorithm 1 (RAD), §4.3 Theorems 1-2, §5-6 (SLO-aware scheduler; Eq. (8) last schedulable time; batch-construction steps (1)-(4)), §6.1 (parameter impacts), §7 (eval: SLAI vs Sarathi FCFS/SPF).
