> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Research below was completed on Day 0 (2026-08-14) and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Notes: LLM Serving Systems (Chunked Prefill, Batching, Throughput-Latency Tradeoff)

**Date**: 2026-08-14 (Day 0)
**Purpose**: Research input for the ICPC Online Challenge "Scheduling Algorithm for Collaborative Inference System" (Codeforces 2251A). Focus: prefill chunking granularity, decode batching, throughput-latency tradeoff, starvation avoidance.
**Verification protocol**: Every claim below is quoted verbatim from the extracted `.txt` of the downloaded PDF in `research/papers/` (pdftotext). Quotes are verbatim modulo pdftotext ligature artifacts (e.g., "O RCA" -> "ORCA", "1 00" -> "100"). If a claim cannot be tied to a quote, it is flagged as an *inference*, not a claim.

**Files downloaded this session** (all verified `pdftotext` extract > 5 KB):

| File                                                              | Paper                                                                                                  | Source                                     |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ------------------------------------------ |
| `research/papers/2309.06180.pdf` (+.txt, 83 KB)                   | vLLM: Efficient Memory Management for LLM Serving with PagedAttention                                  | arXiv (open access)                        |
| `research/papers/osdi22-yu.pdf` (+.txt, 90 KB)                    | Orca: A Distributed Serving System for Transformer-Based Generative Models                             | USENIX OSDI'22 (open access, not on arXiv) |
| `research/papers/2405.14636.pdf` (+.txt, 42 KB)                   | PerLLM: Personalized Inference Scheduling with Edge-Cloud Collaboration for Diverse LLM Services       | arXiv (open access)                        |
| `research/papers/2508.01989.pdf` (+.txt, 92 KB)                   | Prefill-Decode Aggregation or Disaggregation? Unifying Both for Goodput-Optimized LLM Serving (TaiChi) | arXiv (open access)                        |
| `research/papers/2606.09061.pdf` (+.txt, 66 KB)                   | Fairness-Aware and Latency-Controllable Scheduling for Chunked-Prefill LLM Serving                     | arXiv (open access)                        |
| `research/papers/agrawal-chunked-prefills-osr.pdf` (+.txt, 35 KB) | Efficient LLM Inference via Chunked Prefills (Sarathi-Serve summary, ACM SIGOPS OSR 59(1))             | NSF PAR accepted manuscript (open access)  |

**SLaI (2405.18081) — NOT DOWNLOADED, paper could not be verified to exist**:

- The task-suggested ID `2405.18081` was checked against the arXiv API and resolves to "Optimality of Approximate Message Passing Algorithms for Spiked Matrix Models with Rotationally Invariant Noise" — an unrelated signal-processing paper.
- Exhaustive title search ("SLaI speculative looping", "Accelerating SLO-aware Inference Serving with Speculative Looping", arXiv API, Semantic Scholar, web) finds no paper with that title on arXiv or anywhere open-access.
- The only "SLAI" in the serving literature is the SLO-Aware LLM Inference scheduler *inside* arXiv `2508.01002` ("Optimal Scheduling Algorithms for LLM Inference: Theory and Practice"), which is **already downloaded** in this workspace. Per instructions, we skip the unverifiable paper rather than download the wrong one.

---

## 1. vLLM / PagedAttention — arXiv 2309.06180

**Title**: Efficient Memory Management for Large Language Model Serving with PagedAttention
**Authors**: Woosuk Kwon, Zhuohan Li, Siyuan Zhuang, Ying Sheng, Lianmin Zheng, Cody Hao Yu, Joseph E. Gonzalez, Hao Zhang, Ion Stoica (UC Berkeley et al.)
**Year/Venue**: 2023, SOSP '23
**URL**: https://arxiv.org/abs/2309.06180

Key insights (all quotes in VERIFIED §V1):

1. Prefill is compute-efficient, decode is memory-bound: the decode phase "severely underutilizes GPU computation and becomes memory-bound, being responsible for most portion of the latency of a single request" while prefill "can efficiently use the parallelism inherent in GPUs" — the fundamental asymmetry that motivates chunking/piggybacking.
2. Batching is how throughput is won: "the overhead of moving weights is amortized across the requests in a batch" and "the engine's throughput is highly dependent on the batch size" (the second sentence is from Orca; vLLM's analog: batching is "non-trivial" because requests "may arrive at different times" and "may have vastly different input and output lengths").
3. Iteration-level (fine-grained) scheduling beats request-level: "After each iteration, completed requests are removed from the batch, and new ones are added. Therefore, a new request can be processed after waiting for a single iteration, not waiting for the entire batch to complete."
4. KV-cache memory waste directly caps batch size: "only 20.4% - 38.2% of the KV cache memory is used to store the actual token states in the existing systems" (Orca variants); memory management is "critical in determining the maximum batch size."
5. Scheduling policy in vLLM is plain FCFS with preempt-latest: "we adopt the first-come-first-serve (FCFS) scheduling policy for all requests, ensuring fairness and preventing starvation. When vLLM needs to preempt requests, it ensures that the earliest arrived requests are served first and the latest requests are preempted first."
6. Output lengths are unknown a priori: "the resulting output lengths are not known a priori" — a constraint we share (Lout arrives only at FIN).
7. Headline result: "vLLM improves the throughput of popular LLMs by 2-4× with the same level of latency compared to the state-of-the-art systems, such as FasterTransformer and Orca."

---

## 2. Orca — USENIX OSDI'22 (open access; not on arXiv)

**Title**: Orca: A Distributed Serving System for Transformer-Based Generative Models
**Authors**: Gyeong-In Yu, Joo Seong Jeong, Geon-Woo Kim, Soojeong Kim, Byung-Gon Chun (SNU / FriendliAI)
**Year/Venue**: 2022, OSDI '22, pp. 521-538
**URL**: https://www.usenix.org/conference/osdi22/presentation/yu (PDF: https://www.usenix.org/system/files/osdi22-yu.pdf)

Key insights (quotes in VERIFIED §V2):

1. **Iteration-level scheduling** is the core abstraction: "we propose iteration-level scheduling, a new scheduling mechanism that schedules execution at the granularity of iteration (instead of request)"; combined with **selective batching**: "we suggest selective batching, which applies batching only to a selected set of operations."
2. FCFS at iteration granularity is the fairness anchor: "early-arrived requests are processed earlier. That is, we ensure iteration-level first-come-first-served (FCFS) property... for any pair of requests (xi, xj) in the request pool, if xi has arrived earlier than xj, xi should have run the same or more iterations than xj."
3. New requests join within one iteration: "the request gets a chance to start processing... after execution of the currently scheduled iteration, significantly reducing the queueing delay."
4. **Batch size is THE tunable knob with diminishing returns**: "Increasing the batch size trades off increased throughput for increased latency, but as the batch size becomes larger, the amount of return (i.e., increase in throughput) diminishes... ORCA also has a notion of a max batch size: the largest possible number of requests within a batch. The ORCA system operator can tune this knob to maximize throughput while satisfying one's latency budget."
5. Scheduling rule = sort by arrival time, cap at max batch size, reserve K/V slots: "it selects at most 'max batch size' requests based on the arrival time, while reserving enough space for storing keys and values to a request when the request is scheduled for the first time."
6. Headline result: "ORCA can significantly outperform NVIDIA FasterTransformer in terms of both latency and throughput: 36.9× throughput improvement at the same level of latency"; concretely "FasterTransformer provides a throughput of 0.185 req/s whereas ORCA provides a throughput of 6.81 req/s, which is a 36.9× speedup" at equal median latency.
7. *Inference*: iteration-level batching = our decode grouping is free-form; the only knob is group size, exactly like Orca's max_bs. FCFS with an arrival cap is the natural baseline (our V1: round-robin cloud + batch size 1).

---

## 3. PerLLM — arXiv 2405.14636 (NOTE: title differs from task description)

**Title**: PerLLM: Personalized Inference Scheduling with Edge-Cloud Collaboration for Diverse LLM Services
**Authors**: Zheming Yang, Yuanhao Yang, Chang Zhao, Qi Guo, Wenkai He, Wen Ji (ICT, Chinese Academy of Sciences)
**Year/Venue**: 2024, arXiv preprint (under review)
**URL**: https://arxiv.org/abs/2405.14636

Key insights (quotes in VERIFIED §V3):

1. The task description gave this ID as "Enhancing LLM Serving Performance through Efficient Placement and Scheduling" — that title does not match; the actual 2405.14636 paper is the edge-cloud collaboration PerLLM, which is *more* relevant to us (local + K clouds).
2. Motivation matches our architecture: "it is difficult for bandwidth-constrained cloud servers to simultaneously process massive LLM services in real-time"; edge-cloud collaboration balances edge responsiveness vs cloud capability.
3. Scheduling framed as **combinatorial multi-armed bandit with constraint satisfaction** (CS-UCB): "we model the complex edge-cloud collaboration process as a combinatorial multi-armed bandit problem and propose a constraint satisfaction upper confidence bound algorithm for efficient service scheduling and resource allocation."
4. Constraints are exactly ours in spirit: processing-time requirement per service (C1), per-server compute capacity (C2), per-server bandwidth (C3), and each service assigned to exactly one server (C4): "The constraint C1 is used to ensure that the processing time requirement of each server is met. Otherwise, it is assigned to a more resource-rich server."
5. Objective is a weighted mix of latency-attainment and cost — structurally like our w_tp/w_c weighted score.
6. Headline result: "PerLLM achieves 2.2×, 2.1×, and 1.6× throughput and reduces the energy cost by more than 50%" vs baselines.
7. *Inference*: cloud assignment in our problem can be a lightweight bandit (per-cloud measured completion vs predicted) rather than static round-robin; energy is irrelevant to us, but the "meet time requirement else reassign" logic maps to assigning the request to the cloud with earliest availability.

---

## 4. TaiChi — arXiv 2508.01989 (Prefill-Decode Aggregation or Disaggregation?)

**Title**: Prefill-Decode Aggregation or Disaggregation? Unifying Both for Goodput-Optimized LLM Serving
**Authors**: Chao Wang, Pengfei Zuo, Zhangyu Chen, Yunkai Liang, Zhou Yu, Ming-Chang Yang (CUHK, Huawei Cloud, Sun Yat-sen University)
**Year/Venue**: 2025, arXiv preprint (Aug 2025)
**URL**: https://arxiv.org/abs/2508.01989

Key insights (quotes in VERIFIED §V4) — **the single most decision-relevant paper for our TDR/TPOT weighting**:

1. **Phase-priority regimes**: "PD aggregation is optimal under tight time-to-first-token (TTFT) and relaxed time-per-output-token (TPOT), while PD disaggregation excels under strict TPOT and relaxed TTFT. However, under balanced TTFT and TPOT SLOs, neither approach can deliver optimal goodput." (PD aggregation = prefill+decode on same instances, like Orca/Sarathi — our topology; PD disaggregation = separate instances — not our topology.)
2. TPOT degradation is **linear in prefill interference**: "The high TPOT in PD aggregation arises from prefill interference due to computation-bound linear operations, with a strong linear relationship between interference intensity and TPOT... Figure 4 reveals a strong linear correlation between TPOT and interference intensity, as evidenced by the fitted line's R-squared value of 0.99" and "if we aim to keep TPOT below 100 ms, we must limit interference intensity to fewer than 273.77 prefill tokens per output token."
3. **Chunk-size choice rule (directly actionable)**: "although smaller chunk sizes reduce interference by limiting the maximum interference per decode token (or batch), they are not always optimal... chunk sizes below 1024 (e.g., 128, 256, 512) constrain TPOT but result in prohibitively high TTFT... Thus, the optimal configuration should adopt the smallest chunk size that still satisfies the TTFT constraint."
4. Chunk-size tradeoff statement: "Larger chunk sizes improve an instance's prefill throughput, which benefits TTFT, but also increase prefill-decode interference, thereby degrading TPOT."
5. TTFT violation under disaggregation is a queuing problem: "The high TTFT in PD disaggregation stems from request queuing, which occurs because PD disaggregation offers lower prefill processing capacity compared to PD aggregation."
6. **Latency shifting**: "we strategically degrade the latency of requests already meeting SLO constraints, thereby reallocating overprovisioned GPU resources (i.e., GPU time) to prioritize SLO-violating requests through scheduling."
7. **Longest-first TPOT degradation**: "we employ a longest-first approach, which selects the request with the current longest output in the D-heavy instance for degradation, as it has the greatest remaining TPOT budget currently and can better absorb performance degradation. Finally, to prevent over-degradation, we monitor the TPOT of migrated requests in real time. Once the TPOT approaches the SLO constraint, the request is flowed back."
8. **Length-aware prefill degradation**: "we propose a length-aware prefill scheduling strategy, which selectively degrades TTFT by assigning short prefill requests to slower instances when doing so does not violate SLO constraints... we estimate its projected TTFT on each D-heavy instance by summing the expected queuing delay and degraded execution time. If this total remains within the TTFT SLO, the request is marked as degradable."
9. Unknown output lengths complicate per-request degradation: "since the output length of a request is unknown in advance, the scheduler lacks the necessary information to make precise, per-request degradation decisions." (Mirrors our Lout-at-FIN limitation.)
10. Headline results: "TaiChi improves goodput by up to 77% compared to state-of-the-art systems under balanced TTFT and TPOT SLOs... It also reduces TTFT by up to 13.2× and TPOT by up to 1.69×."
11. Offline search for config: "the optimal configuration for a given workload and SLO can be determined via offline search, following approaches from prior work [3, 19, 36]... our system adopts an on-demand search-and-reconfigure strategy like DistServe, which triggers a new search and reconfiguration only upon significant workload changes."

---

## 5. Fairness-Aware Chunked-Prefill Scheduling — arXiv 2606.09061

**Title**: Fairness-Aware and Latency-Controllable Scheduling for Chunked-Prefill LLM Serving
**Authors**: Haoxin Liu, Jiayi Wang, Yueshen Xu, Rui Li (Xidian University)
**Year/Venue**: 2026, arXiv preprint (Jun 2026)
**URL**: https://arxiv.org/abs/2606.09061

Key insights (quotes in VERIFIED §V5):

1. **Why FCFS fails under chunking**: "once a long prompt reaches the head of the queue, it may repeatedly consume the residual prefill budget across multiple rounds. Short prompts arriving behind it can therefore experience long TTFT even though their prefill cost is small... a shortest-prefill-first policy improves the responsiveness of short requests, but it can indefinitely delay long requests when short requests keep arriving."
2. **Aging priority formula** (the concrete fix): "Pi(n) = α · (t − ai) + β · ri(n), where t is the current system time, and α > 0 is the aging weight, and β < 0 is the remaining-work weight... The first term α(t − ai) increases with the waiting time of a request... The second term βri(n) favors requests with less remaining prefill work." (In the .txt, ri(n) = "remaining number of prefill tokens at the scheduling round n".)
3. The α/|β| ratio is a single continuous knob: "When this ratio is small, the remaining-work term dominates... behaves closer to the shortest-prefill-first policy... When α/|β| is large, the waiting-time becomes more influential. So long requests can accumulate priority more quickly and eventually overtake newly arrived short requests, thus reducing starvation risk."
4. O(k log n) implementation via heap: "The waiting prefill queue is implemented as a max-heap ordered by Ki(n)... If the queue contains n requests, and k requests are selected in a scheduling round, the heap operations introduce O(k log n) overhead."
5. **Decode-first discipline is the status quo in chunked-prefill engines**: "since decode requests are typically scheduled first to preserve generation continuity, only the residual token budget in each round is available to prefill requests."
6. **Static token budgets are too rigid** (argues for target-time budgets): "Prior studies have shown that fixed chunk sizes or static token budgets are often too rigid for online LLM serving, because actual execution latency varies substantially with many reasons, such as batch composition, context length, key-value cache usage, and request concurrency."
7. LPRS: "we propose the Latency-prediction-based request scheduling (LPRS) mechanism. The key idea is to shift the scheduling objective from maximizing token filling to minimizing deviation from a target latency budget... the problem is how to choose a chunk such that the actual execution time remains close to a target latency budget."
8. **Anti-fragmentation (APC) — budget dilution and micro-progress**: "budget dilution, where the remaining token budget (Bmax − Ut) is divided among too many requests, resulting in highly fragmented, sub-optimal chunk sizes; and 2) micro-progress, where requests are allocated negligible chunk sizes (e.g., a single token), trivially maintaining their active status while inflating the total number of scheduling rounds." The active-prefill cap: "Ct = min(Cmax, Smax − |Dt|, (Bmax − Ut)/Lmin)" with Lmin = "the minimum effective chunk size for each active prefill"; decision rule keeps "a small number of meaningful prefills active, rather than allowing a large number of low-quality fragmented chunks to occupy the active set for a long time."
9. Results: "the aging policy reduces mean end-to-end latency by over 10% compared to FCFS. Moreover, LPRS and APC significantly reduce P99 tail latency and suppress prefill fragmentation."

---

## 6. Sarathi-Serve summary — "Efficient LLM Inference via Chunked Prefills" (ACM SIGOPS OSR 59(1))

**Title**: Efficient LLM Inference via Chunked Prefills
**Authors**: Arney Agrawal, Nitin Kedia, Ashish Panwar, Jayashree Mohan, Nipun Kwatra, Bhargav S. Gulavani, Alexey Tumanov, Ramachandran Ramjee (Microsoft Research India / Georgia Tech) — the Sarathi-Serve authors
**Year/Venue**: 2025, ACM SIGOPS Operating Systems Review 59(1), pp. 9-16 (DOI 10.1145/3759441.3759444); accepted manuscript via NSF PAR
**URL**: https://par.nsf.gov/biblio/10656320

Key insights (quotes in VERIFIED §V6):

1. This is the canonical **chunked prefill + stall-free batching** reference (Sarathi-Serve) — the exact technique family our contest rewards.
2. The fundamental tradeoff: "Prefill-prioritizing schedulers improve throughput but introduce significant latency jitter (generation stalls) by interfering with ongoing decodes. Conversely, decode-prioritizing schedulers maintain low latency but underutilize GPU resources, resulting in low throughput"; conclusion: "the former category is better at optimizing throughput whereas the latter is better at optimizing TBT latency."
3. Generation stalls are the killer: "A long prefill computation, potentially taking hundreds of milliseconds or even seconds, blocks the decode steps of other requests in the batch, causing a spike in their TBT." And: "Conventional metrics like average Time Per Output Token (TPOT) or Normalized Latency often mask these stalls due to normalization, providing a misleading picture of user experience." (Warning for us: our TPOT is a *mean* — spikes within a request still affect the mean.)
4. **Chunk size is a token-budget knob tied to the SLO**: "it carefully sizes the prefill chunk(s) added to a batch such that the total compute load fits within a target iteration time (token budget), determined by the application's TBT SLO"; concrete settings: "chunked prefills divide the prompt into multiple, smaller chunks (e.g., 512 or 2048 tokens)"; "We use token budget of 2048 and 512 for all models under the relaxed and strict settings, respectively"; and the policy: "we use a strict token budget and split prompts into smaller chunks when operating under strict latency SLO. This reduces system efficiency marginally but allows us to achieve lower tail latency. On the other hand, when the latency constraint is relaxed, we increase the token budget to allow more efficient prefills."
5. Why chunking works: "First, Prefill computation often saturates GPU compute capacity even with moderately sized inputs (e.g., a few hundred tokens). Second, contrary to earlier assumptions, the overhead of chunking (e.g., repeated KV cache reads for attention) is often minimal, especially for models employing optimizations like Multi-Query/Grouped-Query Attention (MQA/GQA)." (In our problem the analog is: per-chunk overhead is exactly the setup cost S — the paper's "minimal overhead" claim does NOT transfer; S is ours to pay per chunk.)
6. Stall-free batching = "Crucially, it does not pause ongoing decodes to execute prefills. Instead, it carefully sizes the prefill chunk(s) added to a batch such that the total compute load fits within a target iteration time."
7. Results: "Sarathi-Serve can sustain up to 4.0x higher load compared to Orca and 3.7x higher load than vLLM under strict SLO (Yi-34B, openchacsharegpt4)"; "Sarathi-Serve achieves 3.5x higher capacity compared to vLLM under strict SLO (100ms, Mistral-7B) using a small token budget of 512. For scenarios with more relaxed SLO constraints, picking a larger token budget of 2048 allows Sarathi-Serve to operate more efficiently resulting in 1.65x higher capacity compared to vLLM (1s, Yi-34B)"; "Sarathi-Serve improves the serving capacity of Mistral-7B by up to 2.6x on a single A100 GPU and up to 5.6x for Falcon-180B on 8 A100 GPUs."
8. SLO definition anchor: "we define the SLO on P99 TBT to be equal to 5x and 25x the execution time of a decode iteration for a request (with prefill length of 4k and 32 batch size) running without any prefill interference for the strict and relaxed settings, respectively." (Useful concrete anchor: strict ≈ 5 decode iterations, relaxed ≈ 25 decode iterations.)

---

## MAPPING TO OUR PROBLEM

Our problem: 1 local computer + K identical clouds, FIFO UP/DOWN queues, setup cost S per task (per prefill chunk, per decode group), chunkable cloud prefill over num_layers layers, batched decode with task-table durations (decreasing marginal), score = 1000·(w_tp·clamp(tp; tp_base, tp_UB) + w_c·clamp(dist; dist_base, 0)), tp = total tokens/makespan, dist = combined TDR/TPOT target violation. The papers above map as follows. **Each recommendation states the assumption it rests on; verify against the live statement before implementing.**

**Context flag (verify with the statement, Day 0 priority)**: In our model each cloud runs one task at a time and P PROC is chunkable but never batched with other prefills. The Sarathi-style benefit — prefill chunks piggybacking *decode* groups on the same cloud — is real in our model *only if* the statement permits a prefill chunk and a decode group to occupy a cloud in an interleaved/co-scheduled manner (i.e., the scheduler may place a decode group between two chunks of the same request). Chunking a request's prefill into pieces with no interleaving is a pure loss of K·S.
The zoo cases `zero_S` and `huge_S` only make sense if chunking has a real purpose — the purpose is (a) bounding the interruption to decode groups (TPOT) and (b) letting a long prefill yield the cloud between chunks. Confirm this reading of the protocol before tuning chunk sizes.

1. **Chunk size = a TPOT-budget knob, not a throughput knob** (TaiChi §V4, OSR §V6). Choose chunk boundaries so that the *time* a prefill chunk can delay decode groups is bounded: chunk_time ≈ max(2·S, TPOT_guard) where TPOT_guard derives from SLO2 (TaiChi's interference-intensity bound: keep prefill-tokens-per-output-token below a threshold; their measured anchor: 273.77 prefill tokens per output token ↔ 100 ms TPOT). Chunking beyond need only adds S; chunking less than need lets long prefills spike TPOT. Sarathi's strict/relaxed split (token budget 512 vs 2048) shows the tuning range is ~4× around a nominal value; our equivalent nominal: chunk_dur ≈ 5× decode-iteration time under strict TPOT (their SLO anchor).

2. **The "smallest chunk that still satisfies the latency target" rule** (TaiChi §V4.3) is directly applicable: with S fixed per chunk, the per-chunk overhead fraction is S/chunk_time; choose the smallest chunk that (i) keeps per-chunk overhead ≤ some threshold (e.g., chunk_time ≥ 2·S so amortized overhead ≤ 33%), and (ii) keeps the *prefill's own* completion (TDR) within SLO1 headroom. If w_c >> w_tp (TDR-heavy), lean coarse; if w_tp >> w_c (throughput-heavy) but SLO2 is strict, lean fine. Enumerate candidate boundaries as integer layer counts: chunk_count = clamp(round(prefill_proc(Lin)/quantum), 1, num_layers), quantum = max(2·S, SLO2-derived guard). This gives the A/B axis for Step 7 (Prefill Chunking) of the plan.

3. **Decode batching: pure marginal-benefit on the task table + S amortization** (Orca §V2.4, vLLM §V1.2). "No maximum group size exists" in our spec, so the only limiters are the table's diminishing returns and the TPOT bound: per Orca, "as the batch size becomes larger, the amount of return (i.e., increase in throughput) diminishes" — compute decode_proc(m) for m=1.. and add members while the marginal per-token cost drops faster than the TPOT budget allows. Additionally, S is paid once per group, so batching amortizes S across members (Orca's weight-amortization rationale, vLLM §V1.2: "the overhead of moving weights is amortized across the requests in a batch"). When batch size 1 costs decode_proc(1)+S and batch m costs decode_proc(m)+S, the break-even is the classic fixed-cost amortization — exactly what the task table makes computable offline.

4. **Decode-first with residual capacity for prefill** (2606.09061 §V5.5, OSR §V6.6): when a cloud is free at frame t, serve decode groups before starting new prefill chunks; a prefill chunk is the default fill only when no decode group can be formed. Rationale: decode continuity is what TPOT measures, and interrupted decode groups are irreversible. This is Sarathi's stall-free discipline ("it does not pause ongoing decodes to execute prefills"). BUT if w_c is small and w_tp dominates (pure throughput), prefill-priority (start all ready prefills first) is the correct opposite regime (OSR §V6.2: prefill-prioritizing is "better at optimizing throughput"); the w_c/w_tp ratio decides, matching TaiChi's regime table (aggregation ↔ tight TTFT, disaggregation ↔ tight TPOT — we are structurally aggregation, so our SLO2 risk is interference-driven).

5. **Aging priority for prefill start order instead of FCFS** (2606.09061 §V5.2): priority = α·(t − a_i) − |β|·remaining_prefill_work; α/|β| tuned by the score weights (w_c-heavy → larger α/|β| for starvation safety; tp-heavy → smaller). Rationale: pure FCFS lets a long prefill at the head of the queue starve short ones repeatedly ("it may repeatedly consume the residual prefill budget across multiple rounds"), and pure shortest-first starves long requests. Aging is O(k log n) with a max-heap and one knob. In our protocol, this orders which P PRE-ready request starts when a cloud frees. Note: our per-frame decision has no fine-grained rounds, so apply the aging score at each frame to the ready set.

6. **TPOT-budget-aware decode group composition** (TaiChi §V4.7): choose which requests share a decode group with the longest-output-first logic — requests that have already produced many tokens have "the greatest remaining TPOT budget" and "can better absorb performance degradation". Since we don't know Lout until FIN (TaiChi: "the output length of a request is unknown in advance"; vLLM: "output lengths are not known a priori"), use the produced-so-far proxy (tokens_produced) or the current measured TPOT per request. Batch long-produced requests into the large groups that give better S amortization; keep young requests in small groups so their early TPOT is clean.

7. **Latency shifting on slack requests** (TaiChi §V4.6): when the score formula is known (Day 4+), implement targeted degradation: requests far from their TDR/TPOT violation bound can tolerate chunk delays / bigger groups; requests near violation get priority. TaiChi's decision rule is "estimate projected TTFT (queuing + execution) and degrade only if the total remains within the SLO" — our analog: delay a prefill chunk only if its projected P POST completion still meets SLO1; migrate decode-group membership only if the projected TPOT stays under SLO2. This is where the w_tp/w_c weights become a per-request urgency function.

8. **Avoid fragmentation and micro-progress** (2606.09061 §V5.8): never schedule a prefill chunk whose duration is a negligible fraction of the round (their "micro-progress" anti-pattern; ours: chunk_time << S is doubly wasteful since S ≥ 0 adds cost per chunk), and do not spread a token budget across too many concurrent prefills ("budget dilution" → "highly fragmented, sub-optimal chunk sizes"). In our sequential-cloud model the analog is: per cloud, at most one prefill chunk at a time, and no chunk smaller than the minimum effective size Lmin ≈ max(2·S, guard). Also do not start every ready prefill at once in a burst (all K clouds + local saturating the UP FIFO is our "transfer_burst" zoo trap — vLLM's preempt-latest policy has no analog since we cannot preempt, so the equivalent is *admission pacing*: spread prefill starts over frames).

9. **Cloud assignment as constraint-satisfaction/least-loaded, adaptive** (PerLLM §V3.4, V3.6): PerLLM's "ensure that the processing time requirement of each server is met. Otherwise, it is assigned to a more resource-rich server" maps to: at ARR, assign the request to the cloud whose projected prefill+decode load best meets its TDR target (all clouds identical in our spec, so this reduces to least-loaded / earliest-free with a per-request TDR urgency term). PerLLM's UCB machinery is overkill for identical clouds, but its *constraint-first* selection (never violate the time requirement if a better assignment exists) is the right rule for Step 5 (Cloud Assignment) A/B: score candidate cloud = earliest_free_time + predicted_pipeline_time, pick min; break ties by queue depth.

10. **Global freeze-frame principle**: every regime choice above (chunk quantum, group-size cap, α/|β|, prefill-vs-decode priority) is a *config* determined offline per contest input from (S, K, task table, w_tp, w_c, SLO1, SLO2, tp_base, tp_UB) — TaiChi's "optimal configuration... can be determined via offline search" with re-search only on significant workload changes. Given we see the whole input at startup (task table, weights, SLOs), do a small offline sweep over the few config knobs *before* the event loop starts, using the exact task-table arithmetic; do not tune online.

**Open questions for the human (resolve before Day 1 coding)**:

- Q1: May a prefill chunk and a decode group share/alternate on one cloud between two event frames, i.e., is chunking's purpose to bound decode interruption (Sarathi/TaiChi model), or is P PROC truly atomic per cloud with chunking useless except for S? (Determines whether Step 7 exists at all.)
- Q2: Is D PROC's batch duration decode_proc(member count) paid per cloud with S per group per cloud, or per logical group? (Determines batching cost arithmetic.)
- Q3: Does the DOWN of a decode group occur after D POST (which we schedule), i.e., do we control the interval between consecutive tokens of a request directly via group scheduling? (Determines whether TPOT is purely a function of our group schedule.)
- Q4: Exact formula of `dist` (TDR/TPOT violation combination) — needed before any w_c/w_tp weighting (Day 4+).

---

## VERIFIED — exact quotes from .txt extracts (every claim above)

### V1. vLLM (research/papers/2309.06180.txt)

- "vLLM improves the throughput of popular LLMs by 2-4× with the same level of latency compared to the state-of-the-art systems, such as FasterTransformer and Orca." (abstract, lines 38-41)
- "the overhead of moving weights is amortized across the requests in a batch, and can be overwhelmed by the computational overhead when the batch size is sufficiently large" (lines 328-330)
- "the requests may arrive at different times. A naive batching strategy would either make earlier requests wait for later ones or delay the incoming requests until earlier ones finish, leading to significant queueing delays. Second, the requests may have vastly different input and output lengths" (lines 331-335)
- "After each iteration, completed requests are removed from the batch, and new ones are added. Therefore, a new request can be processed after waiting for a single iteration, not waiting for the entire batch to complete." (lines 340-342)
- "The computation at different iterations cannot be parallelized due to the data dependency and often uses matrix-vector multiplication, which is less efficient. As a result, this phase severely underutilizes GPU computation and becomes memory-bound, being responsible for most portion of the latency of a single request." (lines 292-296)
- "the computation of the prompt phase can be parallelized using matrix-matrix multiplication operations. Therefore, this phase can efficiently use the parallelism inherent in GPUs." (lines 273-274)
- "our profiling results in Fig. 2 show that only 20.4% - 38.2% of the KV cache memory is used to store the actual token states in the existing systems." (lines 253-256)
- "In vLLM, we adopt the first-come-first-serve (FCFS) scheduling policy for all requests, ensuring fairness and preventing starvation. When vLLM needs to preempt requests, it ensures that the earliest arrived requests are served first and the latest requests are preempted first." (lines 1327-1332)
- "the resulting output lengths are not known a priori, contingent on both the input prompt and the model." (lines 1334-1336)

### V2. Orca (research/papers/osdi22-yu.txt)

- "we propose iteration-level scheduling, a new scheduling mechanism that schedules execution at the granularity of iteration (instead of request)" (lines 39-40)
- "we suggest selective batching, which applies batching only to a selected set of operations." (lines 43-44)
- "Our evaluation on a GPT-3 175B model shows that ORCA can significantly outperform NVIDIA FasterTransformer in terms of both latency and throughput: 36.9× throughput improvement at the same level of latency." (lines 48-50)
- "the engine's throughput is highly dependent on the batch size, i.e., the number of inference requests the engine processes together. Reusing the model parameters loaded from off-chip memory is another merit in batched execution" (lines 450-453)
- "For a newly arrived request, the request gets a chance to start processing (i.e., the scheduler may select the new request to run next) after execution of the currently scheduled iteration, significantly reducing the queueing delay." (lines 589-593)
- "We design the ORCA scheduler to use a simple algorithm that does not change the processing order of client requests; early-arrived requests are processed earlier. That is, we ensure iteration-level first-come-first-served (FCFS) property." (lines 969-971)
- "for any pair of requests (xi , x j ) in the request pool, if xi has arrived earlier than x j , xi should have run the same or more iterations than x j ." (lines 973-975)
- "Increasing the batch size trades off increased throughput for increased latency, but as the batch size becomes larger, the amount of return (i.e., increase in throughput) diminishes." (lines 979-982)
- "ORCA also has a notion of a max batch size: the largest possible number of requests within a batch. The ORCA system operator can tune this knob to maximize throughput while satisfying one's latency budget." (lines 982-986)
- "it selects at most 'max batch size' requests based on the arrival time, while reserving enough space for storing keys and values to a request when the request is scheduled for the first time." (lines 1000-1003)
- "FasterTransformer provides a throughput of 0.185 req/s whereas ORCA provides a throughput of 6.81 req/s, which is a 36.9× speedup." (lines 1649-1651)

### V3. PerLLM (research/papers/2405.14636.txt)

- "it is difficult for bandwidth-constrained cloud servers to simultaneously process massive LLM services in real-time." (lines 14-15)
- "we integrate the upper confidence bound algorithm based on the constraint satisfaction mechanism in PerLLM." (lines 22-23)
- "PerLLM achieves 2.2×, 2.1×, and 1.6× throughput and reduces the energy cost by more than 50%." (lines 28-29)
- "we model the complex edge-cloud collaboration process as a combinatorial multi-armed bandit problem and propose a constraint satisfaction upper confidence bound algorithm for efficient service scheduling and resource allocation." (lines 121-123)
- "The constraint C1 is used to ensure that the processing time requirement of each server is met. Otherwise, it is assigned to a more resource-rich server." (lines 474-475)
- "Constraint C4 ensures that only one server can be selected for each service in time slot t." (lines 478-479)

### V4. TaiChi (research/papers/2508.01989.txt)

- "PD aggregation is optimal under tight time-to-first-token (TTFT) and relaxed time-per-output-token (TPOT), while PD disaggregation excels under strict TPOT and relaxed TTFT. However, under balanced TTFT and TPOT SLOs, neither approach can deliver optimal goodput." (lines 38-43)
- "PD aggregation (like Orca [32] and Sarathi-Serve [3]) co-locates the prefill and decode phases of a request on the same hardware instance to achieve high resource utilization." (lines 87-89)
- "we strategically degrade the latency of requests already meeting SLO constraints, thereby reallocating overprovisioned GPU resources (i.e., GPU time) to prioritize SLO-violating requests through scheduling." (lines 110-112)
- "since the output length of a request is unknown in advance, the scheduler lacks the necessary information to make precise, per-request degradation decisions." (lines 130-132)
- "we employ a longest-first approach, which selects the request with the current longest output in the D-heavy instance for degradation, as it has the greatest remaining TPOT budget currently and can better absorb performance degradation. Finally, to prevent over-degradation, we monitor the TPOT of migrated requests in real time. Once the TPOT approaches the SLO constraint, the request is flowed back to a D-heavy instance to preserve service quality." (lines 233-240)
- "we propose a length-aware prefill scheduling strategy, which selectively degrades TTFT by assigning short prefill requests to slower instances when doing so does not violate SLO constraints." (lines 241-243)
- "we estimate its projected TTFT on each D-heavy instance by summing the expected queuing delay and degraded execution time. If this total remains within the TTFT SLO, the request is marked as degradable and scheduled accordingly." (lines 247-249)
- "TaiChi improves goodput by up to 77% over SOTA systems. It also reduces TTFT by up to 13.2× and TPOT by up to 1.69×, relative to PD disaggregation and PD aggregation, respectively." (lines 251-253)
- "Sarathi-Serve proposes chunked prefill, which divides prefill into smaller chunks that are piggybacked in decode batches as additional computation." (lines 387-389)
- "The high TPOT in PD aggregation arises from prefill interference due to computation-bound linear operations, with a strong linear relationship between interference intensity and TPOT." (lines 561-563)
- "Figure 4 reveals a strong linear correlation between TPOT and interference intensity, as evidenced by the fitted line's R-squared value of 0.99." (lines 577-578)
- "if we aim to keep TPOT below 100 ms, we must limit interference intensity to fewer than 273.77 prefill tokens per output token." (lines 546-548)
- "although smaller chunk sizes reduce interference by limiting the maximum interference per decode token (or batch), they are not always optimal... chunk sizes below 1024 (e.g., 128, 256, 512) constrain TPOT but result in prohibitively high TTFT... Thus, the optimal configuration should adopt the smallest chunk size that still satisfies the TTFT constraint." (lines 549-555; ellipses join lines 549-551 and 554-555)
- "The high TTFT in PD disaggregation stems from request queuing, which occurs because PD disaggregation offers lower prefill processing capacity compared to PD aggregation." (lines 585-587)
- "Larger chunk sizes improve an instance's prefill throughput, which benefits TTFT, but also increase prefill-decode interference, thereby degrading TPOT (§ 2.3.1)." (lines 1067-1068)
- "the optimal configuration for a given workload and SLO can be determined via offline search, following approaches from prior work [3, 19, 36]." (lines 1076-1077)
- "our system adopts an on-demand search-and-reconfigure strategy like DistServe, which triggers a new search and reconfiguration only upon significant workload changes." (lines 1089-1091)

### V5. Fairness chunked-prefill (research/papers/2606.09061.txt)

- "once a long prompt reaches the head of the queue, it may repeatedly consume the residual prefill budget across multiple rounds. Short prompts arriving behind it can therefore experience long TTFT even though their prefill cost is small." (lines 211-213)
- "a shortest-prefill-first policy improves the responsiveness of short requests, but it can indefinitely delay long requests when short requests keep arriving." (lines 214-215)
- "since decode requests are typically scheduled first to preserve generation continuity, only the residual token budget in each round is available to prefill requests." (lines 207-209)
- "Pi (n) = α · (t − ai ) + β · ri (n)" with "α > 0 is the aging weight, and β < 0 is the remaining-work weight. The first term α(t − ai ) increases with the waiting time of a request. It gives older requests progressively higher priority and prevents them from being permanently dominated by newly arrived short requests. The second term βri (n) favors requests with less remaining prefill work" (lines 229-239)
- "When this ratio is small, the remaining-work term dominates the priority function. The scheduler behaves closer to the shortest-prefill-first policy, so it can give priority to short requests, and thus can reduce TTFT for latency-sensitive workloads. 2) When α/|β| is large, the waiting-time becomes more influential. So long requests can accumulate priority more quickly and eventually overtake newly arrived short requests, thus reducing starvation risk under sustained mixed arrivals." (lines 259-264)
- "The waiting prefill queue is implemented as a max-heap ordered by Ki (n)." (line 299); "If the queue contains n requests, and k requests are selected in a scheduling round, the heap operations introduce O(k log n) overhead." (lines 303-305)
- "fixed chunk sizes or static token budgets are often too rigid for online LLM serving, because actual execution latency varies substantially with many reasons, such as batch composition, context length, key-value cache usage, and request concurrency." (lines 150-152)
- "the problem is how to choose a chunk such that the actual execution time remains close to a target latency budget." (lines 327-328)
- "we propose the Latency-prediction-based request scheduling (LPRS) mechanism. The key idea is to shift the scheduling objective from maximizing token filling to minimizing deviation from a target latency budget." (lines 329-331)
- "budget dilution, where the remaining token budget (Bmax − Ut ) is divided among too many requests, resulting in highly fragmented, sub-optimal chunk sizes; and 2) micro-progress, where requests are allocated negligible chunk sizes (e.g., a single token), trivially maintaining their active status while inflating the total number of scheduling rounds." (lines 525-530)
- "Ct = min(Cmax , Smax − |Dt |, (Bmax − Ut )/Lmin)" (line 543, with Lmin = "the minimum effective chunk size for each active prefill", line 604)
- "APC favors keeping a small number of meaningful prefills active, rather than allowing a large number of low-quality fragmented chunks to occupy the active set for a long time." (lines 634-635)
- "the aging policy reduces mean end-to-end latency by over 10% compared to FCFS. Moreover, LPRS and APC significantly reduce P99 tail latency and suppress prefill fragmentation" (lines 30-32)

### V6. Sarathi-Serve OSR (research/papers/agrawal-chunked-prefills-osr.txt)

- "Prefill-prioritizing schedulers improve throughput but introduce significant latency jitter (generation stalls) by interfering with ongoing decodes. Conversely, decode-prioritizing schedulers maintain low latency but underutilize GPU resources, resulting in low throughput." (abstract, lines 25-29)
- "This paper summarizes the approach presented in Sarathi-Serve [8, 9], which employs chunked prefills and stall-free batching to mitigate this tradeoff." (lines 98-99)
- "the former category is better at optimizing throughput whereas the latter is better at optimizing TBT latency." (lines 928-930)
- "A long prefill computation, potentially taking hundreds of milliseconds or even seconds, blocks the decode steps of other requests in the batch, causing a spike in their TBT." (lines 380-381)
- "Conventional metrics like average Time Per Output Token (TPOT) or Normalized Latency often mask these stalls due to normalization, providing a misleading picture of user experience." (lines 384-385)
- "chunked prefills divide the prompt into multiple, smaller chunks (e.g., 512 or 2048 tokens) . Each chunk is processed in a separate iteration." (lines 451-453)
- "Prefill computation often saturates GPU compute capacity even with moderately sized inputs (e.g., a few hundred tokens)." (lines 454-455)
- "contrary to earlier assumptions, the overhead of chunking (e.g., repeated KV cache reads for attention) is often minimal, especially for models employing optimizations like Multi-Query/Grouped-Query Attention (MQA/GQA)" (lines 455-457)
- "it carefully sizes the prefill chunk(s) added to a batch such that the total compute load fits within a target iteration time (token budget), determined by the application's TBT SLO." (lines 401-403)
- "Crucially, it does not pause ongoing decodes to execute prefills." (line 400)
- "we use a strict token budget and split prompts into smaller chunks when operating under strict latency SLO. This reduces system efficiency marginally but allows us to achieve lower tail latency. On the other hand, when the latency constraint is relaxed, we increase the token budget to allow more efficient prefills." (lines 650-654)
- "We use token budget of 2048 and 512 for all models under the relaxed and strict settings, respectively" (lines 655-656)
- "we define the SLO on P99 TBT to be equal to 5x and 25x the execution time of a decode iteration for a request (with prefill length of 4k and 32 batch size) running without any prefill interference for the strict and relaxed settings, respectively." (lines 732-735)
- "Sarathi-Serve can sustain up to 4.0 x higher load compared to Orca and 3.7 x higher load than vLLM under strict SLO (Yi-34B, openchacsharegpt4)." (lines 749-750)
- "Sarathi-Serve achieves 3.5 x higher capacity compared to vLLM under strict SLO (100ms, Mistral-7B) using a small token budget of 512. For scenarios with more relaxed SLO constraints, picking a larger token budget of 2048 allows Sarathi-Serve to operate more efficiently resulting in 1.65 x higher capacity compared to vLLM (1s, Yi-34B)." (lines 915-919)
- "Sarathi-Serve improves the serving capacity of Mistral-7B by up to 2.6 x on a single A100 GPU and up to 5.6 x for Falcon-180B on 8 A100 GPUs." (lines 937-938)

---

## Cross-cutting summary (what to take into the solver)

- **The one decision that dominates**: chunk quantum (S vs TPOT trade) — see recommendation 1/2; Sarathi anchor 512 vs 2048 tokens, TaiChi rule "smallest chunk that still satisfies TTFT".
- **The second decision**: decode group size = Orca's max-batch knob; diminishing returns + S amortization make it computable from the task table.
- **The third decision**: prefill ordering (FCFS vs aging vs shortest-first) with α/|β| as the tunable; plus decode-first vs prefill-first regime chosen by w_c vs w_tp.
- **Cross-check for the zoo**: `zero_S` ⇒ fine chunks, heavy interleave (chunking is free); `huge_S` ⇒ 1 chunk (chunking dominated by S); `tpot_sensitive` ⇒ small quantum + decode-first + longest-first groups; `tdr_sensitive` ⇒ coarse quantum + prefill-priority; `transfer_burst` ⇒ admission pacing of prefill starts (do not start all ready prefills at once).
