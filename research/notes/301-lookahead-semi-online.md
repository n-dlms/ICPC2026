> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 301 — Semi-online Scheduling with Lookahead (k-lookahead model)

**Paper:** Debasis Dwibedy, Rakesh Mohanty, *Semi-online Scheduling with Lookahead*, arXiv:2306.06003 (2023). Fetched OK (research/papers/2306.06003.pdf, 35.3 KB text).

## Claim

Introduces the **k-lookahead model** for online makespan minimization on identical machines: when job *i* arrives, the algorithm also sees the processing times of the next *k* jobs. For 2 identical machines: **lower bound 4/3 for ANY k ≥ 1** (Theorem 1, p.3) and a matching 1-lookahead algorithm **2-LA1** (Alg. 1, p.4) → 1-lookahead is **optimal and increasing lookahead beyond 1 yields nothing**. For 3 machines: LB 15/11 ≈ 1.364, UB 16/11 ≈ 1.455 with 1-lookahead (vs 5/3 without).
Baseline no-lookahead tight bound is 3/2 (Graham LS); so the *entire provable worst-case gain of unbounded lookahead* on 2 machines is 3/2 → 4/3 (~12.5% relative), and it saturates at one lookahead step.

## Relevance

This is the cleanest quantitative answer to "how much lookahead/simulation is worth": in the makespan-on-identical-machines world, **one lookahead step saturates the benefit**; unbounded lookahead does not beat 1-lookahead. Our LOOKAHEAD variant simulates to the horizon T per candidate per tick — vastly more than 1 step — and the experiment log shows it is ~break-even. Theory says that is the *expected* outcome: the recoverable worst-case gap is small and mostly captured by the greedy incumbent; lookahead's value is in a few structural decisions, not in horizon length.

The mechanism also matters: **2-LA1 uses the lookahead only inside a threshold rule** — assign job i to machine 1 iff `l1 + pi ≤ (2/3)(l1 + l2 + pi + p_{i+1})`, last job to the least-loaded machine (Alg. 1, p.4; invariants `l1 ≤ 2·l2` and `max load ≤ (2/3)Σp`). The one-step lookahead adjusts a closed-form acceptance threshold; no search, no simulation. This is a provably optimal way to spend lookahead: a cheap invariant-maintenance rule, not an expensive rollout.

## Algorithm sketch

```
2-LA1 (2 machines, 1-lookahead):
  l1=l2=0
  for i in 1..n-1:  # pi and p_{i+1} known
    if l1+pi <= (2/3)(l1+l2+pi+p_{i+1}):  assign Ji to M1; l1+=pi
    else:                                  assign Ji to M2; l2+=pi
  assign Jn to the least-loaded machine.
Guarantee: Cmax <= (4/3)·C*OPT, for every instance (Theorem 3, p.5, minimal-counterexample proof).

```

3-LA1 (Alg. 2, p.9) is the same shape with nested thresholds `l1 ≤ 16/33·T`, `l2 ≤ 15/33·T` where T = total processing time seen so far.

## Port plan

- **Variant `-DTHRESH_LOOKAHEAD`**: keep greedy EDD ordering, but make admission/decode *acceptance thresholds* functions of the one-next-request view: e.g., when deciding which DW-phase request gets a decode grant at tick t, look at the next arrival (known per protocol at tick t+1... only arrivals at t are revealed in our skeleton — so the "lookahead" object is instead the *next candidate in sorted order*, exactly the k-lookahead spirit: the next schedulable unit, not the next arrival). Concretely: grant decode to request i only if `(slack_i − cost_i) ≥ (2/3)(remaining_total)`, mirroring the (2/3) threshold; A/B vs baseline and vs `-DLOOKAHEAD`.
- **Budget guidance (the quantified part):** per-tick simulation depth should be cut from T to O(1) decision-steps; re-invest the freed budget in candidate *coverage* (all candidates, not K=8) and in a margin gate (see 303/306 notes). A depth-1 lookahead provably captures the full competitive upside of this family.
- **Warning for A/B:** the makespan bound is about *load balance*, our objective is on-time count + latency; the threshold constants (2/3) are for P2||Cmax and will need re-tuning — but the *shape* (threshold with next-item knowledge) transfers.

## Weaknesses

- Model is **non-preemptive, no release-time slack, no deadlines, no two-resource coupling**: jobs are independent unit loads on identical machines. Our requests are multi-stage chains (prefill then Lout coupled decode tokens) across two heterogeneous sites with exact comm lags; **no ratio here transfers numerically to our objective** (on-time count under SLOs, not makespan).
- The 4/3 result is a **worst-case (adversarial) bound**; our case generator is stochastic — typical-case gains can be smaller (consistent with the ~0 gain observed) or larger on pathological seeds.
- Observation (p.6, after 2-LA1): if the algorithm ever receives a job with `pi = 0` or loses the next-size knowledge, the ratio degrades to 3/2 — i.e., the guarantee is **fragile to the lookahead model being exactly right**. In our interactive protocol the "next" object (next arrival) is NOT known in advance, so the honest mapping is to the *buffer* variant (Kellerer et al., buffer of k jobs ⇒ 4/3 for k=1) — but our admission is irrevocable-commit, unlike buffer models where you may defer.
- 16/11 vs 15/11 gap on 3 machines shows the technique stops being tight quickly as machines grow — weak guidance for our 2-site setting beyond the 2-machine case.

## Sources

- arXiv:2306.06003 (PDF + text in research/papers/). Key results: Thm 1 p.3 (LB 4/3), Alg 1 p.4 (2-LA1), Thm 3 p.5 (UB 4/3), Observation p.6 (lookahead>1 no gain; fragility to pi=0), Alg 2 p.9 (3-LA1), Thm 4/5 p.8-12 (15/11 LB, 16/11 UB).
- Baseline no-lookahead 3/2 bound: Graham 1966 (Bell Syst. Tech. J. 45:1563–1581), cited p.1; buffer-model 4/3 for k=1: Kellerer et al. ORL 1997 + Zhang IPL 1997 (refs [23],[35]).
