# SYNTHESIS — Intentional Delay (Holding Ready Decode Batches) for ICPC 2251 (archived post-contest)

# Research-only artifact. Every cited claim is verifiable in a file in research/papers/ (file + line numbers given).

# Uncited analysis is labeled [OWN] (Ntokozo Dlamini's analysis with assistant drafting support, not from any paper).

# Date: 2026-08-14. Companion deliverable: the final chat message (sections A-D).

# Archive note: SA01–SA10 program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Retained as written; proposal language below was pre-contest and is closed.

---

## 0. The question

V3 (D-009) batched ALL ready decode members per frame and issued immediately (work-conserving).
Proposal studied pre-contest: sometimes HOLD a ready D_PRE batch for later members (e.g., 1 member ready now,

a 4th joining soon), paying a delay to amortize S. Studied sketch: if |ready| >= 2 and

decode_pre(2) < 1.5*decode_pre(1), batch immediately; else wait up to delta = S/2.

Protocol facts this analysis must respect (docs/ASSUMPTIONS.md §1, tools/interactor.py):

- No self-wake timer; the solver acts only at judge frames. Hold is safe only if a completion
  frame is guaranteed (constraint #1). [interactor.py stuck() L364-380, next_frame_time() L333-341]

- Score formula [interactor.py L382-397]:

```text
Score = 1000*(w_tp*C_tp + w_c*C_w)
C_w = max(0, 1 - dist/dist_base)
dist = sqrt(e_tdr^2 + e_tpot^2)
e_tdr = max(0,(mu_tdr-SLO1)/SLO1)
e_tpot = max(0,(mu_tpot-SLO2)/SLO2)
mu_tpot = sum_span/total_gaps
mu_tdr = sum_tdr/R
tp = tokens/(last_finish - first_arrival)
```

- `Score` — final 0–1000
- `w_tp` — throughput weight
- `w_c` — waiting weight
- `C_tp` — throughput component
- `C_w` — waiting component
- `dist` — Euclidean excess
- `dist_base` — waiting budget, 0 gives binary
- `e_tdr` — relative TDR excess
- `e_tpot` — relative TPOT excess
- `mu_tdr` — mean TDR
- `mu_tpot` — mean TPOT
- `tp` — output rate

  => BOTH means and the makespan feed the score; dist_base=0 makes C_w binary.

- D-009: groups are formed WITHIN one cloud; DOWN queued per D_PROC task. D-004: transfer oracle makes
  XDN completion times exactly predictable at issue time.

---

## 1. New downloads (this task)

| File                                     | Source                                                                             | Why                                                                                               |
| ---------------------------------------- | ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| clipper-nsdi17-crankshaw.pdf/.txt        | usenix.org (NSDI'17)                                                               | Production dynamic-batching with max delay under latency SLO (Q5)                                 |
| skirental-brown-slides.pdf/.txt          | cs.brown.edu (M. Claire talk)                                                      | Ski-rental competitive analysis: deterministic 2-1/b, randomized e/(e-1) (Q2)                     |
| rentorbuy-siam.pdf/.txt                  | siam.org (Kodialam)                                                                | Rent-or-buy with time-varying demand, CR 2 deterministic / 1.582 randomized (Q2)                  |
| springer-bulk-queues-thresholds.pdf/.txt | link.springer.com (Germs & van Foreest, OR Spectrum 35:563-583, 2013, open access) | Primary-source digest of Deb-Serfozo threshold optimality for batch service with setup costs (Q1) |
| triton-batcher-docs.md                   | docs.nvidia.com (Triton user guide)                                                | Triton dynamic batcher: preferred_batch_size, max_queue_delay_microseconds (Q5)                   |

### 1.1 Germs & van Foreest 2013 — "Analysis of finite-buffer state-dependent bulk queues" (OR Spectrum 35:563-583)

- L181-184: "A situation in which it is reasonable to keep the server idle while there are customer
  waiting in queue is when the aim is to minimize average waiting time of customers in the system. In
  fact, Aalto (2000) and Deb and Serfozo (1973) prove that it is optimal to start serving customers only
  when the number of customers in queue exceeds some threshold a."

- L236-246: "Deb and Serfozo (1973) show that the minimal batch service policy is optimal for a batch
  service queue where costs are incurred for serving the customers and for holding them in the system.
  Aalto (2000) generalizes the result to queueing systems with compound Poisson arrivals. Note that if
  the cost of serving is set to zero, minimizing the expected averaged cost is equivalent to minimizing
  the average waiting time."

- L1189-1191 (setup-cost model): "the holding cost is ch per customer in the queue per unit time, a
  service cost ck + cs j is incurred at each service epoch when j is the batch service size"

- L1277-1282 (threshold vs setup tradeoff): "the optimal minimal batch service threshold value a is 6
  for the complete rejection policy, 7 for the partial acceptance and complete acceptance policies.
  Thus, the threshold value increases when the acceptance policy is less 'strict'. This is as expected,
  since the policy makes a trade-off between set-up costs, i.e., a cost ck is incurred for each service
  interval, and the rejection costs."

- L172 (own check): batches cannot be joined once service starts: "any arrival during a service joins
  the queue... the group cannot join the batch already undergoing service." [also L186-188]
=> Q1 anchor: with setup cost + holding cost, the OPTIMAL policy is a queue-length threshold (accumulate
then serve-all). Threshold rises with setup cost per service interval. Batch closing = dispatch time.

### 1.2 Clipper (Crankshaw et al., NSDI'17) — "Clipper: A Low-Latency Online Prediction Serving System"

- L843-848: "Clipper imposes a maximum batch size to ensure that latency objectives are met and avoid
  excessively delaying the first queries in the batch."

- L917-927: "Under moderate or bursty loads, the batching queue may contain less queries than the
  maximum batch size when the next batch is ready to be dispatched. For some models, briefly delaying
  the dispatch to allow more queries to arrive can significantly improve throughput under bursty loads.
  Similar to the motivation for Nagle's algorithm [44], the gain in efficiency is a result of the ratio
  of the fixed cost for sending a batch to the variable cost of increasing the size of a batch."

- L929-938: "Delayed batching provides no increase in throughput for the Spark SVM because Spark is
  already relatively efficient at processing small batch sizes... In contrast, the Scikit-Learn SVM has
  a high fixed cost for processing a batch... a 2ms batch delay provides a 3.3x improvement in
  throughput and allows the Scikit-Learn model container to keep up with the throughput demand while
  remaining well below the 10-20ms latency objectives"
=> Q5 anchor: production systems DO use bounded delay (Nagle-motivated), the gain is fixed-cost /
variable-cost ratio, the delay is capped by the latency SLO, and the benefit is model-dependent
(small fixed cost => delayed batching buys nothing).

### 1.3 Ski rental (Brown slides; Kodialam SIAM)

- skirental-brown-slides.txt L2-6: rent 1/day, buy b; offline cost min{D, b}.
- L9-14: deterministic algorithm: rent t-1 days, buy on day t; cost t if D<t, t-1+b if D>=t.
- L38-39: "which is minimized for t = b, yielding optimal deterministic competitive ratio of 2 - 1/b."
- L318-340: randomized ratio e/(e-1) ~ 1.58.
- rentorbuy-siam.txt L5-13: "We develop deterministic and probabilistic online algorithms for rent/buy
  decision problems with time-varying demand. We show that these algorithms have competitive ratios of
  2 and 1.582 respectively. We also further establish the optimality of these algorithms."

- rentorbuy-siam.txt L25-27: "Applications of the ski-rental problem include scheduling tasks on a
  computer (the system must decide if it should do a task immediately by paying a high cost in
  processing time, or if it should wait and pay a 'rental' cost for keeping the task in waiting)"
=> Q2 anchor: the online delay-vs-issue decision is the ski-rental structure. Optimal deterministic
rule: accumulate delay cost until it equals the one-time cost of the alternative action (t=b); any
fixed threshold that is not at the break-even point is strictly worse in the worst case; the optimal
deterministic CR is 2-1/b; randomized can reach e/(e-1).

### 1.4 Triton batcher docs (triton-batcher-docs.md)

- "By default the dynamic batcher will create batches as large as possible up to the maximum batch size
  and will not delay when forming batches."

- "Set batch delay to a non-zero value. Try increasing delay values until the latency budget is
  exceeded to see the impact on throughput." (Recommended Configuration Process)

- "The max_queue_delay_microseconds property... will delay sending the batch as long as no request is
  delayed longer than the configured max_queue_delay_microseconds value. If a new request arrives
  during this delay and allows the dynamic batcher to form a batch of a maximum or preferred batch
  size, then that batch is sent immediately... If the delay expires the dynamic batcher sends the batch
  as is."

- "Preferred batch sizes should not be used for most models."
=> Q5 anchor: Triton's delay policy = fixed per-request delay cap + dispatch-early-if-target-size-reached.
Exactly the human's "wait up to delta" sketch, with the extra rule: if the target size becomes reachable
early, stop waiting. No adaptive/threshold logic in production; just a cap.

---

## 2. Library reuse (already downloaded, quotes re-verified today)

### 2.1 sasikala-bulk-service-survey.txt (Sasikala & Indhira 2016, "Bulk service queueing models - a survey")

- L200-203: "In N-policy queueing system, the server begins service from idle period only when the
  queue length reaches at least N (N >= 1)." => the N-policy = accumulate-to-N threshold, the classical
  instantiation of the Q1 threshold policy (Yadin-Naor line, cited as [64]-[70] in the survey).

- L109-115: the (a, d, b) accessible-batch rule: idle below a; serve all between a and d, late arrivals
  admitted until d; above d, closed batch of b. => thresholds parameterize the whole family.

### 2.2 batch-service-map-vacation.txt (arXiv:2208.13043)

- L26-31: "(a,b) rule, the server determines the thresholds a (minimum) and b (maximum), respectively,
  to serve the customers. Once service is completed, if the customers waiting in queue are between a
  and b, all the customers are serviced... if the customers waiting in queue are < a, then it falls
  into a dormant state until the queue size a is reached." + state-dependent batch service rates
  (L8-13: "service rate of the batch is dependent on the size of the batch").

### 2.3 2504.07347.txt (Dai et al., "Throughput-Optimal Scheduling Algorithms for LLM Inference", 2026)

- L23-25: "prove that a large class of 'work-conserving' scheduling algorithms achieve maximum
  throughput for both individual requests and AI-agent workloads"

- L940-944: "Our main result is that a class of work-conserving scheduling algorithms can (almost)
  achieve the system's maximal throughput rate."

- L1013-1017: "then the DTMC {X(n), n in N} is positive recurrent (i.e., the system is stable) under any
  work-conserving K-FCFS algorithm. Theorem 2 provides a complete characterization... the critical
  threshold is lambda(mp+md) = bmax/tbmax"
=> Q2/Q5 anchor: deliberate idle (holding a batch) can NEVER beat the throughput of a work-conserving
policy on the stability metric; the only thing a hold can buy is SLO-side (mean latency) improvement,
and it risks throughput.

### 2.4 goldwasser-soda99-patience.txt (Goldwasser, SODA'99)

- L64: "show that for any kappa > 0 a simple greedy algorithm is (2 + 1/kappa)-competitive even when
  arbitrary job lengths are allowed." (patience = slack >= kappa * length)
=> Q2/Q3 anchor: greedy with EDD is 2-competitive; lookahead (patience parameter) improves the bound
linearly in 1/kappa. Already synthesized in research/notes/305.

### 2.5 1606.09395.txt (Bohm et al., "Online Packet Scheduling with Bounded Delay and Lookahead")

- L110-112: "1-lookahead corresponds to the situation in which a router can see the packets that are
  just arriving to the buffer and that will be available for transmission in the next time slot."

- L116-119: "we present an online algorithm for this problem with competitive ratio of 1/2(sqrt13 - 1)
  ~ 1.303. Then, in Section 5, we give a lower bound of 1/4(1 + sqrt17) ~ 1.281 on the competitive
  ratio of algorithms with 1-lookahead"
=> Q3 anchor: knowing one time-step of future arrivals shrinks the worst-case gap (from 2 to ~1.3 in
this deadline model). Our pipeline oracle (D-004) gives us lookahead for in-flight members with
CERTAINTY — the strongest possible form of the 1-lookahead assumption.

### 2.6 osdi22-yu.txt (Orca, SOSP'22)

- L583-590: "we propose to schedule executions at the granularity of iteration... Since the scheduler
  receives a return on every iteration, it can detect the completion of a request and immediately
  return its generated tokens to the client... With iteration-level scheduling, the scheduler has a
  full control on how many and which requests are processed in each iteration."
=> Q5 anchor: production LLM serving batches per-iteration what is ready; Orca's win is iteration-level
REACTIVE batching, not waiting-for-members. (Synthesis note research/notes/407.)

### 2.7 2403.02310.txt (Sarathi-Serve, EuroSys'24)

- L18-22: "SarathiServe introduces chunked-prefills which splits a prefill request into near equal
  sized chunks and creates stall-free schedules that adds new requests in a batch without pausing
  ongoing decodes. Stall-free scheduling unlocks the opportunity to improve throughput with large batch
  sizes while minimizing the effect of batching on latency."
=> Q5 anchor: the performance gain is from NOT stalling (no intentional delay), i.e., batch-what-is-
ready. (Synthesis note research/notes/401.)

### 2.8 2508.01002.txt (Bari, Hegde, de Veciana — "Optimal Scheduling Algorithms for LLM Inference")

- Abstract: "SLAI uses real-time measurements to prioritize decode requests that are close to missing
  their TBT deadlines and reorders prefill requests based on known prompt lengths to further reduce the
  Time To First Token (TTFT) delays."
=> Q4 anchor: SLO-aware practice = urgency (deadline proximity) drives priority; the analog in our
mean-score setting: SLO HEADROOM (SLO1 - mu_tdr, SLO2 - mu_tpot) drives whether delay is affordable.

### 2.9 2605.02329.txt (Wang — "Taming Request Imbalance: SLO-Aware Scheduling for Disaggregated LLM")

- Abstract: "In production environments, large language model (LLM) serving is required to meet
  stringent service-level objectives (SLOs) amid highly variable request patterns... Current systems,
  which adopt first-come-first-served (FCFS) scheduling for prefill and continuous batching for decode,
  lack the ability to adapt to this imbalance, resulting in compromised SLO attainment."
=> Q4 anchor: adversarial/variable arrivals + SLOs => FCFS + greedy batching misses SLOs; scheduling
must be SLO-attainment-aware. In our terms: the hold decision must read the SLO cliff.

### 2.10 2510.18544.txt (SLICE — "SLO-Driven Scheduling for LLM Inference on Edge Computing Devices")

- L24-31: "ultimately results in persistently high violation rates for end-to-end latency or TPOT
  related SLOs... SLICE significantly improves LLM inference service SLO attainment... up to 35x higher
  SLO attainment"
=> Q4 anchor: edge-local scheduling driven by (TTFT/TPOT) SLO attainment is the same axis as our
TDR/TPOT score; TPOT appears verbatim as an SLO metric in the serving literature.

---

## 3. The mathematical model [OWN unless cited]

Notation (per cloud c, since D-009 groups are within one cloud):

```text
D(m) = d_pre(m) + d_proc(m) + d_post(m)
tr(m) = lat + alpha*m
G(m) = 3S + D(m) + 2*tr(m)
```

- `D` — table durations sum
- `tr` — transfer time, affine in `m`
- `G` — one-round group cost
- `m` — group size
- `S` — setup cost
- `lat` — link latency
- `alpha` — per-token transfer slope

(table durations, strictly positive, observed sublinear in practice; D-009 A/B showed MarginalBatch == GreedyAll on 333 cases; affine: latency + 8*m*bpt/(bw*1e6); docs/ASSUMPTIONS.md §1.6; one S per stage: D_PRE, D_PROC, D_POST; UP + DOWN).

Marginal gain of adding one member to a group of k (merge of two groups of sizes k and 1):

```text
Delta(k) = G(k) + G(1) - G(k+1)
Delta(k) = 3S + D(k) + D(1) - D(k+1) + 2*lat
Delta(1) = 3S + 2D(1) - D(2) + 2*lat
```

- `Delta` — merge gain
- `G` — group cost
- `S` — setup cost
- `D` — table durations
- `lat` — link latency

(derivation: tr(k)+tr(1)-tr(k+1) = lat because tr is affine; the 3S comes from the three stages.)

Key consequence [OWN]: the human sketch's test d_pre(2) < 1.5*d_pre(1) examines only ONE of the three
amortized compute columns and ignores 3S + 2*lat entirely. Since S >= 0, lat > 0, and the proc/post
columns amortize identically, the sketch's condition is a sufficient-but-not-necessary proxy for
Delta(1) > 0. It can both (a) block profitable holds (when proc/post are the sublinear columns) and
(b) allow holds the full test would reject. Replace it with the full-function test.

Delay cost of holding the group for delta (all from interactor.py score, L382-397) [OWN]:
CRITICAL FACT (verified in interactor.py:243-244): TDR is recorded at P POST completion
(sum_tdr += (t+S+dur) - arr_time in the P POST handler), i.e., TDR covers ONLY the input stage
P PRE -> UP -> P PROC -> DOWN -> P POST. DECODE DECISIONS NEVER TOUCH mu_tdr.

```text
TPOT = sum_span/total_gaps
mu_tpot += delta * k2 / total_gaps
tp -= delta * tp / (last_finish - first_arrival)
dScore/ddelta ~ -1000*w_c*(e_tpot*(k2/total_gaps)/SLO2)/dist - 1000*w_tp*(tp*delta/span)
h2 = SLO2 - mu_tpot
```

- `TPOT` — pooled gap mean
- `sum_span` — total span
- `total_gaps` — total gaps
- `mu_tpot` — mean TPOT
- `k2` — members past first token
- `tp` — output rate
- `e_tpot` — relative TPOT excess
- `SLO2` — TPOT target
- `dist` — Euclidean excess
- `h2` — TPOT headroom
- `w_c` — waiting weight
- `w_tp` — throughput weight
=> Consequence: holding FIRST-iteration members is C_w-free (TDR is unaffected by decode, TPOT
span is unchanged); the only C_w cost is on already-started members (k2), and the only C_tp cost
is when the held group contains the final token of the makespan-finishing request.

Sticky merging [OWN — not in any downloaded paper]: members of one D_PRE group complete D_POST
together, return to RD_PRE together, and are re-batched together by GreedyAllBatch on every subsequent
iteration until one member's Lout ends. Hence the value of a merge is not Delta(k) once, but
approximately Delta(k) * (number of remaining aligned iterations). With Lout potentially large this
multiplies the merge value manyfold; equally, it means the DELAY is paid once while the SAVINGS recur.
Counterfactual: two members that are never merged pay G(1) per member per iteration forever.

Ski-rental structure [cited 1.3]: hold = renting (per-time cost), issue = buying (one-time cost).
Offline optimum knows arrival gaps; the deterministic worst-case optimum waits until accumulated delay
cost equals the one-time alternative cost (threshold t = b, CR 2 - 1/b). Any fixed wait budget delta
that is not at the break-even point degrades the worst case; the break-even point is
delta* = (value of the merge)/(delay cost rate), i.e. delta* = Delta(1)*Lbar / v(k), where v(k) is the
per-time score loss of holding the k members and Lbar the remaining-alignment count. The human's
delta = S/2 is a proxy for this break-even; it is only right when S/2 happens to equal delta*.

Pipeline lookahead [cited 2.5 + OWN]: the D-004 transfer oracle makes the ready time of every member in
W_IN_DOWN/RD_PRE exactly computable. Members joining within the hold are CERTAIN (not probabilistic),
so p is deterministic and the hold is a certainty gain if Delta(k+p) sum exceeds the delay cost. The
only uncertainty is brand-new ARR events, which are adversarial and unobservable: the hold is
one-step-lookahead with the certainty property for pipeline members only.

SLO cliff [cited 2.8-2.10 + interactor formula]: with dist_base = 0, C_w = 1 only if dist = 0 exactly,
i.e., BOTH mu_tdr <= SLO1 and mu_tpot <= SLO2. A hold that pushes either mean over its SLO kills the
whole w_c component. With dist_base > 0 the cliff is linear (C_w decays to 0 at dist = dist_base).
BUT mu_tdr is input-stage-only (see delay-cost box): the only SLO a hold can violate is SLO2 (TPOT),
and only through members that already produced a first token. The binding mean headroom is:
    h2 = SLO2 - mu_tpot.
The mean-based structure (not per-request deadlines) means EDF/LLF per-request deadline theory (notes

203) does NOT apply directly; the binding constraint is the TPOT mean headroom (plus the C_tp clamp).

Work-conserving caveat [cited 2.3]: deliberate idle cannot increase throughput; the hold only pays
through C_w headroom. If w_tp = 1 (pure throughput) or tp <= tp_base (C_tp already 0), any hold is
pure loss. If the case is throughput-saturated (tp >= tp_UB), the hold costs nothing on C_tp and is
free except for C_w.

---

## 4. Claim manifest (verification table)

| Claim                                                                                                                                                                                                                                                                  | Source                                                     | File:lines                                                          |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- | ------------------------------------------------------------------- |
| Optimal batch-service policy with setup+holding cost = threshold on queue length (serve-all above a)                                                                                                                                                                   | Germs & van Foreesextract of Deb-Serfozo 1973 / Aalto 2000 | springer-bulk-queues-thresholds.txt:181-184, 236-246                |
| Threshold increases with setup cost per service interval                                                                                                                                                                                                               | Germs & van Foreest                                        | springer-bulk-queues-thresholds.txt:1277-1282                       |
| No member can join a batch once service starts                                                                                                                                                                                                                         | Germs & van Foreest                                        | springer-bulk-queues-thresholds.txt:186-188                         |
| N-policy: idle until queue >= N                                                                                                                                                                                                                                        | Sasikala survey                                            | sasikala-bulk-service-survey.txt:200-203                            |
| (a,b) accumulate-then-serve rule                                                                                                                                                                                                                                       | arXiv 2208.13043                                           | batch-service-map-vacation.txt:26-31                                |
| Deterministic ski-rental optimum: wait until rent = buy (t=b), CR 2 - 1/b                                                                                                                                                                                              | Brown slides                                               | skirental-brown-slides.txt:9-14, 38-39                              |
| Randomized ski-rental CR e/(e-1)                                                                                                                                                                                                                                       | Brown slides                                               | skirental-brown-slides.txt:318-340                                  |
| Rent/buy with time-varying demand: deterministic 2, randomized 1.582, optimal                                                                                                                                                                                          | Kodialam SIAM                                              | rentorbuy-siam.txt:5-13                                             |
| Ski-rental models "wait vs do task now with high cost"                                                                                                                                                                                                                 | Kodialam SIAM                                              | rentorbuy-siam.txt:25-27                                            |
| Work-conserving policies achieve max throughput (stability)                                                                                                                                                                                                            | Dai et al.                                                 | 2504.07347.txt:23-25, 940-944, 1013-1017                            |
| Greedy EDD (2+1/kappa)-competitive with patience/lookahead                                                                                                                                                                                                             | Goldwasser                                                 | goldwasser-soda99-patience.txt:64                                   |
| 1-lookahead cuts worst-case ratio from 2 to ~1.3 (bounded-delay scheduling)                                                                                                                                                                                            | Bohm et al.                                                | 1606.09395.txt:110-112, 116-119                                     |
| Production ML serving: cap batch size by latency SLO, delay dispatch (Nagle-motivated) when fixed cost / variable cost ratio is high; model-dependent                                                                                                                  | Clipper NSDI'17                                            | clipper-nsdi17-crankshaw.txt:843-848, 917-927, 929-938              |
| Triton: delay up to max_queue_delay, send early when preferred size reached; default = no delay                                                                                                                                                                        | Triton docs                                                | triton-batcher-docs.md (Dynamic Batcher, Delayed Batching sections) |
| Iteration-level batching of what is ready = Orca's mechanism                                                                                                                                                                                                           | Orca SOSP'22                                               | osdi22-yu.txt:583-590                                               |
| Stall-free (never hold decodes for late joiners) unlocks throughput                                                                                                                                                                                                    | Sarathi-Serve                                              | 2403.02310.txt:18-22                                                |
| SLO-aware practice: prioritize near-miss decode requests                                                                                                                                                                                                               | Bari et al.                                                | 2508.01002.txt:abstract                                             |
| Variable arrivals + greedy batching compromise SLO attainment                                                                                                                                                                                                          | Wang                                                       | 2605.02329.txt:abstract                                             |
| TPOT as an edge-serving SLO metric                                                                                                                                                                                                                                     | SLICE                                                      | 2510.18544.txt:24-31                                                |
| Score formula (C_tp, C_w, dist, means), stuck() semantics                                                                                                                                                                                                              | tools/interactor.py (repo)                                 | L364-397                                                            |
| Merge gain formula Delta(k) = 3S + D(k)+D(1)-D(k+1) + 2*lat; TDR is input-stage-only (interactor L243-244) so holds never touch mu_tdr; delay cost = delta*k2/total_gaps on mu_tpot (k2 = already-started members) + C_tp tail risk; sticky merging; break-even delta* | Own derivation from interactor.py + protocol               | — [OWN]                                                             |

All [OWN] items are algebraic consequences of the interactor score formula and the D-009/D-004
protocol findings, not literature claims.
