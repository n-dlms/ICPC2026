# OPTIMIZATION_DOCTRINE.md

# ICPC Huawei Challenge 2026 — Collaborative Inference Scheduling

# "How to win: algorithms, traps, and judge survival"

# Last updated: 2026-08-14 14:00 UTC+2 (Day 0, post-spec)

> **Historical note (post-contest).** I wrote this doctrine before and during the contest and executed it to reach **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. Imperatives below are preserved as the rules I followed. I (Ntokozo Dlamini) originated the design decisions; AI sped up drafting, implementation, and checks under my direction.

---

## 0. The Optimization Landscape

This problem had **three independent optimization axes** that interacted destructively. A solver that optimized one axis without understanding the others scored in the bottom quartile.

| Axis | Decision | What It Controls | Conflict |
| ------ | ---------- | ----------------- | ---------- |
| **Cloud Assignment** | Which of K clouds gets each request? | Load balance, transfer locality, queue distribution | Greedy load-balancing may create transfer bursts |
| **Prefill Chunking** | Split `[0, num_layers)` into how many pieces? | Pipeline parallelism vs. setup cost amortization | More chunks = less blocking but more S overhead |
| **Decode Batching** | Group how many requests per decode step? | Setup cost amortization vs. queue serialization | Larger batches amortize S but serialize behind FIFO queues |

The score function combined **TDR** (time to first token) and **TPOT** (time per output token). These two objectives were in direct tension:

- **TDR wanted**: immediate start, no chunking, no waiting for batchmates, fastest path to P POST
- **TPOT wanted**: a SATURATED LOCAL COMPUTER (it touched every decode token: D PRE + D POST per token, ~16x more tasks than each cloud), large decode batches, amortized S, steady-state throughput. Clouds were 16x less loaded — their saturation was secondary.

The contest tested my ability to navigate this tension. A solver that minimized TDR at all costs had terrible TPOT. A solver that maximized decode batching delayed first tokens and hurt TDR.

---

## 1. The Core Algorithmic Frameworks

### 1.1 Online Load Balancing on Identical Machines (Cloud Assignment)

**The Theory:**
Graham (1966) proved that greedy assignment to the least-loaded machine is `(2 - 1/K)`-competitive for makespan minimization. For identical machines with online arrivals, no deterministic online algorithm can beat this ratio.

**What this meant for my solver:**

- Greedy cloud assignment (assign to cloud with earliest `busy_until`) was **provably optimal** among deterministic online algorithms for the compute-bound regime.
- However, this ignored **transfer queues**. A cloud could have early `busy_until` but a long UP queue, meaning the data didn't arrive until later.

**The Transfer-Aware Variant:**
Maintain for each cloud:

- `compute_free[k]` = when cloud k finishes current task
- `up_queue_tail[k]` = when the last queued UP transfer for cloud k finishes
- `effective_ready[k]` = `max(compute_free[k], up_queue_tail[k])`

Assign to the cloud with minimum `effective_ready`, not minimum `compute_free`.

**Why this mattered:**
If K=8 and 100 requests arrived simultaneously, greedy on `compute_free` assigned ~12-13 to each cloud. But all their UP transfers queued on the shared uplink FIFO. The last transfer finished after `100 * transfer_time`. The first cloud sat idle waiting for data while others computed. My transfer-aware assignment staggered the load.

**Hidden test trap I defended against:** the contest included cases where `latency_ms` was large and `bandwidth_gbps` was small, making transfer time dominate. A compute-only greedy scored 30-50% worse than transfer-aware.

**Research to read:**

- Graham, "Bounds for certain multiprocessing anomalies" (1966) — the original list scheduling bound
- Caragiannis, "Better bounds for online load balancing" — randomized improvements (not worth implementing for identical machines)
- Azar et al., "On-line load balancing" — the competitive analysis framework

### 1.2 Batch Scheduling with Sequence-Dependent Setup Costs (Decode Batching)

**The Theory:**
Every decode step pays setup cost S. Batching m requests pays S once but takes `duration(m)` time. The **batching efficiency** is:

```text
efficiency(m) = m / (S + duration(m))
```

If `duration(m)` grows linearly with m, efficiency is constant — batching doesn't help.
If `duration(m)` grows sublinearly, efficiency increases with m — batching helps.
If `duration(m)` grows superlinearly (unlikely here), efficiency decreases — don't batch.

**The Marginal Benefit Rule:**
Add a request to the batch if:

```text
duration(m+1) - duration(m) < S / (m+1)
```

This says: the additional time from adding one more request must be less than the setup cost amortized over the new batch size.

**Implementation:**
For each ready decode group, compute the marginal benefit of adding each candidate request. Build the batch greedily by adding requests with lowest marginal cost until no beneficial request remains.

**Why this mattered:**
The task-time table could show `decode_proc(1) = 10` and `decode_proc(10) = 15`. The marginal cost of the 10th request was `(15-10)/9 = 0.55`, while S/10 = 1.0 (if S=10). The 10th request was profitable. But if `decode_proc(10) = 100`, marginal cost was 10, which exceeded S/10 = 1.0 — batching was catastrophic.

**Hidden test trap I defended against:** the contest included tables where batching was highly profitable (sublinear scaling) and tables where it was not (linear scaling). A solver that always batched size-1 or always batched everything lost on half the tests.

**Research to read:**

- Allahverdi et al., "A review of scheduling research involving setup considerations" — comprehensive survey of batch scheduling with setup times
- Potts & Kovalyov, "Scheduling with batching: a review" — the batch scheduling taxonomy
- Webster & Baker, "Scheduling groups of jobs on a single machine" — the SPT-batch rule and its optimality conditions

### 1.3 Pipeline Stage Granularity with Setup Costs (Prefill Chunking)

**The Theory:**
Prefill can be split into chunks. Chunking splits ONLY the P PROC (cloud) stage into `c`
pieces; P PRE (local) and P POST (local) are each ONE task per request. **Transfers are
NOT per-chunk**: P PRE queues exactly ONE UP transfer (local→cloud) on completion, and
only the LAST P PROC piece queues the ONE DOWN transfer (cloud→local) on its TDN. Each
later piece waits on the previous piece's TDN — no extra transfer is queued between
pieces. (Earlier draft wrongly claimed "c UP transfers"; that is corrected here.)

Every task pays setup S once. So with `c` chunks:

- P PRE: 1 task → S (local)
- P PROC: c tasks → c·S (cloud)
- P POST: 1 task → S (local)
- Transfers: exactly 1 UP + 1 DOWN, paid ONCE regardless of c.

Total prefill time with `c` chunks:

```text
T_prefill(c) = (c + 2) * S + prefill_proc_total + up_transfer + down_transfer
```

Without chunking (c=1):

```text
T_prefill(1) = 3 * S + prefill_proc_total + up_transfer + down_transfer
```

Extra cost of chunking vs c=1 = (c − 1) · S. **No extra transfer cost.** Because the
transfer cost is fixed (not multiplied by c), chunking is much cheaper than a model that
charged per-chunk transfers — with S ∈ [1,10], each extra chunk costs only 1–10 ms, so it
pays off whenever it avoids even modest cloud/local idle time.

Chunking is profitable only if the **interleaving benefit** exceeds the **setup (S)
overhead** (c − 1) · S. The interleaving benefit is that while chunk k of request A is on
the cloud, the local computer (freed between P PRE and P POST) can work on other requests,
and different requests' pieces pipeline across clouds.

**The Optimal Chunking Formula:**
For K clouds and local computer as bottleneck, chunking is beneficial when:

```text
(c - 1) * S < (c - 1) * (local_idle_time_avoided)
```

More formally, if the local computer is the bottleneck (P PRE and P POST are slow),
chunking allows the local computer to work on other requests between chunks. The benefit is
the throughput improvement on the local computer.

**Practical rule:**

```text
if S == 0:
    chunk aggressively (up to num_layers pieces) — chunking is free
elif S > prefill_proc_total:   # S dominates the whole prefill
    c = 1                      # don't chunk
else:
    c = min(num_layers, max(1, (int)(prefill_proc_total / S)))
```

**Hidden test trap I defended against:** cases where S=0 (chunking had to be aggressive, up to num_layers chunks) and cases where S=1000 (chunking had to be disabled). A solver that didn't adapt lost 20-40% on one of these.

**Research to read:**

- Huo & Leung, "Parallel machine scheduling with batch setup times" — the setup time vs. throughput tradeoff
- Cheng et al., "Scheduling with batch setup times on parallel machines" — approximation algorithms
- PipeDream / GPipe / Zero Bubble papers — the chunking/pipelining insight from ML systems (your notes 501/502)

### 1.4 FIFO Queueing and Throughput (Transfer Management)

**The Theory:**
The UP and DOWN queues are FIFO. For a FIFO queue with arrival rate λ and service rate μ, the average wait time is `1/(μ - λ)` in steady state (M/D/1 queue approximation). But here we control λ by controlling when tasks complete.

**Key insight:** Bursting completions creates queue buildup and head-of-line blocking. Staggering completions (by intentionally delaying task starts) smooths the queue and reduces maximum wait time.

**The Staggering Decision:**
If multiple requests are ready for P PRE and we assign them all immediately, their UP transfers queue up. The last request waits for `(m-1) * transfer_time` before its data even starts moving.

Alternative: Delay some P PRE starts so that UP transfers are spaced out. The cloud can start processing the first transfer while the second is still in the queue.

**When to stagger:**

- If transfer time is large relative to compute time
- If K is small (queue contention is high)
- If we care about TDR (the last request in a burst suffers)

**When NOT to stagger:**

- If compute time dominates (queue clears instantly)
- If we care about throughput (staggering reduces utilization)

**Implementation:**
Maintain `next_up_slot` = when the UP queue will be free. Before starting a P PRE, check if `next_up_slot > t + some_threshold`. If so, delay starting until the queue clears, or start a task on a different cloud.

**Hidden test trap I defended against:** cases where `latency_ms = 1000` and `bandwidth_gbps = 0.01`, making transfer time ~1 second per request. A burst of 50 requests created a 50-second UP queue. Staggering was essential there.

**Research to read:**

- Bertsimas & Van Ryzin, "A stochastic and dynamic vehicle routing problem" — queueing optimization
- Any queueing theory text on batch arrivals to single-server queues

---

## 2. The Score Function: How I Reverse-Engineered and Optimized It

The exact score formula was partially visible in the spec. I derived it from the statement and sample outputs.

### 2.1 What We Know

Parameters: `SLO1`, `SLO2`, `tp_UB`, `tp_base`, `dist_base`, `w_tp`, `w_c`

TDR = time from ARR to P POST completion (first token ready)
TPOT = mean gap between consecutive tokens for a request

### 2.2 Hypotheses to Test

**Hypothesis A: Normalized latency score**

```text
score_tdr = max(0, 1 - TDR / SLO1)  # or similar
score_tpot = max(0, 1 - TPOT / SLO2)
score = w_tp * throughput_component + w_c * (score_tdr + score_tpot) / 2
```

**Hypothesis B: Throughput with penalty**

```text
throughput = total_tokens / total_time
score = throughput * (1 - penalty)
where penalty = w_c * sum(max(0, TDR_i - SLO1)) + w_tp * sum(max(0, TPOT_i - SLO2))
```

**Hypothesis C: Baseline-relative**

```text
score = 1000 * (our_throughput - tp_base) / (tp_UB - tp_base)
- dist_base * (violation_measure)
```

### 2.3 Derivation Strategy I used

1. I ran the public worked example (Example 1) with a minimal solver.
2. I recorded TDR and TPOT for each request.
3. I tried to fit the reported score using the parameters.
4. Where the formula was not explicit, I tested linear combinations of:
   - `sum(tokens_i)` / `total_time`
   - `sum(max(0, SLO1 - TDR_i))`
   - `sum(max(0, SLO2 - TPOT_i))`
   - `count(TDR_i <= SLO1)` / R
   - `count(TPOT_i <= SLO2)` / R

### 2.4 Optimization Strategy per Score Type

| Score Type | Strategy |
| ----------- | ---------- |
| **Throughput-dominated** (w_tp >> w_c) | Maximize decode batch sizes. Sacrifice some TDR for higher throughput. **Keep the LOCAL computer 100% saturated** (it is the bottleneck — clouds are 16x less loaded, so their utilization is secondary). |
| **Latency-dominated** (w_c >> w_tp) | Minimize TDR at all costs. Start prefill immediately. Small or no chunking. Decode batch size 1 if it reduces wait time. |
| **Hard SLO** (violations = heavy penalty) | Conservative admission. Ensure every request meets SLO1/SLO2 before maximizing throughput. |
| **Soft SLO** (linear penalty) | Trade off marginally. Optimize the weighted sum directly. |
| **Baseline-relative** | Find the baseline's weakness. If baseline doesn't batch, batch aggressively. If baseline doesn't chunk, find the chunking sweet spot. |

---

## 3. The Interactive Protocol: Architecture for Correctness

The interactive protocol is the #1 source of WA and RE. One misread frame and your state diverges from the interactor's.

### 3.1 The Frame Loop (Critical)

```text
while (true):
    read t (double, 9 decimals)
    if (EOF): break
    read e (int)
    read e event lines
    process all events into state update
    decisions = scheduler.decide(state, t)
    print decisions.size()
    for each decision: print formatted line
    flush stdout
```

**Rules:**

- You must read the **entire frame** before printing.
- All your assignments start at timestamp `t`.
- You cannot assign a task that depends on an event in the same frame? Actually: "Your response may use any event in the frame: every resource freed by a TDN is free, even if that event is the last line." So yes, you can use TDNs from the current frame.
- No self-wake timer. If no events arrive, you do nothing.

### 3.2 Event Processing Order

Within a frame, process events in this order:

1. **TDN** (task done): Free the computer. If this task triggered a transfer, queue the transfer with completion time `t + transfer_duration`.
2. **XDN** (transfer done): Mark data ready. This may enable a new task.
3. **ARR** (arrival): Create new request. This may enable P PRE.
4. **FIN** (finished): Mark request complete. Do not schedule again.

**Why this order:** A TDN at time `t` frees a computer at `t`. An XDN at time `t` makes data available at `t`. An ARR at time `t` makes the request available at `t`. All of these can enable tasks that you schedule in response to this frame.

### 3.3 Assignment Format Validation

Every assignment line must be exactly:

```text
P PRE <cloud_id> <request_id>
P PROC <request_id> <ls><le><m><m><m>
```

**Validation checklist:**

- P PRE: cloud_id in [0, K-1], request_id exists and is in ARR phase.
- P PROC: ls < le, ls >= 0, le <= num_layers, gap-free with previous chunk.
- P POST: request_id has completed all P PROC chunks and DOWN transfer.
- D PRE: all request_ids are ready for decode (P POST done or previous D POST done), not finished.
- D PROC: all request_ids assigned to the same cloud, all have UP XDN for current iteration.
- D POST: all request_ids have DOWN XDN for current iteration.
- No duplicate request_ids within a group.
- No finished request in any group.

### 3.4 Double Precision Traps

Times are real milliseconds with 9 decimal places. Use `double` for all times.

**Dangers:**

- `t1 == t2` is unsafe. Use `t1 > t2 + EPS` or `t1 < t2 - EPS` where `EPS = 1e-9`.
- `busy_until <= t` means "computer is free at time t". Use `busy_until <= t + EPS`.
- Accumulating times: `busy_until = max(busy_until, t) + S + duration`. This is safe if all inputs are positive.
- Do not cast double to int for time comparisons. Ever.

---

## 4. Performance Engineering

### 4.1 Time Complexity Budget

Per frame, you may need to:

- Process e events: O(e)
- Find ready tasks: O(R) in naive scan, O(1) or O(log R) with active lists
- Make scheduling decisions: O(R log R) for sorting, O(R) for greedy
- Output assignments: O(a) where a = number of assignments

For R = 1000, K = 8, frames = ~10000:

- Total budget: ~5 seconds CPU time
- Per-frame budget: ~0.5ms

**Target complexity:** O(R log R) total per frame, not O(R²).

### 4.2 Data Structure Choices

| Operation | Structure | Why |
| ----------- | ----------- | ----- |
| Find free clouds | `priority_queue` or sorted array | O(log K) or O(K) — K is small (<=16), so array scan is fine |
| Find ready requests | Active lists per phase | O(1) to iterate ready set, O(log R) to move between phases |
| Task-time lookup | Sorted array + binary search | O(log N) per query, N <= 4096 |
| Transfer queue | FIFO queue per direction, per cloud | O(1) enqueue/dequeue |
| State validation | Debug-only scans | O(R) in debug, O(1) in release |

### 4.3 Memory Layout

- Pre-allocate vectors for requests, clouds, queues.
- No `std::map` or `std::set` in hot path. Use `std::vector` + manual indexing.
- Request objects should be contiguous in memory (vector, not list).
- Transfer queues: `std::deque` is fine (O(1) push/pop, good cache locality).

### 4.4 The Hot Path

The per-frame hot path is:

1. Process events (free computers, complete transfers)
2. Identify newly ready tasks
3. Assign tasks to free computers

**Optimization:** Maintain these invariants incrementally:

- `free_clouds`: list of clouds with `busy_until <= current_t`
- `ready_ppre`: list of requests waiting for P PRE
- `ready_pproc[k]`: list of requests waiting for P PROC on cloud k
- `ready_ppost`: list waiting for P POST
- `ready_dpre`: list waiting for D PRE
- `ready_dproc[k]`: list waiting for D PROC on cloud k
- `ready_dpost`: list waiting for D POST

When an event occurs, move requests between lists. When making decisions, scan only the relevant ready list.

**Avoid:** Scanning all R requests every frame to find ready ones. This is O(R * frames) = O(R²) and will TLE on large cases.

---

## 5. Hidden Test Taxonomy (What They Will Throw At You)

### 5.1 Protocol Edge Cases

| Test | What It Does | How You Fail |
| ------ | ------------- | -------------- |
| `empty_first_frame` | First frame has e=0, just a timestamp | You print nothing, but must still read correctly |
| `eof_after_config` | Stream ends after startup config | Your parser loops forever or crashes |
| `eof_mid_table` | Stream ends after table row 3 of 10 | Uninitialized values, crash |
| `same_timestamp_burst` | 1000 events at identical timestamp t | O(e²) event processing, TLE |
| `precision_boundary` | t = 0.000000001, S = 0.000000001 | Double comparison with `==` fails |
| `massive_frame` | e = 10000 in one frame | Stack overflow if you allocate arrays on stack |

### 5.2 Resource Bottleneck Cases

| Test | Parameters | What It Tests |
| ------ | ----------- | ------------- |
| `transfer_bound` | latency=1000, bw=0.001 | UP/DOWN queues dominate. Compute is idle. Staggering essential. |
| `compute_bound` | latency=0.001, bw=1000, prefill_proc=1000 | Compute dominates. Transfer is instant. Load balance essential. |
| `local_bound` | S=100, prefill_pre=100, decode_pre=100 | Local computer is bottleneck. Clouds idle. Local scheduling matters. |
| `cloud_bound` (RARE edge) | S=0, prefill_proc=1000, decode_proc=1000, AND local task durations ≈ 0 | Only when local P PRE/P POST/D PRE/D POST are near-instant does the cloud become the bottleneck. In the GENERAL lifecycle the LOCAL computer is the bottleneck (~16x more tasks, since it processes D PRE+D POST for every decode token), so this case is the exception, not the rule. |
| `imbalanced_k` | K=1 vs K=16 with same workload | K=1: serialization. K=16: parallelism but transfer bursts. |

### 5.3 Algorithm Traps

| Test | Trap |
| ------ | ------ |
| `batching_linear` | decode_proc(m) = m * 10. Batching provides zero benefit. Solver that always batches wastes time waiting for batchmates. |
| `batching_superlinear` | decode_proc(m) = m². Batching is catastrophic. |
| `chunking_hurt` | S=500, num_layers=2. Two chunks pays 1000 setup for no benefit. |
| `chunking_help` | S=0, num_layers=100, local_pre=100. Chunking keeps local busy. |
| `cloud_affinity` | Requests arrive such that greedy assigns all to C0 because C0 finishes 1ms earlier. But C0's UP queue explodes. |
| `tight_tdr` | SLO1 = minimum possible TDR. Any delay = violation. |
| `tight_tpot` | SLO2 = minimum possible TPOT. Any batching delay = violation. |

### 5.4 Scale Traps

| Test | Scale | Danger |
| ------ | ------- | -------- |
| `r_10_k_1` | R=10 | Basic correctness |
| `r_1000_k_8` | R=1000, K=8 | O(R²) algorithm TLEs |
| `r_10000_k_16` | R=10000, K=16 | O(R * K) may TLE if not careful |
| `layers_1000` | num_layers=1000 | Chunking decisions matter more |
| `lout_1000` | Lout=1000 per request | Decode iteration count huge. State tracking must be efficient. |

---

## 6. Research Directions (Priority Order I used)

**How I used this mandate:** The tiers below were starting points, **not limits**. I checked further avenues including LLM inference systems, scheduling theory, queueing analysis, pipelining, batching heuristics, competitive analysis, real-world serving engines, and contest write-ups. There were **no limits on ideas**.

I brought every idea back, summarized it, and evaluated it against my correctness-first doctrine. Where a new idea looked promising, I recorded it with a hypothesis and a proposed A/B test before any code change.

I added findings to this document as they arrived.

### Tier 1: Read and Port (Days 1-3)

1. **Graham (1966) — List Scheduling**
   - Insight: Greedy is optimal-enough for identical machines.
   - Port: Cloud assignment policy.

2. **Allahverdi et al. — Scheduling with Setup Times (Survey)**
   - Insight: The marginal benefit rule for batching.
   - Port: Decode batching policy.

3. **PipeDream / GPipe / Zero Bubble (your notes 501/502)**
   - Insight: Optimal chunking for pipeline stages.
   - Port: Prefill chunking policy.

### Tier 2: Read and Reference (Days 4-7)

4. **Potts & Kovalyov — Scheduling with Batching**
   - Insight: SPT-batch rule and dynamic programming for optimal batch sizes.
   - Reference: For proving your greedy batching is near-optimal.

5. **Azar et al. — Online Load Balancing**
   - Insight: Lower bounds on competitive ratio.
   - Reference: To know when you've hit the theoretical limit.

6. **Queueing Theory — M/D/1 with Batch Arrivals**
   - Insight: How FIFO queues behave under bursty arrivals.
   - Reference: For understanding when staggering helps.

### Tier 3: Skim for Ideas (Days 8-10)

7. **FastServe / Sarathi-Serve / DistServe (your notes 305/403/401)**
   - Insight: Real systems use chunking + batching. Their heuristics are empirically tuned.
   - Reference: For validation that your approach matches real-world best practices.

8. **SplitLLM (your note 406)**
   - Insight: Model splitting strategies.
   - Reference: Less relevant since split point is fixed, but useful for understanding communication-compute tradeoffs.

---

## 7. The A/B Testing Framework I enforced

Every optimization I shipped was A/B tested. The framework:

```python
# tools/ab_test.py usage
python3 tools/ab_test.py \
    --baseline build/frozen_vN \
    --candidate build/candidate_vN+1 \
    --cases cases/dev/*.txt \
    --metric score \
    --min_improvement 0.01 \
    --max_regression 0.00
```

**Rules I enforced:**

- Candidate had to be valid on 100% of cases.
- Candidate had to not regress any case by more than 0.1%.
- Candidate had to improve aggregate score by at least 1% to be promoted.
- One fresh secret evaluation per promotion.

---

## 8. The Panic Checklist (What I checked when score was zero)

When I submitted and got zero or near-zero, I checked:

1. **I/O format.** Had I flushed? Had I output the right number of assignment lines? Had I used `'\n'` not `std::endl`?
2. **Assignment validity.** Was I assigning to busy computers? Were chunks gap-free? Were D PROC groups same-cloud?
3. **Event processing.** Had I read the entire frame? Handled e=0? Processed TDN before XDN before ARR?
4. **Double precision.** Was I comparing times with `==`? Accumulating floating point error?
5. **Table interpolation.** Had I handled -1? Clamped correctly? Interpolated linearly?
6. **Chunking rules.** Was ls=0 for first chunk? le=num_layers for last? Pieces ascending gap-free?
7. **Batching rules.** Were all ids distinct? No finished requests? D PROC same-cloud?
8. **Transfer model.** Was I simulating FIFO correctly? Were both directions active simultaneously?
9. **Score formula.** Had I derived it correctly? Was I optimizing the right thing?

---

## 9. Daily Optimization Targets I executed

| Day | Target | Milestone I hit |
| ----- | -------- | ----------- |
| 0 | I/O + Baseline | Compiled, submitted, non-zero score |
| 1 | Zoo pass | Zero crashes on all zoo cases |
| 2 | Cloud policy | A/B: greedy vs. transfer-aware |
| 3 | Decode batching | A/B: size-1 vs. marginal-benefit |
| 4 | Prefill chunking | A/B: 1-chunk vs. adaptive |
| 5 | Score tuning | Derived formula, optimized weights |
| 6 | Integration | Best policies combined, frozen |
| 7 | Polish | Profiled, micro-optimized |
| 8-10 | Advanced | Queue-aware staggering, predictive |
| 11-14 | Hold | No changes unless panic |

---

## 10. Final Principle I proved

The solver that won was not the one with the cleverest algorithm. It was the one that did not crash on edge cases, did not TLE on large cases, and did not WA on protocol traps — that discipline carried me to #56.

I optimized only after correctness was proven. The frozen baseline that scored 400 points reliably beat the experimental solver that scored 800 points on 80% of cases and crashed on 20%.

My edge was not one brilliant insight. My edge was the combination I built:

- Work-conservation (never idle)
- Transfer-aware load balancing
- Adaptive chunking
- Marginal-benefit batching
- Bulletproof protocol handling

I built these five pieces correctly, and tuned them well — that is what put me in the top 1.3%.

---

End of OPTIMIZATION_DOCTRINE.md. I read it before every coding session during the contest.
