# SB02 — Deterministic Forward Model + Rollout/MPC Search

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB02). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
Scope (archival): the compute-the-answer engine that was scoped to
replace hand-tuned heuristics inside `src/submission.cpp` (C++17, ≤65,535 chars,
single-threaded, 15 s/test, ≤2·10^6 frames), for CF 2251A (ended 2026-08-28).

Claim tags: **[FACT]** = statement/protocol fact (problem.md, verified lines cited);
**[FACT-repo]** = verified against this repository's code/judge; **[PROVEN-theory]** =
stated and verified in a downloaded paper (path given); **[DERIVED-here]** = my derivation
from verified facts; **[HEURISTIC]** = plausible, no guarantee.

Prerequisites read:

- `research/techniques/SB01_online_scheduling_theory.md` (gradient calculus D1–D8, §4.1 coefficients `V_E` / `V_P` / `V_S` — reused verbatim below).
- `tools/interactor.py` (the local judge = the exactness blueprint).
- `src/scheduler.cpp`, `src/policies.cpp`, `src/state.hpp`.
- D-147/D-150/D-151/D-152/D-153 in `docs/DECISIONS.md`.

The license, verified: "For a fixed test it is deterministic and non-adaptive: the same
legal responses produce the same events and score" (problem.md:38) and "If you simulate
ahead for planning, use ordinary double-precision arithmetic with the piecewise-linear
lookup and transfer formula above" (problem.md:290). Everything except future ARR
times/Lins and un-revealed Lout is computable exactly inside the submission.

---

## 1. Forward-model architecture (Task 1)

### 1.1 What "the state" is, minimally

The judge's event stream is huge (up to 2·10^6 frames), but the set of *pending causes*
at any instant is tiny. After applying the current frame's events, the entire future of
the closed part of the system (no new arrivals) is determined by:

| Block | Contents | Size |
|---|---|---|
| `e_free` | local computer free time (0 if idle now) | 1 double |
| `c_free[K]` | per-cloud free time | K ≤ 8 doubles |
| `up_q` | UP FIFO entries not yet delivered: `(finish_time, len, member ids/payload ref)` | depth = in-flight transfers |
| `down_q` | DOWN FIFO, same shape | same |
| `req[]` | per request: `phase` (17 values, uint8), `iter` (uint16), `next_ls` (uint8), `cloud` (int8), `lin` (immutable), `lout_hat` (predictor) | R ≤ 2000 rows |
| `ora` | oracle deltas: `sum_tdr, n_tdr, sum_span, n_gaps, tokens, last_finish, first_arr` | 7 doubles |

**[FACT-repo]** This is exactly the information `tools/interactor.py::Sim` keeps
(`local_busy_until`, `cloud_busy_until[K]`, `up`/`down` FIFO lists, `req[]` phase
records, running sums), and exactly what `State` (`src/state.hpp`) tracks live. The
solver already maintains the model; what is missing is a *hypothetical-branch* copy of
it that can be advanced without touching the live state.

### 1.2 Event queue: NO heap — an O(K) min-scan

The pending deterministic event set at any model instant is:

- ≤ K+1 task TDNs (one per computer currently busy) — each is `free_time` itself;
- the UP head finish and the DOWN head finish (FIFO: only the head can complete next;
  note a head completing at time τ reveals the next head, which may also finish at τ —
  the scan must loop while `|head − τ| < TIE_EPS`);

- the next *sampled* future ARR (see §2) — 1 per active future sample.

So `next_event_time() = min` over ≤ 11 timestamps: an O(K) scan, no priority queue, no
allocation. **[DERIVED-here]** The judge's big heap exists because it prints every event;
the *causal* state has ≤ K+3 pending causes. Cost per simulated event ≈ one O(K) scan +
one table lookup + a few writes: ~60–250 ns in C++ (table lookup dominated; see §3.5
for the m-cache that makes it ~20 ns).

### 1.3 Event application (mirror of the judge, op-for-op)

Per `tools/interactor.py::apply_one` (verified line-by-line):

- Task assigned at frame time t on computer X: `dur = table(step, m)` (or
  `(le−ls)/L · table(P_PROC, Lin)` for pieces — **[FACT]** problem.md:149);
  `free[X] = t + S + dur`; the TDN fires at `free[X]`.

- P PRE completion enqueues UP with `len = Lin` **at the TDN time**; D PRE completion
  enqueues one UP per distinct cloud, entering **in increasing cloud index**
  **[FACT]** problem.md:164/206; P PROC last-piece and D PROC completions enqueue DOWN
  (`len = Lin` / `len = m`).

- FIFO enqueue: `finish = max(enter, tail_finish) + latency + 8·len·bpt/(bw·1e6)`
  **[FACT-repo]** interactor.py:127-130 — the *tail* is the last finish time, not now.

- XDN delivery pops the head; readiness flips exactly as `self_apply_xdn` does.
- D POST completion: +1 token/member; FIN at `iter == Lout` (uses `lout_hat` in the
  model — the *only* non-deterministic input besides future ARRs).

- Frame boundary: all events with `|τ − t| < TIE_EPS` coalesce into one decision turn
  **[FACT]** problem.md:242 ("only events with exactly the same internal timestamp are
  coalesced"; the local judge implements `abs(...) < 1e-9`, interactor.py:352/370).

### 1.4 Per-rollout state handling: undo journal, not copy

Full `req[]` is ~2,000 × ~8 bytes ≈ 16 KB; FIFOs can hold hundreds of entries. Copying
per candidate (≤ 23 candidates × 10^4 decision points) is 10-100 MB/s of memcpy —
wasteful and allocation-hostile. **[DERIVED-here]** Use a rollback journal:

```
struct Undo { void* p; uint64_t old; };   // 16 bytes
vector<Undo> undo;                        // depth cap 4096, reserve()d once
template<class T> void setv(T& x, T v){ undo.push_back({&x, *(uint64_t*)&x}); x = v; }
void rollback(size_t mark){ while (undo.size()>mark){ restore; pop; } }
```

Each simulated event writes ≤ ~12 words (one free time, one FIFO finish, one phase,
one iter, oracle accumulators) → a rollout of H=48 events journals ≤ ~600 entries =
~10 KB of stack-free writes ≈ 1–2 µs of journaling per rollout; rollback is the same
magnitude. Group membership during D PRE candidates pops ready-list heads — intrusive
lists (existing `rl_*` machinery) journal cleanly. The immutable majority (`lin`,
`cloud`, table, cfg) is never touched. **[DERIVED-here]** This is the standard undo-log
DFS used in game-tree search; it makes candidate evaluation ~free of state cost.

### 1.5 Tie-order and double-precision exactness

**Bit-exactness is unattainable — and provably so within this repo.** The solver's
table interpolation computes `lo + ((b−b_lo)/(b_hi−b_lo))·(hi−lo)`
(src/task_table.cpp:62-63 — divide first, then multiply), while the local judge computes
`v1 + (v2−v1)·(b−b1)/(b2−b1)` (interactor.py:80 — multiply first, then divide).
`f·d` vs `d·num/den` differ in the last ulp on some inputs; the CF judge is a third
implementation. **[FACT]** The statement licenses the *formula* ("ordinary
double-precision arithmetic with the piecewise-linear lookup and transfer formula",
problem.md:290), not bit-identity.

**Therefore the model requirement is ε-exactness with tie-robust decisions:**

1. `TIE_EPS = 1e-9` (same scale as the 9-decimal printed timestamps and the local
   judge's coalescing tolerance). Two model events within `TIE_EPS` are treated as
   simultaneous (one frame), matching the judge's coalescing rule. A 1-ulp prediction
   error (~1e-13 at t≈1000 ms) is three orders below `TIE_EPS` — it can only flip
   whether two events land in the *same or adjacent* frames, never a big reorder.

2. FIFO *entry* order is known exactly — it is our own output order (assignment-line
   order for same-finishing tasks, increasing cloud index within a D PRE)
   **[FACT]** problem.md:206. FIFO *delivery* order is fixed at entry, so ε errors
   never reorder deliveries; they only wobble the timestamp by <TIE_EPS.

3. Within-frame event order carries no scheduling priority **[FACT]** problem.md:242 —
   our base policy in the model must be written to read the whole coalesced frame
   before deciding (the live solver already does this), so within-frame order
   divergence is semantically void.

4. **ε errors never accumulate across the test.** Every frame, the live state is
   re-synchronized to *observed* events (TDN echoes `dur`; XDN frame time is the
   delivery time; ARR is exact). The rollout is always re-rooted at actual reality;
   prediction error is confined to one planning horizon (H events ⇒ ≤ H·ulp ≪
   TIE_EPS). **[DERIVED-here]** This re-anchoring is the structural reason MPC works
   here at all: the plant is deterministic and fully observable in retrospect.

5. Residual risk: a decision that flips because two candidates' rollout *values* differ
   by < ε — handled by the margin rail (§6.1), not by arithmetic.

### 1.6 Model-vs-judge drift monitor (dev build only)

After each issued task, when its TDN arrives, assert `|predicted − observed| < 1e-6`;
count violations on a `[METER]`-style stderr counter. Zero violations on the 68-case
suite + 500-crash sweep is the model's correctness gate before any decision uses it.

---

## 2. The unknown-arrivals problem (Task 2)

Unknowns: (i) future ARR times + Lins; (ii) un-revealed `Lout` of arrived requests.
Everything else is exact. Treat as determinization (Whitehouse et al.: sampling hidden
information into a fixed instance so a search for the full-information game can run).

### 2.1 Sample future family

- **F0 = ∅ (closed system).** No future arrivals. This is the *endgame truth* and the
  optimistic mid-test bound. Exact (given `lout_hat`).

- **F1 = replay.** Resample interarrival gaps and Lins i.i.d. from the observed
  empirical stream (fixed-seed PRNG, §6.3). Preserves burstiness (Kingman variability,
  SB01 §5.2) — Poisson undersamples bursts.

- **F2 = Poisson(λ̂)** with observed Lin distribution. Guards against replaying an
  unrepresentative prefix. λ̂ = EWMA of arrival rate.

**[DERIVED-here]** `Lout` determinization: use the existing `predict_lout` per-Lin
bucket means (D-151 scaffold, src/state.hpp:124) — one determinization for almost all
decisions; add a ±spread variant only for endgame critical-request identification.

### 2.2 Myopic-dominant (future-invariant) decisions

**[DERIVED-here]** Definition: candidates a, b are *future-invariant* at time t if
their rollout trajectories differ only at events before `τ_min = max(next sampled ARR,
t)`, i.e., their differing resource effects have fully played out before any plausible
future arrival can interact. Then every future in the sample family induces the same
pairwise value difference (arrivals only inject NEW requests; they interact with the
pair only through resources after τ_min, identically for both).

In this class — decide by F0 rollout alone, no future model needed:

- **P_POST issue** (advances TDR of a done request; nothing downstream of the choice
  remains after the next event) — the work-conserving class generally: "run best legal
  task on an otherwise-idle resource".

- **D_PROC issue when the cloud would otherwise idle** (Dai Thm 2 work-conserving,
  SB01 §5.1).

- **Chunk continuation** (the piece boundaries of an in-flight request are private).
- **D_POST issue** (token emission; span bookkeeping only).

### 2.3 Structurally fragile decisions

- **D PRE hold-or-issue / group size**: holding pays only through pool GROWTH; growth
  has a known component (pipeline completions: WAIT_INPUT_DOWN + P_PROC_RUNNING — the
  model computes these exactly) and an unknown component (new arrivals). The decision
  is fragile exactly insofar as the unknown component matters — i.e., when the known
  pipeline is empty and the pool is thin.

- **P PRE cloud choice**: permanent assignment; couples to every future round of the
  request AND to co-location with future requests. Most fragile early (arrival
  statistics thin) and at burst boundaries.

- **Admission pacing (UP in-flight cap)**: exists to protect decode from a prefill
  wave; the wave's tail is unknown.

- **Endgame mode switch**: the *belief* "stream is dry" — if wrong, park-then-burst
  (SB01 D8) wastes V_S window; if right, it is the single biggest E-channel lever.

### 2.4 The robust-decision test

**[DERIVED-here; structure from determinization theory]** With active future set
`S_f ⊆ {F0, F1, F2}` and candidate set A at a decision point:

```
V_maximin(a) = min_{f ∈ S_f} V(rollout(a, f))         // worst sampled future
a* = argmax_{a ∈ A} V_maximin(a)
COMMIT to a*  iff  (a* == heuristic_choice)           // free: agrees, no risk
                 or (V_maximin(a*) − V_maximin(heuristic_choice) > ε_v)   // margin rail
else            emit heuristic_choice                 // fallback = base policy
```

Properties: (i) agreement across futures is detected *empirically* — if the argmax is
the same under every sampled future, the decision is future-invariant even when §2.2's
syntactic test can't see it; (ii) maximin (not mean) avoids *strategy fusion* — the
classic determinization failure where the planner assumes, inside each sample, that it
will act with sample-specific knowledge it will not have (Frank & Basin's "strategy
fusion", verified in research/papers/whitehouse-determinization-cig11.txt lines
187-193). Maximin only commits when the choice is fusion-proof: it wins under the worst
sample, so conditioning on inside knowledge cannot be what makes it win. (iii) The
fallback preserves the Bertsekas sequential-improvement floor (§3.2). [UNVERIFIED-full-
text] The AFIT MCTS-scheduling thesis (Pipan 2021, search snippet only) states
determinization "works well if the hidden information does not influence the game until
it is revealed" — consistent with §2.2/§2.3's split; treat as anecdotal.

Mid-test defaults: `S_f = {F0, F1}` (replay is the most plausible generator; Poisson
added only for pacing decisions). Endgame (dry-stream belief): `S_f = {F0}` — the
system is closed and the model is exact.

---

## 3. Search design over the real decision space (Task 3)

### 3.1 What the literature supports, and what it rejects here

- **Bertsekas rollout** (one-step lookahead; each candidate evaluated by simulating the
  BASE policy to the end; commit argmax) — research/papers/bertsekas-rollouts-survey.
  Verified: Definition 1 (sequential consistency, txt lines 342-347); **Proposition 1**
  (lines 399-410): *with a sequentially consistent base heuristic, rollout terminates
  and its cost is ≤ the base heuristic's* (the "sequential improvement" property, eq
  2.5); greedy base heuristics are sequentially consistent (lines 348-379); cost =
  base-heuristic runtime × low-order polynomial in lookahead (lines 167-168). This is
  the ONLY method here with a no-loss guarantee relative to what we already ship.

- **Determinized UCT / ISMCTS** — rejected: per-decision budget is microseconds
  (§3.5); UCT's value comes from many playouts to amortize exploration variance; with
  ≤ ~10 playouts the UCB term is noise, and ISMCTS (Whitehouse et al.) needs ~20×
  determinizations. Use ONE-step lookahead with ≤ 3 futures instead.

- **Optimization-based MPC (MILP/CP per turn)** — rejected on scale: published MPC for
  flexible job-shop reports solve times of tens of ms on 3-machine/9-task instances
  ([UNVERIFIED-full-text] Wenzelburger et al. 2021, https://www.mdpi.com/2076-3417/11/17/8145
  — abstract verified via search, timings second-hand), vs our ~10-100 µs/decision and
  ~10^5 decision points. Simulation-based MPC (rollout) is the right class.

- **Beam search** — rejected: branching lives in group membership, and SB01 D4/D6 show
  value is ~a function of m along the pool frontier, not of member identity; a 4-wide
  beam re-derives what the candidate-set reduction gives for free.

### 3.2 The recommended scheme (concrete)

**Bertsekas rollout, one-step lookahead over a reduced candidate set, truncated
base-policy rollout, sequential greedy across resources, maximin over sampled
futures (§2.4).**

1. **Decision points** = frames where ≥ 1 computer is free ∧ ≥ 1 legal task exists.
   Frames with everything busy cost nothing (this is most XDN frames).

2. **Candidate sets** (small, parametric — no subset enumeration):
   - Local E: `{P_PRE head, P_POST head, D_PRE(m) for m ∈ M, D_POST(m̄), idle}` where
     `M = {1, m* (period-argmin, SB01 §1.4), pool, pool/2}` dedup'd → |A_E| ≤ 7.

   - Cloud c: `{P_PROC (SRPT pick, full piece), P_PROC (chunked variant, only if the
     D-150-falsification gate `S<1 && layers>8` passes), D_PROC(m) for m ∈ {1, m*,
     pool_c}, idle}` → |A_c| ≤ 4.

3. **Member selection inside a size-m candidate**: order the D PRE/D POST pool by SB01
   D7 priority (SRPT-blend, Lout=1 demoted), take PREFIXES of the ordering. This
   replaces exponential membership choice by O(pool) ordering + ≤ 4 sizes.
   **[DERIVED-here]** Justification: within one pool at one round, members are
   interchangeable up to their e_last positions, and the prefix is exactly the
   "end-compressed" shape SB01 §4.2 says is optimal for spans.

4. **Pruning**: (i) legality; (ii) dominance — drop candidate a if b ⊇ a's immediate
   effect and b's period argmin dominates (per SB01 §1.4); (iii) drop `idle` on clouds
   unless evaluating an explicit hold; on local, keep `idle` ONLY when the decision is
   a hold-class decision (§4a/§4c) — this preserves work-conservation elsewhere
   (Dai Thm 2, SB01 §5.1).

5. **Sequential greedy across resources within a frame** (the joint action is a
   product over ≤ K+1 resources — too big): decide local E first (it is the structural
   bottleneck, SB01 §2.4), commit to the model, then clouds in index order, each
   argmax re-evaluated against the updated model. **[DERIVED-here]** This is
   coordinate descent on the frame's action; ≤ 1+K passes, each ≤ 7-4 candidates.

6. **Rollout of a candidate** = apply candidate, then simulate the BASE policy (the
   existing heuristics, refactored to pure functions of model state) decision-point by
   decision-point, until the stopping rule, then terminal value.

7. **Horizon H**: stop at the max of the candidate horizon and the next decision points, capped H ≤ 48 events (L2) / ≤ 256 events (L3, endgame only). Typical H ≈ 16–32. Endgame rollout for the E-channel may run to completion of all in-flight tokens (bounded: m̄ rounds × ≤ 3 tasks/round + coalescing ≈ ≤ 256 events for pools ≤ 64).

```text
H_stop = max(tau_cand + 2*P_round, next decision after primary milestone + 1)
```

- `tau_cand` — candidate time; `P_round` — round period; `H` — event horizon.

8. **Terminal evaluator** (SB01 §4.1 coefficients, computed from rollout oracle deltas):

```text
V = V_E * dE + V_P * d(sum_ppost)/R + V_S * d(span)/G - sum_r lambda_r * idle_r
```

- `V_E`, `V_P`, `V_S` — exact marginals; `dE`, `d(sum_ppost)`, `d(span)` — rollout deltas.
- `lambda_r` — shadow price per resource; `idle_r` — idle time.
- The last term (SB01 §5.4 shadow prices, from the D-142 busy accumulators) is load-bearing: `V_S` rewards *parking* (late first tokens), so without an idle-regret price a short-horizon rollout will strategically idle to farm `V_S`. **[DERIVED-here]**

### 3.3 Why the base policy must be the CURRENT heuristic

Bertsekas Prop 1's no-loss floor holds only for (a) the exact model, (b) untruncated
rollout, (c) a sequentially consistent base. We have (a) ε-exactly, (b) not, so the
guarantee degrades to: *rollout ≥ base whenever the truncation error and ε margin are
below the commit margin* — which is exactly what §6's rails enforce. Using our tuned
heuristic stack as base keeps the D-147/D-148 CF-verified behavior as the floor every
decision falls back to.

### 3.4 Volume estimate (R = 2000, ΣLout = 2·10^5, batching m̄ ≈ 16, K = 8)

**[DERIVED-here]** Prefill: 2,000 × 3 tasks + 2 transfers = 10,000 events. Decode:
≈ 2·10^5/16 = 12,500 rounds × (1 D PRE + ~2 UP-XDN + ~2 D PROC + 1 DOWN-XDN + 1 D POST)
≈ 87,500 events. Total ≈ 10^5 events → **frames ≈ 6–9·10^4** (coalescing merges D POST
TDN+FIN and simultaneous TDN+XDN pairs; statement cap 2·10^6 corresponds to the
m̄≈1/no-coalescing degenerate — D-161 measured a local max of 1.01 M). **Decision
points ≈ 4–7·10^4** (frames where a computer is free). Per-decision budget: 15 s /
5·10^4 ≈ **300 µs typical; 7.5 µs/frame worst-case** (2·10^6 frames). These two numbers
bracket the ladder (§5).

### 3.5 Cost per candidate (measured anchors)

Anchor: D-161 measured the (now guarded) SPT scan at ~10.5 µs/frame in the current
solver **[FACT-repo]** — i.e., the existing per-frame budget is already of order 1-10
µs. Model event cost ≈ O(K) min-scan + table lookup + journal writes ≈ 60–250 ns;
with a tiny (step,m) memo for the ≤ 8 m-values used per test, lookup ~20 ns → event
≈ 0.1 µs. Candidate (H=32) ≈ 3.5 µs + terminal ≈ 0.2 µs. Decision point (8 candidates,
2 futures) ≈ **60 µs**; L1-only decision ≈ 2-3 µs. L2 on 15% of decision points over a
big test ≈ 5·10^4 × 0.15 × 60 µs ≈ **0.45 s** — well inside budget; the governor (§5)
handles the 2·10^6-frame tail.

---

## 4. Where rollout provably beats heuristics here (Task 4)

Per recurring hard decision: the rollout spec (state / candidates / terminal / H /
complexity) and the hand-tuned code it would replace (file:line of the D-147/D-148
leader). "Provably" = exact-model decision (no future needed) or replaces a
first-order approximation of a queue-dependent quantity with its exact computation.

### 4a. D PRE hold-or-issue + group size under live contention — HIGHEST LEVERAGE

- Replaces: `SloAwareDelay::hold_dpre` (src/policies.cpp:314-391) — whose
  `true_period(m)` is the **uncongested** parallel-period formula `max(t_local,
  t_cloud, t_transfer)` — plus `MarginalBatch::group_size` (src/policies.cpp:220-308,
  the D-119 F1/Twist-1 crossing) and the `DPRE_ACCUMULATE` skip block
  (src/scheduler.cpp:169-196).

- The gap: under live contention the real period is queue-dependent (FIFO tails,
  cloud backlog), which no closed form in the solver models; the rollout computes the
  congested truth directly. **[DERIVED-here]** This is the largest class of
  hand-tuned-vs-exact deltas in the codebase, and D-144's saturation finding (E/mk ∈
  [0.975,1.000], no idle channel) says these decisions are made AT saturation —
  exactly where first-order formulas are worst.

- Spec: candidates `{D PRE(m): m ∈ {1, m*, pool, pool/2}} ∪ {hold}`; futures {F0, F1};
  H ≈ 32 (two round periods); terminal per §3.2(8) with V_S on members' e_last and
  V_E on the projected-last member; complexity ≈ 5 cand × 2 futures × 3.5 µs ≈ 35 µs,
  fires on ~10-15% of frames (pool ≥ 2 or near-SLO2 period).

### 4b. P PRE cloud choice under decode-table shape — CHEAPEST BIG WIN

- Replaces: `PrefillLoadBalanceCloud::choose` (src/policies.cpp:12-115) — the mega
  herd gate + Tier-1 straggler isolation + EFT with ratio-argmin + PoTC band: four
  stacked approximations of "which co-location minimizes this request's TDR + future
  round cost".

- The gap: EFT is a static backlog estimate; the rollout measures the request's actual
  P POST time and first-round participation through the simulated FIFOs and cloud
  queues, including cross-effects on co-located requests' TDR (the pooled-mean V_P
  gradient, SB01 §4.1 consequence 1 — every co-delayed request costs the same).

- Spec: candidates = {EFT-argmin, least-active, herd(most-active), 2nd-best-EFT} (≤ 4);
  H ≈ 64 (to the request's first token); fires only at P PRE issue ≈ R = 2,000 times →
  2,000 × 4 × 7 µs ≈ **56 ms/test**. Endgame/critical cases may use L3. Note the
  x9 mega-regime defeat (D-161 A1: EFT-spreading lost 388.6 vs herd 736.8) is exactly
  the class a rollout decides by computation instead of by gate.

### 4c. Accumulate-vs-drain at round boundaries

- Replaces: the `dpre_acc_skip` logic (src/scheduler.cpp:179-196) — waits when
  `dpre_n < target` and the known pipeline can fill the batch.

- The gap: the rule is binary (wait iff pipeline ≥ target−pool); the real question is
  whether the WAIT's V-cost is below the larger group's V-gain, which depends on where
  the pipeline completions land in time — the rollout computes this exactly (the known
  pipeline is deterministic: WAIT_INPUT_DOWN + P_PROC_RUNNING are model events, not
  future samples).

- Spec: candidates {drain now (m = pool), drain (m = pool + pipeline inflow), hold};
  futures {F0, F1} (only the arrival part of inflow is sampled); H ≈ 48; ≈ 25 µs;
  fires at round boundaries when `0 < pipeline < target`.

### 4d. Endgame ordering — PROVABLY exact (closed system)

- Replaces: nothing directly (the solver has no explicit endgame module; SB01 D8 is
  the design). With the dry-stream belief (ARR gap > 3·EWMA), **F0 is the truth**:
  no unknown inputs remain except `lout_hat`, so the rollout is an exact
  what-if engine for the entire remaining test.

- Spec: at each endgame decision point, candidates per §3.2 but with L3 depth
  (H ≤ 256, to token-completion), `S_f = {F0}`; terminal = exact projected score
  (Oracle::projected_score) rather than gradients. This decides critical-path
  ordering, maximal final groups, and first-token parking by direct measurement of
  the score. Targets the E channel — where D-143/D-144 place the structural
  ~1,100 pts on CF #5/#6 and where V_E ∝ 1/E² peaks (SB01 §4.1(3)).

- Complexity: endgame decision points ≈ in-flight rounds × ~3 ≈ 5·10^3; × 4 cand ×
  ~25 µs ≈ **0.5 s** — governor-gated (§5).

### 4e. Chunking decisions

- Replaces: `AdaptiveChunk::next_piece` (src/policies.cpp:150-184, k ≤ 2 heuristic).
- Spec: candidates {full, c, 2c} pieces; H = the decode-stall window on that cloud
  (≈ 2·P_round); fires only when decode work waits on that cloud (rare; the D-150
  falsification gate already confines chunking to `S<1 && layers>8`). Complexity
  negligible (~3 × 5 µs × ~10^2 fires/test).

- The rollout prices the Sarathi trade (stall saved vs (c−1)·S + serialization)
  exactly under the current queue state, replacing the `local_avg` proxy.

---

## 5. Budget ladder + CPU governor (Task 5)

The artifact must NEVER TLE. Measured anchor: the current solver's unbounded SPT scan
was ~10.5 µs/frame ⇒ ~21 s on a 2·10^6-frame test (D-161 #0, [FACT-repo]) — the exact
failure mode the governor must prevent.

### 5.1 The ladder

| Level | What | Cost/decision | Trigger |
|---|---|---|---|
| **L0** | Base heuristic only (current stack) | ~0.5-2 µs | Fallback; all frames when governor says so |
| **L1** | One-step closed-form candidate compare: for each candidate compute immediate milestone times analytically (free-time updates + FIFO tail arithmetic are O(1) closed forms), value by SB01 gradients — no event simulation | ~1-3 µs | DEFAULT on every decision point |
| **L2** | Short rollout: H ≤ 48, futures {F0} or {F0,F1}, base-policy rollout | ~35-70 µs | Stake classes: (i) D PRE hold/size with pool ≥ 2 or period within 10% of SLO2; (ii) P PRE cloud when cloud EFTs within 10% or burst start; (iii) accumulate boundary; (iv) chunk gate open |
| **L3** | Deep rollout: H ≤ 256, futures {F0}, exact terminal score | ~0.1-1 ms | Endgame belief set AND budget allows; plus ≤ 1% mid-test sampling of endgame-identifying decisions |

### 5.2 The governor (hard, arithmetic)

- Budget: `B_total = 12.0 s` CPU of the 15 s limit (3 s reserve for judge-side I/O
  jitter and machine variance; CF machines are slower than dev boxes — the 21 s vs 15 s
  lesson above).

- Clock: `steady_clock` sampled every 1024 frames (amortized ~0 ns/frame).
- Remaining-work estimate: no end-of-test signal exists, so use the conservative
  growth rule `F̂_rem = max(2·n_so_far, 2·10^5)` (assume we are never past the
  midpoint until frames exceed 10^6, then linearly decay). **[DERIVED-here]**

- Admission test each sample: let `B_left = 12.0 − T_e`, `r_need = c(L)/F̂_rem` per
  active stake class (c(L) = per-decision cost of level L, calibrated in-dev by
  chrono over the 68-case suite and re-anchored by the first 10^4 frames of the live
  test). Keep the highest level whose `stake_rate · c(L) < 0.8 · B_left/F̂_rem`.

- Hard cuts: `T_e > 10 s` → L2/L3 off; `T_e > 12 s` → L1 off (L0 only); any single
  decision point exceeding 400 µs wall (checked around the candidate loop) → finish
  remaining candidates at L1.

- Quantization/hysteresis: level decisions keyed to (frame-count bucket, burn-rate
  bucket), with sticky downgrade only — so small timing jitter cannot flip behavior
  frame-to-frame (determinism rail §6.3).

---

## 6. Safety rails / falsification analysis (Task 6)

### 6.1 Ways rollout can be WORSE, and the rail for each

1. **Tie-order divergence vs judge** (model predicts events at τ, τ+ulp in different
   frames; judge coalesces or splits differently): rail = TIE_EPS simultaneity in the
   model (§1.5) + re-anchoring each frame (error confined to one horizon) + commit
   margin ε_v. Worst case is one sub-optimal decision, self-corrected at the next
   frame — never a protocol error (legality is checked against live state, never the
   model).

2. **Overfit to observed arrivals** (replay future encodes the prefix's accidents;
   mid-test pacing optimizes for a burst that is over): rail = maximin over {F0,F1}

   + heuristic fallback + the SB01 aging floor (never strand mandatory work; D7).
3. **ε drift compounding over 2·10^6 frames**: impossible by construction — live state
   is rebuilt from observed events each frame; drift exists only inside one rollout
   (§1.5(4)). The only compounding channel is DECISION feedback (our choices change
   the future), which is real optimization, not error.

4. **Truncation bias** (short H makes parking look free / defers costs past H):
   rail = the idle-regret terminal term (§3.2(8)) + H ≥ 2 round periods + L3 for
   endgame where truncation would be most biased.

5. **Strategy fusion** (planning with sampled knowledge the live policy won't have):
   rail = maximin + fallback (§2.4); never commit on a future-sample-only win.

6. **Lout estimator poisoning** (predict_lout cold buckets → wrong FIN times → wrong
   round counts): rail = rollout decisions only when buckets have ≥ n0 samples, else
   L1/L0; endgame uses observed iters where Lout is revealed at FIN.

7. **The D-150 class of failure** (a "principled" rewrite losing 1,194 local points):
   rail = one decision class per CF submission (D-152's lesson: composite submissions
   are unattributable); the engine replaces a heuristic only when that heuristic's
   block is deleted to fund the artifact bytes (§6.4).

### 6.2 Commit-margin rail (formal)

Override the heuristic choice only when:

```text
V_maximin(a*) > V_maximin(a_H) + eps_v
```

```text
eps_v = max(0.05 pts, 0.02*|V_maximin(a_H)|)
```

- `a*` — rollout argmax; `a_H` — heuristic choice; `eps_v` — 2% of the stakes.
- Below-margin disagreements defer to the CF-verified base. This also subsumes the identical-response check: when the rollout's preferred candidate IS the heuristic's choice, emit it directly (zero-risk path; the common case in steady state).

### 6.3 Determinism rail

The judge's determinism license is conditional on OUR responses being a function of
the observed stream alone. Rails: fixed-seed xorshift64 (seed = hash of config+table,
constant across runs); F1/F2 sampling draws from indexed (not wall-clock) state; no
`unordered_*` iteration anywhere in decisions (arrays/vectors in fixed order);
governor affects only the LEVEL (quantized, sticky-downgrade), never the choice given
a level. Dev-build drift monitor (§1.6) doubles as the model-correctness assert.

### 6.4 Artifact-size rail (binding constraint)

`src/submission.cpp` is currently **65,467 / 65,535 chars — 68 free**
([FACT-repo], 2026-08-23). The engine (model + journal + rollout loop + governor)
minifies to ≈ 2.5–4 KB. Funding plan recorded pre-freeze, per decision class migrated (§4 order):
delete the replaced block as its decision passes CF validation — `hold_dpre` ≈ 2.3 KB,
`choose` EFT block ≈ 1.5 KB, `group_size` twist block ≈ 1.5 KB of source (pre-minify).
No block is deleted before its replacement's CF verdict is green (repo rule: CF is the
arbiter). Dev tree keeps full stacks behind `#ifndef PSGO_STATIC`-style strips
(precedent D-151: `predict_request` dev-only).

### 6.5 Validation protocol (per migrated decision class)

1. Green-pass battery on the artifact (zoo 22/22, oracle 68/68, crash 500/500,
   hostile, extremes unchanged, A/B explainable) + model drift monitor at zero.

2. Local A/B is directional only (D-117/D-152/D-153: local certifiability is
   exhausted); single-mechanism CF probe, retire-on-loss.

3. Rollback = prior leader artifact via git.

---

## 7. References

Downloaded & verified (research/papers/):

- `bertsekas-rollouts-survey.{pdf,txt}` — Bertsekas, *Rollout Algorithms for Discrete
  Optimization: A Survey* (2010). Verified: Definition 1 (sequential consistency,
  txt 342-347), Proposition 1 / eq (2.5) (rollout cost ≤ base cost, 399-410), greedy
  bases sequentially consistent (348-379), cost = base × low-order polynomial
  (167-168). Source: https://web.mit.edu/dimitrib/www/Rollouts_Survey.pdf

- `whitehouse-determinization-cig11.{pdf,txt}` — Whitehouse, Powley, Cowling,
  *Determinization and Information Set MCTS for Dou Di Zhu* (CIG 2011). Verified:
  strategy fusion / non-locality (Frank & Basin) quoted at txt 186-193; ISMCTS as the
  heavy alternative (752-801). Source:
  http://orangehelicopter.com/academic/papers/cig11.pdf

Primary URLs (not downloaded; abstract-verified via search only, contents
[UNVERIFIED-full-text]):

- Wenzelburger et al., *MPC for Flexible Job Shop Scheduling*, Applied Sciences
  11(17):8145, 2021 — https://www.mdpi.com/2076-3417/11/17/8145 (optimization-based
  MPC precedent; used only to reject MILP-per-turn on cost grounds).

- Pipan, *Application of MCTS to Multi-Action Scheduling*, AFIT thesis 2021 —
  https://scholar.afit.edu (determinization-until-revealed anecdote).

- Bertsekas & Castanon 1999, *Rollout Algorithms for Stochastic Scheduling Problems*,
  J. Heuristics 5:89-108 — the stochastic-scheduling rollout origin (cited inside the
  verified survey).

Internal (this repo, verified): `tools/interactor.py` (`Sim::apply_one`,
`enqueue_up/down`, `next_frame_time`, `drain_transfers` — the exactness blueprint);
`src/task_table.cpp:62-63` vs `interactor.py:80` (the ulp-level interp-order
divergence — §1.5's exhibit); `src/scheduler.cpp` (pick_local/pick_cloud decision
inventory); `src/policies.cpp` (hold_dpre/group_size/choose/chunk specs);
`src/state.hpp` (Oracle, predict_lout, busy accumulators); `docs/DECISIONS.md`
D-117/D-147/D-150/D-151/D-152/D-153/D-161 (CF-arbiter rule, TLE anchor, size
history); `research/techniques/SB01_online_scheduling_theory.md` (V_E/V_P/V_S
gradients D1-D8, terminal evaluator, three-regime batch law, endgame D8).

---

## 8. One-paragraph recommendation recorded pre-freeze (archival)

Pre-freeze recommendation (archival): build the undo-journal forward model (O(K) event scan, TIE_EPS=1e-9 coalescing,
re-anchored every frame), and replace heuristics one decision class at a time with
Bertsekas rollouts over ≤ 7 parametric candidates, using the then-current heuristic stack
as the base policy and the SB01 gradient terminal (plus an idle-regret price),
governed by a 12 s quantized budget ladder (L1 default, L2 on stake classes, L3
endgame-only). Commit only on maximin-over-futures wins with a 2% margin; fall back
to the base choice otherwise. First three migrations, in planned order: **(1) endgame F0
rollout (4d — provably exact, targets the E channel where D-144's ~1,100 structural
points live); (2) D PRE hold-or-issue/size under contention (4a — the largest
hand-tuned-vs-exact gap, `true_period` is uncongested); (3) P PRE cloud choice (4b —
cheapest at ~56 ms/test, removes the four-gate stack that lost x9).**
