# OPTIMIZATION ROADMAP — Research → Code Mapping

> **Historical note (post-contest).** I wrote this mapping on 2026-08-15 to direct my remaining experiments; the work below fed the solver that reached **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) originated the mapping decisions; AI sped up paper summaries and implementation under my direction.

Status: 2026-08-15. Re-mapping notes 401/405/406 (Sarathi / DistServe / SplitLLM) to
specific code changes. Each paper's ONE transferable insight was mapped to the policy in
`src/policies.cpp` / `src/scheduler.cpp` that implemented (or should have implemented) it.

KEY CONTEXT (from DECISIONS + build flags): the frozen binary `build/frozen_v5` = base +
**V3 (GreedyAllBatch)** + **V5 (SloAwareDelay)**, with **V2 (QueueAwareCloud) REJECTED**
and **V4 (AdaptiveChunk) REJECTED** by A/B on a seed-42 suite. The runtime_stress tool
(D-017) now exposes *transfer-saturated* and *bursty* regimes where the rejected
mechanisms (V2 transfer-aware backlog, V4 chunked interleave) plausibly matter — and
where the original A/B never looked. That is the research payoff: re-test V2/V4 on the
extremes, gated by the new score-regression zoo_gate (Failure 4).

---

## 1. Sarathi-Serve (note 401) → CHUNKING + DECODE-BATCHING axes

**Insight:** stall-free batching = decode-first; admit prefills only into decode slack
(never displace a decode window). Decode batching boosts throughput ~linearly; prefill
saturates alone.

**Code mapping:**

- Decode-batching half → **IMPLEMENTED & FROZEN** as `GreedyAllBatch` (V3, D-009):
  `group_size = n_ready`, decodes batched to capacity every issue. Matches Sarathi's
  "batching is free, grant all feasible."
- Prefill-into-slack half → **IMPLEMENTED** as `SloAwareDelay::hold_dpre` (V5, D-012):
  holds D_PRE when decode is comfortably ahead of SLO, freeing the local computer for
  P_PRE — exactly "fill prefill into decode slack."
- Chunked-prefill half → **REJECTED** as `AdaptiveChunk` (V4, D-010): splits prefill
  into 2 pieces to interleave local P_PRE work. Rejected on seed-42 (net −0.4%,
  zero collapses).

**Specific code change I made:** I RE-TESTED `AdaptiveChunk` (`make v4_chunk`) against
`frozen_v5` **only on the dribble / bursty regimes** (zoo `dribble.txt`, `burst_100.txt`,
and the runtime_stress `burstiness` axis). Theory said interleaving local prefill work
helped precisely when arrivals were dribbled/sparse — the regime the original A/B skipped.
Files: `src/policies.cpp` `AdaptiveChunk::pieces`, `src/state.cpp` piece emission.
(Corrected cost model — OPTIMIZATION_DOCTRINE §1.3 fix 2026-08-15: extra chunking cost
was **(c−1)·S with NO extra transfers** — made chunking strictly cheaper than the prior
doctrine assumed, *strengthening* the case for this re-test.)

## 2. DistServe (note 405) → BATCHING axis + GOODPUT framing

**Insight:** decode batching is free (bandwidth-bound) → always batch decodes to
capacity; prefill batching is mostly useless. Goodput (meet-ALL-SLO rate) is the real
objective → SLO attainment first, latency terms second.

**Code mapping:**

- Decode-to-capacity → **IMPLEMENTED & FROZEN** (`GreedyAllBatch`, V3). No change.
- Goodput framing → **IMPLEMENTED** in the score `on_time − 1e-6·V` and operationalized
  by `SloAwareDelay` (V5), which reorders by SLO slack. Matches DistServe's
  "optimize SLO attainment first."
- M/D/1 utilization probe (`rho = N·avg_per/T / min(Ecap,Ccap)`) → **optional debug aid**
  to classify cases (underloaded → ordering barely matters; near 1 → admission dominates).
  Not submission code.
- `MarginalBatch` (`V3_MARGINAL`) = the superlinear-table hedge; identical to GreedyAll
  on all sublinear judge tables so far. Enable only if a judge table proves superlinear.

**Specific code change I made:** NONE required — DistServe's insight was already in the
frozen binary. I kept `GreedyAllBatch` and promoted `MarginalBatch` only on evidence of
superlinear decode tables. File: `src/policies.cpp`.

## 3. SplitLLM (note 406) → CLOUD-ASSIGNMENT (TRANSFER-AWARE) axis

**Insight:** per-request end-to-end latency = Σ(compute + transfer) along the
edge↔cloud chain; assign each request to the placement minimizing this chain-sum
(transfer-aware), not round-robin/blind load.

**Code mapping:**

- `QueueAwareCloud` (`V2_TRANSFER`) **IMPLEMENTED** — `choose()` picks the cloud with
  the smallest predicted ready-time = `busy_until + Σ pending P_PROC frac + D_PROC
  backlog`. This IS the chain-sum/transfer-aware assignment SplitLLM prescribes.
- **REJECTED** (D-008/D-011) on seed-42: the backlog term almost never differs from
  `busy_until` at P_PRE issue time (clouds' ready lists typically empty) → 263/327 ties,
  noise. So the frozen binary uses `LeastLoadedCloud`.

**Specific code change I made:** I RE-TESTED `QueueAwareCloud` (`make v2_transfer`) against
`frozen_v5` **on the transfer-saturated and bursty regimes** (zoo `transfer_bottleneck.txt`,
`burst_100.txt`; runtime_stress `transfer_ratio` axis where clouds' ready lists WERE
non-empty and backlog diverged). This was the direct, research-motivated re-evaluation:
SplitLLM said transfer-aware placement mattered under heavy transfer load, which the
original seed-42 A/B never exercised. File: `src/policies.cpp` `QueueAwareCloud::choose`.

---

## Gate for every V2/V4 re-enable (as I enforced it)

1. I built the candidate (`make v2_transfer` / `make v4_chunk`).
2. `python3 tools/zoo_gate.py --solver build/<cand> --ref build/frozen_v1 --regress 0.9`
   — had to pass 22/22 (no verdict fail, no score regression vs V1).
3. `python3 tools/runtime_stress.py --solver build/<cand> --steps 6` on the specific
   axis (transfer_ratio / burstiness) — had to not introduce a verdict break.
4. `tools/ab_test.py` vs `frozen_v5` on the targeted regime only (dribble/burst or
   transfer) — I accepted only on structural (>10pt) wins there, not noise.
5. I wrote the DECISIONS entry and re-froze. No self-merge.
