> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Work items below were executed pre-contest and are preserved as historical working notes. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 10 — Challenge Reality Layer (workplan)

Reframe (applied 2026-08-06): Days 1-9 had made the engine stronger. Day 10 started making
the engine **harder to fool**. Two tracks, both first-class:

- Track A — make the solver better (score).
- Track B — make the solver safely deployable under the ACTUAL protocol (validity, runtime,
  compliance, scoring formula, submission logistics).

Objective: maximize expected score + robustness + coverage + runtime margin + validity —
NOT raw score. A +1000 candidate with 2% failure risk can lose to a +700 candidate with
zero failure risk (estfix is the object lesson).

## Gating item (before competition-investment decisions)

Eligibility: participant is under 18; Conditions require age of majority unless the
organizer agrees with parental/guardian consent. Resolved via the official organizers
first (research/day10/clarifications.md Q1-Q3, preserved historical). Solver work continued as research/
engineering regardless; submission-strategy and freeze decisions waited for confirmation.

## Phase 1 — Rule extraction (DONE, 2026-08-06)

Official Rules/Conditions text -> machine-checkable assumptions in protocol/protocol.yaml
(groups challenge_rules + scoring): combined_score CONFIRMED, own_compute CONFIRMED,
full_compliance CONFIRMED, submission_judging_limited CONFIRMED, clarification_channel
CONFIRMED; predeveloped_technology UNKNOWN (tension to clarify), eligibility UNKNOWN
(gating). Status vocabulary extended with OBSERVED (tools/reconcile_spec.py).

## Phase 2 — Submission contract

contest_runner.py + limits.yaml = foundation. At contest start (Aug 14): INPUT/OUTPUT/RUNTIME/
JUDGING were filled from the real spec; per-test vs aggregate was the #1 question. Scoring abstraction:
score(problem_i, submission_j) -> aggregate() — no formula hardcoded (multi-problem
possible per "highest combined score").

## Phase 3 — Unknowns registry

protocol/protocol.yaml IS the registry (KNOWN via CONFIRMED/OBSERVED, ASSUMED, UNKNOWN).
Every protocol item has a blast radius (--blast) and --check blocks on residual ASSUMED.

## Phase 4 — Generalization matrix

research/day10/generalization.md: 7 structural dimensions, informative corners, >=30 cases
per cell; cells are the class unit for selection discipline; freeze gate unchanged.

## Day-10 mindset (write-in)

"Day 10 is not about finding the next winner. It is about discovering what conditions make
a winner possible." Regimes, not policies: characterize when each decision policy is
appropriate (EXP-36: burst_clusters cell FAILs under estfix, big cells gain — the regime
map is real). Do NOT implement regime-switching architecture until the regimes are proven;
the matrix cells are the proof vehicle.

## Transferable principles (problem-specific knobs may change Aug 14; these should not)

admission vs displacement; local feasibility vs global opportunity cost; future-demand
blindness; contention-dependent policy; horizon sensitivity; lookahead budget vs coverage;
valid-answer-always. The specific Lout/COMM/per/E values are NOT transferable.

## Phase 5 — Day-9 structural research (Track A, the ONE open thread)

- EXP-35 verdict: burst displacement is NOT information-limited (AUC flat ~0.6 clairvoyant);
  bigpool starvation IS state-predictable (0.81). Two phenomena, two levers:

  - Bigpool: identify a system state where the FROZEN policy is demonstrably making the
    wrong decision (the strong question form), then a system-scale-keyed admission rule
    (threshold = f(E,T,n,arrival density); mine the relationship FIRST; never a constant).
    EXP-36: push big_spread to n=150-300 to reproduce the starvation tail before mining.

  - Burst: POLICY, not prediction — "defer loose admit when tighter candidates pending
    under contention" (re-checkable, never doom). Validate by replay-counterfactual on the
    displacement chains (P@-delay dossiers) before building.

- Guardrail: any candidate must preserve the +300 flip-completion value while trimming the
  ~-11 tax; gates that eliminate large flip fractions are structurally wrong.

## Phase 6 — Robustness

Hostile-but-spec-compliant inputs (N/T near max, boundary values, extreme arrival/deadline
patterns, max output, long runs) via stress_max + runtime_stress + crash_sweep; rehearse
I/O lifecycle per Day-5 (EOF/uninitialized-k lesson); sanitizer + assert passes for any
built change.

## Phase 7 — Freeze discipline

- Frozen winner untouched (sha 37d09a8...). Every experiment on a branch; default build ==
  estfix byte-identical (verified). Nothing promoted without: matrix pass + suite 20 gate +
  no class regression + runtime margin + ASan/assert + rehearsal.

- Estfix stays a documented high-value candidate; its burst tax is a class regression ->
  not freezeable until the Phase-5 policy or scale-key fixes the class.

## Artifacts this day

- protocol/protocol.yaml: challenge_rules + scoring groups (CONTRACT-VALID)
- research/day10/clarifications.md (Q1-Q12, eligibility first)
- research/day10/generalization.md (structural matrix)
- research/notes/experiments-log.md EXP-34 (flip mining) + EXP-35 (knowledge curve)
- tools/reconcile_spec.py: OBSERVED status added
