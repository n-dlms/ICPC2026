> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 305 — Greedy Admission with Patience and Lookahead (Goldwasser, SODA 1999)

**Paper:** Michael H. Goldwasser, *Patience Is a Virtue: Self-Organizing Greedy Admission Control*, SODA 1999. Fetched OK (curator-hosted copy: research/papers/goldwasser-soda99-patience.pdf, extracted .txt).

## Claim

Admission-control for a **single resource** with non-preemptive jobs, release times, deadlines, and weights (unit service lengths, w.p. 1/2, 1, or 2); scheduler chooses an online schedule and may **reject jobs** at admission. Two deliverable results: (a) **greedy earliest-expiration (EDD) is (2 + 1/p)-competitive**, where p = ratio of maximum to minimum weight (Thm 1, p.4, eq. 1); (b) a **greedy with lookahead κ and "sloppiness" is 1/κ-competitive** (Thm 2, p.9-11): with 1-lookahead (κ = 1) the bound becomes **2**, and with patience (allowing rejects that cost) it reaches **(2 + 1/κ)**.
Corollaries: greedy-EDD is 2-competitive for unit weights; the earliest-**available** greedy is **optimal** when all service lengths are equal (p.13); the EDD ordering with weighted tie-breaks inherits the bounds (p.10-11).

## Relevance

This is the **closest single-resource model to our admission problem** in the corpus: non-preemptive requests with release times, deadlines, and weights, admission = reject-or-accept, and — critically — the **only paper in the set that quantifies lookahead as a parameter in the competitive ratio** (1/κ). Three concrete transfers:

1. **The 1/κ tradeoff is the direct theory for our lookahead knob.** `-DLOOKAHEAD` currently simulates to horizon T (full); Goldwasser shows the value of each tick of lookahead is a *factor 1/κ in the worst-case ratio* — beyond κ = 1-2 ticks the marginal worst-case value is thin. This is the quantified justification for reducing the default horizon to K ≈ 1-2 ticks (matching 302's plan over a 2-bounded window), with `-D` knobs `HORIZON` and `PATIENCE` for the tranche sweep.
2. **Patience ≈ slack-aware admission.** The paper's "patience" is the option to *reject a job while paying for it*; our analog is *deferring* an admit (never forcing) and preferring the expiring-est candidate. Greedy-EDD-with-patience is the formal statement of our `(deadline, weight)` cmp_key: admit the earliest-expiring heavy job, reject infeasible ones without apology. The 2-competitive bound on greedy-EDD (unit weights) means a *pure deadline-ordered policy cannot be worse than 2×* — our `-DEDD` baseline is in this family, and the bound transfers to justify keeping it as the fallback tier.
3. **Lookahead via earliest-available optimality** (p.13): with equal service lengths, greedy "earliest available" achieves the optimum online. Our equal-length sanity case (identical per-token decode widths) should sit at ~optimal; if the audit shows a large gap there, it is a *scheduling-order* bug (note 304's Johnson diagnostic), not an admission bug.

## Algorithm sketch

```
Greedy-EDD: admit in deadline order; if the pending workload cannot fit
  (infeasible by deadlines), reject the lowest-value job and re-run;
  else schedule.  (p.4, Thm 1; reject rule = "expire lowest weight")
With 1-lookahead κ=1: see arrivals one tick ahead; can defer/reject with
  knowledge of the next job; ratio 2 (Thm 2, p.9-11).
Greedy earliest-available: schedule whenever resource free, in EDD order;
  optimal for equal lengths (p.13).

```

The lookahead analysis (p.9-11) is a charging argument over "active interval" sets, partitioning time into phases; constants: (2+1/κ) for the sloppy variant, 2 with clean 1-lookahead.

## Port plan

- **Recommended single transfer (report §4)**: `-DLOOKAHEAD` variants default `HORIZON` to 1-2 ticks (from T) and add `PATIENCE` as an admission *defer* option — admit only if a candidate's simulated margin ≥ (2+1/κ)-style threshold scaled by ρ (the 302/303 gate combined with the Goldwasser horizon). Complexity: `HORIZON` shrinks the per-tick simulation cost from O(K·T) to O(K·H) with H ≤ 2; `PATIENCE` adds O(1) per candidate. Worst case remains bounded; no clock().
- Keep greedy-EDD as the **base tier** (`run_tick_policy` unchanged): the 2-competitive bound is our worst-case floor claim — a safe answer is always the EDD admission, and the whole tranche stack improves on it.
- **Audit metric** (from p.13): on equal-length cases, compare greedy's objective vs a simple upper bound (all jobs admitted); ratio close to 1 confirms ordering is sound; deviation points at the two-level key (304).

## Weaknesses

- **Single resource, unit lengths, weights {1/2,1,2}**: our decode pool is multi-window with per-request (PEDGE, PCLOUD) and 1e-6-dominated latency tiebreakers; the 2-competitive bound does not account for *two-resource coupling* or *length variation* — treat it as a floor argument, not a tight bound for us.
- **Non-preemptive**: our decode grants can be *partially* preempted in effect (decode pacing), which the model doesn't cover; the theory is conservative for us only if we never preempt (recommended).
- The 1/κ result requires *real* lookahead (knowing next arrival at t); our skeleton reveals arrivals at t only, so the *number* 1/κ must be read as "value of the lookahead *parameter*" (the horizon), not as an achieved ratio in-protocol (same caveat as 302).
- The paper's "patience" costs the scheduler; our deferral is free. The (2+1/κ) constants therefore overestimate our achievable ratio, which is fine for a floor/ceiling argument but must not be cited as a tight bound.

## Sources

- goldwasser-soda99-patience.pdf/.txt (curator copy, research/papers/). Thm 1 p.4 (eq. 1), Thm 2 p.9-11 (1/κ; sloppy variant p.11), earliest-available optimality p.13, EDD tie-breaks p.10-11.
- Formal name: admission control model of Garay et al. (J. Algorithms 1998), cited p.1; see `docs/04-scheduling-theory-toolkit.md` for EDD/Johnson context.
