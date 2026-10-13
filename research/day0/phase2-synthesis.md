> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). The "read before any optimization work" instruction below was executed pre-contest and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Phase 2 — Research Synthesis (Day 0, post-statement)

# Insight mapping from scheduling literature (research/notes/*, docs/04-scheduling-theory-toolkit.md)

# to the ACTUAL score formula. Read before any optimization work.

---

## 1. The score formula in closed form (the master key)

Score = 1000 · (w_tp · C_tp + w_c · C_w) with

- C_tp = clamp( tp ; tp_base, tp_UB ),      tp  = ΣLout / (T_last − T_first_arr)
- C_w  = clamp( dist ; dist_base, 0 ),      dist = √(ε_tdr² + ε_tpot²)
- ε_tdr  = max(0, (μ_tdr − SLO1)/SLO1),     μ_tdr = Σ_i (ppost_i − arr_i) / R
- ε_tpot = max(0, (μ_tpot − SLO2)/SLO2),    μ_tpot = Σ_i (e_last,i − e_first,i) / (ΣLout − R)

### Consequences (all verified against statement text)

1. **μ_tpot depends only on per-request FIRST and LAST token times.** Internal cadence is irrelevant to the score. Policy goal for tpot: once a request's first token is out, push it through to its last token as fast as possible (continuous decode, no mid-request starvation). Do not think in terms of per-token pacing; think in spans.
2. **μ_tdr is an unweighted mean** — every request counts equally, including tiny Lout=1 requests. Policy goal: minimize mean (ppost − arr), i.e. get ALL requests through the input stage promptly. No per-request deadlines exist; only the mean.
3. **C_tp is makespan-driven**: T_last (latest final token) is the only active variable (T_first_arr is fixed by input). Optimization = minimize the time of the LAST finished token — classic makespan/minimax objective → list scheduling on clouds (Graham) + keep every resource busy (work conservation) + amortize S via batching.
4. **Dead zones:** ε_tdr = 0 once μ_tdr ≤ SLO1 (no further TDR gain), ε_tpot = 0 once μ_tpot ≤ SLO2, C_tp = 1 once tp ≥ tp_UB. Policy: FIRST reach the targets (they are mean-based, easy to verify offline on generated cases), THEN spend all remaining effort on tp. Tuning knobs have zero marginal value beyond their saturation points.
5. **dist_base = 0 is binary**: both means must be ≤ SLOs or the whole w_c contribution is 0. If a test has dist_base = 0 and w_c > 0, hitting BOTH means is a hard requirement for that component.
6. **No partial credit**: every test must complete all requests or it scores 0. Completeness is a hard constraint, not an objective. Never risk a stuck state (always leave at least one schedulable future action; responding 0 while arrivals remain is safe).
7. Weights sum to 1; either may be 0. If w_tp = 1: pure throughput; if w_c = 1: pure waiting-time means.

## 2. Research mapping (family → decision)

### 2.1 Cloud assignment (notes: none needed beyond Graham; doctrine §1.1)

- Graham list scheduling (1966): greedy least-loaded is (2 − 1/K)-competitive for makespan; no deterministic online algorithm beats it. For identical clouds, assignment to min busy_until is the baseline — provably sound for the C_tp component.
- **Transfer-aware variant is the real lever** (doctrine §1.1): UP is a SHARED FIFO across clouds. effective_ready[c] = max(busy_until[c], time UP data for this request reaches cloud c). When latency/bw dominate, min-busy_until misassigns (a cloud that is idle but whose UP queue is long waits anyway). Assign to min effective_ready. This is the single biggest policy lever on transfer-bound tests.
- K ≤ 8: an O(K) scan per decision is free.

### 2.2 Decode batching (notes 401 Sarathi, 402 work-conserving; doctrine §1.2)

- Setup cost S is paid per task. Batching m requests into one D PRE/D PROC/D POST pays S once. Efficiency = m/(S + dur(m)) — batching wins iff dur(m) grows sublinearly (interpolate from the table).
- Marginal rule (Potts–Kovalyov batching taxonomy): add a member iff dur(m+1) − dur(m) < S/(m+1).
- Sarathi/Orca insight (401): stall-free batching — decode batches are formed from everything currently ready; never make decode wait for work that is not ready. Here: group ONLY requests that are ready at the same decision frame. Do not hold ready requests for latecomers beyond a frame (there is no self-wake timer anyway — waiting means leaving the local computer idle, which costs μ_tdr for every held request and T_last).
- IMPORTANT tension resolved by formula: batching helps C_tp (S amortization, fewer tasks) but delays every member's first token (μ_tdr) and extends spans for held members (μ_tpot). Because μ_tpot/μ_tdr are means and C_tp is makespan, batching is a balancing act governed by (w_tp, w_c, SLOs, tp_UB). Rule of thumb to A/B: batch all-ready at each frame vs. size-1.

### 2.3 Prefill chunking (notes 501 Zero Bubble, 502 CrossPipe, 406 SplitLLM; doctrine §1.3, CORRECTED)

- CORRECTED COST MODEL: one UP + one DOWN per request regardless of chunks; chunks wait on previous piece TDN; each chunk pays S; chunk duration = fraction · prefill_proc(Lin).
- Pipelining value (501/502): chunking lets a cloud start a piece of request B while request A's next piece waits — but only helps when the REMOTE side is the bottleneck and S is small. Local P PRE/P POST and the FIFO transfers are the serializing resources; chunking trades (c−1)·S for the ability to interleave P PROC pieces across clouds and requests.
- Zero-bubble insight (501): the idle "bubbles" happen because a machine waits for a piece of a single request; overlapping multiple requests' pieces fills them. With K clouds and many requests, the pipelining benefit appears once clouds are the binding resource.
- num_layers ≤ 64, S ∈ [1,10], prefill_proc ∈ [0.001, 10^4]: chunking is worthwhile when S is small vs. prefill_proc fraction and when clouds would otherwise idle. V1 = 1 piece; V4 (chunking) A/B with adaptive split.
- Note the interaction with scoring: more chunks delay the LAST piece's DOWN → hurts μ_tdr (first-token mean) if overused; helps C_tp when clouds are otherwise starved.

### 2.4 FIFO queues / staggering (notes 402; doctrine §1.4)

- Bursting completions create queue buildup and head-of-line blocking on the single UP/DOWN pipe. With R = 2000 and up to 64 layers of prefill all finishing simultaneously, UP can queue dozens of transfers.
- Staggering = not starting every ready P PRE at the same frame when the UP queue is the bottleneck. This is a TDR-vs-TP trade: delaying P PRE start delays that request's ppost (μ_tdr) but smooths the pipe and reduces makespan (C_tp) under saturation.
- Practical first version: queue-aware cloud assignment (2.1) already staggers implicitly; explicit staggering only if A/B shows a win on transfer-bound cells (transfer_bottleneck zoo case).

### 2.5 SLO-aware / score-aware (notes 403 SLaI, 405 DistServe, 406 SplitLLM; doctrine §1.5/2.4)

- DistServe goodput framing (405): SLO attainment rate, not raw throughput — mirrors the ε structure here (mean-based, not per-request; much easier).
- SLaI last-schedulable-time (403): irrelevant per-request — means have no per-request deadlines. Discard deadline bookkeeping; use mean-target steering instead.
- Priority/ordering policies: since μ_tdr and μ_tpot are means, there IS an ordering effect: FIFO start order minimizes mean completion for equal durations (SPT rule: shortest first minimizes mean completion — but TDR is start-to-ppost span; SPT on Lin (input stage length) minimizes mean TDR given a single machine queue). A/B: SPT (short Lin first) vs FIFO for P PRE queueing.

### 2.6 Robustness (doctrine §4, §5)

- Interpolation: binary search + clamp + -1 skip — pure engineering; the statement gives the exact semantics (docs/ASSUMPTIONS §2). Validate by comparing our get_duration vs every TDN dur field (the interactor echoes actual dur — free oracle!).
- Double precision: 9 decimals, values to 10^12, EPS = 1e-9; never ==; frame t comparisons with EPS.

## 3. What NOT to do (from statement constraints)

- No preemption, no rejection, no partial credit: every request finishes.
- No predicting needed for correctness: purely reactive bookkeeping is sufficient (TDN/XDN/FIN drive state). Simulation only for policy decisions (lookahead).
- R is unknown, Lout unknown until FIN, no end-of-arrivals signal: grouping policy must be online; END is the only termination signal.
- n ≤ K+1 per response; one task per resource per frame.
- 2·10^6 frames: O(events) amortized state, O(ready) decisions.
