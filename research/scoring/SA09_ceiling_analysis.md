# SA09 — Ceiling Analysis: Rigorous Upper Bounds on Achievable Score

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA09 drafting). READ-ONLY on src/, tools/, cases/; artifacts in
`experiments/sa09/` (ceiling.py, ceiling_full.csv, ceiling_table.txt, probe.py,
probe_results.csv), scratch in /tmp/sa09/. Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Baseline: `build/submission` (2026-08-23 tree,
117,016 B, same binary as SA07's scorecard — achieved values reused from
`experiments/sa07/scorecard_full.csv`, 95 cases, 0 errors).

Extends SA03 (solo reconstruction + resource floors), SA05 (loss ledger),
SA07 (scorecard + L12–L14). Everything new here: rigorous E lower bounds with
the transfer-volume verification, partition/atomicity-aware cloud bound,
per-token local bound, per-case score ceilings, a protocol-correct Python probe
solver used to validate the ceilings AND to attack the schedule, and the final
CF contested-pool mapping.

---

## 1. Bound derivations

### 1.1 Transfer-volume verification (the foundation)

**Verified from problem.md "Output Steps" + "Communication" and confirmed in
tools/interactor.py (enqueue_up/enqueue_down):**

- Prefill UP: 1 transfer per request, `len = Lin[i]` (P PRE completion).
- Prefill DOWN: 1 transfer per request (last P PROC piece), `len = Lin[i]`.
- Decode UP: D PRE completion queues **one transfer per distinct remote in the
  group, len = that remote's member count** (interactor: `for remote in
  sorted(groups): enqueue_up(..., len(mem), ...)`).

- Decode DOWN: D PROC completion queues **one transfer, len = this D PROC
  group's size** (the per-cloud sub-batch).

⇒ **Each member costs exactly bpt UP + bpt DOWN per round REGARDLESS of group
size.** Batching reduces only (a) the number of latency headers (≥1 per round
per direction instead of one per member) and (b) table durations (dpre/dpost/
dproc per group). A group split across k clouds pays k headers per direction.
This kills any "batching shrinks the link bill" hypothesis permanently.

### 1.2 The four E lower bounds (all valid for every legal schedule)

Let `n_rounds = max(maxLout, ceil(ΣLout/R))` (each request needs Lout_i
distinct groups; each group has ≤ R distinct members). Let `xfer(l) = lat +
8·l·bpt/(bw·1e6)`.

**(a) LINK**: every transfer occupies its direction's FIFO inside
[first ARR, last token]:

```text
UP_serial   = Σ_i xfer(Lin_i)  +  n_rounds·lat + ΣLout·8·bpt/(bw·1e6)
DOWN_serial = the same expression (prefill DOWN len=Lin; decode DOWN lens sum to group size)
E ≥ max(UP_serial, DOWN_serial)
```

- `UP_serial` — total UP link busy time
- `DOWN_serial` — total DOWN link busy time
- `xfer` — transfer time
- `Lin_i` — input length of request `i`
- `n_rounds` — decode rounds
- `ΣLout` — total tokens
- `E` — elapsed

**(b) CLOUD**: cloud j is busy with its P PROC + D PROC work; a D PROC group is
**atomic on one cloud** and a request's cloud is fixed at P PRE. Two valid forms:

```text
divisible:  E ≥ ( Σ_i pproc(Lin_i) + ΣLout · min_m dproc(m)/m ) / K
partition:  E ≥ min_{k∈[1..K]} (ΣLout/k) · r_min(ceil(R/k))
r_min(n) = min_{m≤n} dproc(m)/m
```

- `E` — elapsed
- `pproc` — prefill proc time
- `dproc` — decode proc time
- `K` — cloud count
- `R` — request count
- `r_min` — best per-token rate up to `n`

The partition form is certified by pigeonhole: for any assignment over k'
non-empty clouds, some cloud carries T_j ≥ ΣLout/k' tokens with n_j ≥
T_j/maxL ≥ (ΣLout/k')/maxL requests, hence busy_j ≥ T_j·r_min(n_j) ≥
(ΣLout/k')·r_min(ceil(R/k')). The divisible form alone is **badly loose on
flat-capped tables** (it credits per-token cost at m=R that only single-cloud
groups could achieve, then divides by K — m19b: 2,258 vs the certified 4,480;
fix worth +2× tightness).

**(c) LOCAL**: the local computer serially runs every P PRE, P POST, D PRE, D POST:

```text
local = Σ_i (S+ppre_i + S+ppost_i) + max(n_rounds·(min_m(S+dpre(m)) + min_m(S+dpost(m))), ΣLout·min_m((2S + dpre(m) + dpost(m))/m))
```

- `local` — total local busy time
- `S` — setup cost
- `ppre` — prefill PRE time
- `ppost` — prefill POST time
- `dpre` — decode PRE time
- `dpost` — decode POST time
- `n_rounds` — decode rounds
- `ΣLout` — total tokens

The per-token form kills the V-table illusion (x9: min at m=1 gives 2,200 vs
the true 21,200 — the old SA07 floor2 for x9 was 3.9× too low).

**(d) DEPENDENCY**: per-request solo pipeline (chunk-free prefill chain +
Lout_i × min-stage decode chain, stages minimized independently — rigorous):

```text
E ≥ max_i ( ARR_i − min_ARR + solo_pref(lin_i) + lout_i · chain_min )
```

- `E` — elapsed
- `ARR_i` — arrival of request `i`
- `solo_pref` — solo prefill time
- `lout_i` — output length
- `chain_min` — minimal per-round chain

`E_floor = max(a, b, c, d)`; piecewise-linear tables ⇒ every min over m ∈ [1,R]
is attained at an endpoint of {1} ∪ listed sizes ∪ {R} (a clamped piecewise-
linear function's ratio minima lie at breakpoints), so evaluating that finite
set is exact.

### 1.3 Ceiling score

```text
Score_ceiling = 1000·( w_tp·clamp(ΣLout/E_floor; tp_base, tp_UB) + w_c·C_w(tdr_floor, tpot_floor) )
tdr_floor = mean_i solo_prefill(lin_i)
tpot_floor = Σ_i (lout_i−1)·chain_min / G
```

- `Score_ceiling` — upper bound score
- `w_tp` — throughput weight
- `w_c` — waiting weight
- `E_floor` — elapsed floor
- `tdr_floor` — queue-free prefill mean
- `tpot_floor` — serialized span mean
- `G` — total gaps
- `tp_base` — reference rate
- `tp_UB` — high rate

```

Each aggregate is floored independently; the score is componentwise monotone
in (E, tdr, tpot), so this is a valid **upper bound on every achievable
score**. It is optimistic precisely where the floors are not jointly
achievable (e.g. queue-free tdr under link saturation).

**Validation**: ceiling ≥ achieved on **95/95 cases** (0 violations); and
ceiling ≥ every one of **43 probe-solver schedules** (§3) including several
that beat the artifact — the bounds survived every attack.

---

## 2. Full per-case table

`experiments/sa09/ceiling_full.csv` (95 rows), `experiments/sa09/ceiling_table.txt`
(sorted by gap). Headline numbers:

- **Total local contested (ceiling − achieved): 14,080.7 = tp-side 1,614.8 +
  wait-side 12,465.9.** The wait-side is solo-timing fantasy (SA07 already
  proved the tdr→floor lever unreachable on every sat0 case); **the honest
  contested mass is the 1,615 tp-side points.**

- TIGHT (E ≤ 1.02×floor): 14 cases incl. cf9_shape (1.000), cf10_shape (1.000),
  cf14_shape (1.005), cf19_prefillsteep (1.005), m17c (1.005), m17d (1.015),
  abyss (1.000), x1_blackhole (0.998→TIGHT by achieved), dribble, example1.

- Top tp-side gaps: **x9 +611** (floor 25,600 vs E 66,090), **m19c +277**,
  **m5_order +169 tp (+220 total)**, **living_cliff_infeasible +166**,
  adv_living_cliff +141, m19b +37, m6b +17, m6 +7.3.

Per-bound verdicts on SA05/SA07's "structural" calls:

| case                              | binding bound        | E/floor       | carries SA05/SA07 verdict?                                                                                                      |
| --------------------------------- | -------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| m6 / m6b (#5/#6 starve)           | local                | 1.042 / 1.111 | YES — even at the physical floor c_tp ≤ 0.198/0.173 vs tp_UB 20; on real #5/#6 windows full-floor E still buys only +11/+30 pts |
| cf9_shape (#9)                    | local(=arrival span) | 1.000         | YES — E done; D-153 proof stands                                                                                                |
| cf10_shape (#10)                  | dependency           | 1.000         | YES — E done                                                                                                                    |
| m15 (#15)                         | UP                   | 1.025         | YES — link-saturated                                                                                                            |
| m17c/m17d (#17 fat)               | UP                   | 1.005/1.015   | YES — link-saturated                                                                                                            |
| cf19_prefillsteep (#19)           | local                | 1.005         | YES (this shape) — but m19b/m19c (1.15/1.83) show the #19 family is NOT uniformly tight                                         |
| abyss/x1/x6/blackhole (#11 class) | UP                   | 1.000–1.03    | YES — no idle channel, pipelining dream dead                                                                                    |
| adv_degenerate                    | cloud                | 1.758         | partial — tp gap only 0.1; the 156 wait-gap needs span→floor (fantasy per SA07)                                                 |
| teeter/ambush/flip                | dependency           | 1.283         | NO — contention-blind (SA03 showed the tail is at its chain floor); gap overstated                                              |

---

## 3. Validation probes (the ceiling is honest; the artifact is not always)

Tool: `experiments/sa09/probe.py` — a protocol-correct Python solver (greedy /
mega=full lockstep re-merge / cohorts:C staggered pipelining / hold:T; optional
forced single cloud). 43 runs, all verdict ok, **none exceeded its ceiling**
(full log: `experiments/sa09/probe_results.csv`). Three headline results:

### 3.1 m5_order: a TRIVIAL policy beats the artifact by ~200 pts

mega lockstep: E 30,862 → 22,189 (−28%), score 779.9 → **979.0** (cohorts:2:
980.3). tdr 1,173 → 513, tpot 127 → 4,365 (the trade: spans explode, but
w_tp=0.9 and tp saturates). The artifact is ALREADY lockstep-optimal on
uniform pools (m6 mega = 270.843 = artifact 270.844 to 3 decimals) — **the
deficit is specific to mixed-Lout/order pools** where the artifact's caps and
straggler logic fragment the groups. Caveat: m5_order is the documented
wrong-sign family (D-152/D-153: every local starve win failed to transfer to
CF #5); this is mechanism evidence, not a CF prediction.

### 3.2 x9: single-cloud mega on a V-shaped table = +348 pts

Forcing all 2,000 requests onto cloud 0 with full-group lockstep: E 66,090 →
37,811, score 388.6 → **736.8**. The V-shaped table (dpre/dproc/dpost = 10 @1,
200 @10, 5 @4096) makes 2,000-member groups cheaper per token than any split;
spreading across clouds forces 250-member groups at 18.9× the per-token cost.
The artifact's cross-cloud spread (D-146/D-147 dynamic cap) is exactly wrong
on this shape. Realistic x9 max ≈ 750–780 (chain floor ≈ 36.2k for the single
cohort; the 25.6k local floor needs multi-cohort overlap that raises local
work). The lever is sharply gated: linear tables (x4) unaffected,
cloud-bound cases (m5_order c0: 300.3; x7: 243.8) are HURT by it.

### 3.3 Where probes LOSE (the artifact's real strengths)

Living cliffs (adv_living_cliff 201 vs artifact 734; adv_degenerate 149 vs
344), greedy fragmentation (burst_100 0.1), m19b/m19c (artifact's 8-way split
≈ optimal: probes −3 to −28). Any promotion of §3.1/§3.2 mechanisms MUST be
weight- and table-gated (w_tp-dominant, tp-linear, table-shape check) and pass
adv_living_cliff — the standing rule.

Also: m3 mega +13.4 (tdr −38% at tpot ×16 — pure-wait trade), m19 +3.1,
cf9_shape tie (E at floor), m6 tie.

---

## 4. CF prelim mapping — the final contested-pool table

Missing points from SA05's ledger (16,147.866 leader); "ceiling-recoverable" =
points the E-floor analysis permits, with the analogue evidence. tp-headroom
converted with SA03's fitted windows (points ≈ 1000·w_tp·tp·h/(tp_UB−tp_base)
for E-cut h).

| #   | missing | analogue (E/floor)                  | ceiling-recoverable                               | lever / verdict                                                                |
| --- | ------- | ----------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------ |
| 6   | 638.3   | m6b 1.111, m6 1.042                 | **+12 (h=4%) … +34 (h=11%); +107 IF order-class** | starve: tp_UB 2.09–3.33× physical; ~95% structural. m5_order lever conditional |
| 5   | 559.6   | m6 1.042                            | **+11 … +30; +96 IF order-class**                 | same                                                                           |
| 14  | 584.7   | cf14 dep-TIGHT (mimic loose)        | **0–71**                                          | only the 71-pt wait slice is concrete; no mechanism                            |
| 3   | 499.4   | m3 1.112; mega probe +13            | **0–25**                                          | pure-wait; tdr −37% needed; cliff tail −500                                    |
| 1   | 500.0   | tp below base, every protocol       | **0**                                             | reference unbeatable — dead                                                    |
| 2   | 500.0   | same                                | **0**                                             | dead                                                                           |
| 11  | 499.8   | abyss/x1/blackhole UP-TIGHT 1.00    | **0** (link-sat proven locally)                   | only the never-run CF discriminator could dissent                              |
| 10  | 315.9   | cf10 dep-TIGHT 1.000                | **0–25**                                          | E done; wait side needs 144× tdr cut                                           |
| 13  | 290.0   | m19b 1.149 (artifact already there) | **0–95** (h≤13%); +247 if order-class             | tp-linear; probes could NOT beat artifact on m19b/c                            |
| 9   | 264.0   | cf9 1.000 + D-153                   | **0**                                             | PROVEN structural                                                              |
| 4   | 211.2   | none faithful                       | 0–30                                              | unknown                                                                        |
| 12  | 195.9   | none                                | 0–100 (unquantified)                              | PURE-TP; no resident                                                           |
| 8   | 189.3   | none                                | 0–20                                              | unknown                                                                        |
| 19  | 120.7   | cf19 1.005 vs m19b 1.149            | **0–50**                                          | only if #19 is m19b-shaped, not prefillsteep                                   |
| 15  | 117.3   | m15 UP 1.025                        | 0–15                                              | link-sat                                                                       |
| 22  | 81.2    | x9 resident (probe +348 local)      | **0–73**                                          | x9-class V-table lever IF #22's tables are V-shaped/local-bound                |
| 17  | 55.0    | m17c/d UP-TIGHT                     | 0–15                                              | link-sat                                                                       |
| 18  | 83.8    | Lout=1 fam                          | 0–5                                               | exchange rate 4.5e-6 pts/ms                                                    |
| 21  | 29.7    | 99% UB                              | 0–10                                              | tail                                                                           |
| 16  | 21.6    | 97% UB                              | 0–10                                              | tail                                                                           |
| 20  | 1.8     | near-maxed                          | 0                                                 | —                                                                              |

**Total CF ceiling-recoverable: realistic core ≈ 50–150 pts; structure-
conditional upside (if #5/#6/#13/#19/#22 turn out order- or V-table-mixed)
≈ 350–580 pts.** Given the D-152/D-153 wrong-sign record on exactly these
families, the honest point estimate is the low end.

**Final priority order recorded at program close for remaining effort (prelim side; program completed 2026-08-23):**

1. **x9-class single-cloud/lockstep re-merge** — the only lever where a trivial
   policy beat the artifact by >300 local pts AND the ceiling permitted it.
   Recorded gate at close: table per-token costs minimized at m≈R AND local-bound E AND
   w_tp-dominant AND tp-linear. CF upside at close: #22 (≤81), #13/#19 tails.

2. **m5_order-class mixed-Lout re-merge** (+200 local, validated): same gates
   minus the table condition. CF upside at close: #5/#6 (≤100 each) — but this was the
   documented wrong-sign family: the recorded guidance was one weight-gated CF probe maximum, retire on
   loss.

3. **#14 wait slice (71)** — concrete but mechanism-less; only with a
   slack-guarded tdr cut.

4. Starve tails #5/#6 (+11..+34), #19 (≤50) — mined; only via levers 1–2.
5. Everything else ≤ 25/test — do not spend.
6. (Carried from SA05 §5, unchanged and still dominant over all of the above:
   frozen-set robustness — TLE/frame-count hardening protects up to 50 final
   mean points per prevented zero.)

---

## 5. Loose-bound caveats (where ceilings are optimistic)

1. **LINK bound ignores FIFO dead time** — transfers can only enter after
   their triggering computation finishes; on m17/m17b the UP bound (3.1k) is
   6.5× below achieved E (20k) while m17c/d (same family, fatter tables) sit
   at 1.005–1.015 — the bound is tight only when the link is genuinely
   saturated end-to-end.

2. **DEPENDENCY bound is contention-blind** — teeter/ambush/flip gaps (+31..34)
   are overstated (SA03: the tail is at its chain floor once the herd owns the
   local machine).

3. **Ceilings floor tdr/tpot/E independently** — wait-side "gaps" (12.5k of
   the 14.1k total) are joint-achieval fantasy; only the tp-side column is
   decision-relevant.

4. **Cloud/local bounds still relax ramp structure** — prefill/decode overlap
   on the local machine is all-serial in reality (x9's honest serial-local
   25.6k is tight; m19c's 1.83 gap over the cloud bound may partly be ramp,
   which the probes could not close either).

5. tpot_floor uses per-stage minima (m=1 chains); real spans in big groups run
   dproc(m) — the wait-side ceiling is additionally optimistic there.

## 6. Bottom line

- The transfer physics is now verified line-by-line: **batching never reduces
  transfer volume** — only headers and table time.

- Of SA05's 5,852 missing CF points, the rigorous floors leave **~50–150
  realistically recoverable, ≤ ~580 under the most favorable (order/V-table)
  structural hypothesis**; every big pool (#1/#2/#11/#9/#15/#17/#3-tp/#14-tp/
  #5/#6-tp) is now bounded structural by ceilings that survived 43 probe
  attacks plus the artifact itself.

- The artifact was ceiling-tight on uniform pools, link-saturated families and
  cliffs — but **lost ~200 pts to a trivial lockstep on mixed-Lout pools
  (m5_order) and ~348 pts to single-cloud lockstep on V-shaped tables (x9)**.
  Those two gated mechanisms were the only new evidence-backed levers at program close; both
  would have had to pass adv_living_cliff and the weight gates before any CF submission.

## Reproduction

```bash
python3 experiments/sa09/ceiling.py --csv /tmp/c.csv --table /tmp/t.txt
printf '#!/bin/bash\nexec python3 experiments/sa09/probe.py --policy "$PROBE_POLICY"\n' > /tmp/pb.sh
chmod +x /tmp/pb.sh
PROBE_POLICY=mega PROBE_CLOUD=0 python3 tools/interactor.py \
  --case cases/adv/adv_x9_above.txt --solver /tmp/pb.sh

```
