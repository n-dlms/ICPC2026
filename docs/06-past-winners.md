# 06 — How Past Winners Actually Won (ICPC Online Challenges + Huawei)

> **Historical note (post-contest).** I compiled this review before the contest from winner writeups and repos; it backed every architectural choice that carried me to **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) originated the synthesis and decisions; AI sped up gathering and summarizing the sources under my direction.

Compiled from winner writeups, postmortems, public repos, and comment threads of every past edition. The 2026 challenge (Scheduling Algorithm for Collaborative Inference System) was closest to the 2026 Huawei Cloud LLM-scheduling challenge; I treated sections 1–4 as cumulative evidence and section 5 as the distilled playbook I executed.

## 1. ICPC 2023 Online Challenge (XR over 5G, CF 1885) — winners documented

**Standings:** #1 dbdr (France) 17327.88, #2 Degalat57 17326.87 (gap ~1 pt!), #3 E869120, #7 gleb.astashkin, #10 FakePsyho, #16 el_tunel, #24 visho33. 27,528 participants. The winner has NO public writeup — his final-hour "decisive optimization" separated the top spots and was never revealed.

**Techniques actually used (from code + postmortems):**

| Rank | Who | Technique |
| --- | --- | --- |
| 2 | Degalat57 | Started with brute-force pairwise grouping on a *simplified* score formula → held rank 1 for 3–4 days (~15–15.5k), then slipped to ~100th when tests exposed the simplified model. Comparator tuning worth only ~100–200 pts. **Meta-advice:** estimate optimization headroom first; abandon "reasonable" optimizations yielding little; don't chase small per-test losses while losing thousands elsewhere |
| 7 | gleb.astashkin | `CheckTL()` aborts at **1500 ms** (safety margin inside the 2 s limit), mt19937 random, preallocated memory |
| 10 | FakePsyho | **Simulated annealing directly over the output state** (power usage), 6 move types (change power; move power across n/r/k; swap unit-row; swap slot-column), incremental eval, eval = 1.0 if frame finished else 0.1·(finished%)^3.5, under-steps SA on larger tests |
| 24 | visho33 | Greedy with gain = W·K·log₁ₚ(geometric-mean SINR), per-frame gain heap, quota/exchange/evict rules on residual slack, multiple sort keys, drop unsatisfiable frames, randomized local search |

**Lessons:** (1) winner ≠ best algorithm — it was a final-hour optimization on an already-top setup; (2) a simplified score model can give a false rank-1 early on; (3) every top solution had aggressive-but-safe time management (fixed abort thresholds); (4) greedy + light search + multi-strategy selection beat exact methods.

## 2. ICPC 2022 Online Challenge (Graph Partitioning + VM Placement) — the overfitting controversy

**Winners:** P1 Graph Partitioning → Sung.An; P2 Topology-Aware VM Placement → icecuber (944k). Scoring was **baseline-relative**: `S = Σ w·(0.8·PC/PC* + 0.2·SC/SC*)`.

**icecuber's #1 P2 approach (self-published):**

1. Greedy baseline first (~650k) — "surprisingly hard to beat."
2. **Knapsack-derived "future-fit" score**: precompute optimal knapsack packing of a PM's 2-NUMA resources; score a candidate placement by `scorePM(after) − scorePM(before)` — i.e., **how many more future VMs this machine can still hold**. Extra cost terms encode all constraints (don't open new racks, don't top up near-full networks). Place the whole multi-VM request at once via a joint DP.
3. **The killer move:** comparing near-equal doubles made tie-breaks rounding-sensitive → replaced with pseudorandom epsilons + tunable seed, then **submitted ~200 runs with different seeds to climb the leaderboard** (774k).
4. **Testcase fingerprinting:** per-test scores were visible; he fingerprinted each testcase from its first 10 queries and dispatched the best seed among his prior submissions per detected testcase → 944k. He'd asked the organizers in advance whether this was allowed (no reply). Rivals explicitly chose not to ("decided this is not fair").

**Other winners:**

- eulerscheZahl (#15, 707k): hard-constraint prefilter; per-PM penalty = "VMs placeable before vs after" (same future-fit idea); **two scoring functions — exact slow one, then switches to fast approximate at the 9-second mark** (15 s limit); fill-rack-first, allow spill via score adjustment; no seed-hardcoding, "submitted the same code a few times."
- qwerty787788 (#34): "**all 'smart' ideas I had ended up working worse than simple greedy implemented on the first day**."
- P1 approaches: minimize max component size ≈ ⌊c√N⌋ (chromate00); spanning tree + **tree-DP splitting into components ≤ B, iterate B×1.7** (Noam527, 7th); MST as base for weighted case.
- P2's forced admission: unable to place every VM → print `-1` and **the whole test dies** — admission failures are fatal, not just score-reducing.

**Lessons:** (1) greedy feasible incumbent is a hard-to-beat floor — build it first; (2) forward-looking "future-fit" objectives beat myopic first-fit (score placements by their effect on future packability); (3) batch decisions per request jointly (DP), not item-at-a-time; (4) **expect testcase fingerprinting / seed ensembling to happen** — it's been done, condemned, and organizers now design against it; decide your stance; (5) time management = fixed switchover points, never optimize toward the wall.

## 3. ICPC 2025 Online Winter Challenge (OXC Ports, CF 2177) — most recent format precedent

Top-3 was won on **structural insight + exact graph tools**, not generic search:

- **maxplus (3rd, ~126k):** hierarchical assignment (planes→spines→OXCs→links), each stage solved by **augmenting-chain (AC) search on a bipartite variable↔value graph** (Kuhn generalized to vertex capacities, iterative Dijkstra-like with negative edges); per-stage objective = Σ|assignment differences| vs previous query (minimizing adjustment cost); fallback to MFC=2 when plane assignment fails; special-cases test structure (1:3 / 1:7 ratios).
- **Nanored (14th):** constructive MFC=1: **Euler-circuit orientation of the flow graph + bipartite matching decomposition** into spine pairs, then group-level matchings partitioned onto OXCs.
- **eulerscheZahl (20th):** random assignment to "containers", fix violations by moving tasks, pin reuse of previous connections, **fallback after ~400k mutations to a simpler valid solution** (two TLEs earlier cost him rank 15→52).
- **Problem-setter Jelefy:** top participants' ideas mirror Huawei's **FastReChain** paper (arXiv:2507.12265) — replacement/augmenting-chain for the 2-layer case. Also: official test data was weak (23 cases) — winning solutions may be brittle.
- Hidden gotcha: conflicts count leaf–spine links too, not just OXC ports — **replicate the checker locally before optimizing**.
- Known lower bounds per test bucket (MFC=1/4/8); matching often wins over generic search.

**Lessons:** (1) decompose hierarchically; solve each stage with exact graph tools (matching/flow/Euler/augmenting chains), not one monolithic heuristic; (2) primary objective first, secondary as a delta term (adjustment cost = difference vs previous state, bias reuse); (3) hard fallback under a mutation budget — feasible answer always beats an optimal one that times out; (4) special-case the test parameter grid from pretests; (5) sequential batches → persist state, and beware "optimal per-batch" ≠ "optimal across batches" (adjustment cost couples them).

## 4. 2026 Huawei Cloud LLM scheduling challenge (the direct analogue)

Huawei #38 "Memory-constrained LLM inference optimization" (Jun–Jul 2026, 152 participants, `Solution.cpp`, 120 s total suite, 4 GB). Same problem as the 2026 UCup Finals Challenge. **Live winners: USA1 (ecnerwala, ksun48, tourist) 1st; Dvoe protiv vetra 2nd; Afterlife 3rd** (onsite). No official code; the podium strategies are independently reconstructed in the frankthinker archive (github.com/frankthinker/huawei-llm-scheduling-challenge), whose author finished top-40-ish with best-valid 374.98.

**The three podium strategies (as publicly reconstructed):**

- **USA1 (winners):** high-frequency **rolling controller** — priority `L_out × (L_in + L_out)^1.594982`, 75-tick planning window, **commit only the next tick's cohort** (everything further out is revisable), piecewise-affine earliest-feasible-start per GPU, deterministic GPU-order shuffle for tie-breaking, SPT switch under low-concurrency pressure.
- **Afterlife:** request key `(2·L_in + L_out + 1)·L_out` (exactly twice the memory-time area), dynamic GPU price, "19-failure" current-tick reject frontier.
- **Dvoe:** a different duration–input-length exchange-rate key + complete-plan strategies for the full-known domain.

**What actually moved the score (the archive's evidence):**

- **Architecture jump (+27.25 over the basin) from adopting the USA1-style rolling controller — dwarfs every other gain.** "Live policy" beats terminal search by an order of magnitude.
- Full-known-domain **complete-plan envelope** (min over Dvoe/Afterlife-style full-policy columns, parent always a legal incumbent) → +0.94. This is the *safest* kind of mechanism: per-instance non-worsening theorem, zero future risk.
- Sparse pair/triple repair local search: +0.22/+0.18/+0.10 with a 0.24 s / 24M-work ledger.
- Live-policy experiments repeatedly **regressed** online (V115 −0.08, V117 −0.14) despite positive local banks — revealed-objective improvements have wrong sign on the hidden distribution (provable: identical prefixes with opposite future gains).
- **Regret decomposition: 89.8% of the recoverable gap is locked by the committed prefix** — admission/owner decisions at tick t are permanent; only ~10% is recoverable at end-of-input. Terminal-only search is too late.
- Time-expanded LP diagnostics: ≤3 fractional requests in the integer kernel; ~240/16,850 active columns — "active GPU×time prices + small integer kernel" structure.

**The timeout plague (the archive's #1 lesson):** 11 of ~35 versions died of "Run timeout" (−1, invalid): V69, V75, V78, V91–92, V96, V100–101, V108, V119, **V146 (the final submission — a timeout on the last day cost them the valid score)**.

Root causes: per-case time measured instead of suite sum; 239 textual `clock()` calls making the candidate set depend on compiler layout/CPU/cache; work appended not exchanged; cold-code layout changes; no recovery after time spent on rejected candidates.

Fix: **fixed semantic-work ledger** (each stage gets a frozen tranche of work units), `clock()` only as an emergency brake, aggregate gate Σ child CPU + overhead < 120 s with 10–20% margin on both GCC and Clang, A/A identity gates, **freeze the best-valid ZIP and default the final submission to it**.

**Score-formula discipline:** `score = max(1 + (V_base − V)/ΣL_out, 0)` — raw normalized-V deltas mislead (17 objective wins → only 12 visible in score). Gate releases on the official formula.

## 5. Distilled playbook: what actually won these Huawei scheduling contests (and how I applied it to reach #56)

1. **Architecture beat parameter tuning.** The single biggest observed jump ever came from replacing the live planning structure, not from priority formulas (+27.25 vs +0.1–0.9 for everything else). I got the rolling/online layer right first.
2. **I won the live layer.** ~90% of recoverable gap was locked in the committed prefix: correct admission + owner decisions at each tick, reserving scarce only-feasible resources even when another was free at the time (owner opportunity cost), same-earliest-start tie-breaking. I committed only what had to be committed at each tick and kept everything else revisable.
3. **Greedy feasible incumbent was the floor that beat "smart" ideas.** Winners repeatedly reported simple greedy outperforming clever schemes (qwerty787788; icecuber's 650k baseline). I built it first, made it always-feasible, then improved on top of it.
4. **Forward-looking objectives, not myopic ones.** I scored placements by effect on future packability (knapsack future-fit, Afterlife's memory-time-area key). I encoded every constraint as cost terms in one framework and batched per-request decisions jointly (DP), not item-at-a-time.
5. **Full-known domain → complete-plan envelope with transactional acceptance.** Once everything was known, I ran cheap complementary full-policy columns and took the min; I accepted candidates only when a complete plan passed exact validation and strictly improved the true objective. I never accepted on proxies (LP/dual were candidate generators only).
6. **I never trusted revealed-objective-only improvements for live actions.** Local gains flipped sign under the real future; I kept conservative actions unless provably non-worsening.
7. **The total time budget was a correctness constraint, not a performance target.** I used a fixed semantic-work ledger, no `clock()`-driven normal work, aggregate gate with margin on both compilers, and froze the best-valid submission for the final slot. Timeout = −1 = invalid; it had killed more finalists than any algorithm mistake.
8. **Score formula discipline:** I tracked the official formula (baseline-relative, clamped, normalized), not proxies, and replicated the official scorer locally.
9. **Admission was forced or fatal:** in interactive formats, an infeasible action could kill the whole test; in SLO settings, over-commitment created violations. I preferred "correct, feasible, conservative" (never forced).
10. **I special-cased the test grid** from pretests (OXC winners did; problem-setters warned solutions could be brittle); and I heeded the warning — where organizers moved to secret final sets, pretest overfitting backfired (2023: Huah TLE'd on secret test 9; Noam527: "if you put yourself tight on the time limit on pretests, you can't expect to pass everything").
11. **I validated on held-out cases.** Pretest overfitting backfired when finals used secret sets (2023: TLE on secret test 9).

## Key sources

- 2023 standings: clist.by/standings/icpc-2023-online-challenge-powered-by-huawei-47614571/ · Degalat57 postmortem: codeforces.com/blog/entry/120925 (comment 1090242) · visho33 repo: github.com/visho33/HeuristicContests · FakePsyho repo: github.com/FakePsyho/cpcontests (various/cf_huawei_2023, includes approach.md) · gvastash: github.com/gvastash/huawei_fall_23 · sunkafei: github.com/sunkafei/huawei-codeforces (2023 winner's repo)
- 2022: results blog codeforces.com/blog/entry/107690 · icecuber comment 958999 · eulerscheZahl comment 959128 · announcement blog 105097 (161 comments) · eulerscheZahl gist (WF Challenge scheduler: greedy + hill-climb with wall-clock loop): gist.github.com/eulerscheZahl/0ca50c4386bb04a10e4f06ea2bae72d5
- OXC 2025: blog 148541 (maxplus comment 1339478, Nanored 1339501, ZloyNegr 1339436, Jelefy 1349945) · eulerscheZahl/CodingContests Huawei/Codeforces2025 · FastReChain: arXiv:2507.12265
- LLM scheduling: github.com/frankthinker/huawei-llm-scheduling-challenge (docs/results.md, architecture, postmortem, historical/) · ucup.ac/news/wrap-up-2026-ucup-finals/ · developer.huaweicloud.com/competition/information/1300000270 · arXiv:2508.06133 (LLM serving with variable prefill/decode lengths — challenge's academic basis)
