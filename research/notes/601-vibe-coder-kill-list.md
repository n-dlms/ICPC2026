> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Kill-list below is preserved as a historical working note; defenses landed are recorded in `research/notes/experiments-log.md` and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 601 — Vibe-Coder Kill List + AI-Bug Catalog

Synthesis of opencode Part 4 (`src/opencode.md` lines 153-161) and Part "Hour 0-12" (`src/opencode.md` lines 487-512). The ICPC challenge explicitly permits AI tools; this is a filter, not generosity. ICPC has built the problem to exploit every AI failure mode. This note is the catalog of what AI-assisted submissions will get wrong, and our defense per trap.

## The Vibe-Coder Kill List (opencode lines 153-161)

| Vibe-coder approach                            | Why it dies                                                                            | Our defense                                                                                                                                                           |
| ---------------------------------------------- | -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Use a priority queue with shortest-job-first" | Ignores phase interference and bubbles; treats prefill and decode as fungible "tasks"  | Frozen solver's `next_stage()` state machine + phase-separated admit (C_EP vs C_ED); per-tick `wait_list`/`dw_list` keep decode-rounds separate                       |
| "Model this as standard job-shop scheduling"   | Job shop has no bidirectional communication delays; U-shape has edge↔cloud round-trips | Rayward-Smith 1992 (`research/notes/507-*.md`) — open shop with delays is NP-complete even for 2 machines; we use heuristic portfolio, not a polynomial-optimal claim |
| "Just batch all requests for throughput"       | Batching prefill + decode degrades both                                                | Phase-aware frozen solver; `case_gen.py --force-adversarial prefill_in_decode_batch` regression (to add per Trap A)                                                   |
| "Simulated annealing with random neighborhood" | Random swaps don't respect pipeline dependencies                                       | LNS uses *structured* neighborhoods (batch size + permutation, paper 505); the SA in opencode line 159 specifically respects pipeline dependencies                      |
| "Optimize for makespan"                        | Wrong objective if scoring is SLO-attainment-based                                     | `tools/score_probe.py` blocks the Day-1 checklist until shape is KNOWN (see docs/ASSUMPTIONS.md). Makespan is one specific shape — never assume it |
| "Assume communication is instant"              | Fails on comm-bound test cases                                                         | `case_gen.py --force-adversarial zero_comm` and `comm_dominant` families (zoo entries in cases/zoo/). `tools/engine0_probe.py` `comm_bound` cell label                |

## The Specific Bugs AI Submissions Will Have (opencode lines 487-512)

These are the *concrete* implementation bugs that appear in LLM-generated `solution.cpp` files, and our defense.

### Bug 1 — The "Makespan" Misobjective

- **What they'll write:** `minimize max_completion_time`
- **What the judge scores:** probably `sum(throughput_i · SLO_met_i)` or relative-to-baseline.
- **Defense:** `tools/score_probe.py` fingerprints the actual scoring from the judge's `protocol/protocol.yaml` (docs/ASSUMPTIONS.md "Score Function Shape Detection"); an objective is adopted ONLY after KNOWN confirmation, never assumed.

### Bug 2 — The "Blocking" Blindness

- **What they'll write:** Standard open-shop / flow-shop (jobs can wait in infinite buffers between stages).
- **What the problem is:** 2-machine open shop with blocking (no buffer between edge and cloud) and communication delays. After edge prefill completes, the request *blocks the edge* until cloud accepts it.
- **Defense:** The frozen solver's `wait_list` (WAIT state) and the COMM round-trip modeling in `step()` (`fast_judge.py`) already implement blocking semantics. The `dw_list` (decode-waiting) holds requests that have completed cloud compute but are waiting for edge decode slot — that's the blocking state. Cross-validate: `tools/fast_judge.py --feasibility-check` re-derives completion including blocking; if it disagrees with the solver's own score, desync = abort (use `build/solution_assert` per docs/DECISIONS.md "Validation builds").

### Bug 3 — The "Phase Fungibility" Error

- **What they'll write:** `struct Job { int processing_time; int deadline; };`
- **What the problem requires:** `struct Request { int prefill_time; int decode_time; int cloud_time; int comm_time; int SLO; int Lout; };` — at minimum 5 parameters.
- **Defense:** The frozen solver's `Req` struct in `src/Solution_asort_frozen.cpp` has the full 5-parameter structure. The `case_gen.py` generator exercises varying Lout/prefill/decode ratios (the "prefill-heavy vs decode-heavy" cell per opencode line 240). Do not collapse parameters — the test generator explores the collapsed dimensions and exposes the bug.

### Bug 4 — The "Integer Time" Assumption

- **What they'll write:** `int start_time, int end_time;`
- **What the problem might use:** microsecond precision, or double for timing.
- **Defense:** The frozen solver uses `long long` for tick accounting (no floating point in the timing-critical path — see the integer-ratio key pattern). `tools/fast_judge.py --overflow-check` scans for int32 hazards. The `huge_T` (T up to 8000 gen / 50000 stress per cases/zoo/ "Adversarial Case Zoo") catches under-allocated tick buffers. **Never use float for deadlines** — the SLO boundary exactness (Trap D above) requires integer arithmetic.

### Bug 5 — The "Static Schedule" Output

- **What they'll write:** A schedule computed once from a full input batch at start.
- **What the problem requires:** Online scheduling — requests arrive over time; the judge feeds per-tick stdin lines (the protocol per src/ARCHITECTURE.md "Engine 0" Per-tick `k` lines).
- **Defense:** The frozen solver's per-tick `run_tick_policy()` loop is the online-schedule implementation. The Day-5 input-loop robustness fix (docs/DECISIONS.md "Variants" `cap4_fix`) is the canonical defense — reading `k` mid-stream without proper EOF check left k UNINITIALIZED → infinite loop on truncated streams. Test with `tools/rehearsal.py` (per docs/DECISIONS.md "Rehearsal" — input lifecycle: empty/garbage/partial/header-only/truncate/END-early/N1/huge-T/sigpipe/dribble/paused-reader).

### Bug 6 — The "No Infeasibility Handling"

- **What they'll write:** Code that assumes all requests can be scheduled.
- **What the judge includes:** test cases where some requests MUST miss their SLO.
- **Defense:** The frozen solver's `doom` gate (`est = t + pe + COMM + pc + COMM + Lout*per; if (est > D || est > T) doomed = true;` per EXP-42 Finding B). Note: the `doom` formula uses the NAIVE `durations()` sum; `judge_min_completion()` (the actual judge semantics) is ~1/3 earlier due to phase pipelining — `impossible_slo` zoo uses `judge_min_completion()` so the solver's `doom` over-marks doomed requests that the judge would actually complete on time. **This is the known EXP-42 Finding-B score leak; fixing it requires a re-freeze decision on Day 1** (the frozen solver is sha-pinned). Post the known leak note in `build/checkpoint-log.json` as a Day-1 candidate fix.

## The 6-Hour Vibe-Coder Trajectory (opencode lines 425-475)

Hour 0: paste spec into ChatGPT → generic priority-queue O(N log N) scheduler that passes the sample.
Hour 2: "make it faster" → faster priority queue, segment tree, maybe multithreading — but the bottleneck is *bubble minimization*, not data-structure speed.
Hour 6: "handle impossible SLO, handle zero comm" → try/catch + if (impossible) skip branch — both wrong (infeasibility is a structural cell, not an exception).
Hour 12: "score is 0 on hidden tests, debug this" → guesses; changes the priority function; suggests a completely different algorithm — debugging a model of the problem the model doesn't have.
Hour 48: "forget that, write a new approach" → new algorithm, new bugs, no validation harness, no fallback. Final submission scores lower than the first one because there's no frozen solver.

The structural reason (opencode line 477-486): LLMs are *local optimizers over code space*; this problem requires *global optimization over algorithm space*. You can't get there by incrementally improving a bad approach.

## Our Defense: The System vs The File (opencode "Paradox" lines 309-317)

| How the C++ file is produced | Vibe coder                           | Us                                                                                    |
| ---------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------- |
| Process                      | Prompt → Model → Compile → Submit    | Research → Hypothesis → Implement → Validate → A/B Test → Select → Distill → Submit   |
| File contents                | Model's best guess                   | Empirically strongest algorithm for the observed test distribution                    |
| Hidden tests                 | Unknown (never tested adversarially) | Known (tested against `case_gen.py --force-adversarial` and structural cell variants) |

The C++ file is the *artifact*; our system is the *process that produces the artifact*. In a contest where everyone has the same AI models, the process is the only edge.

## Sources

- `src/opencode.md` lines 153-161 (kill-list table), 425-475 (Hour 0-12 trajectory), 477-512 (structural reason + bug catalog).
- docs/DECISIONS.md "AI-Assisted Development Guardrails" (the existing defense rules — "The model does not understand bubbles", "The model hallucinates constraints", "The model optimizes for 'looks correct'", etc.).
