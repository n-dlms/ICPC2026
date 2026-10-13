> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 504 — SOLA: State-Aware Scheduling for LLM Serving (SLO attainment)

- Status: **unavailable** — paper not on arXiv under that name (arXiv searches for `"SOLA" SLO state-aware` return only astronomy, vision, and LLM-compression variants, none of which match "state-aware scheduling that outperforms vLLM" per the opencode description). Distilled from `src/opencode.md` lines 56-59. Reopen when a verified open PDF is located (record DOI); mark `unavailable` in `research/papers/manifest.json`.

## Claim

SOLA outperforms vLLM on SLO attainment by treating scheduler state (which decode iterations a request is in, where the KV cache lives) as the primary scheduling variable. Key insight: SLO-aware scheduling improves throughput for *small* tasks, which improves Average Normalized Turnaround Time (ANTT).

## Relevance

Moderate. SOLA's "state-aware" framing maps to the ICPC U-shape's per-request phase state machine: a request is in one of C_EP (edge prefill), COMM_E→C, C_CP (cloud compute), COMM_C→E, C_ED (edge decode) per decode round, done. This is *exactly* what `next_stage()` and the `seen`/`seen_sel` dedup machinery in `src/Solution_asort_frozen.cpp` already track. SOLA is the citation-body for the existing architecture, not a new algorithm.
The throughput-on-small-tasks insight is the most transferable bit — small requests should be prioritized to clear them out fast (the naive SJF/SPT insight), but under SLO constraints this is the SLO-MAEL subcase (paper 503).

## Algorithm sketch (per opencode description)

State-aware scheduling treats the (phase, kv_cache_state) tuple as the scheduler key. vLLM's FCFS-with-continuous-batching is stateless about *which* phase a request is in. SOLA maintains a per-request state machine and uses the state to decide admission and batch composition. Specific algorithm details not available without the paper; the opencode description is at the level of "state-aware beats vLLM on small-task throughput".

## Port plan

No new engine. Use as **citation support** for the existing phase-state tracking (`next_stage`, `dw_list`, `seen` dedup) in `src/Solution_asort_frozen.cpp`. When writing the strategy rationale in `docs/05-strategy-playbook.md`, cite SOLA as the literature anchor for treating phase as a first-class variable. Do NOT port a new algorithm without the paper.

## Weaknesses

- vLLM is single-cluster (no edge↔cloud split); SOLA's "state" is decode-iteration and KV cache placement within one engine. Our state space is fundamentally the bidirectional edge↔cloud round-trips across the U-shape, which SOLA does not model.
- "Throughput for small tasks" maps to SPT/SJF which the frozen solver already uses; SOLA's specific contribution over SPT is unclear from the second-hand description.

## Sources

- `src/opencode.md` lines 56-59 (algorithm description, mapping to Engine 1).
