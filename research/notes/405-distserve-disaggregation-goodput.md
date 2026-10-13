> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 405 — DistServe: Prefill/Decode Disaggregation, Goodput, and M/D/1 Prefill Analysis

- arXiv: 2401.09670, "DistServe: Disaggregating Prefill and Decoding for Goodput-optimized Large Language Model Serving", Zhong et al. (OSDI'24).
- `research/papers/2401.09670.pdf` / `.txt` fetched OK.

## Claim

Co-locating prefill and decode on one GPU couples TTFT and TPOT — under either SLO, the other is wasted. DistServe disaggregates the phases onto separate instances and co-optimizes (a) resource allocation (GPUs per instance) and (b) parallelism choice (intra-op vs inter-op per phase), all under per-phase SLOs, optimizing **goodput** = request rate meeting ALL latency SLOs (vs raw throughput). 7.4× more requests at same SLO, 12.6× tighter SLOs at same load vs colocated systems. Two analysis results transfer:

1. **M/D/1 prefill queue:** with one prefill request saturating the GPU, FCFS + uniform prefill time D gives `Avg_TTFT = D + RD²/(2(1−RD))` — at low load, execution time dominates (favor fast execution); at high load, queueing delay dominates (favor throughput). The utilization condition `RD < 1` is the admission ceiling.
2. **Critical input length Lm:** prefill batching helps only below Lm (below it prefill is memory-bound, batching is free; above it the phase is compute-bound and batching just proportionally extends every request in the batch). DistServe's observation: decode batching is essential (bandwidth-bound), prefill batching is mostly useless.

## Relevance

- **Goodput is our objective:** our score `on_time − 1e-6·V` is a goodput-constrained throughput objective (doc 07). DistServe's framing justifies optimizing SLO attainment first, latency terms second — the order our score enforces.
- **M/D/1 bound:** for our judge, a sanity ceiling: if arrivals exceed `min(Ecap,Ccap)/per` per tick sustained, no policy survives; cases likely sit near this (where the interesting scheduling questions live). Also: at high utilization, *queueing order* (which request waits) matters far more than per-request speed differences — consistent with SLAI's criticality ordering being the highest-leverage lever.
- **Lm analog:** our prefill is a bulk stage per request (pe ticks on edge, pc on cloud, ALPHA-fixed). Batching multiple prefills simultaneously is *not* free in our model (each consumes capacity), so DistServe's "don't batch prefills" maps to: don't admit prefills just to fill idle capacity when decode-critical work is pending — again the note-401/403 theme.

## Algorithm sketch

```
per phase SLO: TTFT_SLO for prefill instance, TPOT_SLO for decode instance
allocation search:  enumerate (np, nd) GPUs + parallelism configs, pick max goodput
                    via simulator; placement constraints for KV handoff bandwidth
prefill queue:  M/D/1:  E[TTFT] = D + R·D²/(2(1−R·D)),  feasible iff R·D < 1
decode batching: always batch to capacity (bandwidth-bound ⇒ batch is free)

```

## Port plan

- Nothing structural ports (we cannot disaggregate or choose parallelisms — stages are fixed by protocol). Two planning aids only:
  1. Per-case utilization probe (debug tool, not submission): print `rho = N·avg_per/T / min(Ecap,Ccap)` to classify cases (underloaded → ordering barely matters; near/over 1 → admission control dominates). Useful for tuning which variants to run per case profile.
  2. Confirm the decode-grant rule "always grant all feasible" (batching is free) is kept — it is.

## Weaknesses

- **Disaggregation itself does not transfer:** the protocol fixes the stage sequence PE→PC→DE→DC on edge+cloud; we cannot move prefill off the shared sites, and KV-handoff/bandwidth placement are non-issues (COMM is a free latency).
- M/D/1 assumes Poisson arrivals, FCFS, single server, uniform jobs — our arrivals are adversarial per-case, deadlines are absolute, and the two sites are coupled through COMM chains.
- TTFT/TPOT per-phase SLO decomposition doesn't exist here; we have one end-to-end deadline per request (we must do the decomposition ourselves — note 403's op-due-date).
- Goodput optimization in the paper is offline (allocation search before serving); ours is online per tick.

## Sources

- arXiv:2401.09670; §3.1 (prefill analysis, M/D/1, Eq. 1-3), §3.2 (decode batching, bandwidth-bound), §3.3 + Fig. 6 (critical input length Lm), §4 (allocation algorithms), §6.3 (goodput eval, 7.4× / 12.6×).
