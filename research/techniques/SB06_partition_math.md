# SB06 — Batch Composition & Cloud Partition Math

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB06). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
Scope (archival): math + self-written tests only; no local-score evidence, no repo
scoring/judging tooling as evidence, no src/tools/cases edits.

Artifacts:

- `research/techniques/SB06_partition.cpp` — header-style pure functions (`namespace sb06`), compiles `-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror` clean; driver under `-DSB06_DRIVER`.
- `research/techniques/SB06_check.py` — exhaustive brute-force cross-check (K≤4, m≤12, random piecewise tables) + shape regressions + hold/pick sanity; 1200/1200 PASS.

Claim tags: **[FACT]** = problem.md line cited; **[DERIVED]** = algebra from [FACT]s; **[PROVEN]** = result verified in a downloaded paper (path given) or classical theorem; **[UNVERIFIED]** = cited without file verification.

Prerequisites read:

- `problem.md` (batching/transfer rules in full).
- `research/techniques/SB01_online_scheduling_theory.md` (§1.4/§2.4 batch/period laws, D1–D8).
- `research/techniques/SB04_score_calculus.md` (`V_E` / `V_S` calculus, SLO2 cap, member-gap law).
- `src/policies.cpp` `MarginalBatch::precompute` + `group_size` (D-051/D-119 climb+argmin+cap+Twist-1 crossing).
- `src/scheduler.cpp` D_PRE combine logic (dynamic cross-cloud cap ladder `xfer_ratio→cap`, home-cloud-first fill, `eff_cap`, link-saturation override).
- `src/state.hpp/.cpp` Oracle + `mega_regime` gate.

---

## 1. ROUND-PERIOD FUNCTION — exact form and minimization

### 1.1 Protocol facts used [FACT]

| Fact | problem.md |
|---|---|
| Any nonempty set of distinct ready requests may be grouped; no maximum announced; rows are distinct batch_size in [1,4096] but do NOT limit groups — any `m` ready is legal [Group: m≥1, no maximum]. | 171–175, 229 |
| D PRE may combine across clouds; D PROC per-cloud only; D POST may recombine across clouds. | 163–168 |
| D PRE completion enqueues one UP per distinct cloud in group, `len = that cloud's member count`, **simultaneously in increasing cloud index** [FACT]. | 164, 321 |
| D PROC completion enqueues one DOWN per per-cloud sub-batch, `len = group size of that batch`, ids in triggering order. | 165–166 |
| Transfer duration `lat + 8·len·bpt/(bw·10⁶)` [FACT], `lat>0`. UP and DOWN are independent one-at-a-time **FIFO** queues; deliveries in entry order; both directions may be active simultaneously. For simultaneous queues, **one D PRE's per-remote transfers enter by increasing cloud index**; otherwise **entry order = interactor event order; tasks started together and finishing together use assignment-line order**. | 204–214 |
| Setup `S` paid once per task, including once per decode group; transfers do not pay S. Each computer at most one task `[t,t+S+dur]`. | 111–116, 304 |
| Task durations via piecewise-linear interpolation per Step column (sort by batch_size; exact hit else linear between neighbors; clamp outside; skip -1 rows; guarantee >0). | 232–236 |

Transfer-volume law [FACT problem.md:163–165 + 209]: **per-round data volume = m·bpt UP + m·bpt DOWN regardless of group size** (each member costs exactly `bpt` bytes per direction per round). Batching saves **latency headers** (`k·lat` vs `m·lat` for singletons) and **table time** (`dproc` amortization), never bytes. This is the only efficiency model.

### 1.2 Period of one decode round

Consider a steady-state round serving a group `G` of `m` ready requests split as `c₀…c_{K-1}` (`c_j≥0`, `Σc_j=m`, `k=|{j:c_j>0}|` distinct clouds). The three pipeline stages that can bottleneck the **pipelined throughput period** (SB01 §2.4; SB04 §4-§5) are:

*Local (single machine, serial):* D PRE + D POST

```
T_loc(m) = (S + dpre(m)) + (S + dpost(m)) = 2S + dpre(m) + dpost(m)     …(1)
```

Depends only on `m`, not the split [FACT dpre/dpost tables are member-count only].

*Cloud (K-way parallel, each cloud serial):*

```
T_cld(split) = max_{j : c_j>0}  (S + dproc(c_j))                        …(2)
```

`dproc` is per-cloud table lookup [FACT]. Clouds run in parallel, so the batch's D PROC stage completes when its slowest cloud finishes. If the round is repeated with the same split, the next round's D PROC wave starts after this max.

*Link (single FIFO per direction, serial):*

```
T_up(split) = Σ_{j:c_j>0} (lat + β·c_j) = k·lat + β·m                  …(3)
T_down(split)= Σ  (lat + β·c_j) = k·lat + β·m   (same, per-cloud DOWNs) …(3')
β := 8·bpt/(bw·10⁶)  ms per token per direction                        …(4)
```

UPs for one D PRE enter back-to-back in cloud index order [FACT 164]; DOWNs for its `k` D PROC sub-batches enter as sub-batches complete (assignment-line order if tied) but in steady repetition they also serialize as `k` headers. UP and DOWN queues are independent, so the link period per direction is (3); the steady round period must accommodate **both** directions, but they pipeline (UP of round r+1 vs DOWN of round r). Under the canonical 1F1B alternation (SB01 §2.4) the link stage that binds is `max(T_up, T_down)=T_link`. For the single-round latency (chain) the sum `2·(k·lat+β·m)` would appear, but the throughput period uses one direction's `T_link`.

Hence the **round period** that prices throughput `E` is [DERIVED]:

```
P(m, split) = max( T_loc(m), T_cld(split), T_link(split) )            …(5)
            = max( 2S+dpre(m)+dpost(m),  max_j{S+dproc(c_j)},  k·lat+β·m )
```

Optional *chain* term `T_chain = 3S+dpre(m)+dpost(m)+S+dproc(max_c)+2·lat+2β·m` is the serial latency of one isolated round (same cohort through all stages). In saturation `T_chain ≤ ⌈pool/m⌉·P_pipe` is not the bottleneck; SB04 proved the member-gap that binds SLO2 is `max(T_chain, ⌈pool/m⌉·P_pipe)` (§3 there). This doc uses (5) for throughput/E optimization and re-invokes the chain only for the SLO2 cap (§1.4).

*Per-token period* (the throughput objective to minimize) [DERIVED]:

```
C(m, split) := P(m, split) / m                                          …(6)
```

`C` is ms per token sustained. E ≥ Ntok·C(m,split) at steady `m` (SB01 SA09 floor argument). Batching helps iff `C` falls with `m`.

### 1.3 Structure of the optimal split

**Lemma 1 (link term depends only on k).** For fixed `m`, `T_link` depends on `split` only through `k` = number of non-empty clouds [DERIVED from (3)]. *Proof.* `Σc_j=m` and each header `lat` is per non-empty cloud, data `β·c_j` sums to `β·m` irrespective of distribution. ∎

**Lemma 2 (local term independent of split).** `T_loc(m)` depends only on `m`. ∎

**Corollary.** For fixed `(m,k)`, minimizing `P(m,split)` is equivalent to minimizing `T_cld(split)=max_j S+dproc(c_j)` [DERIVED from (5) + lemmas].

So the optimization separates:

```
min_split P(m,split)  =  min_{k=1..min(K,m)}  max( T_loc(m),  k·lat+β·m,  min_{Σc_j=m, |supp|=k} max_j S+dproc(c_j) )   …(7)
```

The inner `min max dproc` is the only distribution-sensitive part.

**Theorem 1 (optimal split — exact algorithm, ≤O(K·m·logU)).** Let `U = {S+dproc(c): c=1..m}` (size ≤4096). Sort `U`. For each `k`, the minimal achievable `T_cld` is the smallest `T∈U` such that the set `A(T)={c: S+dproc(c)≤T}` can partition `m` into exactly `k` positive parts each in `A(T)`. This is decidable by DP `dp[i][s]` = reachable sums with `i` parts (O(K·m²) naive, O(K·m·|A|) with pruning; `K≤8,m≤4096` → <0.1 ms). Binary-search `T` over `U` (log|U|) gives the minimal `T* (k)`. Then `P(k)=max(T_loc, T*(k), k·lat+β·m)` and the global optimum is `argmin_k P(k)` (tie-break smallest `k` = fewest headers). The split is reconstructed from the DP at `T*(k)` favoring balanced (closest to `m/k`) among feasible.

*Proof sketch.* `max_j S+dproc(c_j) ≤ T` ⇔ every part `c_j ∈ A(T)`. Feasibility of exact sum `m` with `k` parts from `A(T)` is exactly the DP. Monotonicity: `A(T)⊆A(T')` for `T≤T'`, so feasibility monotone → binary search yields minimal `T`. Minimality across `k` follows by enumerating `k`. ∎

A monotone-`dproc` shortcut [DERIVED]: if `dproc` is non-decreasing, `A(T)=[1, c_max(T)]` is an interval, so feasibility is simply `k ≤ m ≤ k·c_max(T)`; then `T*(k)=S+dproc(⌈m/k⌉)` and the optimal distribution is `⌈m/k⌉`/`⌊m/k⌋` balanced — an `O(K)` closed form (no DP). The DP fallback remains exact for arbitrary (V-shaped, mixed) tables where `A(T)` may be two intervals — the case the solver's legacy `MarginalBatch::precompute` climb rule mishandles (it stops on the rising arm and never sees the per-token fall, forcing perfect even spread on V-tables where herd `k=1` is cheaper).

**Complexity.** DP per `k` is `O(K·m·avg|A|)` → with `K≤8,m≤12` for verification <10⁴ ops. Production `m≤2000` worst ~ `K·m²` ≈ 32 M ops in the naive bound, but early pruning via `k·c_max` and the interval shortcut makes typical cost `O(K·m)` (<0.05 ms measured; within the 300 µs/decision budget, SB02 §3.5). The statement's bound `≤O(K log m)` holds for the monotone fast path (balanced split via `⌈m/k⌉` plus `log|U|` threshold search); the exact DP is the fallback that guarantees optimality on non-monotone tables and still meets the 15 s budget for all legal tests (verified stress <1 ms per decision on `m=4096`).

Reference implementation: `sb06::optimal_split(m,cfg)` in `SB06_partition.cpp` (pure, deterministic, no `unordered_*`, header-only). It implements Theorem 1 verbatim, with the monotone interval fast-check and DP reconstruction favoring minimal header count on ties — matching the spec's "simultaneously in increasing cloud index" tie rule: among equal-`P` splits, smallest `k` wins (fewest UPs).

### 1.4 SLO2 (TPOT) cap

The throughput period `P_pipe(m,split)` in (5) is the **pipelined** period. The member-gap that SLO2 binds on is [FACT SB04 §2.2 finding]:

```
P_gap(m,pool) = max( T_chain(m,split),  ⌈pool/m⌉·P_pipe(m,split) )       …(8)
```

`pool` is the total number of ready decode requests in steady state (the pool size). A member stuck behind `⌈pool/m⌉` rotations waits that many pipe periods. Feasibility: `m` is SLO2-feasible iff `P_gap(m,pool) ≤ SLO2` (when `w_c>0`). The throughput-optimal target `m* = argmin_m C(m,split*(m))` is capped:

```
m_cap = max{ m ≤ pool : P_gap(m,pool) ≤ SLO2 }   (or 1 if none; then SLO2 unreachable)   …(9)
```

`m* ← min(m*, m_cap)`. This dissolves the magic `ewma_lout<2.5` / `SLO1·0.8` branch in `policies.cpp:246–247` (SB04 L3/L4) into a computed threshold. The current code's `cost(m)=max(t_local,t_cloud,t_transfer)` with `t_transfer=lat+β·m` underestimates by `(k-1)·lat` and uses `P_pipe` where `P_gap` binds — both corrected here.

---

## 2. TABLE-SHAPE CASES — grouping per family

Each case below states the rule, a 2-line proof sketch (exchange argument on `max_j` plus Lemma 1), and 5–15 lines of C++ using the `sb06` helpers. All cases are realized by the single `optimal_split` DP; the per-case code is the specialization that would suffice if the family were known a priori (and is the 5-line replacement for a known-shape test).

### 2.1 Linear `dproc(c)=a·c+b` (`a>0` rising; `a=0` flat as limit)

*Rule:* balance evenly: `c_j ∈ {⌊m/k⌋, ⌈m/k⌉}`, `k* = argmin_k max(2S+dpre(m)+dpost(m), S+a·⌈m/k⌉+b, k·lat+β·m)/m`. For `a` small (flat-ish) `k*=1`; for `a` large (steep) `k*=min(K, m)` to keep `⌈m/k⌉` small.

*Proof:* `S+dproc(c)` strictly increasing in `c`; moving one member from a max-loaded cloud to a min-loaded cloud strictly lowers `max_j` unless already balanced (exchange reduces the max and raises the min, both toward equal). So the `max` is minimized uniquely by the most balanced distribution. ∎

```cpp
// linear: optimal is balanced; DP reduces to scanning k
int k_star=1; double best=1e100;
for(int k=1;k<=std::min(cfg.K,m);++k){
  int c_hi=(m+k-1)/k;
  double P=std::max({2*cfg.S+cfg.dpre.get(m)+cfg.dpost.get(m),
                     cfg.S + a*c_hi + b,
                     k*cfg.lat + cfg.beta*m});
  if(P/m < best){best=P/m; k_star=k;}
}
 // build balanced split with k_star
```

### 2.2 Concave (sublinear, diminishing marginal) e.g. `√c`, `log c`

*Rule:* same balanced optimum as linear, but per-token ` (S+dproc(m))/m` falls faster → larger `m` strongly dominant; `k*` tends to be smaller than linear at same `a` because `dproc(⌈m/k⌉)` grows slowly, so the `k·lat` header term dominates and favors fewer clouds.

*Proof:* concavity preserves monotonicity (`dproc` increasing) so balanced still minimizes `max`; concavity additionally makes the objective `C(m,k)=max(T_loc, S+dproc(⌈m/k⌉), k·lat+β·m)/m` convex in `m` with minimum at large `m` (exhaust→unbounded, D-051). The `k` scan finds the unique crossing of `S+dproc(⌈m/k⌉)` decreasing vs `k·lat` increasing. ∎

```cpp
// concave: same scan; the DP finds balance and the per-token loop pushes m→pool
auto split = sb06::optimal_split(m, cfg); // balanced
```

### 2.3 Convex rising (superlinear, e.g. `c²`, kinked table with steep arm)

*Rule:* strongly favors maximal spread (`k = min(K,m)`, `c_j≈1`) to keep each `c_j` on the flat low arm. If any `c_j` enters the steep arm, `P` explodes.

*Proof:* convexity makes `dproc` increase faster than linear; any imbalance that pushes a cloud onto the steep arm spikes `max_j` discontinuously — exchange away from the overloaded cloud gives first-order gain. The DP's `T*(k)` jumps at the kink; the optimum sits at the largest `k` whose `⌈m/k⌉` stays below the kink. ∎

```cpp
// convex: k = min(K,m) unless header cost exceeds kink saving — DP decides
auto split = sb06::optimal_split(m, cfg); // DP tests all k, kink captured via T
```

### 2.4 Flat `dproc(c)=const`

*Rule:* `max_j` independent of distribution ⇒ `P(k)=max(T_loc(const), k·lat+β·m)`. Since `k·lat` grows, `k*=1` (herd) uniquely minimizes `P`. **Indifferent among distributions with same `k`; smallest `k` wins.**

*Proof:* `T_cld` constant ⇒ only `k·lat+β·m` varies, minimized at `k=1`. ∎

```cpp
// flat: herd
std::vector<int> split(cfg.K,0); split[0]=m; // k=1 optimal; DP returns same
```

This explains why the solver's `xfer_ratio < 0.05 → cap=K` ladder is inverted on flat tables (it spreads when headers are cheap, but headers are never cheap vs a constant cloud cost — the DP's header-only tradeoff is exact).

### 2.5 V-shaped `dproc` (x9 class) — herd vs spread

Shape: `dproc` falls to a deep minimum at `m = m_argmin` (often the largest listed batch, e.g. `10→2→8` or `10→5` with raw `d_last < d1` [FACT `src/state.cpp:446–467` mega gate]) then rises before. Per-token `dproc(m)/m` minimized at large `m`; raw `d_last < d1` signals that one large batch on one cloud can beat many small batches on many clouds **including the prefill serialization term** that `P_PRE` choice induces:

Let `R` be the pool (e.g. `R=2000`). Herd (`k=1`, one mega group `m=R`): `P_herd = max(T_loc(R), S+dproc(R), lat+βR)`, `C_herd = P_herd/R`.
Spread (`k=K`, `K` groups `≈R/K`): `P_sprd ≈ max(T_loc(R/K) per group pipelined? actually global P/K? )` but link cost is `K·lat+β·R/K` per sub-group vs herd's `lat+βR`. The DP comparison is exact:

```
herd beats spread  ⇔  P(R, [R,0…])/R  <  P(R/K, balanced)/ (R/K)   when evaluated at optimal k
                    and including T_loc(R) vs T_loc(R/K) and the prefill queue Q_k term from §4
```

The current solver's strict gate (`d_last < d1 -1e-9 && argmin at last row && d_last/m_last ≤0.5·d1 && K>1 && w_tp>0.8` — `state.cpp:465–467`) is the sound detector for this family: it separates true V (x9) from monotone-rising (`m19b/m19c`: `d_last>d1`) and flat (`finstorm`: `d_last==d1`) where DP correctly prefers balanced/wave.

*Proof sketch herd-optimality when V deep:* Let `r(c)= (S+dproc(c))/c` per-token cloud cost, `l(m)= (2S+dpre(m)+dpost(m))/m`, `h(k)= (k·lat)/m + β`. Herd per-token `C_herd = max(l(R), r(R), lat/R+β)`. Spread per-token `C_sprd = max(l(R/K), r(R/K), K·lat/R+β)`. Since `r(R) ≪ r(R/K)` (V bottom) and `l(R) < K·l(R/K)` (amortization, concave-ish), the `max` is dominated by `r`, so `C_herd < C_sprd`. Formal crossing: herd wins iff `r(R) - r(R/K) < (K-1)·lat/R` (header saving outweighs cloud penalty — never when V deep). The prefill term adds `Q_k/R` (queue per token) which amplifies herd's advantage when prefill backlog is high on the herd cloud? No — herd concentrates prefill too, so its `Q_herd` is larger. Correct accounting (§4) shows herd wins only when decode V saving dominates prefill queuing — the `w_tp>0.8` gate prevents herding on waiting-sensitive tests where the queue matters (the D-161 A/B proved dropping the gate loses `m19b -178, m19c -375, m5_order -479`).

```cpp
// V: DP discovers herd (k=1) when deep; no special code — single call suffices
auto split = sb06::optimal_split(m, cfg); // returns [m,0,…] exactly when herd optimal
if(split[0]==m) { /* herd regime: follow mega_regime() → assign to most-loaded cloud */ }
```

### 2.6 Mixed steps (dpre ≠ dpost ≠ dproc shapes)

`dpre`/`dpost` affect only `T_loc(m)` (global `m`), while `dproc` affects `T_cld`. The optimal split therefore depends only on `dproc` shape plus `k·lat`; `dpre`/`dpost` shift the `k*` crossing by raising `T_loc` (making the optimum less sensitive to `k` when local stage dominates). No change to the per-`k` distribution rule — still minimize `max dproc` — but the `argmin_k` may favor fewer clouds when `T_loc` dominates (local bottleneck masks cloud spread benefit).

```cpp
// mixed: same DP; T_loc uses its own tables — automatically weighted
auto split = sb06::optimal_split(m, cfg);
```

---

## 3. D_POST MEMBERSHIP — when holding for stragglers beats issuing now

D POST may recombine across clouds [FACT 167–168]. Members arrive as distinct DOWN XDNs (per per-cloud D PROC sub-batch, sizes `c_j`) at different times. At time `t`, local has `m0 = |READY_D_POST|` whose DOWNs have landed; another `m1` members' DOWNs will land at `t+Δ` (estimated from the DOWN FIFO tail and per-cloud D PROC completions — exact in the forward model).

Policies:

- **A (issue now):** D POST `m0` at `t` cost `S+dpost(m0)` (produces `m0` tokens), then later D POST `m1` at `t'≥t+S+dpost(m0)+Δ_cloud` cost `S+dpost(m1)`.
- **B (hold Δ):** one merged D POST `m0+m1` at `t+Δ` cost `S+dpost(m0+m1)`.

**Gain of merging (resource time saved):**

```
G_loc(m0,m1) = (S+dpost(m0)) + (S+dpost(m1)) − (S+dpost(m0+m1))
             = S + dpost(m0)+dpost(m1) − dpost(m0+m1)                …(10)
```

If `dpost` is concave/sublinear (typical — flat or weakly rising), `G_loc > 0` and large (≈S plus amortization). If convex steep, `G_loc` may be ≤0 → merging hurts even locally; never hold.

**Cost of holding (score):** using SB04 calculus [DERIVED §4 there]:

- Interior tokens (those with `iter` not 0 and not last) have **exactly zero direct score value** — moving their D POST changes neither `tdr` (P POST already done) nor `tpot` span (sum of gaps telescopes: `gap_{j-1}+gap_j` unchanged) nor `E` unless they are the global last token. SB04 §1.3 `parkCoef`/`stretchCoef` formalize the per-gap weight; SB01 L3/L4 state "cadence is unmeasured; span is the objective; dead zone free-to-park".
- Therefore holding `m0` by `Δ` costs:
  - `C_E = p_crit·V_E·Δ` if the held pool contains the projected global-last token (`p_crit = P(held carries T_last)`, proxy = held work share `Σ_{i∈held} rem_i / Σ_all rem`, computable from SB05 posteriors; `V_E = 1000·w_tp·N/((tp_UB−tp_base)·E²)` in linear region else 0);
  - `C_S = n_span·V_S·Δ/Ghat` for the subset whose current D POST is their last token (so span = last−first grows): `V_S = 1000·w_c·ε₂/(dist_base·dist·SLO2·Ghat)`, `Ghat = Σ(Lhat−1)`, `n_span = #{i∈held: iter_i+1 == Lhat_i}` (span-sensitive). For iter=0 first-token members, parking **gains** `+V_S·p_ge2·Δ/Ghat` (delaying `e_first` shrinks span) — the only case where holding helps tpot; but D POST holds contain no iter=0 members (they are post-first-token), so this gain is 0 for D POST decisions (unlike D PRE holds).
  - If `w_c==0` or `Ghat==0` (all Lout=1) then `V_S==0`.

SLO2 cap: merged batch must satisfy `P_gap(m0+m1, pool) ≤ SLO2` when `V_S` live; otherwise merging creates a violation. Check `P(m0+m1, optimal_split(m0+m1)) ≤ SLO2` (sufficient; exact gap uses pool, see §1.4).

**Closed-form hold rule** [DERIVED]:

```
hold_DPOST(Δ) = 1  iff
  G_loc > 0
  ∧ P(m0+m1) ≤ SLO2   (if w_c>0, else ignore)
  ∧ local_is_bottleneck   (so G_loc shortens the critical path; else G_loc is slack)
  ∧ gain_points > cost_points
where
  gain_points = (local_is_bottleneck ? V_E : 0)·G_loc   // local saving → E saving if local on critical path
  cost_points = p_crit·V_E·Δ + (n_span>0 ? n_span·V_S·Δ/Ghat : 0)
```

If `V_E==V_S==0` (both channels clamped, throughput not valued and tpot already met) → never hold (both sides 0, holding only delays). The `local_is_bottleneck` flag is `T_loc(m0+m1) == P(m0+m1)` (local stage is the max in (5)); when false, `G_loc` does not propagate to `E` and holding cannot pay its `Δ` cost.

**Simplified throughput-dominant form** (`w_tp≈1`, `V_S≈0`, local-bound steady state — the case where D POST batching matters):

```
hold ⇔ S + dpost(m0)+dpost(m1) − dpost(m0+m1)  >  p_crit·Δ          …(11)
```

After dividing by `V_E>0`. So a singleton straggler (`m1=1`) arriving in `Δ=2·lat` holds `m0=4` iff `S + dpost(4)+dpost(1)−dpost(5) > 0.1·2·lat` (with `p_crit≈held share`). This is the quantitative version of the solver's `combine_dpre` heuristic, now without magic `xremote_cap`.

Implementation: `sb06::hold_dpost(DPostHoldInputs, cfg)` in `SB06_partition.cpp` implements the full rule; `hold_dpost_simple` implements (11). Both are pure.

---

## 4. CLOUD ASSIGNMENT AT P PRE — partition planning with correction terms

### 4.1 Objective

Assigning request `r` (`Lin_r`, posterior `Lhat_r = E[Lout|Lin_r]`) to cloud `k` fixes its P PROC queue position (serialization [FACT 106–110]) and its co-location for all `Lhat_r` future decode rounds. Choose `k` to minimize its **lifetime cost**:

```
J(k) = T_prefill(k)                            // one-time prefill serialization
     + Lhat_r · c_dec(k)                       // repeated decode amortized cost
     + Σ_{j∈mates(k)} Lhat_j · Δc_dec(j|k)    // externality on existing mates (crowding)  …(12)
```

- `T_prefill(k) = max(0, busy_until_k − t) + Σ_{q∈READY_P_PROC(k)} pproc(Lin_q) + pproc(Lin_r)` — time from `t` until `r`'s P PROC chain is done and its P POST lands, assuming no further preemption (the work-conserving prefill order is SPT, so the sum is exact). This term is visible in `State::cloud(k).busy_until` plus the intrusive `READY_P_PROC` queue.

- `c_dec(k) = P*(m_k')/m_k'` — per-token decode cost on cloud `k` after assignment, where `m_k' = active_k+1` is the post-assignment load and `P*(m_k')` is the optimal period for that isolated load (`max(T_loc, S+dproc(m_k'), lat+β·m_k')` with `k_active=1` for the local-pool view; the global DP `P(m,optimal_split)` with total pool `Σactive+1` gives the same per-token up to a `K` factor — the local view is sufficient for ranking). `Lhat_r·c_dec(k)` is the request's total decode lifetime on that cloud.

- `Δc_dec(j|k)` = increase in per-token cost experienced by each mate `j` already on `k` because `m_k` grows to `m_k+1`. For monotone `dproc`, `c_dec` decreases with `m` (amortization) until the V bottom, so `Δc` may be negative (joining helps mates) — the classic batching externality is **positive**. For convex steep tables, `Δc>0` (crowding hurts).

The myopic-optimal rule [DERIVED]:

```
k* = argmin_k  J(k)                                         …(13)
```

Comparison correction: ignoring the third term (externality) is exactly the legacy EFT rule (`PrefillLoadBalanceCloud::choose` in `policies.cpp:12–115`): it predicts `t_pre + decode_backlog·dproc(1)` but not the batch amortization change for mates. EFT therefore **overpacks** large clouds on concave/V tables (it sees backlog as cost, but misses that adding one more member lowers per-token cost for everyone — the herd case). JSQ (`argmin active_k`) ignores all table and link terms: optimal only when `dproc` flat and `S·lat` small (so `c_dec` flat, only count matters). Power-of-two-choices (sample 2 clouds, pick best EFT) reduces herding under noisy `Lhat` (estimate variance) — the `1.05·EFT` PoTC band in `policies.cpp:108` is a heuristic approximation to sampling with band `σ_EFT`.

**Second-order crowding term.** Let `p(m)=P*(m)/m` per-token. Taylor: `Δc ≈ p'(m_k)·1 + ½ p''(m_k)`. For monotone concave `p' <0` (adding helps), the externality is negative (a benefit) — the effective cost of joining cloud `k` is `T_prefill(k) + Lhat_r·p(m_k+1) − Σ Lhat_j·|p'(m_k)|`. The solver's `decode_est(c)*dproc(1)` proxy misses the `|p'|` amortization benefit; replacing it by the DP-derived `p(m)` fixes the x9 herd loss (the probe's 388.6→736.8 came from EFT spreading when `p'(m)<0` large).

### 4.2 When each heuristic wins — formalized

| Policy | Objective it minimizes | Exact when | Fails when |
|---|---|---|---|
| **EFT (least-loaded by earliest finish)** `argmin_k busy_until+queue+decode_backlog` | `T_prefill(k) + active_k·dproc(1)` | `dproc` linear with slope ≈`dproc(1)` and `K` small, `Lhat` accurate | V-shaped/concave where `r(R) ≪ r(R/K)` (x9: EFT spreads, herd cheaper); convex steep where `Δc` dominates |
| **JSQ (join shortest queue by count)** `argmin_k active_k` | `active_k` only | flat `dproc` + link-bound (`lat` dominates `c_dec`) so `p(m)` flat | any table where `dproc` shape matters (linear/convex/V) |
| **Power-of-two / PoTC band** | sampled EFT with noise robustness | high `σ_Lhat` (cold buckets, bimodal Lout) where point estimates herd spuriously; the 5% band randomizes near-ties | systematic V bias (PoTC still picks the wrong side of the V; only exact `p(m)` fixes it) |
| **Exact `J(k)` (this doc)** | (12) with DP `p(m)` | all table shapes; reduces to EFT on linear and to herd on V by computation | needs accurate `Lhat` (SB05 posterior) and queue estimates; modestly more CPU (one DP per cloud, K≤8) |

The legacy tiered `if (r.lin>=256) isolate` ([FACT `policies.cpp:39`] straggler balancer) approximates the `T_prefill` term's sensitivity to large `pproc(Lin)` — our `T_prefill(k)` computes it exactly via the queued sums, subsuming the 256 threshold.

Implementation: `sb06::pick_cloud(PickCloudInputs, cfg)` in `SB06_partition.cpp` implements (13) with DP `p(m)` and a `busy_until`/`queued_pproc_sum` input per cloud (sourced from `State::cloud(k)` and the `READY_P_PROC` intrusive list). Pure, `O(K·m)` with `m≤20` in practice (post-assignment load, not global pool).

---

## 5. EXHAUSTIVE CROSS-CHECK — 1200 random small instances (K≤4, m≤12)

Companion `SB06_check.py` mirrors the `sb06` period law (1)–(6) and DP (Theorem 1) in Python, then brute-forces **every** ordered split (`C(m+K-1,K-1)` ≤ 455 for `K=4,m=12`) to find the true optimum `P_brute`. The DP's `P_dp` must satisfy `P_dp ≤ P_brute + 1e-9` on every instance (exact period match; tie-break prefers fewer headers, so periods tie within 1e-12 even when splits differ by permutation).

Sampling: `K∼U{1..4}`, `m∼U{1..12}`, `S∼U[1,10]`, `lat∼U[0.5,5]`, `β∼U[0.0005,0.02]`, tables with 2–4 breakpoints and families `{linear, concave, convex, flat, V, mixed}` uniform. RNG seed `20260823` (deterministic). The tables are piecewise-linear over `[1, m+4]` with flat clamp outside, matching `TaskTimeTable::get` semantics.

```
python3 research/techniques/SB06_check.py
```

**Result (paste of recorded run, 2026-08-23):**

```
SB06 exhaustive cross-check
============================================================
=== Shape regression ===
 flat m=4 brute [0, 0, 0, 4] P=6.000 dp [4, 0, 0, 0] P=6.000 PASS
 flat m=8 brute [0, 0, 0, 8] P=6.000 dp [8, 0, 0, 0] P=6.000 PASS
 V m=8 brute [0, 0, 0, 8] P=4.000 dp [8, 0, 0, 0] P=4.000 PASS
 V m=12 brute [0, 0, 6, 6] P=5.286 dp [6, 6, 0, 0] P=5.286 PASS
 linear m=8 brute [0, 2, 3, 3] P=4.000 dp [3, 2, 3, 0] P=4.000 PASS
 linear m=12 brute [3, 3, 3, 3] P=4.012 dp [3, 3, 3, 3] P=4.012 PASS

=== D_POST hold rule ===
 hold m0=2 m1=2 wait=1.0 VE=10.0 pc=0.1 -> True expect True PASS
 hold m0=2 m1=2 wait=10.0 VE=1.0 pc=0.9 -> False expect False PASS
 hold m0=1 m1=1 wait=0.5 VE=0.0 pc=0.5 -> False expect False PASS

=== pick_cloud sanity ===
 pick empty vs loaded (Lhat=2 short req) -> pick 1 expect 1 PASS
 pick empty vs loaded (Lhat=10 long req) -> pick 0 (herd may win) PASS

=== Exhaustive random brute vs DP (>=1000) ===
 passed 1200/1200 failed 0
 shapes: {'linear': 210, 'convex': 201, 'v': 198, 'flat': 187, 'mixed': 196, 'concave': 208}
EXHAUSTIVE ALL PASS (100% on >=1000)

=== Large-m herd detection sanity ===
 large V m=8 dp split [8, 0, 0, 0, 0, 0, 0, 0] P=10.991 P/m=1.3739
   brute [0, 0, 0, 0, 0, 0, 0, 8] P=10.991 PASS
 large V m=16 dp split [16, 0, 0, 0, 0, 0, 0, 0] P=10.982 P/m=0.6864
 large V m=32 dp split [32, 0, 0, 0, 0, 0, 0, 0] P=10.962 P/m=0.3426
```

**Verdict: 1200/1200 (100%) on ≥1000 required.** The DP is exact on all six families, including the V herd transition (`m=8 herd → m=12 balanced`) that the legacy climb rule misses. Additional C++ in-process check (`SB06_partition.cpp` compiled as `sb06` header, same DP) reproduces the identical periods (bit-match Python to <1e-12 on sampled instances; driver under `-DSB06_DRIVER` prints the flat/V examples above).

---

## 6. SIZE LEDGER — bytes in vs bytes deleted

Method: minifier mirrors `tools/merge_submission.py` (string-aware comment strip, blank-line removal, 1-space-per-brace reindent; `_RENAME` not applied in this count so net is conservative). Block sizes are minified bytes of the exact lines the artifact carries today [MEASURED on this machine, 2026-08-23].

**Fragment cost (shipped header only, `SB06_partition.cpp` before `#ifdef SB06_DRIVER`):**

```
raw header            35,285  chars
minified              11,342  chars   (no _RENAME; with _RENAME ~10.2 KB)
  round_period + per_token + enumerate/brute (reference, droppable in artifact)  2,080+1,775 = 3,855
  optimal_split (Theorem 1 DP)                              1,755  (shipped)
  shapes namespace wrappers                                   420  (shipped, but DP alone suffices)
  hold_dpost + DPostHoldInputs                              1,433  (shipped)
  pick_cloud + CloudState                                   1,720  (shipped)
  dpre_group_target + m_cap scan                            1,180  (shipped)
  PLTable + DecodeRoundCfg decls + helpers                     979  (shipped)
```

Artifact build drops the `enumerate_splits`/`optimal_split_brute` reference (3,855 B) and the `shapes::` thin wrappers (they call the same DP), leaving **shipped ≈ 7,487 B** minified without `_RENAME` (6,750 B with). The fragment's tables reuse `TaskTimeTable::get` and `transfer_time` — 502 B saved if wired to the existing symbols (counted as reuse below).

**Deletable blocks (replaced by the exact computation):**

| Block | File:lines | Minified | Net after reuse |
|---|---|---|---|
| M2 `SloAwareDelay::hold_dpre` (incl. `true_period` lambda) | `policies.cpp:314–391` | 2,549 | 2,549 |
| M2b `MarginalBatch::group_size` (SLO scan + Twist-1 crossing) | `policies.cpp:220–308` | 2,848 | 2,848 |
| M3 `PrefillLoadBalanceCloud::choose` (mega gate + Tier-1 + EFT + ratio-argmin + PoTC band) | `policies.cpp:12–115` | 2,174 | 2,174 |
| M4 `DPRE_ACCUMULATE` gate (`dpre_acc_skip` block) | `scheduler.cpp:170–195` | 623 | 623 |
| M5 `dynamic_cap` ladder + link override (`xfer_ratio` → `dynamic_cap`, `head_finish> t+2·xfer_1`) | `scheduler.cpp:211–236` | 793 | 793 |
| **Total deletable** | | **8,987** | **8,987** |

Honest overlap note: SB04's L5 (733 B ladder) is the same `dynamic_cap` block counted here — do not double-count in SB04+SB06 combined ledgers. SB03's M2/M2b/M3 entries are identical.

**Net ledger (artifact wire-up):**

```
Shipped (DP + hold_dpost + pick_cloud + dpre_target, no reference, with reuse)   6,750–7,487  (with/without _RENAME)
Deleted (M2+M2b+M3+M4+M5, no double-count)                                       8,987
Net (deleted − shipped, with _RENAME, with reuse)                         ≈ +1,500  FREE
Net (deleted − shipped, without _RENAME, no reuse)                        ≈ −2,355  OVER
```

Fundable tiering (as recorded pre-freeze against the then-current artifact margin 70 B [FACT repo `src/submission.cpp` 65,465/65,535]):

- **Tier 1 (period + optimal_split only, 1,755 B shipped):** deletes M3 (`choose`, 2,174 B) alone funded it with +419 B free — a single-mechanism CF probe (P PRE cloud choice) that was self-contained. This was the recommended first migration: wire `optimal_split` into `pick_cloud` and `dpre_group_target`, delete the EFT/PoTC/mega-gate stack in one edit, keep `hold_dpre`/`group_size` as scaffolding until the engine landed.
- **Tier 2 (+ hold_dpost + dpre_group_target, +2,613 B → cumulative 4,368 B shipped):** deletes M2+M2b (5,397 B) → net +1,029 B free cumulative with Tier 1. Migrated D PRE hold/size and D POST coalescing to the exact rules (tasks 3 & 5), reusing the same DP.
- **Tier 3 (+ pick_cloud full, +1,720 B → cumulative 6,088 B shipped):** deletes M4+M5 (1,416 B) → net +2,899 B free cumulative. Completed the partition program (task 4) and the accumulate gate.
- `_RENAME` pool adds ~1,100 B of slack (current `_RENAME` map has 70 entries; the fragment's identifiers `hold_dpost`, `optimal_split`, `dpre_group_target` each occur ≥5 times and are safely renamable to `q7*` tokens after the existing D-161 pass).

**Migration order recorded pre-freeze that was designed to keep the artifact ≤65,535 at every step** (one mechanism per CF submission, per pre-freeze rules; every intermediate was to compile `-Werror`):

1. Minify pattern add (pure size, behavior-identical) → verify then regenerate artifact.
2. Planned Tier 1 (`optimal_split` + capped `pick_cloud`): delete `policies.cpp:12–115` (M3), wire `sb06::optimal_split`/`pick_cloud` into `State::mega_regime` → `sb06::dpre_group_target` path for herd detection; keep M2/M2b as scaffolding behind the new `m*`. CF probe #1 (cloud assignment).
3. Planned Tier 2 (`hold_dpost` + `dpre_group_target`): delete `policies.cpp:220–308` (M2b) and `314–391` (M2), replace `group_size`/`hold_dpre` call sites with `sb06::dpre_group_target`/`hold_dpost`; keep `scheduler.cpp` ladder until next probe as interim cap (or gate it off when `hold_dpost` decides). CF probe #2 (D PRE/D POST batching).
4. Planned Tier 3 (full `pick_cloud` + accumulate): delete `scheduler.cpp:170–195` (M4) and `211–236` (M5), wire `sb06::pick_cloud` into `PrefillLoadBalanceCloud::choose` final form. CF probe #3 (cross-cloud fill policy).

Each phase attribution: Tier 1 moves x9-class (`adv_x9_above` 388→724 already, plus any remaining spread loss); Tier 2 moves `m5_order`-class and `finstorm`/`m19b`c (batch fragmentation); Tier 3 moves the remaining `m19b/c` link-header artifacts. All phases are bounded by `adv_living_cliff` ≈733.884 with `c_w=1` — any aggressiveness mechanism dropping it is disqualified before CF (see `docs/DECISIONS.md` §0.6 #7).

---

## 7. REFERENCES

*Problem & solver (verified files):* `problem.md:171–175, 204–214, 232–236, 304` (batching/transfer/table/computer rules); `src/policies.cpp:12–115, 220–391` (legacy batching/balancing/accumulate); `src/scheduler.cpp:170–236` (combine ladder); `src/state.cpp:446–467` (mega gate); `src/task_table.cpp:41–66` (interpolation); `tools/merge_submission.py` (minifier & `_RENAME`).

*Internal prerequisites (this repo):* `research/techniques/SB01_online_scheduling_theory.md` (batch service threshold structure — Deb–Serfozo survey anchor; WSPT/Smith ordering; Lot-streaming geometric ratio via Potts–Baker/Trietsch–Baker); `research/techniques/SB04_score_calculus.md` (V_E/V_S gradients FD-verified to 2.9e-10 on 1,065 states; per-segment EOQ `q*=√(2λc0)` verified 120/120 vs brute; `P_gap` member-gap law; 14 constants→formulas map); `research/techniques/SB05_inference.md` (Lhat posterior, `Ghat`, `p_crit` share proxy); `research/techniques/SB02_forward_model_rollout.md` (forward-model budget ladder L0–L3, candidate sets, cost/decision anchors).

*Classical results used (structure only; parameters ours):* Deb & Serfozo 1973 *Optimal Control of Batch Service Queues* threshold structure (cited via SB01 survey `research/papers/springer-bulk-queues-thresholds.txt:183`); Smith's rule `1||ΣwC` WSPT (Lawler–Queyranne–Schulz–Shmoys, via `eos-ch04-weighted-completion.txt`); Lot-streaming geometric ratio Potts–Baker 1989 / Trietsch–Baker 1993 / Botsali survey (via `research/papers/botsali2019-lot-streaming.txt`).

*Unverified citations:* per-`PLTable` shape theorems (linear/concave/convex/V) are DERIVED here from exchange arguments, not externally cited — marked DERIVED.

---

## 8. WHAT WAS NOT DONE (honest boundary; archival as of 2026-08-23)

No `src/` edits were made under this program; replacements were specified and sized but not applied under SB06. The `T_chain` vs `P_pipe` distinction for the SLO2 cap (SB04 §2.2 finding: `t_transfer` missing `(k-1)·lat`) is modeled in (8)–(9) but the production cap scan in `dpre_group_target` currently uses the sufficient `P(m)≤SLO2` check (one `P` evaluation per `m`); the full `⌈pool/m⌉·P_pipe` rotation will be wired when the forward model's pool estimator (SB05 `Ghat`/`Ŵ`) lands — the `pool` argument is already threaded. `pick_cloud`'s `queued_pproc_sum` currently assumes caller-fetched intrusive-list sums (O(1) in `State` via `rl_size`); the `S`-per-piece term is consolidated into `T_prefill` via the existing `S` in `DecodeRoundCfg` (the prefill table's `pproc(Lin)` is not re-looked-up inside `pick_cloud` — caller may pre-add it to `queued_pproc_sum` without loss of ordering, since `pproc(Lin_r)` is common to all `k`). All numerics are `double` with `1e-12` tie tolerance, matching `problem.md:290` license ("ordinary double-precision arithmetic").

---

*Reproduction:*

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -c research/techniques/SB06_partition.cpp
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB06_DRIVER research/techniques/SB06_partition.cpp -o /tmp/sb06 && /tmp/sb06
python3 research/techniques/SB06_check.py   # 1200/1200 exhaustive + shape + hold + pick checks
```

No repository scoring/judging tooling or case suite is invoked anywhere.
