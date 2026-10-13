> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Reading plan below was executed pre-contest and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 600s — Pre-Friday Reading List + Day-0 Runbook Update

Per opencode lines 162-172. The spec dropped Friday (Aug 14, 2026). Two days before: the priority papers' formulation sections were read (completed pre-contest). Day 0: the spec was mapped to Paper 1's formulation; if match > 80%, frozen solver was valid; if < 80%, pivot to the Engine 1 + 2 portfolio.

## Priority reading order (next 2 days per opencode)

1. **"Throughput-Optimal Scheduling Algorithms for LLM Inference and AI Agents" (2025)** — Read Sections 1-2 only. Understand the prefill/decode interference model.
   - Already distilled: `research/notes/402-work-conserving-queueing-theory.md` (arXiv 2504.07347). Sections 1-2 are the work-conservation theorem for LLM inference. Read the note, then re-scan the paper Sections 1-2 if anything is unclear.

2. **"Zero Bubble Pipeline Parallelism" (2023)** — Read Sections 2-3. Understand the warm-up / steady-state / tail scheduling pattern (the 1F-1B-1W pattern, the ZB-V V-shaped allocation).
   - Distilled: `research/notes/501-zero-bubble-pipeline-parallelism.md` (arXiv 2401.10241). Read the note + paper Sections 2-3 for the algorithm details.

3. **KAIST SLO-MAEL paper** — Read the aging mechanism. Implement it in ~20 lines of C++.
   - Distilled from second-hand description: `research/notes/503-slo-mael-aging.md` (paper not on arXiv; reopen if open PDF found). Read the note's algorithm sketch; the ~20-line port plan is concretely there.

4. **CrossPipe (USENIX ATC 2025)** — Read Section 4.2 (greedy algorithm). It's the LNS initialization for Engine 2.
   - Distilled: `research/notes/502-crosspipe-cross-datacenter-pipeline.md` (arXiv 2507.00217). Read the note + paper Section 4.2 for the `CrossUDSub` greedy algorithm.

5. **Euro-Par WMSH** — Read the three-phase heuristic (STH → PRH → LMH). It's the Engine 1 (greedy) fallback logic.
   - Distilled from second-hand description: `research/notes/506-wmsh-three-phase.md` (paper not on arXiv). Read the note's algorithm sketch.

Per opencode line 171: "Do not read these cover-to-cover. Read the problem formulation, the algorithm pseudocode, and the evaluation." One day reading, one day scaffolding the algorithms into the engine interfaces (Engine 1 aging, Engine 2 LNS init from CrossPipe greedy, Engine 3 flow per ZB-V *conditional* on spec structure, Engine 0/4 already implemented).

## Day-0 runbook step (NEW from opencode line 172)

Insert this as Step 0 of the Day-0 runbook (`research/notes/day0-runbook.md`):

> **Step 0 (NEW, post-opencode):** Map the spec to Paper 1's formulation (`research/notes/402-work-conserving-queueing-theory.md` and arXiv 2504.07347 Sections 1-2).
>
> - If match > 80% (the problem is throughput-optimal under SLO constraints with explicit prefill/decode phase interference, online arrival model, work-conservation being the theorem): the frozen solver (`src/Solution_asort_frozen.cpp`) is **valid** as-is; A/B test it against the spec's sample inputs and proceed to the standard Day-0 cadence (reconcile assumptions → bank a safety-net submission → characterize the scoring landscape → freeze early).
> - If match < 80% (the spec is *not* a work-conservation-throughput-SLO problem — e.g. baseline-relative scoring, per-phase time budgets, variable split points, chunked prefill, asymmetric delays, edge memory budgets): **pivot** to the Engine 1 + 2 portfolio (Engine 1 greedy with aging as the safety net, Engine 2 LNS as the refiner, Engine 3 flow *conditional* on the structural mismatch type).
>
> The match-test is a qualitative decision by the human, not a model verdict. The archived AI guardrail (docs/DECISIONS.md): "Never let AI decide the freeze decision." Review the spec text and map each clause to a contract.yaml field (`protocol/protocol.yaml`); each UNKNOWN is a flag for the pivot question.

## Day-1 cadence summary (existing, docs/DECISIONS.md "When the spec arrives")

1. Parse the spec → fill `docs/05-strategy-playbook.md` day-1 checklist + `problem/contract.yaml` + `protocol/protocol.yaml` + `limits.yaml`.
2. Build the frozen-candidate validation: `python3 tools/freeze_gate.py --candidate build/solution_frozen --baseline build/solution_frozen` (sanity: identity).
3. Bank the safety-net submission early (greedy Engine 1 or the frozen winner if Step-0 match > 80%).
4. Architecture: greedy feasible incumbent → budgeted local search with safe CPU cutoff → full-information re-optimization (transactional accept: only full legal plans that strictly improve the objective).
5. Selection discipline (`docs/08-validation-lab.md`): 20-case regression suite = freeze gate; DEV pool = development; SECRET pool = one-look per freeze candidate. Accept = robust dominance (valid, sum ≥ baseline, no class regression beyond tolerance).

## Day-1 selection guidance

- **If Step-0 match > 80%:** keep the frozen solver; don't rewrite. The system is insurance; if the frozen solver matches the problem, ship it.
- **If Step-0 match < 80%:** start from the frozen solver, but each UNKNOWN dimension (baseline-relative scoring, per-phase budgets, variable split points, chunked prefill, asymmetric delays, edge memory) is a candidate experiment. A/B test each in isolation against the frozen baseline, accept with robust dominance only. The frozen solver remains the safety-net re-freeze candidate if experiments regress.

## Sources

- `src/opencode.md` lines 162-172 (reading list + Step 0 instruction).
- `research/notes/day0-runbook.md` (existing runbook — append Step 0).
- `docs/05-strategy-playbook.md` (existing day-1 checklist).
- docs/DECISIONS.md "When the spec arrives", "Submission Strategy (Pre-Spec)".
