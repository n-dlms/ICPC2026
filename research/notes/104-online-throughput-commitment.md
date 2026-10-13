> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 104 — Online Throughput with Commitment: "Commitment is No Burden"

**Claim.** For online preemptive throughput maximization (jobs arrive at release dates with processing time p_j, deadline d_j, slack ε: d_j − r_j ≥ (1+ε)·p_j) on m parallel machines, a **Θ(1/ε)-competitive** deterministic algorithm exists both *with and without* commitment, where "commitment upon admission" means: once a job starts, the algorithm must complete it. Commitment **upon arrival** is provably unbounded (no bounded competitive ratio, even randomized).
So committing at admission (start) is asymptotically *free*; only committing at arrival is fatal. (Eberle–Megow–Schewior, arXiv 2019, rev. 2021.)

**Relevance.** This is the closest published model to our interactive protocol: our `P`/`D` emissions commit us (an illegal action = case death; a started stage cannot be un-started), arrivals are online, and requests carry SLO deadlines. The paper's central design — the **blocking algorithm** — gives two transferable rules for our solver:

1. **Execute admitted work by Shortest Processing Time (SPT)** ("Scheduling jobs … follows the SPT order for the set of uncompleted jobs assigned to a machine", §2). For our decode-grant stage this says: among *admitted, feasible* decode requests, grant by shortest remaining work — the throughput-maximizing order under deadline pressure (this is the SRPT half of note 105, here with a proof).
2. **Geometric admission gating**: a committed job j only admits new jobs that are **smaller by a factor ≥ γ = δ/16**, and every admitted job creates a **blocking period** of length ≤ β·p (β = 16/δ) that repels similar-size jobs (§2, Figure 2). This prevents the classic pathology of committing to many similar-length jobs that jointly cannot finish — precisely the "don't over-commit / never force" philosophy of the archived working discipline (see README.md), made quantitative.

**Algorithm sketch (blocking algorithm).** Per machine: admit the shortest available job whenever the current time is outside every admitted job's scheduling interval; on admission, job j gets scheduling interval S(j) = [a_j, a_j + (1+δ)p_j) and a blocking period B(j) of size ≤ β·p_j following it, during which only jobs ≤ p/2 of j's size may be admitted; run the admitted set in SPT order; a job admits another only if p_new < γ·p_j (γ = δ/16) and τ ∉ B(k) for all admitted k with p_k ≤ 2·p_new. Analysis: every admitted job completes within (1+2δ)·p_j of its start (p.
9); the blocking periods keep the machine's committed load bounded, yielding Θ(1/ε)-competitiveness (Theorem 1).

**Port plan.**

- `-DSPT_GRANT`: in `grant_decodes` (Solution.cpp:96), order the DW-phase candidates by **remaining work** `tokens_left·per` ascending *within the tight/late half*, keeping EDD for the loose half (combined with note 105's laxity split). Test A/B against EDD default.
- `-DBLOCK_GATE`: in `admit_prefills` (Solution.cpp:123), before booking, apply the geometric rule: refuse to admit a request if it is not at least γ× smaller than the smallest *currently admitted-and-unfinished* prefill (reference size = max(pe,pc) or total prefill work), i.e., prevent stacking similar-size prefills into a horizon they cannot share. γ is a tunable constant (start δ = ε/2 default from the paper, i.e. γ = 1/32).
- The blocking-period idea maps to our window booking directly: a booked prefill window already *is* a blocking period; the addition is to refuse similar-size stack-ups *ahead* of the window check, which the greedy currently permits whenever windows happen to fit.

**Weaknesses / mis-transfers.**

- Model assumes **preemption/resumption at arbitrary granularity**; our prefill is a single non-preemptive block. SPT ordering applies cleanly only to decode tokens (which are granular), not to prefill admissions.
- Single processing resource; ours is the two-resource chain (edge+cloud+comm offsets). The commitment theorems do not cover two-site pipelines; treat the admission rule as a heuristic with theoretical motivation, A/B-verify.
- The ε-slack assumption (window ≥ (1+ε)·work) is not guaranteed in our cases; tight-SLO requests (est ≈ D) violate it, and the theory says those are exactly where online algorithms cannot guarantee anything — consistent with our `doomed` handling (Solution.cpp:136–138).
- **Commitment upon arrival vs. us**: we do NOT commit at arrival — we only commit when emitting P/D, and planned bookings are revocable. So we are *more* permissive than their "commitment upon admission" model; their Θ(1/ε) bound is an upper-bound setting, not a limitation for us. The unboundedness result (commit-at-arrival) is a warning against our doc-05 P2 full-information phase committing too early — a caution for the "freeze the plan" step.
- Unit weights: the score's 1e-6·latency terms are not modeled; SPT ordering may slightly increase latency variance (protected by the ε-latency being dominated — safe in the assumed score shape).

**Sources.** Eberle, Megow, Schewior, "Online Throughput Maximization on Unrelated Machines: Commitment is No Burden", arXiv:1912.10769v2 (`research/papers/1912.10769.txt`; blocking algorithm §2 pp. 7–9, Theorem 1 abstract, impossibility of commit-on-arrival p. 2). Related: Moseley–Pruhs–Stein–Zhou (note 105) builds on the same single-machine "region" algorithm line (KP00/KP03).
