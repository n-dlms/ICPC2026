> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). The sweep brief below was executed pre-contest and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# RESEARCH BRIEF — Open-Ended Literature & Technique Sweep

## Target: ICPC 2026 Codeforces Problem A — "Edge–Cloud Collaborative Scheduling"

> **This is an ICPC 2026 Codeforces problem.** It is an interactive, online,
> deterministic scheduling/scoring problem. The sweep below was completed pre-contest, while
> the working solver was competitive (leading the preliminary standings at the time), to find
> mechanisms the current architecture might have been blind to. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). **Do not assume the contest-time approach was optimal. Treat it as
> strong but beatable.** The brief explicitly asked for things not yet
> considered.

---

## 0. WHAT WE WANT FROM YOU (READ THIS FIRST)

We want an **exhaustive, evidence-backed catalogue** of algorithms, policies,
theorems, systems, and control strategies from any field that could plausibly
improve an online scheduler for this exact problem. For every candidate you
report, give:

1. **Name & canonical reference** (paper, theorem, system, or textbook).
2. **One-paragraph description** of what it does.
3. **Why it might apply here** — mapped to a specific mechanic of this problem.
4. **Expected impact hypothesis** — which scoring component it targets
   (throughput vs. TDR vs. TPOT), and on what kind of test.

5. **Feasibility under our constraints** (online, reactive, no future knowledge,
   15 s wall-clock, 256 MB, single-threaded C++, deterministic interactor).

6. **Risk / failure mode** — how it could hurt or when it degenerates.
7. **A concrete pointer** to how it would change our scheduler (which knob:
   batch size, admission, load balancing, chunking, priority, timing).

**Rank all findings by (expected score impact × feasibility) at the end.**
Prefer named, citable methods over hand-waving. If a field is relevant but you
are unsure of specifics, SAY SO and list what to look up — do not fabricate
citations. We value honest "I don't know, but search here" over confident noise.

---

## 1. THE PROBLEM, STATED PRECISELY

*(Quoting the mechanics of the ICPC 2026 Codeforces problem.)*

### 1.1 Topology

- **One local computer (Edge / E)** and **K identical remote computers (Cloud / C0..C_{K-1})**, K ≤ 8.
- **Two shared FIFO links**: UP (local→remote) and DOWN (remote→local). Each is
  a **single one-at-a-time FIFO queue shared by all K remotes**. Both directions
  may be active simultaneously. Transfers are automatic (we never schedule them
  directly); they are queued by the interactor when triggering tasks complete.

- Each computer runs **at most one task at a time**, non-preemptible.
- A task assigned at time `t` with duration `dur` occupies its computer over
  `[t, t + S + dur]` where `S` is a fixed **schedule cost** paid once per task.
  Transfers do not pay `S`.

### 1.2 Request lifecycle

Each request `i` has input length `Lin[i]` (revealed at arrival) and hidden
output length `Lout[i]` (revealed only at FIN). It runs:

1. **Prefill (input stage), once, single-request only:**
   `P_PRE (E)` → UP transfer (len=Lin) → `P_PROC (C)` → DOWN transfer (len=Lin) → `P_POST (E)`.

   - `P_PROC` computes `num_layers` "parts" and **may be split into ascending
     gap-free ranges `[ls,le)`** so other remote work can run between pieces.
     Piece duration = `(le-ls)/num_layers × prefill_proc(Lin)`.

   - Completion of `P_POST` makes the request "ready" and **ends TDR**.
2. **Decode (output steps), exactly `Lout[i]` iterations, one token each:**
   `D_PRE (E)` → UP transfers (one per distinct remote, len=that remote's member count)
   → `D_PROC (C)` → DOWN transfer (len=group size) → `D_POST (E)` → **one token per member**.

   - **Decode tasks can group multiple requests** (batch). `D_PRE`/`D_POST` may mix
     requests from different remotes; `D_PROC` requires all members on one remote.

   - Request's remote is **fixed at `P_PRE`** and never changes.

### 1.3 Task durations

Given by a **task-time table** indexed by `batch_size` (for prefill, `Lin`;
for decode, member count). Values are piecewise-linear-interpolated between
listed rows; clamped below the smallest and above the largest row. Durations
are fixed per (step, size) — independent of token position or KV size.

### 1.4 Transfer time

`transfer_time = latency_ms + 8·len·bytes_per_token / (bandwidth_gbps·10⁶)`.
`latency_ms > 0` always, so every transfer costs a **fixed latency overhead**
plus a byte-proportional term. **Batching amortizes the fixed latency.**

### 1.5 Scoring (the objective)

`Score = 1000 × ( w_tp·norm_tp + w_c·norm_c )`, with `w_tp + w_c = 1`.

- **Throughput:** `tp = total_tokens / makespan`, where
  `makespan = last_token_time − earliest_arrival`.
  `norm_tp = clamp((tp − tp_base)/(tp_UB − tp_base), 0, 1)`.

- **Waiting:**
  `TDR` = mean over requests of (P_POST completion − arrival).
  `TPOT` = mean over consecutive token gaps (pooled across requests).
  `excess_tdr = max(0, (TDR−SLO1)/SLO1)`, `excess_tpot = max(0, (TPOT−SLO2)/SLO2)`.
  `dist = sqrt(excess_tdr² + excess_tpot²)`.
  `norm_c = max(0, 1 − dist/dist_base)` (or 0/1 step if `dist_base=0`).

### 1.6 Hard constraints on the solver

- **Online:** `R` (total requests) is unknown; arrivals are unknown; `Lout` hidden.
- **Reactive only:** we act only when an event frame arrives; we cannot self-wake
  or schedule at future times. No predicting the future is required (though we may
  simulate ahead if we wish).

- **No preemption**, no pausing. Deterministic, non-adaptive interactor.
- Per frame we may assign ≤ K+1 tasks (one per computer).
- **Stuck states score 0** — we must never permanently abandon a request.
- 15 s wall-clock, 256 MB, up to 2×10⁶ frames, up to 2000 requests, up to 2×10⁵ total tokens.

### 1.7 What we control (the decision knobs)

1. **Admission:** when to start a new request's `P_PRE` (and which remote to assign).
2. **Load balancing:** which remote each request lives on (fixed forever).
3. **Chunking:** how to split `P_PROC` into `[ls,le)` pieces.
4. **Batching/grouping:** which ready requests to combine into each decode
   `D_PRE` / `D_PROC` / `D_POST`.

5. **Timing/holding:** whether to run a task now or wait for a future frame
   (e.g., to accumulate a bigger batch).

---

## 2. WHAT OUR CURRENT SOLVER DOES (ARCHITECTURE INVENTORY)

Treat this as "strong but beatable." Components:

- **Reactive greedy frame loop:** read frame → update state → pick one local task
  - one task per free cloud → flush.
- **Prefill admission cap** (`PPRE_MAX_INFLIGHT`): limits prefill UP transfers in
  flight to protect decode UP from FIFO congestion; relaxed when throughput-dominant.

- **Load balancer (`PrefillLoadBalanceCloud`):** picks remote minimizing estimated
  completion time (queued P_PROC + projected decode batches).

- **Chunking:** currently always one full piece (`OnePieceChunk`); an `AdaptiveChunk`
  exists but is effectively disabled (gated on `S < 1`, and S ≥ 1 always).

- **Batch policy (`MarginalBatch`):** precomputes per-column EOQ-style batch caps by
  "increase m while marginal duration increase ≤ average cost per member"; then
  caps by an SLO2-feasibility scan and a shadow-price comparison (marginal value of
  throughput vs. TPOT loss). **Known limitation: batch size can only be capped DOWN
  from the EOQ optimum, never raised above it.**

- **SLO-aware delay (`SloAwareDelay`):** sometimes holds `D_PRE` to accumulate a
  larger batch, comparing throughput gain vs. wait/TPOT/TDR loss via shadow prices.

- **Static regime scalar `alpha = w_tp/(w_tp+w_c)`** computed once at startup;
  selects delay policy and cross-remote grouping aggressiveness.

- **Bookkeeping:** per-phase ready lists, per-cloud queues, an "oracle" tracking
  projected TDR/TPOT/tp/marginal values; busy-time accumulators for E/UP/DOWN/C
  (**currently diagnostic-only — never read back to make decisions**).

**Suspected gaps (verify/refute):**

- Throughput-bound tests (~30% of `norm_tp`) where batch size is pinned below optimum.
- TDR-blown tests where prefill/UP FIFO congestion dominates.
- No closed feedback loop from live utilization to decisions.
- Fixed local-task priority may be wrong regime-by-regime.
- No deliberate decode **pipelining** (sub-batching to overlap E/UP/C/DOWN stages).

---

## 3. RESEARCH MANDATE — THE FULL TAXONOMY

**Investigate every area below.** For each, extract what is actually applicable.
Be thorough; we would rather triage 200 leads than miss the one that wins.

### A. Online scheduling theory

- Competitive analysis of online scheduling; lower bounds for online flow shop.
- Work-conserving vs. non-work-conserving policies; when deliberate idling helps.
- **Clairvoyant vs. non-clairvoyant scheduling**; sojourn-time-optimal policies.
- **Shortest-Job-First / Shortest-Remaining-Time-First** optimality for mean
  response time (and its limits under unknown sizes); **SRPT/SJF approximations**
  when size is partially known (we know `Lin`, not `Lout`).

- **Gittins index** scheduling for unknown/heterogeneous jobs.
- Scheduling with **setup times / sequence-dependent setups**.
- **Makespan minimization on unrelated/identical parallel machines**; Graham's
  bounds, LPT, list scheduling, and their online variants.

- **Open shop / flow shop / job shop** scheduling (this pipeline is a flow shop
  with a shared transport resource); flow-shop makespan heuristics (NEH, Johnson's
  rule for 2-machine, Taillard, etc.) and their online/stochastic versions.

- Scheduling with **communication delays / DAG scheduling** (HEFT, list scheduling,
  communication-aware task assignment).

- **Real-time scheduling:** EDF, RMS, LLF — for SLO/deadline-like targets.

### B. Queueing theory

- **M/G/1, G/G/1, GI/G/1** mean waiting; **Pollaczek–Khinchine** formula (esp.
  how service-time *variance* inflates wait — batching changes variance!).

- **Kingman's approximation** for G/G/1 waiting time.
- **Heavy-traffic / diffusion approximations** for queues near saturation.
- **Queueing networks / Jackson networks**; tandem queues (our pipeline is tandem).
- **Batch arrivals / batch service queues** (M^X/M/1, bulk-service queues) — the
  decode loop is literally a bulk-service queue.

- **Setup/vacation models**: servers that need setup before serving (our `S` cost
  and transfer latency behave like setups/server vacations).

- **Priority queues, conservation laws**, and the **cμ-rule** for multi-class
  holding-cost minimization (map TDR/TPOT to holding costs).

- Fork-join queues (grouping across remotes then synchronizing at D_POST).
- How **FIFO variance** differs from compute-time variance (latency blow-up on a
  congested shared link).

### C. LLM INFERENCE SERVING SYSTEMS (highest prior — this IS that problem)

The problem maps almost 1:1 onto **LLM serving with prefill/decode disaggregation**:
`P_PRE/P_PROC/P_POST` ≈ prefill, `D_*` ≈ decode steps, `bytes_per_token` ≈ KV-cache
movement, remotes ≈ GPU workers, links ≈ interconnect. **Mine this literature hard:**

- **Orca** — iteration-level scheduling / **continuous batching** (vs. static batching).
- **Sarathi / Sarathi-Serve** — **chunked prefills**, "stall-free batching",
  prefill-decode **pipelining** to keep GPUs busy and tame TPOT.

- **DistServe / Splitwise / TetriInfer** — **prefill-decode disaggregation** and
  KV-cache transfer scheduling; goodput under TTFT/TPOT SLOs.

- **Mooncake (Moonshot)** — KVCache-centric disaggregated architecture; transfer-engine scheduling.
- **AlpaServe / ServerlessLLM** — model placement, multiplexing, burst handling.
- **FastServe** — **preemptive SJF** (MLFQ) to minimize TTFT/job completion under unknown lengths.
- **Sloth / Vidur / Llumnix / Andes** — SLO-aware scheduling, live migration,
  TTFT/TPOT SLO attainment prediction.

- **vLLM / PagedAttention** — memory/batching implications (mostly background).
- **Speculative/parallel decoding** analogies (less likely applicable but check).
- **Goodput definitions** and how systems balance throughput vs. SLO attainment —
  directly analogous to our `w_tp·norm_tp + w_c·norm_c`.

- Specifically look for: **when to chunk prefill**, **how big to batch decode**,
  **prefill admission control**, **prefill/decode interference**, and **transfer
  overlap with compute** — each maps to a knob we have.

### D. Batch-processing & setup-time scheduling

- **Batch processing machines** (burn-in ovens) — batch size vs. flow time tradeoffs.
- Economic **EOQ and its scheduling analogs**; optimal batch size under fixed
  setup + holding cost (we already use an EOQ-flavored rule — find its true optimum form).

- **Batching to amortize fixed overhead** (setup, latency) vs. latency penalty —
  formal results on the throughput-vs-delay Pareto curve of batch size.

### E. Pipeline / flow control

- **Pipeline parallelism** (GPipe/1F1B/micro-batching): sub-batching to overlap
  stages — **is deliberate decode sub-batching our missing throughput lever?**

- **Flow control / congestion control:** TCP (AIMD), window sizing, **BBR**,
  **Vegas**, ECN/AQM ideas — for regulating how many requests we admit/in-flight.

- **Little's Law** (`L = λW`) as a diagnostic/steering identity.
- **Throughput–latency Pareto frontier** and how to operate on it given weights.
- **Kanban / CONWIP** (constant work-in-progress) production control — cap WIP to
  reduce flow time while saturating the bottleneck. **Consider CONWIP-style admission.**

- **Theory of Constraints / bottleneck-first** scheduling (Drum-Buffer-Rope).

### F. Admission control & index policies

- **Admission control** in queues (when to accept new requests) — MDP-optimal
  admission thresholds.

- **Ski rental problem** — the "keep waiting vs. act now" decision for D_PRE holds.
- **Whittle index / restless bandits** for indexable admission and activation.
- **cμ-rule**, **Smith's rule / WSPT** (weighted shortest processing time) for
  ordering; check applicability to our priority choices.

### G. Load balancing

- **Join-shortest-queue, power-of-two-choices**, consistent hashing.
- Load balancing under **heterogeneous, persistent assignment** (our remote is
  fixed — this is a one-shot bin/vector packing decision with downstream cost).

- **Stochastic knapsack / vector bin packing** for choosing remotes at P_PRE.
- How to hedge against unknown future `Lout` when assigning remotes.

### H. Stochastic control, bandits, RL, ADP

- **Multi-armed bandits** (UCB, Thompson sampling) for online-tuning hyperparameters
  (batch caps, admission) per test without prediction.

- **Contextual bandits** keyed on observed config (`w_tp`, SLOs, latency, K).
- **Markov decision processes / approximate dynamic programming** for admission &
  batching; value iteration on a reduced state.

- **Reinforcement learning** for LLM-serving schedulers (papers exist) — but note
  we cannot train online per test; only pre-trained policies or offline reasoning.

- **Bayesian optimization / offline simulation-based tuning** of our heuristics
  using the deterministic interactor (we can replay/configure offline?).

### I. Lyapunov / drift-plus-penalty

- **Neely's drift-plus-penalty** method for online utility maximization with queue
  stability — naturally yields throughput-vs-delay tradeoffs with provable guarantees.
  Check whether a Lyapunov policy for "maximize tokens while bounding TDR/TPOT
  queues" gives a principled version of our shadow-price heuristic.

### J. Mathematical programming & decomposition (for offline planning/simulation)

- **LP/IP relaxations**, column generation, Lagrangian relaxation / **subgradient**
  methods; **primal-dual** online algorithms.

- Use only for deriving structural insight or offline warm-starting, since the live
  run must be reactive.

### K. Metaheuristics & simulation-based search

- Simulated annealing / genetic / local search over **policy parameters**;
  offline parameter sweeps per config family. (Feasibility: tuning happens offline.)

### L. Scoring-function exploitation (meta but important)

- Since `norm_tp` and `norm_c` are **clamped linear**, identify when we are
  "clamped at 1" on one component and should spend all effort on the other.

- When `dist_base=0` the waiting component is **all-or-nothing**: find conditions
  under which achieving `dist=0` is feasible and restructure to guarantee it
  (we already have a "cliff flip" — formalize/extend).

- Sensitivity of the score near clamp boundaries; marginal-value-driven decisions
  (we have shadow prices — check they are the correct dual variables).

### M. Networking analogies

- **Nagle's algorithm / delayed ACK / interrupt coalescing** — exactly the
  "batch now vs. wait to coalesce" tension of D_PRE holds. Extract the optimal
  stopping rule literature.

- **Packet coalescing, framing overhead** analogs of our per-transfer latency.

### N. Numerical & implementation details

- Correctness of piecewise-linear interpolation and extrapolation at boundaries.
- Floating-point pitfalls in parse_double / scoring estimates (audit).
- Fast I/O under 2×10⁶ frames (already addressed; verify no hidden cost).

### O. Heavy-traffic / fluid models for decision-making

- Fluid-limit and mean-field models to predict the **bottleneck resource** from
  config alone (so we can pick a regime before observing it).

- Using **utilization estimates** (we already accumulate E/UP/DOWN/C busy time) as
  feedback — control-theoretic regulators (PI/PID) on batch size or admission.

---

## 4. SPECIFIC OPEN QUESTIONS WE NEED ANSWERED

1. **Is throughput monotone non-decreasing in decode batch size?** Under our
   sublinear/clamped tables, is the throughput-optimal batch always the largest
   SLO2-feasible one? Give conditions and a counterexample if any.

2. **What is the principled prefill-admission rule** as a function of SLO1 slack,
   UP transfer time, and throughput weight? (We suspect a queueing bound
   `n_max ≈ 1 + f·(SLO1 − TDR_min)/τ̄`; validate or replace with something better.)

3. **Should we deliberately sub-batch decode to pipeline E/UP/C/DOWN?** What does
   the Sarathi/1F1B literature say about the optimal micro-batch size given a
   shared FIFO transfer bottleneck?

4. **What is the correct local-task priority** (P_PRE vs P_POST vs D_PRE vs D_POST)
   as a function of the current bottleneck? Is there a theorem (e.g., cμ-rule or
   bottleneck-work-first) that prescribes it?

5. **Fixed remote assignment with unknown Lout:** what is the best one-shot load-
   balancing rule (stochastic knapsack / power-of-two-choices analog) to minimize
   makespan and TDR simultaneously?

6. **Chunking P_PROC:** when (if ever) does splitting the input stage into pieces
   beat one full piece, given `S ≥ 1` and shared links? Find the exact condition.

7. **Can a Lyapunov / drift-plus-penalty policy** reproduce or beat our shadow-price
   batching/holds with provable guarantees, and is it implementable reactively?

8. **Continuous-batching analog:** our decode is effectively iteration-level already;
   is there any Orca-style improvement (e.g., admitting new requests' first decode
   into an existing batch mid-flight) that the rules permit and we are missing?

---

## 5. DELIVERABLES & FORMAT

Return a single document with:

1. **Executive summary:** top-5 ranked opportunities, each ≤ ½ page.
2. **Full catalogue** organized by the taxonomy sections above.
3. For each candidate: the 7 fields listed in §0.
4. **A "kill list":** things we currently do that literature says are likely
   suboptimal, with reasoning.

5. **A verification plan:** for the top candidates, how to A/B test them one at a
   time on the deterministic interactor and which tests/telemetry would confirm impact.

---

## 6. GROUND RULES

- **Be honest about uncertainty.** If you are not sure a result exists or applies,
  flag it and say what to search for. No invented citations.

- **Prefer primary sources** (papers, canonical theorems, named systems) over
  folklore, but folklore with a clear mechanism is acceptable if labeled.

- **Map everything to a concrete knob** (§1.7). A technique with no implementable
  mapping under our constraints should be marked "insight-only."

- **Respect the constraints:** online, reactive, no future knowledge, non-preemptive,
  15 s / 256 MB, deterministic scorer. Anything requiring training at test time or
  lookahead is out unless it can be precomputed.

- **Remember the objective is a weighted, clamped sum** of throughput and
  SLO-attainment; every proposal should say which term it moves and by how much we
  might expect.

---

*End of brief. This is an ICPC 2026 Codeforces problem. Go wide, go deep, and
surface whatever we have not thought of — including ideas from fields that seem
only tangentially related. The winning mechanism may be one we haven't named yet.*
