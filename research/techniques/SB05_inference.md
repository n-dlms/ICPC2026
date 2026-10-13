# SB05 — Statistical Inference Layer

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB05). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.

**Status:** DONE 2026-08-23. Fragment `SB05_inference.cpp` (namespace `sb05`,
header-style + `SB05_DRIVER` verification harness), validation script
`SB05_check.py`. Compiles `-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror`
clean; deterministic (no RNG, no unordered iteration); no allocations; all
queries O(1), all updates bounded by ≤512-step loops per event.

**Mission (completed 2026-08-23):** replaced the solver's ad-hoc estimators (`src/state.hpp/.cpp`
`ewma_lout_`, `lout_s_/lout_c_`, `predict_lout`) with statistically correct,
compact, runtime-computable posteriors, plus a lull/endgame detector and an
optimal-stopping drain rule. Everything here is statement + mathematics +
self-generated synthetic evidence only (pre-freeze rules honored: no repository
scoring/judging tooling used anywhere, no local scores cited).

---

## 0. Statement facts used (verified in problem.md)

| Fact | Line |
|---|---|
| `1 ≤ Lin[i] ≤ 4096`, revealed at ARR | 268, 101 |
| `1 ≤ Lout[i] ≤ 512`, hidden until FIN | 269, 103 |
| `R ≤ 2000`, R **not announced**, ids arrive in arrival order | 267, 98 |
| arrivals nondecreasing timestamps in `[0, 10⁹]` ms | 286 |
| **no end-of-arrivals signal**: "you cannot distinguish a lull from the end of the stream" | 400–401 |
| FIN rides the last D POST TDN frame | 242, 333 |
| Σ Lout ≤ 2·10⁵ tokens per test | 270 |

Consequence used throughout: while request *j* has produced `iter_j` tokens we
know `Lout_j ≥ iter_j + 1` exactly, and its FIN is the only exact reveal. The
observation model is **Type-I right-censored-then-revealed**: censored while
running, fully revealed at completion, censoring times endogenous (a request's
own progress is the censoring clock).

---

## 1. Task 1 — the survival-conditioned Lout posterior

### 1.1 Model

Hierarchical empirical-Bayes model (Robbins' non-parametric EB framing;
`research/papers/wikipedia_empirical_bayes.txt`):

```
Lin_i → geometric bucket b(i)          (feature; half-octave edges below)
Lout_i | b(i) ~ iid p_b(·) on {1..512}   (unknown per-bucket law)
p_b drawn from a population of laws      (between-bucket spread τ²)
```

**Why geometric buckets, not exact Lin.** R ≤ 2000 over 4096 exact-Lin cells
leaves ≈0.5 samples/cell — the deployed D-151 per-exact-bucket mean is mostly a
single noisy draw whenever it fires (it takes priority over the global EWMA,
`state.cpp:643-652`). Half-octave edges give real replication while preserving
the monotone Lin→Lout trend:

```
edges: 1,2,3,4,6,8,11,16,23,32,45,64,91,128,181,256,362,512,724,1024,
       1448,2048,2896,4096        → NB = 24 buckets (+ global row)
lookup: lin2b[4097] uint8 table built once at init (O(1) query, PPRE_MAXLIN-sized)
```

### 1.2 Estimator (as specified pre-freeze)

Per row r ∈ {buckets} ∪ {global} maintain, over COMPLETED requests:

```
cSuf[r][c] = #{fin in r with L ≥ c}         } maintained incrementally:
sSuf[r][c] = Σ ℓ·n over those               } on_fin touches levels 1..lout,
qSuf[r][c] = Σ ℓ²·n over those              } O(lout) ≤ O(512) per FIN
tCnt/tSum/tSum2[r]                          untruncated moments (Mills branch)
```

plus the **self-consistent injected mass** (§1.4): corrected aggregates
`eCnt/eSum/eSum2[r][c]` = completed-exact + active-injected.

Query — running request, bucket b, produced `iter` tokens, cut `c = iter+1`:

```
T̂(b,c)   = eSum[b][c]/eCnt[b][c]            empirical truncated mean ≥ c  (coherent)
T̂(g,c)   = same on global row
θ̂        = w·T̂(b,c) + (1-w)·T̂(g,c),   w = n_b/(n_b + n_g + κ)      [EB shrinkage]
hierarchy: shrunk truncated mean
        → if no mass at c anywhere: inverse-Mills normal-tail estimate
          θ̂ = μ̂ + σ_eff·m(z), z=(c−μ̂)/σ_eff, m(z)=φ(z)/Φ̄(z)   (uses ALL data)
        → EWMA (α=.05, prior 10) → prior 10;  every level floored at c.
κ = 4 pseudo-counts  (normal-normal EB: weight n_b/(n_b+σ²/τ²), asserting τ²≈σ²/4)
```

Coherence property the current code lacks: **θ̂ ≥ iter+1 always** (all summed
mass sits at levels ≥ c; floors elsewhere). The deployed `predict_lout` returns
`round(bucket-mean)` which can be `< iter+1` for any advanced request — an
incoherent answer the EFT cloud chooser then multiplies into its decode-debt
term (`policies.cpp:62-63`). Note also `max(Lin, predict_lout)` there
double-counts the Lin signal; with Lin-bucketed posteriors the `max` was specified to be
dropped (integration note §7).

### 1.3 Variance estimator

```
Var(L | L≥c, data) = w²-weight-free blend (law of total variance):
  v = w·v_b + (1-w)·v_g + w(1-w)(μ_b − μ_g)²
  v_r = qSuf[r][c]/cSuf[r][c] − μ_r²     (completed-only second moment)
thin-tail floor: v ← ω·v + (1−ω)·((512 − max(c,μ))²/12),  ω = n_comp/(n_comp+N0)
Mills branch sd: sd_eff(sd,n,c,μ) — same floor idea by sample size (N0=20)
```

Two deliberate choices, both forced by measured failures during development:

1. **Variance blending must use the total-variance law**, not squared weights.
   My first version used `w²σb²+(1−w)²σg²`; on bimodal mixtures this halved
   already-tight intervals and CI coverage collapsed (first runs: 3% at 1·SE).

2. **Injected mass informs location only.** Counting EM pseudo-points in the
   second moment fakes confidence (they are deterministic given registration);
   variance reads completed-only moments + thin-tail floor. Measured impact of
   getting this wrong: coverage fell from 26.5% to 0.8% when the point-mass
   second moment was counted (see check history in §6).

### 1.4 Self-consistent tail (the "hard-EM" correction)

Completed-only histograms are **length-biased early**: short-Lout requests
finish first, so during burn-in the empirical tail beyond any cut c is too
thin, and both `E[L|L≥c]` and its variance are biased low. This is the classic
truncated-sample problem (Lynden-Bell 1971 NPMLE; Turnbull 1976 self-
consistency; Kaplan–Meier product-limit logic for censored data — see
`research/papers/wikipedia_kaplan_meier.txt`).

Full EM assigns each running request j its expected conditional mass
`p(ℓ)/S(c_j)` over ℓ ≥ c_j. One E-step with the completed-data law, collapsed
to its expected value, gives the shipping scheme: each active request injects
**count 1 at value**

```
v_j = max(cut_j, T_glob(cut_j))
T_glob(c) = raw-completed global truncated mean at c
            (fallback chain: raw moments + Mills → EWMA floored → prior floored)
```

into `eCnt/eSum/eSum2` at every level ≤ cut_j, registered at ARR, re-registered
on each token (`on_iter`), removed at FIN. Cost O(cut) per event (bounded 512),
O(1) query; per-request state `aCut/aVal/aOn[2000]`. This is one hard-EM step
with frozen injections — not iterated to the fixed point (budget), but it
removes the first-order missing-tail bias; the residual is quantified in §6/C.

### 1.5 Cold-start hierarchy (as prescribed)

bucket(n>0) → shrunk-with-global → global → Mills(raw moments) → EWMA → prior 10.
Every level floored at c. Before the first FIN the answer is `max(c, 10)`.

### 1.6 Recurrences (exact costs)

| Event | Work |
|---|---|
| ARR | O(1) counters + inject at cut 1: loop 1..1 |
| token (D_POST) | `on_iter`: re-inject at new cut — loop ≤ 512 |
| FIN | remove injection (loop ≤ 512) + histogram update (loop ≤ 512) + O(1) scalars |
| query predict/var/work-member | O(1) array reads |
| λ̂, p_alive, p_wait, trigger | O(1) |

Total event work per test ≤ (ΣLout)·512 + 2000·512·small ≈ 10⁸ worst-case
elementary ops, typically ≪ (Σ Lout²/2 + 2·ΣLout) ≈ 5·10⁷.

---

## 2. Task 2 — total remaining work Σ over unfinished

```
Ŵ = Σ_j max(1, θ̂(b_j, c_j) − (c_j − 1)),    c_j = max(1, iter_j + 1)
SE² = Σ_j [ v_j  +  v_j / n_eff(j) ]
      └predictive spread┘   └parameter error of the bucket mean┘
CI: Ŵ ± z·SE   (z = 1 → 68%, 1.645 → 90% under normality)
```

`total_work()` is O(pool) per call — caller cadence decision (endgame checks
and rollout entry points only, never per-frame; 2·10⁶ frames × O(R) would be
4·10⁹ ops). An O(1)-amortized variant (maintain Σ est_j with ± deltas at
token/FIN, refresh lazily) is trivially derivable from the same per-request
values if a consumer needs it.

**Measured calibration** (§6 Check C, physically-paced cuts, 3,975 pooled
checkpoints across 10 streams × {heavy, bimodal}): max |Ŵ−truth| 5,174 tokens,
mean |z| = 2.04, coverage 26.5% @1·SE / 46.1% @1.645·SE.

**Known, documented bias (irreducible within the conditioning set):** the
posterior conditions on {bucket, iter} only. It cannot see token pacing, so on
survivor pools (endgame: most unfinished requests sit near their final cut) it
over-predicts remaining work systematically — truth concentrates near 1/request
while `E[L|L≥c] − (c−1)` does not. Direction is known (HIGH), magnitude
bounded ≈ pool size × population tail factor. Consumers were advised to treat Ŵ as an
order-of-magnitude drain-size input, not a calibrated integral; p_crit in the
trigger uses work RATIOS (§4), which survive scale errors. Exact Gaussian
coverage is therefore NOT claimed; gates in the checker encode the achievable
calibration (§6).

---

## 3. Task 3 — arrival process: λ̂, lull-vs-ended, R̂

### 3.1 Rate estimator (generation-based decayed counts)

```
on event:  A ← A·γ + 1 ;   ḡ ← ḡ·γ + (1−γ)·gap      γ = 0.98
λ̂ = (A·(1−γ)/γ) / ḡ        events/ms after ≥2 events
predictive P(next gap > Δ) = (β/(β+Δ))^α,  α = A/(1−γ), β = α/λ̂   (gamma-Poisson)
```

Design notes:

- **Decay by generation (event count), not wall-clock.** Clock-based decay at
  t~10⁹ ms needs careful scale handling and prices a burst correctly only by
  accident; generation decay makes a burst of tiny gaps raise λ̂ directly.

- ḡ is an **EWMA of gaps** (steady state = E[gap]). My first version kept a
  decayed SUM (steady state ḡ/(1−γ)) and λ̂ came out ~50× low — caught by
  Check D, now median |rel err| 8.4% on stationary Poisson, tracks a 4×
  rate jump (0.01→0.04 ms⁻¹ → λ̂ settles at 0.0411).

- Same machinery fed **decode-ready events** (`on_decode_ready`) yields λ_dr —
  the relevant rate for batching-wait decisions (pool growth comes from
  prefills completing + new ARRs, not ARRs alone).

Failure modes: deterministic/batch arrivals violate Poisson smoothness (λ̂ then
estimates mean rate only); alternating-rate regimes are tracked with lag
~1/(1−γ) ≈ 50 events.

### 3.2 Lull-vs-ended posterior (nonparametric gap survival)

Model, stated explicitly:

- H_alive: next gap iid from F (unknown; estimated empirically from observed
  inter-ARR gaps);

- H_dead: stream ended (no further arrivals ever);
- prior odds π₀ = P(dead) = 0.02 at any moment (assumption; only rescales).

```
Shat(d) = (#{observed gaps ≥ d} + 1)/(n_gaps + 2)     add-one smoothed exceedance
P(alive | silence d) = (1−π₀)·Shat(d) / (π₀ + (1−π₀)·Shat(d))
```

Implementation: log₁₀ bins (quarter-decade, 1 ms…10⁹ ms, GAPB=32), RAW
undecayed suffix counts `uSuf[k]`, update O(GAPB) per ARR, query O(1).

Two measured design points:

- **Raw, not decayed, counts.** The gap law does not change because arrivals
  paused; end detection wants the full evidence. Decay serves nonstationary
  *rate* tracking, which is λ̂'s job; the two are deliberately separated.

- **Add-one smoothing placed on the exceedance count.** With zero observed
  gaps beyond d, Shat = 1/(n+2) > 0 — a longer gap than any seen is always
  possible; without smoothing the posterior hit exactly 0 (caught by hand-test).

Measured separation (Check E): gaps U[9,11] ms → P(alive) = 0.962 at
silences inside the support, **0.058** at a 20×-median silence after stream
end, recovering to 0.962 after one resumed arrival. Heavy-tail (exponential)
gaps: 0.264 at a 10×-median end-silence — correct ambiguity under that model
(a 10×-median exponential gap occurs ~e⁻⁶·᠎n times in any long stream).

### 3.3 R-hat — honest identifiability statement

Without a horizon signal, **E[R] is unidentifiable**: any R larger than the
observed count is consistent with a long enough silent tail. Shipping form:

```
R̂(H) = N_arrived + λ̂·H        for a hypothesized remaining horizon H (caller-supplied)
```

plus P(alive|d) as the horizon-free alternative. Failure mode: any fixed H
prior can be arbitrarily wrong; do not feed R̂ into score projections without
a horizon argument (SB02's rollout terminal evaluator was advised to prefer
work-based drains over R̂-based horizons).

---

## 4. Task 4 — endgame trigger as optimal stopping

Decision epoch: a frame offering idle local capacity with a non-empty
READY_D_PRE pool. Protocol fact: **no self-wake** (problem.md:364) — "hold"
means respond without issuing D PRE and re-decide at the next frame; the hold
ends at the next event regardless.

Value model (per-ms comparison, both sides priced in score points):

- **Cost of holding Δ**: the held pool may carry the eventual global-last
  token → makespan risk `V_E·Δ·p_crit` with p_crit := P(held pool carries the
  global last | data), computable proxy = held-work share of Ŵ; plus span
  stretch for held requests already emitting: `V_S·Δ·n_mt` (n_mt = #held with
  iter>0; their e_last moves, e_first fixed — SB01 §4.1 sign convention).
  tdr term is ZERO (tdr closed at P POST for all READY_D_PRE members).

- **Gain of holding Δ**: a joiner arrives within Δ with probability
  `p_join(Δ) = p_alive·(1−e^{−λ_dr·Δ})` and absorbs what would otherwise be a
  follow-up round; conservative gain = one avoided singleton round on the
  critical path, worth `V_E · C₁ · p_join(Δ)` where

  ```
  C₁ = 3S + d_pre(1)+d_proc(1)+d_post(1) + 2·latency + 2·bytes_per_token_rate
  ```

  (SB01 chain time of a singleton round; table-driven).

Per-unit-time net:

```text
G(D)/D = V_E*[C1*p_alive*(1-exp(-lambda_dr*D))/D - p_crit] - V_S*n_mt
```

- `G(D)` — net gain of holding `D`; `C1` — singleton round chain time; `p_alive` — lull posterior.
- `lambda_dr` — decode-ready rate; `p_crit` — critical share; `n_mt` — emitting held count.

Since `(1-exp(-lambda*D))/D` is strictly decreasing in `D` (derivative argument; the stationary equation `x = exp(x)-1` has only `x = 0`), **there is no finite optimal hold** — the greedy per-frame threshold is optimal within the model:

```text
HOLD this frame <=> p_alive * lambda_dr * C1 > p_crit + rho_S * n_mt
```

```text
rho_S = V_S / V_E
```

- `p_alive` (§3.2) — lull-vs-ended posterior.
- `lambda_dr` (§3.1) — decode-ready rate.
- `C1` — table + `S` + link singleton chain.
- `p_crit` — `W_hat` shares (§2).
- `n_mt` — `multi_token_dpre_count`, already `O(1)` in `State`.
- `rho_S` — SB04 calculus marginals.
- Regime note: if `w_tp = 0` then `V_E = 0`; the makespan gain vanishes, `rho_S` blows up — `HOLD = false` (drain) unless a separate span-gain argument is coded; ship `rho_S = 0` and `V_E == 0` ⇒ `false`.

**Degenerate cases (each resolved by the formula itself, no special-casing):**

1. **No arrivals ever seen (n_arr < 2, no gap history).** `p_alive` returns
   its vacuous prior 1.0 (`if (nArr < 2) return 1.0`) but λ_dr = 0 until two
   decode-ready events exist ⇒ LHS = 0 ≤ RHS ⇒ never hold: with zero partner
   evidence there is no batching expectation to wait for; drain immediately.
   (An empty pool makes the trigger vacuous anyway — the only way to hold is
   to have requests, which are arrivals.) The Lout posterior answers
   max(cut, 10) (prior level, §1.5).

2. **Single request (pool = {j}).** p_crit = 1.0 by the work-share proxy (the
   held pool IS the whole remaining work ⇒ it certainly carries the global
   last token). HOLD then requires p_alive·λ_dr·C₁ > 1 + ρ_S·n_mt — i.e. more
   than one expected joiner per singleton-round chain time — so the certain
   makespan delay of holding the only request is priced at full weight and
   the trigger fires only in genuinely heavy-arrival regimes, where a joiner
   is near-certain within C₁. If j is already emitting (iter > 0), ρ_S·n_mt ≥
   ρ_S adds its span-stretch cost on top.

3. **Lout unknown while running.** Never actually unknown: the statement
   certifies Lout ≥ iter+1 at every moment, so the posterior conditions on
   cut = iter+1 exactly (§1); at iter = 0 it degenerates to the bucket law
   floored at 1. The trigger itself consumes no Lout point value — Lout
   uncertainty enters only through Ŵ and hence p_crit, where the coherence
   floor (θ̂ ≥ cut, remaining-work floored at 1) keeps shares in [0,1].

Endgame regime definition (detector): `is_endgame` := p_alive(current
silence) < ε_end (default 0.05) — i.e., the stream has likely ended; combined
with (★) failing persistently, the policy stops waiting for batch partners and
drains, prioritizing the projected global-last request (SB01 age escalation).

---

## 5. API surface (fragment)

```cpp
sb05::Infer inf;  inf.init();                       // once
inf.on_arr(rid, lin, gap_ms);                       // ARR
inf.on_iter(rid, lin, new_cut);                     // D_POST token (cut=iter+1)
inf.on_decode_ready(gap_ms);                        // READY_D_PRE push (optional feed)
inf.on_fin(rid, lin, lout);                         // FIN
double th  = inf.predict_lout(lin, iter);           // posterior mean, ≥ iter+1
double sd  = inf.predict_sd(lin, iter);             // posterior sd
double W   = inf.total_work(cuts[], lins[], m, &se);// O(m) on demand
double la  = inf.lambda_arr();  double ld = inf.lambda_dr();
double pa  = inf.p_alive(silence_ms);  double pw = inf.p_wait_arr(delta);
double rh  = inf.r_hat(arrived, horizon);
bool hold  = sb05::Infer::hold_dpre(pa, ld, C1, p_crit, n_mt, rho_S);
```

Fixed memory: `sizeof(Infer)` = 543,928 B (~532 KB static, no heap use;
dominated by the 25×513 histogram rows). Deterministic; float ops order-fixed
(bit-matched by the Python reference, §6/A).

---

## 6. Validation (SB05_check.py) — ALL PASS

Independent slow Python references mirror the fragment operation-for-operation;
synthetic streams only (no repository case suites, no judging tools).

**A. Equivalence** — 4 scenarios (heavy-tailed, bimodal, uniform, Pareto-on-Lin)
× randomized event scripts incl. mid-flight cut advances: ~11,000 printed
estimator values bit-matched C++↔Python (tolerance 1e-12; achieved ~1e-15).

**B. Posterior quality vs TRUE Lout** (survival-conditioned query points,
physically-paced iters; 400 requests × 10 seeds × 4 scenarios):

| scenario | NEW MSE / MAE | plain EWMA | max(Lin,ewma) | exact-Lin bucket mean (deployed) |
|---|---|---|---|---|
| heavy       | **11,902 / 89.5**  | 23,071 / 94.5  | 524,593 / 371   | 23,281 / 96.4 |
| bimodal     | **17,239 / 105.2** | 84,125 / 259.9 | 313,168 / 325.1 | 87,565 / 263.5 |
| uniform     | **15,704 / 100.6** | 30,992 / 148.2 | 250,594 / 257.7 | 35,729 / 158.1 |
| pareto_lin  | **1,926 / 15.6**   | 2,962 / 15.1   | 1,462,557 / 682 | 2,391 / 13.7 |

NEW bias: +39.8 / +21.3 / +7.1 / +5.2 (sign explained in §2). NEW wins MSE in
every scenario; largest relative win exactly where the deployed heuristic is
most wrong (bimodal: 4.9× better than the bucket-mean path). Residual
weakness: on pareto_lin the plain-EWMA/bucket-mean paths keep slightly lower
MAE (15.13 / 13.66 vs NEW 15.62) — they shrink toward small prior values and
collect small absolute errors on short requests while losing MSE badly
(2,962 / 2,391 vs NEW 1,926); under squared loss NEW dominates everywhere.

**C. Total work** — max |err| 5,174 tok; mean |z| 2.04; coverage 26.5%/46.1%
(n=3,975). Gates encode the documented pacing-blindness bound (§2).

**D. Arrival rate** — stationary λ=0.01/ms: median rel err 8.4%; 4× jump
tracked to 0.0411 (truth 0.04).

**E. Lull-vs-ended** — decisive on low-variance gaps (0.058 post-end),
conservative under heavy tails (0.264, model-correct), recovers on resume.

**F. Trigger** — 10,000/10,000 exact matches vs closed form.

**G. Determinism** — 3 fresh driver runs on a fixed ~3,000-event seeded
script: byte-identical stdout.

Events exercised across A–C: ~127,000 ≥ 10⁵ required.

---

## 7. Integration map (archival; recorded for SB09/SB10; no src/ edits were made under SB05)

| Site today | Action |
|---|---|
| `state.cpp:284` ewma_lout_ update at FIN | replace with `on_fin(rid, lin, lout)` |
| `state.cpp:286-291` D-151 bucket feed | delete |
| `state.cpp:193` `++req.iter` (D_POST) | add `on_iter(req.id, req.lin, req.iter+1)` |
| `state.cpp:74-76` ARR (has `last_arr_`) | call `on_arr(rid, ev.lin, t − last_arr_prev)`; keep `ewma_lin_` (used by admission cap) |
| `state.cpp:643-653` predict_lout_impl/predict_lout | delegate to `predict_lout(lin, iter)` |
| `state.hpp:360,362-367` members + decls | delete (ledger §8) |
| `policies.cpp:62-63` `this_lout = max(Lin, predict_lout)` | drop the `max(Lin,·)` (double-counts Lin); use posterior directly |
| `policies.cpp:246` `ewma_lout()<2.5` low-lout gate | byte-neutral keep; recorded as a candidate to replace by posterior-mass test (flag for SB10) |
| `SloAwareDelay::hold_dpre` | optionally evaluate `(★)`; inputs available |

## 8. Size ledger (merge_submission-style minify: comments/blanks stripped, 1-space indent)

Fragment header (shipped code, no driver): **14,239 B**
Driver (verification only, NOT shipped): +2,259 B

| block | B |
|---|---|
| decls + constants | 843 |
| init + lin2b + gap_bin | 514 |
| events + EM hooks | 494 |
| injection helpers (hard-EM) | 2,676 |
| suffix views | 1,299 |
| TASK-1 posterior (mean/sd/Mills/floors) | 4,407 |
| total_work | 2,265 |
| arrival + lull + R̂ | 944 |
| trigger | 162 |

Deleted machinery (state.hpp/.cpp ewma_lout_/lout_s_/lout_c_/predict_lout):
**968 B** → **net cost +13,271 B.**

Budget reality (docs/DECISIONS.md §0.8): artifact margin ≈ 70 B; SB03 sized the lean
forward engine at 8.0–8.7 KB against ~7.5 KB deletable. The fragment as-is
does NOT fit. Fundable subsets, by value per byte:

1. **Core tier (~8.7 KB):** posterior + hierarchy + floors + trigger +
   arrival/lull + init/events, dropping EM injection (−2,676) and total_work
   (−2,265). Loses the self-consistent tail (burn-in bias returns) and Ŵ/p_crit
   becomes caller-side arithmetic.

2. **Recommended tier (~11 KB):** core + EM injection (keeps measured Check-B/C
   quality); fund via `_RENAME` pool (D-139 mechanism) + consolidation pass on
   TASK-1 (the mean/sd pair triplicates blend logic; a shared blended-view
   helper saves ~0.7 KB).

3. total_work (−2,265 B) was noted as droppable if SB08 derived p_crit from scheduler
   counters instead of posterior sums.

Runtime memory (~532 KB static) is irrelevant vs 256 MB; source bytes are the
binding constraint.

---

## 9. Model assumptions & failure modes (complete list)

| Assumption | Where | Failure mode | Mitigation |
|---|---|---|---|
| Lout iid within geometric-Lin bucket | §1.1 | within-bucket heterogeneity (bimodal-by-something-else) inflates irreducible MSE | more features unavailable; shrinkage limits damage |
| between-bucket spread τ² ≈ σ²/4 (κ=4) | §1.2 | mis-shrinkage if buckets far more homogeneous | κ tunable constant; effect bounded |
| normal tail shape in Mills fallback | §1.2 | heavy true tails → tail means under-estimated at extreme cuts | floor keeps coherence; EM injection adds tail mass |
| one-step EM with frozen injections | §1.4 | residual length-bias early; injections stale within a frame | bounded, measured (Check B biases) |
| truncation-only conditioning (no pacing) | §2 | survivor pools over-predicted (direction HIGH) | documented; consumers use ratios/order-of-magnitude |
| gamma-Poisson predictive | §3.1 | bursty/deterministic arrivals break smoothness | λ̂ remains a mean-rate estimate; lag ~50 events |
| gap law stationary (raw counts for p_alive) | §3.2 | permanent mid-stream rate drop read as "alive" | λ̂ carries the slowdown; trigger multiplies both |
| π₀ = 0.02 dead-prior | §3.2 | rescales posterior only; no calibration claim | tunable |
| R̂ needs horizon H | §3.3 | unidentifiable otherwise | documented; prefer work-based drains |
| critical-path share proxy for p_crit; singleton-round gain | §4 | over/under-states hold value when rounds pipeline | conservative (gain side minimized); CF probe arbitrates |

## 10. References

- Robbins non-parametric empirical Bayes, shrinkage, Good–Turing connection:
  `research/papers/wikipedia_empirical_bayes.txt` (downloaded 2026-08-23).

- Product-limit/censored-data estimation context (Kaplan–Meier 1958 JASA):
  `research/papers/wikipedia_kaplan_meier.txt`.

- Sequential change detection (Page 1954 Biometrika CUSUM) — context for
  rate-change tracking: `research/papers/wikipedia_cusum.txt`.

- Lynden-Bell (1971) / Turnbull (1976) truncated-data self-consistency and
  inverse-Mills ratio: standard results, formulas verified numerically against
  the Python reference in-check (no downloaded paper; marked [UNVERIFIED-
  primary], formulas tested).

- In-repo prerequisites: SB01 (marginals V_E/V_S, batch law, chain time C₁),
  SB04 (score calculus, ρ_S inputs), SB03 (event semantics for on_iter wiring).
