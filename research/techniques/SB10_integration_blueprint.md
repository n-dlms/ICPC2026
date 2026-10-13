# SB10 — Integration Blueprint: Heuristic-to-Computation Migration of `src/submission.cpp`

Program context (completed 2026-08-23; contest ended 2026-08-28, final rank #56 worldwide).
Prepared by Ntokozo Dlamini with assistant support (program SB10). Assistant work sped up
derivation/implementation/checks; all ideas, selections, and decisions are Ntokozo Dlamini's.
This was the FINAL deliverable of the SB01–SB09 program (archival pre-freeze plan): the phased,
byte-funded, correctness-proven engineering plan that was scoped to migrate the Codeforces
submission from hand-tuned heuristics to computed decisions. Post-contest record: P4 was completed
2026-08-26 and the P7 cliff controller later landed (see PROGRAM_STATUS.md).

Claim tags: **[FACT]** = problem.md line cited; **[MEASURED]** = number produced by
recorded commands on this machine (this session where marked ●, otherwise the cited
SB doc); **[DERIVED]** = algebra/arithmetic from facts; **[PLAN]** = this document's
engineering decision as recorded pre-freeze (archival).

Prerequisites read in full: problem.md; SB01–SB09 docs + fragments +
PROGRAM_STATUS.md; src/{Solution,scheduler.*,state.*,policies.*,protocol.*,
task_table.*}; tools/merge_submission.py; docs/HIDDEN_ATLAS.md and the CF judgement
protocols (contest feedback — the only permitted scorer).

---

## 0. EXECUTIVE SUMMARY (read this page first)

**What you have.** Nine verified computation modules: an exact forward simulator
(SB03, 286 ns/event, Example-1 byte-identical), an FD-proven score calculus
(SB04, max rel err 2.9e-10), a survival-conditioned inference layer (SB05), an
exactly-checked partition/partition DP (SB06, 1200/1200 brute), lot-streaming /
pipelining math with a wake-safety proof (SB07), an endgame-drain + binary-cliff
controller with a zero-violation adversarial record (SB08, 0/300k), and artifact
engineering: parser/writer/alloc/governor/size machinery (SB09).

**What stood in the way (as measured pre-freeze).** The artifact had **~70 spare chars** [MEASURED,
65,465–65,467 / 65,535]. Every module had to be *funded* by deleting the heuristic
code it replaced, in an order that kept the artifact compilable, under the cap,
and attributable — one mechanism per CF submission.

**The plan in one paragraph (archival pre-freeze plan).** Probe #0 was to submit the current tree to establish the
true CF baseline row-vector (D-158/D-159/D-161 were unattributed at the time). P0 was to land a
behavior-neutral enabler pack (minify patterns + renames + allocation-free hot
path + writer + stderr diet) freeing ~2.4–3.4 KB. **P1** was to land the lean forward
engine (F0 closed-system), CPU governor, endgame trigger, critical-path drain, and
take over the D PRE hold decision — retiring `SloAwareDelay::hold_dpre` and the
`DPRE_ACCUMULATE` gate (one CF probe). **P2** was to give the rollout the D PRE size /
composition decision (retiring `MarginalBatch::group_size`'s SLO-scan/Twist-1
brain). **P3** was to give it the P PRE cloud choice (retiring the four-layer `choose`
stack) powered by SB06's exact-split DP. **P4** was to swap the three Oracle marginals
for the exact SB04 trio + knife-edge rates + finite delay costs, dissolving six
magic constants. **That saturated the artifact.** The remaining modules (inference
upgrade, partition completion, cliff controller, pipelining) mutually excluded each
other byte-wise: exactly ONE fitted in the residual ~0.3–1.5 KB terminal pool
— this blueprint recommended the **cliff controller (SB08 core)** because a single
frozen-test cliff loss cost −50 final-mean points, and its SAFE-mode contract was
provably non-increasing. Full edit lists, byte ledgers, verification protocols,
CF attribution tables, and kill rules follow (all archival).

**New tooling delivered with this blueprint** (pre-freeze-clean, statement-derived,
zero repository judging code): `research/techniques/SB10_fuzz.py` — Example-1
byte-exact replay (PASS●), closed-loop legality fuzzing (500/500 smoke PASS●;
10^4-stream record §3.5●), determinism double-run (100/100 PASS●), and two
max-density legal stress streams with exact child-CPU accounting (§3.5●).

**Measured before writing a single line of the plan** (§3.5, §5-R2): on a
forced-singleton legal stream (478k frames) the CURRENT artifact burns
~10 µs/frame ⇒ linearly extrapolated **≈20 s at the statement's 2·10^6-frame
cap — a live TLE exposure the D-161 guard does NOT cover** (forcing the guard
on at frame 500 changed nothing: its FIFO-head swap targets the SPT scan, which
is not the hotspot on this class). Segment timers attribute ≈4.2 µs to decide
(dominated by `group_size`'s ~85 `cost(m)` interpolation evaluations per decode
issue), ≈3.4 µs to write+flush, ≈1.7 µs to event apply. A behavior-neutral
memoization of `cost()` (pure function of immutable config+table) was built and
validated: **−17 % CPU, transcript-identical on 40 fuzz streams** ● — it heads
P0's edit list below. This is the blueprint's first lesson: measure the artifact
under statement-legal worst cases BEFORE trusting any hotspot folklore.

---

## 1. DEPENDENCY GRAPH AND PHASED MIGRATION PLAN (archival pre-freeze plan)

### 1.0 Unified byte ledger (single source of truth; overlaps reconciled)

All numbers NET MINIFIED chars (merge_submission transforms applied), from the
cited SB ledgers. Where two SB docs measured the same block differently, the
SMALLER (conservative) figure is binding. Blocks inside another block's line
range are marked SUBSUMED (never double-counted).

| ID | Block (file, function) | Source ledger | Net B | Retired in |
|----|------------------------|---------------|------:|------------|
| D1 | `SloAwareDelay::hold_dpre` + `NoDelay` + `DelayPolicy` vtable plumbing (policies.cpp:310–391; includes SB04-L1 `0.15·dist_base`, SB08-C3 cliff branch, SB07-B `true_period`) | SB03 M2 | 2,303 | P1 |
| D2 | `DPRE_ACCUMULATE` gate incl. D-161 A1b target expr (scheduler.cpp:170–195) | SB03 M4 / SB06 M4 | 435 | P1 |
| D3 | `dynamic_cap` xfer_ratio ladder + link override (scheduler.cpp:211–236) | SB03 M5 / SB06 M5 / SB04-L5 | 531 | P1* |
| D4 | `tdr_urgent` shortcut (scheduler.cpp:118–120) | SB04-L2 / SB08-C6 | 155 | P1* |
| D15 | alpha<0.8 delay-selection split + xremote forms (Solution.cpp:188–194) | SB04-L6 | 216 | P1* |
| D5 | `MarginalBatch::group_size` SLO-scan + Twist-1 crossing body (policies.cpp:227–307; static-cap min() retained as L0 floor; includes SB04-L3/L4, SB08-C4/C5 — those ledger entries are SUBSUMED here) | SB03 M2b | 2,396 | P2 |
| D6 | `PrefillLoadBalanceCloud::choose` mega gate + Tier-1 + EFT/ratio-argmin + PoTC (policies.cpp:12–115; SB04-L9 `1.05` band SUBSUMED) | SB03 M3 | 1,805 | P3 |
| D11 | V11 inflight bases + `0.5·dist_base` headroom clamp (scheduler.cpp:93–104) | SB04-L7/L8/L10 | 654 | P4 |
| D12 | K=1 V_tpot unit error + cliff V_tpot jump fallback (policies.cpp:275–280) | SB04 audit / SB08-C4-part | 242 | P4 |
| D10 | `ewma_lout_`/`lout_s_`/`lout_c_`/`predict_lout*` machinery (state.hpp/.cpp) | SB05 §8 | 968 | P5 |
| D8 | `compute_alpha` dist_base==0 min-TDR early-out (Solution.cpp:81–98) | SB08 C1 | 460 | P7 |
| D9 | traffic-aware cliff flip + `[cliff]` stderr (Solution.cpp:258–284) | SB08 C2 | 1,117 | P7 |
| D7 | `AdaptiveChunk::next_piece` (policies.cpp:150–184) + `S<1&&layers>8` selection (Solution.cpp:180–182) | SB07 A | 1,184+212 | P8 |
| — | `pick_local` run_ppost arbitration block (scheduler.cpp:126–132) | SB03 M5′ | 591 | optional P4+ |

\* D3/D4/D15 are P1 *lever-pool* items: retired in P1 only if the byte ledger
requires it (§1.4 ladder); otherwise they fall to P2 (D3, composition-dependent)
or P4 (D4, D15 — superseded by the exact exchange-rate test).

Grand total deletable inventory: **12,678 B** (+591 optional).

New-module costs (net minified, AFTER a fragment-local `_RENAME` pass at the
SB06/SB08 observed ~10–15% shrink; raw figures in the source SB docs):

| Module | Raw B | Renamed B | Lands in |
|--------|------:|----------:|----------|
| Lean forward engine (design-B State-integrated, F0-only, no sampler/inject/FIN-emission, dup-check-free, ring journal) | 8,000–8,700 (SB03 §6 lean spec minus listed cuts) | 7,000–7,600 | P1 |
| CPU governor (ledger + quantized sticky level + hard cuts) | 300 | 260 | P1 |
| Endgame trigger (gap-survival bins uSuf[32] + p_alive query; EWMA fallback form −220) | 350 | 300 | P1 |
| Drain ordering (`cp[]` chains + star escalation + maximal-group rule, SB08 §1.3–1.5) | 550 | 480 | P1 |
| Rollout driver (candidate sets, commit-margin rail, work-conserving override) | 600 | 520 | P1 |
| D_PRE size/composition candidates + SB06 `m_cap` member-gap scan + EOQ thin-pool analytic | 1,700–2,200 | 1,500–1,900 | P2 |
| SB06 `optimal_split` DP (Theorem 1) + `J(k)` lifetime-cost wrapper | 2,355 | 2,000–2,300 | P3 |
| SB04 exact marginal trio + knife-edge one-sided rates + `delayCost{Ppost,Span}` + region helpers (sec A+B, minus 560 B existing `marginal_*` bodies it replaces) | 1,900 | 1,650 | P4 |
| SB05 inference — P5a core (buckets+suffstats+hierarchy+Mills+floors+λ̂+p_alive), P5b EM injection, P5c sd/total_work | 6,300/2,700/2,265+1,000 | 5,400/2,350/2,800 | P5 (conditional) |
| SB06 remainder: `hold_dpost` + `dpre_group_target` formal wiring | 2,050 | 1,800 | P6 (conditional) |
| SB08 cliff core: `tdr_slack`+`tpot_slack`+`classify_cliff`+SAFE contract (reserves tiers +1,490/+1,480 later) | 3,850 | 3,300–3,500 | P7 (conditional) |
| SB07 chunk tier + wake guards (+cohorts optional) | 3,540 (+3,560) | 3,150 (+3,150) | P8 (conditional) |

### 1.1 Phase table (archival; dependency-ordered; every phase was scoped as ≤1 CF probe)

| Phase | Name | Mechanism (single sentence for the DECISIONS entry) | Deletes | Adds (renamed B) | Net free Δ | CF probe? |
|-------|------|------------------------------------------------------|---------|------------------|-----------:|-----------|
| #0 | Baseline attribution | none (submit current tree) | — | — | 0 | **YES — first action** |
| P0 | Enabler pack | behavior-neutral size/perf engineering | — (stderr diet LOCAL_DIAG-wraps ~850 B) | −(2,400–3,400) | **+2,400–3,400** | optional insurance |
| P1 | Computation-driven holds + endgame | F0-exact rollout decides D PRE hold-or-issue everywhere and owns the endgame (trigger → critical-path drain → maximal groups); heuristics D1+D2 retired | 2,738 (+902 lever-pool) | 7,560 base / 6,610 all-levers (renamed) | −4,852 … **+500** | **YES** |
| P2 | Computed batch composition | rollout sizes/groups decode rounds (candidates ∩ m_tp member-gap cap; EOQ thin-pool analytic fallback) | 2,612 (D5+D15 if unspent) | 1,700 | **+700…+1,100** | **YES** |
| P3 | Computed cloud assignment | rollout over {J(k) argmin, EFT, herd, 2nd-EFT} with SB06 exact-split DP as the L1 evaluator | 1,805 | 2,150 | −350…−150 | **YES** |
| P4 | Exact score calculus | Oracle marginals → SB04 trio + knife-edge + delayCost; constants D11/D12 dissolved; rollout terminal upgraded | 896 (+591 opt) | 1,650 | −750…−160 | **YES** |
| P5 | Inference upgrade | survival-conditioned Lout posterior + λ̂ + p_alive replace EWMA/bucket means | 968 | 5,400 (P5a) | −4,400 | conditional |
| P6 | Partition completion | `hold_dpost` merge rule + formal `dpre_group_target` | ~0 left | 1,800 | −1,800 | conditional |
| P7 | Cliff controller | computed slack + concentration margins + SAFE behavioral contract | 1,577 (D8+D9) | 3,400 | −1,830 | conditional |
| P8 | Pipelining | computed chunk k\* + wake-proof guards (+cohort waves) | 1,396 | 3,150 | −1,750 | conditional |

Projected free-bytes trajectory (midpoint estimates, P0 = +2,900):
F₀=70 → P0 **+2,970** → P1 with all levers **−475 … +500** (the joint worst
corner — P0 at floor AND new at high estimate — additionally needs the optional
run_ppost retirement, +591, per the §1.4 closure rule) → banked after P1
≈ **0–500** → P2 **+700…+1,600** → P3 **−350…−150** → P4 **−750…−160**
(+591 option if unspent) ⇒ terminal pool ≈ **0.3–1.5 KB** ⇒ **exactly one
conditional module fits** (P7 recommended; see §1.9). This saturation is a
structural fact of the 65,535 cap, not a plan deficiency — the blueprint makes
it explicit so the coordinator chooses the last slot deliberately instead of
discovering it at `make submission` time.

### 1.2 PHASE #0 — Baseline attribution probe (archival; was to be done BEFORE any edit)

- **Edits**: none. Submit the current working tree artifact as-is.
- **Why**: the tree carries D-158/D-159 (weight-gated pipeline modes) and D-161
  (x9 pack + TLE guard) with CF attribution pending (docs/DECISIONS.md §0.5). Every later
  phase's per-test deltas must be measured against a row-vector that includes
  them, or attribution is confounded from day one.

- **Attribution rule**: informational. Expected movement vs sealed leader
  (16,147.866, commit a40d6f3): rows #16/#19/#22-class (pure-tp / near-saturated /
  R=2000-stress, the D-158/159 target weights) and nothing else.

- **Kill/revert**: if total < sealed leader − 40, revert tree to a40d6f3
  (`git checkout a40d6f3 -- src/`), regenerate, and treat THAT as the baseline;
  re-measure P0 byte ledger on the reverted tree before P1.

### 1.3 PHASE P0 — Enabler pack (archival; behavior-neutral; no CF probe required)

Single mechanism: "artifact engineering, zero behavior delta". Proof obligation:
byte-identical solver TRANSCRIPTS on the full fuzz corpus (§3 V4), not scores.

Edit list (file → change → est. freed minified B):

1. `tools/merge_submission.py`
   a. **P1 std::-strip pattern**: hoist `using namespace std;` into the prolog
      (before `namespace icpc2026`), strip `std::` prefixes token-wise OUTSIDE
      string literals. Collision proof obligation: grep merged TU for
      declarations named `vector|string|sort|min|max|swap|data|size|getline`
      inside icpc2026 — none exist today [MEASURED, SB09 §4.3]; re-run the grep
      after landing. Est. **+1,100–1,500**.
   b. **P5 include collapse**: 18 individual `#include<...>` lines →
      `#include <bits/stdc++.h>` (GNU G++23 judge: available). Est. **+230**.
   c. **_RENAME extensions** (word-boundary safe; none appear in string
      literals; none collide with std): `"Request":"Rq"` (+225),
      `"latency_ms":"q7y"` (+48), `"bandwidth_gbps":"q7z"` (+72),
      `"bytes_per_token":"q80"` (+78), `"num_layers":"q81"` (+48),
      `"pending":"q82"` (+30), `"duration":"q83"` (+30). Est. **+531**.
   d. DO NOT add SB09-P6 (assert-strip): `assert(parsed)` in state.cpp wraps
      `spec.parse(...)` which HAS SIDE EFFECTS — stripping would delete the
      parse. Pattern rejected by review [PLAN].

2. `src/scheduler.cpp` — A1/A2 alloc fixes: `vector<Assignment> out` →
   caller-owned fixed buffer (`Assignment out_buf[9]; int out_n` threaded
   through decide/pick_*), `vector<pair<int,int>> others` → `int arrays[8]`.
   Net ≈ **+90** and removes ~2 allocs/frame (harness-verified class, SB09 §3.3).

3. `src/state.cpp` / `src/state.hpp` — A7/A8: `vector<char> seen(K,0)` and
   `vector<int> cnt(K,0)` in `State::issue` → stack arrays `char seen[8];
   int cnt[8];`. Net ≈ **+60**, removes 2 allocs per D PRE issue.

4. `src/Solution.cpp` — writer: `i64` single-memcpy form (SB09 §2.3);
   drop per-digit `digit()` indirection. **+16**, 1.61× format speed.

5. `src/policies.cpp` — **cost() memoization** (VALIDATED ●, §3.5): inside
   `MarginalBatch::group_size`, wrap the `cost` lambda with an init-once
   `static double cmemo[6][4097]` cache keyed by (step, m); route the linear
   scan, the >M binary search, and the Twist-1 crossing evaluations through it.
   Pure function of immutable cfg+table ⇒ behavior-identical by construction;
   transcript-identical on the fuzz corpus. ≈ **−40 net chars**,
   ~1.7 µs/frame saved on the singleton-stress class.

6. `src/scheduler.cpp` + `src/policies.cpp` — **fast-mode extension**
   (wiring only; engages at the existing 1.2M-frame site, Solution.cpp:249):
   `set_fast_mode(true)` additionally makes `group_size` return
   `min(n_ready, precompute argmin cap)`, skipping both SLO scans. Same
   emergency-guard philosophy as D-161's FIFO-head swap: behavior-identical on
   every observed test class, divergence only beyond 1.2M frames where TLE is
   otherwise certain (measured ≈20 s extrapolation WITHOUT this, R2). ≈ +60 B.

7. `src/Solution.cpp` + `tools/merge_submission.py` — **stderr diet**: wrap the
   `[alpha]`, `[cliff]`, `[ORACLE FINAL]`, `[METER]` blocks in `#ifdef
   LOCAL_DIAG` (minifier strips them). Frees ≈ **+850** and eliminates the only
   stdout-pipe-adjacent risk (D-059 concern becomes moot). NOTE: this retires
   the local oracle-final hook — ACCEPTED post-pivot; that gate is retired
   evidence (§0.8). Dev builds keep everything.

P0 total: **+2,400 (conservative) … +3,400 (top)**; CPU effect measured:
~10 µs/frame → target ≤ 6 µs/frame on the singleton-stress class (memo −1.7,
writer/format −0.3…0.5, alloc removals −0.5…1.5, fast-mode tail flattening).

Verification: V1 compile matrix; V2 replay PASS; V4 transcript-equality A/B on
2,000 seeded streams (pre-P0 binary snapshot `/tmp/sub_pre_P0`); V3 loop 10^4 +
det + stress on the rebuilt artifact; V5 battery safety-only; V7 size green.
CF: optional single insurance submission (only if probe budget is comfortable);
otherwise P0 rides along with P1's probe (its behavior-neutrality is proven
locally by V4, which is a determinism statement, not a scoring claim).

Rollback: `git checkout <pre-P0 commit> -- src/ tools/merge_submission.py`;
regenerate.

### 1.4 PHASE P1 — Computation-driven holds + endgame (archival; THE ENGINE PLAN)

Mechanism sentence: *"Replace the hand-tuned D PRE hold heuristics and the absent
endgame policy with decisions computed by an exact deterministic forward
simulation of the closed system (statement-licensed, problem.md:38/:290),
governed by a hard CPU budget."*

Scope (deliberate): the rollout owns (i) the **D PRE hold-or-issue decision** at
every frame (base-policy fallback below margin), and (ii) **all decision classes
once the endgame trigger fires** (stream-believed-dry ⇒ system closed ⇒ F0 is
EXACT truth — SB02 §4d). Mid-test SIZE and cloud choice stay with the legacy
stack until P2/P3 (their bytes fund those phases; their behavior remains
CF-attributable separately).

#### 1.4.1 Edit list

New file region inside `namespace icpc2026` (inserted between `task_table` unit
and `state` unit so it sees Config/table/transfer_time and State's types; in the
layered tree: new files `src/fwsim.hpp` added to HEADERS+UNITS lists in
tools/merge_submission.py):

1. **`FwdSim` struct** (lean design-B per SB03 §7): fields mirror live branch
   state only — `lfree, cfree[8], lt, ct[8]` (busy-until/step/ls/le/off/n),
   UP/DOWN FIFO vectors `Xf{f,len,off,n,rc,up,pre}` + head indices, id arena
   `ar`, width-carrying undo journal `{void* p; uint64_t o; uint8_t w}` (SB03
   defect D1's fix is MANDATORY), oracle delta accumulators
   (`sum_tdr,n_tdr,sum_span,n_gap,tok,last_fin`), per-request shadow scalars
   journaled DIRECTLY on `State::Request` (`ph,iter,next_ls` — never the
   intrusive links). Reuses `TaskTimeTable::get`, `transfer_time`,
   `State::Phase`, `Oracle::projected_score`. Drops vs reference: FIN emission,
   dup-id checks (candidates constructed legal), F1/F2 arrival injection,
   `nevt/jclear`, trace. Methods: `anchor(state,t)` (per-frame re-anchor —
   FIFOs REBUILT from live tails each frame ⇒ append-only growth impossible,
   memory bounded by in-flight × horizon; SB03's compaction pass NOT needed),
   `apply(Act)`, `deliver(tau)` with transfers enqueued at TDN time in
   (finish,push-seq) order and D PRE per-cloud UPs in increasing cloud index
   (SB03 defect D2's fix is MANDATORY), `next_event_time()` O(K) min-scan,
   `advance_to(T)`, `mark()/rollback()`, TIE_EPS=1e-9 coalescing.
   Renamed est. **6,000–6,600 B**.

2. **Governor** (`Gov` struct): `steady_clock` sampled every 1024 frames, `B_total = 12.0 s`, hard cuts at 10 s / 12 s, per-decision-point 400 µs wall guard. **260 B**. (Wires next to the existing D-161 frame-count guard; the SPT-scan fast path stays.)

```text
F_hat_rem = max(2*n, 2e5) then linear decay past 1e6
```

```text
admission: stake_rate * c(L) < 0.8 * B_left / F_hat_rem
```

- `F_hat_rem` — estimated frames remaining; `n` — frames so far.
- `stake_rate` — fraction attempting level `L`; `c(L)` — per-decision cost; `B_left` — budget left.
- Sticky-downgrade levels L0 (base) / L1 (closed-form compare) / L2 (H≤32 rollout) / L3 (endgame H≤256 exact terminal).
3. **Endgame trigger** (SB05 §3.2/§4 distilled): log-binned RAW inter-ARR gap exceedance `uSuf[32]` + add-one smoothing + `p_alive(d)` posterior.

```text
endgame <=> p_alive(silence) < 0.05
```

- `p_alive` — gap-survival posterior; lever-cutable to EWMA-silence ratio (two scalars, −220 B) if the ledger demands. **300 B**.
4. **Drain ordering** (SB08 §1.3–1.5):

```text
cp[i] = solo_prefill_rem + FIFO_tails + (Lhat - iter) * P_gap(pool)
```

```text
m_drain = min(pool, m*)
```

- `cp[i]` — remaining-chain projection; star = argmax, star-first escalation on the ready-list pop order.
- `m*` — throughput-optimal group; maximal-group rule with singleton-break condition. Uses the static cap (`MarginalBatch::max_m_` equivalent) as `P_gap` surrogate until P2 wires the member-gap law. **480 B**.
5. **Rollout driver** (`rollout_decide`): candidate sets — local
   `{P_PRE-head, P_POST-head, D_PRE(m∈{1,m*,pool}), D_POST(m̄), idle}`,
   clouds `{P_PROC-SPT, D_PROC(m∈{1,pool_c}), idle}`; members = prefixes of the
   existing list order; sequential greedy across resources; value =
   `Oracle::projected_score` deltas on the simulated finals + idle-regret term
   `Σ_r λ̂_r·idle_r` (shadow prices from the D-141 busy accumulators — prevents
   V_S farming); commit rule: override base choice iff maximin-margin
   `> max(0.05 pt, 2%)` (SB02 §6.2); work-conserving override never idle-with-
   legal-work outside hold-class decisions (Dai Thm 2 rail); H caps per level.
   **520 B**.

6. Call-site rewiring:
   - `src/scheduler.cpp:158`: `bool hold = policies_.delay != nullptr &&
     policies_.delay->hold_dpre(state, t);` →
     `bool hold = rollout_should_hold(state, t, gov.level());`
     (gated: L≥1 evaluates; L0 ⇒ false = NoDelay semantics).

   - `src/scheduler.cpp:169–195` (D2 block) → deleted; accumulate intent is a
     rollout candidate (`D_PRE(pool+pipe_inflow)` vs drain-now), wake-safe by
     construction (pipeline events are deterministic model inputs).

   - `src/policies.cpp:310–391` (D1) → deleted; `policies.hpp` DelayPolicy/
     NoDelay/SloAwareDelay erased; `Policies::delay` member removed.

   - `src/Solution.cpp:188–190` (D15 selection) → removed (fold into levers if
     unspent); `Solution.cpp:241–284` cliff-flip block keeps its detection but
     `policies.delay = &no_delay` line becomes `g_force_no_hold = true;`
     consulted by `rollout_should_hold` (flip semantics preserved byte-for-byte
     until P7 replaces it).

   - `main()` loop: instantiate `FwdSim fwsim; Gov gov;` after `state.init`;
     call `gov.sample(frame_count)` each frame; `fwsim.anchor(state,t)` lazily
     on first rollout use per frame.

7. **Dev-build-only drift monitor** (`#ifndef PSGO_STATIC`): after each issued
   task's TDN arrives, assert `|sim-predicted − observed| < 1e-6`; counter to
   stderr meter. Never in artifact.

Byte ledger P1 (apply levers top-down until `make submission` green; RECORD
which fired in the DECISIONS entry):

```
AVAILABLE  = margin(~70) + P0(2,400–3,400) + D1(2,303) + D2(435)
           = 5,208 … 6,208                        [+ avail-levers below]
AVAIL-LEVERS (+): D3 531 | D15 216 | D4 155      = up to +902 (order D15→D3→D4)
COST-LEVERS (−): ring-journal −250 | trigger-EWMA −220
                 | drop drain-cp module −480     = up to −950
NEW(renamed), base scope = engine 6,000–6,600 + gov 260 + trigger 300
                           + drain 480 + driver 520
                         = 7,560 base … 6,610 all-cost-levers-cut
BALANCE: worst  (avail 5,208, new 7,560)                = −2,352
         all levers both sides (6,110 vs 6,610)         = −500
         P0 at mid (≥2,900 ⇒ avail ≥6,110+ΔP0)          = −200…0
         best   (avail-top 7,110, new-low 6,610)        = **+500 ✓**
CLOSURE RULE: apply COST-levers first, then AVAIL-levers in the order
         D15 → D3 → D4; each step re-run `make submission`. The ladder closes
         at any P0 outcome ≥ its conservative floor EXCEPT the joint worst-worst
         (P0=2,400 AND every cost-lever fired AND new at high estimate); that
         single corner requires either one extra small retirement (the optional
         run_ppost block, +591 — rollout owns P_POST/P_PRE arbitration from P1)
         or coordinator-approved composite funding of D5 (below).
GATE: proceed to CF probe only when green. If red after ALL levers,
      coordinator may authorize folding P2's D5 retirement into P1 funding
      (composite probe — costs attribution cleanliness; requires explicit
      written approval per §0.8 discipline).
```

#### 1.4.2 Verification (full protocol §3)

V1–V7 mandatory. Additional P1-specific gates:

- **Drift monitor zero** on all suites + 10^4 fuzz streams (dev build).
- **Wake/liveness proof instantiation**: the rollout introduces exactly ONE new
  hold class (D_PRE hold beyond the old heuristic's conditions). Its wake
  condition inherits the old block's pending-cause citation (`pipe>0` ⇒
  WAIT_INPUT_DOWN/P_PROC_RUNNING events in flight — SB07 W3 argument applies
  verbatim); the work-conserving override covers the no-cause case. Document in
  the DECISIONS entry by reference to SB07 §4.

- **Determinism**: det-mode 200 streams identical; governor level keyed to
  (frame/1024, burn bucket) — no wall-clock in decisions.

- **CPU**: stress extrapolation < 12 s; governor hard cuts exercised by a
  forced-threshold dev build (D-161 method).

#### 1.4.3 CF attribution rule (rows from docs/HIDDEN_ATLAS.md)

| Moves UP (expected) | Why |
|---|---|
| #19 (PURE-TP, 88 % UB, ×1.135 headroom), #21 (99 % UB), #22 (R-stress, 84 % UB) | endgame drain compresses the tail; E-channel is the only channel there (w_tp ≥ 0.5, huge dist_base ⇒ c_w saturated) |
| #5/#6 (TP-RACE at 30 % UB) | hold timing shapes pool growth; rollout prices congested truth vs uncongested `true_period` (SB02 §4a gap) — CAUTION: wrong-sign family (D-152), see kill rule |
| #14 (tightest dist_base 0.865), #20 (near-maxed) | hold/no-hold flips decide whether c_w survives; exact model beats `imminent==1` guessing |

Direction-neutral/watch: #3/#7/#9 (pure-wait — holds rarely fire; verify no
regression), #1/#2 (example-class — replay must stay byte-perfect).

**Kill/revert criterion**: revert to pre-P1 artifact commit if ANY of
(i) total Δ ≤ −10; (ii) any single row Δ ≤ −50; (iii) rows #3/#7/#14 lose
≥ 20 combined (c_w damage signature); (iv) any row that was ≥ 950 drops below
900 (top-end erosion). One tighten allowed (gate narrowed: rollout only when
`p_alive<0.05 ∨ pool≥8`) then re-probe once; second failure ⇒ park P1, restore
leader, revisit post-P2 carryover.

Rollback: `git checkout <pre-P1> -- src/`; regenerate; the deleted D1/D2 text
lives in git history.

### 1.5 PHASE P2 — Computed batch size & composition

Mechanism: *"Decode round size/composition decided by rollout candidates capped
by the exact member-gap feasibility `m_tp = max{m≤pool : P_gap(m,pool) ≤ SLO2}`
(SB06 §1.4, 120/120 brute-checked) with the Deb–Serfozo threshold + EOQ
thin-pool law as the L0 analytic fallback."*

Edits:

1. Extend `rollout_decide` D_PRE/D_PROC/D_POST candidates:
   `{drain(pool∩cap), drain(m*), drain(pool/2), hold-for-pipeline(known inflow
   only)}`; D_POST additionally offered the SB06 merge candidate
   (`G_loc = S+dpost(m0)+dpost(m1)−dpost(m0+m1)` priced vs
   `p_crit·V_E·Δ + n_span·V_S·Δ`) — the SB06 §3 closed form, 1,433 B raw, IF
   the ledger allows (else deferred to P6).

2. Add `m_cap` scan (SB04 §4 saturated regime; integer scan cached at startup
   like `MarginalBatch::precompute`; V-table crossings found by the scan, not a
   breakpoint assumption) — ~700 B renamed.

3. L0 analytic fallback (used at governor L0/L1): thin-pool `q* = clamp(√(2λ̂c0),
   1, pool)`, critical-load `q_crit = λ̂f0/(1−λ̂a)` — 150 B; λ̂ from decode-ready
   EWMA already tracked.

4. Delete D5: `policies.cpp:227–307` (group_size body → `return
   min(n_ready, max_m_[col]);` one-liner retains precompute as the L0 floor),

   + D15 if unspent, + D3 if unspent (cross-cloud fill cap becomes a rollout
   composition property; the ladder dies).

5. `MarginalBatch::group_size` call sites unchanged (signature stable).

Ledger: deletes 2,396 (+531+216 unspent levers) vs adds ~1,500–1,700 ⇒
**net +700…+1,600 FREE** (banked for P3/P4).

Verification: V1–V7; PLUS a statement-derived unit check: the `m_cap` scan vs
brute-force `P_gap` evaluation on 10^4 random tables ported from
SB06_check.py's sampler into a standalone g++ test under research/techniques/
(one-command, no repo tooling) — rerun and paste output in DECISIONS.

CF attribution: UP: #20 (tpot-sensitive near-max), #14 (SLO2=356 race), #17 (dist 462 — batch period dominates), #4/#8 (mixed), #22 (R-stress batching).

Watch/kill-fast: #5/#6 (wrong-sign family — ONE probe max per D-152 doctrine; any −30 ⇒ immediate revert, no tighten), #1/#2 (example-class neutrality via replay).

Kill/revert: total Δ ≤ −5 or any row −40 ⇒ revert. Tighten once (cap-only, no
hold candidates) then park.

Rollback: `git checkout <pre-P2> -- src/`.

### 1.6 PHASE P3 — Computed P PRE cloud assignment

Mechanism: *"Cloud choice = argmin over {SB06 lifetime cost J(k) (exact-split DP
evaluator), EFT, herd, 2nd-EFT} evaluated by rollout; the four-layer heuristic
stack (mega gate / Lin≥256 isolation / EFT ratio-argmin / PoTC band) is retired
and its duties are subsumed by computation."*

Edits:

1. Insert SB06 `optimal_split` DP (Theorem 1: threshold search over sorted
   `U={S+dproc(c)}` + reachability DP; monotone-interval fast path) + `PLTable`
   glue reusing `TaskTimeTable::listed/get` — 1,755 B raw.

2. Insert `pick_cloud_rollout`: candidates per cloud `{herd-most-active,
   least-active, EFT-argmin, J(k)-argmin}`; J(k) = `T_prefill(k) +
   L̂out_r·c_dec(k) + Σ_mates L̂out_j·Δc(j|k)` with `c_dec` from the DP —
   ~550 B; evaluated at L1 by J(k) directly, at L2+ by rollout through the
   request's first token (H≈64 events, fires ≤R times/test ⇒ ~56 ms/test,
   SB02 §4b).

3. Delete D6 (policies.cpp:12–115) + the `PrefillLoadBalanceCloud` class;
   `policies.cloud` call site → `pick_cloud_rollout(state, r, t)`.

4. `mega_regime()` (state.cpp:435–473) RETIRED AS A GATE — its duty (detect V
   tables ⇒ herd) is a DP outcome; the strict `d_last<d1` signature stays ONLY
   as a comment-reference (D-161 evidence) — actually DELETE the function; the
   DPRE_ACCUMULATE target consumer died in P1.

Ledger: deletes 1,805 vs adds ~2,150–2,300 ⇒ net **−350…−150** (covered by P2
bank). Verification adds: DP-vs-brute standalone rerun (SB06 sampler, 1,200
instances) pasted in DECISIONS.

CF attribution: UP: #19/#12/#16 (pure-tp geometry), #6 (w_tp .9), #13, #22;
also the x9-analog class (probe solver showed 388→737 headroom on that shape —
if a prelim row is V-shaped it will move visibly). Watch: #3/#7 (pure-wait —
assignment shifts tdr), #14. Kill/revert: any row −40 or #14 −20 ⇒ revert
(cloud choice is permanent per request; wrong herds are unrecoverable — the
strictest kill of the program). One tighten (restrict rollout to
`w_tp≥0.5 ∧ K>1`, else keep legacy EFT) then park.

Rollback: `git checkout <pre-P3> -- src/`.

### 1.7 PHASE P4 — Exact score calculus + constant dissolution

Mechanism: *"Oracle marginals replaced by the FD-verified exact forms
(V_E, V_P, V_S with knife-edge one-sided rates and finite delayCost objects);
six magic constants dissolved into computed thresholds."*

Edits:

1. Swap bodies: `Oracle::marginal_tp_v/tdr_v/tpot_v` → SB04 sec-A exact trio
   (region catalogue incl. exact zeros; `tp==tp_base` one-sided-full convention;
   `d>=dist_base` boundary recovery direction). Fixes the three headline
   wrongnesses: dist==0 knife-edge zero-marginal, K=1 V_tpot 1000× unit error
   (D12 deleted), cliff V_tpot jump priced 0 when ε₁>0 makes recovery
   impossible (D12).

2. Add `delayCostPpost(n,δ)/delayCostSpan(n,δ)` finite forms; wire into
   `rollout_should_hold` and P2's hold candidate pricing (finite crossings out
   of slack now priced exactly — SB04 finding #1).

3. Dissolve D11: `scheduler.cpp:93–104` inflight bases + `0.5·dist_base`
   headroom → admission test `waitLoss(Δdist) ≤ V_E·ΔE_effect` with
   `dE_effect(min(Δ, F*−F₂))` (SB04 §1.5); window base
   `A* = max(1,(P_round−kℓ)/β)`.

4. Dissolve D4 if unspent: P_POST-vs-P_PRE arbitration →
   `runPpostFirst(a_i,q_j,κ) = V_P↑·((a_i+S)−(S+q_j)κ) > 0`, κ from cloud
   backlog (SB04 L2 exact form); optionally retire the M5′ run_ppost block
   (+591) if the arbitration moves wholly into the rollout — coordinator flag.

5. Rollout terminal upgraded: projected finals use R̂-aware pooled means
   (R̂ = arrived + p_alive-weighted expectation; horizon-free conditioning per
   SB08 §3.5) — kills the survivorship bias in `projected_tdr`.

Ledger: deletes 654+242 (+155+591 options) vs adds ~1,650 ⇒ net **−750…−160**
(covered by bank).

CF attribution: UP: #3 (+1.034 pts/ms of TDR — T-EXCH; knife-edge pricing is
exactly this row's lever), #14 (+2.47 pts/ms, 28.9 ms headroom), #9, #12
(norm_c 0.413 recovery), #10/#20 sharpening. Watch: everywhere (marginals feed
every gated decision — widest-blast-radius phase; the battery + fuzz gates carry
extra weight here).

Kill/revert: total ≤ −10 or any row −40 ⇒ revert. No tighten (these are exact
forms; there is nothing to tune — a loss means a wiring bug: find it locally via
the FD self-check rerun before considering a re-probe).

Rollback: `git checkout <pre-P4> -- src/`.

### 1.8 PHASES P5–P8 (conditional; mutually exclusive after P4)

Post-P4 projected free ≈ **0.3–1.5 KB** (§1.1 trajectory; midpoint ≈ 0.7–1.1 KB
including the optional run_ppost pool). Each of
P5a/P6/P7-core nets −1.8…−4.4 KB ⇒ **at most ONE lands** unless new pools appear
(they do not, short of retiring load-bearing detectors). Decision rule for the
human: pick by (expected frozen-set points)/(byte × risk):

| Option | Net cost | Value case | Risk |
|---|---|---|---|
| **P7 cliff controller (RECOMMENDED)** | −1,830 (after D8+D9) | Frozen sets historically harder; one binary-cliff zero = **−50 final-mean points**; SAFE contract proven non-increasing (SB08 §4.2); 0 violations / 300k adversarial trials | classifier inputs (ranges) conservative ⇒ occasional missed NORMAL (bounded loss) |
| P6 partition completion | −1,800 | D_POST coalescing gains on fragmenting tails | medium; needs p_crit proxy quality |
| P5a inference-lite | −4,400 (over budget ⇒ requires trimming to ~3.3 KB: posterior mean only) | diffuse (better L̂out feeds everything) | estimator regressions are silent |
| P8 chunk tier | −1,750 | narrow regime (S<1 ∧ layers>8) | wake-guard complexity |

If P7 chosen (recommended): edits = insert `tdr_slack/tpot_slack/classify_cliff`

+ SAFE contract (prefill-absolute priority, `m=min(pool,m_tp)` cap, SRPT-on-
remaining-gaps order, no-hold/no-chunk) gating `pick_local` and the rollout
level ceiling; delete D8+D9 (the `compute_alpha` early-out and the traffic-aware
flip — both superseded by the computed mean-based feasibility). Hoeffding
reserve tier (+1,490) only if a subsequent pool frees. CF attribution: any
row with recoverable norm_c (#12 0.413, #4 0.944, #10 0.629, #13 0.829) and
protection of #14/#3; kill rule: ANY norm_c-loss signature on any row ⇒ instant
revert (this module exists solely to protect c_w).

### 1.9 Sequencing calendar (archival; pre-freeze dates: Sun Aug 23 start; contest ended Thu Aug 28 11:00 UTC)

| Window | Action |
|---|---|
| Aug 23 (Sat) | Probe #0 (baseline attribution). Land P0 + full verification. |
| Aug 23 night – Aug 24 (Sun) | P1 implementation + levers + verification; CF probe P1 (submission #2). Attribution read; keep/revert. |
| Aug 24 (Sun) – Aug 25 (Mon) | P2 implement + probe #3. P3 implement + probe #4. |
| Aug 25 (Mon) – Aug 26 (Tue) | P4 implement + probe #5. Bank read. Choose last slot. |
| Aug 26 11:00 UTC (**48-h rule starts**) | LAST new-mechanism probe (chosen conditional module) allowed BEFORE this line; after it, only reverts. |
| Aug 27 (Wed) | Revert-to-leader only; rebuild identity checks; regenerate + full §3 protocol on the final artifact. |
| Aug 28 (Thu) morning | Sealed final submission ≥ 2 h before 11:00 UTC; verify verdict row appears; STOP. |

Submission budget: 6 planned probes (#0, P0-insurance optional, P1–P4, one
conditional) + up to 3 tighten-retries + revert-resubmits. Historically ample.

---

## 2. THE FILE MAP — resulting `src/submission.cpp` architecture

Post-program layout (one TU, outer `namespace icpc2026`, global-scope `main`),
estimated minified chars INCLUDING the P0 patterns and all P1–P4 landings
(conditional module excluded / included shown separately):

| # | Module (origin) | Content | Est. B |
|--:|-----------------|---------|-------:|
| 1 | includes + flags + usings | `<bits/stdc++.h>`, baked FLAGS (now incl. `FWSIM`), `using namespace std;` | 240 |
| 2 | Config/EPS/transfer_time (protocol.hpp) | unchanged | 380 |
| 3 | FastScanner/ProtocolReader/task_table (protocol/task_table units) | parser (timestamp9 fast-path optional), table interp | 1,650 |
| 4 | Writer (Solution.cpp) | BufWriter + i64_opt | 620 |
| 5 | State core (state.*) | Request/CurrentTask/TransferQueue/intrusive lists/PPreBuckets/apply_event/issue | 6,600 |
| 6 | Oracle + ScoreCalc (state.hpp + SB04 sec A/B) | running aggregates + exact marginals + delayCost + projections | 1,700 |
| 7 | FwdSim engine (SB03 lean) | anchor/apply/deliver/enqueue/advance/journal(TIE_EPS) | 6,300 |
| 8 | Governor + endgame trigger + drain (SB02 §5, SB05 §3–4, SB08 §1) | budget ladder, p_alive, cp[] ordering | 1,050 |
| 9 | Rollout driver (SB02 §3) | candidates, rails, commit margin, level dispatch | 900 |
| 10 | Batch composition (SB04 §4 + SB06 §1.4 subsets) | m_cap scan, EOQ/critical laws, merge rule (if landed) | 1,400 |
| 11 | Cloud partition (SB06 Thm 1 + J(k)) | optimal_split DP + pick wrapper | 2,200 |
| 12 | Scheduler (scheduler.cpp residual) | decide/pick_local/pick_cloud shells, SPT+fast path | 3,300 |
| 13 | Policies residual (policies.cpp) | MarginalBatch precompute + one-line group_size + OnePieceChunk | 1,300 |
| 14 | main() (Solution.cpp residual) | startup, regime gates (compute_alpha cores), frame loop, output | 2,600 |
| 15 | (conditional) cliff controller OR partition-completion OR inference-lite | §1.8 choice | 3,300–3,500 |
| — | **TOTAL (P0–P4, no conditional)** | | **≈ 65,200 ± 400** |
| — | **TOTAL (with P7)** | | **≈ 65,300 ± 400** (fits only with lever surpluses realized; gate is `make submission`) |

Residual heuristics kept DELIBERATELY (rollout base policy / L0 floors):
compute_alpha's X10 worst-case sufficient bound (Solution.cpp:111–127 — exact
sufficient detector, SB04 audited EXACT); MarginalBatch::precompute static caps
(L0 batch floor); OnePieceChunk (SAFE-mode chunk policy); SPT prefill pick +
TLE fast path; the cliff-flip detection predicate until P7 replaces it;
intrusive ready lists (the rollout's member-order source).

---

## 3. CORRECTNESS PROTOCOL (no scoring tools — statement-only)

Standing pre-submission checklist, executed by the implementing pair for EVERY
phase; results pasted into the phase's DECISIONS.md entry.

- **V1 Compile matrix**: `make build/solution build/submission san debug` all
  clean (`-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror`). Artifact ALSO
  compiled standalone: `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -o
  /tmp/artchk src/submission.cpp`.

- **V2 Example-1 replay (ground truth)**:
  `python3 research/techniques/SB10_fuzz.py replay --solver build/submission`
  — feeds the statement's interactor column, diffs every response against the
  statement's response column. Required: `PASS (byte-identical responses,
  11 frames)`. This is the ONLY transcript-level oracle that survives the pivot.

- **V3 Statement-derived fuzz** (`research/techniques/SB10_fuzz.py`; fresh
  implementation from problem.md — generator, judge side, legality checker,
  stuck detector, FIFO/coalescing physics, hidden-Lout draws; ZERO code shared
  with repository judging tools):

  - `loop -n 10000` → 10^4 random legal streams (configs spanning constraint
    corners incl. dist_base=0/w=edges/K=1/layers=1; tables rising/falling/flat/
    V/step/concave/random with −1 gaps and shuffled rows; arrivals burst/
    dribble/poisson/lull/tie-heavy/mixed). Required: `10000/10000 passed`,
    i.e. zero protocol violations, zero stuck states, 100 % completion, exit 0.

  - `det -n 200` → byte-identical transcripts on double-run (determinism rail:
    catches unordered iteration, wall-clock, RNG leakage in decisions).

  - `stress [--hard]` → two max-density LEGAL streams (R=2000, ΣLout ≤ 2e5):
    density (batching allowed, raw throughput) and `--hard`, a table+SLO2
    combination forcing singleton decode rounds (`P_gap(2)>SLO2≥P_gap(1)`) with
    anti-coalescing timing jitter — the frame-maximizing class; asserts
    child-CPU (getrusage RUSAGE_CHILDREN) linearly extrapolated to 2e6 frames
    < 12 s and RSS < 256 MB. Note: a fully legal 2e6-frame test is not
    constructible under ΣLout ≤ 2e5 (SB03 §5 caveat) — the extrapolation factor
    is recorded, and forced-threshold builds verify every guard actually
    engages (D-161 method).

- **V4 Transcript-equality A/B (behavior-neutral phases only)**: snapshot
  pre-change binary to /tmp/sub_pre_<phase>; run BOTH binaries over a 2,000-
  stream seeded corpus via `loop -n 2000 --seed <k>` with collect-enabled
  variant; require identical token streams. (For behavior-changing phases this
  becomes the explain-the-delta step.)

- **V5 Repository safety battery (SAFETY-ONLY)**: AGENTS §0.6 steps 1–8 — zoo
  gate 27/27 verdicts, crash sweep 500/500, hostile harness, san/debug clean,
  extremes verdict-ok. **Scores printed by these tools are VOID and MUST NOT
  appear in any DECISIONS entry** (§0.8). Their sole role: crash/stuck/verdict
  safety on the curated suite, complementing V3's random coverage.

- **V6 Drift monitor (dev builds only)**: predicted-vs-observed event-time
  counter at zero across V3+V5 corpora (`#ifndef PSGO_STATIC` meter).

- **V7 Size gate**: `make submission` prints margin ≥ 0; record the number and
  the lever list fired in the DECISIONS entry.

### 3.5 Recorded evidence for the CURRENT tree (baseline, pre-P0) — this session ●

All commands run against `build/submission` (the PSGO_STATIC artifact, freshly
rebuilt `-Werror`-clean). Full transcripts:
`research/techniques/SB10_fuzz_evidence.txt`.

```
$ python3 research/techniques/SB10_fuzz.py replay --solver build/submission
EXAMPLE-1 REPLAY: PASS (byte-identical responses, 11 frames)

$ python3 research/techniques/SB10_fuzz.py det -n 100 --solver build/submission
DETERMINISM: 100/100 identical (PASS)

$ python3 research/techniques/SB10_fuzz.py loop -n 500 --solver build/submission
FUZZ LOOP: 500/500 passed (PASS)

$ python3 research/techniques/SB10_fuzz.py loop -n 10000 --solver build/submission
FUZZ LOOP: 10000/10000 passed (PASS)   [seeds 777000..786999]

$ python3 research/techniques/SB10_fuzz.py stress --solver build/submission
STRESS[density]: frames=22711 child_cpu=0.25s wall=2.2s child_max_rss=16MB
STRESS[density]: extrapolated_2M=22.44s FAIL (superseded by hard mode: density
                 lets the solver mega-batch, so it is not a frame-maximizer)
$ python3 research/techniques/SB10_fuzz.py stress --hard --solver build/submission
STRESS[hard]: frames=477730 child_cpu=4.90s wall=27.3s child_max_rss=16MB
STRESS: extrapolated CPU at 2e6 frames = 20.51s   -> FAIL vs program gate 12s

# Forced-threshold guard probe (D-161 method; threshold sed'd to frame 500):
STRESS[hard]: frames=477730 child_cpu=4.90s        -> NO CHANGE: the shipped
# fast-mode guard does not touch this workload's hotspot.

# Segment timers (instrumented /tmp copy; steady_clock):
[TIMER] parse=(n/a) apply=0.864s decide=2.093s write=1.687s   (~500k frames)
# => ~1.7 + 4.2 + 3.4 us/frame respectively

# Ablations (identical deterministic stream):
 linear-scan removed from group_size : cpu 4.90 -> 4.06 s (-17%)
 cost() memoized (behavior-neutral)  : cpu 4.90 -> 4.08 s (-17%), transcripts
                                       identical on 40 fuzz streams (40/40)
```

Interpretation: the artifact's per-frame budget on singleton-heavy legal tests
is dominated by `group_size` cost evaluations, output formatting/syscalls, and
event-apply allocation churn — none addressed by the shipped D-161 guard.
P0 items 4–6 are measured countermeasures; P1's governor bounds everything
above them; R2 tracks the residual until P2 structurally replaces the scan.

Harness limitations stated honestly: the Python judge models the protocol, not
the CF interactor's binary internals; ε-level tie handling follows the statement
(exact-equality coalescing, problem.md:242) rather than any reverse-engineered
implementation detail; scores printed by `debug_score()` are debug-only and
never cited; the 2e6-frame figure is a linear extrapolation because ΣLout ≤ 2e5
(problem.md:270) caps any legally constructible stream below it (SB03 §5).

---

## 4. CF PROBE PROTOCOL (submission discipline)

1. **One mechanism per submission.** A phase may bundle only its own edit list;
   anything else visible in the diff waits for its own probe.

2. **Baseline hygiene**: probe #0 precedes everything; every phase's attribution
   is `row_new − row_probe0` aggregated through intervening KEPT phases.

3. **Per-test reading**: judge protocol lines give points/tp/mean_tdr/mean_tpot/
   dist/norm_tp/norm_c per test — save EVERY run's full protocol to
   `docs/CF_JUDGEMENT_<date>.md` (standing policy since D-085).

4. **Attribution table** (mechanism → rows that can move): §1.4.3 (P1),
   §1.5 (P2), §1.6 (P3), §1.7 (P4), §1.8 (conditional). Rows outside the table
   moving > ±5 indicate an unintended coupling ⇒ treat as a bug hunt, not a win.

5. **Decision rule** per phase: KEEP if total Δ ≥ +10 AND no kill-trigger fired;
   REVERT (git checkout prior artifact; regenerate; resubmit leader only if the
   broken probe is the latest scored artifact) on any kill-trigger; TIGHTEN at
   most once where the table says a gate exists, then park.

6. **Wrong-sign families** (#5/#6 analogs): max ONE probe ever, instant revert
   on regression (D-152 doctrine).

7. **Freeze discipline**: after Aug 26 11:00 UTC no new mechanisms (48-h rule,
   AGENTS §5.3); Aug 27 revert-only; Aug 28 sealed rebuild + full §3 protocol +
   submission ≥ 2 h before close. The final submitted file must be byte-identical
   to the locally verified artifact (`sha256sum` recorded in DECISIONS).

8. **Every probe preceded by**: V1–V7 green + DECISIONS entry drafted (context/
   options/decision/rationale/expected impact/verification/rollback) + the
   exact artifact committed locally (checkpoint tag `cf-probe-<phase>`).

---

## 5. RISK REGISTER

| # | Risk | Phases | Likelihood | Impact | Mitigation / trigger |
|---|------|--------|-----------|--------|----------------------|
| R1 | Tie-order divergence sim↔judge (ulp-level) splits/merges frames differently | P1–P4 | medium | one suboptimal decision, self-corrected next frame (never illegality — commits derive from live state) | TIE_EPS=1e-9 coalescing; per-frame re-anchor confines error to one horizon; commit-margin rail (SB02 §6.1/§6.2); drift monitor counts real divergences |
| R2 | CPU overrun on monster tests | P0, P1+ | **HIGH (measured)**: baseline extrapolates ≈20 s at 2e6 frames on a forced-singleton legal stream; D-161's guard does not engage its target loop there | test zero ⇒ −50 final mean | P0 items 5–7 (memo −17 % measured; writer/alloc; fast-mode extension flattening the >1.2M-frame tail) + governor (12 s budget, sticky levels, hard cuts) + V3 stress gate re-run after EVERY phase; forced-threshold builds used to verify each guard actually engages |
| R3 | Byte overrun at `make submission` | P1, P7 especially | HIGH (by design) | phase blocked | lever ladders §1.4/§1.8 with named byte values; HARD-FAIL guard untouched (no cap loosening, ever); fallback = defer phase, never shrink the guard |
| R4 | Cliff (dist_base=0) regression from aggressiveness | P1–P3 | med | frozen-zero class | living-cliff discipline: fuzz corpus contains dist_base=0 corner streams (V3); P4 knife-edge pricing + (recommended) P7 SAFE contract; strictest kill rules on c_w-bearing rows |
| R5 | Non-determinism sneaks in (iteration order, clock, RNG) | P1+ | low | judge license voided + unreproducible attribution | fixed-seed xorshift keyed to config hash (only if futures ever sampled — P1 is F0-pure); no `unordered_*` in decisions; governor quantized+sticky; V3 det-mode gate every phase |
| R6 | Estimator poisoning (cold buckets ⇒ bad L̂out drives rollout) | P1–P3 | med | bad candidates win margins | L̂out consumers gated on sample counts (SB05 n0 rule) — pre-P5 the static `predict_lout` prior + `max(lin,·)` floor stays; commit-margin ignores wins < 2 % |
| R7 | Composite-attribution confusion | all | med | unattributable CF deltas | one-mechanism rule; probe #0 baseline; per-phase tags `cf-probe-*`; judgement protocols archived per run |
| R8 | Wake/liveness break (new hold strands a request ⇒ stuck zero) | P1, P2 | low-med | test zero | SB07 §4 proof obligations instantiated per new hold (pending-cause citation + issue-now fallback); V3 stuck-detector over 10^4 streams; battery stuck sweep |
| R9 | Sim mirrors a behavior the live solver cannot legally emit (or vice versa) | P1 | low | rollout evaluates ghosts | anchor copies live State only; candidates constructed from live readiness; legality NEVER taken from the model (SB03 §7 step 4) |
| R10 | Judge-side I/O deadlock / pipe stall | all | low | TLE-style zero | flush discipline untouched; stderr diet (P0) removes the last large-write path; V3 timeout detection |
| R11 | GNU G++23/msys2 portability (builtins, `bits`, `__int128`) | P0, P1 | low | CE on the judge | only GCC-universal constructs; `bits/stdc++.h` verified available on judge profile (SB09 §4.3-P5); artifact compiled with `-static` locally as a link smoke test |
| R12 | Working-tree regression vs sealed leader discovered at probe #0 | #0 | ? | baseline shifted | pre-agreed revert to a40d6f3 and re-baseline; P0 ledger re-measured before P1 |
| R13 | Human bandwidth (single implementer, 5 days) | all | HIGH | schedule slip | phases are strictly ordered and independently landable; slipping P4/conditional costs little if P1–P3 land clean; the calendar (§1.9) names the drop-dead lines |
| R14 | Local battery "green" misread as scoring validation | all | med | false confidence | §0.6 demoted to safety-only; DECISIONS entries may cite verdicts/counts but NEVER local scores; the template in §4 enforces this |

---

## 6. HUMAN DECISION POINTS (archival; required explicit answers before execution at the time)

1. **Probe #0 authorization** (submits current tree; establishes baseline).
2. **P1 lever pre-approval**: may D3/D4/D15 be retired inside P1 if the ledger
   demands (each widens the probe's blast radius slightly)? Default: YES in
   order D15 → D3 → D4.

3. **Last-slot allocation** (post-P4): default recommendation **P7 cliff
   controller**; alternatives P6/P5a-trimmed/P8.

4. **P0 insurance submission**: burn one slot to isolate P0 on CF, or ride it
   with P1? Default: ride with P1 (V4 proves neutrality).

5. **Composite escape hatch**: if P1 cannot fit after all levers, authorize
   folding D5's retirement into P1 (one composite probe)? Default: NO — prefer
   deferring P1 until P2's simpler static-cap groundwork lands first
   (group_size one-liner + precompute retention is itself fundable and
   attributable as "interim simplification" riding P1's probe).

---

## 7. REFERENCES

- Program docs: research/techniques/SB01…SB09 (`.md` + fragments + checkers),
  `PROGRAM_STATUS.md`.

- Statement: problem.md (verified line citations throughout; Examples :497–658).
- Contest feedback (permitted scorer): docs/CF_JUDGEMENT_2026-08-16/17.md,
  docs/HIDDEN_ATLAS.md (row classes/exchange rates used in §1.4.3–1.8).

- Tooling: tools/merge_submission.py (minifier/_RENAME/HARD-FAIL — extended, not
  weakened); research/techniques/SB10_fuzz.py + SB10_fuzz_evidence.txt (this
  deliverable).

- Theory anchors (downloaded, verified in SB docs): Bertsekas rollout survey
  (Prop. 1 sequential improvement); Whitehouse et al. determinization (strategy
  fusion); Deb–Serfozo threshold batches; Potts–Baker/Trietsch–Baker geometric
  sublots; Graham/LPT; Hoeffding/Bennett; Schrage SRPT; Dai et al. work-conserving
  stability; Sarathi-Serve chunked prefill.

*SB10 honored all pre-freeze prohibitions: no src/tools/cases edits, no commits, no
scoring evidence; every number in §1 ledgers traces to a cited SB measurement or
was labeled PLAN.*
