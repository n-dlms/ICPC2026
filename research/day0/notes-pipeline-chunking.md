> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Research below was completed on Day 0 (2026-08-14) and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Pipeline Parallelism & Chunked Prefill: Granularity/Bubble Literature

## Research notes for Day 0 — prefill chunking strategy (Codeforces 2251)

**Purpose**: Understand the pipeline-bubble / granularity tradeoff literature and the
"chunked prefill" scheduling literature, and map it to our problem: splitting one request's
remote prefill into consecutive layer pieces `[ls, le)` on a single-server cloud, +S per
piece, only last piece queues DOWN, pieces of different requests may interleave.

**Date**: 2026-08-14. **Author**: Ntokozo Dlamini.

**File ownership**: PDFs in `research/papers/<id>.pdf`, text extracts `<id>.txt`.
All quotes in the VERIFIED section below were re-checked against the `.txt` extracts on
2026-08-14.

---

## 0. ID verification results (what was checked before downloading)

| Requested                            | Given ID   | arXiv API result                                                                                                                                                            | Verdict                                                                                                                                                       |
| ------------------------------------ | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPipe                                | 1811.06965 | "GPipe: Efficient Training of Giant Neural Networks using Pipeline Parallelism" (v5)                                                                                        | **OK — downloaded**                                                                                                                                           |
| PipeDream                            | 1806.03377 | "PipeDream: Fast and Efficient Pipeline Parallel DNN Training" (v1)                                                                                                         | **OK — downloaded**                                                                                                                                           |
| LoongServe                           | 2311.03299 | "Scalar field dark energy..." (unrelated)                                                                                                                                   | **WRONG ID**; real paper = **2404.09526** "LoongServe: Efficiently Serving Long-Context Large Language Models with Elastic Sequence Parallelism" — downloaded |
| BurstPipe                            | 2410.14796 | "Invitation to p-adic vertex algebras" (unrelated); arXiv full-text search for "BurstPipe" returns **zero** results; web search finds no such paper (only plumbing content) | **DOES NOT EXIST on arXiv** — reported, not downloaded; substituted 2025-2026 chunked-prefill scheduling papers                                               |
| 1F1B-interleaved / granularity       | (search)   | 2006.09503 "Memory-Efficient Pipeline-Parallel DNN Training" (Narayanan et al., ICML 2021) — 1F1B/2BW schedule paper                                                        | **OK — downloaded**                                                                                                                                           |
| 2025-2026 chunked prefill scheduling | (search)   | 2510.08055 "From Tokens to Layers... Layered Prefill" (2025); 2606.09061 "Fairness-Aware and Latency-Controllable Scheduling for Chunked-Prefill LLM Serving" (June 2026)   | **OK — downloaded**                                                                                                                                           |

Already present (not re-downloaded): 2401.10241 Zero Bubble, 2403.02310 Sarathi-Serve,
2507.00217 CrossPipe, 2306.06003 semi-online lookahead.

---

## 1. Per-paper metadata

| arXiv ID   | Title                                                                              | Venue/date          | Relevance to us                                                                                                                     |
| ---------- | ---------------------------------------------------------------------------------- | ------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| 1811.06965 | GPipe: Easy Scaling with Micro-Batch Pipeline Parallelism                          | arXiv v5 2019-07-25 | Canonical bubble formula + "micro-batch count ≥ 4×partitions" rule                                                                  |
| 1806.03377 | PipeDream: Fast and Efficient Pipeline Parallel DNN Training                       | arXiv v1 2018-06-08 | 1F1B steady-state scheduling, NOAM (keep pipeline full), layer profiling                                                            |
| 2006.09503 | Memory-Efficient Pipeline-Parallel DNN Training (1F1B/2BW)                         | ICML 2021           | Flush-free schedules; m ≥ d rule; steady-state interleaving                                                                         |
| 2401.10241 | Zero Bubble Pipeline Parallelism                                                   | already present     | Interleaved/stage-splitting reduces bubble: "assigning multiple stages to the same device... further reduces the bubble size"       |
| 2403.02310 | SARATHI: Efficient LLM Inference by Piggybacking Decodes with Chunked Prefills     | already present     | THE chunked-prefill baseline: token budget, stall-free batching, P:D ratio, chunking overheads                                      |
| 2404.09526 | LoongServe: Elastic Sequence Parallelism                                           | arXiv v2 2024-10-29 | Dynamic per-iteration chunking (DoP scaling), critique of static chunked prefill                                                    |
| 2510.08055 | From Tokens to Layers: Layered Prefill (MoE serving)                               | arXiv v2 2026-04-16 | Chunk size as THE tuning knob; one-group-per-iteration; chunk caps for tail latency                                                 |
| 2606.09061 | Fairness-Aware and Latency-Controllable Scheduling for Chunked-Prefill LLM Serving | arXiv 2026-06-08    | Aging priority formula; feasible chunk upper bound h_i = min(r_i, Bmax−U_t); minimum effective chunk L_min; decode-first discipline |

---

## 2. Key insights per paper

### 2.1 GPipe (1811.06965) — the granularity/bubble tradeoff in its purest form

1. Pipeline parallelism = partition model into K cells (consecutive layer groups), split the
   mini-batch into M equal micro-batches, all accelerators process different micro-batches
   concurrently. Bubble = idle time per accelerator between pipeline start and steady state.

2. Bubble is amortized: with M micro-batches and K partitions, the bubble fraction shrinks
   as M grows; empirically "negligible when M ≥ 4 × K" (i.e., micro-batch count at least 4×
   the number of pieces). Rule of thumb: pieces ≤ ~concurrency/4.

3. "When M is 1, there is effectively no pipeline parallelism" — splitting work into pieces
   provides ZERO benefit when there is no other work to interleave. Chunking is only useful
   under concurrency.

4. Evenly balanced pieces are assumed; imbalanced partitions lose throughput ("imperfect
   partitioning" hurts).

5. Cost side: pieces only communicate at boundaries (in our problem: no transfer between
   pieces at all — we get this for free).

### 2.2 PipeDream (1806.03377) — steady-state interleaving discipline

1. 1F1B (one-forward-one-backward): in steady state each worker alternates F/B passes of
   different inputs; "In a balanced pipeline, 1F1B ensures that no GPU is idle in steady
   state". Steady-state interleaving beats one-task-at-a-time.

2. NOAM (number of optimal active minibatches) = pipeline depth: that many tasks must be
   in flight to keep the pipeline full. Below that, bubbles. → Each cloud must have ≥1
   ready piece at all times; the number of "stages" sharing a server bounds the number of
   interleave slots.

3. Stage durations are profiled layer-by-layer first ("PipeDream profiles..."), then the
   partitioner balances work. → In our problem the task-time table IS that profile; piece
   sizing must use it, not assumptions.

4. Asynchronous pipelining is theoretically bubble-free but at the cost of consistency;
   synchronous (flush-based) pays periodic idle flushes.

### 2.3 1F1B/2BW (2006.09503)

1. GPipe-style flushing is "expensive, limiting throughput"; 2BW+1F1B avoids flushes with
   m ≥ d micro-batches per weight version (d = pipeline depth).

2. The memory-efficiency work exists because finer granularity = more in-flight state; in
   our problem there is no memory limit, so we never pay that cost — chunking is cheaper
   for us than for any of these systems.

### 2.4 Zero Bubble (2401.10241) — the interleaving payoff, quoted

1. "Based on 1F1B, Narayanan et al. introduced the 1F1B interleaved strategy. By assigning
   multiple stages to the same device, it further reduces the bubble size" — i.e., splitting
   one stage into multiple smaller stages on the SAME device creates more interleave points
   and shrinks idle time. This is exactly our situation (one server per cloud).

2. Cost of finer granularity = "more communication and higher peak memory" — in our problem
   the communication cost is absent (pieces do not transfer), leaving only +S per piece.

### 2.5 SARATHI (2403.02310) — chunked prefill, our closest analog

1. Chunked prefill = split long prefill into chunks, co-schedule (coalesce) chunks with
   decode batches; "stall-free batching" = decodes never wait behind a prefill chunk.

2. Per-iteration rule: first pack all running decodes, then leftover token budget goes to
   prefill chunks; new requests only after that. Chunk size of a prefill = min(remaining
   tokens, leftover budget).

3. Token budget chosen from TWO competing factors: TBT SLO vs chunked-prefill overhead.
   Smaller budget → less decode interference; larger budget → fewer chunks.

4. Chunking overheads (explicitly measured): per-chunk fixed overheads (kernel launch
   analog of our +S) — "there can be small overhead associated with chunking due to fixed
   overheads of kernel launch"; KV-cache re-read amplification (first chunk's KV loaded N−1
   times — we have no KV, so no such amplification); tile-quantization (chunk size 257 vs
   256 → 32% longer prefill!). Takeaway: per-chunk fixed cost exists and is sensitive to
   awkward sizes; in our problem the fixed cost is S exactly, and pieces must be whole
   layer-counts.

5. Naive hybrid batching (no chunking) gives up to 28.3× TBT blowup vs decode-only → the
   reason to chunk is decode/TPOT protection, not prefill speedup.

### 2.6 LoongServe (2404.09526) — why STATIC chunking is insufficient

1. "chunked prefill splits the long context into chunks and processes them chunk by chunk
   with the decoding phase, but still incurs interference between two phases".

2. SplitFuse/SARATHI-style chunked prefill "needs to set the chunk size... we set the chunk
   size as the ideal 'P:D ratio' described in SARATHI by calculating it for each dataset
   before experiments, although it is unknown in practice" — the P:D ratio is a static
   global constant, unknown online. This is the classic flaw of fixed chunking.

3. "decomposing the input sequence makes the prefill phase inefficient" — chunking has a
   real prefill-side cost (not just decode benefit).

4. Their fix: elastic/adaptive chunking — "ESP dynamically decides the Degree of
   Parallelism for requests in each iteration", prefill with max parallelism, decode with
   less. Message: the chunk/parallelism decision must be per-iteration, not fixed.

5. Result: 3.85× vs static chunked prefill, 5.81× vs prefill-decode disaggregation.

### 2.7 Layered Prefill (2510.08055) — chunk size is THE tuning knob

1. "The chunk size (number of tokens per chunk) is the key tuning knob: smaller chunks
   reduce per-iteration prefill work and relax TBT pressure, whereas larger chunks reduce
   the number of chunks and lower TTFT."

2. Rule: "chunk size is capped to meet tail-latency SLOs, limiting per-iteration batch
   size" — cap chunk work by a latency budget.

3. One-group-per-iteration rule: each iteration does prefill for exactly one group; prefill
   of a request completes in exactly N_groups iterations without stalling decode. Prefill
   and decode progress in lockstep.

4. Typical chunk = 256-512 tokens; short requests are coalesced into one chunk.

### 2.8 Fairness/latency chunked-prefill scheduling (2606.09061) — 2026, directly on topic

1. "Chunked-prefill execution... breaking a long prefill into multiple schedulable chunks,
   allowing prefill and decode requests to be interleaved at a finer granularity."

2. Decode-first is the invariant: policy "must preserve the decode-first scheduling
   discipline and therefore should not interfere with TPOT".

3. Aging priority formula: P_i(n) = α·(t − a_i) + β·r_i(n), α > 0 (aging), β < 0
   (remaining work). Short prefill chunks first (β term) but long requests can never be
   starved (α term grows with waiting).

4. Feasible chunk upper bound per round: h_i = min(r_i, B_max − U_t) — remaining tokens,
   clipped by the leftover budget. Greedy fill of the budget is the baseline; they improve
   on it with a latency target because "identical chunk sizes may induce substantially
   different execution time under varying context states".

5. Minimum effective chunk L_min: prevents "micro-progress" (requests getting single-token
   chunks); concurrency constrained by slots AND budget AND minimum progress.

6. Measured: aging policy cuts mean end-to-end latency >10% vs FCFS; LPRS/APC cut P99 tail
   latency and "suppress prefill fragmentation" (fragmentation = many requests each with
   tiny remaining chunks — a real failure mode of naive round-robin chunking).

---

## 3. MAPPING TO OUR PROBLEM (concrete recommendations)

Our system differs from all the above in one crucial way that makes chunking CHEAPER:
pieces of one request are strictly sequential on ONE cloud's single server, there is NO
transfer between pieces, and only the last piece queues DOWN. So the only costs of
splitting a prefill into K pieces are (K−1)·S of compute and scheduling granularity
effects. The only benefit is interleaving with OTHER work on that same cloud.

1. **Chunk only when the cloud has competing work.** With zero concurrency, pieces give
   "effectively no pipeline parallelism" (GPipe) — they only add S. Split a prefill iff
   (a) another request's decode batch or prefill piece is ready on the same cloud, or
   (b) the local computer or DOWN queue will otherwise starve (chunking lets the cloud
   yield control to other requests). At low load: 1 piece always.

2. **Pieces ≈ concurrency/4 rule.** GPipe's empirical "bubble negligible when M ≥ 4×K"
   gives a direct cap: with M pieces/batches of other work expected on the cloud, keep the
   number of pieces of any single request ≤ M/4. Never split a prefill into more pieces
   than there are concurrent work items to fill the gaps.

3. **Decode-first on each cloud.** Sarathi's per-iteration discipline maps exactly: when a
   cloud has a ready decode batch (D PROC) and a ready prefill piece, run decode first
   (protect TPOT — 2606.09061: "must preserve the decode-first scheduling discipline").
   The D PRE/D POST batching layer (ours) is the "coalescing" step; the piece scheduler is
   the "token budget" step. Chunking exists to give decode batches insertion points.

4. **Piece size from the task table, not fixed.** The P:D ratio / fixed chunk size is
   called out as unknowable online by both Sarathi's own analysis and LoongServe. Instead:
   pick piece layer-count so that `(le−ls)/num_layers × prefill_proc(Lin) + S` ≈ a target
   quantum — the natural quantum being the expected next-decode-batch duration on that
   cloud (computed from task table + group size). This is the "latency budget" idea of
   Sarathi (token budget) and 2606.09061 (h_i bound), made dynamic per request.

5. **Chunking never helps the chunked request's own TDR — treat it as a system-level
   lever.** Since only the last piece queues DOWN and pieces run back-to-back when the
   cloud is otherwise idle, the request's own P POST completion is ≥ the 1-piece time.
   Chunking trades the request's own TDR for others' TDR and for system TPOT. Never chunk
   "for the request itself"; chunk for the co-tenants. (This is the crucial inversion
   vs. GPipe, where splitting helps the SAME work's throughput.)

6. **DOWN queue interaction: chunking is a natural enqueuer throttle.** Only the last
   piece's TDN queues DOWN, so keeping a request mid-prefill (while interleaving other
   work) defers its DOWN entry; short requests whose DOWN is already queued pass it. When
   the DOWN queue is congested, finish other clouds'/requests' DOWN first by interleaving
   work in front of a long prefill's final piece. Do NOT idle to achieve this — work-
   conserving must hold — but ordering pieces of OTHER requests ahead of the last piece is
   legal and buys queue reordering.

7. **Minimum piece size: guard against fragmentation and S-inefficiency.** Each piece pays
   S. Rule from 2606.09061 (L_min) and Sarathi's measured per-chunk overheads: no piece
   whose compute time is small relative to S (e.g., piece_time < ~4×S is wasteful; piece
   count K should keep total overhead (K−1)·S ≤ ~10% of prefill compute) and no piece
   smaller than, say, 1/8 of the prefill (fragmentation → "micro-progress"). Also always
   piece with integer layer bounds (mandatory) and prefer piece sizes that make
   `(le−ls)/num_layers` land on a large enough duration per the interpolation table.

8. **Fairness across requests sharing a cloud: aging, not FCFS.** 2606.09061 measured
   >10% latency improvement of aging vs FCFS; a long prefill hogging a cloud destroys
   co-tenants' TDR (SLO1). Implement priority = α·wait + β·remaining-work (β<0) for
   picking which ready piece runs next; this yields short-prefill-first while bounding
   starvation of long requests. With 1 cloud per request permanently assigned, this
   matters when many requests share one cloud.

9. **Validation caveat — the classic bubble formula.** The "(m−1)/(m+p−1)-type" formula
   (bubble = (K−1)/(M+K−1)) appears in GPipe's text only as a pdftotext-garbled stacked
   fraction; the clean, extract-verifiable versions of the idea are GPipe's "negligible
   when M ≥ 4×K", 1F1B "no GPU idle in steady state", and Zero Bubble's interleaved
   statement. Use those as the load-bearing claims, not a derived closed form. In our
   setting the exact bubble math is: total prefill time for K pieces = sum of piece times

   - (K−1)·S + idle gaps; benefit exists iff gaps get filled by other work worth more
   than S per gap.

---

## 4. VERIFIED — exact quotes from .txt extracts (with file:line)

### 1811.06965.txt (GPipe)

- L88-89: "as a sequence of layers, and consecutive groups of layers can be partitioned
  into cells. Each cell is then placed on a separate accelerator."

- L153: "During the forward pass, GPipe first divides every mini-batch of size N into M
  equal micro-batches,"

- L272-274: "As illustrated in Figure 2c, partitioning introduces some idle time per
  accelerator, which we refer to as the bubble overhead. This bubble time is O( MK−1
  +K−1 ) amortized over the number of micro-steps M. In our experiments, we found the
  bubble overhead to be negligible when M ≥ 4 × K."
  (NOTE: the fraction is a stacked formula garbled by pdftotext; the verifiable content
  is "negligible when M ≥ 4 × K".)

- L346-347: "When M is 1, there is effectively no pipeline parallelism."

### 1806.03377.txt (PipeDream)

- L859-861: "minibatches to keep the pipeline full in steady state. Once in steady state,
  each stage alternates between performing the forward and backward pass for a minibatch.
  We call this mechanism one-forward-one-backward (1F1B). In a balanced pipeline, 1F1B
  ensures that no GPU is idle in steady state"

- L913-920: "the optimal number of minibatches admitted per input stage to keep the
  pipeline full in steady state is given by d (# machines) / (# machines in the input
  stage) e. We refer to this quantity as the NUM_OPT_ACTIVE_MINIBATCHES (NOAM)."

- L585: "To determine Tl for all layers, PipeDream profiles a" (profiling-based stage
  sizing)

### 2006.09503.txt (1F1B/2BW)

- L346-350: "PipeDream-2BW uses a novel double-buffered weight update (2BW) scheme in
  conjunction with 1F1B scheduling (Narayanan et al., 2019), where each worker alternates
  between forward and backward passes for different inputs... 2BW has a lower memory
  footprint than PipeDream and GPipe, and also avoids GPipe's expensive pipeline flushes."

- L359-360: "m microbatches (m ≥ d, the pipeline depth)."

### 2401.10241.txt (Zero Bubble)

- L61-64: "With the same number of microbatches, it yields similar bubble ratios but with
  a distinct advantage in peak memory. Based on 1F1B, Narayanan et al. (2021) introduced
  the 1F1B interleaved strategy. By assigning multiple stages to the same device, it
  further reduces the bubble size at the cost of more communication and higher peak
  memory."

### 2403.02310.txt (SARATHI)

- L1168-1183: "Sarathi-Serve first calculates the budget of maximum number of tokens that
  can be executed in a batch based on user specified SLO... In every scheduling iteration,
  we first pack all the running decodes in the next batch... Only after all the running
  requests have been accommodated, we admit new requests... When adding prefill requests
  to the batch, we compute the maximum chunk size that can be accommodated within the
  leftover token budget for that batch... By restricting the computational load in every
  iteration, stall-free batching ensures that decodes never experience a generation stall
  due to a co-running prefill chunk. ... Naive hybrid batching leads to dramatic increase
  of up to 28.3× in the TBT latency compared to a decode-only batch."

- L1261: "The token budget is determined based on two competing factors — TBT SLO
  requirement and chunked-prefills overhead."

- L1280-1283: "if a prefill sequence is split into N chunks, then the first chunk's
  KV-cache is loaded N − 1 times, the second chunk's KV-cache is loaded N − 2 times, and
  so on."

- L1294: "using chunk size of 257 can increase prefill time by 32% compared to that with
  chunk size 256."

### 2404.09526.txt (LoongServe)

- L329-330: "chunked prefill [20, 41] splits the long context into chunks and processes
  them chunk by chunk with the decoding phase, but still incurs interference between two
  phases [61]."

- L1511-1512: "Similar to SARATHI [8], it needs to set the chunk size. We set the chunk
  size as the ideal 'P:D ratio' described in SARATHI [8] by calculating it for each
  dataset before experiments, although it is unknown in practice."

- L1621-1623: "DeepSpeed MII and LightLLM w/ SplitFuse try to split the input sequence
  into chunks to protect the decoding phase. However, decomposing the input sequence
  makes the prefill phase inefficient."

- L198-202: "Elastic Sequence Parallelism (ESP). Unlike existing static parallelisms, ESP
  dynamically decides the Degree of Parallelism (DoP) for requests in each iteration. For
  instance, during the prefill phase, ESP could set the DoP to the total number of GPUs...
  Upon transiting to the relatively lightweight decoding phase, ESP can decrease the DoP..."

- L232: "improves the throughput by up to 3.85× compared to chunked prefill and 5.81×
  compared to prefill-decoding disaggregation."

### 2510.08055.txt (Layered Prefill)

- L101-108: "Chunked prefill (Agrawal et al., 2024) addresses this by splitting long
  prompts into smaller chunks and interleaving their prefills with decode. The chunk size
  (number of tokens per chunk) is the key tuning knob: smaller chunks reduce per-iteration
  prefill work and relax TBT pressure, whereas larger chunks reduce the number of chunks
  and lower TTFT."

- L124-126: "In each iteration, prefill is performed alongside decode for exactly one
  group, while the remaining groups perform decode only. This design ensures that prefill
  completes in exactly Nlg iterations without stalling decode."

- L231-234: "by setting the chunk size—typically 256 or 512 tokens—the scheduler both
  satisfies the TBT constraint and distributes prefill work more evenly across iterations,
  which improves hardware utilization."

- L241-242: "The chunk size is capped to meet tail-latency SLOs, limiting per-iteration
  batch size."

### 2606.09061.txt (Fairness-aware chunked-prefill scheduling, 2026)

- L206-208: "Chunked-prefill execution can improve LLM serving by breaking a long prefill
  into multiple schedulable chunks, allowing prefill and decode requests to be interleaved
  at a finer granularity."

- L220-222: "it must preserve the decode-first scheduling discipline and therefore should
  not interfere with TPOT (Time Per Output Token)"

- L230-233: "Pi (n) = α · (t − ai ) + β · ri (n), where t is the current system time, and
  α > 0 is the aging weight, and β < 0 is the remaining-work weight."

- L450-452: "hi = min(ri , Bmax − Ut )." (feasible upper bound of chunk size per round)
- L461-464: "it ignores the fact that identical chunk sizes may induce substantially
  different execution time under varying context states."

- L610-615: "This formulation ensures that the active-prefill concurrency is jointly
  constrained by sequence slots, the remaining token budget, and the minimum progress
  quality... This boundary prevents scenarios where request concurrency remains compliant,
  but the actual forward progress of each request is too minor to be effective."

- Abstract (L14-17): "the aging policy reduces mean end-to-end latency by over 10%
  compared to FCFS. Moreover, LPRS and APC significantly reduce P99 tail latency and
  suppress prefill fragmentation"

---

## 5. Open questions for the human (not answerable from these papers)

1. Does the score formula reward TPOT at the request level only, or system-wide? This
   decides whether decode-first interleaving (protecting TPOT) should dominate TDR.

2. Is there a per-piece DOWN enqueue (only last piece) — confirmed by spec — meaning
   chunking lets us choose WHEN DOWN is enqueued? (Human: re-verify against statement.)

3. Whether "D PROC must wait for TDN" means a cloud can be idle waiting for DOWN data
   while its server is free for P pieces — this is exactly the interleave opportunity
   chunking exploits (pieces of OTHER requests fill the gap).

4. No evidence found that piece count should be tied to num_layers' divisibility for
   scoring; only integer bounds matter for WA. (Human: verify against statement.)

## 6. BurstPipe report (as requested)

arXiv 2410.14796 = "Invitation to p-adic vertex algebras" — NOT BurstPipe. arXiv
full-text search "BurstPipe": 0 results. Web search: no such systems paper exists
(only water-pipe content). The task's ID/title pair could not be verified; treated as
non-existent and replaced with 2510.08055 + 2606.09061 (both 2025/2026 chunked-prefill
scheduling papers). If the human has another citation for BurstPipe, provide it and we
will download it.
