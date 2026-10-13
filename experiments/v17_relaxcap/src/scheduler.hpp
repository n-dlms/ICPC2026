#ifndef ICPC2026_SCHEDULER_HPP
#define ICPC2026_SCHEDULER_HPP

// Layer 4: decision engine — decide(state, t) -> assignments, work-conserving.
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

}

#endif