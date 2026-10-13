> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Engine-mapping proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Portfolio Engine Mapping (Engine 0–4 → algorithm → paper → role)

Synthesizes the opencode Part-3 portfolio table (`src/opencode.md` lines 100-151) with the archived "Portfolio Architecture (Single-File Implementation)" section (see src/ARCHITECTURE.md). The submission is one C++ file; the portfolio is the set of *compile-time* candidate binaries, selected at *freeze time* by `tools/portfolio_runner.py`. There is NO per-instance regime switching (the archived discipline is explicit on this — see docs/DECISIONS.md).

Note: the archived working discipline (see README.md) renames "Engine 4 (Frozen)" to "the frozen incumbent" — this document uses the opencode names for clarity but the archived naming is authoritative for code. The frozen solver is the current `src/Solution_asort_frozen.cpp` (sha-pinned, never edited — see docs/DECISIONS.md for the binary sha `44246348...`).

## Engine 0 — Parser/Validator (Pre-Solver Safety Layer)

- **Algorithm:** Protocol validation + constraint validation + structural cell extraction + infeasibility detection (genuine `impossible_slo` via `judge_min_completion()`).
- **Paper:** Rayward-Smith 1992 (NP-completeness justifies the heuristic need — paper 507).
- **Role:** Early warning system; runs in <1% of total budget; structured feature summary consumed by `tools/case_classify.py`.
- **Status:** IMPLEMENTED — `tools/engine0_probe.py` (standalone) and the `in_range` guards in `main()` (`src/Solution.cpp`) mirror the C++ enforcement (archived "Engine 0" section, see src/ARCHITECTURE.md).

## Engine 1 — Greedy (SLO-MAEL with aging + WMSH)

- **Algorithm:**
  - SLO-MAEL with aging (paper 503): priority = base (1 / max(1, SLO − expected_latency)) + age_weight·age + violation_degree. Fallback to MAEL when no SLO violations projected.
  - WMSH three-phase (paper 506): STH (satisfy throughput = admission control) → PRH (processor reduction = consolidate, no unnecessary round-trips) → LMH (latency minimization = LOOKAHEAD/SLAI/EVT on the feasible plan).
- **Papers:** KAIST SLO-MAEL (503), Euro-Par WMSH (506). SOLA (504) is the literature anchor for state-awareness; the existing `next_stage`/`seen` machinery in the frozen solver already implements it.
- **Role:** Fast safety net; **Day-1 submission candidate** (bank a non-zero score early per docs/DECISIONS.md).
- **Status:** PRIMARY — the frozen solver (Engine 4) is structurally an Engine 1 with opencode enhancements layered (LOOKAHEAD/SLAI/SJF/EVT/IMBAL_GATE). The pure-Engine-1 binary (`build/solution`, no `-D` flags) already exists and is "freeze-safe" per docs/DECISIONS.md. Aging is a NOT-YET-PORTED extension (paper 503 port plan); confirm `rejected-ideas.md` doesn't preclude it.

## Engine 2 — LNS (Simulated Annealing)

- **Algorithm:** Simulated annealing over (permutation, batch_size) state. Neighbor = swap-2-requests OR change-1-batch-size. Score = G = n_meeting_SLO / total_latency (VERIFY against spec — see docs/ASSUMPTIONS.md). Temperature = high→cool.
- **Papers:** SLO-Aware SA 2025 (505).
- **Role:** Refiner; post-greedy improvement on tight-SLO / loose-SLO-large-batch cases (opencode line 150).
- **Status:** SKETCH in `src/Kolution.cpp` under `#ifdef ENGINE_LNS`. Hard gates before submission:
  - `tools/score_probe.py` confirms the score-function shape is compatible (block Day-1 checklist on `score_function` being KNOWN — see docs/ASSUMPTIONS.md).
  - CPU-time budget (`std::clock()`) — never wall. NEVER multi-threaded.
  - Always able to emit a valid incumbent on budget exhaustion (the archived working discipline, see README.md).
  - `tools/ab_test.py` on `cases/*.txt` (20-case freeze gate) AND `cases/adversarial/*.txt` (class regression) — robust dominance only.

## Engine 3 — Flow (ZB-V or CrossPipe greedy, conditional on pipeline-friendly spec)

- **Algorithm:**
  - If stages have explicit communication (the spec defines edge↔cloud communication as a countable variable): ZB-V from Zero Bubble (paper 501) — the V-shaped 2-chunks-per-worker assignment with warm-up→1F-1B-1W→tail. ILP-formulable; the greedy heuristic + automatic-schedule search is the practical port.
  - If the spec reveals a pipeline with blocking (no inter-stage buffer): CrossPipe greedy (paper 502) `CrossUDSub` — incremental earliest-schedulable ready-op scheduler with bandwidth-occupancy queue. Wave vs unidirectional by delay regime (unidirectional wins as delays grow).
  - If the spec allows **variable split points** (UNKNOWN until Aug 14): NSGA-III over (K, L) from paper 509.
- **Papers:** Zero Bubble (501), CrossPipe (502), NSGA-III segmentation (509).
- **Role:** Conditional — "if the spec reveals a pipeline-friendly structure" (opencode line 106). If the spec is a plain greedy-favorable scheduling problem, Engine 3 is not built.
- **Status:** NOT BUILT. Day-0 trigger: run `tools/reconcile_spec.py --blast pipeline_structure` and `--blast split_point` on the spec. If both ASSUMED → skip. If KNOWN-as-pipeline-friendly → port ZB-V or CrossPipe greedy.

## Engine 4 — Frozen (the incumbent)

- **Algorithm:** As-is — `src/Solution_asort_frozen.cpp` (sha-pinned `927800f7...` for source, binary `44246348...` for the rebuilt executable). The behavioral fingerprint is: EDD admission + USA1 decode-ordering + per-tick plan-tranche LOOKAHEAD + SLAI last-schedulable-time key + SJF admit key + IMBAL_GATE (disables SLAI/SJF keys on severely-imbalanced graphs `3*E < C || 3*C < E`) + EVT event-driven active lists + cap4 SIM_WORK_CAP = 1.8e9.
- **Papers:** the distillation across papers in `research/notes/` (notably 402, 403, 405, 406) plus experimental exploration in `research/notes/experiments-log.md` (EXP-1 through EXP-42).
- **Role:** The incumbent; assumption-validated; DO NOT EDIT (sha-pinned). Variants go into separate source files / conditional compilations and A/B test against this.
- **Status:** FROZEN — `build/solution_evt_cap4_asort_frozen` (binary sha `44246348...`). docs/DECISIONS.md is the source of truth.

## Structural-cell routing rule (opencode lines 145-151)

Per opencode, `tools/portfolio_runner.py` should evaluate each engine against each structural cell and select the winner. docs/DECISIONS.md is explicit: "No per-instance regime switching: the selected engine runs uniformly across all test cases." **The portfolio picks the engine with best aggregate+per-cell performance, not a context-switch per case** (see src/ARCHITECTURE.md). So the opencode routing rule is a **freeze-time** rule across the structural cell matrix, NOT a runtime rule.

The cells (archived "Engine 0", see src/ARCHITECTURE.md, + `tools/case_classify.py`):

- `comm_bound` (total_comm > total_compute) → prefer Engine 3 (flow/pipeline) if pipeline-friendly spec, else Engine 4 (frozen).
- `slo_tight` (min(SLO_i) < 2·max(stage_time)) → prefer Engine 1 (greedy with aging) or Engine 4. **The frozen solver is already SLO-tight-hardened** via SLAI.
- `decode_saturated` (N·avg_decode > T·decode_capacity) → aging is the closure (paper 503). Engine 1+aging or Engine 4 with aging.
- `loose SLO, large batch` → Engine 2 (LNS) can refine without feasibility risk.
- `small N (<20)` → not a portfolio route; use `tools/opt_gap.py` branch-and-bound as an offline oracle to *judge* the portfolio, never as a runtime engine.

The structural-cell classifier (`tools/case_classify.py`) outputs the cell labels; `tools/matrix_gen.py` generates the cell matrix; `tools/portfolio_runner.py` runs each engine binary against each cell and ranks. The *winner* is the binary that has best aggregate score across cells with no class regression (per `docs/08-validation-lab.md` robust-dominance discipline).

## Day-1 cadence (per docs/DECISIONS.md)

1. `python3 tools/problem_contract.py --statement problem/statement.txt --out problem/` (spec → contract.yaml).
2. `python3 tools/arch_mapper.py --contract problem/contract.yaml` (dimension table + regime verdict vs existing engine — do NOT reuse architecture blindly).
3. `python3 tools/case_classify.py 'cases/*/*.txt' --table` (structural cells already populated).
4. `python3 tools/freeze_gate.py --candidate <bin> --baseline <frozen>` (CANDIDATE FREEZE battery).
5. Accept/reject per `docs/08-validation-lab.md` robust dominance — class regression and validity block regardless of aggregate score.

## Sources

- `src/opencode.md` lines 100-151 (Part 3 portfolio table + routing rule).
- `research/notes/501-507-*.md` (the distilled paper notes above).
- src/ARCHITECTURE.md, docs/DECISIONS.md (archived portfolio/engine/spec-arrival sections).
