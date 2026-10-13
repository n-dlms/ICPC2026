# SB04 — Runtime Score Calculus: Exact Marginals, Oracle Audit, Constants→Formulas

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB04). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
Scope (archival): math + self-written tests only; no local-score evidence, no repo
scoring/judging tooling, no source edits.

Artifacts:

- `research/techniques/SB04_score_calculus.cpp` — the drop-in fragment
  (`namespace sb04`, header-style) + verification driver under `-DSB04_DRIVER`.

- `research/techniques/SB04_check.py` — builds the fragment with the strict
  flag set, verifies EVERY formula against brute-force finite differences /
  discrete differences of the exact score on 1,065 randomized states plus 120
  random-table batch-law checks, and prints the minified-size ledger.

Claim tags: **[FACT]** = problem.md (line cited); **[FD-PROVEN]** = verified by
SB04_check.py against finite/discrete differences of the exact score
(numbers below); **[DERIVED]** = algebra from [FACT]s; **[APPROX]/[WRONG]** =
audit classifications of the current `src/` code (read-only; no edits made).

Score function [FACT problem.md:425-495], the object everything below differentiates:

```text
Score = 1000 * (w_tp * TP + w_c * W), w_tp + w_c = 1 (either may be 0)
```

```text
tp = N / E, N = sum(Lout), E = (last final-token time) - (earliest ARR)
```

```text
TP = clamp((tp - tp_base) / (tp_UB - tp_base), 0, 1)
```

```text
tdr = (1/R) * sum(ppost_i - arr_i) (pooled mean over ALL requests)
```

```text
tpot = sum(e_last - e_first) / G, G = sum(Lout - 1) (tpot = 0 iff G == 0)
```

```text
e1 = max(0, (tdr - SLO1) / SLO1), e2 = max(0, (tpot - SLO2) / SLO2), dist = sqrt(e1^2 + e2^2)
```

```text
W = dist_base > 0 ? max(0, 1 - dist / dist_base) : (dist == 0 ? 1 : 0)
```

```text
Ranges: SLO1,SLO2 in [1e-3,1e9]; 0 <= tp_base,dist_base <= 1e9; tp_UB > tp_base; R <= 2000; sum(Lout) <= 2e5; K <= 8; S in [1,10]
```

- `N`, `E`, `R`, `G` — pooled totals; `TP` — clamped throughput; `W` — waiting component.
- [FACT :279] for ranges.

Everything is evaluated at **projected finals** (N̂, Ê, R̂, tdr̂, tpot̂, Ĝ).
Mid-test these are estimates (running oracle + remaining-work model); the
FORMULAS are exact, the projection quality is the caller's. This distinction
drives several audit findings below (the current code evaluates at running
values).

---

## 1. THE CALCULUS (Task 1 — full algebra)

### 1.1 V_E — points per ms of last-final-token reduction

E = T_last − A_first with A_first immutable, so dE = dT_last. tp = N/E gives
dtp/dE = −N/E². In the linear region tp_base < tp < tp_UB:

```
∂Score/∂E = 1000·w_tp·(1/(tp_UB−tp_base))·(−N/E²)
V_E ≡ −∂Score/∂E = 1000·w_tp·N / ((tp_UB−tp_base)·E²)          … verify ✔ above
```

Clamped forms (both boundaries EXACT, one-sided semantics matter):

- **tp < tp_base**: TP clamped at 0, dTP/dtp = 0 ⇒ **V_E = 0**.
- **tp = tp_base exactly**: reductions move INTO the ramp; the one-sided
  (increase-direction) derivative is the **full value** (checked: one-sided
  FD at the kink equals V_E to <1e-9). The convention is `tp ≥ tp_base`.

- **tp ≥ tp_UB**: TP clamped at 1 ⇒ **V_E = 0** (also at tp = tp_UB exactly:
  further increase pays nothing).

Equivalently V_E = V̄_tp·N/E² with V̄_tp = 1000·w_tp/(tp_UB−tp_base) the
tp-slope (the quantity `Oracle::marginal_tp_v` actually returns). **V_E ∝ 1/E²
falls as E grows**: per-ms value is highest on tight tests, and late-test ms
are worth LESS in absolute points, not more — SB01 §4.1(3) states the opposite
direction ("near the end each time unit on the critical request is worth
more"); correction recorded in §7. The endgame matters because it is the only
remaining opportunity (and the max-coupling concentrates there), not because
the rate rises.

### 1.2 V_P — points per ms of ONE request's P POST reduction

Writing pᵢ = ppostᵢ − arrᵢ: ∂tdr/∂pᵢ = 1/R (R = final request count).
For dist_base > 0, inside the wait ramp (0 < dist < dist_base):

```
∂dist/∂tdr = (ε₁/dist)·(1/SLO1)   [requires ε₁ > 0; = 0 when ε₁ = 0 ∧ ε₂ > 0]
∂W/∂tdr = −(1/dist_base)·∂dist/∂tdr
V_P(mean) ≡ −∂Score/∂tdr = 1000·w_c·ε₁/(dist_base·dist·SLO1)
V_P ≡ V_P(mean)/R = 1000·w_c·ε₁/(dist_base·dist·SLO1·R)   … per request ✔
```

Region catalogue (all FD-verified, including the exact zeros):

- **ε₁ > 0, dist < dist_base**: interior rate above (equality at
  dist = dist_base included for the recovery direction).

- **ε₁ = 0 ∧ ε₂ > 0** (tdr within SLO, tpot breached): the norm gradient
  points along ε₂ only ⇒ **V_P = 0 exactly** — tdr reductions buy nothing
  until tdr itself crosses SLO1. ✔ exact-zero checks.

- **dist ≥ dist_base** (W clamped 0): **V_P = 0** until a finite recovery
  move crosses back under dist_base. ✔.

- **dist = 0** (both SLOs met): `dist = 0` is a REGION (`tdr ≤ SLO1` ∧ `tpot ≤ SLO2`), not a point. Inside it (`tdr < SLO1`) the infinitesimal cost of delay is **0** (slack absorbs).

  ON the knife edge `tdr = SLO1` (`tpot ≤ SLO2`) the one-sided cost of an increase is the full rate:

```text
V_P_up(knife) = 1000 * w_c / (dist_base * SLO1 * R)
```

  - `V_P_up(knife)` — one-sided up-rate at the knife edge.
  - FD at the knife: the response is exactly linear in `h` out to `h = dist_base*SLO1`, since `dist = h/SLO1` there — verified to 2.4e-12.
- **dist_base = 0 (cliff)**: W is a step, not a ramp — there is NO gradient.
  The mean-crossing condition (exact): delaying request i by δ flips W 1→0
  iff `SLO1 − tdr < δ/R` while tpot ≤ SLO2 (both excesses must be 0 for
  W = 1). Recovery of W from 0→1 by reducing ONE ppost by δ happens iff
  `tdr − SLO1 < δ/R ≤ tdr − SLO1 + (anything)`, i.e. the reduction drives the
  mean to ≤ SLO1, AND tpot ≤ SLO2 already. When the other axis is also in
  excess, NO single-channel move can rescue W — the marginal is 0/undefined
  (a step of height 1000·w_c), priced discretely by `cliffPpost(δ)`. ✔ exact
  discrete checks.

**R matters**: one request's ms enters as 1/R with R = FINAL request count —
including requests that have not arrived yet. Using completed-count
(count_tdr) instead of R̂ overstates per-request effects by R̂/count_tdr ≥ 1.

### 1.3 V_S — points per ms of ONE request's span reduction

tpot = Σⱼ spanⱼ / G with spanᵢ = e_lastᵢ − e_firstᵢ ⇒ **∂tpot/∂spanᵢ = 1/G
for every i** — the same for all requests:

```
V_S(mean) = 1000·w_c·ε₂/(dist_base·dist·SLO2)      [interior, G > 0]
V_S = V_S(mean)/G                                   … per request-span ms ✔
```

The (Loutᵢ−1)/G weight appears at the NEXT level of structure — pricing a
**per-gap stretch** (all remaining gaps of request i each by δ, e.g. a slower
round cadence): Δtpot = (Loutᵢ−1−gaps_doneᵢ)·δ/G, so the coefficient is
`stretchCoef(iter, lout_hat) = max(0, lout_hat − iter)/G` [DERIVED; the
posterior-expected form uses E[Lout|Lin-bucket] for the hidden remainder].
For the NEXT single token of request i (the common decision atom):

- iter ≥ 1: the delay opens one new gap or moves e_last — coefficient exactly
  `1/G` (certain: a pool member always has another token coming).

- iter = 0: the delayed token is e_first; delaying it SHRINKS span by δ —
  a GAIN — but only if Loutᵢ ≥ 2 (Lout = 1 ⇒ no gaps, span ≡ 0): expected
  coefficient `p_ge2·1/G` with p_ge2 = P(Lout ≥ 2 | bucket). This is the
  exact form of the L4 dead-zone parking gain: `parkCoef = p_ge2/G`. ✔ mode-D
  checks.
G = 0 (all Lout = 1): tpot ≡ 0 by definition ⇒ V_S ≡ 0 and the entire tpot
channel is void (SLO2 irrelevant) — the exact detector is `G == 0`, not an
Lout EWMA threshold. Knife-edge and cliff forms mirror §1.2 with SLO2/G.

### 1.4 Exchange rates and the composite action marginal

Per ms of the pooled means (interior):

```text
d(dist)/dtdr = e1/(dist*SLO1), d(dist)/dtpot = e2/(dist*SLO2)
```

- `e1`, `e2` — excesses; `dist` — norm.

Along the iso-dist circle `e1*de1 + e2*de2 = 0` ⇒ in ms units:

```text
d_tpot = -(e1*SLO2)/(e2*SLO1) * d_tdr
```

- Keeps `dist` constant to `O(d^2)` (the probe's residual shrinks exactly 4x when the step halves — the measured drift ratio is 0.250).

Sacrifice rates per single-request ms:

```text
V_P/V_E = w_c*e1*(tp_UB-tp_base)*E^2 / (w_tp*N*dist_base*dist*SLO1*R)
```

```text
V_P/V_S = e1*SLO2*G / (e2*SLO1*R)
```

- `V_P` carries `1/R`; `V_E` does not — for large `R` the tp channel dominates any bounded per-request sacrifice, and when `e1 = 0` (final tdr within SLO) sacrifice through tdr is FREE up to the slack (finite moves priced by `delayCostPpost` below).

**Composite marginal.** An action that simultaneously reduces E by dE, ONE
ppost by dP and ONE span by dS (all ≥ 0) is worth `gain = V_E·dE + V_P·dP +
V_S·dS` to first order; delays are priced with the one-sided up-rates
(V_E, V_P↑, V_S↑). For FINITE delays the exact (kink-free) objects are

```
delayCostPpost(n, δ) = 1000·w_c·[W(dist) − W(dist((tdr+nδ/R−SLO1)/SLO1, ε₂))]
delayCostSpan(n, δ)  = 1000·w_c·[W(dist) − W(dist(ε₁, (tpot+nδ/G−SLO2)/SLO2))]
```

because the pooled means move by EXACTLY nδ/R and nδ/G — verified as exact
discrete score differences in every region (mirrored-expression bitwise
equality + score-difference equality < 1.2e-11 where conditioning allows).
These finite forms are what hold/admission decisions should use: they price
crossing SLO1 out of the slack, which the infinitesimal rates cannot see.

**Interior tokens carry exactly zero direct value.** Moving token j of
request i (1 < j < Loutᵢ, not the global last token) by Δ: gap_{j−1} += Δ and
gap_j −= Δ, so Σgaps (and spanᵢ = Σgapsᵢ) is unchanged — tpot is a function
of the gap SUM only; tdr is unaffected (P POST precedes all tokens); tp and E
are unaffected (N unchanged; T_last moves only if the token IS the global
last). ⇒ ΔScore = 0. Direct value zero; indirect value only through resource
release. (This is the exact statement behind law L3/L4.)

### 1.5 The critical request (max-coupling of E)

E = maxᵢ Fᵢ with Fᵢ the request's final-token time. Advancing the
projected-last request i\* by dAdv lowers E by min(dAdv, F\*−F₂) (F₂ = max of
the others); advancing any other request lowers E by 0:
`dE_effect(dAdv, is_star, F*, F2)` ✔. Running estimate of i\*:
re-project every frame
`F̂ᵢ = now + [remaining chain: solo prefill remainder + UP/DOWN FIFO tails + cloud
queue] + (L̂outᵢ − iterᵢ)·P_gap(pool)` for decode-phase members (analogously
plus the first-round chain for prefill-phase requests), take argmax. V_E
attaches ONLY to i\*'s actions (and to moves that change the argmax);
everyone else keeps the additive V_P/V_S gradients. F₂ is the floor of any
E-reduction: no schedule beats finishing the second-last earlier than F₂.

---

## 2. AUDIT of the current Oracle and its call sites (Task 2)

Classifications: **EXACT** (matches the calculus for what it computes),
**APPROX** (right shape, wrong scale/inputs), **WRONG** (wrong formula/region/
units). File:line from the working tree read 2026-08-23.

### 2.1 Oracle members (src/state.hpp:234-313)

| item | file:line | class | finding / exact replacement |
|---|---|---|---|
| projected_tdr | state.hpp:244 | EXACT as running mean / **APPROX as projection** | mean over COMPLETED P_POSTs (survivorship-biased). Final-tdr estimator: tdr̂ = (sum_tdr + Σ_inflight(now + chain_rem − arr)) / R̂. |
| projected_tpot | state.hpp:245 | EXACT / APPROX | same structure; also only realized gaps. |
| projected_tp | state.hpp:246-249 | EXACT / APPROX | tokens_out/(last_finish−earliest); running N and E both under-estimate the finals; use N̂/Ê. |
| excess_tdr / excess_tpot | state.hpp:250-257 | **EXACT** | clamp forms of the running values. |
| excess_dist | state.hpp:258-262 | **EXACT** | |
| projected_cw_exact | state.hpp:263-267 | **EXACT** | incl. dist_base=0 binary. |
| normalized_tp | state.hpp:269-272 | **EXACT** | |
| projected_score | state.hpp:273-278 | **EXACT** | (of running values). |
| marginal_tp_v | state.hpp:283-287 | **EXACT as ∂Score/∂tp** | = V̄_tp, constant in the linear region. NOT V_E: callers must multiply by N/E². Kink: `tp <= tp_base → 0` kills the value exactly AT tp_base (one-sided full). |
| marginal_tdr_v | state.hpp:288-294 | **EXACT interior; WRONG at two edges** | interior = V_P(mean) exactly. (a) at dist==0 with tdr==SLO1 (knife edge) returns 0 but the one-sided delay cost is 1000·w_c/(dist_base·SLO1) per mean-ms; (b) inside the dist==0 slack correctly 0 — but finite crossing moves are unpriced (need delayCostPpost). `d >= dist_base → 0` also excludes the boundary recovery direction. |
| marginal_tpot_v | state.hpp:295-301 | same as above with SLO2 | same two edges. |
| rem_tp / rem_c | state.hpp:305-312 | EXACT (dead in decisions) | no decision reads them. |

### 2.2 Call sites

| site | file:line | class | finding |
|---|---|---|---|
| group_size crossing, V_tp side | policies.cpp:274,286 | **APPROX** | V̄_tp·Δ(steady-state rate) omits the remaining-work factor dScore/dr = V̄_tp·N·T_rem/E² (E = τ + T_rem/r): overstates the tp side by up to N·T_rem/E²; ranking vs the tpot side is skewed (the tpot side has its own dilution error — they partially cancel, accidentally). |
| group_size V_tpot, K=1 fallback | policies.cpp:277 | **WRONG (units)** | `w_c/(SLO2·max(dist_base,1.0))` is 1000× smaller than marginal_tpot_v's scale AND drops the ε₂/dist direction cosine (same value whether tpot dominates dist or is negligible). Exact: identical formula for K=1 — marginal_tpot_v(cg.w_c, cg.dist_base, cg.SLO1, cg.SLO2). |
| group_size cliff fallback | policies.cpp:279-280 | **WRONG (region)** | sets V_tpot = 1000·w_c whenever dist_base ≤ 0 ∧ ε₂ > 0. On the cliff W recovers only if BOTH excesses hit 0: with ε₁ > 0 the tpot cap can never rescue W — the jump value is 0. Exact: `V_tpot_jump = (e1==0) ? 1000·w_c : 0` (and only while tpot can still reach ≤ SLO2). |
| group_size delta_tpot | policies.cpp:283-285 | **APPROX** | treats the future round period as the pooled mean directly: Δtpot = ΔP·(future gap share), share = remaining_gaps/G, not 1. |
| group_size tdr_loss | policies.cpp:288-299 | **APPROX/WRONG denominator** | V_tdr·dt_local·pipe/count_tdr: divides by COMPLETED count, not final R̂ (overstates by R̂/count_tdr; unbounded early). |
| group_size cost(m) transfer leg | policies.cpp:240 | **APPROX** | transfer_time(cfg, m) = ℓ+βm — one transfer. A k-cloud round's UP leg is k FIFO entries: kℓ+βm; understates by (k−1)ℓ. |
| group_size cost(m) = P_pipe | policies.cpp:234-242 | **WRONG MODEL for the SLO2 constraint** | max(2S+dpre+dpost, S+dproc(⌈m/K⌉), xfer) is the RESOURCE-BOTTLENECK (pipelined) period — the right object for THROUGHPUT/E. The TPOT constraint binds on the MEMBER gap = chain/rotation: P_gap(m,pool) = max(P_chain(m), ⌈pool/m⌉·P_pipe(m)) with P_chain = 3S+dpre+dproc(⌈m/K⌉)+dpost + (kℓ+βm) + (ℓ+βm) (same-cohort rounds serialize: D_PRE(j+1) needs D_POST(j)). The cap can admit batches whose real member gaps breach SLO2. (D-119's +372 says the loosened cap paid on CF — via the tp-trade, not because the period model is right.) |
| hold_dpre tdr_critical 0.15 | policies.cpp:320-323 | APPROX const | see C-map L1. |
| hold_dpre gain | policies.cpp:372,378-383 | **APPROX (shape right, inputs running)** | gain = V̄_tp·(N/E)·ΔP/E = V_E·ΔP exactly — but with running N/E/makespan instead of projected finals ⇒ V_E over-evaluated (E small early). |
| hold_dpre loss E-term | policies.cpp:389 | **APPROX (max-coupling ignored)** | V_E·wait charges the wait against E unconditionally; if the held members are not the projected-last, pipeline slack may absorb it (overstates loss ⇒ conservative). Exact: V_E·dE_effect(wait, is_star, F*, F₂). |
| hold_dpre loss tpot-term | policies.cpp:373,389 | **APPROX + WRONG SIGN for the branch's own population** | V_tpot·(P(k+1)−P(k)) prices the merged batch's longer period as a per-member mean change — missing the k/G multiplicity. Moreover this branch only runs with iter==0 members (multi_token check at :318): for them the tpot effect of waiting is the parking GAIN +V_S·p_ge2·wait, not a loss. |
| hold_dpre tdr_loss | policies.cpp:385-387 | **WRONG channel** | charges V_tdr·wait·pipe for P_POSTs of pipeline requests — but the hold keeps the LOCAL FREE; their P POSTs are not delayed by the hold itself (if anything they run earlier). The real tdr cost comes later when the merged D_PRE occupies the local. |
| hold_dpre dist_base=0 branch | policies.cpp:329-341 | APPROX | structural (imminent==1) stand-in for the cliff jump calculus (§1.2): exact = hold iff ΔE-gain > 0 while the projected post-hold ε₁,ε₂ both stay 0 (else the 1000·w_c jump is at risk). |
| pick_local tdr_urgent | scheduler.cpp:111-124 | **APPROX — not the right finite difference** | `mv_tdr>0 && a_i>0` ⇒ always run P_POST first when the tdr marginal is live. Exact FD (§C2): P_POST first iff V_P↑·[(a_i+S) − (S+q_j)·κ] > 0, κ = P(the prefill's S+q_j chain delay reaches its own ppost) (0 idle → 1 backlogged). The code's NON-urgent branch (q_j < a_i && (!cloud_starved || link_busy), :126-132) IS a κ-form heuristic — tdr_urgent overrides it with κ=1 unconditionally. Scale note: the 1/R cancels in the comparison, so V_P's denominator error does not bite here. |
| V11 inflight constants | scheduler.cpp:93-104 | APPROX | see C-map L7/L10. |
| compute_alpha min_TDR | Solution.cpp:81-98 | APPROX (conservative) | floor uses transfer_time(cfg, 1.0); the true per-request floor uses transfer_time(cfg, Lin) (UP/DOWN len = Lin) — understates the floor ⇒ fewer flips. The traffic-aware cliff flip (:258-269) uses the real Lin per arrival (EXACT per-request feasibility), but "any feasible arrival cancels the flip" is weaker than the exact MEAN test: flip iff mean-of-floors(linᵢ) > SLO1. |
| compute_alpha X10 worst-case | Solution.cpp:111-127 | EXACT as sufficient bound | valid upper bound ⇒ correct pure-tp detection when it fires. |
| mega gate w_tp>0.8 | state.cpp:444 | APPROX | proxy for "tp channel dominates": exact = 1000·w_tp·rem_TP > 1000·w_c·rem_W at the current projection (rem_* already in the Oracle). Table-shape parts (d_last<d1 etc.) are not score-derived — irreducible detectors. |
| predict_lout rounding | state.cpp:643-648 | APPROX (minor) | bucket mean rounded to int before entering coefficient arithmetic; use the double mean (only the coefficient matters). |

**Three most consequential findings** (archival; recorded 2026-08-23):

1. **marginal_tdr_v/marginal_tpot_v return 0 on the dist==0 knife edge and
   price nothing for finite crossings out of the slack** — the exact
   one-sided cost at tdr==SLO1 is 1000·w_c/(dist_base·SLO1·R), and the exact
   finite object delayCostPpost(n,δ) is a closed form. Every hold/admission
   decision currently underprices tdr/tpot damage exactly when both SLOs are
   barely met — and hold_dpre:376 then holds unconditionally (its gain>loss
   is never reached).

2. **group_size's K=1 V_tpot fallback is 1000× off scale with no direction
   cosine, and the cliff fallback assigns the full 1000·w_c jump even when
   ε₁>0 makes recovery impossible** — the SLO2 crossing trade is mispriced on
   every K=1 test and every dist_base=0 test with tdr excess.

3. **The SLO2 period model is the pipelined throughput period, but the
   constraint binds on the member gap** = max(chain, ⌈pool/m⌉·P_pipe), and
   the transfer leg misses (k−1)·ℓ. The cap systematically admits batches
   whose real member gaps breach SLO2 (and misjudges the m_tp frontier on
   V-shaped tables, where P_gap is non-monotone and the max-feasible m is a
   crossing between breakpoints, not a breakpoint).

---

## 3. CONSTANTS → FORMULAS MAPPING (Task 3)

For each: the quantity the constant approximates, the exact replacement
(C++ forms are on ScoreCalc — ≤5 lines each; all inputs live-measurable),
or a proof the constant is not score-derived.

| # | constant (file:line) | approximates | exact replacement |
|---|---|---|---|
| L1 | `0.15·dist_base` tdr-critical, hold_dpre policies.cpp:321-322 | "ε₁ big enough that holds are dangerous" | dissolves into the hold test itself: hold iff V_E·ΔP + Σpark > V_E·dE_effect(wait) + delayCostPpost(n_block, δ_occ); i.e. delete the shortcut, run the exact inequality. (172 B) |
| L2 | `mv_tdr>0 && a_i>1e-9` tdr_urgent scheduler.cpp:118-120 | the P_POST-first finite difference | `runPpostFirst(a_i,q_j,S,κ) = V_P_up()·((a_i+S)−(S+q_j)·κ) > 0`, κ from cloud backlog = clamp((busy_until−t)/(S+q_j),0,1). (355 B) |
| L3 | `ewma_lout()<2.5` low-lout policies.cpp:246 | P(tpot structurally void) | `noGapsEver() = (Ĝ <= 0)` with Ĝ = done_gaps + Σ max(0, L̂outᵢ−iterᵢ): tpot ≡ 0 iff Ĝ = 0 — deterministic structure, not an EWMA threshold. (131 B incl. L4's first half) |
| L4 | `SLO1·0.8` round cap policies.cpp:247 | local-occupancy tdr budget | `tdrBudgetMs() = max(0, SLO1−tdr̂)·R̂` request-ms of slack; cap m where V_P↑·(2S+dpre(m)+dpost(m))·n_blocked = marginal occupancy cost vs V_E·ΔE amortization gain. |
| L5 | ladder `0.05/0.3/1/5` on xfer_1/decode_ref (+ `>2 && >t+2·xfer_1` override) scheduler.cpp:221-236 | break-evens of per-member amortization saving vs extra-UP-header link time | `admitCrossCloud(m) = σ_local(m) ≥ λ_up·(ℓ+β·m_c)` with σ_local(m) = [2S+dpre(m)+dpost(m)]·(1/m − 1/(m+1))⁻¹-shaped marginal and λ_up = UP shadow price (0 when UP idle, else the best queued alternative's value-rate). The ladder is a 4-level quantization of λ_up; the override is a crude λ_up>0 detector. (733 B) |
| L6 | `Lin ≥ 256` Tier-1 isolation policies.cpp:39-50 | "pproc(Lin) big enough to block co-located P POSTs" | `isolateStraggler(pproc(lin), n_co, φ, ΔE*) = V_P↑·pproc(lin)·n_co·φ > V_E·ΔE* + V_P↑·own_queue_delay`, φ = cloud-busy overlap fraction. Lin units are NOT score-derived (the table maps Lin→duration; the 256 is a table-shape proxy). (263 B) |
| L7 | `alpha<0.8` NoDelay split Solution.cpp:188-190 (+ alpha xremote forms :193-194) | "w_c ≤ 0.2 too small to matter" | dissolves: ONE hold policy using the exact test — with V_P=V_S=0 (w_c=0) it returns hold iff V_E-gain>0, reproducing NoDelay's behavior by computation. (216 B) |
| L8 | `0.5·dist_base` dist-headroom scheduler.cpp:96-97 | unprojected future dist growth allowance | `admitInflight(Δdist, ΔE) = waitLoss(Δdist) ≤ V_E·ΔE` with Δdist the admission's projected dist move (from the pipeline/rollout); waitLoss is exact in every region. The 0.5 = "assume Δdist ≤ 0.5·dist_base". |
| L9 | `1.05` PoTC band policies.cpp:108 | EFT estimator noise | `potcBand(dproc*, σ_lout) = dproc*·σ_lout` — the tie band is the EFT estimate's standard deviation (lout-bucket dispersion), not a score constant. Partially irreducible (estimator noise, not score). (247 B) |
| L10 | `max(2,K)`, `100` (worst_up_lin<1), `2+2K`, `/2+1` inflight bases scheduler.cpp:93-104 | pipeline window keeping clouds+link busy | `inflightAstar(P_round) = max(1, (P_round − K·ℓ)/β)` tokens (UP-direction saturation window); admission per-decision via L8's test. The constants are quantized stand-ins with no per-test scaling. |
| L11 | `(S+2ℓ+dur)/m` precompute argmin policies.cpp:205-215 | per-member round cost | **already exact** for its objective (amortization argmin over listed rows; the climb rule needs convexity — ratio-argmin (D-161 A3) is the exact form on V-tables). Irreducible-as-is. |
| L12 | `local_avg = 2S+dpre(1)+dpost(1)` chunk budget policies.cpp:173 | decode round period (stall budget G) | `k = ceil(pproc(Lin)/G)` with G = θ·max(0, SLO2 − P_gap) from the exact period law (§4); local_avg ≈ P_chain(1). |
| L13 | `up.tail > t+1.0` link_busy scheduler.cpp:130 | "UP has real backlog" | `> t + transfer_time(cfg,1)` (one minimum transfer) — or exact: `up_.tail > t`. |
| L14 | `1e-9/1e300/1<<20` sentinels | numerical/table-exhaust | irreducible (not score-derived). |

Byte counts in parentheses = minified bytes of the block the exact form
replaces (ledger §6).

---

## 4. EOQ / BATCH CLOSED FORMS (Task 4)

Inputs, all live-measurable: ready-member arrival rate `lambda_hat` (EWMA of `P_POST` completions + `D_POST` recycling), the chain cost `f(m) = P_chain(m)` (§2.2 cost finding — now with the `k*l` transfer leg), its piecewise-linear segments (`c0_j`, `c1_j`) between table breakpoints, pool size, `SLO2`, and `P_gap`.

**Thin pool (resources idle, Deb–Serfozo threshold).** Per-member waiting cost:

```text
W(q) = q/(2*lambda) + f(q)/q
```

- `W(q)` — mean fill wait + amortized overhead.

On a segment `f = c0 + c1*q`:

```text
dW/dq = 1/(2*lambda) - (f - q*f')/q^2 = 1/(2*lambda) - c0/q^2 = 0
```

```text
q* = sqrt(2*lambda*c0) per segment
```

- `q*` — EOQ law holds exactly per segment; the global optimum is the min over segments of `[q/(2*lambda)+c0/q+c1]` at `q_s = clamp(sqrt(2*lambda*c0), m1, m2)` [DERIVED; verified: `W(qThin)` = dense-scan continuous min on 120/120 random tables, worst excess 0.0].
- Serve (issue) when `pool >= q*`; hold only while all resources hold better work (work-conserving override).
**Threshold-structure citation, verified against the literature**:
Deb & Serfozo 1973, "Optimal control of batch service queues", Adv. Appl.
Probab. 5(2):340-361 — "it is optimal to start serving customers only when
the number of customers in queue exceeds some threshold a" (quoted in the
downloaded survey research/papers/springer-bulk-queues-thresholds.txt:183-184,
with the minimal-batch-service form at :238-246 and the full reference at
:1687). The q* PARAMETERS are the fluid derivation above (mine); only the
threshold STRUCTURE is Deb–Serfozo's theorem.

**Critical load.** A round of `q` serves `q` members in `f(q)`: stability demands `lambda*f(q)/q <= 1`.

```text
q_crit = lambda*f0/(1 - lambda*a), needs lambda*a < 1
```

- `q_crit` — critical batch; identity `lambda*(f0+a*q_crit)/q_crit = 1` verified to <1e-9 on every finite case [DERIVED].
- Below `q_crit` the pool grows without bound — the batch must at least clear the arrival rate.

**Saturation.** `lambda*a >= 1` (unstable alone) or pool persistently `>= q*`: batch to the pool cap, bounded by tpot feasibility:

```text
m_tp = max{m <= min(mmax, pool) : P_gap(m, pool) <= SLO2}
```

```text
P_gap(m, pool) = max(P_chain(m), ceil(pool/m)*P_pipe(m))
```

- `P_gap` — member-gap law (§2.2 finding 3).
- `P_gap` is non-monotone on V-shaped tables and the max feasible `m` is a threshold crossing between breakpoints: exact = full integer scan once at startup (≤4096 lookups, cached like `MarginalBatch::precompute`); the `O(rows*log)` production form binary-searches the right crossing per piecewise-linear segment (max of linears is convex within a segment). Verified: candidate/scan == brute force 120/120.

**Regime detector** (inequalities on `lambda_hat`, `f0`, `a`):

```text
rho(q) = lambda_hat * f(q) / q at the thin optimum q*
```

- `rho(q*) < 1` → **thin**: `q = clamp(q*, 1, pool)`.
- `rho(q*) >= 1` and `lambda_hat*a < 1` → **critical**: `q = clamp(max(q*, q_crit), 1, pool)`.
- ρ(q*) < 1 → **thin**: q = clamp(q*, 1, pool);
- ρ(q*) ≥ 1 ∧ λ̂·a < 1 → **critical**: q = clamp(max(q*, q_crit), 1, pool);
- λ̂·a ≥ 1 (or pool binds) → **saturated**: q = min(pool, m_tp) (m_tp = 0 ⇒
  SLO2 unreachable even at m=1 ⇒ pool cap; SLO dead).
Verified 120/120 against an independent Python re-derivation.

---

## 5. VERIFICATION SUMMARY (Task 5)

`python3 research/techniques/SB04_check.py` (builds with
`g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB04_DRIVER` — clean):

```
== SB04 FD verification: 1065 randomized states (seed 20260823) ==
formula                     max rel err   in-region n / exact-0 n
V_E                           2.634e-10
V_P                           2.944e-10
V_P/V_E closed                4.878e-16
V_P/V_E form                  0.000e+00
V_P/V_S closed                0.000e+00
V_P_mean                      2.944e-10
V_P_up(knife FD)              2.366e-12
V_P_up(knife)                 0.000e+00
V_S                           2.934e-10
V_S_mean                      2.934e-10
V_S_up(knife)                 0.000e+00
cliffPpost                    6.766e-15
cliffSpan                     8.426e-16
dDist/dTdr                    2.943e-10
dDist/dTpot                   2.935e-10
delayCostPpost(exact)         5.385e-12
delayCostSpan(exact)          1.204e-11
iso-dist drift ratio          2.500e-01     (= exactly the O(h^2) 1/4)
V_E                           0 (exact)   exact-0 checks: 697
V_P                           0 (exact)   exact-0 checks: 848
V_S                           0 (exact)   exact-0 checks: 677
uncertified-FD skips (ill-conditioned, not asserted): 236
FD ALL PASS (max in-region rel err < 1e-9; zeros exact)
== SB04 batch law vs brute force: 120 random tables ==
  law_states 120 | qThin_exact 120 | mTP_scan==brute 120 | regime_ok 120
  qCrit_identity 87 (rest unstable by construction: lam*a>=1) | Astar_ok 120
  worst W(qThin)-dense rel excess: 0.000e+00
BATCH LAW ALL PASS
```

Method notes: derivatives are 4th-order central + Richardson at h = 5% of the
kink slack (the stencil never straddles a clamp); every asserted comparison
is backed by an h/4 self-convergence certificate (≤3e-10); saturated-region
zeros are asserted as EXACT (score bit-identical under the capped
perturbation); knife-edge rates use the exactly-linear response in h;
cliff/finite-delay forms are checked as exact discrete score differences plus
mirrored-expression bitwise equality. The 236 skips are individual FD probes
on states whose conditioning (cancellation between score components at
extreme parameter scales) exceeds double precision — each such state is still
covered by the mirror/bitwise or exact-zero checks; no formula value is
asserted on an uncertified probe. Sampler covers: w_tp=0 and w_c=0,
dist_base=0, tp strictly below base / exactly at base / exactly at UB /
strictly above UB, ε₁=0 with ε₂>0, ε₂=0 with ε₁>0, both zero (slack and
knife-edge), dist past dist_base, all-interior, G=0.

---

## 6. SIZE LEDGER (Task 6)

Minified bytes (SB04_check.py's sizer mirrors tools/merge_submission.py's
transforms: string-aware comment strip, blank-line removal, 1-space-per-brace
reindent, plus a fragment-local short-name pass in that file's style; nothing
from tools/ is executed):

```
SB04 fragment TOTAL (minified)                                  9,916
SB04 fragment TOTAL (short names)                               9,304
  sec A: exact marginals (drop-in over marginal_*: 560 B today) 4,974
  sec B: constants->formulas helpers                              874
  sec C: PLTab/RoundCost/batch law (overlaps MarginalBatch
         precompute + SB03 engine)                              3,345
DELETE L1 0.15*dist_base tdr-critical (hold_dpre)                 172
DELETE L2 K=1 V_tpot + cliff V_tpot fallbacks (group_size)        242
DELETE L3 ewma_lout<2.5 + SLO1*0.8 (group_size)                   131
DELETE L4 dynamic-cap ladder + link override                      733
DELETE L5 Lin>=256 Tier-1 straggler isolation                     263
DELETE L6 alpha<0.8 NoDelay + alpha xremote forms                 216
DELETE L7 inflight bases + 0.5*dist_base headroom                 654
DELETE L8 tdr_urgent shortcut                                    355
DELETE L9 1.05 PoTC near-tie band                                 247
TOTAL deletable (constant/ladder blocks)                        3,013
```

Honest tiering (consistent with SB03's funding ledger — SB03's M2/M2b/M3
blocks SUBSUME my L1/L2/L3/L9; do not double-count):

- **Tier 1 (Oracle upgrade only)**: replace marginal_tp_v/tdr_v/tpot_v with
  the exact trio + knife-edge up-rates + delayCostPpost/Span (~560 B today →
  ~1.3 KB of sec A; net ≈ +0.75 KB) and delete L1+L2+L8 (769 B) ⇒ **net ≈
  −20 B: break-even, fits in the current 70-byte margin only if trimmed**;
  with L4/L5/L7 (1,650 B) also migrated ⇒ net ≈ −0.9 KB free.

- **Tier 2 (+ constants→formulas helpers, sec B)**: +874 B — fundable from
  L4+L5+L7+L6+L9 (2,113 B) once those decisions are CF-validated.

- **Tier 3 (+ batch law / period models, sec C)**: +3,345 B — overlaps
  MarginalBatch::precompute (deleted under SB03's M2b, 2,486 B) and the SB03
  engine's transfer/table machinery (reuse credits in SB03 §6); only sensible
  as part of that migration, not standalone.

- Sec A beyond the marginal trio (projections, coefficients, composite,
  cliff jumps, dE_effect) is what SB02's terminal evaluator consumes; if the
  SB03 engine lands, sec A's remaining ~3.6 KB rides the same funding.

---

## 7. CORRECTIONS to SB01 (verified here)

1. SB01 §4.1(3) "near the end each time unit is worth more": the exact V_E ∝
   1/E² FALLS with E; the endgame is the concentration of remaining
    opportunity, not a rising rate. The D8 recommendation stood; the stated
   monotonicity does not.

2. SB01 §4.1 V_P/V_S interior forms confirmed EXACT ( FD 3e-10); the "σ≈½
   hedge" near kinks is superseded by the exact knife-edge rates and the
   finite delayCost forms (§1.2-1.3 here).

3. SB01 §1.4's W(q) = q/(2λ) + f(q)/q with the EOQ √(2λf0) — confirmed and
   sharpened to the exact per-segment law (§4), including non-convex tables.

4. SB01 §2.4 P_round = max(local, cloud, transfer) is the PIPET (throughput)
   period; the member-gap law P_gap = max(chain, ⌈pool/m⌉·P_pipe) is new
   here and is the quantity SLO2 binds on.

## 8. What was NOT done (honest boundary; archival as of 2026-08-23)

No source edits were made under this program (prohibited at the time); the replacements were specified but not applied
under SB04. Post-contest record: exact score-calculus work later landed as P4 on 2026-08-26 (see PROGRAM_STATUS.md).
R̂ (final request count) and Ĝ/N̂/Ê projection quality are estimator questions
outside the calculus — the formulas are exact given the projections, and the
sampler/conditioning choices in the checker are documented in §5.
