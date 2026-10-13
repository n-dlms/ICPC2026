# 05 — Strategy Playbook for the 2026 Challenge

> **Historical note (post-contest).** I wrote this playbook before the spec dropped (Aug 14, 2026) and executed it during the contest. The competition ended Aug 28, 2026 — I finished **#56 worldwide with 16,147.866/22,000**. What follows is preserved as I used it; imperatives are reframed as what I did. I (Ntokozo Dlamini) originated the strategy decisions; AI sped up drafting, implementation, and checks under my direction.

Synthesis of docs 01–06: what I implemented once the exact spec dropped (Aug 14, 2026). I assumed the problem was an online/interactive or batch scheduler over edge+cloud sites with prefill/decode stages, per-request latency SLOs, and a throughput-oriented objective — the family all evidence pointed to.

Doc 06 (past winners) was the empirical backing for every architectural choice below; the #1 observed score lever across all editions was **architecture, not parameters** (+27.25 from a rolling-controller rewrite), and ~90% of recoverable gap was locked by early committed decisions.

## P0: How I got the skeleton right (format safety first — see doc 01)

1. **Single C++17 file, stdio, fast I/O.** The judge interface was interactive per-tick (reveal arrivals, commit actions) or one-shot batch; I built an event loop that handled both.
2. **Fixed semantic-work ledger, not `clock()` portfolios.** The platform shared one global budget across all tests (120 s total, 4 GB seen in the analogous Huawei Cloud challenge), so a `clock()`-driven strategy portfolio was the #1 "Run timeout = −1" killer. I budgeted work per case by known input size and always kept a valid answer ready to emit immediately.
3. **Greedy feasible incumbent first, then budgeted improvement** that could be cut off at any time — that is what I shipped.
4. **I froze a safe submission early** (latest-nonzero-preliminary-submission → final retest rule) and never overwrote it near the end with an experiment.
5. **Pluggable score function** — the real objective was unknown at the time; I isolated it behind one function.
6. **Local simulator + judge harness** mirroring the per-tick protocol (I copied the pattern from the public archive: `tools/fast_judge.py`, `tools/suite_gate.py` in github.com/frankthinker/huawei-llm-scheduling-challenge).

## P1: Core scheduler (the first architecture, not the last)

**Model I used:** request = chain of stages alternating edge→cloud→(edge tail) with comm delays; prefill (bulk, compute-bound) then decode (per-token round trips, latency-bound).

**Design principles I took from past winners (doc 06):**

- **Rolling controller, commit-next-tick.** I planned over a horizon window but emitted only the current tick's cohort; everything further out stayed revisable (USA1's winning pattern). This was the single highest-leverage architecture — a structural jump of +27 in the direct analogue.
- **Greedy feasible incumbent first.** Winners repeatedly reported simple greedy beating "smart" ideas (2022: "all my smart ideas worked worse than day-1 greedy"). I built the always-feasible greedy as the floor and never shipped without it.
- **Forward-looking objectives, not myopic.** I scored each decision by its effect on future feasibility/packability (knapsack "future-fit" from 2022's winner; memory-time-area keys). I encoded every constraint as cost terms in one framework.
- **Commit carefully — prefix regret dominated.** ~90% of recoverable gap was locked by admission/owner decisions at tick t; I reserved scarce only-feasible resources even when another was free at the time; I tie-broke same-earliest-start deliberately (deterministic shuffle for diversification, as USA1 did).

1. **Two-level structure:**
   - *Global scheduler:* admission + ordering + split-point/route decisions.
   - *Local per-site scheduler:* continuous batching of ready work.
2. **Bubble elimination (the big lever):** keep **≥2 requests in flight** through the two sites so edge work of B fills cloud idle of A and vice versa; interleave decode round trips of multiple requests; never let a site idle with ready work (work-conserving ⇒ throughput-optimal under the load condition).
3. **Decode-first batching:** pack all running decodes; fill remaining budget with prefill chunks (Sarathi/vLLM); derive the per-step token budget from the SLO. Never co-batch full prefills with decodes.
4. **Ordering:** per-site EDD with operational due dates (`stage_deadline = arrival + SLO − remaining_worst_case_service − comm`); tiebreak SPT for throughput, protect near-deadline requests (Kairos-style urgency = slack/length).
5. **Admission control:** Moore–Hodgson-style per site; reject requests that cannot be feasibly scheduled (feasibility via preemptive-EDF simulation if preemption allowed, else EDD check). Under overload, rejection is the only way to keep SLO attainment. Remember 2022: an infeasible action may kill the whole test — never push to the edge.
6. **Communication as a resource:** model the link as a third concurrent "machine" (double buffering); if ρ = comm/total ≳ 20%, explicitly overlap transmit of chunk k+1 with compute of chunk k.

## P2: What I improved once input was fully known (full-information phase)

1. **Complete-plan envelope (the safest late gain):** after all arrivals were revealed (interactive format end-of-input), I ran cheap complementary complete-policy columns (different priorities/horizons/orderings, e.g. USA1/Afterlife/Dvoe-style keys from doc 06) and took the min over validated full plans. The parent plan was always a legal incumbent → **per-instance non-worsening guarantee, zero future risk**. This produced the largest safe late gain in the direct analogue (+0.94).
2. **Transactional acceptance everywhere:** a candidate replaced the incumbent only when a complete plan passed exact validation and strictly improved the true official objective. LP/dual/proxy values only *generated or pruned* candidates, never accepted them.
3. **Local search / constraint generation:** I simulated → found SLO-violating or idle-inducing spots → swapped orderings, shifted start times, blocked conflicting pairs → re-simulated; kept strict improvements. Sparse pair/triple owner/order/relocation repairs under a fixed work budget (as in the 374.98 archive solution).
4. **Phase separation:** I treated prefill work and decode work as separate schedulable units with separate queues (DistServe-style); mixed chunks only when the SLO budget allowed.
5. **Bottleneck-balance:** I computed per-site utilization; if U_E ≠ U_C and SLOs allowed, I added/redirected work to the underutilized side (deliberate stage balancing, COACH-style cost model).
6. **I did NOT trust revealed-objective-only improvements for live actions** — local gains routinely flipped sign under the real future (V115/V117 regressions in the analogue). I kept live rules conservative unless provably non-worsening; I spent improvement effort in the full-known domain.

## P3: How I adapted to whatever the spec actually said

- **Where decode round trips dominated (RTT-bound):** the lever was amortizing round trips — I batched multiple tokens per interaction, multi-token speculative-style steps. Where the spec exposed token-level control, I grouped decode steps of one request into fewer, larger interactions.
- **Where prefill dominated (compute-bound):** I used chunked prefill + pipelined long prefills; Mooncake-style parallel prefill chunks cut TTFT.
- **Where SLOs were per-request end-to-end:** I converted to per-stage operational due dates and kept headroom `SLO − ε`.
- **Where preemption was allowed:** token/chunk-level preemption (FastServe skip-join MLFQ: long-input prefills skip to low priority, preemptible at chunk boundaries) — biggest SLO-goodput win available.
- **Where the objective had latency terms (makespan, total wait) + throughput terms:** I optimized the primary first (throughput/SLO attainment), then tightened latency via ordering; mimicked `X − 1e-6·p` / `1 + (V_base−V)/ΣL_out` shapes (dominated-term secondary).

## Current verified state (assumed protocol, Aug 2026)

Everything below was validated on the 20-case local suite (c1–c4 + 16 generated profiles) under the assumed protocol; treat it as the starting point, not the answer to the real spec.

- **Winner / freeze candidate:** `build/solution_lookahead_slai_sjf_imbg` (`-DLOOKAHEAD -DKEY_SLAI -DADMIT_SJF -DIMBAL_GATE`) — rolling per-tick simulation (commit-next-tick) + SLAI decode ordering + SPT admission ordering, with `use_keys()` disabling the SLAI/SJF keys on severely imbalanced graphs (`3*E < C || 3*C < E`) where they backfire. **+2.0** over baseline on the 20-case suite, 20/20 valid, 0.2 s / 120 s suite gate. DEV +70.0, SECRET +124.0, adversarial gap 20-set neutral (was −22 for the ungated keys).
- **Baseline floor:** `build/solution` — greedy EDD+USA1 admission/decode; always-feasible incumbent, never ship without it.
- **Large-T variant:** `build/solution_lookahead_contain_h` (horizon capped at 100 ticks) — matches the winner; use if the real judge has large T.
- **Verified invariants (never regress):**
  - CONTAIN (containment displacement) is ablation-only: its measured +2.0/+3.0 were artifacts of simulated bookings leaking into the global owner maps (`simulate_from` mutates `owner_ee`/`owner_cc`); after the snapshot/restore fix it measures 0 over plain LOOKAHEAD. Any displacement gain must pass `-DASSERT_STATE` before it counts.
  - Feasibility (`est ≤ D`, `est ≤ T`) enforced in both the candidate list and the transactional apply set.
  - No `clock()`-driven portfolios; the aggregate 120 s suite budget is a correctness wall (timeout = −1).
  - Acceptance is transactional: a change ships only if `tools/ab_test.py` returns valid AND not worse on all 20 cases; final gate via `tools/suite_gate.py`; every freeze is preceded by a run of the assert build over the suite.
- **Known-losing experiments (do not revisit without new case evidence):** SJF alone, SLAI alone, ρ-gate ≠ 1.0 (0.98/0.995/1.02), CONTAIN (post-leak-fix, neutral), start<t displacement, infeasible-probe candidates (−20.0).

## Problem-Day playbook (as executed 2026-08-10 dry run + contest days; reached #56)

- **I interleaved fast Reality-Layer triage into the first 10 minutes**, not after the
  formal reconciliation slot: as materials were saved, I updated ASSUMED/UNKNOWN
  statuses in parallel so the branch plans (protocol/protocol.yaml `branch_plan:`
  on scoring aggregation, timing regime, communication model, judge compilation)
  could be read off by minute ~12 instead of being derived in the 25-35 min window.
- **Behind-schedule contingency (I decided this before the contest and kept it written down):** when the evaluator was
  not trustworthy by minute 50, I submitted the frozen baseline unmodified rather than
  rushing an unvalidated change. Valid > clever. Every rushed change on a hidden
  set was an unmeasured risk; the frozen winner was measured.
- **Protocol-format traps (EXP-40 dry run):** (a) arrival lines without explicit
  ids — I determined immediately whether ids were implicit (order) or explicit, and
  assumed unique tuples; duplicate-tuple arrivals were a spec trap. (b) Solver-side
  model-desync (emitting `A` without advancing internal phase) produced valid but
  useless output — the assert build was part of my day-1 baseline loop, not an
  afterthought.

## Checklist of quantities I extracted from the spec on day 1 (all verified during the contest)

- [x] **Arrival model** — I had assumed online per-tick reveal (interactive loop in `src/Solution.cpp`). I verified batch-mode fallback wasn't required; burst-pattern generation matched `cases/gen_burst_*`.
- [x] **Per-request fields** — I had assumed `a` (arrival), `pe`/`pc` (prefill durations), `Lout` (decode length), `D` (deadline), `PEDGE`/`PCLOUD` (decode unit costs). I verified names, units, and granularity → mapped into `struct Req`.
- [x] **Site capacities** — I had assumed `Ecap`/`Ccap` (edge/cloud rates), `COMM` (per-interaction comm time), horizon `T`; concurrency assumed unlimited (time-based occupancy). I verified concurrency/per-node limits and comm model (double-buffering opportunity confirmed where comm was a real resource, doc 04).
- [x] **Batching rules** — I had assumed none controllable (tokens per interaction fixed by protocol). I verified whether batch composition/chunk sizes were controllable (this opened Sarathi/FastServe-style chunked prefill, P3, where applicable).
- [x] **Objective formula** — I had assumed `on_time − 1e-6·V` (pluggable score function in solver + `tools/fast_judge.py`). I verified normalization (V_base), SLO attainment definition (strict on-time vs slack credit), and per-test validity rules (illegal action vs SLO violation vs timeout consequences).
- [x] **Judge protocol** — I had assumed interactive per-tick commitment, no action retraction (this is what forbade start<t displacement). I verified whether retraction/preemption was allowed — I only re-enabled preemptive displacement (FastServe-style, P3) where confirmed.
- [x] **Limits & submission** — research (research/day7/icpc-domjudge-huawei-judge.md) said the 2026 challenge was **Codeforces-hosted (contest 2251) → per-test limits most probable (~5 s / 1024 MB, CPU-time primary)**, alongside the aggregate-120 s suite regime on the sister platform. I verified per-test vs suite budget, memory, and preliminary/final submission mechanics ("latest non-zero submission is re-tested on a hidden set" — I never overwrote a good submission with a late experiment).
- [x] **Timing regime** — PER_CASE vs TOTAL vs PER_PHASE changed how the release gate computed margins; my pre-spec placeholder was `limits.yaml` (3.0 s / 256 MB, WARN 0.5× hard 0.83×). See research/day7/README.md §2 for the regime matrix. contest_runner reported BOTH wall and CPU.
- [x] **Integer ranges / overflow risk** — max N, max T, capacity×horizon products. The Day-7 audit (research/day7/a1-integer-audit.md) listed 8 latent UB sites conditioned on spec bounds (int fields, inner int sums, `prio_afterlife` product, `llround` truncation, `T+5000` sizing); for each spec bound that crossed the threshold, I applied the localized widening from that file. I tested with max values, not just nominal.
- [x] **Install-mode questions** — one run over a large collection of cases vs one process per case (changed caching/init architecture); whether stdout/stderr were constrained (DOMjudge default output cap 8 MB; my worst-case measured output was ~100 KB); exit-code behavior; invalid-output behavior; whether randomization, filesystem, or network access was restricted.
- [x] **Scaling wall (measured Day 6/7, 2026-08-06)** — solver CPU was ~1/4 of the wall reported by the Python harness (wall case: 1.56 s wall vs 0.43 s CPU — the gap was harness feeder I/O, not solver compute). Heaviest adversarial zoo case (all_at_tick1_burst, N=1000) = 1.46 s CPU. Official CPU-primary budget ~5 s → ~30% headroom on the worst case found. I confirmed real max N and T before trusting suite-scale numbers.

## Score-identification fallback (as executed; written in stone, EXP-47)

I ran the forced-discriminator battery on the official sample on Day 1
(`tools/score_oracle.py --casegen`, then `--identify` with the official
score; the `forced_violation` case separated abs_eps_v from throughput by
~1e-2 even when every completed request was on time, because V measured
time-in-system, not violations).

When the oracle returned **AMBIGUOUS** (or NO-match) after the forced-violation
case, I did the following:

1. **I submitted the Day-1 candidate anyway** (pool-validated on secret_b first,
   per docs/11-pool-budget.md). The safety net needed validity and a
   non-zero score, not the exact formula — every sane scoring function
   rewarded meeting SLOs.
2. **I assumed absolute scoring** (`on_time - 1e-6*V`) for all engine tuning.
   It was the best-supported hypothesis and the only one with a pluggable
   implementation at the time.
3. **I built a relative-scoring hedge candidate on Day 2-3** that maximized
   raw throughput / SLO margin (throughput-dominant keys, minimum slack
   reserve), engineered so it could not lose much under either formula.
4. **I validated the hedge with secret_c** (its reserved final-freeze slot)
   once judge leaderboard feedback or a second sample suggested relative
   scoring. I never burned secret_c for a hedge without such evidence.
5. **I pivoted to the hedge only with evidence** — never mid-day, never after
   the freeze gate, never without `freeze_gate.py --submission-ready` and
   the 48-hour override protocol.

The one thing that NEVER happened on my run: guessing the formula, retuning the
engine to the guess, and submitting the retuned engine before Day-1 evidence
existed. opencode Trap 4 (objective-function mirage) was the highest-impact
failure this playbook defended against, and my discipline there helped reach #56.

## Reference implementations to mine (see doc 01)

- 2026 LLM scheduling archive (rolling planner + portfolio + local search + timeout postmortem): github.com/frankthinker/huawei-llm-scheduling-challenge
- 2023 XR 24th-place (greedy + local search + ensemble): github.com/visho33/HeuristicContests/tree/main/HuaweiChallenge/ICPC-2023-Online-Challenge
- 2023 XR 10th-place (SA over output state): github.com/FakePsyho/cpcontests (various/cf_huawei_2023, approach.md)
- 2025 OXC 20th-place (container + fallback): github.com/eulerscheZahl/CodingContests/tree/master/Huawei/Codeforces2025
- WF Challenge scheduler (greedy list + hill-climb with wall-clock loop): gist.github.com/eulerscheZahl/0ca50c4386bb04a10e4f06ea2bae72d5

## Key theory anchors (details in doc 04)

- Throughput ceiling: `1/max(t_E, t_C, t_comm)` per steady-state unit; Little's law sanity bound
- Bubble ratio: `(stages−1)/(in_flight+stages−1)`; keep both sites non-empty
- EDD optimal for Lmax per site; Moore–Hodgson for admission; Horn for preemptive feasibility
- Johnson for batch makespan/idle minimization; rolling horizon for online commitment
