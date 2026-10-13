> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 509 — Cloud-Edge Collaborative Task Scheduling with Model Segmentation (Springer 2024, NSGA-III)

- Status: **unavailable** — Springer 2024 paper, no open arXiv preprint located. Distilled from `src/opencode.md` lines 95-99. Record DOI when a verified open copy is located; mark `unavailable` in `research/papers/manifest.json`.

## Claim

NSGA-III-based multi-objective optimization for choosing model segmentation points (where to split the LLM between edge and cloud). MTMGMO algorithm improves calculation speed by 60% over single-threaded multi-group computation. Finds optimal segmentation points via pre-execution profiling.

## Relevance (Engine 3 conditional, ONLY if spec allows variable split points)

This is the **conditional Engine 3 (Flow)** candidate per opencode line 99. The frozen solver assumes a *fixed U-shaped split* (first/last layers on edge, middle in cloud) per the archived project framing (see src/ARCHITECTURE.md). If the spec (Aug 14) allows **variable split points** (the edge takes the first K layers and the last L layers, with K and L as decision variables per request or per deployment), this paper is the algorithm: NSGA-III multi-objective optimization over (K, L) split choices with the profiled completion-time as the objective.

If the spec **fixes** the split (the U-shape is mandated by the problem), this paper is reduced to reference — there is no decision variable to optimize, only scheduling given fixed phases.

## Algorithm sketch

NSGA-III (Non-dominated Sorting Genetic Algorithm III) with multiple groups, optimizing a multi-objective function over the (split_point, batch_size) decision space:

```
P := initial_population()
for g in 1..G:
    offspring := mutate_and_recombine(P)         # MTMGMO: multi-group parallel
    P := nondominated_sort(P ∪ offspring)         # NSGA-III reference-point selection
    P := truncate_to_pop_size(P)
return best(P)

```

MTMGMO innovation: parallel multi-group computation that runs ~60% faster than single-threaded multi-group (still a serial-pop GA, not multi-threaded).

## Port plan

**Conditional, post-spec**. Do NOT implement before the spec drops. If `tools/reconcile_spec.py --blast split_point` shows split points are decision variables, sketch Engine 3 (Flow) with NSGA-III over (K, L). Otherwise skip. The implementation cost is high — NSGA-III is ~300 lines, profiling the segmentation points is its own offline step. Ensure GA acceptance uses the same robust-dominance rule as LNS (paper 505): no class regression on `cases/adversarial/*`, sum ≥ baseline on `cases/*`.

## Weaknesses

- **Multi-threaded** MTMGMO is FORBIDDEN per the archived working discipline (see README.md) ("The model does not know the judge timing regime... Never let the model suggest multi-threading, `std::jthread`, or wall-clock-dependent cutoffs"). The 60% speedup comes from parallel groups; we cannot port that. The serial NSGA-III is correct and slower.
- NSGA-III overhead per iteration is high; the per-test-case budget (~5s CPU per `research/day7/` matrix) may allow only a few hundred GA generations — too few for convergence. `tools/runtime_stress.py` is the gate.
- Pre-execution profiling is offline; we can't profile inside the judge. Either pre-profile the family of split points at Day 0 (when the spec is known) and ship a lookup table, or skip NSGA-III entirely and use the greedy CrossPipe variant (paper 502) as the Engine 3 without variable splits.
- The decision variable only exists if the spec allows it. Verify UNKNOWN `protocol/protocol.yaml` `split_point` BEFORE implementing.

## Sources

- `src/opencode.md` lines 95-99.
