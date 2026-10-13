> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 404 — FastServe: Preemptive Decode Scheduling with Skip-Join MLFQ

- arXiv: 2305.05920, "Fast Distributed Inference Serving for Large Language Models", Wu et al. (NSDI'26).
- `research/papers/2305.05920.pdf` / `.txt` fetched OK.

## Claim

LLM jobs are semi information-agnostic: output length unknown, but per-iteration (per-token) execution time is predictable and the first (prefill) iteration dominates. FastServe schedules preemptively at iteration granularity with a **skip-join MLFQ**: a new job joins the lowest priority queue whose quantum ≥ its predicted first-iteration time (skipping demotions for long prefills, approximating SRPT without knowing job size), gets preempted at token boundaries, and supports live migration / KV-cache swapping.
Up to 31.4× throughput vs vLLM under equal-latency SLOs; evaluates preemption costs and admission under load.

## Relevance

- **Ordering insight:** priority should reflect *remaining work vs deadline*, not arrival order — the paper approximates SRPT under unknown lengths. Our judge gives us Lout upfront (full information), so the skip-join trick (estimating size to set priority) is unnecessary; but the ordering principle is the same one SLAI formalizes with deadlines (note 403).
- **Preemption granularity:** FastServe preempts between iterations. Our protocol already grants decode tokens one at a time — we have the finest possible "preemption" for free (we re-decide every tick), so the ordering lever is *between grants*, not preemption itself.
- The paper's admission control (reject when a job cannot meet SLO under predicted load) matches our `doomed` mechanism and doc-05's Moore–Hodgson-style admission.

## Algorithm sketch

```
skip-join MLFQ:
  on arrival: init_time ← predicted prefill-iteration time
              queue[ p ] ← job,  p = min{ i : quantum_i ≥ init_time }   # long prefills skip ahead, short ones don't demote
  every iteration: run batch of highest-priority jobs (preempt at iteration boundaries)
latency model: E[T] under MLFQ with preemption ≈ SRPT when size distribution is long-tailed

```

## Port plan

- Nothing to port verbatim (no preemption, no unknown lengths). Two cheap ideas worth a variant build:
  1. **Priority = remaining-window pressure:** for DW requests, priority key `(D − t) − tokens_left·per` (= SLAI's op-due-date, note 403) — FastServe's rationale (long jobs deprioritized unless near deadline) is one line of reasoning for the same comparator change.
  2. **Queue-level admission:** FastServe admits based on predicted SLO feasibility at arrival — our `admit_prefills` est check is the deterministic version of this; no change needed.

## Weaknesses

- **Preemption/migration does not transfer:** our judge has no mid-stage preemption; in-flight DE/DC stages are committed, and the only "preemption" is not-granting the next token. FastServe's KV-swap overhead analysis is irrelevant.
- Unknown output length is a core premise (the whole skip-join design exists because Lout is unknown); the judge gives Lout exactly — the sophistication buys nothing.
- SLO model is a single global latency target with percentile objectives; ours is per-request absolute deadlines — SLAI's criticality is the right refinement of this idea for our setting.

## Sources

- arXiv:2305.05920; §4.1 Algorithm 1 (skip-join MLFQ) + Fig. 5 (iteration-time predictability), §4.2 (KV-cache swap), §6.2 (SLO-aware scheduling discussion), §7 (throughput results 31.4×).
