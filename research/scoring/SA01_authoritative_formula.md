# SA01 — Authoritative Scoring Function and Local-Implementation Audit

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA01 drafting;
read-only audit of src/ and tools/; experiments in
`experiments/sa01/` and `/tmp/sa01/`). Program completed 2026-08-23; contest
ended Aug 28 2026 with final rank #56.

Claim tags: [FACT-statement] = quoted from `/home/dlamini/ICPC2026/problem.md` (official
CF statement scrape, PRIMARY AUTHORITY), with `problem.md:LINE` cites. [FACT-code] =
cited from local source. [INFERRED] = my experiment. [ASSUMED] = contestant belief in docs.

---

## 1. The Authoritative Formula [FACT-statement]

All citations are to `/home/dlamini/ICPC2026/problem.md`.

### 1.1 The conversion primitive (problem.md:425-431)

> "(0 at base, 1 at target, clamped outside)."

```text
clamp(x; base, target) = max(0, min(1, (x−base)/(target−base)))
```

- `x` — measured value
- `base` — value mapping to 0
- `target` — value mapping to 1

Note the generic form admits `target < base` (used for the waiting component with
base=dist_base, target=0).

### 1.2 Output-rate component (problem.md:433-441)

```text
tp = Σ_i Lout[i] / total elapsed time
total elapsed time = (latest final-token production time) − (earliest arrival time)
tp_component = clamp(tp; tp_base, tp_UB)
```

- `tp` — output rate in tokens/ms
- `Lout[i]` — output tokens of request `i`
- `total elapsed time` — latest final-token time minus earliest arrival, in ms
- `tp_base` — one-request-at-a-time reference rate
- `tp_UB` — estimated high rate

- Component = `clamp(tp; tp_base, tp_UB)`. (problem.md:436)
- Semantics: "tpbase comes from a fixed one-request-at-a-time reference schedule; tpUB is
  an estimated high rate. At or below the baseline the component is 0; at or above the
  upper bound it is 1; between them it grows linearly." (problem.md:437-441)

- "final-token production time" per request = "the completion times of its output D POST
  tasks" (problem.md:443-445); FIN "appears beside the TDN of that request's final output
  final step" (problem.md:242), so the latest final-token time = latest FIN frame time.

### 1.3 Waiting-time component (problem.md:443-478)

- TDR target: "SLO1 is the target average wait until a request is ready for its first
  output step (time to decode ready, TDR)" (problem.md:443); "TDR is measured from arrival
  to this [P POST] completion" (problem.md:137).

```text
tdr = mean over all requests of (input stage final step completion − arrival)
tpot = mean of e_{j+1}−e_j over all consecutive output gaps pooled (1 ≤ j < Lout[i])
excess_tdr = max(0, (tdr−SLO1)/SLO1)
excess_tpot = max(0, (tpot−SLO2)/SLO2)
dist = sqrt(excess_tdr² + excess_tpot²)
waiting-time component = clamp(dist; dist_base, 0)
```

- `tdr` — mean input-stage wait over all requests
- `tpot` — pooled mean output gap
- `e_j` — j-th token time
- `SLO1` — TDR target
- `SLO2` — TPOT target
- `excess_tdr` — relative TDR excess
- `excess_tpot` — relative TPOT excess
- `dist` — Euclidean excess
- `dist_base` — reference amount above targets

  (problem.md:447-450). ALL requests — no Lout filter anywhere.

- "A request with Lout[i]=1 contributes no gaps. If no request
  contributes a gap, tpot is defined as 0." (problem.md:451-458)

- (problem.md:459-460, scrape flattens the LaTeX; the fractions are
  `(tdr−SLO1)/SLO1` and `(tpot−SLO2)/SLO2` and the root is a square root — confirmed by
  the written-out cliff form at 462-470 and by Example 1 arithmetic in §5).

- `waiting-time component = clamp(dist; dist_base, 0)` (problem.md:462), written out
  (problem.md:470):

```text
waiting = max(0, 1 − dist/dist_base) if dist_base > 0
waiting = 1 if dist_base = 0 and dist = 0
waiting = 0 if dist_base = 0 and dist > 0
```

- `dist` — Euclidean excess
- `dist_base` — waiting budget, 0 gives binary cliff
- `waiting` — waiting-time component

  - `max(0, 1 − dist/dist_base)` if `dist_base > 0`
  - `1` if `dist_base = 0 and dist = 0`
  - `0` if `dist_base = 0 and dist > 0`
- Special case emphasized: dist_base=0 makes the component binary (problem.md:472-478).

### 1.4 Composition (problem.md:485-486)

```text
NormalizedScore = w_tp·clamp(tp; tp_base, tp_UB) + w_c·clamp(dist; dist_base, 0)
Score = 1000·NormalizedScore
```

- `w_tp` — throughput weight
- `w_c` — waiting weight
- `tp` — output rate
- `dist` — waiting excess
- `NormalizedScore` — 0–1 blend
- `Score` — final 0–1000

### 1.5 Guarantees and edge cases the statement defines (problem.md:414-421, 279-283)

- `wtp, wc ≥ 0`, `wtp + wc = 1`, `SLO1 > 0`, `SLO2 > 0`, `tpUB > tpbase`, `distbase ≥ 0`
  (problem.md:414-419). "Either weight may be 0, but every request must still be
  completed" (problem.md:420-421).

- Parameter ranges: `0.001 ≤ SLO1,SLO2 ≤ 1e9`; `0 ≤ tpbase, distbase ≤ 1e9`;
  `1e-9 ≤ tpUB ≤ 1e9` (problem.md:279-283). tp_base may be exactly 0.

- Score bounds: "each completed test awards a score in [0,1000]" (problem.md:488).

### 1.6 Zero conditions (problem.md:366-390, 489-490)

- Zero on the test: protocol error, malformed output, stuck state, TLE (15 s per test,
  problem.md:36) / MLE (256 MB, problem.md:37). "No partial credit is awarded for an
  unfinished test." (problem.md:490). Other tests unaffected.

### 1.7 Aggregation (problem.md:74, 492-495)

- "Test 1 is the public worked Example 1, tests 2–22 are hidden preliminary tests, and
  finals use a separate frozen set." (problem.md:74)

- "The 22 preliminary tests provide feedback and do not contribute to the final ranking.
  The final score is the arithmetic mean of the 20 frozen final-test scores. Ranking uses
  that mean before display rounding; the displayed score is rounded to three digits after
  the decimal point." (problem.md:492-493). No hacks (problem.md:495).

---

## 2. ASSUMPTIONS.md §8 diff vs statement

`docs/ASSUMPTIONS.md:76-85` records the formula. Line-by-line:

| ASSUMPTIONS.md claim (line)                                                                                              | Statement                                                                                                                                                                              | Verdict                                                 |
| ------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| "tp = ΣLout / (latest final-token completion − earliest ARR)" (:78)                                                      | problem.md:433-435                                                                                                                                                                     | MATCH                                                   |
| "tp_comp = clamp = max(0,min(1,(tp−tp_base)/(tp_UB−tp_base)))" (:78)                                                     | problem.md:425, 436                                                                                                                                                                    | MATCH                                                   |
| "tdr = mean over requests … μ_tdr = Σ(ppost_i − arr_i)/R" (:79)                                                          | problem.md:447-450                                                                                                                                                                     | MATCH                                                   |
| "tpot = Σ_i(e_last,i − e_first,i)/(ΣLout − R) (0 if no gaps exist)" (:80)                                                | problem.md:451-458 (pooled per-gap mean; mathematically identical by telescoping — verified [INFERRED], §4.2)                                                                          | MATCH                                                   |
| "ε_tdr, ε_tpot, dist" (:81)                                                                                              | problem.md:459-460                                                                                                                                                                     | MATCH                                                   |
| "wait_comp = max(0,min(1,1−dist/dist_base)); if dist_base=0: 1 iff dist=0 else 0" (:82)                                  | problem.md:462-478 (statement's direct form omits min(1,·); equivalent since dist≥0)                                                                                                   | MATCH                                                   |
| "Score = 1000·(w_tp·tp_comp + w_c·wait_comp)" (:83)                                                                      | problem.md:485-486                                                                                                                                                                     | MATCH                                                   |
| "Example 1 … tp = 1/45 = tp_base → tp_comp 0; … score = 0.5·1000 = 500 ✓" (:84)                                          | Derived score is 500.000002759 with the 9-decimal input tp_base=0.022222222, because 1/45 = 0.0222222222222… > 0.022222222 [INFERRED, §5]. The claim is right to 8 significant digits. | MATCH (negligible imprecision)                          |
| Omissions: the §8 entry does not restate wtp+wc=1, tp_base may be 0, tp above tp_UB clamps to 1, or the 15s/256MB limits | problem.md:414-421, 280, 36-37                                                                                                                                                         | OMISSIONS only (recorded elsewhere in the file: §0, §1) |

No mismatch found in ASSUMPTIONS.md §8.

---

## 3. Local code audit — tools/interactor.py and tools/score_trace.py

### 3.1 Audit table

| #   | Item                          | Statement (problem.md)                                                    | Local code                                                                                                                                                                                                                                                            | Verdict                                        |
| --- | ----------------------------- | ------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| 1   | clamp primitive               | :425 `max(0,min(1,(x−base)/(target−base)))`                               | interactor.py:398 (`c_tp`), score_trace.py:130                                                                                                                                                                                                                        | MATCH                                          |
| 2   | tp numerator                  | :433 ΣLout                                                                | interactor.py:149,149 `tok_out += r["iter"]` (=Lout at FIN); score_trace.py:114                                                                                                                                                                                       | MATCH                                          |
| 3   | tp elapsed window             | :434-435 latest final-token time − earliest ARR                           | interactor.py:397 `tok_out/(last_finish−first_arrival)`; `last_finish` from final-D-POST completion (interactor.py:150,299), `first_arrival` = min ARR (interactor.py:124); score_trace.py:123-125 (uses latest FIN — equals latest final-token time, problem.md:242) | MATCH                                          |
| 4   | earliest arrival ≠ 0          | :434-435 (earliest request arrival, whatever it is)                       | verified with first ARR at t=100 [INFERRED] — window starts at 100, not 0                                                                                                                                                                                             | MATCH                                          |
| 5   | tdr definition                | :447-450 mean over ALL requests of (P POST completion − arrival)          | interactor.py:249 accumulate at P POST (`(t+S+dur)−arr`), :399 divides by `c.R` (all requests; `done==R` enforced :395); score_trace.py:101-105 over all R                                                                                                            | MATCH (includes Lout=1 requests)               |
| 6   | tpot pooled gaps              | :451-458 mean of e_{j+1}−e_j pooled, Lout=1 contributes none, no gaps → 0 | interactor.py:145-148 telescoping `sum_span=Σ(last−first token)`, `total_gaps=Σ(iter−1)`, :400 `sum_span/total_gaps if total_gaps>0 else 0`; score_trace.py:110-119 identical. Telescoping ≡ pooled-gap mean [INFERRED, §4.2: float diff ≤ 5e-10]                     | MATCH                                          |
| 7   | token times                   | :443-445 completion times of D POST tasks                                 | interactor.py:296-297 records `t+S+dur` at D POST assignment = the TDN time                                                                                                                                                                                           | MATCH                                          |
| 8   | excesses / dist               | :459-460                                                                  | interactor.py:401-403; score_trace.py:131-133                                                                                                                                                                                                                         | MATCH                                          |
| 9   | wait comp, dist_base>0        | :470 `max(0,1−dist/dist_base)`                                            | interactor.py:404-405; score_trace.py:134-135                                                                                                                                                                                                                         | MATCH                                          |
| 10  | wait comp, dist_base=0        | :470-478 binary, 1 iff dist=0                                             | interactor.py:406-407 `1.0 if dist==0.0 else 0.0`; score_trace.py:136-137. dist==0 ⟺ both excesses==0 exactly in IEEE                                                                                                                                                 | MATCH                                          |
| 11  | weights / final score         | :485-486                                                                  | interactor.py:408; score_trace.py:138                                                                                                                                                                                                                                 | MATCH                                          |
| 12  | unfinished test → 0           | :490                                                                      | interactor.py:395-396 `if done != R: return 0.0`; stuck raises → verdict 0 (:724, 790-792)                                                                                                                                                                            | MATCH                                          |
| 13  | protocol error → 0            | :366-388                                                                  | violation verdicts (:724), unparsable → ValueError → 0 (:790-792)                                                                                                                                                                                                     | MATCH                                          |
| 14  | TLE/MLE → 0                   | :383, 36-37                                                               | TLE: proc.wait timeout → "tle" verdict (:793-796). MLE: NOT enforced locally — `--measure-rss` only observes peak RSS (interactor.py:534-537); no 256 MB kill                                                                                                         | DEVIATION (harness scope, not formula)         |
| 15  | local extras not in statement | —                                                                         | EMPTY idle frames (case `EMPTY` markers, interactor.py:62-66, 651-655) — extra decision opportunities the real judge never sends; `--fin-first` line-order stress (:542-547); `--ghost` timestamp shifts (:521-527). None change the score formula                    | DEVIATION (test-feature, opt-in per case/flag) |
| 16  | degenerate guards             | statement guarantees tp_UB>tp_base (problem.md:282-283)                   | interactor.py:398 would ZeroDivisionError if tp_UB==tp_base; score_trace.py:129 guards (`c_tp=0` if tp_UB≤tp_base); interactor.py:397 tp=0 if last_finish≤first_arrival (unreachable: latency>0 forces last token > first ARR)                                        | UNVERIFIABLE on legal inputs (harmless)        |
| 17  | display rounding              | :492-493 display 3 decimals, ranking before rounding                      | score printed `%.3f` (interactor.py:789; score_trace.py:148)                                                                                                                                                                                                          | MATCH                                          |

### 3.2 Notable code-fidelity observations (beyond the formula)

- [FACT-code] The two historical judge defects are documented and fixed: D-048
  (docs/DECISIONS.md:1859-1866) — (1) TPOT denominator was ΣLout, corrected to ΣLout−R;
  (2) the trailing final TDN+FIN frame was not delivered (comment retained at
  interactor.py:641-646). D-075 (docs/DECISIONS.md:2790) — C_w corrected to
  clamp(1−dist/dist_base). Current code reflects both corrections and matches the
  statement.

- [FACT-code] Transfer FIFOs enter at the triggering task's computed completion
  (interactor.py:199, 225, 287, 331 with enqueue at `t+S+dur`), matching "queued when
  their triggering computation finishes" (problem.md:204); same-D-PRE UPs in increasing
  remote index (interactor.py:283). Simulation fidelity, consistent with the statement.

- [FACT-code] interactor.py applies a 1e-9 slack in time equality and frame coalescing
  (:352, 370) — float-safety, consistent with "exactly the same internal timestamp"
  (problem.md:241) up to double rounding.

- [FACT-code] `read_response` (interactor.py:411-426) has no read timeout; a solver
  hanging mid-protocol hangs the local judge instead of producing a tle verdict (tle only
  fires at the post-END `proc.wait`, :775). Harness robustness gap, not scoring.

---

## 4. Experiments [INFERRED]

Method: independent from-first-principles scorer
(`experiments/sa01/independent_scorer.py`) that parses the case file's scoring line and
the interactor's trace (`--trace-file`), and computes tp/tdr/tpot/dist/score using the
LITERAL statement definitions — per-request sorted token times with consecutive-diff
sums (not the telescoping identity), last-token time from D POST TDN events (not FIN),
earliest arrival from ARR events. Compared against `tools/interactor.py`'s reported
score and `tools/score_trace.py` on the same runs (solver = `build/submission`).

### 4.1 Twelve diverse real cases — three-way agreement

| case                    | interactor | score_trace | independent    | agree |
| ----------------------- | ---------- | ----------- | -------------- | ----- |
| zoo/example1            | 500.000    | 500.000     | 500.000002759  | yes   |
| zoo/single_k1           | 500.016    | 500.016     | 500.016128991  | yes   |
| zoo/decode_only         | 500.111    | 500.111     | 500.110929790  | yes   |
| zoo/burst_100           | 500.293    | 500.293     | 500.293252751  | yes   |
| zoo/mixed               | 500.253    | 500.253     | 500.252758510  | yes   |
| zoo/dribble             | 500.185    | 500.185     | 500.184996242  | yes   |
| zoo/transfer_bottleneck | 0.000      | 0.000       | 0.000029693    | yes   |
| zoo/cf19_prefillsteep   | 639.265    | 639.265     | 639.265214991  | yes   |
| adv/adv_degenerate      | 344.079    | 344.079     | 344.078681999  | yes   |
| adv/adv_x10_slo1e9      | 1000.000   | 1000.000    | 1000.000000000 | yes   |
| adv/adv_teeter          | 609.955    | 609.955     | 609.954551957  | yes   |
| adv/adv_void            | 1000.000   | 1000.000    | 1000.000000000 | yes   |

Full-precision variants (literal-gap vs telescoping tpot; token-based vs FIN-based elapsed) produced identical scores to 9 decimals on all 12.

```text
max |tpot_literal − tpot_telescoping| = 4.99e-10
max |tp_token − tp_FIN| = 0
```

- `tpot_literal` — per-gap consecutive-diff mean
- `tpot_telescoping` — span over gaps form
- `tp_token` — D POST TDN-based rate
- `tp_FIN` — FIN-based rate

Adv_x10 case gives zero score impact at SLO2=1e9. Traces: `/tmp/sa01/trace_*.txt`; script: `/tmp/sa01/precision_check.py`.

### 4.2 Five crafted edge probes (cases in `/tmp/sa01/probe_*.txt`)

| probe            | setup                          | interactor = score_trace = independent | statement-conformant behavior                                 |
| ---------------- | ------------------------------ | -------------------------------------- | ------------------------------------------------------------- |
| probe_lout1      | 5 requests, ALL Lout=1         | 500.042                                | n_gaps=0 → tpot:=0, no crash; tdr still over all 5            |
| probe_dist0_miss | dist_base=0, SLO1=1 (violated) | 0.036                                  | dist=24.67>0, dist_base=0 → wait comp 0; only tp term remains |
| probe_dist0_hit  | dist_base=0, SLO1=SLO2=1e9     | 700.036                                | dist=0 → wait comp 1 (w_c=0.7 → 700)                          |
| probe_wtp0       | w_tp=0, w_c=1                  | 1000.000                               | tp term vanishes entirely                                     |
| probe_late_arr   | first ARR at t=100, w_tp=1     | 0.025                                  | tp window starts at 100.0 (6/(342−100)=0.024793), not at 0    |

No mismatch on any of the 17 runs.

---

## 5. Example 1 hand computation (the public anchor)

Inputs from problem.md:499-500: K=1, S=1, latency=2, bw=1 Gb/s, bytes_per_token=125000,
num_layers=4; scoring line `SLO1=30 SLO2=15 tp_UB=0.0625 tp_base=0.022222222
dist_base=0 w_tp=0.5 w_c=0.5`. One request, ARR at t=0 (problem.md:504-506), Lin=4,
Lout=1. The local trace (`/tmp/sa01/trace_example1.txt`) reproduces the statement's
transcript event-for-event: P PRE TDN at 4 (dur 3), UP XDN at 10, P PROC TDN at 21
(dur 10), DOWN XDN at 27, P POST TDN at 30 (dur 2), D PRE TDN at 32, UP XDN at 35,
D PROC TDN at 40, DOWN XDN at 43, D POST TDN + FIN at 45 [INFERRED].

Arithmetic with symbols defined below:

```text
tdr = 30 − 0 = 30; mean over R=1 → 30
excess_tdr = max(0,(30−30)/30) = 0
Lout=1 → no gaps → tpot := 0
excess_tpot = max(0,(0−15)/15) = 0
dist = √(0²+0²) = 0; dist_base=0 and dist=0 → waiting = 1
tp = 1/45 = 0.02222222222… tokens/ms
clamp(0.02222222222…; 0.022222222, 0.0625) = 5.517e-9
NormalizedScore = 0.5·5.517e-9 + 0.5·1 = 0.50000000276
Score = 500.00000276, displays 500.000
```

- `tdr` — mean wait
- `tpot` — pooled gap mean
- `dist` — Euclidean excess
- `dist_base` — waiting budget
- `tp` — output rate
- `NormalizedScore` — 0–1 blend
- `Score` — final points

(If tp_base is taken as exactly 1/45 — the true reference-schedule value before 9-decimal rounding — c_tp is exactly 0 and Score is exactly 500.)

Local run: zoo/example1 → 500.000; independent scorer: 500.000002759. The statement
does not print a numeric score for Example 1, so 500 is the derived anchor; local and
statement arithmetic agree to <3e-6 points. ASSUMPTIONS.md:84's "verified on Example 1"
claim holds.

---

## 6. Fallacy candidates (leads recorded at program close — NOT falsified here; program completed 2026-08-23)

1. [ASSUMED] `docs/DECISIONS.md:448` (D-021): "TPOT is the POOLED mean of per-request
   mean gaps, weighted by `iter` (tokens)." Loose wording — the statement
   (problem.md:451-458) pools GAPS (weight = Lout−1 per request, not iter=Lout); a
   mean-of-per-request-means would weight each request equally and disagree whenever
   Louts differ. The CODE implements the correct pooled-gap mean, so this is a
   documentation-only hazard.

2. [ASSUMED] `docs/ASSUMPTIONS.md:255` (§1.8): "TPOT: Mean gap between consecutive tokens for a
   request." Omits "pooled over all requests" — per-request-mean reading is wrong
   (same hazard as #1; docs/ASSUMPTIONS.md:268 later states the correct pooled ΣLout−R form).

3. [ASSUMED] `docs/ASSUMPTIONS.md:84`: "tp = 1/45 = tp_base → tp_comp 0 … score = 500."
   With the input's 9-decimal tp_base, tp is 1e-10 ABOVE tp_base, so tp_comp = 5.5e-9
   and the exact score is 500.00000276, not 500 [INFERRED]. Negligible numerically, but
   it means "sitting exactly at tp_base" is not quite the null point when tp_base is a
   rounded decimal.

4. [ASSUMED] `docs/ASSUMPTIONS.md:79` and `docs/DECISIONS.md:2507-2508` (D-070 marginal values):
   `V_tpot = w_c/SLO2` "when dist_base<=0" (binary cliff) and generally
   `V_tdr = 1000·w_c·e_tdr/(dist_base·dist·SLO1)`. These are valid derivatives ONLY in
   the interior of the linear region; at dist_base=0 the waiting component is a step
   function (problem.md:470-478) with zero gradient a.e., so "V_tpot = w_c/SLO2" is a
   finite-difference heuristic to reach dist=0, not a statement-derived gradient. Fine
   as a steering heuristic; do not treat as exact score geometry near thresholds/clamps.

5. [ASSUMED] `docs/DECISIONS.md:571` (D-020 era): "Throughput = makespan = (latest
   final-token time − earliest arrival)". "Makespan" shorthand is safe only because all
   real tests appear to start arrivals at t≈0; the statement subtracts the EARLIEST
   ARRIVAL, not 0 (problem.md:434-435) — verified distinct in probe_late_arr. If any CF
   final test has a late first arrival, "makespan from 0" reasoning underestimates tp.

6. [ASSUMED] `docs/DECISIONS.md:2750` (D-075): "several norm_tp rows are inconsistent
   with tp/tp_UB, i.e. the local TPOT reconstruction family is not faithful" — a claim
   about local CASE reconstruction of CF tests (SLO/tp_UB guesses), not about the
   scoring function. Consistent with this audit; relevant to anyone fitting CF params.

7. [FACT-code, positive] The two historical judge bugs (TPOT denominator ΣLout; missing
   trailing TDN/FIN frame — DECISIONS.md:1859-1866) are FIXED in the audited code; any
   doc or memory asserting "local judge overstates/understates TPOT" predates D-048
   (2026-08-15) and is stale.

---

## 7. Bottom line

**The local judge's scoring function is faithful to the statement.** Across 17 runs
(12 real cases + 5 crafted edge probes), `tools/interactor.py` Sim.score
(interactor.py:393-408), `tools/score_trace.py` (:127-138), and an independent
first-principles scorer agree to display precision, and to ≤5e-10 in the raw tpot on
full-precision cross-checks. Every statement edge case behaves exactly as specified:
pooled-gap tpot with Lout=1 contributing nothing, tpot=0 when no gaps, dist_base=0
binary waiting component, zero weights, earliest-arrival-anchored tp window, tdr over
all requests. The authoritative formula retained for the archive is §1 above.

Watch out for:

- The local harness does NOT enforce the 256 MB memory limit (only measures), has no
  mid-protocol read timeout, and supports case-level EMPTY idle frames and stress flags
  (--fin-first, --ghost) that the real judge never produces — score research should use
  plain cases and remember ML/TLE enforcement is CF-only.

- Historical docs before D-048 (2026-08-15) describe the buggy judge; D-021's "mean of
   per-request mean gaps" wording and docs/ASSUMPTIONS.md §1.8's per-request TPOT phrasing are
  imprecise (code is right).

- Exact-arithmetic nit: with 9-decimal tp_base, a schedule sitting "at the baseline"
  scores epsilon above the nominal value (Example 1 = 500.00000276, not 500).

- Marginal-value formulas (V_tp/V_tdr/V_tpot) are exact only inside the linear regions;
  the dist_base=0 "binary cliff" V_tpot is a heuristic, not a gradient.
