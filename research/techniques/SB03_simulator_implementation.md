# SB03 — Forward Simulator: Audit, Verification, Size, and Integration Spec

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB03). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
Scope (archival): simulator computations only; no local-score evidence, no repo
scoring/judging tooling used as evidence in this document (rev 2 — completed the power-cut PARTIAL session; the prior session
produced `SB03_forward_sim.cpp`/`SB03_sim_test.py` uncompiled and unverified).

Artifacts:

- `research/techniques/SB03_forward_sim.cpp` — the simulator (core `namespace
  sb03::Sim` + test driver under `#ifdef SB03_DRIVER`; judge-format trace under
  `#ifdef SB03_TRACE`).

- `research/techniques/SB03_sim_test.py` — build + verify + size + ledger
  driver. The previous revision's cross-check against `tools/interactor.py` was
  REMOVED (pivot mandate: repository judging tooling is banned as evidence);
  every check below is a statement-example replay or a self-constructed
  scenario.

Claim tags: **[FACT]** = problem.md line cited; **[MEASURED]** = number produced
by the commands in this doc on this machine (dev box, g++ 12); **[DERIVED]** =
consequence of the above.

---

## 1. AUDIT of the pre-existing SB03_forward_sim.cpp (defects found and fixed)

Clause-by-clause against problem.md. Defects are numbered; each cites the
problem.md line that proves the required behavior. Everything not listed as a
defect was verified correct (list at the end of the section).

**D1 (fatal, correctness) — journal rollback restored 8 bytes regardless of the
saved width.** `rollback()` executed `memcpy(e.p, &e.o, 8)` while `jw()` had
saved only `sizeof(T)` bytes (zero-padded). Restoring `Rq::ph` (int, 4B saved)
wrote 4 zero bytes over the adjacent `Rq::it`; restoring `ct[7].n` wrote past
the end of the `Task` array onto `Sim::seqc`; restoring `seqc` zeroed `narr`.
Consequence: any rollout containing those writes corrupted live state.
Fix: `struct US { void* p; uint64_t o; unsigned char w; }` — the entry carries
the exact width; rollback restores `e.w` bytes. Proof of fix: self-check (e)
(roll) failed bit-for-bit before the fix on every seed and passes after
(3 seeds, §4). No problem.md line (internal invariant: rollback must restore
the state bit-for-bit — SB02 §1.4).

**D2 (fatal, exactness) — transfers were enqueued at APPLY time, not at the
triggering TDN.** problem.md:133 "on completion, the … transfer is queued
immediately"; problem.md:206 "Otherwise transfers enter in interactor event
order; tasks started together and finishing together use assignment-line
order". Interactor event order for TDNs is (finish time, push-seq) — a
C1-task assigned *second* but finishing *first* enqueues its DOWN before the
C0-task's. Constructed counterexample to the old code: task A on C0 assigned at
t=0 finishing at 20, task B on C1 assigned at t=5 finishing at 10, DOWN xfer
time x<10: judge delivers B at 10+x then A at max(20,10+x)+x; the old sim
enqueued A first (apply order), delivering A at 20+x and B at 20+2x — wrong
FIFO order AND wrong timestamps. Fix: all transfer enqueues moved into
`tdn_effect()` (P PRE→UP len=Lin; D PRE→one UP per distinct cloud, increasing
cloud index; P PROC last piece→DOWN len=Lin; D PROC→DOWN len=m), which
`deliver()` invokes in (time, push-seq) order — judge-op-for-op. (The UP
direction was *structurally* safe even pre-fix: P PRE and D PRE both run on the
single local computer, which serializes completions in assignment order — but
the fix applies to both directions uniformly.)
Corollary fix (D2a): enqueue-while-reading-arena realloc hazard. `enq_up`
appends member ids to the arena `ar`; passing `&ar[T.off]` would dangle on
realloc. Added the `_a` arena-offset variants (no appends) plus a stack
snapshot for the per-cloud D PRE split.

**D3 — duplicate-id check missing on D PROC and D POST.** problem.md:379-380:
"Malformed group: m<1 or duplicate request ids" — the rule applies to every
group task, not only D PRE. Fix: shared `has_dup()` guard on all three group
shapes (reference build; the artifact variant drops it, §6).

**D4 — `#include <cstdio>` absent from the core** while the SB03_TRACE code
calls `snprintf` → the `-DSB03_TRACE` build did not compile (the file had never
been compiled). Fixed.

**D5 — unused variable `RunStat st;`** in main's greedy branch → `-Werror`
failure. Fixed (the strict flags now gate every build in the test driver).

**D6 — FIN emitted before its D POST TDN.** The statement's examples print the
TDN first, then the FIN: problem.md:535-537 (frame "45.000000000 / 2 / TDN E D
POST … / FIN 0") and :653-656 (TDN, FIN 0, FIN 1 at 35.500000000). Within-frame
line order carries no scheduling priority (problem.md:242), so the old order
was semantically void — but it broke byte-exactness against the published
transcript. Fix: the D POST's FINs now take push-seq strictly after the TDN
(`lt.seq` allocated first, `lt.fseq` after), reproducing the statement's order
bit-for-bit.

**D7 — `nev` was incremented without journaling**, so rollback did not restore
the state bit-for-bit (surfaced by self-check (e) after D1's fix). Fix: `nev`
is journaled (`jw(&nev, …)`, correct per-candidate H-cap semantics); a separate
cumulative counter `nevt` (never rolled back) serves the stress totals.

**Verified correct, no change needed** (each checked against the statement):

- startup/table parsing and interpolation order — exact hit, else `v1 + (v2−v1)·(b−b1)/(b2−b1)`, clamp below smallest/above largest [FACT problem.md:233].
- piece duration `(le−ls)/num_layers·pproc(Lin)` [FACT :149].
- piece legality (nonempty, ascending, gap-free, first at 0, last at num_layers) [FACT :138-147].
- only the LAST P PROC piece queues DOWN, `len = Lin` [FACT :151-152].
- P PRE→UP `len = Lin` "whether or not that remote computer is busy" [FACT :133].
- D PRE→per-cloud UPs "enqueued simultaneously in increasing remote computer index order", `len` = that cloud's member count, ids "a subsequence of your D PRE line" [FACT :164, :321].
- D PROC→one DOWN, `len` = group size, ids in the triggering group's order [FACT :165-166, :321].
- D POST may recombine across clouds after each member's DOWN XDN [FACT :167].
- FIFO discipline:

```text
finish = max(enter, tail_last_finish) + latency + 8*len*bpt/(bw*1e6)
```

  - `enter` — enqueue time; `tail_last_finish` — FIFO tail; `len` — transfer length.
  - Both directions independent [FACT :57, :206, :208].
- frame coalescing only for exactly-equal internal timestamps, strictly increasing across frames, "a later event is never moved to an earlier frame" [FACT :242].
- assignments in one response start simultaneously at `t`; one task per resource [FACT :308-309].
- schedule cost `S` once per task incl. once per piece/group [FACT :111-115].
- TDR booked at P POST completion [FACT :137]. token at D POST completion, FIN
beside the final D POST TDN [FACT :242, :333]; 9-decimal printing (`%.9f`);
the terminal score: tp=ΣLout/E, clamp ramp, pooled-mean tdr, pooled-span tpot
(`Σᵢ(e_last−e_first)/Σᵢ(Loutᵢ−1)`, 0 if no gaps), dist/wait forms
[FACT :425-495] — the sim's `sum_span/n_gap` telescopes to exactly the pooled
span quotient. Stuck detection mirrors [FACT :390-394] (unfinished work and no
pending cause ⇒ the frame loop reports "stuck").

---

## 2. COMPILE (mission item 2)

Both configurations build with the exact required flags, zero warnings:

```
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB03_DRIVER -DSB03_TRACE \
    -o /tmp/sb03sim  research/techniques/SB03_forward_sim.cpp      # clean
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB03_DRIVER \
    -o /tmp/sb03bench research/techniques/SB03_forward_sim.cpp     # clean
```

Warnings fixed en route: a multi-line `//` comment (backslash continuation,
`-Werror=comment`), unused parameter `keep_trace`/`s` in non-trace builds,
unused variable `st` (defect D5). The audit is backed by `-Werror` gating in
`SB03_sim_test.py::build()`.

---

## 3. STATEMENT EXAMPLE REPLAYS (mission item 3) — PASS, timestamp-exact

**Example 1** (problem.md:499-556, transcript :580-615). Two independent paths:

(a) In-binary hard-coded scenario → `EX1 PASS` (12 events across 11 frames,
TDR=30.000, token@45.000).

(b) File path: the Example-1 startup config, table, single ARR, and the
example's response schedule are written as a case + script; the sim's `case`
mode replays them and prints the judge stream. Diff against the statement's
input column (problem.md:580-615) — **byte-identical, all 34 lines**:

```
0.000000000          21.000000000         40.000000000
1                    1                    1
ARR 0 4              TDN C0 P PROC 0 4 0 0 10.000000000
4.000000000          27.000000000         TDN C0 D PROC 0 1 0 4.000000000
1                    1                    43.000000000
TDN E P PRE 0 0 3.000000000   XDN DOWN 0 500000 PRE 1 0   1
10.000000000         30.000000000         XDN DOWN 0 125000 DEC 1 0
1                    1                    45.000000000
XDN UP 0 500000 PRE 1 0      TDN E P POST 0 0 2.000000000   2
                     32.000000000         TDN E D POST -1 1 0 1.000000000
                                          FIN 0
```

(full linear listing = `EX1_WANT` in `SB03_sim_test.py`; the diff loop compares
line-by-line, first mismatch aborts; result "byte-identical stream (34 lines,
11 frames, 12 events)".)

**Example 2** (problem.md:622-658, two-cloud excerpt): mid-test state injected
(req 0 READY_D_PRE on C0, req 1's P POST running, finishing 16.5), the
excerpt's responses replayed → `EX2 PASS`: 11 events reproduce 16.500, 19.000,
22.000, 25.000, 27.000, 30.000 (TDN C1 + XDN DOWN 0 coalesced), 33.000,
35.500 (TDN + FIN 0 + FIN 1) exactly; the two UP transfers run one at a time in
cloud order (C0's at 22.000, C1's at 25.000), and the final D POST combines
results delivered by two different DOWN transfers at 35.500 — the properties
the excerpt exists to show [FACT problem.md:658].

---

## 4. SELF-CONSTRUCTED CORRECTNESS CHECKS (mission item 4) — all PASS

Output of `python3 research/techniques/SB03_sim_test.py --only self` (and the
sim binary directly):

```
FIFO PASS: 3 UPs complete in entry order at 11.000/12.800/17.800 (no overtake;
           tail-bound waits exact)
SPLIT PASS: D PRE -1 3 '2 0 1' -> UP C0(2x125000, ids 0 1) @7.000 first,
           UP C1(125000, id 2) @9.000 second; D POST @19.000 FIN x3, all 4 done
CHUNK PASS: 3 pieces dur 10.000000000 each (=(le-ls)/6*30), one DOWN (PRE)
           only after the LAST piece @39.000, TDR=41.000
TIE PASS: equal-duration pair -> ONE 7.000 frame with both TDNs, DOWNs in
           assignment-line order (9.000 C0 then 11.000 C1), UP+DOWN coalesce
           @11.000, 3/3 done
ROLL PASS: 50 journaled random actions rolled back bit-for-bit (checksum equal,
           journal sizes restored), run completed 120/120 in 806 frames   [x3 seeds]
```

- **(a) FIFO discipline** — three P PREs (Lin 10/1/5, xfer times 9/1.8/5 ms)
  enter in assignment order; deliveries at exactly 11.000, 12.800, 17.800:
  the second transfer's own start+service would be 5.800 but the FIFO tail
  holds it to `max(4, 11)+1.8` — completion order = entry order, no overtake,
  tail rule exact [FACT problem.md:57/206].

- **(b) per-cloud UP split** — one cross-cloud `D PRE -1 3 2 0 1`
  (C0:{0,1}, C1:{2}, cloud-1 member listed FIRST in the line): two UPs, C0's
  entering first (cloud-index order) with size 2·bpt = 250000 and ids `0 1` (a
  subsequence of the D PRE line, assignment order preserved), C1's second with
  125000 [FACT problem.md:164/321]. One D POST at 19.000 recombines both DOWN
  deliveries and FINs all three.

- **(c) chunking** — layers=6, pproc=30, pieces [0,2),[2,4),[4,6): TDN durs
  echo exactly `10.000000000` each (=(le−ls)/6·30), no DOWN until the LAST
  piece completes; the single PRE DOWN lands at 39.000 = 37+2 [FACT
  problem.md:149/151-152].

- **(d) tie/coalescing** — two cloud tasks assigned in ONE response with
  identical durations finish at the same double: ONE frame at 7.000000000
  holding both TDNs; their DOWNs enter in assignment-line order (9.000 C0,
  11.000 C1) [FACT problem.md:206]; an UP and a DOWN completing at the same
  instant coalesce into one 11.000000000 frame [FACT problem.md:242].

- **(e) rollback** — at frame 40 of a 120-request run: checksum snapshot, 50
  random LEGAL actions (random clouds, random group subsets, random piece
  splits, interleaved random `advance_to`) applied under the journal, then
  `rollback(mark)`: FNV-1a checksum over every mutable word (free times, task
  blocks, FIFO vectors incl. entry fields, arena, all per-request fields,
  oracle accumulators, counters) equal, journal depths restored to the mark,
  and the run then completes 120/120. Repeated on 3 seeds. This check is the
  one that caught D1 and D7.

---

## 5. DETERMINISM + PRECISION (mission item 5) and the 2M-event stress

**Why ε-divergence cannot flip event order except across exact ties.**
[DERIVED, from FACT problem.md:290 ("If you simulate ahead for planning, use
ordinary double-precision arithmetic with the piecewise-linear lookup and
transfer formula above") + the structure of the model:]

1. Every simulated timestamp is one of `t+S+dur`, `max(enter,tail)+xfer`, or a
   table lookup — each a small fixed expression over frame timestamps and table
   constants. Per-operation double rounding is ≤ 1 ulp ≈ 2.2e-16 relative; at
   the validator's 1e12 ms ceiling the absolute error of any simulated time is
   < 1e-3·(2.2e-16·1e12)… conservatively ≪ 1 µs, and at realistic horizons
   (≤1e6 ms) it is ≈ 1e-10 — three orders below the 9-decimal print resolution
   (1e-9 ms) [FACT problem.md:285-289].

2. **Exact ties are bit-equal, not near-equal.** The judge coalesces only
   exactly-equal internal timestamps [FACT :242]. Such ties arise from
   *identical arithmetic over identical inputs* (two tasks assigned at the same
   frame with the same table row, two transfers with the same tail and len).
   The sim computes the same expressions in the same order, so its tied times
   are bit-equal too, and the `|x−τ| < TIE_EPS` loops coalesce them — the tie
   test (d) constructs precisely this case. **TIE_EPS policy: treat |Δ| ≤ 1e-9
   as simultaneous, order within the frame by the deterministic secondary key
   (push-seq = assignment order, then cloud index), and never let within-frame
   order affect decisions (line order carries no priority, problem.md:242).**

3. **FIFO delivery order is fixed at entry** — entry order is our own output
   (assignment-line order; increasing cloud index within one D PRE) [FACT
   :164/:206] — so ε wobble can never reorder deliveries, only jitter
   timestamps below print resolution.

4. **Drift is non-accumulating by construction.** Before every decision the
   live state is re-anchored to *observed* events: TDN echoes `dur` [FACT :253]
   and its frame time is the completion time; XDN frame time is the delivery
   time; ARR is exact. The rollout is re-rooted at actual reality each frame,
   so prediction error is confined to one planning horizon (H events ⇒ H ulps
   ≪ TIE_EPS) and never compounds across the test. The only compounding channel
   is our own choices changing the future — optimization, not error. Residual
   risk (two candidates' rollout values differing by < ε) is handled by the
   commit-margin rail (SB02 §6.2), not arithmetic.

**2·10⁶-event stress** (`/tmp/sb03bench stress 2000 200000 <seed>`, K=8,
layers=32, singleton decode + 4-piece prefills = the max-frame regime;
R=2000, ΣLout=2·10⁵ is the statement maximum, problem.md:271):

| metric | seed 1 | seed 2 | limit |
|---|---|---|---|
| events (pass A, forward) | 1,623,004 | 1,623,520 | — |
| frames (pass A) | 1,013,462 | 1,013,720 | 2·10⁶ cap |
| sim-core time | 0.464 s | 0.463 s | — |
| **ns/event (sim core)** | **286.0** | **284.9** | — |
| µs/frame (sim core) | 0.458 | 0.456 | 7.5 worst-case budget |
| **extrapolated 2·10⁶-frame test** | **0.916 s** | **0.913 s** | **15 s** |
| peak RSS (getrusage) | 20.6 MB | 20.6 MB | 256 MB |
| rollouts (pass B) | 600 × 48.8 µs mean | 600 × 49.9 µs | — |
| journal checksum mismatches | **0** | **0** | 0 |

Total simulated events across both passes and both seeds ≈ 3.3·10⁶ ≥ 2·10⁶.
Caveats stated plainly: (i) pass A's 52 s *wall* is the TEST-ONLY greedy policy
scan (O(R) per frame over 10⁶ frames) — the artifact's base policy is the
solver's O(1) intrusive-list scheduler, not this driver scan; the sim-core
column isolates `advance_to`+`apply`. (ii) The 48.8 µs/rollout mean includes
the test-only per-rollout `sim_chk` verification (O(R)) and a 60 ms-horizon
`advance_to`; SB02's L2 estimate (~35-70 µs/decision) is consistent. (iii) The
ΣLout ≤ 2·10⁵ constraint bounds a legal test near ~1.6·10⁶ events for this
policy, so the 2·10⁶-frame cap was exercised by extrapolation at the measured
per-frame cost, not by a single 2·10⁶-frame run.

---

## 6. MINIFIED-SIZE LEDGER + FUNDING PLAN (mission item 6; archival pre-freeze plan)

Method: `SB03_sim_test.py` implements its own minifier mirroring
`tools/merge_submission.py`'s transforms (string-aware comment strip, blank
lines, one-space-per-brace reindent, and the `_RENAME` map **read** from that
file — nothing scoring-related is executed). Block sizes are the minified
bytes of exactly the lines the artifact carries today.

**Sim cost** (core = everything outside `#ifdef SB03_DRIVER/SB03_TRACE`):

| variant | minified | − reuse already in solver | NEW bytes |
|---|---|---|---|
| reference core (full judge mirror) | 13,724 | 1,097 (table get 428, xfer 74, score 530, sc fields 65) | **12,627** |
| minimal in-artifact core (legality + dup checks dropped — candidates are constructed legal, exactly like the live solver trusts its own scheduler) | 11,384 | 1,097 | **10,287** |

Component breakdown of the minimal core [MEASURED]: apply() 3,597; deliver()
1,215; tdn_effect() 974; enq helpers 994; journal 836; advance/next/active
613; xdn 305; inject 211; structs/fields/enums 1,607; reuse 1,097.

**Funding ledger** (artifact now 65,465 / 65,535 chars — **70 free**
[MEASURED on src/submission.cpp]; net = minified − mandatory stub residue):

| migration | block (file, lines) | minified | net |
|---|---|---|---|
| M2 D PRE hold → rollout (SB02 4a) | policies.cpp 310-391 `NoDelay+SloAwareDelay::hold_dpre` (incl. true_period) | 2,373 | **2,303** |
| M2b D PRE size → rollout (SB02 4a) | policies.cpp 220-308 `MarginalBatch::group_size` (SLO scan + Twist-1) | 2,486 | **2,396** |
| M3 P PRE cloud → rollout (SB02 4b) | policies.cpp 12-115 `choose` (mega gate + Tier-1 + EFT + PoTC) | 1,885 | **1,805** |
| M4 accumulate gate → rollout (SB02 4c) | scheduler.cpp 170-195 DPRE_ACCUMULATE block | 479 | **435** |
| M5 P POST arbitration → L1 form | scheduler.cpp 111-133 run_ppost block | 591 | **531** |
| M6 cliff/endgame constant block | Solution.cpp 258-284 traffic-aware cliff flip | 607 | **0 — do NOT delete** (load-bearing, D-050/D-115; listed only because the mission asked it measured) |
| **total deletable (M2..M5)** | | | **7,470** |

**The honest verdict (recorded pre-freeze): the full-mirror engine did not fit.** Even deleting
M2+M2b+M3+M4+M5 (7,470 + 70 free = 7,540 available) could not fund the minimal
core (10,287). Gap ≈ 2,747 (the ledger's final running balance is exactly
−2,747). Closing it required the **lean integrated engine**
(§7): drop FIN event emission inside the model (dead in-artifact, done is
booked at apply; ~350), merge the four enq helpers into one direction-flagged
function (~400), reuse `State`'s `Phase` enum instead of the private 17-value
enum (~250), journal `State::Request` fields directly instead of duplicating
`Rq` (~400), drop test-only `nevt/jclear` (~150), plus new minify patterns
(e.g. a `std::` strip pass, ~0.5-1 KB across the whole artifact). Lean target
≈ **8.0-8.7 KB NEW** — fundable only with M2+M2b+M3 deleted (6,504 + 70 =
6,574) plus M4+M5 (966) plus ~1.3-2.0 KB of new minify patterns.

**Order recorded pre-freeze that was designed to keep the artifact ≤ 65,535 at every step** (deletions had to
precede the engine's landing; every intermediate was to be a compilable artifact):

1. add minify patterns first (pure size, behavior-identical; verify by
   regeneration) → est. +1-2 KB free;

2. delete M2 + M2b (+4,699 net) — this was planned as one CF probe (D-PRE hold/size
   replaced by the *existing* MarginalBatch precompute caps + NoDelay as
   interim, OR held in the same probe as the engine if the coordinator accepted
   a composite);

3. delete M3 (+1,805);
4. land the lean engine + endgame F0 rollout (SB02 order: endgame first) —
   had to fit in accumulated free bytes at this point;

5. delete M4 (+435), M5 (+531) as their replacements passed CF.
The attribution problem was real and was flagged for the coordinator: steps 2-3
delete heuristics before the engine exists, so either (a) one composite
landing (violates one-mechanism-per-CF-probe, D-152's lesson) or (b) interim
artifacts between steps 2 and 4 run the degraded NoDelay+static-cap stack —
each option needs an explicit human decision. M6 is never deleted.

---

## 7. INTEGRATION SPEC (mission item 7; archival; no source edits were made under this program)

**Where the sim lives.** One struct `FwdSim` inside `namespace icpc2026`
(artifact; the sb03 reference stays in research/ as the exactness proof). It
owns ONLY hypothetical-branch state: `lfree/cfree[8]`, `Task lt, ct[8]`
(mirror of CurrentTask minus the spec strings), UP/DOWN FIFO vectors (`Xf`
append-only + head index), id arena `ar`, journal vectors, oracle delta
accumulators, and `lout_hat`. Immutable/shared (no copy): `Config`, table
lookup (`TaskTimeTable::get` — the sim's `dur` is dropped, reuse saves 428),
`transfer_time` (reuse, −74), `Oracle::projected_score` (reuse, −595 incl.
sc fields), `State`'s `Phase` enum (−250 vs the private enum).

**Undo journal vs State's intrusive lists — the key coexistence rule.**
Rollouts NEVER touch `State`'s `rl_*` intrusive lists or `Request` objects of
the live state. Two compliant designs:

- (A) reference design (what is verified here): the sim keeps its own `rq[]`
  phase array; the base policy inside a rollout is a *port* of
  pick_local/pick_cloud operating on sim state. Clean isolation; costs a
  duplicate per-request array (~1,607 bytes of structs) and O(R) scans.

- (B) lean design (recommended for the artifact): journal `State::Request`
  scalar fields (phase, iter, next_ls, cloud) directly with `jw`, and give the
  sim its own per-phase FIFO deques (append + head-index, journal-rollback-
  able, same pattern as up/dn) instead of borrowing the intrusive lists — the
  intrusive `prev_ready/next_ready` links are never journaled, so `State`'s
  hot path is untouched. This deletes the duplicate `Rq`/`Ph` machinery
  (~650 bytes) and keeps rollout pops O(1).
Both designs keep the live solver's lists valid at all times — a rollback
restores only sim-owned words (design B journals `Request` scalars, which are
plain values, not links).

**Per-frame re-anchor procedure** (the drift killer, §5.4):

1. read the frame; apply events to the live `State` (unchanged solver path);
2. `sim.anchor(state, t)`: set `now = t`; copy `local_/clouds_` busy_until+step
   (+ls/le/ids slice offsets); rebuild sim FIFOs from the live
   `TransferQueue::finish_times` tails (entry order = issue order, which the
   solver already tracks: `up_proj/up_deq` and the queue vectors); set sim
   phases = live phases; `lout_hat[i] = predict_lout(i)`;

3. `mark = sim.mark(); …rollouts…; sim.rollback(mark);` (or `jclear()` if the
   anchor itself is the mark root — the lean design marks BEFORE the anchor
   copy and rolls back to it, making the anchor free);

4. issue the winning assignments through the EXISTING `State::issue` path
   (legality of live output is guaranteed by construction from live state,
   never from the model — a model bug can only pick a worse candidate, never
   an illegal one, because the committed action is re-derived from live
   readiness).

**CPU governor hook.** Budget counter per SB02 §5.2: `B_total = 12.0 s`,
`steady_clock` sampled every 1024 frames; level ladder L0 (base heuristic) /
L1 (closed-form candidate compare) / L2 (H ≤ 48 rollout, futures {F0,F1}) / L3
(endgame-only, H ≤ 256, exact terminal). The sim's `nev` IS the per-rollout
H cap (journaled per candidate — restored on rollback). Hard cuts: T_e > 10 s
⇒ L2/L3 off; T_e > 12 s ⇒ L1 off; any decision point > 400 µs ⇒ finish at L1.
The governor changes only the LEVEL (quantized buckets, sticky-downward),
never the choice at a given level (determinism rail). The measured 286 ns/event
says a 48-event rollout costs ≈ 14 µs of sim time — the L2 estimate holds with
journal overhead (rollout incl. verification measured at 48.8 µs).

**Candidate sets per decision point** (SB02 §3.2, SB01 D1-D8):

- local E: {P_PRE head, P_POST head, D_PRE(m): m ∈ {1, m*, pool, pool/2},
  D_POST(m̄), idle} — members chosen as prefixes of the SB01-D7 priority order
  (SRPT-blend, Lout=1 demoted, projected-last escalated);

- cloud c: {P_PROC (SRPT pick, full piece), P_PROC (chunked variant, only if
  the `S<1 && layers>8` gate passes), D_PROC(m): m ∈ {1, m*, pool_c}, idle};

- P PRE cloud: {EFT-argmin, least-active, herd (most-active), 2nd-best-EFT}.
Commit rule: maximin over sampled futures, override the base choice only on a

> max(0.05 pt, 2 %) margin; emit the base choice otherwise (SB02 §2.4/§6.2).

**Memory rail for the 2·10⁶-frame tail.** The append-only `up/dn/ar` grow
monotonically (delivered entries stay). Compaction pass when a head index
crosses 4096: erase the delivered prefix, subtract the prefix length from all
`Xf.off` and from task `off` fields (O(queue), amortized ~free). Measured peak
without compaction: 20.6 MB at 1.6 M events — under the 256 MB limit but the
compaction is specified as mandatory at ≥ 1.5 M frames.

**Determinism rail.** Fixed-seed xorshift64 (seed = hash of config+table), no
`unordered_*` in any decision path, F1/F2 sampling draws from indexed state —
the judge's determinism license (problem.md:38) is conditional on our responses
being a pure function of the observed stream, and this rail preserves it.

---

## Reproduction

```
python3 research/techniques/SB03_sim_test.py            # everything (ALL PASS)
python3 research/techniques/SB03_sim_test.py --only ex     # Examples 1/2
python3 research/techniques/SB03_sim_test.py --only self   # checks (a)-(e)
python3 research/techniques/SB03_sim_test.py --only stress # 2M-event stress
python3 research/techniques/SB03_sim_test.py --only size   # sizer + ledger
```

No repository scoring/judging tooling or case suite is invoked anywhere.
