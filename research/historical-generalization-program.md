> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Program plan below is preserved as a historical working note. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Historical Huawei Challenge Generalization Program — master plan & status

Single source of truth for the four-problem generalization program (TEST 1-4).
Append-only status; sessions resumed from here during the program (preserved as archive). See
`history/2020_cloud_scheduling/README.md` for per-problem detail.

## Charter (governing rules — never violated)

1. Isolation: each problem lives under `history/<year>_<name>/` with its own
   contracts, evaluator, generators, solvers, pools. ZERO edits to the 2026 tree:
   `src/Solution_asort_frozen.cpp`, `build/solution_frozen`, `protocol/`,
   `cases/`, 2026 `src/` files. Historical tooling = adapted copies under
   `history/<year>/tools/`, NOT modifications of shared `tools/`. A shared-tool
   fix ships only as a behavior-preserving change verified against the frozen
   binary (`make verify_frozen`), and only at the end of the program.

2. Source discipline: official Codeforces/Huawei materials are the spec. No
   editorial/solution reading until after candidate selection; historical
   solutions consulted only as post-experiment validation. Third-party
   editorials are NOT authoritative specs.

3. Honesty rule: unavailable materials are recorded UNAVAILABLE, never
   fabricated confidence. Synthetic instances are labeled synthetic.

4. Order: 2020 → 2022 → 2025 → 2023, one at a time; report gate after each
   before the next starts.

5. Never convert an assumption into a fact: statuses ASSUMED / CONFIRMED /
   UNKNOWN / OBSERVED / CHANGED / REMOVED only; conflicts in official text are
   preserved and flagged, not resolved silently.

6. Anti-overfitting: TRAIN+VALIDATION seen by selection; HOLDOUT touched once
   per candidate family. If materials can't support this, say so explicitly.

7. The 2026 frozen solver is never modified because of a historical finding.
   Output is: Historical evidence / 2026 relevance (UNKNOWN until Problem Set)
   / Action: improve generic infrastructure only / Frozen solver: untouched.

8. Mechanism-first: every named regime needs structural preconditions, observed
   phenomenon, measurable metric, reproducible generator cell, repeated
   confirmation, candidate-vs-baseline effect. Cells with byte-identical
   results are generator blind spots, not evidence.

9. Transfer classification (final report): Universal / Structural /
   Problem-family / Problem-specific / Accidental.

10. Adaptive budget: MVT → meaningful structural failure? no → report+move on;
    yes → up to ~3 candidate iterations → report. Never manufacture candidates
    when no mechanism exists.

11. When the 2026 Problem Set arrives, its official contract overrides every
    historical assumption.

## Final cross-problem deliverables (after all four)

1. Four per-problem reports (A-K each).
2. Cross-problem comparison.
3. Generalization-success table (framework components that transferred).
4. Generalization-failure table.
5. Transfer classification for every mechanism discovered (rules §9).
6. Concrete architecture improvements (with the honest verdict: general
   research machine vs sophisticated 2026-specific framework; reusable vs
   problem-specific components, accidental assumptions, over/under-engineered
   spots — from actual friction, not theory).

7. Experiments to add to the framework.
8. Recommendation on whether the 2026 system changes before the Problem Set.
9. What stays frozen / what can safely improve.
10. Problem-Day readiness assessment.

## Methodology convergence (after TEST 2 — living section)

Two completed problems (TEST 1 2020 Cloud Scheduling, TEST 2 2022 Task
Scheduling) now support a repeatable experimental methodology — not a bag
of contest tricks. The pattern that survived both:

**greedy feasible baseline → prove the judge → structural matrix →
mechanism ablation → regime detector → conditional gate → fresh draw →
freeze.**

Neither contest needed cleverness; both needed measurement.

### Two fundamentally different discoveries

- **TEST 1:** "A heuristic can look good and be wrong." VM-descent
  ordering won +819 on the train region and *inverted* on validation and
  holdout (index-order won +2000 / +773 there). The ordering looked
  beneficial and was not — selection discipline caught it; scores alone
  would have shipped it.

- **TEST 2:** "A heuristic can be wrong in one regime and right in
  another, and the regime can be detected from the instance itself."
  Priority braking hurt the n=2 sparse non-chain cell (+1.78 by disabling)
  and helped everywhere else (up to +11.2). The sign flip was converted
  into an input-derived gate (machine count, edge count, chain-edge count —
  all parse-time), verified byte-identical everywhere it should not fire.

If TEST 3 finds a third pattern, the framework is converging on something
like: *given an optimization problem, identify the structural regime,
determine which mechanism is beneficial in that regime, and activate only
mechanisms whose causal contribution survives independent evaluation.* That
is a structured research process.

### Three-layer architecture (carry into TEST 3)

**1. Universal infrastructure** — should increasingly be reusable as-is:

independent evaluator construction, exact objective implementation,
malformed-output battery, tiny-instance oracle, deterministic replay,
sanitizer testing, structural matrices, mechanism ablation,
train/validation/holdout separation, candidate-vs-baseline comparison,
regression detection, runtime/validity gates, provenance, experiment
reports.

**2. General optimization primitives** — optional building blocks, NOT
auto-transferred; each must earn its place on the new problem:

gap insertion, adaptive ordering, lookahead, local repair, bottleneck
detection, resource reservation, contention-aware scheduling, critical-path
reasoning, candidate scoring, regime detection, conditional activation of
heuristics.

**3. Problem-specific mechanisms** — stay isolated until the new problem
proves they are useful; never copy in untested:

VM displacement, drain, particular disk-placement strategy, particular
priority formula, 2026-specific inference scheduling logic, anything based
on assumptions unique to one historical problem.

The separation is the point. The failure mode is "historical heuristic →
copy into 2026 → hope." The discipline is "primitive → test on new
problem → ablate → keep only what survives."

### Canonical pipeline (the research machine)

```
Problem arrives
→ formal contract
→ independent evaluator
→ tiny exact oracle
→ baseline
→ structural decomposition
→ mechanism hypotheses
→ controlled candidate generation
→ ablation
→ regime detector
→ conditional gates
→ train/validation/holdout
→ fresh draw
→ runtime/validity
→ freeze

```

NOT: historical heuristic → copy into 2026 → hope.

### 2026 hypotheses (to be tested, not assumed)

Cross-problem inferences are hypotheses until the 2026 problem arrives —
they must be falsified, not assumed to transfer.

- **H-X (idle-window exploitation):** "Seeking the scheduler's
  contention-free residual capacity before touching orderings may dominate
  ordering improvements in the U-shaped edge↔cloud inference scheduler,
  where edge↔cloud bubbles are precisely the idle windows at stake."
  Origin: TEST 2 gap-insertion finding (the dominant mechanism was inserting
  work into idle machine windows, not reordering). Status: **HYPOTHESIS —
  NOT established for 2026.** TEST 2 has not shown this is load-bearing for
  the U-shaped problem. When the 2026 problem arrives, attempt to falsify
  it; if it survives, that is much stronger than assuming the lesson
  transfers.

Add further hypotheses here as TEST 3/4 produce them. Each entry: statement,
origin, status (hypothesis / falsified / survived), evidence pointer.

### Caution

Essays and summaries are *claims* about the experiments, not evidence.
The actual reports, logs, candidate binaries, matrices, and evaluator
outputs are what support each claim. Never let the essay outrun the
evidence.

## Per-problem pipeline (phases)

Phase 0 Availability probe → Phase 1 Reality intake (contract.yaml) →
Phase 2 Architecture analysis (12 questions, written BEFORE solver code, no
2026 import without demonstrated structural correspondence) → Phase 3 Baseline
(simplest valid deterministic solver + INDEPENDENT evaluator code) → Phase 4
Evaluator verification (tiny-OPT anchors + edge battery, verdict table) →
Phase 5 Structural matrix (dimensions from THIS problem's spec) → Phase 6
Mechanism verification (regime cells) → Phase 7 Candidate experiments
(hypothesis/mechanism/cells/benefit/downside/runtime/validity per candidate,
per-cell deltas, worst regression; nothing freezes on total score) → Phase 8
Anti-overfitting (seed-region splits) → Phase 9 Ablation (remove one component
at a time) → Phase 10 Runtime/validity (CPU/wall/RSS/output size/crash/
sanitizer/determinism/margin vs stated limit).

Per-problem deliverable = report A-K: A contract, B architecture
classification, C baseline, D evaluator verification evidence, E structural
matrix, F regime evidence, G candidate mechanisms, H generalization results,
I failure analysis, J runtime, K lessons for the framework.

Cadence: Phase 0 → minimum viable test (contract + parser + evaluator +
baseline + 1 candidate + tiny-OPT anchor) → full phases if tractable → report
→ gate (user approves) → next problem.

---

## TEST 1 — 2020 Cloud Scheduling ("Fixing the Cloud", NERC 2020, CF contest 1460 problem A)

Purpose: closest conceptual match (scheduling + dual-objective tension).
Links: https://codeforces.com/nerc2020 · https://codeforces.com/blog/entry/85241
Workspace: `history/2020_cloud_scheduling/`

### Established facts

- Statement CONFIRMED (statement.md, 190 lines, 2026-08-10); 3 official samples
  anchor the evaluator; hidden tests UNAVAILABLE (CF marathon); evaluator
  SPEC-DERIVED (tools/eval.py) anchored on samples; feasibility guaranteed by
  statement. Score = 1000·log10(S·GB+1) MINIMIZED, 3-decimals; invalid/no
  output = 10,000 penalty; TL 6 s / 256 MB; S ≤ 3·10^6; N 2..1000, M 1..100000.

- Rules: ≤N moves/step; ≤2 touches/server/step (departure+arrival each count);
  capacity = current load + arrivals (departures still counted — transient
  double-booking); one move per VM per step; gb = per-hop vm sum.

- Phase 1 contract: `problem/contract.yaml` (48 statused fields) +
  PROBLEM_CONTRACT.md.

- Phase 2 architecture: `architecture.md`.
- Phase 3 baseline: `src/baseline.cpp` → `build/baseline` (NO Makefile — build
  with `g++ -O2 -std=c++17 -Wall -o build/baseline src/baseline.cpp`; binary
  sha256 dc0d830b...). Two stages: exact BFS (N^M ≤ 2048, OPT) + deterministic
  greedy (direct vm-desc → chain + visited-set → displacement cap 4 → drain,
  freeze+2). rc=2 on genuinely stuck/infeasible input (never emits invalid).

- Independent evaluator: `tools/eval.py` (separate code from solver).
- mvt gate: `tools/mvt.py --bin build/baseline --gen-cases 97` → 97/97 valid,
  0 failures, 0 artifacts, worst gap vs OPT 0.000, deterministic. NOTE default
  --gen-cases 40 only covers seeds 7..46 — always pass 97.

- Medium sweep (n=4..6, m=6..10, rho 0.7, pairs, seeds 500..559): no solver
  failures on reachable instances; seed 550 = UNREACHABLE instance (VM2 wedged;
  generator guarantees packable mappings, not reachable transitions; mvt's
  gen_tiny filters via reachable(), the medium sweep must too). ~30/60 GEN NONE
  at medium size (pairs at rho 0.7 heterogeneous servers = regime limitation).

- Scale (cycles='none'): n=30 (m=50), n=100 (m=163) valid ~0.00 s. History:
  n=100/m=163 SEGV'd until the exact_plan states-overflow fix (bail DURING
  growth, never after) — 2026-08-10.

- Fixes (all recorded in research/notes/experiments-log.md "TEST 1 session"):
  states-overflow crash; gen.py infinite-rejection loop (rej>10000 guard) +
  budget-aware tail; reverted FFD/old-reserve (broke tiny gate 97→81).

- Mechanism dossiers: `history/2020_cloud_scheduling/candidates/`
  (exact-bfs-tiny.md, chain-order-visited-set.md, drain-pass.md,
  vm-desc-ordering.md).

- Phase 0 report: `reports/phase0-availability.md`.

### Remaining work (deep detail)

- [x] **Phase 4 formal edge battery** (tools/edge_battery.py — DONE 2026-08-10,
  22/22 PASS): verdict table + findings in `reports/phase4-evaluator.md`
  (found 2 real solver defects: SIGABRT on negative counts → N/M range check in
  main; BFS O(N²) subset enumeration at N=1000 → N>128 BFS gate).

- [x] **Phase 10 max-size probe** (DONE 2026-08-10, `reports/phase10.md`):
  termination now GUARANTEED for every input (WORK_BUDGET=3e8 charge + SAFETY +
  drain cap + BFS gate; calibrated 0.01–2.14 s across N=100..1000, RSS ~4-5 MB,
  output far under S≤3e6). FINDING: max-scale wall is COMPLETENESS, not runtime —
  greedy INCOMPLETE on the 45%-fill pair-swap regime at every scale (n=100:
  14,377 churn steps then rc=2 on a provably feasible instance; displaced VMs
  stranded on unrelated servers, visited+disp_used=4 prevent recovery). Same
  regime as seed 550 scaled up → Phase-7 candidate: 2-cycle move (O(|pend|) swap
  of pend a:s→t with pend b:t→s) when the direct pass stalls. Behavior-neutrality
  of the budget PROVEN vs pre-budget binary on 17 medium seeds (byte-identical).
  Binary sha c42a7bf9 (WORK_BUDGET=3e8 build).

- [x] **Phase 5 structural matrix** (DONE 2026-08-10, `reports/phase5-matrix.md`):
  24-cell sweep (rho × cycles primary; shapes/slack/displace/scale slices) with
  behavior-free phase counters (PHASES stderr: direct/chain/disp/drain).
  Findings: rho = dominant dimension (score ~2.6x, chain share 3%→24%); perm =
  hardest structural class (max chain+drain at every rho — the Phase-10 swap
  failure scaled down); displace>0 = distinct regime (steps x2, score x2.1);
  slack 0.15 removes rc2 but lowers score; heavy shapes infeasible at rho>0.5 by
  construction; scale genOK collapses at n>=12 (heterogeneous servers). Fixed 2
  gen.py bugs on the way (os import scope; float randint in heavy branch).
  tools/matrix5.py is the reproducible sweep.

- [x] **Phase 6 mechanism verification cells** (DONE 2026-08-10,
  `reports/phase6.md`): ablation binaries (-DNO_BFS/-DNO_VISITED/-DNO_DISP/
  -DNO_DRAIN), all gate-neutral (97/97). ALL FOUR MECHANISMS VERIFIED LIVE:
  BFS beats greedy on 27/178 tiny cases (15%, up to +9.5% score — step-packing;
  greedy never wins; first sweep was a silent blind spot — n=4/m=6 never fires
  BFS, 4^6>2048); visited set essential on 12/759 (rc2 without, ping-pong
  chains); displacement essential on 1/22 + 3 solution-diffs in the
  hot-server cell; drain essential on 4/16 (none@0.95). Full-swap cell
  reproduced at scale = Phase-10 finding (baseline rc2 on feasible pairs).

- [x] **Phase 8 anti-overfitting splits** (DONE 2026-08-10): seed-region pools
  generated under `history/2020_cloud_scheduling/cases/{train,val,holdout}/`
  (174/135/100 cases; 9 cells x per-cell quota; deterministic rc2 classes
  34/30/11 recorded). Selection = TRAIN+VAL; HOLDOUT once per candidate family.
  sizes honestly (synthetic, labeled). → reports/phase8.md.

- [x] **Phase 9 ablation** (DONE 2026-08-10, `reports/phase9.md`): one component
  at a time (-DNO_BFS/-DNO_VISITED/-DNO_DISP/-DNO_DRAIN/-DNO_VMDESC, all
  gate-neutral 97/97). Mechanism essentiality: phase6 cells. ORDERING FINDING:
  vm-desc (EXP-41, gate-distribution choice) does NOT transfer to the n=6/12
  pool — train +819 for vmdesc but val +2000 and holdout +773 for index-order
  (TRUE eval scores); 52-69% output diffs; both orderings win 20-35 cases in
  every region → per-step adaptive ordering is a Phase-7 candidate, baseline
  unchanged (frozen-solver discipline).

- [x] **TEST 1 report A-K** (DONE 2026-08-10 →
  `history/2020_cloud_scheduling/reports/report-test1.md`, sections A-K).
  REPORT GATE: APPROVED by user (2026-08-10) — "I'd approve the gate to
  TEST 2 (2022), with exactly the isolation discipline you've established."
  User's standing directive: TEST 1 output = framework-level HYPOTHESES,
  not 2026-solver changes. Hypothesis list (user-ratified):
  (1) adaptive ordering worth adding as a generic capability;
  (2) exact micro-solvers useful as validation/oracle components;
  (3) structural matrices need problem-specific dimensions;
  (4) mechanism claims need explicit preconditions + observations;
  (5) ablation is essential; (6) train/val/holdout genuinely useful;
  (7) deterministic termination budgets important;
  (8) independent evaluators essential;
  (9) candidate selection must not rely on aggregate score alone.
  **TEST 2 MUST explicitly track component survival** (user's table):
  independent evaluator / tiny exact oracle / structural matrix /
  mechanism verification / ablation / train-val-holdout / adaptive
  candidate search survive-or-not vs TEST 1; exact solver / vm-desc /
  BFS state encoding / drain-displacement = expected problem-specific,
  DO NOT transfer into 2026.

### Open notes

- The NERC 2020 statement says "two different cost functions" per the mission
  text — the archived statement.md uses ONE formula (S·GB). Check for a second
  cost function in the full statement (mission text mentions "minimizing two
  different cost functions, with a non-trivial tradeoff" — contract flags
  conflicts; verify against statement.md before finalizing report A).

- Budget: adaptive per charter §10. 2020 has a strong mechanism (drain/visited/
  BFS) already; further candidates only if the matrix/ablation shows a gap.

---

## TEST 2 — 2022 Task Scheduling and Data Assignment (45th ICPC WF Huawei, Problem 2)

Purpose: coupled scheduling + data placement (scheduling decisions influence
data placement → communication costs → later scheduling). Structural parallel
to edge/cloud coordination — but DO NOT assume the 2026 problem has it.
Links: https://codeforces.com/blog/entry/107590 ·
https://codeforces.com/contest/1751/problem/A (verify problem id on probe —
the id may be a placeholder; the announcement is the primary source).
Expected features: tasks, heterogeneous computing units, memory disks, varying
power/read-write speeds, task dependencies, data generated by tasks, makespan
minimization. Required analysis: local vs globally coupled decisions,
dependency effects, contention, communication costs, delayed consequences,
bottlenecks, critical paths, placement/scheduling interaction; find cases
where greedy looks good locally but creates downstream damage.

### Established facts

- Statement CONFIRMED (problem/statement.md, archived 2026-08-10 from CF 1751
  problem A): ℓ 6..10000 tasks, n 1..50 machines (power 1..20), m 2..30 disks
  (speed 1..20, capacity ≤ 1,050,000), task size 10..600, data 0..20, one
  combined acyclic DAG (task deps + data deps, M+N ≤ 500,000). Batch offline
  makespan-minimization; score = 100·(Σsize/Σpower + Σdata·(deg+1)/Σspeed) /
  makespan per test, invalid = 0; TL 15 s / 1024 MB per test; feasibility
  guaranteed (speed-1 disk with capacity ≥ Σdata exists); 1 official sample
  anchors the evaluator. 51 official tests, never released (2× n=10000, rest
  n≤800 — comment leak).

- Announcement: CF blog 107590 (14-day window 2022-11-17..30, Problem 1 =
  Central Software Institute "Parameterized Branch Instruction Stream
  Generation" = CF 1752, NOT our target; Problem 2 = HiSilicon = CF 1751).
  Results: CF blog 109841 — P2 winner icecuber, 2. Sung.An, 3. Psyho,

  4. Alex_2oo8, 5. eulerscheZahl.
- Participant infrastructure repo (jmerle, MIT): reference judge
  (results/score.py — spec-derived, example-anchored),
  deterministic generator + 50 labeled-synthetic inputs (~23 MB, STALE
  generator revision — validate before reuse as DEV pool), run harness
  (15000 ms timeout mirroring official TL), 20 Pegasus DAX workflow DAGs
  (Montage/CyberShake/Epigenomics/Inspiral/Sipht from WORKS'08) for
  realistic test structure.

- Boundary (charter §2): jmerle src/ v01-v07 + winner submissions RESERVED —
  no reading until post-candidate validation; scorer/generator/harness in
  play.

### Remaining work

- [x] Phase 0 probe (DONE 2026-08-10 → `reports/phase0-availability.md`);
  statement archived problem/statement.md

- [x] Phase 1-3 + MVT gate (DONE 2026-08-10): contract.yaml (13 groups,
  outdegree semantics CONFIRMED by example arithmetic — incoming-dep count
  gives wrong LB), PROBLEM_CONTRACT.md (render_contract.py), architecture.md
  (0/12 dims match — engine rejection expected), src/baseline.cpp +
  src/candidate1.cpp (H1 priority / H2 gap-scheduling / H3 popularity disks),
  tools/eval.py (INDEPENDENT, sample-anchored 96.1111… EXACT), tools/gen.py,
  tools/mvt.py (exact tiny-OPT oracle: topo orders × (mc,dk) assignments;
  UNKNOWN on budget). MVT: 20/20 valid+deterministic, OPT all converged,
  base gap 0..46%, cand1 0..32.5%. DEV pool 50 (jmerle synthetic, labeled):
  both valid 50/50; cand1 wins 34/ties 8/loses 8 (≤−4.3% on dense
  random-500k). Evidence: reports/phase3-baseline.md,
  candidates/candidate1-hypothesis.md, cases/dev/.

- [x] Phase 4 edge battery (DONE 2026-08-10, 20/20 PASS →
  reports/phase4-evaluator.md): hand arithmetic anchor (per-term ceil),
  19 mutated-schedule probes, boundary semantics pinned (a=d/c OK,
  adjacency OK). FOUND + FIXED evaluator defect: score_path crashed on
  malformed output instead of returning 0 (invalid→0 official).

- [x] Phase 5 structural matrix (DONE 2026-08-10, 40 cells x 3 seeds →
  reports/phase5-matrix.md): cand1 ≥ base in 35/40 cells, 5 ties, 0
  losses; edge concentrates at small n (+4–21% n=2, ≤ +0.3% n=50+chain);
  capacity dimension SILENT (tight/loose byte-identical) → dedicated
  saturated probe (identical outputs; pop inert under binding caps too).

- [x] Phase 6 mechanism cells (DONE 2026-08-10 → reports/phase6-mechanisms.md):
  9 ablation binaries (-DNO_PRIO/-DNO_GAP/-DNO_POP × combos). GAP =
  primary mechanism (carries most of every win); PRIO = conditional,
  SIGN-FLIPS at n=2-sparse (+1.78 available by disabling); POP = inert in
  all cells; allOff ≈ baseline → attribution complete.

- [x] Phase 7 candidate (DONE 2026-08-10 → reports/phase7-candidate2.md):
  `src/candidate2.cpp` = cand1 + gated braking (n≤2 && edges≤2ℓ &&
  chain_edges<ℓ/3). 40-cell matrix: 38 byte-identical, 2 changed = the
  two n=2-sparse cells, both pure gains (+2.5/+2.4%).

- [x] Phase 8 pools (DONE 2026-08-10 → reports/phase8-pool.md): fresh
  draw 200 instances (seeds 9000+, ell 40–400): cand2 ≥ cand1 all 200,
  both +3.7 mean over baseline.

- [x] Phase 9 ablation (DONE via Phase 6; single-word summary in phase8-pool.md)
- [x] Phase 10 runtime/validity (DONE 2026-08-10): ℓ=200k / 300k edges:
  cand2 1.28 s wall, output valid, score 104.53; baseline 1.01 s.
  Caveats: gen_scale O(ℓ²) pair loop caps matrix use at ~ℓ=1–2k (tool
  limitation, not solver); independent evaluator = 4.4 s Py at ℓ=200k.
  Deliverable: build/candidate2 (src/candidate2.cpp).

- [x] report A-K + gate (DONE 2026-08-10 → reports/report-AK.md): sections
  A-K + gate paragraph. TEST 2 COMPLETE — candidate2 passes: mechanism
  attribution complete; no class regression on matrix (40) or fresh pool
  (200); runtime/validity margins verified at ℓ=200k; edge battery green.
  See also: Methodology convergence section (cross-problem learning).

---

## TEST 3 — 2025 OXC Ports Arrangement (ICPC 2025 Online Winter Challenge, CF 2177)

Purpose: farthest from scheduling domain (optical cross-connect port
arrangement / network config); official materials describe final hidden
retesting with same parameters, different random seed, latest
non-zero-scoring submission — the overfitting-risk lesson. Do NOT force
scheduler/queueing/edge-cloud/SLO terminology; derive structure from the
problem. Required: valid configuration definition, objective, hard
constraints, parameterized test structure, visible vs hidden, invariant vs
random-instance-specific properties, evaluation protocol mimicking hidden-test
risk (train/validate on public, synthetic supplement, hold out seeds/params,
measure mechanism survival across instance realizations).
Link: https://codeforces.com/contest/2177

### Remaining work: [ ] Phase 0 probe → [ ] full pipeline → report A-K + gate.

---

## TEST 4 — 2023 Buffer Sharing (multi-tenant DB, ICPC 2023 Online Spring Challenge)

Purpose: shared capacity, eviction, multi-tenant contention, temporal
locality, policy design. Required questions: true scarce resource, what
creates contention, what's predictive, does history matter, delayed effects,
workload regimes, one-policy-dominates vs regime-variant policies.
Link: https://codeforces.com/blog/entry/112838

### Remaining work: [ ] Phase 0 probe → [ ] full pipeline → report A-K + gate.

---

## Cross-problem tracking

| Test     | Phase 0 | 1    | 2    | 3    | 4    | 5    | 6    | 7     | 8    | 9    | 10   | Report                              |
| -------- | ------- | ---- | ---- | ---- | ---- | ---- | ---- | ----- | ---- | ---- | ---- | ----------------------------------- |
| 1 (2020) | done    | done | done | done | done | done | done | done* | done | done | done | done — GATE: awaiting user approval |
| 2 (2022) | done    | done | done | done | done | done | done | done  | done | done | done | done                                |
| 3 (2025) |         |      |      |      |      |      |      |       |      |      |      |                                     |
| 4 (2023) |         |      |      |      |      |      |      |       |      |      |      |                                     |

*TEST 1 candidates (drain/visited-set/BFS/ordering) predate the program run —
   their selection used the medium sweep; Phase 8 will re-validate them on
   proper splits.

## Session resume pointers

- TEST 1 COMPLETE (2026-08-10): report A-K at
  `history/2020_cloud_scheduling/reports/report-test1.md`; REPORT GATE
  APPROVED by user → TEST 2 (2022) in progress.

- TEST 2 (2022) COMPLETE (2026-08-10): report A-K at
  `history/2022_task_scheduling/reports/report-AK.md`. All phases done;
  candidate2 (src/candidate2.cpp) frozen. See Methodology convergence
  section for the cross-problem learning (three-layer architecture, H-X
  hypothesis, canonical pipeline).

- **Methodology convergence section** (cross-problem, after TEST 2):
  three-layer architecture (universal infrastructure / general primitives /
  problem-specific mechanisms), canonical pipeline, 2026 hypotheses
  (H-X idle-window exploitation — hypothesis, NOT assumed), two fundamental
  discoveries (TEST 1: heuristic can look good and be wrong; TEST 2:
  regime-dependent sign flip detectable from instance). READ THIS before
  TEST 3 — it carries the methodology forward.

- TEST 1 key evidence files: reports/phase4-evaluator.md, phase5-matrix.md,
  phase6.md, phase9.md, phase10.md; pools cases/{train,val,holdout}; ablations
  build/baseline_{nobfs,novis,nodisp,nodrain,novmdesc} (-D flags in baseline.cpp).

- Key tool invocations (TEST 1): mvt gate 97, medium sweep (seeds 500..559),
  ASan build /tmp/baseline_asan, scale cases /tmp/scale_30|100.txt.

- All TEST 1 session findings: `research/notes/experiments-log.md` (entry:
  "TEST 1 session (2026-08-10)").

- Next problem: TEST 3 (2025 OXC Ports Arrangement, CF 2177) — farthest from
  the scheduling domain; do NOT force scheduler/queueing/edge-cloud
  terminology, derive structure from the problem. Then TEST 4 (2023 Buffer
  Sharing). Continue both before changing the 2026 architecture — the
  remaining two tests tell you whether the lessons are converging or are
  problem-specific.
