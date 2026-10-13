> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 303 — Learning-Augmented Online Packet Scheduling with Deadlines (LAP)

**Paper:** Ya-Chun Liang, Clifford Stein, Hao-Ting Wei, *Learning-Augmented Online Packet Scheduling with Deadlines*, arXiv:2305.07164 (2024). Fetched OK (research/papers/2305.07164.pdf, 51.8 KB text).

## Claim

Designs **LAP**: an online deadline-scheduling algorithm that receives a prediction Ĵ of the whole future job sequence, computes the optimal schedule Ŝ over the prediction, then at each step follows Ŝ **only while a local test passes**: `W(Opt(J_≤t)^(≤t)) / W(P ∪ Ŝ^(t)) ≤ ρ` (Opt = optimum over jobs released so far, P = processed so far, ρ = threshold); otherwise it runs the plain online algorithm OnlineAlg (and can switch back).
Guarantee (Thm 3.7, p.9): competitive ratio **η if η ≤ ρ** (smooth in the prediction error η), and **ρ + γ_on + 1 otherwise** (bounded robustness, γ_on = ratio of OnlineAlg). Complements: **1-consistency ⇒ ratio ≥ 2** (Prop 4.1, p.15), general consistency/robustness tradeoff (Thm 4.2); no 1-consistent algorithm can be better than 2-robust.

## Relevance

This is the *theory of our exact architectural bet*. Our `-DLOOKAHEAD` already implements the LAP shape without knowing it: the "prediction" is our own greedy policy's future (= Ŝ, trivially consistent with itself), the local test is the comparison `score_sim(base) vs score_sim(candidate)`, and the accept-only-if-not-worse rule is a local test with ρ = 1. Three concrete deliverables:

1. **Why probes hurt (matches our experiment log):** "follow the prediction unconditionally" = Algorithm 1 (p.5), which is η-competitive but **unbounded robustness** (Lemma 3.1). Our probe experiment (restoring infeasible candidates, −20 score) was exactly this failure mode: candidates that simulate fine but fail exact feasibility act as a *wrong prediction*; without a local test binding acceptance to exact-apply success, robustness collapses. LAP's proof that a **necessary** tradeoff exists (1-consistent ⇒ ≥2-robust, Prop 4.1, instance J1/J2 with ε-weight expiring packet) is the reason our "never strictly worse" gate is the correct conservative shape.
2. **The local test can use the released-prefix optimum**: `W(Opt(J_≤t))` is computable online from *already-revealed* jobs. For us the prefix problem is NP-hard (note 304), so the plan must be the greedy simulation — but the *formula* of the test (`Opt(prefix)/achieved ≤ ρ`) is a better gate than a raw score delta because it is scale-free: it compares what *could have been done* (given current info) with what we're doing. Port: replace `score_better(base, cand)` with the ratio `score(no-action sim)/score(candidate sim) ≤ ρ`.
3. **Smoothness/ρ tuning**: the guarantee interpolates with error η; robustness adds γ_on + ρ + 1. In our setting η ≡ 0 (the prediction is our own future, error-free by construction), so the relevant numbers are: ρ = 1 ⇒ strict non-worsening (current behavior); ρ > 1 ⇒ we deliberately tolerate small simulated losses for extra admissions (the aggressive bias of 302), with the bound on the damage bounded by ρ. This converts the lookahead variant's binary verdict into a **one-knob family**: A/B `-DGATE` with ρ ∈ {1.02, 1.05, 1.1} under the existing tranche budget.

## Algorithm sketch

```
LAP(ρ, OnlineAlg):
  Ŝ ← optimal schedule over predicted jobs Ĵ
  P ← ∅
  for t = 0..T:
    if Ŝ^(t) ∉ P and W(Opt(J_≤t)^(≤t)) / W(P ∪ {Ŝ^(t)}) ≤ ρ:  # local test
        P ← P ∪ {Ŝ^(t)}            # follow prediction
    else:
        P ← P ∪ {OnlineAlg(J \ (P ∪ expired))(t)}   # fall back
Ratio: η if η ≤ ρ; else ρ + γ_on + 1.  (Thm 3.7)

```

Port mapping (no code written): `Prediction Ŝ` = greedy policy trace from current state; `Opt(J_≤t)` = best simulated plan over known arrivals (approximated by the no-action simulation — honest caveat: an *approximation* of Opt, so robustness degrades to the approximation quality, see Weaknesses); `OnlineAlg` = greedy `run_tick_policy`; `ρ` = new gate parameter; acceptance remains **transactional**: only candidates that pass `apply_action_set`'s exact checks enter P.

## Port plan

- Variant `-DGATE`: in `select_actions`, accept candidate iff `!score_better(sc, base_scaled)`, where comparison uses `base_score` scaled by ρ (equivalently: accept iff `score_cand·(1) ≥ base_score/ρ`). Keep everything else (est-filtered candidate list, K top candidates, transactional apply) unchanged. Expected effect: on burst/tight cases the ρ>1 gate admits a few more prefills that the ρ=1 gate rejects (small simulated loss, real gain if the sim underestimates), on sequential cases it is neutral.
- Complexity bound of the gate: O(1) extra per candidate (one multiplication); the pass stays within the existing `plan_tranche`; no clock().

## Weaknesses

- **Prediction model mismatch**: LAP's prediction is a full-sequence forecast; our "prediction" is self-generated (greedy's own future), which is always consistent but never *more* informative than the greedy itself — so the consistency side (η=0) buys nothing, and the entire value must come from the robustness/ρ knob. The 1.25 constant-lookahead lower bound cited from 302 shows why: without full-sequence predictions, per-request lookahead is capped.
- `Opt(J_≤t)` in LAP is an **exact optimum of the released prefix** (unit jobs ⇒ bipartite matching). Our prefix problem is NP-hard (note 304), so our test ratio is computed against a *heuristic* plan; the ρ+γ_on+1 guarantee does NOT hold if the "optimum" in the test is not an optimum. Any gate we implement inherits the greedy's blind spots (e.g., it cannot see that a prefill admitted now blocks decode windows later if the sim's own policy doesn't exploit that either).
- **Unit jobs, single machine, no comm**: the swap-exchanging arguments (Lemma 3.2-3.5) rely on canonical schedules and per-slot independence; our coupled two-resource tokens break the exchange structure — the ratio-form of the test is portable, its *constants* are not.
- Experiments in the paper are on agreeable-deadline instances (arrival-consistent deadlines) — not our adversarial/stochastic mix; and ρ = 0.1 there is a different normalization.

## Sources

- arXiv:2305.07164 (PDF + text in research/papers/). Alg 2 p.6 (LAP), Thm 3.7 p.9, Prop 4.1 p.15, Thm 4.2 p.16, Alg 1 p.5 (η-competitive non-robust), §1.3 p.3 (1.25 constant-lookahead LB, citing [13] = 1606.09395).
