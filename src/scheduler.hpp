#ifndef ICPC2026_SCHEDULER_HPP
#define ICPC2026_SCHEDULER_HPP

// =============================================================================
// scheduler.hpp — Layer 4: the decision engine.
//
// One entry point: Scheduler::decide(state, t) -> up to K+1 assignments, one
// per free resource (the local computer plus each idle cloud). All assignments
// of a frame start simultaneously at the frame timestamp t. The engine is a
// work-conserving greedy whose choices are delegated to the Layer-5 policies
// (cloud / chunk / batch / delay); the local arbitration order (P PRE vs
// P POST vs D PRE vs D POST) encodes the load-bearing theorems recorded in
// docs/DECISIONS.md (D-140 Theorem B, D-146/D-147).
// =============================================================================
#include "protocol.hpp"
#include "state.hpp"
#include "task_table.hpp"
#include "policies.hpp"

#include <vector>

namespace icpc2026 {

struct Assignment {
    TaskSpec spec;
};

class Scheduler {
public:
    Scheduler(const Config& cfg, const TaskTimeTable& table, const Policies& policies)
        : cfg_(cfg), table_(table), policies_(policies) {}

    std::vector<Assignment> decide(State& state, double t);

private:
    TaskSpec pick_local(State& state, double t);
    TaskSpec pick_cloud(State& state, int c, double t);
#ifndef PSGO_STATIC
    // PSGO Batch 3: dynamic batch-cap feedback loop. Runs every F frames.
    // PSGO_STATIC (D-049/D-061): the engine is compiled and SOURCE-removed
    // from the submission artifact; it exists only in the dev build.
    void psgo_adjust(State& state);
    long long frames_ = 0;  // frame counter (F=1000 cadence)
#endif

    const Config& cfg_;
    const TaskTimeTable& table_;
    const Policies& policies_;
};

#ifdef LOCAL_DIAG
// Instrument 3: end-of-run group-size histogram dump (scheduler.cpp).
void diag_dump_hist();
#endif

}

#endif