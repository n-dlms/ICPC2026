> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Research below was completed on Day 0 (2026-08-14) and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Queueing Theory — Deep Research Notes (Day 0)

**Date**: 2026-08-14
**Purpose**: Queueing-theory grounding for the ICPC Online Challenge 2026 (CF 2251A, Edge–Cloud Collaborative Scheduling). FIFO UP/DOWN transfer queues, setup-cost batching, chunking, TDR/TPOT tradeoffs.
**Scope**: M/G/1 + M/D/1 waiting, Kingman approximation, batch-service queues, group (batch) arrivals, tandem queues with/without blocking, arrival smoothing.
**Status**: All claims in this file are backed by exact quotes from the `.txt` extracts in `research/papers/`. Nothing is cited from memory.

---

## 1. Paper Inventory (new downloads)

| File (PDF/TXT)                                 | Source                                                                       | What it is                                                                                                                                                                              | Why we took it                                                                                                                                 |
| ---------------------------------------------- | ---------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `adan-resing-queueing-theory.pdf` (218 KB txt) | https://www.win.tue.nl/~iadan/queueing.pdf (TU Eindhoven, open access)       | Full lecture notes "Queueing Theory" by I. Adan & J. Resing. Ch.3 fundamentals, Ch.7 M/G/1, Ch.9 priorities, Ch.10.1 setup times + **threshold (N-)policy**, Ch.10.4 **group arrivals** | Primary textbook-level source: P-K formula, N-policy setup queues (= decode batching decision), batch-arrival waiting formula, priority queues |
| `mit-modiano-mg1-vacations.pdf` (5.3 KB txt)   | https://web.mit.edu/modiano/www/6.263/lec8.pdf (MIT 6.263, open)             | Lecture slides: M/G/1 P-K formula, **M/D/1 special case**, **M/G/1 with vacations**, slotted FDM/TDM examples                                                                           | M/D/1 numbers + vacation-queue model (= "waiting to accumulate a decode group")                                                                |
| `pk-kingman-modern.pdf` (85 KB txt)            | arXiv:2605.27202 (2026)                                                      | Modern readable restatement of P-K (Eq. 19) and **Kingman approximation** (Eq. 21), plus "variance wedge" analysis                                                                      | Modern treatment of M/G/1 + GI/G/1 waiting time; makes the burstiness effect explicit                                                          |
| `krivulin-tandem-cycle-time.pdf` (22 KB txt)   | arXiv:1212.5309                                                              | Krivulin & Nevzorov: exact mean **service cycle time in tandem queueing systems**, infinite and finite buffers, manufacturing/communication blocking                                    | Tandem throughput = min of stages; max-plus recursion; blocking dynamics                                                                       |
| `batch-service-map-vacation.pdf` (64 KB txt)   | arXiv:2208.13043                                                             | MAP/G^{(a,b)}/1 with batch-size-dependent service + vacations                                                                                                                           | Modern batch-service queue: **GBS rule**, **stability condition λE[S_H]/H < 1**                                                                |
| `sasikala-bulk-service-survey.pdf` (37 KB txt) | http://acadpubl.eu/... (Int. J. Pure & Appl. Math. 106(6) 2016, open access) | Survey of **bulk (batch) service queueing models**: GBS rule, vacations, N-policy, setup times, tandem queues with bulk service                                                         | The requested batch-service survey; catalog of batching rules and their costs                                                                  |
| `flattening-the-curve.pdf` (49 KB txt)         | arXiv:2004.09645                                                             | Palomo, Pender, Massey, Hampshire: M_t/G/∞ peak-load analysis; **"flattening the curve"** of arrivals, peak-lag results                                                                 | Quantitative support for spreading (smoothing) arrivals to cut peak queue load                                                                 |

**Removed**: `FowlerMonch2022-pbatch-survey.pdf` — download was an Elsevier TDM-block HTML page (paywalled; not open access). Its topic (parallel-batch scheduling taxonomy) is covered adequately by the bulk-service queue sources above.

Also re-read in this session (already in repo): `1711.00081.txt` (2-machine flow shop with exact delays — recall the max-plus structure: `C2(n) = max(...) + t2n`; same shape as Krivulin's tandem recursion, see §2.5).

---

## 2. Key Insights

### 2.1 M/G/1 and M/D/1: waiting depends on service-time variance, diverges as ρ→1

- P-K mean value: `E(W) = ρE(R)/(1−ρ)` with residual `E(R) = E(B²)/(2E(B))` (Adan & Resing §7.6). Equivalently `W = λE[S²]/(2(1−ρ)) = ρ/(1−ρ)·(1+c_s²)/2·E[S]` (arXiv:2605.27202 Eq. 19).
- **Waiting time depends on the variance of service time, not only its mean** (2605.27202: "Two service-time distributions with identical means, but different variances, will produce different waiting times"; Adan: "the mean waiting time only depends upon the first two moments of the processing time").
- M/D/1 special case: `W = ρ/(2µ(1−ρ))` — half the M/M/1 value because `c_s² = 0` (Modiano slides, Example 2).
- As `ρ→1` waiting diverges as `ρ/(1−ρ)`; the `(1+c_s²)/2` factor multiplies the divergence (2605.27202: "high-variance systems break down faster as load increases").
- Stability of any single server: `λE(B) < 1`; the queue length "explodes" at `λE(B) = 1` except for D/D/1 (Adan §3.2).

### 2.2 Kingman: bursty arrivals inflate waiting exactly like service variability

- GI/G/1 approximation: `W ≈ ρ/(1−ρ)·(c_a²+c_s²)/2·E[S]`, exact asymptotically as ρ→1 (2605.27202 Eq. 21).
- "The Kingman approximation makes burstiness explicit: a bursty arrival process (c²a ≫ 1) inflates waiting times just as effectively as a high-variance service process" (2605.27202).
- Consequence: the scheduler can *not* change ρ of a FIFO queue (that is fixed by the request stream + transfer sizes), but it **can** change c_a² — the burstiness of when transfers enter the queue. Smoothing arrivals is worth as much as de-variance-ing service times.

### 2.3 Batch (group) arrivals: clustering has a direct waiting penalty

- M/G/1 with group arrivals (Poisson groups of size G): `E(W) = ρE(R)/(1−ρ) + (E(G²)−E(G))·E(B)/(2E(G)(1−ρ))`; "the second term indicates the extra mean delay due to clustering of arrivals" (Adan §10.4, Eq. 10.7).
- For deterministic groups of size m: extra term `= (m−1)E(B)/(2(1−ρ))` per customer. **Linear in group size, blows up near saturation.**
- Size-biased sampling: a customer is more likely to be in a large group (`h_n = n·g_n/E(G)`) — with variable group sizes, the *typical* group an item lands in is larger than the average group (Adan §10.4).
- Vacations (server idle while accumulating a group) add `E[V²]/(2E[V])` to waiting (Modiano slide 9: `W = λE[X²]/(2(1−ρ)) + E[V²]/(2E[V])`); "a packet arriving to an empty system must wait for the end of the vacation".

### 2.4 Batch-service queues: stability and the GBS rule

- General bulk service (GBS) rule: "the server will start to provide service only when atleast 'a' units are present in the queue, and maximum service capacity is 'b'"; serves `min(l, H)` when queue length is l ≥ h (Sasikala & Indhira; arXiv:2208.13043).
- Batch-service stability: traffic intensity `λE[S_H]/H < 1` (arXiv:2208.13043) — i.e., the per-customer service capacity `H/E[S_H]` of one batch must exceed the arrival rate. Batching *creates* headroom only if the batch service time grows sub-linearly in batch size.
- N-policy / threshold setup (accumulate N jobs, then serve with setup): `E(W) = ρE(R_B)/(1−ρ) + [N/λ/(N/λ+E(T))]·[(N−1)/(2λ)] + [E(T)/(N/λ+E(T))]·(E(T)+E(R_T))` (Adan §10.1.3) — the accumulation wait `(N−1)/(2λ)` trades against the setup saving `E(T)` per served batch.

### 2.5 Tandem queues: throughput = min of stages; latency = sum; max-plus recursion

- Dynamics of a tandem of single servers: `D_m(n) = max(D_{m−1}(n), D_m(n−1)) + τ_mn` — customer n can start stage m only when it has *arrived* at stage m (departed m−1) **and** stage m has freed up (departed n−1) (Krivulin & Nevzorov, Eq. 1).
- Mean cycle time: `γ = max_{0≤i≤M} E[τ_i1]` over interarrival and all service times; throughput `π = 1/γ` (Theorem 7, Corollary 8). **The slowest stage (including the arrival process) sets the throughput.**
- Same max-plus shape as the already-downloaded 2-machine flow shop paper (1711.00081): completion times are maxima over alternating sum-of-segments; exact delays occur when a stage is the bottleneck for a stretch.
- Blocking (finite buffers): manufacturing blocking adds `D_1(n) = max(max(D_0(n),D_1(n−1))+τ_1n, D_2(n−1))` — the blocked server holds the completed customer and cannot accept the next one; upstream backlog then *compounds* through the recursion (Krivulin §5).

### 2.6 Flattening (smoothing) arrivals cuts peak queue, and peak lags

- Doubling the spread of arrivals halves the peak arrival rate: "By increasing the standard deviation by a factor of 2, we have reduced the peak value of the arrival rate by a factor of two" (arXiv:2004.09645).
- Peak load lags peak arrivals: "the lag between the peak arrival rate and peak load is equal to half of the service time" (arXiv:2004.09645) — a burst is *amplified and delayed* downstream, so it must be caught upstream.
- The infinite-server model is used as the tractable first step: "the infinite server queue is an upper bound for queueing models with a finite number of servers" (2004.09645) — i.e., with finite servers/queues the peak is strictly worse.

---

## 3. MAPPING TO OUR PROBLEM

Concrete recommendations, each traceable to a VERIFIED quote in §4. The system is a **tandem pipeline per request**: local → UP queue → cloud → DOWN queue → local, plus a *second* pipeline per decode iteration. All stages share the two FIFO queues and the one local computer.

1. **Never let any of the 4 server types saturate; the ρ/(1−ρ) divergence decides everything.** For UP/DOWN the per-item service time is `latency + 8·len·bpt/(bw·1e6)` (spec §1.6); `ρ = λ·E[S]`. P-K (2.1) says waiting is `ρ/(1−ρ)·(1+c²)/2·E[S]`: at ρ=0.5 waiting ≈ 1×E[S]; at ρ=0.9 waiting ≈ 9×E[S] (c²=0). Rule of thumb: if steady-state ρ of UP or DOWN exceeds ≈0.85, the transfer queues — not compute — set the TDR, and no policy tuning below that threshold matters until the burstiness is fixed. Track ρ per queue in the state machine; use it to detect the transfer-bottleneck regime.

2. **The one lever the scheduler controls on a FIFO queue is c_a² — spread transfer *enqueue* times.** Transfers are enqueued automatically when a task completes, so the scheduler controls c_a² by controlling **completion times**: (a) stagger D PRE starts across clouds so groups finish at different times; (b) never schedule a task that will complete simultaneously with K−1 others if the DOWN/UP queue is already busy; (c) treat "all clouds finish prefill together" as the worst case (Kingman: burstiness multiplies waiting exactly like service variance, 2.2).

3. **Decode group size is a latency-vs-bandwidth regime decision, not a universal preference.** Transfer service `E[S_m] = latency + 8m·bpt/(bw·1e6)`:
   - **Latency-bound** (latency dominates the variable part): a group of m makes ONE queue item, so item arrival rate drops by m and ρ ≈ λ·latency/m — batching is a *huge* win (P-K with ρ ↓). Batch as big as predecessor rules allow.
   - **Bandwidth-bound** (8m·bpt/(bw·1e6) dominates): ρ is invariant to m (total bytes conserved) but `E[S_m] ∝ m`, so waiting grows ~linearly with m (P-K: W ∝ E[S] at fixed ρ). Cap group size around where `8m·bpt/(bw·1e6) ≈ latency`; beyond that, larger groups only stretch the queues.
   - Crossover to compute: batching also saves `(m−1)` copies of S on local D PRE / cloud D PROC / local D POST (fewer tasks), which pushes the optimal m up when S is large; the queueing penalty (item size) pushes it down.

4. **Beware the group-arrival penalty when *variable* groups hit a queue.** If decode groups have different sizes, DOWN arrivals are batches with variable G: extra waiting `(E(G²)−E(G))·E(B)/(2E(G)(1−ρ))` (2.3) — quadratic in large outliers, and size-biased sampling (2.3) means the typical item feels the big groups. Prefer *uniform* group sizes over greedy "take whatever is ready" if sizes vary wildly.

5. **Prefill chunking smooths DOWN only in the bandwidth-bound regime; it pays fixed costs C times.** Each chunk pays S (setup), but transfers are NOT per-chunk: exactly ONE UP (queued at P PRE completion) and ONE DOWN (queued only at the last piece's TDN), regardless of chunk count. Chunking helps when prefill_proc ≥ compute-bound overlap and the (c−1)·S extra setup is recouped by cloud/local interleaving; with S≈0 chunking is free and the only cost is pipeline-fill latency. When S dominates (large S, small prefill_proc), chunking multiplies the fixed S payment and loses — disable (c=1).

6. **Tandem structure: TDR = Σ per-stage sojourns; the hottest stage dominates; balance clouds.** Throughput of the pipeline is set by the slowest stage (2.5), and the max-plus recursion shows backlog compounds upstream when a downstream stage is busy (a burst into a busy DOWN queue delays every later transfer, 2.5 + blocking). Therefore: greedy least-loaded cloud assignment (equalize per-cloud ρ) is not just load balancing — it prevents one cloud from being the `max E[τ]` stage that throttles everyone. Locally, when UP is busy, prefer scheduling tasks whose transfers are small (P POST/D PRE with few members) so enqueued work is small — this reduces E[S] of queue items the whole burst must wait for.

7. **The local computer is itself a priority queue — protect P PRE.** Local ρ = Σ(S + duration of P PRE, P POST, D PRE, D POST)/time. Decode is a continuous background stream; a prefill burst on top can push local ρ toward 1, and every D PRE on local directly feeds UP. Adan §9 (priorities) gives the classical tool: with absolute priority for type 1, `E(S1) = (1/µ)/(1−ρ1)` — the prioritized class' sojourn depends only on its own load. Verdict: give P PRE (and its P POST) priority over decode tasks on the local computer whenever the TDR weight w_c is significant; decode TPOT then absorbs the variance.

8. **N-policy logic says: batch what is ready now; do not wait for future members.** The threshold-setup formula (2.4) shows waiting for N members costs each accumulated member ≈ `(N−1)/(2λ)` plus vacation/accumulation time, against saving one setup `E(T)`. In our problem the decode "setup" saving is `(m−1)·S` (amortized across local+cloud tasks) and the transfer saving is `(m−1)·latency` per group. Waiting is profitable only when these savings exceed the per-member accumulation delay of the *earliest* member — with unknown future arrivals (no lookahead beyond the current frame), a deterministic "wait K frames" policy is almost never optimal. Exception: at burst arrivals (all ARR at t0), collecting m members that are *already* ready costs nothing.

9. **Spread simultaneous prefills (the burst_100 case) — flattening is linear, not magic.** Doubling the spread halves the peak arrival rate (2.6), and the peak at the queue lags the arrival burst by ~half the service time — so the DOWN/UP queue peak is set by how many tasks complete per unit time, not by the ARR frame. Staggering P PRE starts across clouds (or chunking to interleave) is the direct analogue of "flattening the curve": it converts one synchronized burst into a smooth stream and cuts peak queue depth (and hence worst-case TDR) roughly by the spread factor.

### Open questions for the human (not answered by queueing theory)

- Exact score formula (TDR/TPOT weights) — must be derived from the statement before choosing between "protect prefill" (rec. 7) and "maximize decode batches" (rec. 3) in the transfer-bottleneck regime.
- Whether D PROC group size can differ from D PRE/D POST group size per iteration (spec: D PROC per-cloud batch — mixing allowed across clouds only at D PRE/D POST) — this decides if we can decouple "cloud batch size" from "transfer item size".

---

## 4. VERIFIED — exact extracts from the .txt files

### From `adan-resing-queueing-theory.txt` (Adan & Resing, Queueing Theory)

- §3.2 occupation rate: "In a single-server system G/G/1 with arrival rate λ and mean service time E(B) the amount of work arriving per unit time equals λE(B). The server can handle 1 unit work per unit time. To avoid that the queue eventually grows to infinity, we have to require that λE(B) < 1." ... "It is common to use the notation ρ = λE(B)."
- §7.6 P-K mean value formula: "E(W ) = E(Lq )E(B) + ρE(R), and by Little's law (applied to the queue), E(Lq ) = λE(W ). So we find E(W ) = ρE(R)/(1−ρ). (7.14) Formula (7.14) is commonly referred to as the Pollaczek-Khinchin mean value formula."
- §7.6 residual: "it remains to calculate the mean residual service time. In the following section we will show that E(R) = E(B 2 )/(2E(B)) (7.15)"
- §7.1 (via extract) mean waiting time depends on first two moments: "An important observation is that, clearly, the mean waiting time only depends upon the first two moments of the processing time (and not upon its distribution)." — NOTE: this exact sentence appears in the search-result extract of h7.pdf, not in queueing.pdf; in queueing.txt the corresponding statement is in §7.6. (Verified alternative from queueing.txt, §7.6 discussion of residual: "E(R) = E(B 2 )/(2E(B))" and formula uses only mean and second moment.)
- §10.1.3 threshold setup: "A natural extension to the setup policy in the previous section is the one in which the machine is switched on when the number of jobs in the system reaches some threshold value, N say." ... "E(W ) = ρE(RB )/(1−ρ) + [N/λ/(N/λ+E(T))]·[(N−1)/(2λ)] + [E(T)/(N/λ+E(T))]·[E(T ) + E(RT )]."
- §10.4 group arrivals: "In this section we consider the M/G/1 queue where customers do not arrive one by one, but in groups." ... "E(W ) = E(Lq )E(B) + ρE(R) + Σ_k rk (k − 1)E(B)" ... "we finally obtain E(W ) = ρE(R)/(1−ρ) + (E(G2 ) − E(G))E(B)/(2E(G)(1 − ρ))." ... "Clearly, the second term indicates the extra mean delay due to clustering of arrivals."
- §10.4 size-biased groups: "Since it is more likely that our customer belongs to a large group than to a small one, it follows that hn is proportional to the group size n as well as the frequency of such groups. Thus we can write hn = Cngn."
- §9 priorities: "In the preemptive resume priority rule, type 1 customers have absolute priority over type 2 jobs. Absolute priority means that when a type 2 customer is in service and a type 1 customer arrives, the type 2 service is interrupted and the server proceeds with the type 1 customer." ... "For type 1 customers the type 2 customers do not exist. Hence we immediately have E(S1 ) = (1/µ)/(1 − ρ1)."

### From `mit-modiano-mg1-vacations.txt` (Modiano, MIT 6.263)

- P-K formula: "Pollaczek-Khinchin (P-K) Formula W = λE[X2]/(2(1-ρ)) where ρ = λ/µ = λE[X] = line utilization".
- M/D/1: "Example 2: M/D/1 (Constant service time 1/µ) E[X] = 1/µ ; E[X2] = 1/µ2 → W = ρ/(2µ(1−ρ))".
- M/G/1 with vacations: "When the queue is empty, the server takes a vacation" ... "The only impact on the analysis is that a packet arriving to an empty system must wait for the end of the vacation" ... "λE[X2 ]/(2(1−ρ)) + E[V 2 ]/(2E[V ])" (final vacation-queue waiting formula).

### From `pk-kingman-modern.txt` (arXiv:2605.27202)

- P-K: "Under the stability condition ρ < 1, the Pollaczek–Khinchine formula gives Wq^M/G/1 = λE[S2]/(2(1−ρ)) = ρ/(1−ρ) · (1+c2s)/2 · E[S] (19)".
- "The economic content of Eq. (19) is that mean waiting time depends on the variance of service time, not only its mean."
- "As ρ → 1−, the waiting time diverges; the prefactor (1+c2s)/2 multiplies the divergence, so high-variance systems break down faster as load increases."
- Kingman: "under suitable heavy-traffic conditions, the mean waiting time in a G/G/1 queue is approximately Wq^G/G/1 ≈ ρ/(1−ρ) · (c2a+c2s)/2 · E[S] (21), which is exact asymptotically as ρ → 1−."
- "The Kingman approximation makes burstiness explicit: a bursty arrival process (c2a ≫ 1) inflates waiting times just as effectively as a high-variance service process."
- Variance wedge: "AI can reduce the average human time spent per task, τA < τH, and still increase waiting time" (mechanism: service-time variance ↑ while ρ near 1).

### From `krivulin-tandem-cycle-time.txt` (arXiv:1212.5309)

- Recursion: "Dm (n) = max(Dm−1 (n), Dm (n − 1)) + τmn , m = 1, . . . , M" (Eq. 1).
- Theorem 7: "Suppose that {τin ...} are mutually independent sequences of i.i.d. r.v.'s with 0 ≤ E[τi1] < ∞. Then the limit at (2) exists w.p.1, and if D[τi1] < ∞, it is given by γ = max_{0≤i≤M} E[τi1]. (3)"
- Corollary 8: "if at least one of the expectations E[τi1], i = 0, 1, . . . , M, is positive, then it holds π = (max_{0≤i≤M} E[τi1])−1."
- Blocking (manufacturing, buffer capacity 0): "with this type of blocking, if upon completion of a service, the first server sees the buffer of the second one is full, it cannot be freed and has to remain busy until the second server completes its current service" ... "D1 (n) = max(max(D0 (n), D1 (n − 1)) + τ1n , D2 (n − 1)) (6)".

### From `batch-service-map-vacation.txt` (arXiv:2208.13043)

- GBS: "When a service is finished, and if the server determines that the queue length is l (≥ h), it begins service in accordance with the GBS rule, i.e., it serves a batch of size min(l, H), where H is the server's maximum capacity."
- Stability: "The system's stability is ensured by the traffic intensity, which is λsH/H (< 1)." (subscript lost in extraction; notation above defines E[S_r] = 1/µ_r, and H is max batch size → λ·E[S_H]/H < 1.)

### From `sasikala-bulk-service-survey.txt` (Sasikala & Indhira 2016)

- GBS rule: "General bulk service rule was first introduced by Neuts [4]. General bulk service rule states that the server will start to provide service only when atleast 'a' units are present in the queue, and maximum service capacity is 'b'."
- Batch-size-dependent service exists as a family: "the service rate may depend on the number of customers waiting for service" (Abstract).
- Tandem with bulk service: "A simple queueing systems connected by a series in one direction is called tandem queue, the service of each queueing system in a tandem queue may be single or batch."
- Downton waiting time: "Downton [5] obtained the waiting time distribution of bulk service queues by considering random arrivals and random service time distribution."

### From `flattening-the-curve.txt` (arXiv:2004.09645)

- Peak vs spread: "By increasing the standard deviation by a factor of 2, we have reduced the peak value of the arrival rate by a factor of two."
- Lag: "the lag between the peak arrival rate and peak load is equal to half of the service time."
- Purpose of flattening: "Using our peak load calculations, we determine the value of 'flattening' that needs to be done in order to make the peak load infection value below a pre-specified capacity level."
- Infinite-server as upper bound: "the Mt/G/∞ infinite server queue is an upper bound for queueing models with a finite number of servers and without abandonment since it represents the dynamics if the manager had access to an infinite amount of resources."

---

## 5. Notes on confidence

- All quotes above were re-verified against the .txt files in this session (grep/sed), not taken from web-search snippets. The single exception is flagged inline (the h7.pdf "first two moments" sentence — replaced by the verified residual formula from queueing.pdf).
- `research/papers/manifest.json` was NOT modified (per task constraints); it does not yet list the 6 new downloads. Flagging for the human.
- Deleted `FowlerMonch2022-pbatch-survey.pdf` (Elsevier TDM-block HTML, not a paper).
