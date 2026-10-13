# SA08 — Literature Research: Verified, Transferable Scheduling Knowledge for ICPC 2251

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA08 drafting). Read-only on src/, tools/, cases/; papers landed in
`research/papers/` (manifest.json updated, 33 entries); this file in `research/scoring/`. Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56.
Verification protocol: every quoted claim below is checkable in a `.txt` extraction in
`research/papers/` at the cited line numbers (pdftotext of the downloaded PDF). Anything
not checkable is marked [UNVERIFIED] or discarded. Physics laws L1–L14 and ranked
change candidates are cited from `research/scoring/SA07_control_restatement.md`;
value keys from `research/scoring/SA06_admission_value.md` §3.1.

---

## 1. Inventory

### 1.1 Before (68 files: 33 PDFs w/ .txt twins + docs; session start 2026-08-23)

Already present and (spot-)verified this session: DistServe (2401.09670), Sarathi-Serve
(2403.02310), Orca (osdi22-yu.pdf), vLLM/PagedAttention (2309.06180), FastServe
(2305.05920), PDAgg/TaiChi (2508.01989), Dai et al. throughput-optimal (2504.07347),
SCORPIO (2505.23022), SLICE (2510.18544), WISP (2601.11652), PerLLM (2405.14636),
SplitLLM (2410.10759), edge-cloud collab survey (2507.16731), Clipper, GPipe
(1811.06965), PipeDream (1806.03377), PipeDream-2BW (2006.09503), Zero Bubble
(2401.10241), CrossPipe (2507.00217), 2-machine flow shop with exact delays (Ageev
1711.00081), batch-setup approximations (1810.01223), Allahverdi setup survey,
Baker–Trietsch batching notes (ch.13), serial-batch papers (2503.23273, 2504.08793),
online scheduling set (packet/deadlines/throughput: 1606.09395, 2305.07164, 1912.10769,
2111.06564, Goldwasser SODA99, Moore–Hodgson 2104.06210, 2607.16347), queueing set
(federgruen-groenevelt-1986, batch-service MAP vacation, bulk-queue thresholds,
Kingman/PK, Adan–Resing, Mitzenmacher 2-choices, load-balancing 2026), plus
SYNTHESIS-intentional-delay.md and triton-batcher-docs.md.

### 1.2 Added this session (7 papers, PDF + .txt + manifest entries)

| File                                          | What it is                                                                                                                    | Why sought                                                                 |
| --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| `splitwise-2311.18677.pdf/.txt`               | Splitwise (ISCA'24, MSR): prefill/decode phase splitting, chunked prefill pools, ratio control                                | task item 1                                                                |
| `mooncake-2407.00079.pdf/.txt`                | Mooncake (FAST'25, Moonshot): KVCache-centric disaggregation; Conductor pairs prefill+decode instances under bandwidth limits | task item 1                                                                |
| `alpaserve-2302.11665.pdf/.txt`               | AlpaServe (OSDI'23): statistical multiplexing vs model-parallelism overhead                                                   | task item 1                                                                |
| `clockwork-2006.02464.pdf/.txt`               | Clockwork (OSDI'20): predictability from the bottom up; centralized controller                                                | task item 1                                                                |
| `elidrissi2020-single-server-thesis.pdf/.txt` | El Idrissi PhD (HAL, 2020): ch.1 survey + algorithms for **P,S1 (parallel machines + ONE shared server)**                     | task item 2 — the exact OR model of our local computer feeding K clouds    |
| `botsali2019-lot-streaming.pdf/.txt`          | Botsali 2019 survey of **lot streaming** (2/3/m-machine; Trietsch–Baker line)                                                 | task item 2 — splitting a cohort into overlapped sublots = wave pipelining |
| `eos-ch04-weighted-completion.pdf/.txt`       | Elements of Scheduling ch.4 (Lawler/Queyranne/Schulz/Shmoys): Smith's rule, **SRPT Thm 4.25/4.26**, convert-to-nonpreemptive  | task item 3                                                                |

Not obtained (paywalled, content covered by downloaded surveys, marked where used):
Trietsch–Baker 1993 and Hall–Potts–Sriskandarajah 2000 originals (INFORMS/Elsevier
paywalls — the Botsali survey and El Idrissi ch.1 reproduce and cite their results;
HPS-specific claims below are therefore [UNVERIFIED-in-original]); Schrage 1968
(JSTOR paywall — EOS Thm 4.25 states and proves the same result openly).

---

## 2. Per-paper notes (verified quotes; line numbers are into research/papers/<name>.txt)

### 2.1 LLM-serving metrics & SLO-aware scheduling

**DistServe — 2401.09670.txt**

- L119–121: "an effective LLM serving system should balance these needs and maximize
  per-GPU **goodput, defined as the maximum request rate that can be served adhering to
  the SLO attainment goal (say, 90%)** for each GPU provisioned".

- L144–148: "**A prefill step often takes much longer than a decoding step. When batched
  together, decoding steps in the batch are delayed by the prefill steps, significantly
  elongating their TPOT**; similarly, the inclusion of decoding steps contributes to a
  non-trivial increase in TTFT".

- L167–171: disaggregation = scale each phase independently, "may assign different
  numbers of GPUs and parallelism strategies to each phase depending on their respective
  latency requirements".

- TTFT maps to our TDR (ARR→P POST, L1/L4); TPOT maps to our pooled span/G (L5). Their
  resource-level fix is impossible for us (one fixed local computer, K fixed clouds) —
  but the *interference* fact is the wave-lockstep mechanism in miniature: on the shared
  local computer, P_PRE/P_POST legs ARE batched with D_PRE/D_POST legs of the wave train.

**PDAgg/TaiChi — 2508.01989.txt**

- L39–41: "**PD aggregation is optimal under tight TTFT and relaxed TPOT, while PD
  disaggregation excels under strict TPOT and relaxed TTFT. However, under balanced TTFT
  and TPOT SLOs, neither approach can deliver optimal goodput**."

- L55–58: "**latency shifting: by selectively reallocating GPU resources from requests
  that meet TTFT or TPOT SLOs to those at risk of violation**, TaiChi maximizes the
  number of SLO-satisfied requests" (mechanisms: flowing decode scheduling + length-aware
  prefill scheduling).

- This is the literature twin of our regime logic (compute_alpha, D-050/D-058/D-115) and
  of L5/L6/L12: below-SLO latency is unpriced, so shift delay onto SLO-met requests.
  TaiChi goes further: it shifts *within* a test (request-level), not just by regime.

**Sarathi-Serve — 2403.02310.txt**

- L210–215: "Chunked-prefills splits a prefill request into equal compute-sized chunks...
  **Stall-free scheduling allows new requests to join a running batch without pausing
  ongoing decodes. This involves constructing a batch by coalescing all the on-going
  decodes with one (or more) prefill chunks from new requests such that each batch
  reaches the pre-configured chunk size**."

- L216–221: "it **throttles the number of prefill tokens in each iteration**... This not
  only bounds the latency of each iteration, but also makes it **nearly independent of
  the total length of input prompts**."

- L188–190: prioritizing prefills hurts "TBT (time-between-tokens) tail latency whereas
  prioritizing decodes has the opposite effect."

- Translation to our physics: chunk prefill so each local P-leg is bounded and the decode
  wave train never waits on a monster P_PRE/P_PROC; coalesce P work *into* the gaps of the
  wave train (dead zone, L4) instead of between waves.

**Orca — osdi22-yu.txt**

- L39–41: "**iteration-level scheduling**... schedules execution at the granularity of
  iteration (instead of request) where the scheduler invokes the execution engine to run
  only a single iteration of the model on the batch."

- L130–133: "the proposed scheduling can issue a batch of requests where **each request
  has so far processed a different number of tokens**." Selective batching (L140–143)
  splits the batch only where attention shapes differ.

- Our D PRE already batches mixed-iteration requests across clouds (round r_i differs per
  member is legal only if predecessor rule holds — members at different rounds CAN merge;
  Orca certifies this as the default, not the exception. Cohort re-merge (SA07 #2) is an
  Orca-move: re-merge stragglers with fresher cohorts at different token counts.

**FastServe — 2305.05920.txt**

- L468–478: "**SRPT ... is a widely-used preemptive scheduling policy to minimize average
  latency. However, applying SRPT to LLM inference presents challenges ... the total
  number of iterations (i.e., the output sequence length) remains unknown ... SRPT cannot
  be directly employed for LLM inference**."

- L643–647: "Our solution: **skip-join MLFQ**... While the number of iterations remains
  unknown beforehand, the execution-time of each iteration is predictable."

- MLFQ = priority learned from observed service; "skip-join" lets short jobs skip the
  queue. Our `iter` counter (tokens produced so far, SA06 §4.1) is exactly the MLFQ aging
  signal; Lout ≥ iter+1 is a confirmed lower bound.

**Mooncake — mooncake-2407.00079.txt**

- L176–181: Conductor "select[s] a pair of prefill and decoding instances" per request;
  "2) **complete the prefill stage in chunks/layers and continuously stream the output
  KVCache to the corresponding decoding instance**; 3) load the KVCache and add the
  request to the continuous batching process at the decoding instance."

- Early state streaming is FORBIDDEN by our protocol (only the last prefill chunk queues
   DOWN, docs/ASSUMPTIONS.md §1.3) — Mooncake's lever does not transfer; what transfers is the
  pairing decision under link restrictions (their "selection policy is complex due to
  many restrictions", L182).

**Splitwise — splitwise-2311.18677.txt**

- L14–17: "each inference request undergoes two phases: a compute-intensive prompt
  computation phase and a memory-intensive token generation phase, each with distinct
  latency, throughput, memory, and power characteristics."

- L21–24: phase splitting onto separate pools "enables phase-specific resource
  management". Their runtime manages pool sizes as a ratio (prefill-heavy vs decode-heavy
  chunks; Ch.5 of the paper).

- For us the analog is *time*-allocation on the shared local computer: the split between
  P-leg work and D-leg work per frame is a ratio knob (our alpha / arbitration), not a
  placement knob.

**SCORPIO — 2505.23022.txt**

- L24–26: "**TTFT Guard, which employs least-deadline-first reordering and rejects
  unattainable requests, and a TPOT Guard, which utilizes a VBS-based admission control
  and a novel credit-based batching mechanism**."

- Least-deadline-first = serve the request closest to its SLO cliff first — the pooled
  analog is our `dist_headroom` gating (D-142). Rejection is impossible for us (L8
  no-abandon).

**Dai et al., Throughput-Optimal Scheduling for LLM Inference — 2504.07347.txt**

- L137–139: "**work-conserving scheduling remains throughput-optimal for single servers
  with multiple request classes**, DAG routing networks, and fork-join topologies."

- L139–140: "inspired by the Rybko–Stolyar network..., we construct an example showing
  that **work-conserving scheduling can fail under cyclic routing**."

- L141–144: "The maximal batch size constraint... **fundamentally alters the stability
  region from a scalar threshold to a two-dimensional convex hull**, and we demonstrate
  that even work-conserving algorithms can fail under certain configurations."

- L160: "Orca and SarathiServe are throughput-optimal... FasterTransformer and vanilla
  vLLM are not maximally stable."

- Direct support for our work-conserving doctrine (L10 race-the-end) AND two warnings:
  cyclic local↔cloud waves can starve (matches the D-140 P0 deadlock class); admission
  caps (V11_PPRECAP / max_inflight) can destabilize — every cap needs an event-backed
  wake path (L8).

**Clockwork — clockwork-2006.02464.txt**

- L702–708: workers must have no implicit performance side effects; "a predictable
  component either **delegates scheduling decisions that may impact performance to the
  centralized controller**, or otherwise makes schedules deterministic"; deviation from
  the instructed schedule "is treated as an error".

- Our transfer oracle (XDN completion times exactly predictable at issue time, D-004)
  is the same bet: predictability converts online scheduling into offline schedule
  construction inside a frame.

**AlpaServe — alpaserve-2302.11665.txt**

- L23–26: "Our work reveals a **fundamental trade-off between the overhead introduced by
  model parallelism and the opportunity to exploit statistical multiplexing** to reduce
  serving latency in the presence of bursty workloads."

- Analogy: batching across clouds (our cross-cloud D groups) is multiplexing; the
  per-task S and table non-linearity is the overhead — the same trade-off our
  MarginalBatch::precompute resolves with argmin caps.

### 2.2 Pipeline / lot streaming / overlap theory

**GPipe — 1811.06965.txt**

- L271–274: "partitioning introduces some idle time per accelerator, which we refer to
  as the **bubble overhead**. This bubble time is O((M+K−1)/(K−1) + K−1) amortized over
  the number of micro-steps M. In our experiments, we found the **bubble overhead to be
  negligible when M ≥ 4 × K**." (M = microbatches, K = pipeline stages.)

- The fill/drain cost of ANY lockstep pipeline is paid once per cohort; amortize by
  deepening the pipeline (more micro-steps) — for us, more decode rounds per cohort, or
  fewer independent cohorts (re-merge).

**Zero Bubble Pipeline Parallelism — 2401.10241.txt**

- L153–157: 1F1B "**initiates with a warm-up phase. In this phase, workers conduct
  varying numbers of forward passes, with each stage typically performing one more
  forward pass than its immediately subsequent stage. Following the warm-up phase, each
  worker transits to a steady state** where they alternately execute one forward pass
  and one backward pass."

- L893–896 (ZB-H1): "it adjusts the starting points of W depending on the number of
  warm-up microbatches. This ensures all workers maintain the same number of in-flight
  microbatches. As a result... **the bubble size is reduced to a third of 1F1B's size.
  This reduction is because B is initiated earlier across all workers..., and the
  tail-end bubbles are filled by the later-starting W passes**."

- L902–905 (ZB-H2): "**introduce more F passes during the warm-up phase to fill the
  bubble preceding the initial B. We also reorder the W passes at the tail, which changes
  the layout from trapezoid into parallelogram, eliminating all the bubbles**."

- L2763–2764: "an 1F1B iteration takes **(m + p − 1) ∗ (TF + TB + TW)** to complete,
  while a ZB iteration takes (m + p − 1) ∗ (TF + TB) + TW" — the canonical makespan of m
  microbatches through p stages = (m + p − 1) cycle-times; bubble fraction (p−1)/(m+p−1).

- Every concept has a 1:1 dictionary entry for us: microbatch = decode wave; stage =
  {local D-legs, link, cloud}; warm-up stagger = phase-offset cohorts (SA07 #1);
  "tail-end bubbles filled by later W passes" = straggler overlap at the finish line
  (SA07 #4, L2); "same number of in-flight microbatches" = our PPRE max_inflight depth.

**Botsali, Lot Streaming Techniques — botsali2019-lot-streaming.txt**

- L155–161: "for the 1st machine to have no idling restriction does not affect the
  makespan M... **A lot streaming problem and its inverse are equivalent**... for
  2-machine problem, no idling restriction does not affect the optimal makespan."

- L163–166: "If there are m machines in a model with no idling restriction and if
  variable sublots are allowed, it is possible to find optimal sublots by **dividing m
  machines into m−1 adjacent pairs and solving the 2-machine problem for each pair
  separately**."

- L205–214 (2/C/NI): "if there are n sublots and the ratio of the processing times of
  machine 1 to machine 2, that is p2/p1 = q, then for continuous version, the optimal
  solution for jth sublot can be found by solving equation (3): **L_j = q × L_{j−1} =
  q^{j−1} × L_1**."

- L218–226: "**In optimal solution of 2/C/NI problem, there should not be idle time on
  machine 2 after it starts processing**... consider U=7, n=3, p1=2 and p2=1. Then...
  the optimal lot sizes will be L1=4, L2=2 and L3=1... there is no idle time on machine
  2 after it starts processing."

- L186–195: dominance — variable sublots ≤ consistent ≤ equal makespan; intermittent
  idling ≤ no idling; continuous ≤ discrete. "The least restrictive model is m/V/II/CV."

- L249–252: "iterative solution procedures for 2/C/NI/DV and 2/C/NI/CV **with limited
  transporter capacity** are also given" (in Trietsch–Baker [3]).

- This is the exact mathematics of SA07 candidate #1: waves = sublots; n=1 (one giant
  wave) = today's lockstep; n>1 overlapped sublots cut the additive fill term. Sizing:
  keep the SLOWER stage non-idle; if stage rates differ by q, sublots follow a geometric
  progression with ratio q (decreasing if machine 2 is faster — verified direction by
  the U=7 example; increasing if slower). Equal waves are optimal only when q≈1.

**Ageev, 2-machine flow shop with exact delays — 1711.00081.txt**

- L5–11: "Leung et al. (2007) established that the problem is strongly NP-hard when the
  delays may have at most two different values. We... prove that the existence of a
  **(1.25 − ε)-approximation implies P=NP** and develop a 2-approximation algorithm."

- Our D-cycle (local leg → link → cloud → link → local leg with exact FIFO timing) sits
  in this family: even the 2-machine, 2-delay-value special case resists approximation —
  certifies heuristic control (not optimality pursuit) for wave interleaving.

**El Idrissi, P,S1 thesis — elidrissi2020-single-server-thesis.txt**

- L1133–1165 (ch.1 survey, complexity): "For the case of two machines, Kravchenko and
  Werner presented complexity results... They showed that the problems
  **P2,S1|pj,sj=1|Cmax and P2,S1|pj,sj=s|IT are NP-hard**" and (Brucker et al., Hall et
  al.) the long list incl. **P2,S1|pj,sj=1|Cmax, P2,S1|pj,sj=s|ΣCj, P2,S1|pj,sj=1|Lmax,
  P2,S1|pj,sj=1|ΣwjCj, P2,S1|pj,sj=1|ΣUj** — all NP-hard; with unit p_j and s_j still

  **ΣwjUj, ΣTj NP-hard**; EDD solves P,S1|pj=1,sj|Lmax in O(n log n).

- L977–981 (contributions): for **P,S1|pj,sj|Cmax** "two greedy heuristics with a
  complexity of O(n²)... **to minimize respectively the server waiting time and the
  machine idle time**", plus VNS; MIP exact only "up to 30 jobs".

- The single-server model IS our local computer (server setups = D PRE/D POST tasks)
  feeding K clouds (parallel machines): exact optimization is hopeless (NP-hard even with
  unit setups, 2 machines); the field's answer is exactly our architecture — greedy
  arbitration between the two competing idle accounts (server-wait vs machine-idle) +
  local search. Also: scheduling with **loading/unloading operations** and **multiple
  servers** flagged as under-studied (L~9735+).

### 2.3 SRPT / mean-flow / non-clairvoyance

**Elements of Scheduling ch.4 — eos-ch04-weighted-completion.txt**

- L1458–1461: "The natural rule... **Shortest Remaining Processing Time (SRPT): at each
  moment in time, one should always be processing a job that could be completed
  earliest.**"

- L1463–1466: "**Theorem 4.25. Any SRPT schedule is optimal for the problem

  1|r_j,pmtn|ΣC_j.** An important property of the SRPT rule is that it is an **online**

  algorithm... need only know of the existence of a job and its parameters at the moment
  in time that it is released."

- L1499–1500: "**Theorem 4.26. Sequencing jobs in order of nondecreasing completion
  times in the SRPT schedule is a 2-approximation algorithm for the problem

  1|r_j|ΣC_j**" (non-preemptive), extendable online via a **shadow SRPT schedule** that

  only fixes the FIFO queue order (L1501–1505).

- L711–715: with weights, 1|r_j,pmtn|ΣwjCj is strongly NP-hard (Smith's ratio rule
  breaks under release dates).

- This is the theory behind SA06's value keys: for MEAN flow (our pooled tdr/tpot, L5)
  the optimal signal is *remaining work ascending*. The shadow-schedule construction is
  directly implementable: run SRPT on predicted Lout offline-in-the-head, sequence
  non-preemptive tasks in that completion order — a certified 2-approx (for the pure
  mean-flow objective on one machine; our real objective mixes E, so this is directional).

**Rybko–Stolyar / cyclic warning** — covered under Dai et al. above (their §5). The
original 1992 paper is not in the library; the constructed instability example is
verified in 2504.07347.txt L139–140 and L165.

### 2.4 Contest-specific writeups

Searched ("ICPC Online Challenge powered by Huawei" writeup/editorial/solution, plus
past editions). No independent algorithmic writeups exist publicly beyond: (a) the CF
announcement/rules blog 155646 and results blogs (112838, 148279/148541), (b) winner
lists — all already captured by SA02 (`research/scoring/SA02_official_artifacts_web.md`,
artifacts incl. api_standings_2251.json and the Winter-2025 2177 standings). Comments
under results blogs are the only place top-solution hints might appear; they were
403-walled for SA02's fetchers ([UNVERIFIED] — retry from a logged-in session). Huawei's
ICPC challenge page is marketing-level. Nothing to download.

---

## 3. Mapping table: paper → physics law → scheduler change

| Paper (file)                        | Physics law                                   | Suggested change (SA07 candidate / SA06 key)                                                                                                                                                                                                                                                                     |
| ----------------------------------- | --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Zero Bubble (2401.10241)            | L14 (E-gap = pipelining gap), L2 (tail)       | Wave pipelining (#1): half-wave phase-offset warm-up stagger; steady-state 1F1B on local (alternate D_PRE of wave i+1 with D_POST of wave i); fixed in-flight depth per cohort; fill drain-tail with re-ordered light tasks (straggler overlap #4)                                                               |
| GPipe (1811.06965)                  | L14, L13                                      | Cohort re-merge (#2): each fragmented cohort pays its own (m+p−1) fill/drain; M ≥ 4×K rule → want wave-count ≥ ~4× effective stage count OR fewer cohorts; x9's 251+1751 split is two trapezoids that re-merge into one                                                                                          |
| Botsali lot streaming (botsali2019) | L13 (binding resource), L14                   | Wave sizing: keep the SLOWER of {local, cloud+link} non-idle; geometric sublot ratio q = p_slow/p_fast (equal waves only if q≈1); m-machine case solved pairwise — our 3-stage loop = 2 adjacent 2-machine problems                                                                                              |
| Sarathi-Serve (2403.02310)          | L4 (dead zone), L9 (prefill delay never pays) | Chunk P work to bounded per-frame compute; coalesce P-legs into wave gaps (never pause the decode train); "iteration time nearly independent of prompt length" = bound the local-leg stall any request can inflict on a wave                                                                                     |
| Orca (osdi22-yu)                    | L5 (pooled means), L3                         | Cohort re-merge across token counts: members at different rounds legally share a D PRE group; iteration-granularity (not request-granularity) admission is the default posture                                                                                                                                   |
| DistServe (2401.09670)              | L5, L6                                        | Nothing architectural (resources fixed), but quantifies the P-vs-D interference tax that our P_POST arbitration prices; supports disaggregation-in-time (protect decode waves when tp window tight)                                                                                                              |
| PDAgg/TaiChi (2508.01989)           | L6, L12, L5                                   | Regime proof: aggregation-optimal iff TTFT-tight; disaggregation iff TPOT-tight; hybrid latency-shifting when balanced — request-level reallocation from SLO-met to at-risk (per-request cliff_feasible gating already does this; extend to *intra-test* shifting when both axes priced — only 5/95 cases today) |
| Dai et al. (2504.07347)             | L8 (completion hard), L10 (race)              | Work-conserving stays optimal in acyclic topologies — keep race-the-end; cyclic-wave starvation is real: every gate/cap must be event-backed (D-140 P0 fix generalized); batch-size caps can destabilize — audit V11_PPRECAP wake paths                                                                          |
| El Idrissi P,S1 thesis              | L13, L8                                       | Certification: exact P,S1 optimization is NP-hard even at 2 machines/unit setups — greedy arbitration between server-wait and machine-idle is the state of the art, our P_POST arbitration is the right family; EDD wins Lmax with unit p_j (cliff cases: earliest-SLO-cliff-first ordering)                     |
| EOS ch.4 (SRPT)                     | L2, L10, L11                                  | Shadow-SRPT sequencing of admission (2-approx certified for mean flow): run SRPT on predict_lout, order non-preemptive issue by SRPT completion order; contrasts with SA06's throughput keys — see contradictions                                                                                                |
| FastServe (2305.05920)              | L11 (hidden Lout), L2                         | Skip-join MLFQ using observed `iter` as aging signal (no predict needed); confirmed-Lout≥iter+1 jobs are known-long → start early if they'll own the tail (L2), deprioritize for mean-flow (L5) — the tension is real and must be resolved per regime                                                            |
| SCORPIO (2505.23022)                | L6, L12                                       | Least-deadline-first among cliff-proximate requests (= dist_headroom); credit-based batching: admit to a wave only requests whose remaining token budget justifies the wave's cost                                                                                                                               |
| Ageev exact delays (1711.00081)     | L13                                           | No 1.25-approx even in 2-machine/2-value case → do not chase optimal interleaving offline; hill-climb (regret/KKT-style marginal comparisons, our T-theorems) is the right ceiling                                                                                                                               |
| Mooncake (2407.00079)               | L4                                            | Pairing policy under link restrictions (Conductor): choose cloud per request by joint (prefill-instance, decode-instance) feasibility — our cloud choice at P_PRE is the same decision one-shot; early KV streaming is protocol-illegal for us                                                                   |
| Clockwork (2006.02464)              | L8                                            | Full predictability → centralized offline schedule per frame; deviations = errors. Our oracle already predicts XDN exactly; extend prediction to D-cycle completion times to make wave stagger schedulable in advance                                                                                            |
| AlpaServe (2302.11665)              | L13                                           | Multiplexing-vs-overhead trade-off formalized — same shape as our batch-cap argmin (S amortization vs small-m table penalty)                                                                                                                                                                                     |
| CrossPipe (2507.00217)              | L13, L14                                      | Bubble ratio = fraction of idle per device; finer task granularity (F/B/D/W split) unlocks better schedules — split local D-legs (D_PRE vs D_POST) as separately placeable tasks in the wave train                                                                                                               |
| Splitwise (2311.18677)              | L6                                            | Ratio control between phase pools = our alpha arbitration; phase-specific sizing of chunks                                                                                                                                                                                                                       |

---

## 4. Application notes — TOP 5

### AN-1. Zero Bubble Pipeline Parallelism (2401.10241) vs the wave-lockstep bottleneck

What it proves: for a p-stage pipeline of m microbatches under synchronous semantics,
1F1B completes in (m+p−1)(TF+TB+TW) (L2763–2764) — i.e., bubble fraction (p−1)/(m+p−1);
bubbles are NOT inevitable: splitting the backward pass and re-ordering the tail
"warm-up more F passes; reorder W at the tail; trapezoid → parallelogram" removes them
(L902–905), and a third of the bubble vanishes just by keeping a *uniform number of
in-flight microbatches* and letting "tail-end bubbles be filled by the later-starting
passes" (L893–896).
Assumptions that differ: (a) their stages are homogeneous workers with symmetric F/B
passes; our loop is heterogeneous (1 local, K clouds, 1 FIFO link each way) and the link
is a *shared serialized* stage; (b) their schedule is static and precomputed — ours must
be event-driven with hidden Lout; (c) no batch-size-dependent task times — our D-leg
costs depend on group size m, so microbatch width is a variable, not a constant.
Scheduler change (maps to SA07 #1 wave pipelining + #4 straggler overlap; attacks L14's
"local occupancy 27% while E = 2.3× floor"): run W=2 staggered cohorts at half-wave
phase offset (the 1F1B warm-up: "each stage typically performing one more forward pass
than its immediately subsequent stage", L153–155); at steady state the local computer
alternates D_PRE(cohort A) and D_POST(cohort B) — 1F1B's alternation is exactly the
ping-pong/double-buffer staging; keep in-flight depth per cohort FIXED (uniform, as
ZB-H1); at the drain, fill tail bubbles with the other cohort's re-ordered light tasks
rather than idling into the finish line (L2 tail ownership). Falsification guard: if
E does not drop on m19b/m19c/m5_order (SA07's probe), the dependency-chain model is
wrong.

### AN-2. Lot streaming (Botsali 2019, after Trietsch–Baker) — how many waves, what sizes

What it proves (2-machine, consistent sublots, no idling — 2/C/NI): with n sublots and
stage-time ratio q = p2/p1, the optimal sublot sizes are geometric (L214):

```text
L_j = q^{j−1}·L_1
```

- `L_j` — j-th sublot size
- `q` — stage-time ratio p2/p1
- `L_1` — first sublot size

and the optimum is exactly the sizing under which "there should not be idle time
on machine 2 after it starts processing" (L218–219) — i.e., KEEP THE SLOWER STAGE
SATURATED; no-idling constraints cost nothing on the first machine (L155–161); with
variable sublots, m machines decompose into m−1 adjacent 2-machine problems (L163–166);
equal sublots are optimal only in the limit q=1 (dominance, L186–195).
Assumptions that differ: no transport time between stages inside a sublot start (the
link adds a per-sublot serial latency — Trietsch–Baker's "limited transporter capacity"
variant, cited L249–252, is the right extension; original [UNVERIFIED-in-original]);
linear processing times (our table is sub-linear in m, so splitting has a *computing*
penalty, not just overhead); deterministic, known lot size U (our U = cohort tokens,
partially hidden).
Scheduler change (SA07 #1/#2): treat the cohort as a lot; waves as sublots. n=1 is
today's lockstep. Choose W and sizes so the binding stage (L13 `bind` column) never
idles after start: if cloud+link is slower (q>1) use INCREASING wave sizes (verified
direction from the U=7, p1=2, p2=1 → L=(4,2,1) example: faster second stage ⇒
decreasing); if local is the slow stage, decreasing sizes. Equal halves (the SA07 probe)
are the right first cut only when rates are near-equal. The m-machine pairwise
decomposition says our 3-stage loop (local → link → cloud) can be tuned as two 2-machine
problems sharing the link term.

### AN-3. Sarathi-Serve chunked prefill + stall-free batching (2403.02310)

What it proves (empirically, at scale): coalescing all ongoing decodes with bounded
prefill chunks "such that each batch reaches the pre-configured chunk size" (L212–215)
lets new requests join without pausing decodes, bounding each iteration's latency and
making it "nearly independent of the total length of input prompts" (L216–221);
prioritizing prefills vs decodes trades TTFT tail against TBT tail (L188–190).
Assumptions that differ: their prefill and decode share one GPU batch slot per
iteration; ours share ONE local computer across P_PRE/P_POST and D_PRE/D_POST tasks with
no preemption once a task starts; their latency metric is per-request percentile, ours
is pooled mean with a makespan term (L2/L5).
Scheduler change: pre-register a per-frame LOCAL BUDGET for P-leg work (chunk prefill at
P_PRE so that S + ppre(chunk) ≤ slack of the current wave train gap — the L4 dead-zone
size); admit P_POST only into gaps (the D-140 run_ppost arbitration already moves this
way; add the chunk-size lever at P_PRE, which we control via num_layers splits; note the
S<1.0 && num_layers>8 gate is load-bearing, D-150). Expected effect: the wave train's
cycle time stops depending on the largest Lin in flight — the m5_order / living_cliffs
class where fat prefills stall thin decoders.

### AN-4. Dai et al. 2504.07347 — work conservation, and the two ways it dies

What it proves: work-conserving scheduling is throughput-optimal for multi-class single
servers, DAG and fork-join routing (L137–139, L164–166) — but NOT under cyclic routing
(Rybko–Stolyar-style example, L139–140), and the maximal-batch-size constraint morphs
the stability region into a two-dimensional convex hull where "even work-conserving
algorithms can fail" (L141–144).
Assumptions that differ: queueing-station setting (infinite queues, stationary
arrivals); we have finite work, no abandonment (L8) and protocol-atomic tasks.
Scheduler change: two audits, one new design rule. (1) Cyclic audit: local↔cloud waves
form a 2-station cycle through FIFO buffers — every gate on issuing D_PRE/P_PRE must be
provably wakeable by an in-flight event (this is the D-140 P0 fix stated as a standing
invariant; re-run the gate inventory for any new wave-pipelining code path, since
staggering multiplies the cyclic dependencies W-fold). (2) Cap audit: V11_PPRECAP /
max_inflight / max_inflight_eff are maximal-in-flight constraints — the paper's
two-dimensional region says such caps can starve even work-conserving schedules; verify
each cap's release condition is an event, not a threshold that can sit permanently
unmet (the D-144 #15 lesson). Design rule: prefer admission rules that defer *which*
task, never rules that defer *whether any* task.

### AN-5. SRPT + skip-join MLFQ (EOS ch.4 + FastServe) — the finish line and hidden Lout

What they prove: Thm 4.25 — SRPT is optimal for 1|r_j,pmtn|ΣC_j and is online

(eos-ch04 L1463–1466); Thm 4.26 — sequencing by SRPT completion order is a 2-approx
for the NON-preemptive problem, even online via a shadow schedule (L1499–1505);
FastServe: with unknown output length "SRPT cannot be directly employed" (2305.05920
L468–478) — use MLFQ on observed service ("skip-join MLFQ", L643–647, L149).
Assumptions that differ: single machine, preemption, objective = unweighted mean flow;
our flow-time axes are pooled (L5), SLO-clamped (L6, L12), and coupled to a makespan
term (L2): the LAST token sets E for everyone, so pure SRPT (finish short jobs first)
can be exactly wrong at the finish line — the request that will own the tail should be
STARTED earliest (longest-remaining-first at the end), which is the ZB tail-fill again.
Scheduler change: (a) adopt FastServe's aging signal as the admission tie-break:
`iter` (confirmed tokens) is free; requests with high iter have confirmed-large Lout →
they are tail-owner candidates → prioritize their wave membership at the drain (L2),
deprioritize them for mean-flow when spans are priced (L5) — the sign flips by regime,
keyed on dScore/dE vs dScore/dspan (scorecard columns). (b) The shadow-schedule trick:
compute SRPT order on predict_lout inside the frame; use it ONLY to order issue of
non-preemptive tasks (certified 2-approx for the mean-flow part). This attacks SA06's
L11 keys: SRPT orders by remaining work; SA06's link-bound key Lout/(2(Lin+Lout)) orders
by tokens-per-byte — they agree only when Lin is uncorrelated with Lout; where Lin↔Lout
correlate positively, token-dense requests are also long, and the two orderings
CONFLICT (see §5).

---

## 5. Contradictions with our current approach

1. **SRPT vs throughput keys (SA06 L11).** For mean flow time, optimal order is
   remaining-work ASCENDING (EOS Thm 4.25); the link-bound key Lout/(2(Lin+Lout))
   prefers token-dense = large-Lout requests — the opposite ordering whenever
   Lin,Lout co-vary. The keys are right for the E/throughput term (L2 makespan, bytes
   on the link), wrong for the pooled-span term. Both live in our score simultaneously;
   only 5/95 cases price both axes (SA07 §2.1), so today the conflict is mostly latent.
   Resolution per literature: regime-switch the ordering key on which gradient is live
   (L12), not a static key.

2. **Deliberate local idling to sync waves is provably useless in the 2-machine model**
   (Botsali L155–161: no-idling costs nothing on the upstream machine; the inverse
   argument extends it). Any wave-pipelining implementation that PARKS the local
   computer to align phases contradicts the theory: stagger by ISSUE ORDER, not by
   idling. (Consistent with L9/L10 — parking lost everywhere it was tried.)

3. **Equal-cohort splitting is not optimal when stage rates differ** (geometric sublot
   optimality, AN-2). SA07's 2-fixed-cohort falsification probe should be read as a
   LOWER bound on the gain; if it passes, the sizing refinement (q-ratio waves) is the
   follow-up, not equal waves.

4. **Cyclic-routing instability (Dai et al.)** — a formal counterexample family against
   "work-conserving is enough" in exactly our topology class. Our D-140 P0 deadlock and
   the D-144 #15 PPRE depth regression are instances. Wave pipelining multiplies cyclic
   dependencies; every new gate must carry the event-backed-wake proof (L8).

5. **Percentile machinery does not transfer.** DistServe/SCORPIO goodput is defined on
   per-request SLO attainment rates (2401.09670 L119–121); our score pools means (L5)
   with cliff binaries — LDF/credit batching import only as cliff-proximity heuristics,
   not as attainment maximizers. Over-applying them would concentrate damage wrongly
   (dead-zone/low-gap requests should absorb delay, L5).

6. **Mooncake's headline lever is protocol-illegal here** (only the last prefill chunk
   queues DOWN; no early state streaming) — do not chase KV-pretransfer-style overlap
   of prefill and decode for the SAME request.

---

## 6. Open questions recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026)

1. **Lin↔Lout correlation on CF hidden tests** (SA06 §4.2 open) decided SRPT-vs-keys
   (§5.1) and the value of skip-join MLFQ. FastServe's framing ("semi
   information-agnostic": iteration time known, count unknown) was the right model; no
   paper resolved the correlation question.

2. **Transporter-capacity lot streaming**: Trietsch–Baker's limited-transporter
   procedures (cited in Botsali L249–252) matched our FIFO link exactly, but the original
   was paywalled [UNVERIFIED-in-original] — recorded at close as worth a library request if wave pipelining
   had survived the m19b probe; the discrete-capacity case (link carries one transfer at a
   time) changed the optimal n.

3. **Wave count vs table penalty**: lot-streaming theory assumed linear sublot costs;
   our sub-linear decode table made small waves cost compute (SA07 #1 risk note).
   No downloaded paper handled sublot-size-dependent processing rates — the W* optimum
   would have had to be found empirically per test family (MarginalBatch::precompute was the natural
   home for a W-way split evaluation).

4. **In-flight-depth unification**: ZB-H1's uniform in-flight count (L893) vs our
   per-request PPRE depth caps — whether the cap should have been per-COHORT rather than per-request
   when waves were staggered was untested at program close; it interacted with contradiction #4.

5. **CF blog comments** (approach hints from past-challenge winners) remained unfetched at program close —
   403-walled anonymously (SA02); the recorded follow-up was to retry from a logged-in session.

6. **P,S1 with loading/unloading** (both setup AND teardown on the server — precisely
   D_PRE/D_POST on local): flagged under-studied in the El Idrissi survey; no canonical
   algorithm to import — our two-idle-account greedy arbitration was plausibly
   state-of-the-art by default.

---

## 7. Reproducibility

Downloads (2026-08-23): arxiv.org/pdf/{2311.18677, 2407.00079, 2302.11665, 2006.02464};
uphf.hal.science/tel-03429508v1/document; dergipark.org.tr/tr/download/article-file/904347;
elementsofscheduling.nl/PDF/Ch04.pdf. All extracted with pdftotext; quotes verified
against the .txt files at the cited lines. manifest.json updated (33 entries, JSON
validated). Sources for the searches: Codeforces/ICPCNews blogs (via SA02 artifacts),
arXiv, HAL, USENIX/ACM listings.
