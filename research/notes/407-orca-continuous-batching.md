> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 407 — Orca: Continuous Batching (unavailable note, second-hand)

- Status: **unavailable** — Orca is OSDI'22 (USENIX), no open arXiv version. Do not use pirate mirrors.
- Full citation: G.-I. Yu, J. S. Jeong, G.-W. Kim, S. Kim, B.-G. Chun. "Orca: A Distributed Serving System for Transformer-Based Generative Models." Proc. 16th USENIX OSDI, 2022, pp. 521–538. https://www.usenix.org/conference/osdi22/presentation/yu
- Summary below is **second-hand** (from the USENIX page, Sarathi-Serve paper 2403.02310 §2.5, and vLLM/SGLang documentation).

## Claim (second-hand)

Orca introduced **iteration-level (continuous) batching** for LLM inference: instead of request-level batching (pad all requests to the longest in the batch, execute together until done), the scheduler re-selects the batch after every single model iteration, letting requests enter and exit dynamically. Combined with a **Fission inference engine** (CUDA graphs per iteration, "selective batching" of the memory-bandwidth-bound operations) it reached 2–20× higher throughput vs FasterTransformer at the same latency.
Its scheduling policy is prefill-prioritizing: new requests are eagerly prefilled at the first opportunity.

## Relevance (second-hand)

Continuous batching is the substrate that all later schedulers (Sarathi-Serve, SLAI, vLLM V1, our own decode-first ordering) build on; it is the reason "grant all feasible decodes every tick" is correct in our judge — iteration-level reselection is what our per-tick action loop already implements. Orca's flaw (prefill-priority stalls decodes) is exactly the bug our `grant_decodes`-first ordering avoids.

## Weaknesses (second-hand)

- Prefill-prioritizing schedule (no stall control) — shown by Sarathi-Serve/queueing work (notes 401/402) to inflate TBT and, in the non-chunked form, to be non-work-conserving under short-prompt load.
- No SLO/deadline model, no admission control, no edge/cloud dimension; GPU-only; KV memory treated at batch-size granularity.
- Details of the Fission engine (CUDA-graph selective batching) have no analog in our tick-capacity model.

## Sources

- https://www.usenix.org/conference/osdi22/presentation/yu (citation + abstract)
- Sarathi-Serve (arXiv:2403.02310) §2.5 + Algorithm 2 ("Iteration-level batching (vLLM)") for the mechanism description.
