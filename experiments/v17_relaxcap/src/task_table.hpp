#ifndef ICPC2026_TASK_TABLE_HPP
#define ICPC2026_TASK_TABLE_HPP

// Layer 2: task-time table — load, sort, interpolate. Immutable after load.
#include "protocol.hpp"

#include <algorithm>
#include <vector>

namespace icpc2026 {

enum class Step : uint8_t {
    PREFILL_PRE = 0,
    PREFILL_PROC = 1,
    PREFILL_POST = 2,
    DECODE_PRE = 3,
    DECODE_PROC = 4,
    DECODE_POST = 5
};

class TaskTimeTable {
public:
    void load(int N, const std::vector<std::vector<double>>& rows);
    double get(Step step, int batch_size) const;
    int row_count() const { return static_cast<int>(rows_.size()); }
    // Largest listed batch_size for this step (Spec D-004: in [1,4096]).
    // Returns 0 if the column has no listed values (all -1; get() then returns a
    // constant, so batching is always profitable and bounded by n_ready instead).
    int max_batch_for(Step step) const;
    // Listed (batch_size, duration) pairs for a step's column, sorted by
    // batch_size. Empty when the column has no listed durations.
    const std::vector<std::pair<int, double>>& listed(Step step) const {
        return cols_[static_cast<size_t>(step)];
    }

private:
    std::vector<std::vector<double>> rows_;
    std::vector<std::vector<std::pair<int, double>>> cols_;
};

Step step_of(StepKind k);
StepKind kind_of(Step s);

}

#endif