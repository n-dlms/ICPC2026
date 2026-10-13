# SA03 — The Output-Rate (Throughput) Component: Measurement Interval and Strategic Implications

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA03 drafting;
read-only on src/ and tools/; experiments in
`experiments/sa03/` and `/tmp/sa03/`). Program completed 2026-08-23; contest
ended Aug 28 2026 with final rank #56. Solver under test: `build/submission` (the CF
artifact). Tags: [FACT-statement] problem.md cite, [FACT-code] local source cite,
[EXPERIMENTALLY SHOWN] my measurements, [DERIVED] arithmetic/logic from verified facts.

Tools built (also in `experiments/sa03/`):

- `tp_analysis.py` — runs a case via `tools/interactor.py --trace-file`, parses the
  trace, computes tp-clock quantities, analytic one-at-a-time reference (`solo_time`,
  `serial_T`), per-resource structural floors (`resource_floors`: UP/DOWN link serial
  time, local-computer serial work, cloud/K), sensitivities.

- `fit_cf_tp.py` — fits the REAL per-test `tp_base`/`tp_UB` from CF judgement dumps.
- `gen_shift.py`, `gen_finishline.py` — crafted cases (in `/tmp/sa03/cases/`).

---

## 1. Verified clock semantics

Formula [FACT-statement, problem.md:433-441, re-read] in tokens/ms:

```text
tp = Σ_i Lout[i] / (latest final-token production time − earliest arrival time)
tp_component = clamp(tp; tp_base, tp_UB)
```

- `tp` — output rate in tokens/ms
- `Lout[i]` — output tokens of request `i`
- `tp_base` — reference rate
- `tp_UB` — high rate

linear 0→1 between. Judge implementation
[FACT-code, tools/interactor.py:397]: `tp = self.tok_out / (self.last_finish -
self.first_arrival)`; `first_arrival` = min over ARR frames (line 124), `last_finish` =
max over final-D-POST/FIN times (lines 149-150).

**1a. Time before the first ARR is score-free.** [EXPERIMENTALLY SHOWN] `cases/zoo/
mixed.txt` has all 50 arrivals at t=1200. Its zero-shifted twin (`/tmp/sa03/cases/
mixed_shift0.txt`, arrivals −1200) scores byte-identically: both `score 500.253`,
E=395.635, tp=0.505517. Both clock endpoints shift together; nothing before the first
ARR matters. (Also visible in the solo-validation trace: a request isolated at
t=30000 finishes 30058.8; the idle 10043→20000 between earlier requests lands INSIDE
the clock, the pre-first-ARR 0→10000 does not.)

**1b. The clock ends at the LAST token of the whole test — one straggler inflates the
denominator for every token produced.** [EXPERIMENTALLY SHOWN] Crafted pair (K=2,
S=1, flat-ish table, ΣLout=24, arrivals {t=0, t=200}, scoring line tp_base=0.02,
tp_UB=0.5, w_tp=1):

- `tailA_longfirst` (Lout=20 arrives FIRST, Lout=4 at t=200): E=243.20, tp=0.0987,
  c_tp=0.1639, **score 163.925**.

- `tailB_longlast` (Lout=4 first, Lout=20 arrives at t=200): E=368.00, tp=0.0652,
  c_tp=0.0942, **score 94.203**.
Identical total work and arrival times; only WHICH request owns the tail differs →
**69.7 points** (of 1000) swing. In both runs the solver was AT my structural floor
(dctp=+0.0000) — the loss is pure clock semantics, not solver error. Real-case
magnitudes: `adv_straggler` (400×(Lin=1,Lout=1) + 1×(512,512), all t=0): herd done at
2057.0 but rid=400 finishes at 6656.0 → tp=912/6656=0.1370; had the straggler landed
with the herd, tp would be 912/2057=0.443 (3.2×). `adv_teeter`: herd done 3327.0,
straggler (Lout=500, ARR t=1800) ends 6821.0 → straggler slack 3494ms = 51% of E.

**1c. Inter-arrival idle is inside the clock (structural floor).** For spread
arrivals, elapsed ≥ (last ARR − first ARR) + minimal processing tail of the last
arrival's request. [EXPERIMENTALLY SHOWN] `dribble`: arrival span 990.0 = 91.6% of
E=1081.1 — tp is structurally pinned (E within 1.6% of floor 1063.7; zero tp headroom
even at tp_UB=1000: dctp=+0.0000). `cf9_shape`: span 407,785.6 = 97.0% of
E=420,212.0. See the table in §3.

**Numerator fixed.** [FACT-code, interactor.py:393-394] `if self.done != c.R: return
0.0` — every request must finish (all Lout tokens produced) or the score is zero;
ΣLout is a property of the test file. No token can be hidden: tp only reads the LAST
final-token time, but producing fewer tokens = zero score. **The ONLY lever on the tp
component is the denominator (E).** [DERIVED]

---

## 2. Tail/straggler experiments (numbers)

| Experiment                | Case                    | E (ms)       | tp            | score        | Note                              |
| ------------------------- | ----------------------- | ------------ | ------------- | ------------ | --------------------------------- |
| Shift invariance          | mixed vs mixed_shift0   | 395.635 both | 0.505517 both | 500.253 both | pre-first-ARR time free           |
| Tail owner A (long first) | tailA_longfirst         | 243.20       | 0.09868       | 163.925      | at floor                          |
| Tail owner B (long last)  | tailB_longlast          | 368.00       | 0.06522       | 94.203       | at floor, −69.7 pts               |
| Straggler slack           | adv_straggler           | 6656.0       | 0.1370        | 0.137 local  | herd done 2057; tp would be 0.443 |
| Straggler slack           | straggler_purgatory     | 6486.0       | 0.1388        | —            | herd done 1995                    |
| Straggler slack           | adv_teeter              | 6821.0       | 0.2199        | 610.0        | slack 3494ms = 51% of E           |
| Finish-line weights       | fin_tp (w_tp=1)         | **144.0**    | 0.5694        | 1000.0       | same work                         |
| Finish-line weights       | fin_half (.5/.5)        | **151.8**    | 0.5402        | 830.9        | same work                         |
| Finish-line weights       | fin_wc (w_c=1, SLO1=40) | **170.0**    | 0.4824        | 566.8        | same work                         |

The finish-line rows are one crafted case (10×(Lin=8,Lout=8) at t=0 + (64,2) at t=30)
differing ONLY in the scoring line: the solver's marginal-value loop already trades
tail waiting-compliance for clock compression (E varies 144→170, 18%). [EXPERIMENTALLY
SHOWN]

**Tail anatomy** (`adv_teeter` trace, `/tmp/sa03/ab_submission_adv_teeter.txt`): after
the herd drains (t=3327) the straggler runs 500 solo m=1 decode rounds; consecutive
local task starts show period 7.004ms vs the analytic per-request dependency chain
D_PRE(2)+UP(0.001)+D_PROC(3)+DOWN(0.001)+D_POST(2)=7.002ms — the tail is AT the
per-request floor (6.988 ms/token measured). The 3494ms tail is irreducible for a
Lout=500 request whose decode can only start after the herd frees the local machine.
[EXPERIMENTALLY SHOWN]

---

## 3. Structural-floor table (per-case headroom)

ceiling = ΣLout/floor2 where floor2 = max(arrival span + solo(last arrival), UP-link
serial, DOWN-link serial, local-computer serial, cloud/K) — all resource use lies
inside [first ARR, last finish] and each resource is serial, so each is a valid lower
bound on E (local/cloud floors use max-batching optimistic costs; they are still lower
bounds). `dctp@floor2` = extra c_tp points if E were driven to floor2 (waiting
component held fixed).

| case                    | R    | ΣLout | arrival span | tail after last ARR | E (actual) | floor2 (binding) | tp_base | tp_UB   | tp       | c_tp          | ceiling | UB/ceiling | ach/ceiling | dctp@floor2 | pts/1% E |
| ----------------------- | ---- | ----- | ------------ | ------------------- | ---------- | ---------------- | ------- | ------- | -------- | ------------- | ------- | ---------- | ----------- | ----------- | -------- |
| dribble                 | 100  | 400   | 990.0        | 91.1                | 1081.1     | 1064 (local)     | 0       | 1000    | 0.3700   | 0.0004        | 0.376   | 2659       | **0.984**   | +0.0000     | 0.0019   |
| burst_100               | 100  | 400   | 0.0          | 682.0               | 682.0      | 586 (local)      | 0       | 1000    | 0.5865   | 0.0006        | 0.683   | 1465       | 0.859       | +0.0001     | 0.0029   |
| m6 (mimic CF #5/#6)     | 3000 | 96000 | 0.0          | 25286.4             | 25286.4    | 24300 (local)    | 0       | 20      | 3.7965   | 0.1898        | 3.958   | 5.05       | 0.959       | +0.0081     | **1.71** |
| m15 (mimic CF #15)      | 200  | 200   | 9950.0       | 26118.0             | 36068.0    | 35200 (UP)       | 0       | 1000    | 0.00555  | 0.0000        | 0.00568 | 1.8e5      | 0.976       | +0.0000     | ~0       |
| adv_teeter              | 1001 | 1500  | 1800.0       | 5021.0              | 6821.0     | 5320 (local)*    | 0       | 1       | 0.2199   | 0.2199        | 0.282   | 3.54       | 0.779       | +0.062*     | 1.10     |
| adv_abyss               | 2000 | 2000  | 0.0          | 6.56e10             | 6.558e10   | 6.56e10 (UP)     | 1.52e-8 | 4e-8    | 3.049e-8 | 0.6167        | 3.05e-8 | 1.31       | **1.000**   | +0.0006     | 6.15     |
| cf9_shape (mimic CF #9) | 2000 | 2000  | 407785.6     | 12426.4             | 420212.0   | 4.20e5 (local)   | 1e-4    | 0.00452 | 0.00476  | **1.0 (sat)** | 0.00476 | **0.949**  | 1.000       | +0.0000     | 0        |
| adv_x1_blackhole        | 500  | 500   | 0.0          | 1.642e10            | 1.642e10   | 1.64e10 (UP)     | 0       | 1e-7    | 3.045e-8 | 0.3045        | 3.05e-8 | 3.28       | 0.998       | +0.0006     | 3.04     |
| adv_straggler           | 401  | 912   | 0.0          | 6656.0              | 6656.0     | 3780 (local)*    | 0       | 1000    | 0.1370   | 0.0001        | 0.241   | 4143       | 0.568       | +0.0001*    | 0.0014   |

*floor2 ignores cross-request resource contention (a straggler's solo pipeline cannot
start while the herd still occupies the local machine), so teeter/straggler headroom
(dctp) is overstated — the teeter tail is at its dependency floor (§2).

Key readings: on the big/contended cases the solver already sits at 96-100% of the
combined resource ceiling (ach/ceiling column) — matches the D-143/D-144 saturation
finding `max(E,UP,C/K)/mk ∈ [0.975,1.000]`. The only local case with real (small)
headroom is m6 (+0.008 c_tp) and teeter (contention-masked).

---

## 4. tp_base reconstruction — the one-at-a-time reference

**Rule (validated).** Per request, run completely alone with 1-chunk prefill and m=1
decode rounds:

```text
solo(Lin, Lout) = (S+ppre(Lin)) + xfer(Lin)          # P PRE + UP (len=Lin)
                + (S+pproc(Lin)) + xfer(Lin)         # P PROC + DOWN (len=Lin)
                + (S+ppost(Lin))                     # P POST
                + Lout × [ (S+dpre(1)) + xfer(1) + (S+dproc(1)) + xfer(1) + (S+dpost(1)) ]
tp_base_ref = Σ Lout / Σ solo        (back-to-back serial; K irrelevant)
```

- `solo` — isolated request time
- `Lin` — input length
- `Lout` — output length
- `S` — setup cost
- `xfer` — transfer time
- `tp_base_ref` — reference throughput

[EXPERIMENTALLY SHOWN] Validation 1 — exact solo spans vs interactor trace on crafted
isolated-request cases (`/tmp/sa03/cases/solo_valid.txt`, `solo_layers64.txt`): Lin∈
{1,8,64}, Lout∈{2,4,6}, S∈{1,3}, layers∈{8,64}: measured (FIN−ARR) = 43.20002 /
27.60004 / 58.8001 / 73.200 vs analytic 43.20002 / 27.60004 / 58.80010 / 73.20007 —
agreement to 1e-8 ms. The solver chunks prefill into ONE piece when isolated, so its
solo schedule IS the reference schedule. Validation 2 — `adv_abyss` (the only local
case authored from the reference rule, adversarial_generator.py:265-270): authored
tp_base=1.52e-8 vs reconstruction 1.5255e-08 (author rounded to 3 digits).

**Local generators do NOT use the rule**: `tools/zoo_gen.py:118` hardcodes
`tp_base=0.0` everywhere; `tools/adversarial_generator.py` uses 0 except abyss;
`tools/mimics.py` copies/backsolves CF windows (m6 tp_UB=20, cf9_shape
tp_base=1e-4/tp_UB=0.00452).

**Real CF values (new).** Two full judgement dumps (docs/CF_JUDGEMENT_2026-08-16.md,
-08-17.md) give ≥2 (tp, norm_tp) pairs per test; in the linear region
`norm_tp=(tp−b)/(UB−b)` solves exactly for the real (tp_base, tp_UB)
(`experiments/sa03/fit_cf_tp.py`; residuals ≤2e-4; cross-checks D-143's independent
estimates #5 3.38 vs my 3.30, #6 2.16 vs my 2.09):

| CF test                  | tp_base (fitted)                                  | tp_UB (fitted) | UB/base | leader-era norm_tp |
| ------------------------ | ------------------------------------------------- | -------------- | ------- | ------------------ |
| #3                       | 0.003167                                          | 0.005223       | 1.65×   | 0.67               |
| #4                       | 0.008507                                          | 0.115564       | 13.6×   | 0.38               |
| #5                       | 0.02264                                           | 3.3258         | 147×    | 0.30               |
| #6                       | 0.02245                                           | 2.1086         | 94×     | 0.29               |
| #7                       | 0.003271                                          | 0.029793       | 9.1×    | 0.22–0.42          |
| #8                       | 0.002985                                          | 0.016374       | 5.5×    | 0.70               |
| #9                       | 0.001396                                          | 0.004512       | 3.2×    | 0.92–0.96          |
| #10                      | 0.002882                                          | 0.007655       | 2.7×    | 0.994              |
| #13                      | 0.007648                                          | 0.035687       | 4.7×    | 0.57               |
| #16                      | 0.002967                                          | 0.030276       | 10.2×   | 0.87–0.97          |
| #21                      | 0.001430                                          | 0.012554       | 8.8×    | 0.97               |
| #22                      | 0.1658                                            | 43.631         | 263×    | 0.07               |
| #1, #2                   | (b ≥ 0.02222, 0.00576; norm_tp=0 both runs)       | —              | —       | 0                  |
| #11,12,14,15,17,18,19,20 | not fittable (identical runs / tp printed at 6dp) |                |         | 0.8–0.99           |

Independent cross-validation: the fitted #9 tp_UB 0.004512 equals cf9_shape's authored
0.00452 (the D-143 author had done the same two-point fit); the fitted #9 tp_base
0.001396 is 14× the mimic's 1e-4 — the mimic's tp_base is wrong and worth fixing.

**Does the reference rule predict the real tp_base?** Not exactly: real tp_base sits
at 2–33% of the achieved rate (e.g. #5: 0.0226 vs achieved 1.017), i.e. the reference
schedule is 3–45× slower than a pipelined solver — consistent with a serial reference.
But on #9 the real tp_base (0.001396) is 2.2× SLOWER than my reconstruction run on the
cf9_shape mimic (0.00311). Either the real #9 is Lin-heavier than the mimic (solo time
scales with Lin transfers; pipelined rate does not), or the judge's reference is more
pessimistic than pure back-to-back solo (e.g. requests processed at original arrival
times with idle). For dense arrivals both variants coincide; they differ only on
sparse tests. Unresolvable without real inputs — flagged in §8.

---

## 5. tp_UB semantics

[FACT-statement] "tp_UB is an estimated high rate"; [FACT-web, SA02] constraint
`tpUB > tpbase`, tpUB ≤ 1e9. Local practice: zoo 1000 (never binds), adversarial
brackets the physics on purpose (abyss tp_UB = 1.31× ceiling; x1 = 3.28×), mimics copy
fitted CF windows. **The real judge is heterogeneous** (§4 table): UB/ceiling-ratio
where computable: #9 ≈ 0.95× the span-dominated ceiling (tight — solver saturates
c_tp=1); #5/#6/#22 sit 3.3×/3.3×/~13× above the achieved rate while D-143's meter
showed those tests at 97-100% of max(E,UP,C/K) — i.e. **tp_UB above the physical
ceiling** (the "starve class": the linear ramp never saturates and every ms counts,
but full marks are unreachable). Practical consequence: a solver cannot assume either
regime; it must evaluate the marginal value from the scoring line at startup, which the artifact
already does.

```text
V_tp = 1000·w_tp/(tp_UB−tp_base)
```

- `V_tp` — points per unit tp
- `w_tp` — throughput weight
- `tp_UB` — high rate
- `tp_base` — reference rate

[FACT-code, DECISIONS.md:2507].

---

## 6. Sensitivity / marginal-value analysis

In the linear region (tp_base < tp < tp_UB), with the waiting component fixed:

```text
dScore/dE = −1000·w_tp·ΣLout / (E²·(tp_UB−tp_base))
points per 1% E = 10·w_tp·tp/(tp_UB−tp_base)
```

- `E` — elapsed
- `w_tp` — throughput weight
- `ΣLout` — total tokens
- `tp_UB` — high rate
- `tp_base` — reference rate
- `tp` — output rate

[DERIVED]. Zero when clamped (tp ≤ tp_base) or
saturated (tp ≥ tp_UB).

| case       | pts per ms | pts per 1% E | regime                            |
| ---------- | ---------- | ------------ | --------------------------------- |
| m6         | 0.00676    | 1.71         | linear (w_tp=0.9, band 20)        |
| adv_teeter | 0.0161     | 1.10         | linear (band 1)                   |
| adv_abyss  | 9.4e-9     | 6.15         | linear (band 2.5e-8)              |
| adv_x1     | 1.9e-8     | 3.04         | linear                            |
| dribble    | 1.7e-4     | 0.0019       | linear but band=1000 → negligible |
| cf9_shape  | 0          | 0            | **saturated**                     |
| fin_tp     | 0          | 0            | saturated                         |

Real-CF numbers [DERIVED from fitted bands + dump rows]: #5 (w_tp=0.80, band 3.303,
tp≈1.017): **2.46 pts per 1% E**; #6 (w_tp=0.90, band 2.086, tp≈0.632): **2.72 pts
per 1% E**. (w_tp backsolved from points = 1000·(w_tp·norm_tp + w_c·norm_c): #5
0.800, #6 0.900.) On saturated tests (#9, #10, cf9_shape-like) marginal tp value is
exactly 0 — further speedup there is worthless; on #22 even a 2× speedup moves
norm_tp only 0.07→0.14 (~63 pts of 1000 at w_tp≈? — #22's w_tp≈0.87 from
532.773=1000(w·0.0702+(1−w)·0.9954)). dScore/d(token) is undefined/vacuous: the
numerator is fixed by the test (§1).

---

## 7. Strategic implications

1. **[DERIVED, from §1]** Nothing before the first ARR matters — never waste
   design/hoisting effort on "warm-up"; conversely all idle INSIDE [first ARR, last
   FIN] is paid at full rate (dribble: 91.6% of E is arrival span the scheduler
   cannot remove — identify such tests early from the arrival stream and deprioritize
   tp chasing).

2. **[EXPERIMENTALLY SHOWN, §1b]** One straggler sets the clock for ALL tokens:
   69.7/1000 pts on a 2-request crafted pair with identical work; 3.2× tp loss on
   adv_straggler. Priority should weight a request's REMAINING-token span (Lout−
   tokens_done) by its chance of finishing last — finishing order at the tail matters
   exactly when finishing times are spread.

3. **[EXPERIMENTALLY SHOWN, finish-line]** The current solver already adapts its tail
   to score weights (E 144/151.8/170 across w_tp=1/0.5/0 on identical work) — the
   marginal-value loop is live; gains from further finish-line logic must be measured
   against it, not against a static baseline.

4. **[EXPERIMENTALLY SHOWN, §2]** A lone request's decode tail is floored by its own
   dependency chain (D_PRE→UP→D_PROC→DOWN→D_POST, 7.002ms/round on teeter; measured
   6.988). The only levers on a straggler tail are (i) starting it earlier (overlap
   with the herd), (ii) chunking its prefill, (iii) nothing else — reordering within
   the tail cannot beat the chain.

5. **[DERIVED, §6]** On saturated tests (tp ≥ tp_UB) tail speedups are worth 0;
   on starve-class tests (tp_UB ≫ ceiling) every 1% of E ≈ 1.7–2.7 pts. Classify the
   test live via V_tp = 1000·w_tp/(tp_UB−tp_base) vs achievable-rate estimates — the
   artifact already computes V_tp; the fitted real bands (#5 3.30, #6 2.09) can
   sharpen local mimics (m6 currently uses band 20 vs real 3.30).

6. **[DERIVED]** Because tp uses only the LAST final-token time, producing the final
   tokens of nearly-done requests BEFORE starting fresh long decodes strictly shrinks
   E at fixed total work — bounded by the tail-owner experiment (69.7 pts there). But
   tdr/tpot still count, so this is a tail-only (post-last-arrival) rule.

7. **[EXPERIMENTALLY SHOWN, §3]** On every large local case the solver is at 96-100%
   of the resource ceiling — further tp gains must come from STRUCTURE (fewer decode
   rounds via bigger groups, fewer prefill legs, overlap), not from filling idle
   (there is none). Consistent with D-143/D-144's falsification of the idle-channel
   model.

8. **[RECORDED FOLLOW-UP, closed at program completion]** Fix cf9_shape's tp_base (1e-4 → 0.0014) and m6's
   tp_UB (20 → 3.3) so local starve-class tuning saw CF-real slopes: with band 20
   the same ΔE looked 6× less valuable than on real #5/#6.

---

## 8. Open questions recorded at program close (closed — program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56)

1. **Reference-schedule variant**: back-to-back solo vs FIFO-at-original-arrivals
   differed only on sparse tests; real #9's tp_base was 2.2× slower than mimic-solo.
   Actionable at the time: back-solving more (tp, norm_tp) pairs from the later per-test rows
   scattered in DECISIONS.md (D-140..D-153 quotes) to tighten bands for
   #11/12/14/15/17/18/19/20 (unfittable at 6-decimal print precision at program close).

2. **#22's huge band (43.6)**: the question was whether tp_UB=43.6 was ~13× above its ceiling (like x1's 3.28×
   but worse). If so, #22 was a permanent-partial test; the effort cap would have followed.

3. **Straggler-start overlap headroom**: adv_teeter's straggler decode started at 3327
   (herd-drain) vs its prefill completing much earlier — the remaining task at close was to quantify how much of the
   3494ms tail could have overlapped the herd by prioritizing the straggler's early rounds
   (contention-aware floor needed; floor2 was contention-blind).

4. **tp_base=0 tests (#1/#2)**: achieved tp below the reference on #1 (0.0222 = 1/45)
   suggested single-request or pathologically serial tests where norm_tp=0 regardless —
   the recorded check was to confirm w_tp=0 there (points 500.0 = 0.5·1000 exactly, norm_c=1) so tp effort on
   those tests was provably worthless.

5. The planned extension was to extend `fit_cf_tp.py` with an automated DECISIONS.md row scraper; refits after
   later CF submissions would each have added a free equation (each moved test adds a free equation).
