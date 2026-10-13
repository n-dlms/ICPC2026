# SB01 — Online Scheduling Theory for the Edge–Cloud Objective

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB01). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
Scope (archival): decision rules with mathematical grounding for
CF contest 2251 problem A, scoped for implementation inside `src/submission.cpp` (C++17, ≤65,535 chars,
single-threaded, ≤2·10^6 frames).

Claim tags:

- **[PROVEN-theory]** — result stated and verified in a downloaded paper (path given) or a
  primary-source URL.

- **[DERIVED-here]** — my derivation from verified facts only. Verified facts = the scoring
  function (SA01/SYNTHESIS, `problem.md`, cross-checked `docs/ASSUMPTIONS.md` §8) and the
  protocol/transfer-volume facts (SA09 §1.1: one UP+DOWN per prefill; per-round UP per
  distinct remote, DOWN per per-cloud sub-batch; each member costs exactly bpt UP + bpt
  DOWN per round regardless of group size).

- **[HEURISTIC-unbounded]** — plausible rule, no guarantee.

Alignment: SA09's E-floor bounds (link/cloud/local/dependency) are the *offline* ceilings;
this doc supplies the *online decision rules* that chase those ceilings. SA06 §1.1 derived
a delay-profitability inequality; §4 below completes it into a full gradient calculus
including the tpot-span channel SA06 left implicit.

---

## 1. Mean-flow-time + rate objectives (Task 1)

### 1.1 What our objective IS, structurally

Score = 1000·(w_tp·TP + w_c·WT) mixes:

- a **rate objective**: tp = Ntok/E (Ntok = ΣLout, E = last final token − earliest ARR);
- a **mean-flow objective** on a midpoint milestone: tdr = mean(P POST done − ARR);
- a **mean-compression objective** on token bursts: tpot = Σ(e_last−e_first)/G,
  G = Ntok − R.

Key structural fact (from the verified formula, SA04/SYNTHESIS): **tdr ends at P POST,
tpot starts at the first token. The two waiting metrics are sequentially decoupled per
request.** Prefill-stage decisions (P PRE order, cloud, chunking) feed tdr + resource
contention; decode-stage decisions feed tpot + E. They couple only through shared
resources (local machine, link, clouds) and the global clock E.

Second structural fact: **tpot is a span-compression objective, not a promptness
objective** (§4.2): an *earlier first token with unchanged last token hurts the score*.
There is no reward anywhere for early interior tokens.

Third: **all requests must finish** (unfinished ⇒ 0). So this is NOT an
admission/rejection problem (Moore–Hodgson rejection theory does not transfer); it is a
pure ORDERING problem for mandatory work. The classical objective it most resembles is a
weighted mix of `1||ΣC_j`-type means plus a makespan term with coupled machines.

### 1.2 SRPT and its non-clairvoyant variants

- **SRPT optimality** [PROVEN-theory]: SRPT minimizes total/mean flow time on a single
  preemptible machine, even with release dates (`1|r_j,pmtn|ΣC_j`). Schrage 1968,
  "A Proof of the Optimality of the Shortest Remaining Processing Time Discipline",
  Oper. Res. 16(3):687–690, https://pubsonline.informs.org/doi/10.1287/opre.16.3.687.
  Online-optimal (no future knowledge needed) — confirmed in Megow–Schulz 2004,
  https://web.mit.edu/schulz/www/epapers/ms-orl-2004.pdf.

- **Non-clairvoyant flow time** [PROVEN-theory]: with processing times unknown, SETF /
  MLF is the optimal deterministic policy at Θ(log n)-competitiveness for total flow time
  and is *scalable*: (1+ε)-speed ⇒ O(1/ε)-competitive (Kalyanasundaram–Pruhs, *Speed Is
  as Powerful as Clairvoyance*, J.ACM 47(4), 2000; summary verified via
  research/notes/306-kp-speed-as-clairvoyance.md — primary text paywalled, constants
  second-hand). See also
  https://www.researchgate.net/publication/221590801_Non-clairvoyant_scheduling_to_minimize_the_average_flow_time_on_single_and_parallel_machines.

- **Mapping to us**: the PREFILL stage is *clairvoyant* (Lin is revealed at ARR; prefill
  durations come from the table). The DECODE stage is *non-clairvoyant* (Lout hidden until
  FIN; we know only tokens-produced-so-far). So:

  - P PRE ordering should follow SRPT on *prefill remaining work* pproc(Lin_i) (+local
    share) — clairvoyant SRPT, no excuse. [PROVEN-analogy: SRPT is exactly optimal on a
    clean single machine; here heuristic because stages are coupled.]

  - Decode priority must be SRPT-by-estimate with a non-clairvoyant fallback (§3).

### 1.3 Batch setups / serial-batch machines — what is proven

Our D PRE/D PROC/D POST group is a **serial batch** (group completes together; setup S per
task; capacity unbounded) — the `1|s-batch` model, with a *table-driven concave/convex
batch duration* instead of Σp_j.

Verified results:

- **Threshold/control-limit optimality for batch-service queues** [PROVEN-theory]:
  Deb & Serfozo 1973, "Optimal Control of Batch Service Queues": for Poisson arrivals,
  service in batches, linear holding costs, the delay-minimizing policy is a **queue-length
  threshold policy** — serve when queue ≥ q*, where q* is computable.
  https://www.researchgate.net/publication/305982505_Optimal_control_of_batch_service_queues;
  survey anchor https://en.wikipedia.org/wiki/Bulk_queue. Structure persists under finite
  capacity and general arrivals (Eurandom report
  https://www.eurandom.tue.nl/reports/1999/021-report.pdf).

- **Deterministic flow-time batching** [PROVEN-theory, existence/structure]: Santos &
  Magazine 1985 (Oper. Res. Lett. 4:99-103) and Dobson, Karmarkar & Rummel 1987
  ("Batching to Minimize Flow Times on One Machine", Mgmt Sci 33:784-799) characterize
  optimal batchings on one machine with setups; Coffman–Yannakakis–Magazine–Santos 1990
  give batch-sizing + sequencing theorems (citations verified in
  research/papers/BakerTrietsch2009-notes-ch13.txt, lines 29-48, 139-146, 161-175).

- **Complexity wall** [PROVEN-theory]: 1-machine batching with setups for ΣwC is
  polynomially solvable in special cases and NP-hard generally (Albers & Brucker 1993;
  Monma & Potts 1989 "On the Complexity of Scheduling with Batch Setup Times", Oper. Res.
  37:798-804 — both cited with titles in BakerTrietsch notes, lines 12-18, 161).
  Weighted-tardy on a batch machine 1|B,r_j|Σw_jU_j is NP-hard (Knapsack-hard) and W[1]-hard
  in natural parameters (research/papers/1911.12350.txt, abstract). Makespan with class
  setups on parallel machines: linear-time 2-approx and near-linear (3/2+ε)-approx exist
  (research/papers/1810.01223.txt, Thms 1-2). Serial-batch Cmax+fmax Pareto: O(n³) offline
  without releases (research/papers/2503.23273.txt, abstract). With release dates even
  Lmax variants are NP-hard (same paper, intro).

- **Practical corollary**: no polynomial *optimal* rule exists for our coupled online
  problem; the implementable optimum is the threshold family + per-event marginal calculus.

### 1.4 DERIVED: the exact optimal batch size for a fluid/Poisson-like stream

Model: decode-ready tokens arrive (become ready) at rate `lambda` (tokens/time); a held batch of `q` completes together after fixed overhead `f(q)`.

Overhead law:

```text
f(q) = 3*S + dpre(q) + dproc(q) + dpost(q) + 2*k*l + 2*beta*q
```

- `beta = 8*bpt/(bw*1e6)` — per-token cost per direction.
- `l` — latency; `k` — clouds touched.

Average residual fill wait of a token ≈ `q/(2*lambda)`. Mean extra token delay:

```text
W(q) = q/(2*lambda) + f(q)/q
```

- `W(q)` — per-token delay in the thin-pool regime.

First-order optimum:

```text
dW/dq = 1/(2*lambda) - f(q)/q^2 + f'(q)/q = 0
```

- **Flat/linear-overhead tables** (`f(q) ≈ f0 + a*q`): the `a`-terms cancel.

```text
q* = sqrt(2*lambda*f0)
```

  - `q*` — EOQ square-root law. [DERIVED-here]
- **Concave amortizing tables**:

```text
1/(2*lambda) + f'(q)/q = f(q)/q^2
```

  - Solve numerically (3–4 Newton steps at runtime); `q* > sqrt(2*lambda*f0)`. [DERIVED-here]
- **Saturated regime** (pool deep, resources busy): the fill-wait term is wrong — use the bottleneck-balance batch (§5.3).

  Raise `m` until the variable-dominated resource binds; with linear variable cost per token (link `2*beta`), `m*` is finite only through tpot round latency (below) or table convexity — otherwise unbounded caps are optimal (matches the solver's exhaust→unbounded rule, D-051). [DERIVED-here]

- **tpot cap on `m`**: a request participating in every round has consecutive-token gap = round period `P(m) = f(m)/1` (one round) ⇒ its `tpot ≈ P(m)`.

```text
m_tp = max{m : P(m) <= SLO2}
```

```text
P(m) = 3*S + dpre + dproc + dpost + 2*k*l + 2*beta*m (uncongested)
```

  - `m_tp` — clean feasibility rule.
  - This is the theory behind the D-119 F1 parallel-period formula: `max({2S+D_PRE+D_POST, S+D_PROC(m/K), transfer(m)})` — same object computed with K-way cloud parallelism. [DERIVED-here]
- **Deb–Serfozo threshold**: serve immediately if the server would otherwise idle (work-conserving override); hold only when `pool < q*` AND the server has other work. [PROVEN-structure + DERIVED-parameters]

### 1.5 Smallest feasible batch vs amortized batch — the reconciliation

For mean flow time alone, smaller batches always win (`W(q)` increasing in `q` through the fill term) **until stability forces batching**: per-token capacity cost `f0/q + a` must stay below `1/lambda`.

Three regimes:

```text
q* = max(1, sqrt(2*lambda*f0)) in the thin regime
```

```text
q* = lambda*f0/(1 - lambda*a) at critical load
```

```text
unbounded-but-tpot-capped in saturation
```

- `q*` — optimal batch; `f0` — fixed cost; `a` — variable slope; `lambda` — arrival rate.
- The three regimes and their switches are exactly the classic "smallest feasible batch" doctrine restated with our table. [DERIVED-here; threshold structure PROVEN by Deb–Serfozo]

---

## 2. Makespan/rate on parallel machines with a shared FIFO transporter (Task 2)

### 2.1 Complexity: the joint problem is strongly NP-hard [PROVEN-theory]

Two-machine flowshop with transportation constraints (makespan) is strongly NP-hard
(Soukhal–Oulamara–Martineau, EJOR 161:32-41, 2005,
https://ideas.repec.org/a/eee/ejores/v161y2005i1p32-41.html). Parallel machines with a
single server (P,S1, our "one local + K remotes + one link" shape): general case strongly
NP-hard; heuristics of record are list-based (Abdekhodaee & Wirth forward heuristic),
  https://www.sciencedirect.com/science/article/abs/pii/S0305054804002035; thesis with
  online LPT and FH: https://uphf.hal.science/tel-03429508v1/document; local copy:
  research/papers/elidrissi2020-single-server-thesis.pdf. Consequence: do NOT look for an
  exact joint rule; decompose per decision (the decision table, §6).

### 2.2 Lot streaming: the geometric sublot theorem [PROVEN-theory, formulas verified]

From research/papers/botsali2019-lot-streaming.txt (§3, eqs (3)-(5)), restating
Potts & Baker 1989 and Trietsch & Baker 1993 (Oper. Res. 41:1065-1076,
https://pubsonline.informs.org/doi/10.1287/opre.41.6.1065):

- Two-machine lot streaming, consistent sublots, no idling, continuous sizes
  (2/C/NI/CV): **optimal sublot sizes are geometric**: L_j = q·L_{j−1} with
  **q = p2/p1** (ratio of downstream to upstream stage time), L1 = U(1−q)/(1−q^n),
  equal sublots iff q = 1. The ratio is exactly the *no-idle condition on machine 2*.

- Three machines: if (p2)² − p1·p3 ≤ 0 the optimal is a single critical path with
  L_{j+1}(p1+p2) = L_j(p2+p3) (Glass–Gupta–Potts 1994); else split into two 2-machine
  problems. m machines, variable sublots, no idling: solve adjacent pairs; consistent
  sublots: Baker's LP. (Same survey, §4-5.)

- With a transporter of cycle time CT between the machines, the geometric structure
  survives with a modified ratio (survey §3, formula truncated in the PDF text; exact CT
  form [UNVERIFIED-exact-form], derive per instance with the no-idle condition below).

### 2.3 Where lot streaming does and does NOT apply to our protocol

- **Prefill chunking is NOT lot streaming.** Protocol fact: exactly one UP + one DOWN per
  request; chunks have no transfers; only the last chunk queues DOWN
  (docs/ASSUMPTIONS.md §5). Splitting the prefill does not subdivide any transfer lot.
  Its true role is **preemption granularity** (Sarathi-Serve chunked prefill,
  research/papers/2403.02310.txt): chunking bounds how long a big prefill can monopolize
  a cloud, protecting interleaved decode rounds. [PROVEN-analogy in LLM systems]

- **The geometric ratio DOES apply to decode round sizing across a pipelined cycle.**
  Treat consecutive decode groups as "sublots" flowing cloud→DOWN→local (a 2-stage
  pipeline with stage times c_cloud = S+dproc(m) per round and c_link = ℓ+βm per
  transfer). The no-idle condition L_j·p_upstream = L_{j−1}·p_downstream translates to:
  **grow the next group when the link stage is slower than the cloud stage, shrink when
  faster; equal groups iff stages are balanced.** [DERIVED-here from the verified
  Potts–Baker condition]

### 2.4 DERIVED: cyclic decode-round period and stage balance

Steady state, all-`A` in-flight requests batched each round, `k` distinct clouds:

```text
P_round = max(local, cloud, link, chain)
```

```text
local = 2*S + dpre(m) + dpost(m)
```

```text
cloud = S + dproc(m/k) per cloud (k-parallel)
```

```text
link per direction = k*l + beta*A
```

```text
chain latency = 3*S + dpre + dproc + dpost + 2*l + 2*beta*m
```

- `P_round` — round period; `m` — batch size; `A` — in-flight count.

Link-direction saturation:

```text
A* = (P_round - k*l) / beta
```

- `A*` — saturation window from `k*l + beta*A <= P_round`.

```text
E >= Ntok * max(c_local, c_cloud, c_link)
```

```text
c_link = 2*k*l/m + 2*beta, c_cloud = k*(S + dproc(m/k))/m, c_local = (2*S + dpre(m) + dpost(m))/m
```

- k↑: c_cloud ↓ (parallelism), c_link ↑ (headers). **Optimal cloud-spread k\*** = the
  crossing point; compute by scanning k = 1..K at runtime (5 lines). [DERIVED-here;
  compatible with SA09's partition cloud bound]

- m↑: all fixed terms amortize; link variable 2β and any convex table terms remain.
  **Unbounded m is throughput-optimal iff no variable term dominates** — the formal
  statement behind exhaust→unbounded caps (D-051) and the "wave size" family. Cap m in
  practice only by m_tp (tpot) and ready-pool depth. [DERIVED-here]

---

## 3. Non-clairvoyant + hidden remaining work (Task 3)

Lout hidden until FIN; Lin revealed at ARR. Solver state already carries an EWMA +
per-Lin-bucket Lout estimator (see README.md). Best policy family, with guarantees:

1. **Estimate-driven SRPT** (consistency side): priority ∝ 1/ρ̂_i where
   ρ̂_i = max(1, E[Lout|bucket(Lin_i)] − tokens_i). Optimal if estimates exact (SRPT
   clairvoyant optimality, §1.2). [PROVEN-if-consistent]

2. **SETF fallback** (robustness side): priority ∝ tokens_i ascending (least elapsed
   first). Θ(log n)-competitive, optimal deterministic non-clairvoyant for flow time.
   [PROVEN-theory]

3. **Blend with confidence** (learning-augmented): use ρ̂ when the bucket has ≥ n0
   samples and low dispersion, else SETF; or score = 1/(α·ρ̂_i + (1−α)·tokens_i).
   Guarantee shape: consistency 1 + robustness bounded (LAP framework,
   research/papers/2305.07164.txt Thm 3.7: ratio η if η≤ρ else ρ+γ_on+1; 1-consistency
   forces ≥2-robustness, Prop 4.1). Survey: Mitzenmacher–Vassilvitskii, *Algorithms with
   Predictions*, arXiv:2006.09123, https://arxiv.org/abs/2006.09123. [PROVEN-framework]

4. **Hard anti-starvation floor**: mandatory completion (unfinished ⇒ 0) means aging is a
   constraint, not a preference: never let proj_finish_i drift beyond the point where
   finishing all mandatory work within any plausible horizon is endangered (SA09's
   dependency bound gives the horizon: E ≥ max_i remaining-chain). [DERIVED-here]

5. **Age escalation** on the E-channel: the projected-last request i\* earns the V_E term
   (§4.2); re-project F_i = a_i + solo-remaining + queue estimates each frame; aging
   should push i\*'s tasks up the WSPT ladder, not the oldest request's. [DERIVED-here]

Practical note: for Lout=1 requests, span = 0 ⇒ they contribute NOTHING to tpot
(denominator G excludes them); they matter only through tdr (already done at P POST) and
E (their single token). Never prioritize an Lout=1-looking request for tpot's sake;
its one token only matters if it is the global last. [DERIVED-here from the formula]

---

## 4. DERIVED: marginal-value calculus for the exact score (Task 4)

### 4.1 Gradients (closed forms)

Within the linear (unclamped) region of every channel, define:

```text
V_E = 1000 * w_tp * Ntok / ((tp_UB - tp_base) * E^2)
```

- `V_E` — value per unit `E` reduction.

```text
V_P = 1000 * w_c * s1 / (SLO1 * R * dist_base)
```

- `V_P` — per unit reduction of ONE `ppost_i`.

```text
V_S = 1000 * w_c * s2 / (SLO2 * G * dist_base)
```

- `V_S` — per unit span compression of ONE request.

```text
s1 = e1 / dist, s2 = e2 / dist
```

- `s1` and `s2` — direction cosines of the dist vector; `0` when clamped at `0`.

Channel-by-channel marginal effects (all [DERIVED-here] by differentiation of the verified formula):

| Event (advance by Δ) | Score change |
|---|---|
| Reduce global last-token time T_last by Δ | +V_E·Δ (tp channel only, linear region) |
| Reduce request i's P POST time by Δ | +V_P·Δ — **identical for every i** (pooled mean) |
| Advance request i's FIRST token by Δ | **−V_S·Δ** (longer span ⇒ worse tpot) |
| Advance request i's LAST token by Δ (i not global-last) | +V_S·Δ |
| Advance the global-last request's last token by Δ | +(V_E+V_S)·Δ |
| Move any interior token | 0 direct (resource release only) |

Consequences:

1. **The tdr gradient is global, not per-request.** Because tdr is a pooled mean, one
   minute of P POST delay costs the same whether the request is hugely or barely late.
   "Prioritize the most-overdue request" has NO score grounding through tdr — only
   through resource coupling and the E channel.

2. **First-token parking is free-to-good on tpot**: delaying e_first by Δ (without moving
   e_last) GAINS V_S·Δ. Confirms and generalizes SA06's "dead zone" finding (parking pays
   only the E tax). Optimal per-request decode is *end-compressed*, bounded by E and tdr
   realities.

3. **The tp channel is quadratic in E** (V_E ∝ 1/E²): early in a test tokens-for-time are
   cheap; near the end each time unit on the critical request is worth more. The endgame
   (§6, D8) is where V_E peaks — and it is the ONLY channel that grows over time.

### 4.2 Required outputs (closed forms of running means / remaining unknowns)

All four coefficients need only running quantities the solver tracks or can estimate:
Ntok, R, G (completed + estimated remaining: Ntok = done + Σρ̂), Ê (projected last
finish), TDR̂ (running mean of finished + projected), TPOT̂ (running pooled span).
Clamps: while TDR̂ ≤ SLO1, set σ₁=0 (tdr axis worthless — but keep an option-value margin:
if TDR̂ is within noise of SLO1, treat σ₁ ≈ ½ as a hedge); same for σ₂; while
tp ≤ tp_base, V_E = 0; while tp ≥ tp_UB, V_E = 0.

### 4.3 When myopic marginal-score maximization is optimal — and when it is not

**Optimal (exchange-argument-safe) when all three hold:**
(i) all channels linear (unclamped, quasi-static coefficients);
(ii) the only contended resource at the decision instant is the one being ordered (the
local machine), and tasks' score effects are additive;
(iii) no max-coupling is active (the acting request is not projected to set T_last).
Then ordering by **value-rate Π(τ) = Gain(τ)/(S+dur(τ))** is Smith's rule (WSPT) for the
weighted-completion objective `1||ΣwC_j` [PROVEN-theory: research/papers/
eos-ch04-weighted-completion.txt ch. 4.1; Lawler–Queyranne–Schulz–Shmoys] — optimal by
interchange for the one-machine problem it models.

**Fails, with the specific failure and fix:**
(a) **Max-coupling (T_last)**: the value of advancing task work depends on whether its
request is the projected argmax; exchange arguments break. Fix: attribute the V_E term
only to the projected-last request i\* (re-project F_i after each candidate action);
everything else keeps its additive gradient. Residual error = mis-projection of i\*.
(b) **Kinks (clamps)**: zero gradients make mandatory work look worthless (e.g., prefill
   when tdr̂ ≤ SLO1). Fix: work-conserving override — never idle a resource with legal
   work; value-rates only ORDER work [PROVEN: Dai et al. Thm 2, §5.1]. Near kinks,
   quasi-static gradients are discontinuous; use the half-weight hedge above.
(c) **FIFO externality**: issuing a task adds transfers at the tail of a FIFO; the cost is
   borne by everyone queued behind (Σ their value-rates × added delay). First-order when
   the link is saturated; ignorable when idle. Fix: admission test with a link shadow
   price (§5.4): issue iff Gain ≥ λ_up·(link time added) + λ_cloud·(cloud time added)

   + λ_loc·(local time added).

### 4.4 The sacrifice inequality (per-request waiting vs aggregate throughput)

Delaying request `i`'s `P POST` by `X` to buy `dE` on the critical path is profitable iff:

```text
V_E * dE > V_P * X  <=>  dE / X > V_P / V_E
```

```text
V_P / V_E = w_c * s1 * (tp_UB - tp_base) * E^2 / (w_tp * Ntok * SLO1 * R * dist_base)
```

- `V_P / V_E` — exchange rate; `V_P` carries `1/R`; `V_E` does not.

V_P carries 1/R; V_E does not ⇒ **for large R the tp channel dominates any bounded
per-request sacrifice**; when σ₁=0 (final tdr within SLO) sacrifice through tdr is FREE.
The only true prices of sacrifice are: the delayed work's own finish time (it may become
the new critical request) and resource contention. [DERIVED-here; matches SA06 §1.1 and
sharpens it with the σ₁=0 case and the 1/R scaling]

---

## 5. Stability / queueing priors (Task 5)

### 5.1 Work-conserving is throughput-optimal — but only without binding caps [PROVEN-theory]

Dai, Deng, Li, Peng, "Throughput-Optimal Scheduling Algorithms for LLM Inference and AI
Agents" (research/papers/2504.07347.txt):

- **Thm 2** (lines ~988-1014): under λ(mp+md) < bmax/tbmax, ANY **work-conserving +
  K-FCFS** (bounded overtaking) algorithm is stable (positive recurrent); above the load
  line, everything diverges. Work-conserving = fill every feasible batch/slot whenever
  supply exists.

- **Thm 10** (batch-size cap kmax): with caps, the stability region becomes a 2-D convex
  hull; **work-conserving alone no longer achieves the full region** — prefill throughput
  and decode throughput genuinely compete under caps. This is the theory of our
  max_inflight throttle: caps create a prefill-vs-decode trade that pure greediness loses.

- They also construct a Rybko–Stolyar-type cyclic-routing example where work-conserving is
  NOT stable (their §1/intro): parallelism + cyclic dependencies can destabilize
  work-conserving policies — the formal warning behind "adding in-flight work can reduce
  total throughput" when round-trip dependencies exist (our D PRE round-trip through a
  shared FIFO is exactly such a cycle).

### 5.2 Little's law / saturation bounds [PROVEN-theory + DERIVED constants]

Little's law E[L] = λE[S] (research/papers/adan-resing-queueing-theory.txt §3.4,
lines 1227-1253). Applied to our system [DERIVED constants]:

- E ≥ Ntok·max_r c_r(m,k) (§2.4) — matches SA09's link/cloud/local floors; the ceiling is
  the bottleneck resource's per-token cost.

- Mean tdr ≥ half the prefill backlog / admission rate: with a burst of R prefills at
  t=0 processed by a serial local stage of per-request local time a_i, mean ppost ≥
  (Σ a_i)/2 + pipeline constants (one-machine averaging; equality under SPT-ish orders).

- Pollaczek–Khinchine / Kingman for the FIFO delay terms when arrivals are bursty
  (research/papers/pk-kingman-modern.txt §4.2.1): queueing delay grows with variability —
  arrival burstiness multiplies UP-queue stalls; another reason to pace P PRE issue.

### 5.3 Back-pressure / max-weight: what transfers and what does not [PROVEN + DERIVED]

Tassiulas–Ephremides 1992 (IEEE TAC; original PDF
https://drum.lib.umd.edu/bitstreams/ec9ae6dc-036d-4681-acbb-4a35a31100c3/download):
serving the schedule with max Σ (queue length × service rate) is throughput-optimal
(stabilizes everything inside the capacity region).

- **What does NOT transfer**: our link queues are FIFO by issue order — we cannot
  re-order service, so max-weight *matching* is impossible. The system is input-queued,
  and the only control is admission/window.

- **What DOES transfer [DERIVED-here]**: (a) the *weights*: differential backlog in
  score-units — the shadow price of a saturated resource = the marginal score-rate the
  best queued alternative would earn; (b) the *window*: cap in-flight work so the shared
  FIFO never forces the round-trip cycle to stretch: A ≤ A\* = (P_round − kℓ)/β (§2.4);
  (c) the *gate*: admit work whose score-gain exceeds shadow-priced resource cost
  (§4.3(c) Lagrangian test). This is TCP-window-control shape, theory-backed by the
  max-weight stability region.

- **When adding in-flight work REDUCES total throughput [DERIVED-here]**: exactly when
  the admission raises a saturated resource's utilization past 1 (then P_round stretches
  for ALL in-flight requests — the FIFO serializes the round-trip cycle) or when it
  triggers the Rybko–Stolyar cyclic stall (a request's decode waits on a transfer stuck
  behind work its own admission created). Below saturation, added in-flight work strictly
  helps (fills pipeline; work-conserving Thm 2). The empirical local finding "no idle
  channel; E/mk ∈ [0.975,1.000]" (docs/DECISIONS.md D-144) is the signature of operating AT
  A\*: gains must come from lowering the per-token cost c_r (bigger groups, better
  spreading, endgame E), not from more admission.

### 5.4 DERIVED: shadow-price admission gate (the D PRE / P PRE issue test)

Maintain per-resource shadow prices, updated each frame from observed busy fractions:

```text
lambda_r <- lambda_r*(1-eta) + eta*(busy_r)*V_tok_rate
```

- `lambda_r` — shadow price per resource; `eta` — EWMA step.
- `V_tok_rate` — current best score-rate per resource-time unit (`V_E*(Ntok/E)` per time on the critical path; or `V_S*token-rate`).

Issue a candidate group/task iff:

```text
ScoreGain(candidate) >= lambda_up*d_up + lambda_down*d_down + lambda_cloud*d_cloud + lambda_loc*d_loc
```

- `d_up`, `d_down`, `d_cloud`, `d_loc` — resource-time increments INCLUDING the queueing externality (own service + induced delay of queued work).
- When all `lambda_r = 0` (idle resources), the test always passes ⇒ work-conserving is the special case. [DERIVED-here from max-weight/KKT; throughput side PROVEN by analogy only]

---

## 6. DECISION-RULE TABLE (Task 6)

Per recurring solver decision: rule, guarantee, required inputs, C++-ready pseudocode.
All pseudocode assumes the existing state (busy_until per resource, UP/DOWN tails,
ready lists, per-request tokens_i, Lin, arrival; table lookups O(log N)).

### D1. Which local task next (P PRE vs P POST vs D PRE vs D POST contention)

- **Rule**: WSPT value-rate with gradient weights (§4.1-4.3): Π(τ) = Gain(τ)/(S+dur(τ));
  Gain = Σ channel gradients × projected milestone advances, V_E attributed ONLY to the
  projected-last request. Work-conserving override: if a resource would idle and a legal
  task exists, run the best-Π legal task (never idle for value reasons).

- **Guarantee**: Smith's rule optimal for the uncoupled one-machine analog
  [PROVEN-theory]; work-conserving override throughput-optimal absent binding caps
  [PROVEN-theory, Dai Thm 2]; overall [HEURISTIC-unbounded] on the coupled system but
  principled (fails only through mis-projection and kinks, §4.3).

- **Inputs**: V_E,V_P,V_S (running), projections ppost_i, e_first/last_i, F_i (queue
  estimates), S+dur per candidate.

- **Pseudocode**:

```cpp
double gain(Task t) {                       // per candidate local task
  double g = 0;
  if (t.step==PPOST) g += V_P * advanceEst(t.req, ppost);      // tdr now
  if (t.step==DPRE)  for (id : t.group) {                       // tokens one round later
    g += V_S * advanceEst(id, elast);                          // span compression
    if (id == projLastReq()) g += V_E * advanceEst(id, F);      // critical request only
  }
  if (t.step==PPRE)  g += V_P * advanceEst(t.req, ppost) + unlockValue(t.req);
  return g / (S + dur(t));                                     // WSPT ratio
}
// pick argmax gain() among legal tasks; if none legal for an idle resource -> idle;
// never skip a legal task when the resource would otherwise idle (work-conserving).
```

### D2. Which request gets P PRE next + which cloud

- **Rule (request)**: SRPT on prefill remaining work pproc(Lin_i) (clairvoyant — Lin is
  known); tiebreak by tdr-gradient value and age floor. **Rule (cloud)**: least projected
  decode load — load_j = (pending decode tokens on j)/(1) scaled by its per-token cost —
  NOT least prefill count: permanent assignment defines future D PROC geometry; balance
  by expected DECODE work E[Lout|bucket(Lin)]·rate. When link-bound prefer concentrating
  on FEWER clouds (fewer headers, §2.4); when cloud-bound prefer spreading.

- **Guarantee**: SRPT [PROVEN for 1|r_j,pmtn|ΣC, Schrage]; load balancing by two-choices
  is exponentially better than random [PROVEN-theory: research/papers/
  Azar_Balanced_Allocations_1999.pdf; Mitzenmacher
  research/papers/Mitzenzenmacher_Power_Two_Choices_2001.pdf — with K≤8 and full state,
  global least-loaded dominates two-choice]; cloud-split trade [DERIVED-here, §2.4].

- **Inputs**: pproc(Lin), per-cloud in-flight token counts, k\* from §2.4 scan, Lout
  bucket estimates.

- **Pseudocode**:

```cpp
// request choice: min prefill remaining; age floor keeps proj-F feasible (SA09 dep. bound)
rid = argmin_ready( pproc(Lin[i]) - 1e-9*ageFloor(i) );
// cloud choice: min projected decode-busy time; bias to fewer clouds when link-bound
for (j in 0..K-1) w[j] = decodeTokens[j]*(S+dproc(tokens[j]))/max(1,tokens[j]);
j0 = argmin_j w[j];
if (linkBound() && canFit(w[j0]+me, j1)) j0 = j1;    // concentrate when 2k*1 > k*2
```

### D3. Prefill chunking (chunk or not; sizes)

- **Rule**: chunking = preemption granularity, NOT transfer splitting (§2.3). Chunk a
  prefill iff (a) its un-chunked duration exceeds the decode-stall budget G (so
  interleaving matters: some decode work waits on this cloud), and (b) the amortization
  loss ((c−1)·S extra) is under the stall saving. Bound each chunk by the gap budget:
  (le−ls)/L·pproc(Lin) ≤ G. G from tpot slack: G ≈ θ·max(0, SLO2 − P_round) with
  θ<1; if tpot is already breached and E is the binding channel, G from V_E trade.

- **Guarantee**: chunked prefill bounds decode stall by chunk time — the Sarathi-Serve
  design property [PROVEN-as-designed, research/papers/2403.02310.txt]; the S≥1 setup
  per chunk makes total cost (c−1)·S + fill [FACT protocol]; overall rule
  [DERIVED-heuristic]. Note `cfg.S<1.0 && num_layers>8` gate already in solver is the
  S-amortization guard (D-150 falsification: adaptive chunking always-on is wrong).

- **Inputs**: pproc(Lin), num_layers, S, decode-waiting work on that cloud, SLO2 slack.
- **Pseudocode**:

```cpp
double G = theta * max(0.0, SLO2 - Pround());          // stall budget
if (noDecodeWaiting(cloud) || S >= pproc(Lin)) return oneChunk;   // no benefit
int c = 1; while (pproc(Lin)/c - S > G && c < numLayers) c++;    // smallest c meeting G
// split into c near-equal [ls,le) pieces (equal pieces: local optimum of the bound)
```

### D4. D PRE group formation (size, membership, hold-or-issue)

- **Rule**: three-regime batch size (§1.4-1.5): q\* = √(2λ·f0) thin; λf0/(1−λa)
  critical; m_tp = max{m: P(m) ≤ SLO2} saturated; always q ≤ ready pool. Membership:
  prefer same-cloud members (fewer headers) unless local-bound (then cross-cloud to save
  local D PRE/D POST setups: trade (k−1)·2ℓ link vs (k−1)-fewer local setups per round).
  Hold-or-issue: issue immediately when any target resource would otherwise idle
  (work-conserving); hold up to fill threshold (q\*−n)/λ̂ only when all resources are
  busy with higher-value work.

- **Guarantee**: Deb–Serfozo threshold-structure [PROVEN-theory]; fluid q\* closed forms
  [DERIVED-here]; m_tp exact per-request tpot feasibility [DERIVED-here: gap between
  consecutive tokens of a always-batched request = round period]; membership trade
  [DERIVED-here §2.4].

- **Inputs**: λ̂ (token-ready rate, EWMA), f0 = 3S+dpre+dpost+2kℓ (+2βq), table
  derivatives, ready-pool size and per-cloud counts, SLO2.

- **Pseudocode**:

```cpp
double f0 = 3*S + dpre(1)+dproc(1)+dpost(1) + 2*kOpt()*lat;
double q = (poolThin()) ? sqrt(2*lamHat()*f0) : mTP();     // regime switch
q = clamp(q, 1, min((double)ready.size(), (double)RMAX));
if (allResourcesBusy() && ready.size() < q
    && fillEta() < q/among(lamHat()))  return HOLD;        // threshold hold (rare)
emit group: same-cloud members first; add cross-cloud only if localBound();
```

### D5. UP-gate / in-flight cap / P PRE pacing

- **Rule**: window A ≤ A\* = (P_round − kℓ)/β (§5.3) with safety factor; pace P PRE
  issue so its UP lands in projected link troughs; hard-stall guard: a queued prefill
  burst of B must not add more than G to decode gaps: B·(ℓ+βL̄in) ≤ G budget (spread the
  burst otherwise). When link idle: no gating (work-conserving).

- **Guarantee**: A\* derivation [DERIVED-here]; the failure it prevents (cap-induced 2-D
  region where prefill steals decode throughput) [PROVEN-theory, Dai Thm 10]; pacing
  [HEURISTIC-unbounded, Kingman-supported (variability ⇒ delay)].

- **Inputs**: P_round, k, β, ℓ, UP tail projection, decode-waiting count, G budget.
- **Pseudocode**:

```cpp
double Astar = (Pround() - kOpt()*lat) / beta;            // per-direction saturation
if (inflight() >= safety*Astar && linkBusyFrac() > 0.95) defer(candidatePPRE);
else if (upTailProject() + xfer(Lin[cand]) > gapBudget()) defer(candidatePPRE);
else issue;                                               // work-conserving when link idle
```

### D6. D POST grouping

- **Rule**: same marginal-batch logic as D4 on the local machine (D POST emits tokens:
  the milestone moves are e_first/e_last per member). Group across clouds freely (D POST
  has no cloud constraint); size by local-machine value-rate: m_dpost maximizes
  V_S-weighted token advancement per local-time (2S+dpost(m))/m — unbounded amortizing,
  capped by pool and by added DOWN header count (per-cloud sub-batches each add a DOWN
  header).

- **Guarantee**: [DERIVED-here] (same serial-batch calculus; no independent classical
  result for cross-cloud recombination).

- **Inputs**: ready D PROC completions, dpost table, V_S, DOWN tail.
- **Pseudocode**:

```cpp
// batch all ready DPROC-completions whose DOWNs have landed; cap by tpot-neutral bound
m = min(readyPost.size(), argmax_m { m*V_S*advEst - (2*S+dpost(m))*lamLocal() });
emit E D POST with those m ids;
```

### D7. Decode priority among requests (which requests join the next group)

- **Rule**: estimate-driven SRPT with SETF fallback and aging floor (§3): priority
  1/(α·ρ̂_i + (1−α)·tokens_i); α from bucket confidence; escalate the projected-last
  request i\* (V_E term). Exclude Lout=1 requests from tpot-driven priority.

- **Guarantee**: SRPT [PROVEN if ρ̂ exact]; SETF [PROVEN-theory Θ(log n) optimal
  deterministic non-clairvoyant]; blend [PROVEN-framework LAP consistency/robustness,
  2305.07164 Thm 3.7].

- **Inputs**: ρ̂_i (bucket estimator + EWMA), tokens_i, bucket sample counts, projLast.
- **Pseudocode**:

```cpp
double pri(int i) {
  double rho = max(1.0, estLout(Lin[i]) - tokens[i]);
  double a  = conf(bucket(Lin[i]));                 // samples, dispersion
  double p  = 1.0/(a*rho + (1-a)*tokens[i]);        // SRPT<->SETF blend
  if (i == projLastReq()) p += V_E/V_S;             // E-channel escalation
  if (estLout(Lin[i]) <= 1.5) p *= 0.5;             // Lout=1: no tpot stake
  return p;                                          // higher = join next group first
}
```

### D8. Tail / endgame behavior

- **Rule**: when the arrival stream is (believed) dry — long ARR gap vs the interarrival
  EWMA, or finished+inflight ≈ plausibly-all — switch objective to minimizing
  max_i F_i (E is now the only growing channel; V_E ∝ 1/E² is at its peak): (1) attribute
  V_E+V_S to the projected-last request and order its chain first (critical-path / LPT
  on remaining work ρ̂_i·chain_min + solo prefill remainder); (2) form MAXIMAL same-cloud
  decode groups (amortization strictly shortens the tail since P_round(m) sublinear);
  (3) park first-tokens freely (V_S rewards late e_first when e_last is fixed by other
  constraints — park everything that cannot finish earlier anyway); (4) stop pacing
  (gates D5 off — no future arrivals to protect).

- **Guarantee**: LPT ≤ (4/3)·OPT for P||Cmax offline [PROVEN-theory, classical Graham];
  list-scheduling 2−1/K online for Cmax [PROVEN-theory]; our chained/FIFO version
  [HEURISTIC with LPT analogy + DERIVED value attribution]. Endgame compression
  (park-then-burst) [DERIVED-here from span calculus].

- **Inputs**: ARR-gap statistics, remaining ρ̂ per request, chain_min (SA09 dependency
  bound machinery), V_E.

- **Pseudocode**:

```cpp
if (arrGap() > 3*arrEWMA() || sinceLastARR() > tailThresh()) {
  for (i : inflight) cp[i] = soloRemaining(i);        // prefill left + rho*chain_min
  int star = argmax(cp);                               // critical request
  boost(star, V_E/V_S);                                // its tasks top every queue
  dpreCap = mTP();                                     // maximal groups, tpot-capped
  gatesOff(D5); parkFirstTokens(nonCritical);
}
```

---

## 7. The three highest-leverage replacements (archival summary, recorded 2026-08-23)

1. **D1/D7 — replace/augment fixed pick order and decode priority with WSPT value-rate
   Π = Gain/(S+dur) using the §4.1 gradient coefficients, V_E attributed only to the
   projected-last request.** Grounded in Smith's rule (optimal for the uncoupled analog)

   + SRPT/SETF (Schrage; KP) + LAP blending; the coefficients are 4 closed forms from
   running quantities the solver already tracks.

2. **D4/D5 — replace hand-tuned in-flight caps and batch thresholds with the three-regime
   batch law (q\*=√(2λf0) / λf0/(1−λa) / m_tp) and the window A\*=(P_round−kℓ)/β with the
   shadow-price admission test.** Grounded in Deb–Serfozo threshold optimality (PROVEN),
   Dai et al. Thm 2/Thm 10 (work-conserving optimal; caps create the 2-D region that
   justifies gating), Tassiulas–Ephremides weights.

3. **D8 — install the endgame regime switch (critical-path priority + maximal groups +
   first-token parking).** Grounded in LPT/Graham bounds + the derived span calculus
   (late e_first strictly gains V_S; V_E ∝ 1/E² peaks at the tail). Directly targets the
   tp channel, which the D-144 saturation model shows is where the remaining ~1,100 pts
   on #5/#6 live.

---

## 8. References

Downloaded and verified in `/home/dlamini/ICPC2026/research/papers/`:

- `2504.07347.{pdf,txt}` — Dai, Deng, Li, Peng: work-conserving + K-FCFS throughput
  optimality (Thm 2), batch-cap 2-D region (Thm 10), Rybko–Stolyar-type instability.

- `botsali2019-lot-streaming.txt` — geometric sublot formulas (eqs 3-7), 3-machine
  condition, transporter note; after Potts–Baker 1989 / Trietsch–Baker 1993 /
  Glass–Gupta–Potts 1994.

- `BakerTrietsch2009-notes-ch13.txt` — bibliographic anchors: Santos–Magazine 1985,
  Dobson–Karmarkar–Rummel 1987, Coffman et al 1990, Monma–Potts 1989, Albers–Brucker
  1993, Potts–Kovalyov 2000, Allahverdi et al 2008.

- `eos-ch04-weighted-completion.txt` — Smith's ratio rule, preference orders (Lawler,
  Queyranne, Schulz, Shmoys).

- `1810.01223.txt` — Deppert–Jansen: batch setup makespan 2-approx linear / (3/2+ε).
- `1911.12350.txt` — Hermelin–Mnich–Omlor: 1|B,r_j|Σw_jU_j NP- and W[1]-hardness.
- `2503.23273.txt` — Li–Wen–Wei: serial-batch (Cmax,fmax) Pareto O(n³); release-date
  NP-hardness anchors.

- `2504.08793.txt` — Huertas–Van Hentenryck: serial batch with minimum batch sizes (CP).
- `2305.07164.txt` — Liang–Stein–Wei: LAP learning-augmented deadlines (Thm 3.7,
  Prop 4.1).

- `2403.02310.txt` — Sarathi-Serve: chunked prefill stall-bounding.
- `adan-resing-queueing-theory.txt` — Little's law (§3.4), bulk-service chapters.
- `pk-kingman-modern.txt` — Pollaczek–Khinchine / Kingman variability terms.
- `Azar_Balanced_Allocations_1999.pdf`, `Mitzenmacher_Power_Two_Choices_2001.pdf` —
  balanced allocations / two-choice load balancing.

- `elidrissi2020-single-server-thesis.pdf` — P,S1 single-server parallel machines.

Primary URLs (not downloaded):

- Schrage 1968, SRPT optimality: https://pubsonline.informs.org/doi/10.1287/opre.16.3.687
- Megow–Schulz 2004 (SRPT online-optimal): https://web.mit.edu/schulz/www/epapers/ms-orl-2004.pdf
- Kalyanasundaram–Pruhs 2000 (SETF scalable; paywalled — second-hand constants):
  J.ACM 47(4):617-643; see research/notes/306-kp-speed-as-clairvoyance.md.

- Non-clairvoyant flow-time (MLF/SETF Θ(log n) optimal):
  https://www.researchgate.net/publication/221590801_Non-clairvoyant_scheduling_to_minimize_the_average_flow_time_on_single_and_parallel_machines

- Deb–Serfozo 1973, batch-service threshold optimality:
  https://www.researchgate.net/publication/305982505_Optimal_control_of_batch_service_queues ;
  https://en.wikipedia.org/wiki/Bulk_queue ;
  https://www.eurandom.tue.nl/reports/1999/021-report.pdf

- Tassiulas–Ephremides 1992 (original):
  https://drum.lib.umd.edu/bitstreams/ec9ae6dc-036d-4681-acbb-4a35a31100c3/download

- Trietsch–Baker 1993: https://pubsonline.informs.org/doi/10.1287/opre.41.6.1065
- Mitzenmacher–Vassilvitskii, Algorithms with Predictions: https://arxiv.org/abs/2006.09123
- Soukhal–Oulamara–Martineau 2005 (transportation flowshop NP-hard):
  https://ideas.repec.org/a/eee/ejores/v161y2005i1p32-41.html

- Abdekhodaee–Wirth (P,S1): https://www.sciencedirect.com/science/article/abs/pii/S0305054804002035 ;
  thesis: https://uphf.hal.science/tel-03429508v1/document

Internal (this repo, verified): `research/scoring/SYNTHESIS.md` (authoritative formula),
`research/scoring/SA09_ceiling_analysis.md` §1.1 (transfer-volume facts, E floors),
`research/scoring/SA06_admission_value.md` §1.1 (delay-profitability inequality),
`docs/ASSUMPTIONS.md` §5-§8 (protocol + scoring), `src/ARCHITECTURE.md` (solver decision inventory,
D-119/D-140/D-142/D-144/D-150 evidence base).
