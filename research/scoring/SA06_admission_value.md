# SA06 — Request Value, Admission/Abandonment, Unfinished Requests, and the Hidden-Lout Information Problem

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA06 drafting). READ-ONLY on src/, tools/, cases/; experiments in
`experiments/sa06/` (cases, `probe_solver.py`, runners, traces) and `/tmp/sa06/`
(superseded — the bash sandbox virtualized /tmp across calls; everything durable was in
`experiments/sa06/`). Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Solver under test: `build/submission` (artifact) and
`experiments/sa06/probe_solver.py` (scripted full-protocol solver with knobs:
`--abandon=`, `--zero-frames=`, `--stop-after-frame=`, `--hold-ppre=rid=,ms=`,
`--hold-dpre1=rid=,ms=` round-1-only first-decode park, `--decode-batch`).

Claim tags: [FACT-statement] = problem.md:LINE; [FACT-code] = local source:LINE;
[DERIVED] = arithmetic/logic from verified facts; [EXPERIMENTALLY SHOWN] = my run
(command + numbers below; every verdict line reproducible with the listed script).

---

## 1. Admission semantics — can deliberately NOT starting (or delaying) a P PRE ever pay?

### 1.1 Formal ground

- Every request MUST complete: score gate `if self.done != c.R: return 0.0`
  (interactor.py:395); statement "every request must still be completed"
  (problem.md:420-421). There is no reject/abort primitive anywhere in the protocol
  (P PRE fixes the cloud; lifecycle problem.md:97-114, 327+). [FACT-statement/FACT-code]

- tp numerator is fixed by the test; the ONLY lever is E. For delays X_i applied to request i:

```text
tp = ΣLout / (last final-token − earliest ARR)
E ≥ max_i (a_i + X_i + chain_i(m))
```

- `tp` — output rate
- `ΣLout` — total tokens
- `E` — elapsed
- `a_i` — arrival of request `i`
- `X_i` — deliberate delay on `i`
- `chain_i(m)` — minimal remaining pipeline at batch size `m`

where chain_i is i's minimal remaining pipeline
given its batch sizes [DERIVED]. Hence:

  - Delay on the eventual LAST finisher shifts E by +X (1:1, unless batching changes
    its chain).

  - Delay on a non-last request shifts E by 0 if the shared resources it would have
    used are idle during [a_i, a_i+X], and can be negative ONLY if the delay moves its
    occupancy out of the true tail owner's critical window or enables S-amortizing
    groups that shorten the tail (fewer per-round S payments inside the makespan).

- waiting: delay of P PRE raises tdr_i by X ⇒ mean_tdr by X/R (SA04 §3); tpot spans of
  the delayed request and (via batching) others move freely; the [P POST→first token]
  window is unmeasured (SA04 Exp B1).

**Net condition for "delay of X ms on request i is profitable" [DERIVED]:**

```text
1000 · [ −V_tp·(ΣLout/E²)·ΔE + w_c/dist_base·(∂dist/∂tpot·Δtpot_pool − ∂dist/∂tdr·(X/R)) ] > 0
V_tp = w_tp/(tp_UB−tp_base)
```

- `V_tp` — marginal tp value
- `E` — elapsed
- `ΣLout` — total tokens
- `ΔE` — elapsed change
- `w_c` — waiting weight
- `dist_base` — waiting budget
- `Δtpot_pool` — pooled tpot change
- `X` — delay ms
- `R` — request count
- `w_tp` — throughput weight
- `tp_UB` — high rate
- `tp_base` — reference rate

with V_tp in the linear region; ΔE ≥ 0 from the delay itself,
Δtpot_pool ≤ 0 if the delay buys bigger groups. Prefill delay pays BOTH taxes (tdr and
E); first-decode parking ("dead zone", SA04) pays only the E tax and only if the parked
request is/becomes makespan-critical ⇒ parking weakly dominates prefill delay.

### 1.2 Experiment (crafted, `experiments/sa06/cases/adm_*`)

Setup: K=2, S=5, lat=1, bw=1e9, bpt=1, layers=1; decode table 1.25→3.25 (m 1→64,
sub-linear); rid0 arrives t=0 (Lin=4, Lout=12 in `adm_base`, Lout=3 in `adm_short0`),
herd of 8 arrives t=50 (Lin=4, Lout=3). Policies: `imm` (start everything ASAP, batch
all ready decodes), `holdppreX` (delay rid0's P PRE by X — herd ARR frame wakes the
solver, so no stuck), `park1_50` (delay only rid0's FIRST decode round by 50 ms —
EMPTY wake-up frames every 1 ms make the park fireable). Command:
`SA06_ARGS="<knobs>" python3 tools/interactor.py --case cases/adm_base_em --solver
experiments/sa06/probe_solver.py` (batch driver `adm_run.py`).

| case (weights)                                 | policy       | score       | E       | tdr        | tpot   | last            |
| ---------------------------------------------- | ------------ | ----------- | ------- | ---------- | ------ | --------------- |
| adm_tp (w_tp=1)                                | imm          | **319.479** | 375.611 | 68.889     | 27.480 | rid0            |
| adm_tp                                         | holdppre25   | 301.802     | 397.611 | 72.778     | 23.880 | rid0            |
| adm_tp                                         | holdppre50   | 287.841     | 416.897 | 89.556     | 23.861 | rid0            |
| adm_tp                                         | holdppre75   | 282.213     | 425.210 | 89.556     | 23.226 | rid0            |
| adm_tp                                         | park1_50     | 296.372     | 404.897 | 68.889     | 23.861 | rid0            |
| adm_wc (w_c=1)                                 | imm          | 122.844     | 375.611 | 68.889     | 27.480 | rid0            |
| adm_wc                                         | holdppre25   | 297.897     | 397.611 | 72.778     | 23.880 | rid0            |
| adm_wc                                         | **park1_50** | **303.012** | 404.897 | **68.889** | 23.861 | rid0            |
| adm_base (.5/.5)                               | imm          | 221.162     | 375.611 | 68.889     | 27.480 | rid0            |
| adm_base                                       | holdppre25   | 299.850     | 397.611 | 72.778     | 23.880 | rid0            |
| adm_base                                       | park1_50     | 299.692     | 404.897 | 68.889     | 23.861 | rid0            |
| adm_short0 (.5/.5, rid0 Lout=3 = non-critical) | imm          | 198.822     | 226.333 | 68.889     | 30.508 | rid1            |
| adm_short0                                     | holdppre25   | 321.867     | 225.647 | 72.778     | 24.951 | rid1            |
| adm_short0                                     | holdppre75   | 306.699     | 238.460 | 89.556     | 24.465 | rid0 (flipped!) |
| adm_short0                                     | **park1_50** | **335.200** | 226.460 | 68.889     | 24.465 | rid1            |

[EXPERIMENTALLY SHOWN]

1. **tp channel: delay NEVER shortened E** — in every variant E grew (+22.0…+49.6),
   and with w_tp=1 every delay lost points (−17.7 to −37.3). The "delay enables
   batching that finishes everything sooner" channel did NOT materialize even in a
   steep-S, sub-linear-decode crafted case with a later herd: the herd batches by
   itself; rid0's delayed pipeline only pushed its own chain later.

2. **The profitable-delay mechanism is entirely the waiting component via tpot
   (span compression by joining bigger groups)**: tpot 27.48→23.86 (span −40 ms) was
   worth up to +180.2 pts at w_c=1 — enough to dominate the −41.3 ms E damage at the
   crafted SLOs (SLO2=10, dist_base=2, tp in linear band).

3. **First-decode parking dominates prefill delay**: park1_50 achieved the same tpot
   gain with tdr EXACTLY unchanged (68.889, the dead zone) and smaller E cost than
   holdppre50 (404.9 vs 416.9). On the non-critical rid0 (adm_short0) the park was
   almost free on E (226.33→226.46) and worth +136.4.

4. Delay can CREATE a new straggler: adm_short0 holdppre75 flipped the last finisher
   from rid1 to rid0 (the SA03 tail-owner effect, admission-triggered).

5. Landscape is rugged (holdppre25 > holdppre50 < holdppre75 at .5/.5) — SLO clamps
   make the payoff nonmonotone in the delay; any implemented delay needs the
   marginal-value machinery, not a fixed constant.

Artifact reference on the same case: 183.573 (E=380.2, tdr=68.9, tpot=28.9) — it
parks rid0's first decode by 20.75 ms (P POST t=20 → first token t=40.75) but reaps
less span compression than the probe's aggressive batcher (tpot 28.9 vs 23.9;
group caps bind). [EXPERIMENTALLY SHOWN]

### 1.3 Verdict

- **NEVER abandon**: no score channel exists for a request that never completes; the
  test zeroes (§2).

- **NEVER delay P PRE as a scoring play**: it is strictly dominated by first-decode
  parking (pays tdr 1/R per ms AND pushes the whole pipeline; park pays neither).
  The only legitimate P_PRE delay is congestion relief (the V11 `up_full` throttle,
  D-142/D-144 evidence) — a queue-valve, not a value play.

- Deliberate admission delays are profitable ONLY in the waiting component (tpot span
  compression / batch formation), and the right place to buy them is the
  [P POST→first token] dead zone — which is exactly where the artifact's
  `hold_dpre`/`DPRE_ACCUMULATE` machinery operates [FACT-code scheduler.cpp:158-191,
  policies.cpp:283-360].

---

## 2. Stuck-state trigger analysis (abandonment)

### 2.1 Code walkthrough [FACT-code tools/interactor.py]

`Sim.stuck()` (interactor.py:375-392) returns NOT-stuck if any of four escape
channels exists:

1. some unfinished request is mid-task (phase in `*_RUN`),
2. any UP/DOWN transfer is in flight,
3. some request slot is still `None` (future arrival possible),
4. some unfinished request is in a READY phase ("a ready task exists but solver
   refuses (still legal)").

Call site (interactor.py:658-662): reached only when `t = next_frame_time()` is
`None` — heap empty AND both FIFOs empty. Then:

```python
if t is None:
    if sim.stuck():
        raise ValueError("stuck")
    raise ValueError("stuck")

```

— **both branches raise** (duplicated-raise quirk): the channel-4 branch of stuck()
is dead at this call site. Net local semantics: with done<R, once no task, transfer,
or arrival can produce another frame, the verdict is `stuck` → score 0 — including
the case where the unfinished requests are READY and the solver simply keeps
responding 0. This matches the statement's semantics (problem.md:382, 390-396): a
ready task only creates an event if the solver actually issues it; a solver refusing
forever leaves "no possible future event".

`next_frame_time()` (interactor.py:339-346) = earliest of event-heap head and both
transfer-FIFO heads; case-level `EMPTY` idle frames (interactor.py:651-655) are an
extra local-only frame source.

### 2.2 Experiments (`experiments/sa06/cases/stuck4`: 4 arrivals at t=0,0,100,200)

Command pattern: `SA06_ARGS="<knobs>" python3 tools/interactor.py --case cases/stuck4
--solver experiments/sa06/probe_solver.py [--trace-file tr]`.

| probe                                                            | what the solver does                                                                                                                   | verdict       | score   |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------- | ------- |
| base                                                             | completes all                                                                                                                          | ok, 58 frames | 500.018 |
| abandon0                                                         | never issues rid0's P PRE; rid1-3 complete (FIN 1/2/3 at t=24/124/224)                                                                 | **stuck**     | 0.000   |
| zero1                                                            | responds 0 at frame 1 (t=0); arrivals at 100/200 still pending                                                                         | ok, 51 frames | 500.018 |
| zero99                                                           | responds 0 forever; after last ARR (t=200) no frame source remains                                                                     | **stuck**     | 0.000   |
| stop-after-frame=5                                               | works 5 frames then refuses; run advances through remaining in-flight transfer (XDN t=8) AND future arrivals (t=100, t=200), then dies | **stuck**     | 0.000   |
| stop-after-frame=57                                              | refuses only the final (FIN) frame; done==R                                                                                            | ok            | 500.018 |
| stop-after-frame=3 on stuck4_empty (EMPTY frames at 300/400/500) | local-only empties keep framing briefly                                                                                                | **stuck**     | 0.000   |

[EXPERIMENTALLY SHOWN] Consequences:

- Stuck detection is NOT immediate on abandonment: with other work in flight the run
  continues to the very end (abandon0 survived until t=224) — but the score is still

  0. Abandonment is unprofitable regardless of when detection fires.
- "Respond 0 mid-stream" is safe exactly while any of the four channels lives —
  future arrivals count (statement problem.md:393-395 says this verbatim: "While
  future arrivals remain, responding with 0 assignments is safe because time advances
  to the next arrival").

- Refusing a ready task with all channels dry = stuck (the statement's "permanently
  abandoning it can cause this stuck state", problem.md:395-396).

- A request mid-pipeline whose next task needs a free computer that IS free: not
  stuck if the solver issues it (base run). No special case needed.

- EMPTY frames only defer the verdict (local harness artifact; the real judge has no
  idle frames).

### 2.3 The parked-no-wake-up stuck (new failure shape)

A deliberate park/hold that lets ALL channels drain is stuck MID-RUN, not just at
the end: probe `--hold-dpre1=rid=*,ms=400` on `lullB_noem` (10 requests, no future
arrivals, no empties) → **stuck, 0** (the identical policy on `lullB` WITH empties
completed). Mechanism: after the herd's last in-flight event, the solver's only
pending action is the held D PRE; the protocol has **no timer** (problem.md:364 "no
timer or self-wake mechanism"), so nothing will ever produce the next frame.
[EXPERIMENTALLY SHOWN + FACT-statement]

The artifact is structurally immune [FACT-code]: every wait it takes is backed by an
in-flight event — `SloAwareDelay::hold_dpre` requires `pipe>0` (prefill in flight,
policies.cpp:317-318) or the cliff-gate imminent-P_POST case (events from the P PROC
chain); `dpre_acc_skip` requires `dpost_n>0` or `pipe>0` (scheduler.cpp:170-190);
the `up_full` P_PRE throttle implies in-flight UP transfers (scheduler.cpp:104).
**Any new park/admission logic MUST preserve this invariant** (wait only while an
event is in flight), or it can zero a test.

---

## 3. Equal token value, unequal cost — per-regime priority vs current ordering

### 3.1 Per-request resource charges [DERIVED from statement cost model]

For request i (Lin_i, Lout_i), riding decode groups of size m (split m/K per cloud):

```text
A_i = 2S + ppre(Lin_i) + ppost(Lin_i) + Lout_i·(2S + dpre(m) + dpost(m))/m
B_i = S + pproc(Lin_i) + Lout_i·(S + dproc(⌈m/K⌉))·K/m
bytes per token ≈ 2·bpt·(Lin_i + Lout_i)
xfer(t) = latency + t·bpt·8/bw
```

- `A_i` — local occupancy of request `i`
- `B_i` — cloud occupancy of request `i`
- `Lin_i` — input length
- `Lout_i` — output length
- `S` — setup cost
- `m` — decode group size
- `K` — cloud count
- `bpt` — bytes per token
- `xfer` — transfer time

- **Link occupancy**: UP `xfer(Lin_i) + Lout_i·xfer_units(m/K per cloud sub-list)`, DOWN
  `xfer(Lin_i) + Lout_i·xfer(m)/m` (latency amortizes over group size).

Priority keys under saturation of the binding resource (maximize tokens per
binding-resource ms):

```text
local-bound: Lout_i / A_i
link-bound: Lout_i / (2·(Lin_i + Lout_i))
cloud-bound: Lout_i / B_i
```

- `Lout_i` — output tokens
- `A_i` — local cost
- `B_i` — cloud cost
- `Lin_i` — input length

### 3.2 What the artifact actually orders by [FACT-code]

| decision                    | ordering key                                                                                                      | site                                                           |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| P PRE admission             | **SJF by Lin** (smallest-Lin bucket first; FIFO in bucket)                                                        | state.cpp:458-478 (`ppre_pop`), PPRE buckets state.hpp:134-144 |
| P PRE vs P POST arbitration | tdr urgency (marginal value) / `q_j < a_i` prefill-post locality                                                  | scheduler.cpp:108-157                                          |
| P PROC                      | **SPT by Lin** per cloud (Smith's rule, mean-TDR argument)                                                        | scheduler.cpp:295-315                                          |
| D PRE group assembly        | **cloud bucket with most ready**; FIFO head within bucket; fill home cloud then most-ready others up to m/eff_cap | scheduler.cpp:160-259                                          |
| D PROC / D POST             | FIFO from ready lists up to group_size cap                                                                        | scheduler.cpp:275-285, 327-338                                 |
| cloud assignment            | EFT balance with `this_lout = max(Lin, predict_lout)` decode debt; tier-1 Lin≥256 isolation                       | policies.cpp:12-84                                             |

Findings:

- **There is NO per-request value key anywhere in decode ordering** — D PRE is
  pure batch-size greedy + FIFO. Under caps that bind (cross-cloud `eff_cap`,
  group_size), which requests ride first is decided by bucket arithmetic, not by
  tokens-per-resource. [FACT-code]

- The link-bound key `Lout/(Lin+Lout)` exists nowhere. It is computable at runtime
  only through `predict_lout` (hidden Lout) — see §4.

- SJF-by-Lin (P PRE) and SPT-by-Lin (P PROC) are TDR-optimal (Smith) but under
  POSITIVE Lin↔Lout correlation they systematically defer token-dense requests to
  the tail — the admission-side generator of SA03's straggler class (69.7-pt crafted
  swing; adv_straggler 3.2× tp loss). The Lin≥256 isolation tier is the existing
  mitigation. [DERIVED + SA03 cross-ref]

- In link-saturated regimes (m15, adv_abyss, x1 class — SA03 floor table), what
  matters is admission ORDER of prefills onto the link (fat Lin=4096 requests block
  UP for everyone); the V11 inflight cap throttles count, not bytes — a
  tokens-per-byte admission order is the unimplemented regime-correct key.

---

## 4. Hidden-Lout planning

### 4.1 What is knowable [FACT-statement/FACT-code]

- Lout[i] is revealed ONLY by FIN (problem.md:97-99), which arrives beside the final
  D POST TDN (problem.md:242) — i.e. after the request's last token: useless for the
  request itself, a sample for the pool.

- Live censoring signal: a request that has produced `iter` tokens and is not FIN has
  `Lout ≥ iter+1`. The solver tracks `iter` (state.hpp:45) but **uses it in NO
  decision** (grep: only FIN/ewma accounting state.cpp:284, oracle ntok
  Solution.cpp:365). [FACT-code]

- Current estimator: `predict_lout(id)` = per-Lin-bucket running MEAN of FINished
  requests' Lout, bucket = min(Lin,4096); falls back to global `ewma_lout_` (prior
  10.0, update 0.05·obs+0.95·old at each FIN) (state.cpp:603-608, 284;
  state.hpp:353-357). Consumers: cloud EFT decode-debt (policies.cpp:43-44) only.
  `ewma_lout` additionally gates `low_lout_regime` in MarginalBatch (policies.cpp:215-216).

### 4.2 Is Lout correlated with anything exploitable?

- Local generators: zoo_gen draws Lin and Lout INDEPENDENTLY (zoo_gen.py:72-76).
  Census over all 94 on-disk cases (`cases/*/*.txt`): **85/94 have Lin or Lout
  constant within the case** (per-bucket estimation degenerates to the global mean);
  adv_straggler corr=+1.000 (Lin=Lout=512 straggler by design — the generator
  comment says "large-Lin request as a proxy for large Lout", adversarial_generator
  ~line 165-167); adv_living_cliff corr=+0.476; 2 more weak (0.05-0.15).
  [FACT-code census]

- CF examples: Example 1 has a single request (Lin=4, Lout=1); Example 2 is a decode
  excerpt. **The real Lin↔Lout correlation is unknown** — an open question with
  high value (it decides whether Lin-based bucketing is worth its complexity).

### 4.3 Estimator critique and recommendation [DERIVED]

1. **Temporal/survivorship bias**: bucket means are updated only at FIN; early
   samples skew short-Lout. Early-case `predict_lout` underestimates exactly when
   cloud assignment happens (all P PREs are early).

2. **Wrong functional for tail decisions**: makespan depends on max/upper quantiles
   of remaining work, not means; `low_lout_regime = ewma < 2.5` misclassifies bimodal
   pools (half Lout=1 + half Lout=100 → ewma≈50 ⇒ "not low" though half the pool
   contributes no TPOT gaps).

3. **Free censoring ignored**: `max(μ_bucket, iter+1)` is a strictly better
   remaining-work estimate for unfinished requests, available at zero cost.

4. Recommendation: keep the bucket mean as μ; add (a) survival-conditioned point

   estimate `E[Lout_i | alive] = max(μ_bucket, iter_i+1)` and expected REMAINING

   tokens `= that − iter_i` as the tail-priority key (the only live counterfactual
   signal), and (b) if a regime gate is kept, gate on the observed GAP-COMMITTED
   fraction (share of unfinished requests with iter≥1) rather than ewma. Material
   difference expected only on heterogeneous correlated cases (4/94 locally) and the
   endgame/tail — which is precisely where SA03 showed the big swings live. No
   distribution-aware machinery beyond this is justified by current evidence.

---

## 5. No horizon — "unfinished at end" is not a scoring concept

[FACT-statement]

- Arrival timestamps are bounded (`nondecreasing in [0,1e9]` ms, problem.md:286) but
  "Completion frames may exceed 1e9 … validator guarantees a conservative upper
  bound of 1e12 ms even if every legal task and transfer is serialized"
  (problem.md:287-289). At most 2·10^6 frames per test (problem.md:291).

- Termination: "When all requests have finished, the interactor sends END … after
  reading your response to the final frame" (problem.md:398). R is never announced;
  "there is no 'no more arrivals' signal — you cannot distinguish a lull from the end
  of the stream. This is intentional" (problem.md:399-401).

- Therefore: the test runs until all-FIN (or 0 via stuck/protocol error; or TLE 15 s
  wall / MLE 256 MB, problem.md:36-37). A request cannot be "left unfinished at a
  horizon" — leaving it unfinished means stuck (→0) whenever the stream has truly
  ended. Local judge identical (`done != R → 0.0`, interactor.py:395).

---

## 6. Tail under uncertainty (lulls vs end-of-stream)

### 6.1 Current behavior [FACT-code]

The artifact never waits for hypothetical arrivals: every hold/skip path requires an
in-flight event (§2.3) — at a true lull with drained work it simply has nothing to
do; after the true last arrival it drains greedily. There is no lull-depth heuristic
anywhere in src/ (the `last_arr_` meter field exists only for the [METER] stderr
line, Solution.cpp:344-377).

### 6.2 Race-vs-park experiment (`experiments/sa06/cases/lullA_*`, `lullB*`)

Setup: herd 10×(t=0, Lin=4, Lout=4); cluster 10×(t=400..404.5, Lout=8); S=5, K=2,
same sub-linear decode table; EMPTY frames every 1 ms (so the park policy is
fireable). `parkfirst400` = hold every herd request's FIRST decode round by 400 ms
(they wake at the cluster ARR). Commands in §1.2 pattern; stats via `adm_run.py`
parse_trace.

| case                                | policy       | score           | E        | tpot   | last finisher   |
| ----------------------------------- | ------------ | --------------- | -------- | ------ | --------------- |
| lullA_tp (w_tp=1)                   | imm (race)   | **659.139**     | 728.222  | 25.070 | rid11 (cluster) |
| lullA_tp                            | parkfirst400 | 425.605         | 1127.806 | 25.257 | rid19 (cluster) |
| lullA_tp                            | artifact     | 659.139         | 741.333* | 30.341 | rid11           |
| lullA_half (.5/.5)                  | imm (race)   | **450.991**     | 728.222  | 25.070 | rid11           |
| lullA_half                          | parkfirst400 | 329.575         | 1127.806 | 25.257 | rid19           |
| lullA_half                          | artifact     | 323.741         | 741.333  | 30.341 | rid11           |
| lullB_noem (NO cluster, no empties) | imm          | 707.368         | —        | —      | —               |
| lullB_noem                          | parkfirst400 | **stuck 0.000** | —        | —      | —               |

[EXPERIMENTALLY SHOWN]

- **Eager racing dominates lull parking on every axis**: parking pushed E +399.6 ms
  (the park delay transferred straight into the herd's completion, which then
  OVERLAPPED the cluster's tail — last finisher stayed a cluster member but 399 ms
  later), lost 233.5 pts (w_tp=1) / 121.4 pts (.5/.5), and on the no-cluster twin
  the identical policy scores **0 (stuck)**. The asymmetry is brutal: parking risks
  the whole test for at most a tpot-span improvement.

- Why racing wins structurally [DERIVED]: during a lull the shared resources are
  IDLE — draining the backlog then costs zero opportunity (tokens produced in idle
  time are free); parking saves resources that were not contended, delays spans, and
  can flip the tail owner. Batching pays only when the saved resource is the binding
  constraint of the makespan — i.e. waits should be bounded by IN-FLIGHT pipeline
  depth (the DPRE_ACCUMULATE design), never by hoped-for arrivals.

- *Artifact observation (crafted corner, not a regression claim): on lullA_half the
  artifact drained the herd's last rounds as 5 singleton D PREs during the lull
  (t=175-210, m=1 groups after the m=10 rounds — round desynchronization), giving
  tpot 30.34 vs the probe batcher's 25.07 and scoring 323.7 vs 451.0. The eager
  race is right; the round-sync at the drain tail is the soft spot. (E 741.3 vs
  728.2: the artifact's D PRE count 22×m10 + 10×m1 + 2×m5 vs probe 40×m5 + 4×m10.)

### 6.3 Optimal endgame policy sketch [DERIVED]

At any moment with no arrival for τ and unfinished decodes remaining, the solver
cannot distinguish lull from end. The risk-asymmetry (stuck = −everything vs
tpot-span points) plus the idle-capacity argument yield: **race always; permit only
waits that are (i) bounded by in-flight pipeline depth and (ii) vanish when the last
event drains** — exactly the artifact's current invariant (§2.3). A refinement with
measurable upside: during the drain tail (no pipe>0 but D_PRE backlog remains),
synchronize remaining rounds into maximal groups rather than letting per-request
round skew fragment the tail into singletons (§6.2 artifact observation).

---

## 7. Strategic implications

1. Admission is closed: never abandon, never delay P PRE for value; the ONLY
   profitable deliberate delay lives in the [P POST→first token] dead zone and is
   paid for by tpot span compression (measured up to +180 pts at w_c=1 crafted
   SLOs; +136 even at .5/.5 with E unchanged). Any new dead-zone park must keep an
   in-flight event or it can zero the test (park-no-wake-up stuck, §2.3).

2. The artifact's wait machinery is in the right place (dead zone, event-backed);
   its caps reaped less compression than an aggressive batcher on the crafted
   tpot-priced case — the D-119 crossing priced tpot relief at the SLO edge; when
   e_tpot was already far past SLO2·dist_base the cap only added span. Recorded
   candidate follow-up at program close: allow the crossing to consider "span compression beyond the cliff"
   (marginal tpot value was 0 once dist ≥ dist_base — cap would then have flipped to
   throughput-optimal, which Twist 3 half-did).

3. Per-request value ordering is absent in decode (FIFO) — under binding caps the
   regime-correct keys are Lout/A (local-bound), Lout/(2(Lin+Lout)) (link-bound),
   Lout/B (cloud-bound), all computable via predict_lout. The link-bound key is the
   most cleanly missing one for the m15/x1/abyss class.

4. Hidden Lout: bucket-mean + ewma is adequate for 85/94 local cases (degenerate);
   the two real upgrades are the free censoring bound (max(μ, iter+1)) for
   remaining-work/tail priority — currently iter is entirely unused — and a
   gap-committed gate instead of ewma<2.5 for low_lout_regime.

5. Endgame: race, always (measured −121 to −234 pts for parking, plus the stuck
   trap); the improvement lever is round-synchronization at the drain tail, not
   waiting.

6. SJF/SPT-by-Lin admission is TDR-correct but straggler-generating under positive
   Lin↔Lout correlation; the Lin≥256 isolation is the current antidote; a
   token-density admission key (§3) would subsume it if correlation is real on CF.

## 8. Open questions recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026)

1. **CF Lin↔Lout correlation** (decided §3/§4 priorities): unanswerable from the two
   examples at the time; could have been probed indirectly only if a later judgement dump had revealed
   per-test token counts vs input bytes.

2. Whether any REAL CF test sat in the "tpot-priced + staggered lull" corner where the
   artifact lost 127 pts to the eager batcher (lullA_half). The mimic family had no
   such case (m17c/m17d were the closest — drip-fed single-decode). A mimic
   (`lull + cluster + SLO2-blown`) would have let the pool_pick/round-sync idea be A/B'd (not built at program close).

3. The remaining task at close was to quantify the drain-tail desynchronization on the 68-case suite (count singleton
   D PREs issued while count_phase(READY_D_PRE)>1 and pipe==0) to size the §6.3
   refinement before building it.

4. The stuck() channel-4 dead branch + double raise (interactor.py:658-662): harmless
   locally (verdict identical) but the comment "still legal" was misleading — recorded as worth a
   one-line fix note if tools/ were ever unfrozen.

5. TLE risk of no-horizon: worst case the solver had to finish ΣLout ≤ 2·10^5 tokens
   within 2·10^6 frames and 15 s wall — no evidence of frame-count pressure in
   battery runs; not re-verified in that session.

---

### Reproduction index (experiments/sa06/)

- `probe_solver.py` — knob solver (abandon/zero/stop/hold-ppre/hold-dpre1/decode-batch).
- `mkstuck.py`, `mkadmission.py` + inline lull generators in the transcript — case
  generators (`cases/stuck*`, `adm_*`, `lull*`).

- `adm_run.py` — policy×case driver + trace parser (E/tdr/tpot/last).
- Traces: `tr_abandon0.txt`, `tr_stop5.txt`, `tr2_adm_*` (admission table),
  `tr3_*_artifact.txt`, `tr4_lull*` (lull table).
