# RESEARCH CATALOG — ICPC 2026 Edge--Cloud Collaborative Scheduling

> Post-contest archive: prep completed Aug 14; contest ended Aug 28 2026, final rank #56 worldwide (top 1.3%). Author: Ntokozo Dlamini (assistant sped up fetching/summaries/checks).

**Session**: 2026-08-14 (Fri), 14:38–15:27 SAST (UTC+2) — Contest Day 0.
**Purpose**: Single reference document for every research finding that may influence scheduling policy. All claims traceable to files in `research/papers/*.txt` (verbatim quotes) — no unverified claims.
**Note files** (timestamped at write time, SAST):

| Note                         | Written | Coverage                                                                          |
| ---------------------------- | ------- | --------------------------------------------------------------------------------- |
| `notes-serving-systems.md`   | 15:26   | vLLM, Orca, PerLLM, TaiChi, Fairness-Aware Chunked Prefill, Sarathi OSR           |
| `notes-queueing.md`          | 15:21   | P-K/Kingman, M/D/1, batch-service queues, tandem cycle, arrival smoothing         |
| `notes-pipeline-chunking.md` | 15:23   | GPipe, PipeDream, 1F1B/2BW, LoongServe, Layered Prefill, chunked-prefill fairness |
| `notes-batching-setup.md`    | 15:22   | Setup-cost batching surveys, p/s-batching theory, batch-sizing rules              |
| `notes-edge-cloud.md`        | 15:16   | PerLLM, SLICE, Scorpio, Kairos, WISP, Edge-SLM/Cloud-LLM survey                   |
| `phase2-synthesis.md`        | 14:55   | Pre-research score-formula synthesis (closed-form μ_tpot, dead zones)             |

Prior-session theory notes (already committed, `research/notes/1xx–3xx`): online packet scheduling w/ deadlines, throughput maximization identical/unrelated machines, any-order interval selection, flow shop exact delays, EDF/LLF, lookahead. `docs/04-scheduling-theory-toolkit.md` holds the SLICE/queueing toolkit digest.

---

## 1. Paper Inventory (added 2026-08-14)

### 1.1 Serving systems (topic: serving)

| File                           | Paper                                                                                 | Year/Venue |
| ------------------------------ | ------------------------------------------------------------------------------------- | ---------- |
| `2309.06180`                   | vLLM — Efficient Memory Management for LLM Serving w/ PagedAttention                  | SOSP 2023  |
| `osdi22-yu`                    | Orca — Distributed Serving System for Transformer-Based Models (USENIX OA, not arXiv) | OSDI 2022  |
| `2405.14636`                   | PerLLM — Personalized Inference Scheduling w/ Edge-Cloud Collaboration                | 2024 arXiv |
| `2508.01989`                   | TaiChi — Prefill-Decode Aggregation or Disaggregation?                                | 2025 arXiv |
| `2606.09061`                   | Fairness-Aware & Latency-Controllable Chunked-Prefill Scheduling                      | 2026 arXiv |
| `agrawal-chunked-prefills-osr` | Sarathi authors — Efficient LLM Inference via Chunked Prefills (NSF PAR OA)           | SIGOPS OSR |
| `2403.02310` (pre-existing)    | Sarathi-Serve                                                                         | 2024       |
| `2401.09670` (pre-existing)    | DistServe                                                                             | 2024       |
| `2305.05920` (pre-existing)    | FastServe                                                                             | 2023       |
| `2410.10759` (pre-existing)    | SplitLLM                                                                              | 2024       |

SLaI (2405.18081) = unrelated paper; no real "SLaI: Speculative Looping" exists on arXiv — rejected. SLaI scheduler is embedded in `2508.01002` (pre-existing).

### 1.2 Queueing (topic: queueing)

| File                           | Paper                                              | Notes      |
| ------------------------------ | -------------------------------------------------- | ---------- |
| `adan-resing-queueing-theory`  | TU/e lecture notes (P-K, N-policy, group arrivals) |            |
| `mit-modiano-mg1-vacations`    | MIT M/G/1 + vacations notes                        | M/D/1, P-K |
| `krivulin-tandem-cycle-time`   | arXiv 1212.5309 tandem mean cycle time             | Thm 7      |
| `batch-service-map-vacation`   | arXiv 2208.13043 GBS batch-service rule            |            |
| `pk-kingman-modern`            | arXiv 2605.27202 P-K Eq.19 + Kingman Eq.21         |            |
| `flattening-the-curve`         | arXiv 2004.09645 arrival smoothing                 |            |
| `sasikala-bulk-service-survey` | Bulk-service queue survey                          |            |
| `1711.00081` (pre-existing)    | 2-machine flow shop w/ exact delays                |            |

### 1.3 Pipeline/chunking (topic: pipeline)

| File                        | Paper                                                       | Year/Venue                                                                            |
| --------------------------- | ----------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| `1811.06965`                | GPipe                                                       | NeurIPS 2019                                                                          |
| `1806.03377`                | PipeDream                                                   | SOSP 2019                                                                             |
| `2006.09503`                | 1F1B / 2BW memory-efficient pipeline                        | ICML 2021                                                                             |
| `2404.09526`                | LoongServe (elastic sequence parallelism, dynamic chunking) | 2024 (note: 2311.03299 & 2410.14796 are wrong IDs; BurstPipe does not exist on arXiv) |
| `2510.08055`                | Layered Prefill                                             | 2025                                                                                  |
| `2606.09061`                | (shared w/ serving) Fairness-Aware Chunked Prefill          | 2026                                                                                  |
| `2401.10241` (pre-existing) | Zero Bubble                                                 | 2024                                                                                  |
| `2507.00217` (pre-existing) | CrossPipe                                                   | 2025                                                                                  |

### 1.4 Batching/setup (topic: batching)

| File                                 | Paper                                         | Notes                     |
| ------------------------------------ | --------------------------------------------- | ------------------------- |
| `Allahverdi2008-setup-survey`        | Survey: scheduling w/ setup times/costs       | green OA (PolyU IR)       |
| `Agnetis2025-fifty-years-scheduling` | §3.8 digest of Potts & Kovalyov 2000 (HAL OA) | P&K 2000 itself paywalled |
| `BakerTrietsch2009-notes-ch13`       | Author-hosted notes on Webster & Baker 1995   | W&B paywalled             |
| `2102.02002-parallel-batching`       | p-batching family                             |                           |
| `1810.01223`                         | batch setup times                             |                           |
| `2503.23273`                         | serial-batch Pareto                           |                           |
| `1911.12350`                         | batch + tardiness                             |                           |
| `2504.08793`                         | s-batch min batch size                        |                           |

Paywalled, covered via legal secondary sources: Potts & Kovalyov 2000, Webster & Baker 1995, Fowler & Mönch 2022.

### 1.5 Edge-cloud / SLO (topic: edge-cloud)

| File                        | Paper                                                                          | Notes                           |
| --------------------------- | ------------------------------------------------------------------------------ | ------------------------------- |
| `2405.14636`                | (shared) PerLLM — edge-cloud collaboration                                     | actual title differs from brief |
| `2510.18544`                | SLICE — SLO-Driven Scheduling for LLM Inference on Edge Devices                | edge-focused                    |
| `2505.23022`                | Scorpio — Workload-Aware Global Scheduling                                     |                                 |
| `2605.02329`                | Kairos — Taming Request Imbalance (2604.16682 is power-mgmt KAIROS — rejected) |                                 |
| `2601.11652`                | WISP                                                                           |                                 |
| `2507.16731`                | Edge-SLM / Cloud-LLM survey                                                    |                                 |
| `2504.07347` (pre-existing) | Throughput-Optimal Scheduling (Bari et al. — K-server stability)               |                                 |

---

## 2. Consolidated Findings (top 5 per topic — details + quotes in the note files)

### 2.1 Serving (notes-serving-systems.md)

1. **Chunk size is a latency knob, not a throughput knob**: TaiChi shows TPOT degrades linearly in prefill-decode interference; rule = smallest chunk meeting TTFT. Our analog: `piece_time ≥ ~2×S` (S = setup cost is our per-chunk overhead; Sarathi's "minimal overhead" claim does NOT transfer).
2. **Batch size diminishing returns**: Orca tunes max batch to latency budget; our task table makes the marginal-benefit optimum exactly computable (S amortized once per group).
3. **Decode-first, residual capacity for prefill chunks** = default stall-free discipline (Sarathi: "does not pause ongoing decodes to execute prefills").
4. **FCFS starves under chunking; aging fixes it**: priority = α·(t−aᵢ) − β·remaining_work, one tunable ratio.
5. **Degrade longest-remaining requests last** (they "absorb" degradation); degrade prefill only if TTFT stays in budget. Implementable with tokens-produced as Lout proxy.

### 2.2 Queueing (notes-queueing.md)

1. **Scheduler's only FIFO lever = arrival burstiness (cₐ²), not ρ** (Kingman: W ≈ ρ/(1−ρ)·(cₐ²+cₛ²)/2·E[S]). Stagger D PRE completions across clouds = as valuable as cutting service variance.
2. **Latency-bound vs bandwidth-bound regime**: if latency >> 8m·bpt/(bw·10⁶), group of m = one queue item → ρ drops by m → batch big; if bandwidth-bound, E[S] ∝ m → queue wait grows linearly in m → cap group where 8m·bpt/(bw·10⁶) ≈ latency.
3. **Batch-arrival penalty**: extra wait = (E(G²)−E(G))·E(B)/(2E(G)(1−ρ)) — linear in group size, diverges as ρ→1, size-biased (largest group most likely observed). Exact model of decode batches hitting DOWN and simultaneous P PREs hitting UP.
4. **Tandem throughput = slowest stage; backlog compounds upstream** (Krivulin Thm 7; Dₘ(n)=max(Dₘ₋₁(n),Dₘ(n−1))+τₘₙ). Greedy least-loaded clouds never let one cloud be the max stage.
5. **Flattening works linearly**: doubling arrival spread halves peak; peak lags arrivals by ~½ service time. Chunking/staggering prefills converts synchronized burst into a stream.

### 2.3 Pipeline/chunking (notes-pipeline-chunking.md)

1. **Chunking pays only under concurrency**: GPipe bubble "negligible when M ≥ 4×K"; 1 piece at M=1 = no parallelism. Our clean version: pieces transfer nothing between them.
2. **Chunking never helps the chunked request's own TDR** — it's a co-tenant lever (pieces run back-to-back when idle; only last piece queues DOWN). Benefit goes to others' TDR + system TPOT.
3. **Token-budget discipline maps 1:1**: pack decodes first, prefill chunks up to leftover budget; chunk = min(remaining, leftover). Static fixed chunking is "unknowable online" (Sarathi/LoongServe) → adaptive.
4. **Decode-first is inviolable** (2606.09061: "must preserve the decode-first scheduling discipline"); aging priority Pᵢ = α(t−aᵢ) + β·rᵢ(n), β<0 → short-prefill-first + starvation protection (>10% latency win over FCFS).
5. **Over-chunking fragments**: cap concurrency, enforce minimum effective chunk (piece_time ≳ 4×S; Sarathi measured 257 vs 256 tokens → +32% prefill time from kernel-launch overhead).

### 2.4 Batching/setup (notes-batching-setup.md)

1. **Our decode group = "batch-availability" batch**: atomic, all members share one completion time; never add to in-flight groups.
2. **Batching is a cut-point decision, not membership search**: optimal batches = consecutive jobs in a sorted list; sort ready same-step requests by one key, only consider contiguous groups → thresholds.
3. **Marginal-benefit + rounding**: solve relaxed sizing, round (Mosheiov et al. 2005); no hard caps (bounded sizes are the "complicated" regime).
4. **No Group Technology constraint**: splitting same-step requests across clouds/sizes is legal and beneficial (Schaller: significantly reduced tardiness).
5. **NP-hard even toy-form** (constant setup + batch size 3) → heuristic-grade greedy batching is the right engineering level; SWPT/Monma–Potts ordering for tie-breaks.

### 2.5 Edge-cloud (notes-edge-cloud.md)

1. **Queue-depth-aware placement is provably correct**: Bari 2504.07347 K-server stability for monotone-occupancy routing (JSQ, total remaining work); PerLLM: simultaneous uploads dominate cloud throughput → `effective_ready[c] = max(busy_until[c], UP-queue arrival)` at P PRE is the biggest transfer-bound lever.
2. **Cloud choice at P PRE is a batching decision, not just load**: concentrate → bigger decode batches + S amortization; spread → parallelism/makespan (SLICE "dual optimization objective").
3. **Co-batching has a straggler cost**: Kairos 29.3 ms/step idle (8K next to 128K); our groups share one duration → never hold ready groups for latecomers; stop adding when marginal duration growth > per-member S saving.
4. **Length-normalized urgency** (Kairos): priority by (TTFT slack)/input-length — middle ground between HOL-blocking FCFS and starving SJF.
5. **Admission control transfers as ordering, never rejection**: spec requires completing every request → value transfers through ordering + group selection only (Scorpio LDF, WISP knapsack-with-guard-time).

---

## 3. Resolved Open Questions (from review flags → spec)

- **Q1 (serving review): can prefill chunks interleave with decode groups on one cloud?** → **YES**. problem.md: "Separately scheduled input-stage pieces may be alternated with other work." (2026-08-14 15:30, spec-verified)
- **Q4 (serving/queueing review): dist formula unknown?** → **RESOLVED from statement**: dist = √(ε_tdr² + ε_tpot²), ε = max(0,(mean−SLO)/SLO); component = clamp(dist; dist_base, 0), binary if dist_base=0. See `problem.md` Scoring + `phase2-synthesis.md`.
- **No maximum group size exists** → marginal-benefit optimum exactly computable (spec: "no maximum exists or is announced").
- **Iteration index never transmitted** → own bookkeeping is sole source of truth (spec, Output).

## 4. Remaining Open Items (decisions pending)

- `pretest.txt` (Example 1 input copy) — will seed the interactor's first zoo cell (Phase 5).
- Policy weights: chunk floor (2×S vs 4×S), group marginal threshold formula, aging coefficient α/β — to be A/B tested (Phase 6-8) and recorded in DECISIONS.md.
- Exact I/O format of interactor vs zoo — design complete in `docs/TESTING_FORTRESS_DESIGN.md`.

## 5. Integrity Notes

- All `.txt` extracts >5 KB, quotes cross-checked during writing; manifest.json updated.
- Rejected non-papers / wrong IDs documented in the respective note files (SLaI, BurstPipe, Kairos 2604.16682, Fowler&Mönch TDM-block).
- Do not cite any claim without its quote line in the note file.
