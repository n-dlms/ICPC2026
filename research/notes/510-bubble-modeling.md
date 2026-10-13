> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 510 — Bubble Modeling as a First-Class Variable

Per opencode Trap B (`src/opencode.md` lines 7-9) and the C++ sketch (lines 131-144). The "bubble" — idle time between edge and cloud phases — is the **primary optimization target** of pipeline-parallel scheduling research; it is NOT a side-effect of the schedule. Most competitors will optimize makespan or throughput directly and leave bubble time on the table.

## Claim

Model bubble duration as an *explicit* decision variable and minimize it through computation-communication overlap. Pipeline-parallel research (Zero Bubble, CrossPipe) formalizes this. opencode's verdict: competitors pattern-matching on "scheduling problem" miss the bubble dimension entirely.

## Relevance

The ICPC problem is the U-shaped LLM split — every prefill request makes an edge↔cloud round-trip per decode token. Sequential prefill → comm → decode → comm → prefill 🔁 processing creates idle "bubbles" on the edge while the cloud is busy, and on the cloud while the edge is busy. The bubble is **the variable that determines throughput** under fixed SLO feasibility.

The frozen solver (`src/Solution_asort_frozen.cpp`) already models bubbles implicitly: the `wait_list` of WAIT-state requests and the `dw_list` of decode-waiting ones are the *occupancy variables* over which the per-tick emit decisions are made. The LOOKAHEAD simulation walks the per-tick plan-tranche and accepts a candidate dispatch only if its marginal effect on completion is not strictly worse than no-action simulation.
**Bubble time is folded into the plan-tranche, not explicitly named** — this is acceptable for a frozen solver that works empirically, but opencode's trap-warning is that an AI-assisted rewrite will optimize makespan and *not model the bubble*. The bastion here is the *existing frozen solver*; the *risk* is a future rewrite that drops the LOOKAHEAD/plan-tranche structure.

## Algorithm sketch (opencode lines 131-144)

```cpp
struct Stage {
    double compute_time;       // for us: edge prefill (pe), cloud compute (pc), edge decode per token (per)
    double comm_time_to_next;  // for us: COMM (edge→cloud, cloud→edge)
};

// Per-tick, the schedule has occupancy of E (edge) and C (cloud) over time.
// Bubble = total idle time on either machine where work was available but blocked.
double bubble_duration(const Schedule& s) {
    double bubble = 0;
    // For each tick where edge is idle but wait_list has admit-able candidates:
    //   bubble += tick_time
    // For each tick where cloud is idle but cloud-waiting candidates:
    //   bubble += tick_time
    // (This is the *excess* idle — we don't count idle-time when nothing is ready.)
    return bubble;
}

// Minimize: schedule_with_min_bubble under SLO feasibility constraints.

```

The *precise* shape for our problem:

- `E_bubble(t)` = edge is idle at tick t AND `wait_list` at t had an admit-able request whose `doom` was false.
- `C_bubble(t)` = cloud is idle at tick t AND there is a cloud-waiting request (a C_EP-completed request whose COMM has arrived).
- `total_bubble = Σ E_bubble(t) + Σ C_bubble(t)` over the schedule horizon.

## Port plan

This is *not* a new engine; it is a **diagnostic instrumentation** of the frozen solver and a *constraint* on LNS (Engine 2). Concretely:

1. **Behavior-free instrumentation** in `src/Solution.cpp` under `#ifdef BUBBLE_STATS`:
   - Per-tick, increment `e_bubble` when edge idle AND `wait_list` had a candidate; increment `c_bubble` when cloud idle AND a cloud-waiting exists.
   - Emit to stderr at the end of `run()` (NOT stdout — stdout is the judge protocol). Test by running the assert build over the suite (the archived working discipline, see README.md); verify stderr is byte-identical across the 20-case freeze gate (assert build's `ASSERT_STATE` parity check, archived "Engine 0" / validation builds, see README.md).

2. **Diagnostic uses:**
   - Compare `build/solution` (neutral Engine 1) vs `build/solution_evt_cap4_asort_frozen` (Engine 4) — the frozen solver SHOULD have a smaller total bubble. If not, the LOOKAHEAD + EVT machinery is *not* doing what it claims and the EXP-28/EXP-31 attribution is wrong.
   - Find the cells where bubble is highest (`tools/case_classify.py` correlation with bubble) — those are the cells where LNS/Flow engines are most promising.
   - Cross-validate with `tools/metamorphic.py` time-shift (bubble should shift by exactly s under a time-shift of s — the archived working discipline, see README.md).

3. **LNS objective integration (Engine 2 only):**
   - If `tools/score_probe.py` confirms an absolute-throughput score function, the LNS score (paper 505) can incorporate `- λ · total_bubble` as a regularization term — but ONLY after the basic n_meeting_SLO surrogate is verified. NEVER make bubble the primary key without confirmed shape (the score could be SLO-attainment alone, in which case bubble is instrumental, not direct).

## Weaknesses

- The frozen solver's `wait_list`/`dw_list` already implicitly model the bubble; adding an explicit counter risks double-counting or mis-attribution (the existing LOOKAHEAD already charges for plans that fail to maintain admit-able options). Run `tools/crash_sweep.py` + ASan/UBSan after instrumentation (the archived working discipline, see README.md).
- Bubble is *instrumental* — what we ultimately get scored on is the (UNKNOWN) score function. If the function is raw throughput or SLO-attainment, bubble is a great proxy. If it's relative-to-baseline, bubble-minimization may *increase* the bubble of the baseline more than ours (over-optimization). Verify before using as a primary key in LNS.
- The opencode sketch assumes floats; the frozen solver uses integer ratio keys (`usa1_key`, `sjf_key`) for determinism. Bubble is an integer (tick count) — keep it integer, no float.

## Sources

- `src/opencode.md` lines 7-9 (Trap B), 131-144 (C++ sketch).
- `research/notes/501-zero-bubble-pipeline-parallelism.md`, `502-crosspipe-cross-datacenter-pipeline.md` (the pipeline-parallel literature anchor for bubble minimization).
- `research/notes/402-work-conserving-queueing-theory.md` (the work-conserving theoretical foundation — minimizing bubble IS the work-conservation theorem's quantity).
