// Layer 5: optimization policies.
#include "policies.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace icpc2026 {
int LeastLoadedCloud::choose(const State& s, const Request& r, double t) const {
    (void)r;
    int best = 0;
    double best_until = std::numeric_limits<double>::max();
    for (int c = 0; c < s.num_clouds(); ++c) {
        const double until = s.cloud(c).busy ? s.cloud(c).busy_until : 0.0;
        if (until < best_until - 1e-12) {
            best_until = until;
            best = c;
        }
    }
    (void)t;
    return best;
}
int QueueAwareCloud::choose(const State& s, const Request& r, double t) const {
    (void)r; (void)t;
    int best = 0;
    double best_ready = std::numeric_limits<double>::max();
    for (int c = 0; c < s.num_clouds(); ++c) {
        double ready = s.cloud(c).busy ? s.cloud(c).busy_until : 0.0;
        const int pp = s.rl_size(Phase::READY_P_PROC, c);
        if (pp > 0) {
            for (int rid = s.rl_head(Phase::READY_P_PROC, c); rid != -1;
            rid = s.request(rid).next_ready) {
                const Request& q = s.request(rid);
                const double frac = static_cast<double>(s.cfg().num_layers - q.next_ls) /
                static_cast<double>(s.cfg().num_layers);
                ready += frac * s.table().get(step_of(StepKind::P_PROC), q.lin);
            }
        }
        const int dp = s.rl_size(Phase::READY_D_PROC, c);
        if (dp > 0) {
            ready += s.table().get(step_of(StepKind::D_PROC), dp);
        }
        if (ready < best_ready - 1e-12) {
            best_ready = ready;
            best = c;
        }
    }
    return best;
}
int RoundRobinCloud::choose(const State& s, const Request& r, double t) const {
    (void)s; (void)r; (void)t;
    const int c = next_;
    next_ = (next_ + 1) % k_;
    return c;
}
int DecodeLoadBalanceCloud::choose(const State& s, const Request& r, double t) const {
    (void)r; (void)t;
    int best = 0;
    double best_est = s.decode_est(0);
    double best_until = s.cloud(0).busy ? s.cloud(0).busy_until : 0.0;
    for (int c = 1; c < s.num_clouds(); ++c) {
        const double e = s.decode_est(c);
        const double u = s.cloud(c).busy ? s.cloud(c).busy_until : 0.0;
        if (e < best_est - 1e-12 ||
        (std::fabs(e - best_est) < 1e-12 && u < best_until - 1e-12)) {
            best = c;
            best_est = e;
            best_until = u;
        }
    }
    return best;
}
#ifndef PPA_ALPHA
#define PPA_ALPHA 1.0
#endif
#ifndef PPA_BETA
#define PPA_BETA 1.0
#endif
int PpreCloudAssign::choose(const State& s, const Request& r, double t) const {
    (void)r; (void)t;
    const double dproc = s.table().get(step_of(StepKind::D_PROC), 1);
    const double up_tail = s.up().tail;
    int best = 0;
    double best_score = std::numeric_limits<double>::max();
    for (int c = 0; c < s.num_clouds(); ++c) {
        const double compute_free = s.cloud(c).busy ? s.cloud(c).busy_until : 0.0;
        const double effective_ready = std::max(compute_free, up_tail);
        const double decode_load = s.decode_est(c) * dproc;
        const double affinity = static_cast<double>(s.active_on_cloud(c));
        const double score = effective_ready
        + PPA_ALPHA * decode_load
        - PPA_BETA * affinity;
        if (score < best_score - 1e-12) {
            best_score = score;
            best = c;
        }
    }
    return best;
}
std::pair<int, int> OnePieceChunk::next_piece(const State& s, const Request& r, double t) const {
    (void)s; (void)t;
    assert(r.next_ls < s.cfg().num_layers);
    return {r.next_ls, s.cfg().num_layers};
}
std::pair<int, int> AdaptiveChunk::next_piece(const State& s, const Request& r, double t) const {
    const int L = s.cfg().num_layers;
    assert(r.next_ls < L);
    if (r.next_ls > 0) {
        return {r.next_ls, L};
    }
    if (L == 1) {
        return {0, 1};
    }
    const double full_dur = s.table().get(step_of(StepKind::P_PROC), r.lin);
    const int mid = L / 2;
    const double chunk_time = full_dur * static_cast<double>(mid) / static_cast<double>(L);
    if (s.cfg().S > chunk_time * 0.5) {
        return {0, L};
    }
    if (s.local().busy_until <= t + EPS) {
        return {0, L};
    }
    if (s.up().head_finish() > t + EPS) {
        return {0, L};
    }
    if (r.cloud < 0 || s.rl_size(Phase::READY_P_PROC, r.cloud) > 0 ||
    s.rl_size(Phase::READY_D_PROC, r.cloud) > 0) {
        return {0, L};
    }
    return {0, mid};
}
int SizeOneBatch::group_size(StepKind step, int n_ready, const State& s, double t) const {
    (void)step; (void)n_ready; (void)s; (void)t;
    return 1;
}
int GreedyAllBatch::group_size(StepKind step, int n_ready, const State& s, double t) const {
    (void)step; (void)s; (void)t;
    assert(n_ready >= 1);
    return n_ready;
}
void MarginalBatch::precompute(const Config& cfg, const TaskTimeTable& tbl) {
    for (int col = static_cast<int>(Step::DECODE_PRE);
    col <= static_cast<int>(Step::DECODE_POST); ++col) {
        const Step scol = static_cast<Step>(col);
        const int max_table_m = tbl.max_batch_for(scol);
        if (max_table_m <= 0) {
            max_m_[col] = 1 << 20;
            continue;
        }
        const bool cross = (scol == Step::DECODE_PRE) && (cfg.K > 1);
        const double lat = cross ? cfg.latency_ms : 0.0;
        const double S = cfg.S;
        int m = 1;
        while (m < max_table_m) {
            const double dur_m = tbl.get(scol, m);
            const double dur_m1 = tbl.get(scol, m + 1);
            const double avg = (S + 2.0 * lat + dur_m) / static_cast<double>(m);
            const double marg = dur_m1 - dur_m;
            if (marg <= avg) ++m; else break;
        }
        int argmin_m = 1;
        double best_avg = std::numeric_limits<double>::max();
        for (const auto& bd : tbl.listed(scol)) {
            const double a = (S + 2.0 * lat + bd.second) / static_cast<double>(bd.first);
            if (a < best_avg - 1e-12) { best_avg = a; argmin_m = bd.first; }
        }
        int cap = m > argmin_m ? m : argmin_m;
        if (cap >= max_table_m) cap = 1 << 20;
        max_m_[col] = cap;
    }
}
int MarginalBatch::group_size(StepKind step, int n_ready, const State& s, double t) const {
    (void)t;
    const Step col = step_of(step);
    const int mmax = max_m_[static_cast<int>(col)];
    if (mmax < 1) return 1;
    const int dmb = s.dynamic_max_batch();
    int cap;
    if (dmb > 0) {
        cap = dmb < mmax ? dmb : mmax;
    } else {
        cap = 1 + static_cast<int>(alpha_ * (mmax - 1));
    }
    return n_ready < cap ? n_ready : cap;
}
bool NoDelay::hold_dpre(const State& s, double t) const {
    (void)s; (void)t;
    return false;
}
namespace {
    double group_cost(const State& s, int m) {
        const double S = s.cfg().S;
        const double d = s.table().get(step_of(StepKind::D_PRE), m) +
        s.table().get(step_of(StepKind::D_PROC), m) +
        s.table().get(step_of(StepKind::D_POST), m);
        const double tr = transfer_time(s.cfg(), static_cast<double>(m));
        return 3.0 * S + d + 2.0 * tr;
    }
}
bool SloAwareDelay::hold_dpre(const State& s, double t) const {
    (void)t;
    if (alpha_ >= 0.8) return false;
    if (s.cfg().w_c == 0.0) { return false; }
    if (s.multi_token_dpre_count() > 0) return false;
    double next = 1e300;
    if (s.local().busy) next = std::min(next, s.local().busy_until);
    for (int c = 0; c < s.num_clouds(); ++c) {
        if (s.cloud(c).busy) next = std::min(next, s.cloud(c).busy_until);
    }
    if (next > 1e290) return false;
    const int pipe = s.count_phase(Phase::WAIT_INPUT_DOWN) + s.count_phase(Phase::P_PROC_RUNNING);
    if (pipe == 0) return false;
    const double k = s.count_phase(Phase::READY_D_PRE);
    const double dk = group_cost(s, k) + group_cost(s, 1) - group_cost(s, k + 1);
    if (dk <= 0.0) return false;
    return true;
}
}  // namespace icpc2026
