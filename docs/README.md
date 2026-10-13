# Documentation Index

This index covers the engineering record for my (Ntokozo Dlamini's) ICPC 2026 Online Challenge 1 solution, which finished 56th worldwide (top 1.3%, #56) after the contest ended Aug 28 2026.

For the problem, solution, results, and build instructions, start with the [repository README](../README.md).

The solution ideas and decisions are mine — I (Ntokozo Dlamini). AI assisted with communication,
implementation, research summaries, and verification under my direction.

## Core records

| Document                                                   | Contents                                                                                                                    |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| [DECISIONS.md](DECISIONS.md)                               | **Decision log D-001–D-162:** algorithmic changes with context, alternatives, evidence, and rollback plans.                 |
| [CF_JUDGEMENT_2026-08-16.md](CF_JUDGEMENT_2026-08-16.md)   | Official Codeforces judgement protocol, run 1 (13,577.064) — all 22 preliminary tests, per-test metrics.                    |
| [CF_JUDGEMENT_2026-08-17.md](CF_JUDGEMENT_2026-08-17.md)   | Official judgement protocol, runs 2–4 (14,775 → 14,778.987) with per-test attribution.                                      |
| [HIDDEN_ATLAS.md](HIDDEN_ATLAS.md)                         | Inferred hidden-test parameters, including per-test estimates for weights, SLOs, distance baselines, and throughput bounds. |
| [ASSUMPTIONS.md](ASSUMPTIONS.md)                           | Spec facts, assumptions, unknowns — CONFIRMED / ASSUMED / UNKNOWN status legend.                                            |
| [BUG_LOG.md](BUG_LOG.md)                                   | Recorded defects, root causes, fixes, and verification.                                                                     |
| [INVESTIGATION_2026-08-17.md](INVESTIGATION_2026-08-17.md) | D3 trigger refinement and score-gap structural analysis (five failure families).                                            |

## Strategy and preparation

| Document                                                           | Contents                                                                                                                       |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| [OPTIMIZATION_PLAYBOOK.md](OPTIMIZATION_PLAYBOOK.md)               | Pre-contest approach to algorithm design, score analysis, protocol architecture, and performance engineering.                  |
| [OPTIMIZATION_CORRECTIONS.md](OPTIMIZATION_CORRECTIONS.md)         | Design corrections to the problem model (FIFO queues, setup cost S, TDR/TPOT tradeoffs, grouping). |
| [OPTIMIZATION_ROADMAP.md](OPTIMIZATION_ROADMAP.md)                 | Research → code mapping (Sarathi / DistServe / SplitLLM insights to policy axes).                                              |
| [LLM_SERVING_TECHNIQUES.md](LLM_SERVING_TECHNIQUES.md)             | Practical guide to pipelining, batching, and flow control for edge–cloud collaboration.      |
| [04-scheduling-theory-toolkit.md](04-scheduling-theory-toolkit.md) | Classical scheduling theory: flow shop / Johnson, EDD/EDF, Moore–Hodgson, list scheduling, pipeline steady-state analysis.     |
| [05-strategy-playbook.md](05-strategy-playbook.md)                 | Synthesis: concrete scheduler architecture & mechanisms.                                                                       |
| [06-past-winners.md](06-past-winners.md)                           | How past winners actually won (2021–2026): techniques, postmortems, the overfitting controversy.                               |
| [08-validation-lab.md](08-validation-lab.md)                       | Selection discipline: pools, freeze gate, acceptance rules.                                                                    |
| [09-problem-day-intake.md](09-problem-day-intake.md)               | Problem-Day Intake System: contract extractor, architecture mapper, 60-min baseline, freeze gate.                              |
| [TESTING_FORTRESS_DESIGN.md](TESTING_FORTRESS_DESIGN.md)           | Local judge, zoo cells, gates — design document.                                                                               |

## Advisor reviews (external analyses)

| Document                                                                     | Contents                                                                                                                            |
| ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| [advisor-reports/PROFESSOR_REVIEW.md](advisor-reports/PROFESSOR_REVIEW.md)   | Historical structural review: reported findings, scheduling models, proposed changes, and validation order. The original transcript is linked from the review. |
| [advisor-reports/CF_SHEET_ANALYSIS.md](advisor-reports/CF_SHEET_ANALYSIS.md) | Historical Codeforces feedback analysis: test classes, proposed experiments A–E, and the evidence each would need. The original transcript is linked from the analysis. |

## Research programs

| Document                                                                             | Contents                                                                                         |
| ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ |
| [../research/README.md](../research/README.md)                                       | Research pipeline: scope → fetch → extract → distill → apply.                                    |
| [../research/scoring/SYNTHESIS.md](../research/scoring/SYNTHESIS.md)                 | Scoring & physics synthesis: authoritative formula, physics laws, point ledger, structural caps. |
| [../research/techniques/PROGRAM_STATUS.md](../research/techniques/PROGRAM_STATUS.md) | SB01–SB10 computation-module status tracker.                                                     |
| [../research/papers/USAGE.md](../research/papers/USAGE.md)                           | Map of how the 71 archived papers informed the codebase.                                         |
