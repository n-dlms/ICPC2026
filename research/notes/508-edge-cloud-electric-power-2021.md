> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 508 — Edge-Cloud Collaborative Inference Scheduling for Electric Power Image Recognition (IEEE 2021)

- Status: **unavailable** — IEEE 2021 paper, no open arXiv preprint located. Distilled from `src/opencode.md` lines 90-94. Record DOI when a verified open copy is located; mark `unavailable` in `research/papers/manifest.json`. Do NOT use sci-hub.

## Claim

Edge-cloud collaborative inference scheduling for image recognition in electric power systems. Objective: shortest completion time using edge-cloud collaboration to balance load. Provides a completion-time formulation for edge-cloud inference.

## Relevance

Directly addresses edge-cloud collaborative inference scheduling. Most useful as a **reference baseline** for the edge-cloud communication model: their completion-time formulation is a candidate for `case_gen.durations()` and the frozen solver's `doom` estimate. (Note EXP-42 Finding B: the current `durations()` uses a naive phase-sum that overestimates by ~1/3 due to phase pipelining in `fast_judge.step()` — a poorly-formulated completion-time model regresses the doomed-gate, so an external reference baseline is useful for sanity-checking, not directly adopting.)

## Algorithm sketch

Specific algorithm not in the opencode description. The transferable artifact is the **completion-time formulation**:

```
completion_time(req) = arrival + edge_prefill + comm_E_to_C + cloud_compute + comm_C_to_E + edge_decode * n_decode_tokens

```

This is structurally identical to the frozen solver's `est = t + pe + COMM + pc + COMM + Lout*per` in the `doom` check — IEEE 2021 is the *literature anchor* for the formula, not a new formula.

## Port plan

No new code. Use as **reference for the completion-time formulation** in `docs/05-strategy-playbook.md` and as the citation for the `doom` gate's formula shape.
When the spec is reconciled and `tools/engine0_probe.py` extracts the real completion semantics, cross-check against this paper's formulation; if the spec prefill and decode have explicit staged completion (token-level rather than phase-level), the formula needs updating in `case_gen.durations()` and in the frozen solver's `doom` — that is a Day-1 freeze-decision item, NOT pre-spec (the frozen solver is sha-pinned per docs/DECISIONS.md, do not edit).

## Weaknesses

- Domain is image recognition (a single CNN forward pass per inference), not autoregressive LLM inference (multiple decode rounds). The completion-time formula transfers; the *multi-round decode loop* of the U-shaped LLM split does not — IEEE 2021 has no "edge decode round per generated token".
- "Shortest completion time" objective is makespan-style; our scoring is throughput/SLO-attainment, a different objective per `tools/score_probe.py`. Do not adopt their objective.

## Sources

- `src/opencode.md` lines 90-94.
