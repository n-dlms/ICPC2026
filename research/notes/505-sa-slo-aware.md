> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 505 — SLO-Aware Scheduling for LLM Inferences (Simulated Annealing, 2025)

- Status: **unavailable** — paper not located on arXiv under the title opencode cites ("SLO-Aware Scheduling for Large Language Model Inferences (2025)" by no specified authors). Multiple SLO-aware-LLM papers exist on arXiv (`2605.02329` Kairos, `2504.08784` SLOs-Serve, `2504.20068` JITServe) but none of them match the opencode description of a "simulated annealing-based scheduler with ~1ms overhead that boosts SLO attainment by up to 5×". Distilled from `src/opencode.md` lines 51-55. Reopen when a verified open PDF is located (record DOI).

## Claim

A simulated-annealing scheduler with ~1ms overhead boosts SLO attainment by up to 5×. Priority mapping based on SLO, input length, and predicted output length. SA explores sequence permutations and batch sizes.

## Relevance (Engine 2 — LNS)

This is the **theoretical anchor for Engine 2 (LNS refiner)**. The current `src/Solution.cpp` portfolio has Engine 1 (greedy), the frozen Engine 4 (EVT+SLAI+SJF+IMBAL), and a sketch of Engine 2 (LNS). The SA formulation here gives the canonical recipe for the LNS state space, neighborhood, and objective:

- **State:** permutation of requests (priority order) + batch-size assignment.
- **Neighbor:** swap two requests OR change one batch size.
- **Score:** G = n_meeting_SLO / total_latency (a ratio — note: this is a reasonable surrogate when the true score function is UNKNOWN; verify against `protocol/protocol.yaml` `score_function` once the spec drops — see docs/ASSUMPTIONS.md).
- **Temperature:** start high, cool over iterations.

5× SLO attainment gain is the upper-end empirical claim — anchor for the *expected* benefit of running LNS over a frozen greedy. Realistic for the very-step schedule distribution where SA explores well; not realistic when the schedule is already near-optimal (our frozen baseline may be near-optimal on simple cases — `docs/05-strategy-playbook.md` notes the 20-case suite is small).

## Algorithm sketch

```
state s := initial_greedy_solution()
T := T_high
while T > T_low and budget not exhausted:
    s' := s
    if rand < 0.5: swap two requests in s'.order
    else:          change one batch-size in s'.batches
    G_s  := score(s)
    G_s' := score(s')
    if G_s' > G_s or rand() < exp((G_s' - G_s)/T):
        s := s'
    T := cool(T)

```

Per-iteration ~1ms ⇒ on a single CPU budget of ~5s per case (research/day7 timing matrix), ~5000 iterations per case, gated by `tools/runtime_stress.py` (CPU-time, never wall — the archived working discipline, see README.md).

## Port plan

Engine 2 (LNS) in `src/Solution.cpp`, compile-time `-DENGINE_LNS`. Sketch:

```cpp
#ifdef ENGINE_LNS
struct LnsState { std::vector<int> order; std::vector<int> batches; };
double lns_score(const LnsState& s, const Reqs& R) {
    // G = (n_meeting_SLO) / total_latency — VERIFY vs spec score_function
    return ...;
}
LnsState lns_neighbor(const LnsState& s, RNG& rng) {
    LnsState s2 = s;
    if (rng.uniform() < 0.5) std::swap(s2.order[rng.next()%N], s2.order[rng.next()%N]);
    else                      s2.batches[rng.next()%N] = 1 + rng.next()%MAXB;
    return s2;
}
void run_lns(Reqs& R, int cpu_budget_us) {
    LnsState s = greedy_initial(R);            // Engine 1 init
    double G = lns_score(s, R);
    for (double T = T_HIGH; T > T_LOW && !cpu_budget_expired(cpu_budget_us); T *= COOL) {
        LnsState s2 = lns_neighbor(s, rng);     // bounded copy
        double G2 = lns_score(s2, R);
        if (G2 > G || rng.uniform() < std::exp((G2 - G)/T)) { s = std::move(s2); G = G2; }
    }
    emit(s);
}
#endif

```

**Hard gates before this gets near a submission:**

1. **Score-function shape.** `tools/score_probe.py` MUST confirm the objective is `n/t` or compatible. If the spec scoring is absolute-throughput (per docs/ASSUMPTIONS.md), `G = n_meeting_SLO / total_latency` may be the wrong surrogate — `n_meeting_SLO` alone (raw count) may be correct. If relative-baseline, the surrogate must be the *gap* over the baseline, not the absolute. NEVER submit an LNS before the score function shape is KNOWN (block in the day-1 checklist per docs/DECISIONS.md).

2. **CPU-time budget, not wall.** The `cpu_budget_expired` check uses `std::clock()` deltas (the archived working discipline, see README.md). Multi-threading (`std::jthread`/OpenMP) is FORBIDDEN — never let the model suggest it (the archived working discipline, see README.md).

3. **Never time out.** The LNS must emit a valid plan immediately on budget exhaustion — `emit(s)` (the incumbent best-so-far) is the contract, NOT "wait for the next iteration". Always be able to emit a valid answer in < wall backstop.

4. **A/B gate.** `python3 tools/ab_test.py --baseline build/solution_frozen --candidate build/solution_lns cases/*.txt cases/adversarial/*.txt`. Acceptance is robust dominance (valid, sum ≥ baseline, no class regression beyond tolerance per `docs/08-validation-lab.md`). The frozen solver is the incumbent; LNS does NOT win by being cleverer on one case, it wins by *no class regression*.

## Weaknesses

- ~1ms overhead is reported per iteration; on large-N cases with a long horizon, the per-iteration cost may be much higher (a full schedule simulation per neighbor). Budget the iteration count by simulating a few measure-then-scale, not by the paper's promise.
- SA neighborhoods are weakly-structured; an LNS with *structured* neighborhoods (e.g. relocate-block, swap-phases) typically dominates pure random SA on a constrained problem. The paper's "swap two requests OR change one batch size" is the simplest case; richer neighborhoods are a port-time decision.
- Random RNG breaks determinism unless seeded + stashed. `tools/fast_judge.py --determinism-runs 3` is the gate (the archived working discipline, see README.md). A non-deterministic LNS is a silent-failure generator — invalid run.
- The "5× SLO attainment" headline is a max over the paper's evaluation set; the average is what matters and we don't have it. Anchor it as an upper bound, not a forecast.

## Sources

- `src/opencode.md` lines 51-55 (algorithm description), 122-130 (C++ sketch).
