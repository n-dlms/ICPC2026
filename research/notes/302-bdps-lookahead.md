> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 302 — Online Packet Scheduling with Bounded Delay and Lookahead (BDPS)

**Paper:** Martin Böhm, Marek Chrobak, Łukasz Jeż, Fei Li, Jiří Sgall, Pavel Veselý, *Online Packet Scheduling with Bounded Delay and Lookahead*, arXiv:1606.09395 (2016; TCS 2019). Fetched OK (research/papers/1606.09395.pdf, 65.4 KB text).

## Claim

Studies online bounded-delay packet scheduling (BDPS): unit jobs with (release, deadline, weight), one job per slot, maximize total weight of on-time jobs (= `1|p_j=1, r_j|Σw_j U_j`).
Two contributions: (1) **ToggleH**, a φ-competitive algorithm for 4-bounded instances (φ ≈ 1.618, matching the known lower bound) (Section 3, Thm 3.1); (2) **with 1-lookahead** (knowing at time t which packets arrive at t+1), for 2-bounded instances the algorithm **CompareWithBias(α)** achieves competitive ratio **R = (√13−1)/2 ≈ 1.303** (Section 4, Thm 4.1) with a **near-tight lower bound (1+√17)/4 ≈ 1.281** (Section 5, Thm 5.1).

## Relevance

This is the deadlines-setting analogue of note 301 and it *quantifies 1-tick lookahead in the exact objective family we care about* (maximize on-time weighted jobs under deadlines): for 2-bounded instances, 1-lookahead improves the achievable ratio from **φ ≈ 1.618 (no lookahead) to ≈ 1.303**, i.e. ~19% relative worst-case gain, and nothing below ≈1.281 is achievable — the remaining upside of better algorithms is ~2% of ratio. Two architectural facts transfer:

1. **CompareWithBias's "plan"**: at each step compute the **optimal schedule over pending + lookahead packets** (the "plan", p.6: optimal over the 2-bounded window with the earliest-deadline property), then either follow the plan's p1 or, if `r_{p2}=t` and `w_{p1} < min(w_{p2}, w_{p3}, (w_{p2}+w_{p3})/2α)`, schedule p2 instead. This is exactly "simulate the near-future optimum, compare against a **bias threshold**, act" — the same shape as our `select_actions` + `apply_action_set`, but with a *ratio* gate instead of a binary not-worse test.
2. The **lower-bound technique** (Section 5): the adversary runs exponentially growing phases and stops based on the algorithm's behavior — so even 1-lookahead cannot see "whether the next phase arrives". Lesson for us: lookahead cannot protect against arrivals *after* the lookahead window; that's precisely where our no-action-simulation base_score is blind too.

Also: pure greedy (heaviest-first) is only 2-competitive (p.2) while EDF-with-weight-threshold (EDF_α) is the workhorse; the *earliest-deadline property* (canonical order, p.2-3) is used to normalize adversary schedules — matches our EDD-first cmp_key.

## Algorithm sketch

```
CompareWithBias(α), α = (√13+3)/4 ≈ 1.651, R = (√13−1)/2 ≈ 1.303:
at time t: compute plan p1,p2,p3 = optimal schedule over pending∪lookahead packets
           (2-bounded ⇒ plan uses slots t, t+1, t+2; EDF-canonical)
if r_{p2} = t and w_{p1} < min(w_{p2}, w_{p3}, (w_{p2}+w_{p3})/(2α)): schedule p2
else schedule p1.

```

Analysis is a charging scheme (full/split/chain charges) proving per-slot charge ≤ R·weight; constants satisfy R+2δ−1 = αR, 1−2δ+2αδ = R, 1+1/(2α) = R (eqs. (1)-(3), p.7).

## Port plan

- **Variant `-DGATE` (recommended sibling to 303)**: replace the binary accept rule in `select_actions` with a ratio gate: accept candidate iff `score_sim(candidate) ≥ score_sim(base)/ρ` for tunable ρ ≈ 1.03–1.15. This allows *small* simulated losses (aggressive bias) while hard-rejecting big ones (robustness), mimicking the min-threshold in CompareWithBias. Keep the transactional `apply_action_set` with exact feasibility checks — legality is unchanged by construction.
- **Horizon guidance**: the plan is computed over the **s-bounded window** (here 2). In our protocol a decode grant commits edge `[t, t+PEDGE)` and cloud `[t+PEDGE+COMM, t+PEDGE+COMM+PCLOUD)` — a *two-coupled-slots* action ⇒ effective boundedness ≥ 2 ⇒ a plan horizon of **1-2 ticks ahead** captures the bulk of the provable value; simulating to T is overkill (see 301).
- EDF_α-style decode ordering variant: grant decodes in EDD order but *skip* a candidate whose weight-key is below `(heaviest key)/α` unless it is expiring — cheap, and directly the EDF_α idea (2-competitive baseline → φ-competitive on bounded instances).

## Weaknesses

- **Unit jobs, one machine, one slot per job**: our actions occupy *two resources in sequence with a comm lag*, and a request spans many coupled tokens; the s-bounded parameter maps only loosely (a grant's cloud slot is a "deadline"-like commitment 2·(PEDGE+COMM) away).
- **1-lookahead here = perfect knowledge of t+1 arrivals**; our skeleton reveals arrivals only at tick t, so true lookahead is *not available in-protocol*. The transferable part is the *acceptance-gate architecture*, not the clairvoyance itself. (When the real spec arrives: if arrivals are revealed one tick early, this note's 1.303/1.281 numbers become directly relevant.)
- Ratio results are for **weighted throughput** on a single resource; our objective adds latency terms (Σ completion + max) as a 1e-6-dominated tiebreaker — ordering (not just admission) matters to us; BDPS is ordering-agnostic about ties at equal weight.
- The 1.281 lower bound is adversarial and 2-bounded-specific; no quantitative conclusion for our stochastic generator beyond "don't expect large remaining worst-case gaps".
- Note the cited-in-LAP fact (2305.07164, §1.3, ref [13]): a **1.25 lower bound holds for ANY constant lookahead** in the general (non-bounded) deadline model — unbounded lookahead is needed to go below 1.25; that is why 303 switches to full-sequence predictions.

## Sources

- arXiv:1606.09395 (PDF + text in research/papers/). ToggleH: p.3-6 (Thm 3.1 p.4); plan definition p.6; CompareWithBias p.7 (Thm 4.1); constants eqs (1)-(3) p.7; lower bound Section 5 p.15-16 (Thm 5.1, phase adversary construction p.16).
- Greedy 2-competitive / φ lower bound: Kesselman et al. & Chin et al., cited p.1-2.
- Constant-lookahead 1.25 LB: reported in Liang-Stein-Wei, arXiv:2305.07164, §1.3 and §2 (citing [13] = this paper; journal version TCS 776 (2019) 1-13).
