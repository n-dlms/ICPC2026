#ifndef ICPC2026_TASK_TABLE_HPP
#define ICPC2026_TASK_TABLE_HPP

// =============================================================================
// task_table.hpp — Layer 2: the judge-supplied task-time table.
//
// N rows of (batch_size, six step durations). Lookup semantics per the
// statement: sort rows by batch_size; exact row wins; between rows, linear
// interpolation; below the smallest row, clamp to it; above the largest, clamp
// to it; -1 cells are skipped (every column has at least one listed value).
// The table is immutable after load; all hot-path queries are O(log N).
// =============================================================================
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

    // World-A instrument (2b): argmin over m in [1,4096] of the per-token
    // decode E-cost f(m) = (2S + dpre(m) + dpost(m))/m, for the startup
    // schedule cost S. Cached per S value (computed at most once per run).
    // On superlinear tables the argmin lands at small m by construction, so
    // consumers gated on this value are automatically safe there.
    int decode_m_star(double S) const {
        if (m_star_S_ == S && m_star_ > 0) return m_star_;
        double best = 1e300;
        int star = 1;
        for (int m = 1; m <= 4096; ++m) {
            const double f = (2.0 * S + get(Step::DECODE_PRE, m)
                              + get(Step::DECODE_POST, m)) / static_cast<double>(m);
            if (f < best) { best = f; star = m; }
        }
        m_star_S_ = S;
        m_star_ = star;
        return star;
    }

private:
    mutable double m_star_S_ = -1.0;
    mutable int m_star_ = 0;
    std::vector<std::vector<double>> rows_;
    std::vector<std::vector<std::pair<int, double>>> cols_;
};

Step step_of(StepKind k);
StepKind kind_of(Step s);

}

#endif