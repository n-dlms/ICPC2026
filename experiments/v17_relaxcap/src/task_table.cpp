// Layer 2: task-time table.
#include "task_table.hpp"

#include <algorithm>
#include <cassert>

namespace icpc2026 {

void TaskTimeTable::load([[maybe_unused]] int N, const std::vector<std::vector<double>>& rows) {
    assert(N >= 1);
    rows_ = rows;
    assert(static_cast<int>(rows_.size()) == N);
    for (const auto& r : rows_) {
        assert(r.size() == 7);
        assert(r[0] >= 1.0);
        for (int c = 1; c <= 6; ++c) {
            [[maybe_unused]] const double v = r[static_cast<size_t>(c)];
            assert(v == -1.0 || v > 0.0);
        }
    }
    std::sort(rows_.begin(), rows_.end(),
              [](const std::vector<double>& a, const std::vector<double>& b) {
                  return a[0] < b[0];
              });
    for (size_t i = 1; i < rows_.size(); ++i) {
        assert(rows_[i][0] > rows_[i - 1][0]);
    }

    cols_.assign(6, {});
    for (const auto& r : rows_) {
        const int bs = static_cast<int>(r[0]);
        for (int c = 0; c < 6; ++c) {
            const double v = r[static_cast<size_t>(c) + 1];
            if (v != -1.0) {
                cols_[static_cast<size_t>(c)].emplace_back(bs, v);
            }
        }
    }
}

double TaskTimeTable::get(Step step, int batch_size) const {
    const std::vector<std::pair<int, double>>& col = cols_[static_cast<size_t>(step)];
    if (col.empty()) {
        return 1.0;
    }

    auto it = std::upper_bound(col.begin(), col.end(), batch_size,
                               [](int val, const std::pair<int, double>& p) {
                                   return val < p.first;
                               });
    if (it == col.end()) {
        return col.back().second;
    }
    if (it == col.begin()) {
        return col.front().second;
    }
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    if (lo.first == batch_size) {
        return lo.second;
    }
    const double f = (batch_size - lo.first) / static_cast<double>(hi.first - lo.first);
    const double res = lo.second + f * (hi.second - lo.second);
    assert(res > 0.0);
    return res;
}

int TaskTimeTable::max_batch_for(Step step) const {
    const std::vector<std::pair<int, double>>& col = cols_[static_cast<size_t>(step)];
    if (col.empty()) return 0;
    return col.back().first;
}

Step step_of(StepKind k) {
    switch (k) {
        case StepKind::P_PRE:  return Step::PREFILL_PRE;
        case StepKind::P_PROC: return Step::PREFILL_PROC;
        case StepKind::P_POST: return Step::PREFILL_POST;
        case StepKind::D_PRE:  return Step::DECODE_PRE;
        case StepKind::D_PROC: return Step::DECODE_PROC;
        case StepKind::D_POST: return Step::DECODE_POST;
    }
    assert(false);
    return Step::PREFILL_PRE;
}

StepKind kind_of(Step s) {
    switch (s) {
        case Step::PREFILL_PRE:  return StepKind::P_PRE;
        case Step::PREFILL_PROC: return StepKind::P_PROC;
        case Step::PREFILL_POST: return StepKind::P_POST;
        case Step::DECODE_PRE:   return StepKind::D_PRE;
        case Step::DECODE_PROC:  return StepKind::D_PROC;
        case Step::DECODE_POST:  return StepKind::D_POST;
    }
    assert(false);
    return StepKind::P_PRE;
}

}