> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Research below was completed on Day 0 (2026-08-14) and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Edge-Cloud Collaborative LLM Inference — Deep Research Notes (Day 0)

**Purpose**: Transferable scheduling doctrine from the edge-cloud LLM serving literature
(placement, load balancing across clouds, SLO-aware routing, batch-aware placement) mapped
to the ICPC problem (Codeforces 2251: K clouds, 1 edge, shared FIFO UP/DOWN link,
permanent cloud assignment at P PRE, per-cloud decode batching, TTFT/TDR and TPOT SLOs).

**Method**: All papers downloaded to `research/papers/<id>.pdf` + `pdftotext` → `.txt`
(verified > 5 KB each). Every claim below carries an exact quote + line number from the
`.txt` extract. Nothing unverifiable is claimed. Read together with
`research/day0/phase2-synthesis.md` (score-formula mapping) and `research/notes/`.

---

## 1. Papers downloaded today (6 new)

| arXiv ID   | Short name | Title                                                                                                                          | Authors / venue                                                     | Date                      |
| ---------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------- | ------------------------- |
| 2405.14636 | PerLLM     | Personalized Inference Scheduling with Edge-Cloud Collaboration for Diverse LLM Services                                       | Z. Yang et al., ICT CAS / UCAS                                      | 2024-05-23                |
| 2510.18544 | SLICE      | SLO-Driven Scheduling for LLM Inference on Edge Computing Devices                                                              | Will Chow (double-blind)                                            | 2025-10-21, v3 2025-11-18 |
| 2505.23022 | Scorpio    | SCORPIO: Serving the Right Requests at the Right Time for Heterogeneous SLOs in LLM Inference                                  | Y. Tang, T. Lan, X. Huang, H. Lu, W. Chen (ZJU/UVA/UTA)             | 2025-05-29                |
| 2605.02329 | Kairos     | Taming Request Imbalance: SLO-Aware Scheduling for Disaggregated LLM Inference                                                 | Qipeng Wang                                                         | 2026-05-04, v3 2026-08-11 |
| 2601.11652 | WISP       | Waste- and Interference-Suppressed Distributed Speculative LLM Serving at the Edge via Dynamic Drafting and SLO-Aware Batching | X. Li et al. (Virginia Tech et al.), SIGMETRICS 2026 / POMACS 10(2) | 2026-01-15, v2 2026-04-07 |
| 2507.16731 | Survey     | Collaborative Inference and Learning between Edge SLMs and Cloud LLMs: A Survey of Algorithms, Execution, and Open Challenges  | S. Li et al. (HUST, HKUST, NTU...)                                  | 2025-07-22                |

**Note on the task brief**: the brief's "SLICE: Online Scheduling for SLO-Compliant Serving of
LLM Clusters" turned out to be arXiv 2510.18544 "SLICE: SLO-Driven Scheduling for LLM
Inference on Edge Computing Devices" — same SLICE, but *edge*-focused, which is even more
on-topic. "Kairos" (LLM scheduling SLO, 2026) = 2605.02329 "Taming Request Imbalance"
(disaggregated TTFT/TPOT SLO system). A second 2026 paper named KAIROS exists
(2604.16682, power management for agentic serving) — **not** the SLO-scheduling one; not downloaded.

Pre-existing, already in `research/papers/` and cross-referenced below (verified quotes in §4):
2401.09670 (DistServe), 2410.10759 (SplitLLM), 2504.07347 (Bari et al., throughput-optimal
LLM scheduling theory — contains the load-balancing results).

---

## 2. Key insights per paper

### 2.1 PerLLM (2405.14636) — edge-cloud offloading as constrained bandit

1. The edge-cloud collaboration problem is formulated as a **combinatorial multi-armed bandit**
   over "assigning each service to a specific server", with 4 hard constraints: per-service
   processing-time requirement, server computing capacity, server bandwidth capacity, and
   exactly-one-server per service (line 455: "C4 : Σ_j x^t_{i,j} = 1, x_{i,j} = {0,1}").

2. Algorithm: **CS-UCB** — first filter actions by a constraint-satisfaction function, then pick
   max UCB among valid actions (lines 631-633: "the CS-UCB algorithm first filters out all the
   actions that satisfy a given constraint through a constraint satisfaction mechanism and then
   selects the action with the highest UCB value ... among these valid actions for execution").
   Lesson: *constraint feasibility before optimization* — mirrors our "no illegal assignment" hard rule.

3. Motivation measured on hardware: simultaneous uploads of many services cause **network
   congestion** that dominates cloud-side processing (lines 293-298: "a surge in processing time
   and energy costs on the cloud when the number of inference services increases. This is because
   the simultaneous uploading of large-scale services causes network congestion").

4. Cloud-only vs edge-only both fail: cloud-only has lowest throughput "due to network bandwidth
   limitations"; edge-only fails "due to computational resource constraints" (lines 815-816, 886-887).

5. Claimed result: >97% success rate on processing-time requirements vs 58-74% for baselines;
   2.2×/2.1×/1.6× throughput (lines 775-777, 888-889).

### 2.2 SLICE (2510.18544) — per-task decode-rate control on the edge

1. Two-stage strategy: (a) **utility-maximizing task selection**, (b) **rate allocation** via a
   decode-mask matrix that gives each task a per-cycle decode frequency proportional to its
   TPOT requirement (lines 144-170, 632-644).

2. Utility rate selection rule: select tasks by `r_i = U_i · TPOT_i` (utility per token), greedily
   adding tasks until the estimated schedule cycle would exceed the SLO window
   (lines 553-588). Selecting too many tasks makes rates infeasible; too few underutilizes
   (lines 559-564: "If too many tasks are selected, it may not be possible to allocate sufficient
   generation rates to each task due to hardware limitations. Conversely, selecting too few tasks
   may underutilize hardware resources").

3. Empirically on an RTX 4060 Ti: **per-token latency grows near-linearly with batch size while
   throughput improves only marginally** until a critical size, after which absolute latency
   spikes (lines 64-71) — the core tension for decode batching.

4. Real-time tasks carry utility 10-100× higher than non-real-time (lines 102-106) — priority
   weighting of heterogeneous SLO classes.

5. Online version: re-run selection on every arrival/departure (interrupt-driven); a utility
   adaptor lets long tasks be deprioritized to mimic SJF and avoid head-of-line blocking
   (lines 822-857).

6. Claims: up to 35× SLO attainment, 3.4× task-completion advantage vs Orca/FastServe (lines 29-32).

### 2.3 Scorpio (2505.23022) — heterogeneous SLO-aware serving (TTFT + TPOT guards)

1. **TTFT Guard**: least-deadline-first (LDF) reordering of the request queue by time-to-TTFT-
   deadline, plus rejection of unattainable requests under overload (lines 451-458).

2. **TPOT Guard**: requests with looser TPOT SLOs **skip decode iterations**, leaving resources
   for tighter ones; this is the core heterogeneity exploitation (lines 137-139: "requests with
   looser TPOT can skip some iterations of generation, leaving more resources for requests with
   tighter TPOT").

3. **Credit-based batching**: each request earns credit at rate TRP(r) = min TPOT SLO in batch /
   own TPOT SLO; batched when credit ≥ 1.0, debited 1.0; batching frequency converges to TRP
   (lines 477-491, 538-539).

4. **VBS admission control**: admit a request only if estimated TPOT of the batch (using virtual
   batch size = Σ TRP, not headcount) stays ≤ the minimum TPOT SLO among running requests
   (lines 540-563).

5. Batch-step model: inter-token latency modeled as affine in (batch size, avg sequence length),
   R² > 0.9 (lines 395-418). Claims: goodput up to 14.4×, SLO adherence +40.7-46.5% at high QPS
   (lines 662-663).

### 2.4 Kairos (2605.02329) — request-imbalance scheduling for disaggregated serving

1. Long-tail request lengths cause **head-of-line blocking on prefill** (long request first
   monopolizes prefill; short requests' TTFT violated) and **straggler waste on decode** (batch
   step time = slowest request; short request idles ~29 ms/step next to a 128K one)
   (lines 54-73, 303-319).

2. **Urgency-based prefill scheduling**: predict each queued request's prefill finish time under
   FCFS, compute urgency = (TTFT SLO − (finish − arrival)) / TTFT SLO, **normalize by input
   length**, greedily select highest urgency per chunk (lines 155-162, 453-468). Explicitly avoids
   both FCFS HOL blocking and SJF starvation (lines 437-442, 519-532).

3. **Slack-guided adaptive decode batching**: per-request slack vs TPOT SLO; if some requests'
   step time < min slack and packing them improves throughput, execute only those and delay the
   rest (lines 615-651). Profiled LUT keyed by (batch size, seq len), updated online
   (lines 615-635).

4. Claims: TTFT attainment +23.9%, TPOT +27.1%, end-to-end +33.8%, decode throughput +19.3%
   (lines 50-52). At QPS=3.0, Kairos 89.6% vs DistServe 55.8% end-to-end (lines 731-732).

### 2.5 WISP (2601.11652) — edge speculative decoding, SLO-aware verification batching

1. Identifies **Verification Interference**: co-batching heterogeneous requests makes the
   micro-batch a synchronization barrier that completes only when the slowest request finishes —
   a head-of-line effect where long requests inflate latency for many short ones
   (lines 834-845). This is the batching-placement warning: *don't group requests of wildly
   different cost in one batch*.

2. SLO-aware batch construction is modeled as a **knapsack** with value density
   U_i = expected accepted tokens / estimated verification cost; per-request deadline
   d_i = arrival + budget (lines 1010-1081).

3. Greedy with a **time-critical fast path**: requests past their Latest Start Time
   (LST = deadline − est. cost − guard δ) are admitted EDF-first; the rest fill by utility score
   (lines 1086-1101).

4. FCFS verification fails catastrophically under heterogeneous token-speed SLOs: 98.27%
   violation rate for the 8 tokens/s class when all classes share one GPU verifier
   (lines 284-318, Table 1).

5. Claims: capacity +2.1× vs centralized, +4.1× vs SLED; goodput +1.94×/3.7× (lines 27-28).

### 2.6 Survey (2507.16731) — taxonomy of edge-cloud collaboration

1. Inference collaboration taxonomy: **task assignment** (route whole query to best model),
   **task division** (split execution across models/systems), **mixture** at task- and token-level
   (lines 389-401, Table 2).

2. Offloading is distinguished from static routing precisely by **dynamic, workload-aware
   decision-making**: "rather than statically assigning tasks from the outset (as in routing),
   offloading makes dynamic, stage-wise decisions during execution, evaluating current workload
   against system capacity to maximize performance" (lines 660-663).

3. Runtime-adaptive scheduling: "dynamically schedules offloading decisions by adapting to
   real-time metrics, such as latency, confidence, and resource availability" (lines 672-674).

4. Task assignment for QoS: "prior methods employ lightweight scorers, calibrated reward
   estimators, or bandit-based controllers to select the optimal execution path before generation"
   (lines 407-410) — this is the PerLLM family; our problem's assignment decision at P PRE is the
   same shape, but the "path" is the cloud choice and the objective is the score formula.

### 2.7 Cross-reference: theory already in repo (2504.07347, already downloaded)

- For K identical servers behind a load balancer, both **random assignment and JSQ keep the
  system stable** under the aggregate load condition (Prop. 3, lines 1157-1161).

- The stability result "extends well beyond JSQ... any load-balancing algorithm that routes each
  arrival to a server minimizing some non-negative scalar measure of server occupancy — examples
  include total remaining tokens, total expected remaining workload, or any monotone increasing
  function of the prefill and decode counts" (lines 1173-1176). → our "effective ready time"
  (max of cloud busy_until and UP-queue arrival time) is such a measure.

- Work-conserving schedulers (Orca, Sarathi-Serve; vLLM with chunked prefill) are throughput-
  optimal; non-work-conserving ones can be unstable even under the load condition
  (lines 1181-1189). → The work-conserving mandate is theory-backed.

---

## 3. MAPPING TO OUR PROBLEM

Our problem vs literature differences to respect: **no rejection** (every request must finish;
Scorpio/WISP rejection and admission control are for SLO *attainment* — we must instead order
work, never drop), **no preemption**, **no self-wake timer** (decisions only at event frames),
**transfers are automatic FIFO** (we do not schedule transfers, only when to start the tasks
that trigger them), **batch duration is uniform for all members** (a group's D PROC duration
applies to every member — no intra-batch stragglers once grouped), **score is mean-based
TDR/TPOT + makespan-bound throughput** (see phase2-synthesis.md §1).

Recommendations (each with the literature support):

1. **Cloud assignment at P PRE = queue-depth-aware ("effective ready time"), not plain
   least-loaded.** Compute per cloud `effective_ready[c] = max(busy_until[c], time the UP
   transfer for this request would reach c)` and assign to the argmin. Rationale: PerLLM's
   measured network-congestion effect (2.1.3) + 2504.07347's stability theorem for any monotone
   occupancy measure (2.7). On transfer-bound tests (transfer_bottleneck zoo case) the UP FIFO
   makes a "least-loaded but far-in-queue" cloud a trap. O(K) scan, K ≤ 8 — free.

2. **Pool requests per cloud to grow decode batches, but cap by the SLO-relevant marginal rule.**
   D PROC batches are per-cloud; concentrating requests on fewer clouds creates larger groups
   (amortize S), but underutilizes parallel clouds (hurts C_tp makespan). SLICE's dual-objective
   tension (2.2.1, 2.2.3) and marginal-admission logic are the template: admit a member into a
   D batch iff the interpolated group duration grows slower than the S-per-member it saves —
   exactly the Potts-Kovalyov marginal rule already in phase2-synthesis §2.2.

3. **Stagger P PRE starts against the UP queue; order same-frame P PREs by cloud index.**
   UP transfers for one D PRE enter by increasing cloud index (spec); PerLLM's congestion
   measurement (2.1.3) and Kairos's HOL-blocking analysis (2.4.1) both show burst starts
   serialize at the shared link. First version: queue-aware assignment (rec. 1) already staggers
   implicitly; explicit staggering only if A/B wins on transfer-bound cases.

4. **Decode-batch composition: group by readiness frame; never hold ready requests.**
   Kairos's straggler analysis (2.4.1) is about *variable-length* members; our groups have one
   duration, but the insight survives at the group level: mixing a group that is ready with one
   that is not (holding) delays the ready one's first token (μ_tdr) and its span (μ_tpot). WISP's
   sync-barrier warning (2.5.1) is the same caution at batch level. Sarathi stall-free batching
   (research/notes/401) already captures this.

5. **Priority ordering inside the edge/local queues: length-normalized urgency, not FCFS.**
   Kairos's normalized urgency (2.4.2) is the rigorous middle ground between FCFS (HOL blocking)
   and SJF (starvation): for P PRE queueing on the edge, order by expected input-stage span
   normalized by something that prevents starvation (e.g., longest-waiting-time floor). A/B vs
   FIFO; note phase2 §2.5 already derives SPT-for-mean-TDR — Kairos gives the starvation-safe
   variant.

6. **Estimate batch cost with the task table before committing a group; add a guard ε.**
   Scorpio's VBS check (2.3.4: admit iff estimated batch TPOT ≤ min member TPOT) and WISP's
   guard time δ (2.5.3: LST = deadline − cost − δ) both absorb estimator error. Here: when a
   D batch would push a member past its remaining span budget (score-mean steering), drop the
   marginal member; the task table is the estimator and it is exact, so guard can be 0 — keep
   the check for the *interaction* (holding a member delays its mean).

7. **Work-conserving is non-negotiable and provably right.** 2504.07347: non-work-conserving
   policies can be unstable under the load condition; Orca/Sarathi are throughput-optimal
   (2.7). This is docs/ASSUMPTIONS.md §4 Step-3 doctrine, now with literature backing: if a cloud or the
   edge is idle and a legal task exists, schedule it.

8. **Do not import rejection/admission-control mechanisms.** Scorpio and WISP both *reject*
   unattainable requests; our rules forbid it (spec: every request must complete). Their value
   for us is only in *ordering* (what to run first) and *batch composition* (what to group) —
   never in dropping work. The score's "dead zones" (phase2 §1.4) make this even clearer: we
   must reach mean targets, which rejection would make unreachable.

---

## 4. VERIFIED — exact quotes with source lines (pdftotext .txt)

Every quote below was copied verbatim from `research/papers/<id>.txt`; line numbers are the
Read-tool line numbers of that file. (Light PDF-reflow artifacts — page numbers/headers —
removed; wording untouched.)

### PerLLM (2405.14636.txt)

- L455: `C4 : Σ^N_j=1 x^t_i,j = 1, x_i,j = {0,1}` (one server per service per slot)
- L474-478: "The constraint C1 is used to ensure that the processing time requirement of each
  server is met. Otherwise, it is assigned to a more resource-rich server. Constraint C2
  indicates that the required computing power for all services is less than or equal to the
  currently available computing power. Constraint C3 indicates that the bandwidth required to
  upload all services is less than or equal to the currently available bandwidths."

- L490-497: "we conceptualize the above multi-objective optimization problem as a Combinatorial
  Multi-Armed Bandit (CMAB) problem... We define the action space a = [a1, a2, · · · , ai, · · · , aM ]
  as assigning each service to a specific server. Where each element ai = {1, 2, · · · j, · · · , N }
  denotes the index of the server to which the i-th task is assigned."

- L293-298: "We can find a surge in processing time and energy costs on the cloud when the
  number of inference services increases. This is because the simultaneous uploading of
  large-scale services causes network congestion."

- L815-816: "The cloud-only solution FineInfer has the lowest throughput due to network
  bandwidth limitations." / L886-887: "The edge-only solution AGOD also has poor throughput
  results due to computational resource constraints."

- L775-777: "The results show that PerLLM has a success rate of over 97%. Notably, even with
  fluctuating bandwidths, PerLLM maintains a high success rate. Other methods are only 58%-74%."

- L888-891: "On average, PerLLM achieves 2.2×, 2.1×, and 1.6× throughput when comparing
  FineInfer, AGOD, and RewardlessGuidance. This indicates that our proposed solution can avoid
  network congestion and queuing problems through rational service scheduling and resource
  allocation."

### SLICE (2510.18544.txt)

- L26-32 (abstract): "This paper proposes SLICE, an innovative scheduling solution designed for
  edge computing scenarios with differentiated SLO requirements. By combining a utility-
  maximizing request scheduling algorithm with a dynamic iterative control mechanism for
  generation rates, SLICE significantly improves LLM inference service SLO attainment.
  Experimental results demonstrate that compared to state-of-the-art solutions Orca and
  FastServe, SLICE achieves up to 35× higher SLO attainment and 3.4× advantage in task
  completion time."

- L64-71 (Fig. 1 caption): "as the batch size increases from 1 to 9, the per-token latency shows
  near-linear growth while throughput improves only marginally. When batch size exceeds 9,
  although the per-token latency stabilizes and throughput continues to scale linearly with
  batch size, the absolute latency spikes above 120ms."

- L102-106: "real-time tasks are assigned significantly higher utility values than non-real-time
  tasks (typically 10 to 100 times higher) in the edge scenarios."

- L553-558: "If the token interval of task i is TT^i_POT, the number of tokens generated per
  second is 1/TT^i_POT. By distributing the task utility Ui evenly across all tokens generated
  per second, the utility value per token is given by... ri = Ui · TTi_POT" and "the |b| tasks
  with the highest utility rate are selected to scheduling."

- L559-564: "If too many tasks are selected, it may not be possible to allocate sufficient
  generation rates to each task due to hardware limitations. Conversely, selecting too few tasks
  may underutilize hardware resources, resulting in an insufficient number of schedulable tasks."

- L581-586: "the rate allocation mechanism (see section IV-D) of SLICE adopts a periodic
  scheduling strategy where each scheduling cycle generates at least vi tokens for each
  scheduled task i ∈ b, and when the duration of a single cycle does not exceed 1000ms, the
  system can ensure that the SLO requirements of all tasks are met."

- L839-857: "For example, by reducing the utility values of tasks that generate a large number
  of tokens (i.e., those with longer execution cycles), such long tasks can be prioritized for
  exclusion during subsequent task selection. This design mimics the SJF scheduling strategy,
  effectively avoiding head-of-line blocking caused by prolonged resource occupancy."

### Scorpio (2505.23022.txt)

- L17-29 (abstract): "This paper introduces SCORPIO, an SLO-oriented LLM serving system designed
  to maximize system goodput and SLO attainment for workloads with heterogeneous SLOs... SCORPIO
  features a TTFT Guard, which employs least-deadline-first reordering and rejects unattainable
  requests, and a TPOT Guard, which utilizes a VBS-based admission control and a novel
  credit-based batching mechanism... SCORPIO improves system goodput by up to 14.4× and SLO
  adherence by up to 46.5% compared to state-of-the-art baselines."

- L137-139: "From the TTFT perspective, requests with looser SLO can be served a little later.
  From the TPOT perspective, requests with looser TPOT can skip some iterations of generation,
  leaving more resources for requests with tighter TPOT."

- L451-454: "SCORPIO uses a simple but effective LDF reordering strategy. The deadline for a
  request ri is calculated as the time left to its TTFT deadline ST_T(ri). This strategy puts
  more urgent requests (with earlier TTFT deadline) at the front of the queue."

- L477-491: "Credit-based Batching offers requests of different TPOT SLOs with different
  credits... we first introduce a concept called TPOT-relative Proportionality (TRP): ... TRP(r) =
  min_{r′∈R(t)} ST_P(r′) / ST_P(r)... each request earns credits at its TRP. Accumulating
  sufficient credit (≥ 1.0) grants a request to be batched in the next processing batch."

- L558-563: "The request r is admitted to the Running Queue at step t if adding it would not
  cause the estimated TPOT to exceed the minimum TPOT SLO of the running requests set R′:
  EstimatedTPOT(VBS(R′), Lavg(R′)) ≤ min_{r′∈R(t)} ST_P(r′). The mechanism guarantees that the
  admitted requests obey their TPOT SLOs."

- L662-663: "at a QPS of 15, it yields up to 8.8-14.4× higher goodput and 40.7-46.5% higher SLO
  adherence than baselines, demonstrating robust burst handling through its SLO-oriented
  scheduling."

### Kairos (2605.02329.txt)

- L38-52 (abstract): "we propose Kairos, an SLO-aware scheduling system equipped with two
  complementary mechanisms. On the prefill side, Kairos employs urgency-based priority
  scheduling: it predicts prefill completion times and dynamically selects requests to maximize
  the attainment of time-to-first-token (TTFT) SLOs. On the decode side, Kairos introduces
  slack-guided adaptive batching, which leverages the gap between per-step decode time and the
  time-per-output-token (TPOT) SLO to greedily pack short requests... Kairos improves TTFT SLO
  attainment by up to 23.9%, TPOT SLO attainment by up to 27.1%, end-to-end SLO attainment by up
  to 33.8%, and decode throughput by up to 19.3%."

- L54-73: "request lengths in practice follow a long-tail distribution: the vast majority of
  requests are short, while a small fraction are extremely long... a long request that arrives
  ahead of subsequent short requests can monopolize the prefill stage, leading to head-of-line
  blocking... when short and long requests coexist in the same batch, synchronization barriers
  at each decoding step force short requests to wait for long requests to finish."

- L304-319: "when a long request arrives ahead of subsequent short requests under FCFS
  scheduling, it can block others for an extended period... a 128K-token request requires
  approximately 8.8s for prefill, whereas a 8K-token request completes in only 400.4ms... a
  8K-token request takes roughly 11.0ms per decode step, while a 128K-token request incurs
  approximately 40.3ms. When both coexist in the same batch, the batch step time is determined
  by the slowest request—namely, 40.3ms. For the 8K-token short request, this means 29.3ms of
  idle waiting per step due to the synchronization barrier."

- L437-442: "SJF is impractical in online serving scenarios: long requests may be indefinitely
  starved, as newly arriving short requests continuously jump ahead in the queue, causing
  unbounded delays for long requests and eventual SLO violations for them."

- L453-468: "Kairos computes an urgency score... The urgency score is defined by Equation 1,
  where the numerator captures the ratio of the slack between prefill finish time to the TTFT
  SLO under FCFS scheduling policy... Kairos normalizes the urgency score by request length
  (Line 8), such that shorter requests naturally receive higher priority than longer ones at
  comparable finish times. The scheduler then greedily selects the chunks of the requests with
  the highest normalized urgency for prefill."

- L615-621: "Kairos constructs a lookup table (LUT) mapping batch size and sequence length to
  per-step decode time... Before each decode step, Kairos computes a slack value for every
  active request, defined as the remaining time budget before the next token must be delivered
  to meet the TPOT SLO."

- L731-732: "At the critical inflection point of QPS=3.0, Kairos reaches 89.6% combined
  attainment while DistServe collapses to 55.8%, an improvement of 33.8%."

### WISP (2601.11652.txt)

- L22-28 (abstract): "we identify and formalize two critical bottlenecks that limit the
  efficiency and scalability of distributed speculative LLM serving: Wasted Drafting Time and
  Verification Interference... WISP improves system capacity by up to 2.1× and 4.1×, and
  increases system goodput by up to 1.94× and 3.7×, compared to centralized serving and SLED."

- L834-839: "When such heterogeneous requests are co-batched to maximize GPU parallelism, the
  micro-batch becomes a synchronization barrier: it completes only after the slowest request
  finishes its layer stack. As a result, long verifications inflate the batch time and short
  verifications inherit the same completion time. We term this head-of-line effect verification
  interference: a few long verification requests... can dominate the batch and increase the
  end-to-end latency of many short requests."

- L310-318: "FCFS becomes increasingly incapable of meeting stringent speed targets as more
  devices share the verifier, whereas only the most relaxed targets remain consistently
  achievable. This is because FCFS is SLO-agnostic: by treating requests with different urgency
  uniformly, it amplifies queueing and contention." (Table 1, L295-308: class 4 = 8 tok/s shows
  98.27% violation under FCFS vs 0.00% for class 1 = 2 tok/s.)

- L1015-1016: "We model the batch scheduling problem as a knapsack problem and then propose an
  SLO-aware scheduling algorithm."

- L1076-1081: "This motivates our utility score as a value density: U_i ≜ N̂^v_i / v̂_i, which
  measures expected goodput gained by admitting verification request i in the batch."

- L1090-1101: "we define a conservative latest start time (LST): LST_i ≜ d_i − v̂_i − δ, where δ
  is a guard time for modeling error and queueing uncertainty... A request becomes urgent and
  put into urgent queue at time t if t ≥ LST_i. Urgent requests are ordered by earliest deadline
  (EDF), while non-critical requests are ordered by utility score U_i... the planner builds a
  batch incrementally: it first admits EDF-ordered critical requests, and then fills the
  residual capacity using utility score-ordered requests."

### Survey (2507.16731.txt)

- L389-401: "recent work has explored three complementary collaboration paradigms... Task
  assignment, which routes queries to the most suitable model...; Task division, which decomposes
  execution across models or system components (e.g., offloading, request routing, or early-exit
  mechanisms); Mixture strategies, which integrate assignment and division at task-level or
  token-level granularity."

- L660-663: "rather than statically assigning tasks from the outset (as in routing), offloading
  makes dynamic, stage-wise decisions during execution, evaluating current workload against
  system capacity to maximize performance and utilize resources cost-effectively."

- L672-674: "This group of methods dynamically schedules offloading decisions by adapting to
  real-time metrics, such as latency, confidence, and resource availability, and distributes
  tasks across local devices, control units, and auxiliary vehicles."

- L407-410: "To minimize latency and energy consumption under quality-of-service (QoS)
  constraints, prior methods employ lightweight scorers, calibrated reward estimators, or
  bandit-based controllers to select the optimal execution path before generation."

- L703-706: "PerLLM [115] frames coordination as a constrained multi-armed bandit problem,
  employing an upper confidence bound algorithm to adaptively select the most cost-effective
  execution path."

### Theory cross-reference (2504.07347.txt, pre-existing in repo)

- L1144-1149: "We consider two common load-balancing algorithms: Random assignment: each
  arriving request is routed to a server chosen uniformly at random...; Join-the-shortest-queue
  (JSQ): each arriving request is routed to the server k with the fewest total unfinished
  requests |Qk|."

- L1157-1161: "Proposition 3. Suppose K identical servers each run a work-conserving K-FCFS
  scheduling algorithm, with requests routed by either random assignment or JSQ. If the
  aggregate load condition λ(mp + md) < K · bmax/tbmax holds, then the DTMC {X(n), n ≥ 0}
  describing the system is positive recurrent."

- L1173-1176: "Remark 4 (Other routing metrics). The result extends well beyond JSQ. It holds
  for any load-balancing algorithm that routes each arrival to a server minimizing some
  non-negative scalar measure of server occupancy — examples include total remaining tokens,
  total expected remaining workload, or any monotone increasing function of the prefill and
  decode counts."

- L1246-1249: "Through our analysis, Orca and Sarathi-Serve—the two work-conserving
  algorithms—should be preferred... the work-conserving schedulers achieve higher throughput,
  while FasterTransformer and vanilla vLLM become unstable even under the same load."

---

## 5. Bottom line for the solver

The literature converges on one message for our architecture: **placement and batch composition
are one decision**. The cloud chosen at P PRE determines which request pool can later form
decode batches (per-cloud D PROC), so assignment should optimize for future batching, not just
current load — subject to the UP FIFO being the real serialization point when bandwidth is low.
Kairos and Scorpio supply the SLO-aware ordering mechanics (normalized urgency, slack-based
admission), SLICE supplies the marginal-utility selection rule, WISP supplies the batch-
interference warning and guard-time discipline, PerLLM supplies the constraint-first placement
frame, and 2504.07347 supplies the stability proof that queue-depth-aware, work-conserving
policies are the right class. Nothing in these papers licenses rejection or preemption in our
setting; all value transfers through ordering, grouping, and cloud-choice policies.
