# Optimization Doctrine (Design Corrections) — Consolidated

> **Historical note (post-contest).** I recorded these corrections during the contest as I found and fixed each misunderstanding; they directly supported my finish of **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) originated every decision below, traceable from docs/DECISIONS.md; AI sped up implementation and checks under my direction.

Design corrections to early misunderstandings of the problem model. Early drafts
misunderstood FIFO transfer queues, setup cost S, TDR/TPOT tradeoffs, makespan, and grouping
flexibility; every line below was my decision, traceable from docs/DECISIONS.md.

Each part lists: the correction, the scheduling implication, and implementation status in the
solver (grep the cited files/lines). Status legend: IMPLEMENTED, VERIFIED-EMBODIED (already
correct, no change), TESTED-NEGATIVE (tried, rejected), DOCUMENTED (known, not yet exploited).

This file was the canonical doctrine reference during the contest. I kept it in sync with docs/DECISIONS.md on every fix.

---

## PART 1.1 — Chunking cost (D-016)

- **Correction**: every prefill PIECE pays S (incl. each chunk and decode group). Transfer model
  = ONE UP (P PRE, carries the whole prompt) + ONE DOWN (only after the LAST piece's TDN).
- **Implication**: chunking is only worth it when S is small relative to piece compute; extra
  chunks add S with no extra transfer benefit.
- **Status**: IMPLEMENTED (solver was already correct; only docs/clarity fixed).

## PART 1.2 — Local computer is the bottleneck (D-017..D-020)

- **Correction**: the LOCAL computer ran ~16x more tasks than any single cloud (P PRE, P POST,
  D PRE, D POST for ALL requests, vs P PROC / D PROC per cloud). It was the throughput limiter.
- **Implication**: I did NOT blindly keep local busy; that collapsed scores. The real lever I used was letting
  the local INTERLEAVE (e.g. chunked prefill that let the cloud start early) — via V4
  AdaptiveChunk — NOT more local decode work. Late finding I confirmed: COMBINING D PRE / D POST across
  clouds into one local task was the biggest local-save lever I found (see D-026).
- **Status**: DOCUMENTED / REVERTED on the naive fix (D-019 collapsed, I reverted in D-020). I
  re-tested V4 as the local-interleaving lever; V8's D PRE combining (D-026) was the
  concrete local-save win I shipped.

## PART 2.1 — TPOT is pooled, high-Lout dominated (D-021, D-022)

- **Correction**: TPOT = pooled mean span / total_gaps over all requests; high-Lout requests
  dominate; a Lout=1 request contributes ~0. Per-request TPOT is NOT reported.
- **Implication**: to improve TPOT, smooth DECODE for high-Lout requests only. Small high-batches
  (min gap) is the exploit — but see 2.2 for why it can backfire.
- **Status**: TESTED-NEGATIVE. V6_LOUT (small high-batch, `-DV6_LOUT`) implemented; found to
  overload the local computer, delaying prefill for ALL requests and dropping the uniform-mean
  TDR (D-022). NET-NEGATIVE → NOT promoted.

## PART 2.2 — TDR is uniform-mean (D-023)

- **Correction**: TDR = mean over ALL requests of (P POST done - arrival); Lout=1 == Lout=512.
- **Implication**: treat all requests equally for TDR → start PREFILL ASAP for EVERY request.
  TPOT (2.1) only justifies smoothing decode for high-Lout. Never let decode starve prefill.
- **Status**: VERIFIED-EMBODIED. pick_local priority = P_PRE > P_POST > D_PRE(gated by
  hold_dpre) > D_POST (scheduler.cpp). SloAwareDelay::hold_dpre (policies.cpp:149) holds ONLY
  D_PRE (first-token decode) when prefill is in flight, never holds prefill. No code change.

## PART 2.3 — Throughput = makespan (D-024)

- **Correction**: throughput score = makespan = latest final-token time - earliest arrival. One
  straggler cloud tanks it. Cloud assignment must balance CUMULATIVE decode load (proxy = Lin at
  assign, corrected on FIN), not just instantaneous busy_until.
- **Implication**: assign each new request to the cloud with the lowest estimated remaining decode
  load; correct reactively as requests FIN.
- **Status**: IMPLEMENTED — DecodeLoadBalanceCloud (`-DV7_BALANCE`). Huge win on makespan
  cases (heavy_mixed 1.546→1.021, hi50 1.562→1.027), neutral elsewhere, ZERO regression, passes
  all freeze-gate checks. Incorporated into V8 (below).

## PART 3.1 — D PROC combines across different D PRE groups (D-025)

- **Correction**: D PROC members MAY come from different D PRE groups and different
  local-to-remote transfers. Issue a prompt per-request D PRE (TDR benefit) and BATCH at D PROC
  (throughput: saves S + transfer latency). The anti-pattern is batching at D PRE only, which
  makes later arrivals wait for the batch to form (TDR loss).
- **Implication**: never delay a ready D PRE to accumulate a batch; let D PROC do the batching.
- **Status**: VERIFIED-EMBODIED. pick_local D PRE (scheduler.cpp:111) issues the head ready
  request immediately and only grows the group with ALREADY-ready same-cloud peers (never waits).
  pick_cloud D PROC (scheduler.cpp:203) batches ALL ready same-cloud members of READY_D_PROC,
  which each enter independently after their own D PRE UP completes (state.cpp XDN WAIT_DEC_UP →
  READY_D_PROC). So D PROC combines across different D PRE groups by construction. GreedyAllBatch
  returns group_size(D_PROC, n)=n → full D PROC batching. No code change.

## PART 3.2 — D POST across D PROC groups; D PRE across clouds (D-026)

- **Correction**: D POST may recombine members from DIFFERENT D PROC groups AND different
  remote-to-local transfers. D PRE may likewise combine a MIXED-CLOUD group into ONE local task;
  the interactor splits the UP transfer per request cloud. The cross-remote pattern
  (D PRE -1 m all, D PROC parallel per cloud, D POST -1 m all) saves LOCAL tasks — the bottleneck
  per 1.2 — 2 per decode iteration, ×Lout ⇒ enormous for high-Lout.
- **Implication**: never split D POST/D PRE by cloud; batch ALL ready requests into one local task.
- **Status**:
  - D POST combining across D PROC groups: VERIFIED-EMBODIED (scheduler.cpp:159 already batches
    ALL ready D_POST; protocol rule 1.4 permits cross-cloud D POST).
  - D PRE combining across clouds: IMPLEMENTED under `-DV8_PRECOMBINE` (scheduler.cpp D_PRE block).
    Interactor confirmed to support mixed-cloud D PRE (interactor.py:272 groups by per-request
    cloud). Build `solution_v8 = -DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE`.
  - Result: V8 DOMINATED V7 and frozen_v5 on every mixed case (heavy_mixed 1.546→1.565,
    hi50 1.562→1.580, dribble_mixed 0.929→1.035), neutral on zoo/scale, zero regression. Passed
    all freeze-gate checks (zoo 22/22 vs V1 & V5@0.95, ASan/UBSan clean, crash sweep 3000/3000).
    **I promoted it after my freeze review on the path to #56.**

## PART 3.3 — The optimal decode pattern (combine 3.1 + 3.2) (D-027)

- **Correction**: the optimal decode pattern is prompt per-request D PRE (TDR) + batch D PROC per
  cloud when UPs arrive (throughput + cloud parallelism) + combine all results in one D POST
  (throughput). Issue each D PRE the moment its request is ready; coalesce concurrently-ready
  peers into one local task (saves local S, finishes sooner) — this strictly beats emitting
  separate size-1 D PREs, which would SERIALIZE on the local computer and delay later arrivals
  (worse TDR). The sweet spot = minimal local tasks + parallel cloud execution.
- **Implication**: never wait to form a D PRE batch; never split D PROC/D POST by cloud.
- **Status**: VERIFIED-EMBODIED (no code change). V8 already realizes this: D PRE issues the head
  ready request immediately and coalesces only already-ready peers (scheduler.cpp, V8_PRECOMBINE;
  never waits => per-request TDR preserved); pick_cloud D PROC (scheduler.cpp:203) batches all
  same-cloud READY_D_PROC members, each entered after its OWN D PRE UP (so D PROC batches "when
  UPs arrive", per cloud, in parallel); D POST (scheduler.cpp:159) combines all across clouds. V8
  is the realized optimal decode pattern across PART 3.1/3.2/3.3. See D-027.

## PART 3.4 — Cross-remote transfer penalty in D PRE combining (D-028)

- **Correction**: V8's combined D PRE issues ONE local task but the interactor enqueues each
  member's UP transfer in INCREASING remote order, so a member on cloud c waits ~(c−1)·UP before its
  D PROC starts. With K clouds in one D PRE, the highest-index member waits (K−1)·UP — a serial
  head-of-line penalty. At high latency this dominates (K=8, lat=1000 ⇒ 7×1000ms idle). Never
  combine many requests from many clouds into one D PRE if it leaves several idle for ~(K−1)·UP.
- **Mitigation (ADAPTIVE cap)**: cap distinct clouds per D PRE group at 3 (XREMOTE_CAP=3) and
  prefer low-index clouds (smallest serial UP wait). The cap is applied ONLY when
  `latency_ms >= 50.0` (XREMOTE_LAT_THRESH); below that the cap = K, i.e. V8's combine-all
  behavior, because at low latency the (K−1)·UP penalty is negligible while the cap's extra
  local-S cost (the 1.2 bottleneck) is not. Built as V9.
- **Why adaptive, not fixed**: a FIXED cap=3 (V9a) REGRESSED vs V8 on the measured low-latency
  cases (heavy_mixed 1.565→1.546, hi50 1.580→1.562) — the cap's local-S cost dominated where the
  penalty was ~1ms. The adaptive cap removes this regression (V9 == V8 at lat<50) while still
  guarding the latency-heavy regime.
- **Status**: IMPLEMENTED under `-DV9_XREMOTE` (scheduler.cpp D_PRE block; reads `cfg_.latency_ms`).
  A/B: V9 == V8 on every low-latency mixed case; passes all freeze gates (zoo 22/22 vs V1 & V5@0.95,
  ASan/UBSan clean on BOTH cap paths, crash sweep 3000/3000). Could not surface a score gain on a
  synthetic high-latency case (throughput floors C_tp at 0 in ms time units), so the gain is
  expected only on genuinely latency-dominated hidden tests. Adopted as defensive (non-regressive
  superset of V8). See D-028.

## PART 4 — Local-computer scheduling: the priority queue (D-029, D-032)

- **Correction (LITERAL ordering REJECTED, D-029)**: the doctrine ranks local tasks D_POST > P_POST >
  D_PRE > P_PRE (w_c>0) or D_POST > D_PRE > P_POST > P_PRE (w_c==0). Implemented literally; a ready
  D_POST is almost always present during decode, so absolute D_POST priority starves P_PRE/P_POST
  every frame → prefill halts → clouds drain → throughput + TDR collapse. Measured: heavy_mixed
  1.565→1.359, hi50 1.580→1.234, dribble_mixed 1.035→0.425. REVERTED.
- **4.2 (starvation trap / reservation) — EMBODIED, no code change (D-032)**: "reserve local capacity
  for prefill; prioritize P PRE/P POST when the prefill queue builds up, D PRE/D POST otherwise." Our
  existing local order (P_PRE > P_POST > D_PRE > D_POST) IS this reservation: prefill is always the
  first tier, so it is never starved; decode is served only when prefill is idle. This is the
  anti-starvation fix and is empirically best.
- **4.3 (batching D PRE to save local time) — EMBODIED, no code change (D-032)**: "issue D PRE
  per-request when TDR matters; batch at D PROC and D POST." V8_PRECOMBINE coalesces ONLY
  already-ready D PRE peers (head always issued, never waits for batchmates), so no request's TDR is
  delayed while the local S is still saved; D PROC batches per cloud and D POST batches across clouds.
  This is exactly the 4.3 compromise (per-request D PRE + batch D PROC/D POST).
- **4.1 "D_POST most time-sensitive" — A/B REJECTED (D-032)**: tested reordering the decode tier to
  D_POST > D_PRE (prefill tier unchanged). ab_test on cases/zoo+adv (30): 3 wins, 15 ties, 12
  regressions, net −0.040 → REJECT. The proven D_PRE-first order keeps clouds fed (D_PRE starts the
  pipeline occupying clouds; D_POST only emits a token locally) and scores higher. Source reverted;
  `solution_v9` byte-identical to reference.
- **Two protocol limits also constrain PART 4** (D-029): (1) Lout hidden until FIN (state.cpp:238) →
  intra-phase "high-Lout first" not implementable; (2) P_POST mean-TDR tie-break is a no-op (equal
  service times).
- **Status**: PART 4 RESOLVED. Literal ordering rejected (D-029); 4.2/4.3 already embodied by the
  candidate (D-032); 4.1 reorder rejected (D-032). No PART 4-specific code; the current local order is
  the final design.

### PART 4.4 — P PRE cloud assignment (the irreversible decision) (D-033)

- **Formula (human-supplied)** — symbols in `code`, each cost term on its own line:

```text
effective_ready[k] = max(compute_free[k], up_queue_tail[k])
decode_load[k] = est. remaining decode iters × decode_proc time
batch_affinity[k] = # requests already on k that could batch in decode
score[k] = effective_ready[k] + alpha*decode_load[k] - beta*batch_affinity[k]
assign argmin k
```

Implemented as pluggable `PpreCloudAssign` (`-DV10_PPA`, `solution_p10`), A/B'd vs `solution_v9` (DecodeLoadBalanceCloud).
- **Decode vs existing**: `decode_load[k] = decode_est(k) × table.get(D_PROC,1)` — this IS the
  load-balancing key `DecodeLoadBalanceCloud` already uses (D-024). `effective_ready` adds the UP-FIFO
  drain + compute-free readiness (new, but dominated by `decode_load` on the corpus). `batch_affinity[k]
  = active_on_cloud(k)` is a co-location/clustering reward.
- **A/B (cases/zoo+adv, 30)**: alpha=1,beta=0/1 → **identical to v9** (30 ties); beta=10 → 1 regress
  (REJECT); beta=50 → 3 wins / 14 ties / **13 regressions** (REJECT). The `batch_affinity` clustering
  reward is empirically HARMFUL (concentrating requests unbalances load; same lesson as V2/V4).
- **Status**: PART 4.4 RESOLVED. The beneficial core (decode-load balancing) is ALREADY embodied by
  `DecodeLoadBalanceCloud`; the `batch_affinity` reward is REJECTED. `PpreCloudAssign` is kept as a
  NON-PROMOTED pluggable policy; the submission artifact still uses `DecodeLoadBalanceCloud`. No
  candidate code change.

---

## PART 5 — Transfer queue optimizations (D-035)

- **5.1 UP/DOWN are independent — exploit overlap.** The two FIFOs run in parallel
  (interactor-owned); we never schedule transfers. Overlap is therefore *free* and is
  maximized simply by keeping BOTH pipelines fed — which the work-conserving, prefill-first
  scheduler already does (P_PRE→UP stays active while D_PROC→DOWN stays active). **No code
  change.** (Note: P_PRE and D_PRE are both LOCAL tasks, so the local computer, being
  single-threaded, alternates between feeding UP via P_PRE/D_PRE and feeding DOWN via
  D_POST — the two queues are kept busy without our intervention.)
- **5.2 Transfer serialization penalty.** ONE shared UP FIFO, ONE shared DOWN FIFO. Every
  transfer waits behind the queue tail. The penalty is worst when many UPs are *enqueued at
  once*. In our design this is structurally bounded:
  - P_PRE side: the local computer issues at most **one** UP-triggering task per frame
    (single-threaded), so P PRE UPs trickle in one-at-a-time. A backlog can still form only in
    the compute-fast / transfer-slow regime (prefill_pre ≪ UP time), where sequential P PREs
    outrun the FIFO drain — a residual risk, not a per-frame burst.
  - D_PRE side: `V8_PRECOMBINE` batches many members into one D PRE, which on completion
    enqueues one UP *per cloud* simultaneously. `V9_XREMOTE` caps a D PRE to ≤`XREMOTE_CAP`
    (3) distinct clouds ⇒ ≤3 UPs per D PRE ⇒ the (K−1)·UP tail is bounded to (cap−1)·UP. This
    is the direct embodiment of the 5.2 "stagger / cap" mitigation.
  - "Natural stagger via cloud assignment" is partially embodied by `DecodeLoadBalanceCloud`
    spreading load so decode phases differ. **No further code change for the burst case.**
- **5.3 Small-data regime — minimize transfer COUNT.** When bytes_per_token is small and
  bandwidth large, transfer ≈ latency and data size barely matters; the dominant saving is
  fewer transfers / fewer local-S charges. This **supports `V8_PRECOMBINE`**: one D PRE for
  many ready requests ⇒ one UP per cloud (member count independent of transfer count) and one
  local-S instead of many. The lifecycle fixes the transfer COUNT (1 UP+1 DOWN per prefill;
  1 UP per cloud per D PRE; 1 DOWN per D PROC group); batching never increases it and lowers
  S. So maximize batching wherever 5.2 permits — exactly what `V8_PRECOMBINE`+`V9_XREMOTE`
  do (batch within a cloud, cap across clouds).
- **5.2 vs 5.3 reconciliation:** cap cross-remote groups (`V9_XREMOTE`) to bound the UP burst
  while still batching inside a cloud to minimize D PRE count / S. Already realized.
- **5.4 Large-data regime (D-036):** when prefills are heavy and transfers slow, (5.4a) prefill
  transfers must be *prioritized* — embodied by the prefill-first P_PRE tier; (5.4b) batching the
  decode transfers is the single biggest transfer saving — embodied by `V8_PRECOMBINE` (1 UP per
  cloud per D PRE instead of 1 per request); (5.4c) chunking prefill does NOT help there — embodied
  by `AdaptiveChunk`, which disables chunking when `up()` is busy / S large / cloud busy; and
  (5.4d) in this regime you MUST **stagger prefills** so the shared UP queue does not saturate.
  **5.4d is BUILT (D-036, NON-PROMOTED).** The release solver now has an **accurate transfer-FIFO
  mirror** under `V12_MIRROR`: `issue()` pushes to `up_/down_` at `enter_t = busy_until` (= task
  completion = the moment the interactor enqueues its transfer) with `transfer_time(len)` matching
  `interactor.py c.transfer()` exactly; `confirm()` (called on every XDN) pops and maintains `tail`.
  The mirror was PROVEN EXACT — a `-DDEBUG` build passes every `confirm()` assert
  (`fabs(finish_times.front()-t)<1e-6`), i.e. the solver's FIFO finish times equal the interactor's
  XDN timestamps. (The earlier "96s divergence" was a mis-estimate: `bw=0.001,bpt=1e6` ⇒ ~32s per
  transfer, so a ~96s backlog is the REAL depth, not a model bug.) The 5.4d stagger is implemented
  as `-DV11_PPRECAP`: a count-based P_PRE defer (`up().in_flight() >= PPRE_MAX_INFLIGHT`, default 2)
  — robust to transfer size, neutral when the UP stays shallow. A/B of `solution_p12`
  (V9+V12_MIRROR+V11_PPRECAP) vs the no-mirror baseline: **30/30 ties, 0 regression, 0 broken**;
  `crash_sweep 500/500 ok`. The mechanism is defensive — it only acts when the UP FIFO would
  saturate, which does not occur on the visible suite except on `transfer_bottleneck`/`transfer_burst`
  (both score 0.000, c_tp and c_w floor at 0 in ms units), so its benefit is unmeasurable on visible
  cases. It is kept as a gated experiment (`make p12_stagger`, `PPRE_MAX_INFLIGHT` tunable) and NOT
  merged into the candidate/submission (per freeze ON-HOLD); the live candidate `solution_v9` is
  byte-behaviorally unchanged (mirror off). Available for PART 8 score-aware tuning if a
  transfer-saturated hidden test exercises it.
- **Open lever (REJECTED as no-op, prior to 5.4d):** a congestion-aware P PRE throttle (defer P_PRE
  starts while `up().tail` is far ahead, freeing FIFO for decode UPs) was actually implemented
  (`-DV11_PPRECAP`) and A/B'd vs `solution_v9` across thresholds incl. forced — **30 ties, 0
  wins/losses** on all 30 zoo+adv cases. In this suite prefill and decode are phase-separated at
  local decision points, so the UP-FIFO contention never occurs; the throttle is a no-op. Code
  removed (D-035-addendum). Superseded by the accurate-mirror 5.4d stagger above.

## PART 6 — Protocol edge cases that will kill you (D-037)

Consolidated enumeration of protocol pitfalls that score 0 if mishandled. Every item below was
verified against the layered sources AND the generated `src/submission.cpp`; all are
**VERIFIED-EMBODIED** (already correct, no code change). The live candidate `solution_v9` /
`submission.cpp` is unchanged.

### PART 6.1 — The -1 marker is mandatory

- **Correction**: every D PRE / D POST output line MUST contain `-1` as the remote field:
  `D PRE -1 <m> <rid...>`, `D POST -1 <m> <rid...>`. Omitting it → malformed → score 0.
- **Implication**: the serializer must hard-code `-1` for decode PRE/POST local tasks.
- **Status**: VERIFIED-EMBODIED. Output emits `E D PRE -1 <m> ...` (Solution.cpp:97 /
  submission.cpp:1691) and `E D POST -1 <m> ...` (Solution.cpp:107 / submission.cpp:1701). The
  interactor **requires** it: `if spec[2] != "-1" ... return "D %s arity"` (interactor.py:249).

### PART 6.2 — P POST's remote field is a consistency check

- **Correction**: `P POST <remote> <rid>` runs on the LOCAL computer, but `<remote>` must equal the
  request's assigned cloud. Wrong value → violation → score 0.
- **Implication**: emit the assigned `r.cloud` for P POST (never `-1`).
- **Status**: VERIFIED-EMBODIED. Scheduler sets `spec.cloud = r.cloud` (scheduler.cpp:128 /
  submission.cpp:1199) and outputs `E P POST <r.cloud> <rid>` (Solution.cpp:94 /
  submission.cpp:1688). Interactor checks `if r["cloud"] != remote: return "P POST remote !=
  assigned"` (interactor.py:235).

### PART 6.3 — FIN arrives WITH the last D POST TDN

- **Correction**: in the same frame you receive both `TDN E D POST <rid>` (final token) and
  `FIN <rid>`. You must not schedule that request again in your response to that frame. Processing
  events out of order, or scanning ready lists before processing FIN, can schedule a finished request.
- **Implication**: apply ALL frame events (incl. FIN) before deciding; never leave a finished request
  in a ready list.
- **Status**: VERIFIED-EMBODIED. Main loop applies every event then `decide()` (Solution.cpp:75–80).
  The interactor pushes FIN **before** the TDN (interactor.py:287→297, smaller heap seq) ⇒ FIN is
  applied first, marking `finished`; the TDN handler then hits `if (req.finished) continue;`
  (state.cpp:180) and skips re-queue. After the frame the request is FINISHED, never in a ready
  list, so `decide()` never schedules it. The FIN handler's `assert(!req.finished)` (state.cpp:244)
  is safe because FIN is applied before the TDN (request not yet finished) — DEBUG-clean.
  *(Latent, harmless: `pop_head` does not skip a `finished` request and the FIN assert would fire
  under a hypothetical TDN-first frame — neither occurs under the interactor. Defensive hardening
  offered, not applied; see Pending.)*

### PART 6.4 — The chunk range is half-open [ls, le)

- **Correction**: chunk bounds are half-open; ls included, le excluded; first ls=0, last le=num_layers;
  gap-free (each ls = previous le); empty range (ls==le) is a violation.
- **Implication**: every piece must be non-empty, contiguous, and end exactly at num_layers.
- **Status**: VERIFIED-EMBODIED. `OnePieceChunk` returns `{next_ls, num_layers}` ⇒ single `[0,L)`
  piece (policies.cpp:119). `AdaptiveChunk` returns `{0, mid}` then `{mid, L}` (mid=L/2≥1 for L≥2;
  L=1→`{0,1}`) ⇒ gap-free, non-empty (policies.cpp:123–152). Satisfies the interactor's `illegal
  piece` check (`ls != next_ls or le <= ls or le > L`, interactor.py:213).

### PART 6.5 — The dur field excludes S

- **Correction**: TDN's `dur` is execution time WITHOUT S; the computer is busy for S+dur and the TDN
  arrives exactly when it becomes free, so `busy_until = t_TDN`. `dur` is informational — do NOT
  compute `busy_until = t_TDN + dur`.
- **Implication**: set `busy_until = t_issue + S + dur` at issue time and only clear the busy flag on
  TDN; never re-add dur.
- **Status**: VERIFIED-EMBODIED. `issue()` sets `busy_until = t + cfg_.S + dur` (state.cpp:309 /
  submission.cpp:974) — includes S. The TDN handler only does `ct->busy = false` (state.cpp:106 /
  submission.cpp:771) and a DEBUG assert `fabs(busy_until - t) < 1e-6` (state.cpp:103) proving
  `busy_until == t_TDN`. Never `t + dur`.

### PART 6.6 — At most K+1 tasks per frame, ≤1 local

- **Correction**: `n ≤ K+1`: at most K cloud tasks + 1 local task. You CANNOT assign two local tasks
  in one frame. If both P PRE and D PRE are ready, choose ONE per frame (serializes local starts).
- **Implication**: the local computer is single-threaded; its start decisions are serialized by
  construction.
- **Status**: VERIFIED-EMBODIED. `decide()` calls `pick_local()` exactly once (1 local task:
  P_PRE / P_POST / D_PRE gated by hold) and `pick_cloud()` once per cloud (≤K cloud tasks)
  (scheduler.cpp `decide` at 67). Never two local tasks.

### PART 6.7 — The "stuck state" is fatal

- **Correction**: if all resources idle, no transfers in flight, and no future arrivals → stuck →
  score 0. Never leave all resources idle if ready work exists; start a lone request, don't wait for
  batchmates when nothing is in flight.
- **Implication**: be work-conserving; never delay a ready task when nothing is in flight.
- **Status**: VERIFIED-EMBODIED. The scheduler is work-conserving (issues any ready task). The only
  delay path, `SloAwareDelay::hold_dpre`, returns false when `next > 1e290` (nothing busy)
  (policies.cpp:223) and when `pipe == 0` (no prefill in flight) (policies.cpp:225). So it never
  idles all resources while ready decode work exists and nothing is in flight.

### PART 6.8 — The "no timer" constraint

- **Correction**: you cannot wake yourself at a future time; you act only when frames arrive. To delay
  a task you must wait for the next frame (generated by some completing task/transfer).
- **Implication**: only delay when something is in flight whose completion will generate a frame;
  never delay when nothing is in flight.
- **Status**: VERIFIED-EMBODIED (see 6.7). `hold_dpre` returns true only when a computer is busy
  (`next` bounded ⇒ a future TDN frame is guaranteed) AND a prefill pipeline is active
  (`pipe = WAIT_INPUT_DOWN + P_PROC_RUNNING > 0`) (policies.cpp:212–228). No self-timer is used;
  delay is purely event-driven.

### PART 6.9 — Double precision, 9 decimals, EPS

- **Correction**: times are doubles printed with 9 decimals; use `EPS = 1e-9`; never compare times
  with `==`.
- **Implication**: all time math in double; use epsilon comparisons.
- **Status**: VERIFIED-EMBODIED. `constexpr double EPS = 1e-9` (protocol.hpp:11 / submission.cpp:25).
  Time comparisons use EPS, e.g. `s.local().busy_until <= t + EPS` (policies.cpp:142 /
  submission.cpp:1514). No `==` on times (only iterator/int `==` in task_table.cpp). `t` is `double`
  throughout.

### PART 6.10 — Task-time table interpolation

- **Correction**: below smallest listed batch_size → use smallest entry; above largest → largest;
  between two → linear interpolation; `-1` entries are missing (skip per column); rows are unordered
  ⇒ must sort by batch_size. The interpolation is the official cost model — use it exactly.
- **Implication**: implement sort + per-column -1 skip + clamp + linear interp; do not "guess" values.
- **Status**: VERIFIED-EMBODIED. `load()` sorts by batch_size (task_table.cpp:21–24) and skips `-1`
  columns per-column (task_table.cpp:34). `get()` → largest if `it==col.end()` (52), smallest if
  `it==col.begin()` (55), else linear interp `lo.second + f*(hi.second-lo.second)` (57–64). Trap
  (rows at 1 and 4096, query 10) ⇒ `lo=1, hi=4096`, interpolates between them exactly.

### PART 6.11 — Table may list batch sizes > R

- **Correction**: a row with batch_size=4096 may exist even if R=10; use it for interpolation but
  never actually batch 4096 requests. Don't batch more than R.
- **Implication**: batch size is bounded by available ready requests (≤R), never by a table row.
- **Status**: VERIFIED-EMBODIED. `GreedyAllBatch::group_size` returns `n_ready` (available count,
  policies.cpp:160–163); every batching loop is capped by `lst.size()` (scheduler.cpp:39, 87, 97;
  pick_cloud while-loops). Actual batches are always ≤ available ≤ R. A table row at 4096 is used
  ONLY for duration lookup in `get()`, never as a batch count.

### PART 6.12 — Exit code 0 on EOF

- **Correction**: if reading input fails or reaches EOF, exit immediately with exit code 0. Don't
  crash, don't block (beyond the normal stream read that 6.8 requires).
- **Implication**: the reader must return "no more frames" on EOF/END; main returns 0.
- **Status**: VERIFIED-EMBODIED (with one harmless caveat). `next_frame` returns false on EOF
  (`std::cin >> tok` fails → submission.cpp:426) and on `"END"` (submission.cpp:423); `main` returns
  0 (Solution.cpp:116 / submission.cpp:1710); no `exit()` calls. Normal stream-blocking is expected
  (6.8). **Caveat (not triggered by the trusted interactor)**: `next_frame` does not guard
  `std::stod`/`std::stoi` against a malformed numeric token (would throw) and the `else {
  assert(false); }` on an unknown event kind is a no-op in release (would mis-parse as ARR). The
  interactor is well-formed, so this never fires in the contest; hardening Layer 1 is a candidate
  change offered but NOT applied (frozen-early Layer 1, left for post-contest).

## PART 7 — Adaptive strategy matrix (D-038)

The scoring weights (w_tp, w_c, plus SLO1/SLO2/tp_UB/tp_base/dist_base) select a REGIME; the optimal
policy differs per regime. The current candidate `solution_v9` is a FIXED policy (prefill-first local
priority P_PRE>P_POST>D_PRE>D_POST; `hold_dpre` active only when w_c≠0; OnePieceChunk; V7 balance +
V8/V9 cross-remote batching). It does NOT yet detect the regime or switch policies. PART 7 prescribes
the per-regime policy; embodying it requires an adaptive switch keyed on (w_tp, w_c) — a candidate-level
change requiring a freeze decision (and the full matrix from 7.2/7.3 to design the switch
points/boundaries). Status per directive: EMBODIED (already correct), NOT-EMBODIED (contradicted by
the fixed policy), or TENSION (conflicts with a prior finding). `Config` already carries w_tp/w_c
(protocol.hpp:22), so regime detection needs no protocol change.

### PART 7.1 — Throughput-Dominated (w_tp = 1, w_c = 0)

- **Goal**: minimize makespan; maximize tokens/ms.
- **Cloud assignment: balance total estimated decode load (Lin proxy for Lout).** → **EMBODIED** by
  `DecodeLoadBalanceCloud` (`-DV7_BALANCE`); uses Lin at assign and corrects on FIN (D-024).
- **Prefill chunking: chunk aggressively (S small [1,10], interleaving keeps clouds busy).** →
  **NOT-EMBODIED / TENSION**. Candidate uses `OnePieceChunk` (no `V4_CHUNK`); `AdaptiveChunk` exists
  but is not in the candidate, and D-020 found chunking can COLLAPSE throughput — direct TENSION with
  this directive. Open question, see D-020.
- **Decode batching: batch as large as possible; wait for batchmates if transfers in flight.** →
  **PARTIAL**. Batching size is maxed (`GreedyAllBatch` returns n_ready, capped by lst.size()) —
  EMBODIED for size. But "wait for batchmates" requires D_PRE delay, which `hold_dpre` DISABLES at
  w_c==0 (policies.cpp:205 `if w_c==0 return false`) — so in this exact regime the solver does NOT
  wait. The waiting behavior is NOT-EMBODIED.
- **Cross-remote grouping: yes (saves local tasks; local is bottleneck).** → **EMBODIED** by
  `V8_PRECOMBINE` (one D PRE for many) + `V9_XREMOTE` (cap distinct clouds ≤3).
- **D PRE strategy: batch D PRE (one large group) to save local time.** → **EMBODIED** (V8 batches D PRE).
- **D POST strategy: batch D POST across D PROC groups.** → **EMBODIED** (pick_local D POST batches
  members from READY_D_POST, which span clouds).
- **Local priority: D POST > D PRE > P POST > P PRE (keep decode flowing).** → **CONTRADICTED**. The
  fixed policy is P_PRE > P_POST > D_PRE(gated) > D_POST (scheduler.cpp pick_local). This is the
  opposite order; the throughput regime wants decode POST/flowing first, prefill last.
- **Idling: OK to idle local if waiting for batchmates, as long as clouds are busy.** → **NOT-EMBODIED**
  (and currently SAFE per 6.7: `hold_dpre` is a no-op at w_c=0, so the solver never idles waiting; it
  issues immediately). 7.1 explicitly permits idling here, which the fixed policy does not do.

**Architectural gap (D-038):** the solver is NOT adaptive. To realize 7.1 it must (a) detect
w_tp=1,w_c=0, (b) flip local priority to D_POST>D_PRE>P_POST>P_PRE, (c) enable D_PRE waiting when
transfers are in flight even at w_c=0, and (d) resolve the chunking TENSION (D-020). These are
candidate changes requiring the human's freeze decision and the full 7.2/7.3 matrix to design the
switch boundaries. `Config::w_tp/w_c` are already available.

### PART 7.2 — Waiting-Dominated (w_tp = 0, w_c = 1)

- **Goal**: minimize TDR and TPOT; meet SLOs.
- **Cloud assignment: balance for minimum TDR (earliest effective readiness).** → **EMBODIED/PARTIAL**.
  `DecodeLoadBalanceCloud` (`-DV7_BALANCE`) balances decode load via Lin; "effective readiness"
  (incl. UP-FIFO drain) was explored in D-033 (`effective_ready`) but its `batch_affinity` reward was
  REJECTED — the decode-load core remains. Current key ≈ minimum-TDR balancing; acceptable, refine if
  hidden tests show stragglers.
- **Prefill chunking: minimal (c=1). Chunking delays P PROC → P POST → raises TDR.** → **EMBODIED**.
  Candidate uses `OnePieceChunk`; also consistent with D-020 (chunking collapses throughput/TDR). This
  RESOLVES the 7.1 chunking TENSION: chunking is regime-dependent (minimal here, aggressive only in
  7.1) — so chunking must be ADAPTIVE, not the fixed OnePiece.
- **Decode batching: small (m=1 or 2). Batching delays individual tokens.** → **CONTRADICTED**.
  Candidate uses `GreedyAllBatch` (max size). Over-batching delays tokens → hurts TPOT. NOT-EMBODIED.
- **Cross-remote grouping: only if it doesn't delay tokens; UP serialization hurts TPOT.** →
  **NOT-EMBODIED**. Candidate always does `V8_PRECOMBINE` + `V9_XREMOTE` (≤3 clouds, only lat<50ms).
  In this TPOT-heavy regime cross-remote should be minimal/disabled; the V9 cap helps but does not
  switch off by regime.
- **D PRE strategy: per-request (m=1), start immediately when ready.** → **CONTRADICTED**. Candidate
  batches D PRE (V8). NOT-EMBODIED.
- **D POST strategy: per-request or small groups, complete tokens ASAP.** → **CONTRADICTED**. Candidate
  batches D POST (GreedyAll). NOT-EMBODIED.
- **Local priority: D POST(high-Lout) > P POST > D PRE(high-Lout) > P PRE > D POST(low-Lout) >
  D PRE(low-Lout).** → **CONTRADICTED / NOT-EMBODIED**. Fixed policy is P_PRE>P_POST>D_PRE(gated)>D_POST
  with NO high/low-Lout distinction. The high-Lout-aware ordering is entirely absent. (V6_LOUT was a
  simpler small-high-batch attempt, REJECTED for overloading local — 7.2's nuanced priority is a
  different, unbuilt mechanism; see D-022.)
- **Idling: never idle if a high-Lout request is waiting.** → **PARTIAL/SAFE**. Solver is
  work-conserving (issues immediately), so it does not idle; but the LOCAL PRIORITY (above) governs
  WHICH request is served and is wrong for this regime — the priority defect dominates.

**Note (D-038):** 7.1 and 7.2 prescribe nearly OPPOSITE policies (large vs small batches; D_POST-first
vs high-Lout-aware; chunk-aggressive vs minimal; cross-remote yes vs no). The fixed candidate satisfies
ONLY cloud-load-balancing and (in 7.2) minimal chunking. This makes the adaptive switch (keyed on
w_tp/w_c) **MANDATORY**, not optional. The high-Lout-aware local priority is the most novel unbuilt
piece.

### PART 7.3 — Balanced (w_tp ≈ 0.5, w_c ≈ 0.5)

- **Goal**: optimize the weighted sum.
- **Cloud assignment: balance decode load AND transfer readiness.** → **EMBODIED/PARTIAL**. V7
  balances decode load; transfer-readiness was D-033's `effective_ready` (batch_affinity REJECTED,
  readiness concept extant). Decode-load core OK; full readiness is an additive refinement.
- **Prefill chunking: adaptive — chunk if S small and clouds would otherwise idle.** →
  **NOT-EMBODIED (candidate OnePiece) but MECHANISM EXISTS**. `AdaptiveChunk` (policies.cpp) already
  chunks only when S small / local not bottleneck / up drained / cloud idle — exactly 7.3's rule.
  Reuse for this regime (not in candidate yet). TENSION with 7.1's "aggressive" but 7.3 is the nuanced
  middle.
- **Decode batching: medium (m=2-5); batch at D PROC, not D PRE.** → **CONTRADICTED**. Candidate uses
  GreedyAll (max) and batches D PRE (V8). 7.3 wants medium size AND D PRE per-request (batch only at D
  PROC). NOT-EMBODIED.
- **Cross-remote grouping: yes for D POST (saves local), cautious for D PRE (serialization).** →
  **PARTIAL**. D POST cross-remote ✓ (GreedyAll spans clouds). D PRE cross-remote done unconditionally
  (V8, capped by V9) — NOT cautious. NOT-EMBODIED for the D PRE-cautious part.
- **D PRE strategy: per-request (start early), batch at D PROC.** → **CONTRADICTED**. Candidate batches
  D PRE (V8). NOT-EMBODIED.
- **D POST strategy: cross-remote batch.** → **EMBODIED** (GreedyAll D POST spans clouds).
- **Local priority: context-sensitive — track which component (TDR vs TPOT) is closer to its cliff.**
  → **CONTRADICTED/NOT-EMBODIED**. Fixed priority; no SLO-margin tracking. Most novel unbuilt piece
  (alongside 7.2's high-Lout tier).

### PART 7.4 — The distbase=0 special case

- **Correction/Rule**: if `dist_base == 0`, the waiting component is 1 ONLY if `dist == 0`, i.e. BOTH
  `tdr ≤ SLO1` AND `tpot ≤ SLO2`; otherwise it is 0. All-or-nothing — you must meet BOTH SLOs exactly.
- **Strategy**: conservative — prioritize meeting SLOs over throughput. If you cannot meet both SLOs,
  the waiting component is 0, and you should switch to pure throughput mode (7.1 regime).
- **Detection**: at startup, compute the minimum achievable TDR and TPOT. If they exceed SLO1/SLO2,
  don't bother trying — go pure throughput.
- **Status**: NOT-EMBODIED. The solver reads `dist_base` (protocol.hpp:21) but never branches on it and
  performs no startup SLO-feasibility check; it runs the fixed policy regardless. Embodying this needs
  a startup pass that estimates min_TDR/min_TPOT (see 7.5) and selects pure-throughput when SLOs are
  unmeetable (esp. the distbase=0 all-or-nothing gate).

### PART 7.5 — The cliff detection heuristic

- **Correction/Rule**: at startup estimate
  - `min_TDR ≈ S + prefill_pre(1) + up_transfer + prefill_proc(1) + down_transfer + S + prefill_post(1)`
  - `min_TPOT ≈ S + decode_pre(1) + up_transfer + decode_proc(1) + down_transfer + S + decode_post(1)`
  If `min_TDR > SLO1`: TDR violations unavoidable → don't waste effort on TDR. If `min_TPOT > SLO2`:
  TPOT violations unavoidable → don't waste effort on TPOT. If both avoidable: focus on meeting BOTH
  SLOs, then optimize throughput.
- **Implication**: the regime/switch decision must be informed by a startup feasibility estimate, not
  just (w_tp, w_c). This is the analytical layer the adaptive switch depends on.
- **Status**: NOT-EMBODIED. No startup estimate of min_TDR/min_TPOT exists; the solver does no
  feasibility gating. The formula itself is verifiable against the spec/interactor and is a good
  analysis task before any code is written.

### PART 7 — Synthesis: the adaptive switch (D-038)

The three rows prescribe a small set of POLICY KNOBS that must vary by regime. This is a faithful
extraction of my design prescriptions; the candidate currently fixes
all of them, so the switch is the required embodiment:

- **Cloud assignment**: V7 decode-load balance is universal (embodied in all 3); transfer-readiness
  (D-033) is an additive refinement for 7.2/7.3.
- **Chunking**: 7.1 aggressive / 7.2 minimal / 7.3 adaptive → `AdaptiveChunk` for 7.3, OnePiece for
  7.2,   aggressive variant for 7.1 (the 7.1 aggressive TENSION with D-020 needs my call).
- **Batch size**: 7.1 max / 7.2 m=1–2 / 7.3 m=2–5 → injectable `BatchPolicy` (SizeOne / Medium /
  GreedyAll) by regime; plus D PRE per-request (7.2/7.3) vs batched (7.1).
- **Cross-remote D PRE**: 7.1 yes / 7.2 no / 7.3 cautious → gate `V8_PRECOMBINE` by regime.
- **Local priority**: 7.1 D_POST-first / 7.2 high-Lout 6-tier / 7.3 context-sensitive SLO → a
  regime-selected priority comparator (the biggest unbuilt piece).
- **D_PRE wait/hold**: 7.1 yes / 7.2 no / 7.3 context → gate `hold_dpre` by regime (currently only
  active when w_c≠0).
- **Startup detection layer (7.4/7.5)**: before selecting a regime, estimate `min_TDR`/`min_TPOT`
  from the pipeline formula (7.5) and read `dist_base`. If `dist_base==0` and SLOs unmeetable →
  all-or-nothing gate (7.4); if a component's min exceeds its SLO, deprioritize that component (7.5).
  This startup feasibility estimate informs the regime switch. The formula-vs-spec verification and the
  boundary/derivation math are the analysis tasks. The "math" piece is flagged for verification.
- **Regime detection**: keyed on (w_tp, w_c) from `Config` (protocol.hpp:22), refined by the 7.4/7.5
  startup feasibility estimate. Boundary derivation (continuous weights → discrete regime) is the
  design step — see note.

## PART 8 — Micro-optimizations that add up (D-039)

Small ordering/steering levers that are individually tiny but compound. Most are NOT yet embodied;
8.4 is a property the solver already exploits and 8.5's transfer fact is embodied by V8/V9. These are
Step-8 score-aware micro-tuning candidates (docs/DECISIONS.md Step 8) — none require architecture change,
all need the human's freeze decision (and PART 7 matrix context) before promotion.

### PART 8.1 — Request ID order in groups

- **Correction**: id order within a group is semantically irrelevant, but XDN echoes IDs in your order;
  put high-priority IDs first for easier parse/identification.
- **Implication**: sort group member IDs by priority (e.g. high-Lout / urgent first) before emitting.
- **Status**: NOT-EMBODIED. Batching takes ids in ready-list (FIFO) order; no priority sort. Cheap,
  safe to add (sort ids within a group by urgency).

### PART 8.2 — Assignment-line order controls transfer priority

- **Correction**: tasks finishing together queue transfers in assignment-line order; put urgent D PRE
  first in output so its UP queues first.
- **Implication**: issue urgent D PRE earlier (earlier frame / earlier line) → earlier UP.
- **Status**: NOT-EMBODIED. Single local task per frame (6.6) ⇒ at most one D PRE/frame; urgency does
  not reorder across frames. Tied to the 7.2 high-Lout priority (unbuilt) — realizing that also
  realizes 8.2.

### PART 8.3 — Cloud index ordering penalty

- **Correction**: cross-remote D PRE queues UP in increasing cloud index (C0 first, C7 last); assign
  time-sensitive requests to LOW-index clouds.
- **Implication**: when load-balancing, tie-break urgent requests to the lowest free/low-index cloud.
- **Status**: NOT-EMBODIED. `DecodeLoadBalanceCloud` (`-DV7_BALANCE`, policies.cpp:63) balances load by
  accumulated decode estimate with no cloud-index/urgency steering. Cheap tie-break available.

### PART 8.4 — P PRE transfer queued immediately

- **Correction**: on P PRE completion the UP transfer is queued immediately regardless of cloud busy
  state; it drains when the UP queue is free; the cloud need not be ready.
- **Implication**: P PRE may be issued to a busy cloud — the transfer simply waits in the UP FIFO.
- **Status**: VERIFIED-EMBODIED. P PRE is a LOCAL task; `pick_local` issues it whenever the local
  computer is free and does NOT gate on the target cloud's busy state (interactor P PRE only checks
  `0<=remote<K`, interactor.py:181). So the solver already exploits this; no change.

### PART 8.5 — D PRE queues per-remote transfers

- **Correction**: D PRE queues one UP per distinct remote in the group (C0,C1,C2 → 3 UPs; all-C0 → 1 UP
  carrying all members). Single-cloud D PRE is far cheaper in transfer than cross-cloud.
- **Implication**: cross-cloud D PRE saves LOCAL time but costs TRANSFER time; prefer single-cloud when
  transfer-bound.
- **Status**: EMBODIED (fact) via `V8_PRECOMBINE`+`V9_XREMOTE` (one D PRE local task; UPs per distinct
  remote via `byc[]`, capped ≤3 distinct clouds, scheduler.cpp:144–177). The single-vs-cross tuning is
  implicit in the V8/V9 cap, not separately steered by transfer-regime (see 7.3 cautious cross-remote).

### PART 8.6 — "Below smallest" interpolation (small batches free)

- **Correction**: `get()` clamps below the smallest listed size to that row's time (task_table). So
  `decode_proc(1..min)` all cost the min row; batching up to the smallest listed size is FREE in compute.
- **Implication**: never run sub-smallest solo batches; batch to the smallest listed size.
- **Status**: VERIFIED-EMBODIED. The candidate uses `GreedyAllBatch` (`-DV5_DELAY`-family default,
  Solution.cpp:48) which groups ALL ready — so groups reach/exceed the smallest listed size and exploit
  the clamp as free compute. No change.

### PART 8.7 — "Above largest" interpolation (batch to largest is free locally)

- **Correction**: `get()` clamps above the largest listed size to that row's time. So `decode_pre(m)`
  for `m > max_listed` costs the flat last time — batching D PRE beyond the largest listed size is FREE
  for LOCAL computer time (only transfer/S scale).
- **Implication**: batch D PRE as large as possible for local-time savings.
- **Status**: PARTIALLY EMBODIED. `V8_PRECOMBINE` batches all ready D PRE, capped at `XREMOTE_CAP=3`
  distinct clouds (scheduler.cpp:151, transfer tradeoff per 8.5). Within a single cloud all ready are
  batched → local D PRE compute is flat/free above largest. Cross-cloud is capped for transfer reasons,
  not compute. The compute-free insight is exploited where transfer allows.

### PART 8.8 — Sublinear / linear / superlinear scaling detection

- **Correction**: scan the table at startup; `f(m)/m` decreasing ⇒ batch aggressively; constant ⇒
  batching only saves S/transfer; increasing ⇒ keep batches small. Adapt batch size to the curve.
- **Status**: EMBODIED + PROMOTED. `MarginalBatch` (`-DV3_MARGINAL`, policies.cpp:176–193) grows m
  while `(S + 2*latency_ms + dur(m))/m` strictly decreases — the `2*latency_ms` term amortizes the
  fixed UP+DOWN transfer overhead (the linear transfer term cancels in the per-member average).
  **Promoted as the default batch policy (D-043, 2026-08-15)**: replaces `GreedyAllBatch` in the
  frozen candidate (`build/frozen_v10`). Neutral on linear/sublinear tables (per-member cost keeps
  falling ⇒ grows to full group = GreedyAll); strictly protective on superlinear tables (stops at the
  per-member minimum, capping the batch before the convex explosion). Validated: 30/30 ties vs V9 on
  the visible suite, and +500.322 on two constructed superlinear cases (K=8: 0.189→500.322).

### PART 8.9 — "First token doesn't count for TPOT"

- **Correction**: TPOT = mean of consecutive gaps; the first token's production time is not a gap, so
  the P POST → first D POST delay is FREE for TPOT. The first D PRE can be delayed to batch without
  TPOT penalty; only later iterations' delays hurt TPOT.
- **Status**: VERIFIED-EMBODIED. `SloAwareDelay::hold_dpre` (policies.cpp:213–238) returns `false`
  immediately if ANY ready D PRE request has `iter > 0` (lines 219–221) — i.e. it holds ONLY
  first-iteration (`iter==0`) D PRE, exactly the free-TPOT insight. It also self-disables at `w_c==0`
  (line 215). No change.

### PART 8.10 — "Last finish" dominance in throughput

- **Correction**: throughput = total_tokens / (last_finish − first_arrival); the last finisher dominates.
  Avoid stragglers — when a cloud falls behind, steer new requests to other clouds (balance by estimated
  completion time).
- **Status**: PARTIALLY EMBODIED. `DecodeLoadBalanceCloud` (V7, policies.cpp:63) balances by
  `decode_est` (accumulated decode load) → assigns new requests to the least-loaded cloud, avoiding
  stragglers. It does NOT track estimated completion time including pipe/transfer backlog;
  `QueueAwareCloud` (policies.cpp:26–54) does transfer-aware readiness but is NOT in the candidate.
  Core straggler-avoidance is embodied; a stronger completion-time-aware balance is available, not
  promoted. **Decision (OD-8)**: adopt `QueueAwareCloud` for assignment? Needs A/B.

## Submission artifact (self-contained single file I submitted)

- **What I submitted**: `src/submission.cpp` — ONE file, pasted into the Codeforces C++ editor. It was
  generated by `tools/merge_submission.py` (run via `make submit`) and inlined every layer (headers
  in dependency order, then the `.cpp` units, `Solution.cpp` last) with project `#include "*.hpp"`
  lines stripped and the candidate flags `#define`d at the top. **Clean-style (D-034)**: every system
  `#include <...>` collected into ONE block at the very top of the file; ALL comments stripped;
  only a single short header line preceded the includes. It was TRULY self-contained: I verified it to
  compile in isolation (only that file present) and to score identically to the multi-file
  `solution_v9` build (30/30 ties on cases/zoo+adv; pretest replay PASS).
- **Why not the multi-file tree**: the judge could not pass `-D` flags, so a plain build of the `.cpp`
  set compiled the weak baseline (frozen_v1-level). And a `submission.cpp` that merely `#include`d
  the other `.cpp` files compiled locally but FAILED on the judge (missing files). Hence inline.
- **Sync rule I followed**: whenever the promoted candidate changed, I edited `FLAGS` in `tools/merge_submission.py`
  and regenerated; then I re-ran the FULL freeze gate on the isolated single file (`zoo_gate` vs V1 &
  V5@0.95, `crash_sweep --generate 3000`, sanitizer build of `submission.cpp`). The single file was
  the judged artifact and met all conditions independently of the dev tree.

## Build / candidate map

- `frozen_v1`        : base (no flags) — reference for correctness gate.
- `frozen_v5`        : `-DV5_DELAY` only — prior live candidate.
- `solution_v6`      : `-DV5_DELAY -DV6_LOUT` — 2.1 small-high-batch; NET-NEGATIVE, NOT promoted.
- `solution_v7`      : `-DV5_DELAY -DV7_BALANCE` — 2.3 decode-load balancing; win on stragglers.
- `solution_v8`      : `-DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE` — 3.2 D PRE cross-cloud.
- `solution_v9`      : `-DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE -DV9_XREMOTE` — 3.4 ADAPTIVE
  cross-remote cap; equals V8 at lat<50ms, bounds (K−1)·UP→(cap−1)·UP at lat≥50ms. TOP candidate.

## PART 9 — Local simulator (trace → scorer → visualizer) (D-041 / OD-9)

**Status: BUILT + VALIDATED. Not a policy change — a validation/debugging harness. Freeze-neutral.**

### 9.1 Trace emitter (`interactor.py --trace-file`)

- **Decision**: the trace SOURCE is `interactor.py` and ONLY `interactor.py` — it is the only
  component that simulates the transfer FIFOs (UP/DOWN), so it is the only correct place to log
  DECISION + EVENT lines. The solver stays clean (judge-safe: stdout is still only `n` + assignment
  lines). The trace is self-contained: a `CONFIG` line carries all scoring params + task-table size,
  and `ARR` lines carry `lout`, so the scorer/visualizer need no external file.
- Emits three line kinds:
  - `CONFIG K S latency bw bpt layers SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c N`
  - `DECISION t=.. ASSIGN <srv> <spec...>` (one per assignment line the solver emits)
  - `EVENT t=.. ARR <id> <lin> <lout>` / `EVENT t=.. TDN <srv> <spec> dur=..` /
    `EVENT t=.. XDN <dir> <remote> <size> <tag> <count> <ids...>` / `EVENT t=.. FIN <id>`
- **Two trace bugs fixed during validation**:
  1. The main frame loop exits the instant `done==R`, so the FINAL `FIN` (and any trailing
     `XDN`/`TDN`) was never processed/logged → `fin_t` empty → scorer `tp=0`. Fixed with a
     post-loop drain that logs remaining heap events + transfers for trace completeness only
     (scoring already done from `finish()` directly).
  2. `XDN` events are NOT heap events — they are emitted directly inside `drain_transfers`, so the
     `pop_frame` XDN branch was dead code and XDN trace lines were never written. Fixed by emitting
     the XDN EVENT line inside `drain_transfers` (both main loop and post-loop drain).

### 9.2 Scorer (`tools/score_trace.py`)

- Re-derives the EXACT interactor score from the trace (self-contained; mirrors `Sim.score`).
- **Validated**: on all 30 zoo+adv cases, `score_trace.py` Final == interactor `VERDICT` score,
  **to the full printed precision (3 decimals)**. Prior ~0.002–0.008 diffs were 100% explained by
  the missing-FIN trace bug; after the fix they are exact.
- Score pipeline (confirmed identical to `Sim.score`):
  - `tp = tok_out / (last_finish − first_arrival)`
  - `c_tp = clamp((tp − tp_base)/(tp_UB − tp_base), 0, 1)`
  - `mean_tdr = Σ tdr[i] / R`, `tdr[i] = P POST TDN completion − arrival`
  - `mean_tpot = Σ(span_i) / Σ Lout_i`, `span_i = last_DPOST_TDN − first_DPOST_TDN`
  - `e_tdr = max(0, (mean_tdr−SLO1)/SLO1)`, `e_tpot = max(0, (mean_tpot−SLO2)/SLO2)`
  - `dist = √(e_tdr² + e_tpot²)`
  - `c_w = max(0, 1 − dist/dist_base)` if `dist_base>0`, else `1 iff dist==0 else 0`
  - **Final = 1000·(w_tp·c_tp + w_c·c_w)**

### 9.3 Visualizer (`tools/visualize_trace.py`)

- ASCII Gantt of LOCAL / C0..C{K−1} / UP / DOWN busy intervals + utilization (union-of-intervals
  busy fraction, NOT sum — overlapping transfers must not exceed 100%) + concurrency + heuristic
  flags: cloud load-imbalance, TRANSFER BOTTLENECK (link saturated while clouds idle), local-idle.
- **Validation bug fixed**: `transfer_dur` was multiplying `size * bpt`, but the trace `XDN` `size`
  is already `data_bytes (= len·bpt)` — matching the interactor's `transfer()`. Double-counting
  `bpt` made link intervals span billions of seconds. Fixed; UP/DOWN now show correct saturation.
- **Validated**: runs with 0 crashes on all 30 zoo+adv cases; correctly flags
  `transfer_bottleneck.txt` (UP/DOWN 95% busy, clouds <50%) and stays quiet on `mixed.txt`.
- Usage: `python3 tools/interactor.py --case CASE --solver build/solution_v9 --trace-file /tmp/t.txt`
  then `python3 tools/visualize_trace.py --trace /tmp/t.txt`.

## Pending / open at freeze (post-contest record)

  - **V10 promotion — FROZEN (2026-08-15, D-043)**: V9 (D-042) + `MarginalBatch` promoted as the default
    batch policy (`V3_MARGINAL`). Cleared every freeze gate; neutral on linear tables (30/30 ties vs
    V9) and strictly protective on superlinear decode tables (+500.322 on two constructed cases).
    Superseded `build/frozen_v9`. Checkpoint: `build/frozen_v10` + `build/frozen_v10.manifest`. This was part of the lineage that reached #56.
  - **HELD at freeze (no reliable gain on visible suite; I revisited only where the PART 9 visualizer revealed a concrete
    bottleneck regime on hidden tests)**: PART 8.1–8.3 micro-opts (`solution_part8`, +0.004 net =
    sub-noise, not promoted); 8.10 QueueAwareCloud (V7 already accounted for transfer completion);
    5.4d mirror+stagger (OD-5/D-036, validated exact but neutral); PART 7 adaptive `w_tp/w_c` switch
    (OD-6 — doc 7.4/7.5 had 4 unverified errors; I kept the FIXED policy that shipped).
- **PART 3 subsections**: 3.1–3.4 given and resolved. **PART 4 (local priority queue) RESOLVED
   (D-029 + D-032)**: literal ordering REJECTED (starved prefill); 4.2 (reserve prefill capacity) and
  4.3 (per-request D PRE, batch D PROC/D POST) WERE EMBODIED by the shipped candidate; the 4.1-inspired
   within-decode reorder (D_POST > D_PRE) was A/B tested and REJECTED. No PART 4 code change.
- **V4 chunking (1.2 lever)**: I did not re-test it as the local-interleaving exploit before the freeze; it remained documented.
- **V6_LOUT**: stayed non-promoted (D-022).
- **PART 6 defensive hardening (D-037, OFFERED, not applied before the freeze)**: all 6.1–6.12 were VERIFIED-EMBODIED,
  so no code change was required. Two latent-but-harmless items were identified and offered as
  optional hardening (my sign-off was required before any code change, esp. Layer 1 which is
  frozen-early correctness-critical):
  - **6.3 finished-skip**: make `pop_head` skip a `finished` request and make the FIN handler
    idempotent (`if (req.finished) return;`). Only matters under a hypothetical TDN-first frame,
    which the interactor never produces (it pushes FIN before the TDN). Currently harmless.
  - **6.12 Layer-1 EOF/malformed hardening**: wrap `next_frame` reads in failbit/`try-catch` so any
    read failure → `return false` (exit 0) instead of a `std::stod`/`std::stoi` throw or a release
    no-op mis-parse. The trusted interactor never sends malformed input, so this never fires in the
    contest; offered only for literal 6.12 compliance.
