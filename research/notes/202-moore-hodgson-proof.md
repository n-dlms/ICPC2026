> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 202 — A simple proof of the Moore–Hodgson algorithm (Cheriyan, Ravi, Skutella 2021)

**Claim.** The Moore–Hodgson algorithm optimally solves 1||ΣU_j (single machine, minimize number of late jobs = maximize on-time count): scan jobs in EDD order, whenever a prefix goes late discard the longest job in that prefix; O(n log n) with a max-heap. The paper gives a clean 1-page proof. The weighted generalization 1||Σw_jU_j is NP-hard (knapsack), but MH is optimal whenever weights and processing times are *oppositely ordered* (p_i ≤ p_j ⟹ w_i ≥ w_j).

**Relevance.** This is the textbook answer to "which requests to drop to maximize completed-on-time count" — the exact primary objective of our assumed score (# on-time − 1e-6·V). Our solver's admission control (src/Solution.cpp:123) currently admits in EDD order and drops only est-infeasible requests. Moore–Hodgson says: under overload, dropping the *longest-work* request in the first late EDD prefix is *optimal* (unweighted). That is a strict theoretical improvement over "admit whoever fits first".

**Algorithm sketch.** EDD order; maintain accepted set S with total work p(S) via max-heap:

```
for j in EDD order:
    insert(S, j); p(S) += p_j
    if p(S) > d_j: remove argmax p_i in S (deletemax)

```

Output = accepted set in EDD order, then rejected jobs last. Optimality: at the first late prefix [k], some optimal solution rejects some job in [k]; MH's rejection of the longest job is WLOG optimal (Lemma 3), then recurse. Feasibility of a set = EDD schedule meets every prefix deadline (the "deadline-prefix criterion" used throughout note 205).

**Port plan.** Two-phase admission in `admit_prefills`:

1. *Ordering change only (safe, zero-risk):* order WAIT candidates by total serial work `w = pe + COMM + pc + COMM + Lout·per` desc in the *rejection pass* — i.e., when not all candidates fit, prefer to skip the largest-w one. Equivalent to making cmp_key a work key for admission only.
2. *Full MH pass (needs care):* run MH on the WAIT pool with d_j = effective deadline (D minus committed decode time or `min(D,T)`) and p_j = serial work; the MH-rejected set is *pre-doomed* before the greedy scan, freeing capacity for the accepted set. This is exactly "admission control: Moore–Hodgson-style per site" already flagged in docs/05-strategy-playbook.md §P1.5.
A/B via `tools/ab_test.py`; must not regress validity (MH only *removes* candidates, never adds, so the action-level phase/window checks in `apply_action_set` remain the law).

**Weaknesses (transfer gaps).**

- Single machine, all jobs available at time 0, non-preemptive, processing time = continuous serial duration. Ours: two resources with alternating windows, release times (A), a horizon T, and *decode is granted token-by-token* (interleavable, effectively preemptive at token granularity). The "work" of a request is not a single scalar: prefill (bulk, on both sites) and decode (per-token round trips) have different resource footprints; serial lower bound `per` ignores concurrency.
- MH assumes you can *delete an already-accepted job*. In our interactive protocol a request that already got 'P'/'D' actions cannot be un-admitted (we only mark doomed pre-admission). So MH as classically stated is not executable online — it must be used as a *pre-filter on the WAIT pool only* (feasible), not as a revocation mechanism (infeasible). This is exactly why note 205's insertion-only SPT rule is the more faithful online port.
- Weighted case (each on-time completion worth 1 but latency tiebreak V matters) is NP-hard; our USA1/Afterlife keys are heuristic proxies for value.

**Sources.** arXiv:2104.06210 (v1), fetched → `research/papers/2104.06210.pdf` / `.txt`. Published: *Operations Research Letters* 49(6):861–864, 2021. Key results: Proposition 1 & Lemma 3 (pp. 2–3), Theorem 2 (p. 3), weighted-opposite-ordering remark (p. 3). Original: J. M. Moore, *Management Science* 15(1):102–109, 1968 (paywalled, not fetched).
