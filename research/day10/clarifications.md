> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Clarification items below were raised/resolved through official channels pre-contest and are preserved as historical working notes. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 10 — Organizer Clarification List

Rule-derived items raised with the official organizers BEFORE competition-investment
decisions. The rules provided a clarification channel; the Problem Set dropped Aug 14.

## 1. Eligibility (GATING — resolve first)

- **Q1**: Age-of-majority condition — participant is under 18. Under what conditions will the
  Organizer/Sponsor agree to participation with appropriate parental/guardian consent?
  What documentation (consent form, guardian ID) is required and to whom?

- **Q2**: Does eligibility depend on the participant's own jurisdiction's age of majority,
  or the organizer's? (Affects whether consent is needed at all.)

- **Q3**: If eligibility cannot be resolved in time, does the work so far have any standing
  (e.g., as a team/mentor submission, or is the Challenge strictly individual)?

## 2. Pre-developed technology tension

- **Q4**: Conditions say submissions "were not developed in any substantive form/format prior
  to the Challenge"; Rules say participants may bring "any pre-developed or licensed
  Technology". For a solver developed day-by-day as a learning exercise BEFORE the Challenge
  announcement: may it be submitted as-is, or must it be re-derived/adapted during the
  Challenge window? What does "substantive form" mean for code, and does the archive
  (git history, research notes) help or hurt?

## 3. Scoring

- **Q5**: "Highest combined score" — one problem or multiple problems? If multiple, how are
  per-problem scores aggregated (sum / weighted / ranked)?

- **Q6**: Per-case points vs suite-aggregate; is a TLE/RE case scored 0 or does it invalidate
  more? Penalty structure for resubmissions (score update / last-nonzero / final-only)?

## 4. Execution contract

- **Q7**: Per-test time limit, aggregate suite limit, or both? CPU or wall time? Measured how
  (per-process / RUSAGE / per-tick)?

- **Q8**: Interactive protocol confirmation: per-tick stdin lines, flush requirements, output
  format, EOF/termination semantics, output size caps.

- **Q9**: Memory limit and per-case process model (fresh process per case?).
- **Q10**: Maximum N, T, and all parameter ranges (exact constraints).

## 5. Judging logistics

- **Q11**: Submission rate limits; when is the final score locked (latest non-zero re-test?);
  hidden-set generation policy (same params, different seed?).

- **Q12**: Timezone/duration specifics if the challenge has fixed windows.

## Tracking

Each answer updates `protocol/protocol.yaml` status (CONFIRMED/CHANGED/REMOVED) and runs
`tools/reconcile_spec.py --blast <item>` for the affected components. Unanswered items
remain UNKNOWN and tooling must not depend on them.
