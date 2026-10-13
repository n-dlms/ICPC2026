> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 401 — Sarathi-Serve: Stall-Free Batching and Uniform Batches

- arXiv: 2403.02310 (OSDI'24), Agrawal et al., Microsoft.
- `research/papers/2403.02310.pdf` / `.txt` fetched OK.

## Claim

LLM serving has a throughput–latency tradeoff: prefill-prioritizing schedulers (Orca, vLLM) improve throughput but inflate TBT (time-between-tokens); decode-prioritizing ones (FasterTransformer) do the opposite. Sarathi-Serve resolves it with **chunked prefills + stall-free batching**: every batch = all running decodes + as much prefill chunk as fits in a per-iteration **token budget** derived from the TBT SLO, so decodes never stall on prefills. Uniform token-count batches also minimize pipeline bubbles.
2.6–3.7× serving capacity (Mistral-7B/Yi-34B), 5.6× with pipeline parallelism (Falcon-180B) vs vLLM/Orca under tail-latency SLOs.

## Relevance

This is the closest published *scheduler structure* to our judge. Our solver already mirrors its two core rules (decode-first, then fill leftover with prefill — see §4.2 Algorithm 3, lines 6-20): `grant_decodes()` runs before `admit_prefills()`. What matters for us:

- The token budget τ (§4.3) is the analog of our per-tick capacities Ecap/Ccap; the "budget is too small → excessive chunking overhead" warning maps to our fixed PEDGE/PCLOUD stage sizes (we cannot chunk, so that half of the tradeoff is fixed by the protocol).
- "Batching boosts decode throughput ~linearly, prefill saturates alone" (Takeaway-1, Fig. 3): in our tick model this says decode grants are free to co-schedule — grant ALL feasible DW requests every tick, never hold one back to "save capacity" (our solver does this).
- Uniform batches ⇒ minimal pipeline bubbles: our stages are already uniform quanta (PEDGE/PCLOUD per token), so bubble minimization reduces to *keeping both sites non-empty* (≥2 requests in flight, doc 03) and *admitting prefills into decode slack, not into decode-critical windows*.

## Algorithm sketch

```
per iteration:
  τ ← compute_token_budget(TBT_SLO)          # profiling-based
  nt ← 0
  batch ← {all running decodes}              # decode-first, lines 6-8
  nt ← #decode tokens
  batch ← {partial prefills fitting in τ}    # lines 9-12
  for new request R in admission order:
      c ← max chunk of R fitting in τ − nt   # lines 13-20
      if c == 0: break
      nt ← nt + c
  process batch                              # uniform token load

```

## Port plan

- Nothing to port for ordering *within* a phase (we already do decode-first). Two variants worth A/B-testing (compile-time `-D` switches, `tools/ab_test.py`):
  1. **Admission timing (Sarathi-fill):** when the decode queue is non-empty and capacity is tight, defer prefill admission by a tick or two (start at the first tick where the prefill's windows fit without displacing any decode window). Mirrors "fill the leftover budget" — prefills only use capacity that decodes won't need.
  2. **Slack-aware token budget:** currently the per-request feasibility test uses serial `per = PEDGE+COMM+PCLOUD+COMM`; a variant that books decode tokens in *batches of consecutive ticks* (grant K tokens at once when K consecutive windows fit) reduces the number of decision points — not expected to change score (grants are idempotent windows) but halves bookkeeping.
- Both are A/B-tested against the greedy EDD baseline on the local suite; accept only if `ab_test` exits 0.

## Weaknesses

- **Chunking does not transfer:** pe/pc split is fixed by protocol (ALPHA); there is no chunk-size knob, no KV-cache reload cost, no tile-quantization effect. The entire τ-tradeoff (chunk too small → overhead) is absent; our τ is just hard capacity.
- The token budget is about *compute slack within one GPU iteration*; our model has no such slack — a tick of edge capacity is all-or-nothing.
- No deadline ordering: Sarathi admits requests in admission order (FCFS within the loop); it has no SLO-per-request ordering theory (that is SLAI/FastServe territory, notes 403/404). All requests share one TBT SLO in the paper.
- Throughput/latency claims are empirical; no optimality proof (the queueing proof is note 402).

## Sources

- arXiv:2403.02310; §3.1 Takeaway-1 (batching asymmetry), §4.2 Algorithm 3 (stall-free batching), §4.3 (token budget), Fig. 3/Fig. 9 (28.3× TBT inflation without chunking), §5.1 (capacity results).
