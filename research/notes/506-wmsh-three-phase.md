> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 506 — Workflow Scheduling: WMSH (STH → PRH → LMH), Euro-Par 2007

- Status: **unavailable** — Euro-Par 2007 post-proceedings paper, Springer LNCS, not on arXiv. Distilled from `src/opencode.md` lines 80-84. Record DOI when a verified open copy is located; mark `unavailable` in `research/papers/manifest.json`.

## Claim

Three-phase classic heuristic for latency minimization under throughput constraints in workflow scheduling: STH (Satisfy Throughput) → PRH (Processor Reduction) → LMH (Latency Minimization). Given throughput constraint T, (1) verify if a non-pipelined schedule meets it; (2) if not, pipeline with task replication; (3) merge task-clusters to reduce processor count; (4) minimize latency by exploiting task parallelism.

## Relevance (Engine 1 fallback)

Maps to **Engine 1 (Greedy) as a robust fallback** when the spec is reconciled. The three-phase structure is exactly the Day-1 cadence in `docs/05-strategy-playbook.md`:

- **STH (Satisfy Throughput)** = admission control + feasibility ("every request meets its SLO"). The frozen solver's `doom` gate + admission control is the analog. Deal with the hard constraint first.
- **PRH (Processor Reduction)** = consolidate to minimal resources — for us, "don't spin up a pipeline bubble unless needed"; one edge↔cloud round-trip per request is the floor.
- **LMH (Latency Minimization)** = the throughput/SLO-attainment objective on top, where LOOKAHEAD/SLAI/EVT live.

The lesson: satisfy the hard constraint (SLO feasibility) first, *then* optimize the objective (throughput). This is the same ordering the archived working discipline (see README.md) already encodes ("Prefer correct, feasible, conservative actions over aggressive ones. Admission control beats forced fits."). WMSH is the citation anchor for that ordering.

## Algorithm sketch

```
WMSH(workflow W, throughput_target T):
  # Phase 1: STH — non-pipelined schedule meets T?
  s := schedule_no_pipeline(W)
  if throughput(s) >= T: return s           # done

  # Phase 2: PRH — pipeline with replication
  s := replicate_tasks_and_pipeline(W)       # task replication to hit T
  # Phase 2b: merge task clusters to reduce processors
  s := merge_clusters(s)

  # Phase 3: LMH — minimize latency on top
  s := exploit_parallelism(s)                # latency minimization on the feasible plan
  return s

```

## Port plan

No new code in `src/Solution.cpp`. Use as the **literature anchor for the Engine 1 fallback in `docs/05-strategy-playbook.md`** — the strategy is already structured this way (admission control + greedy dispatch + LOOKAHEAD refinement on top). When the spec drops and `tools/reconcile_spec.py --blast score_function` confirms an SLO-constrained-throughput objective (the most likely shape per docs/ASSUMPTIONS.md), cite WMSH as the prior art for STH→PRH→LMH ordering.

## Weaknesses

- Paper is for *offline workflow scheduling* (DAG, all tasks known); our problem is *online scheduling* (per-tick stdin feed — see `build/verify_build.sh` and the Day-5 input-loop finding). The order changes: in online scheduling, STH and LMH are interleaved per-tick, not sequential phases.
- The "pipelining with task replication" step is for a processor-count reduction problem we do not have — we have fixed edge capacity (E) and cloud capacity (C), no replication freedom.
- The cited 1-3 sentence "60% improvement" / specific speedup numbers are not in the opencode description; without the paper, we have only the structural lesson.

## Sources

- `src/opencode.md` lines 80-84 (algorithm description, Engine 1 mapping).
