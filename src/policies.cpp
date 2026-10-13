// =============================================================================
// policies.cpp — Layer 5 implementation.
//
// PrefillLoadBalanceCloud::choose — Tier-1 straggler isolation (very large
// Lin gets the least-active cloud) over an EFT model that prices the full
// prefill queue plus the decode backlog with the T9 alignment floor
// (D-124/D-147); the climb rule caps the assumed batch size at the table's
// per-member optimum.
//
// MarginalBatch::precompute — the batch-cap brain: grow m while the marginal
// duration pays for itself against the per-member average (bulk-service
// law), take the breakpoint argmin over listed rows (non-monotone tables),
// and treat an exhausted climb as unbounded (flat/linear regions are free).
// group_size — runtime caps: the parallel-period SLO2 feasibility scan
// (binary-searched; low-Lout regime keys the budget to SLO1), then the exact
// V_tp/V_tpot/V_tdr crossing test that clamps only when the SLO is genuinely
// at risk.
//
// AdaptiveChunk::next_piece — Sarathi-style prefill chunking: split only
// when the local is busy and the target cloud is idle (stall-free interleave),
// with the piece count bounded by setup affordability (D-162 kept the
// computed k* grid in the research line).
//
// SloAwareDelay::hold_dpre — the rent-or-wait decision with exact marginal
// delay physics (Theorem 6): hold only while the throughput value of a
// bigger batch outweighs the priced TPOT/TDR loss of waiting; vetoed under
// TDR pressure, multi-token requests, or a single imminent P_POST on a
// dist_base==0 cliff. NoDelay is the pure-throughput arm.
// =============================================================================
#include "policies.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace icpc2026 {
static double tg(const State& s, StepKind k, int m) {
    return s.table().get(step_of(k), m);
}
int PrefillLoadBalanceCloud::choose(const State& s, const Request& r, double t) const {
    // STRAGGLER BALANCER (P_PRE only): P_PROC time depends on Lin, so a large
    // Lin request occupies a cloud for a long time and, if placed on a busy
    // cloud, inflates the TDR of co-located requests. Three tiers:
    //   Tier 1 (straggler): very large Lin -> isolate on least-loaded cloud.
    //   Tier 2/3 (normal): EFT balancing with a Lin-as-Lout proxy.
    // (No D_PRE straggler avoidance in pick_local — the marginal batching logic
    //  groups them for iteration 1 naturally; D-147.)
    if (r.lin >= 256 && s.num_clouds() > 1) {
        int min_active = 1 << 30;
        int isolated = 0;
        for (int c = 0; c < s.num_clouds(); ++c) {
            const int act = s.active_on_cloud(c);
            if (act < min_active) {
                min_active = act;
                isolated = c;
            }
        }
        return isolated;
    }
    // THEOREM 12: EFT (earliest-finish-time) cloud balancing — Graham's list
    // scheduling: the new request's P_PROC runs after the cloud's queued
    // prefills (finish = busy_until + queue + own P_PROC) and its decode
    // chain starts after the existing decode backlog (T9 alignment model,
    // scaled by the estimated Lout). Pick the argmin of the exact finish.
    int best = 0;
    double best_eft = 1e300;
    const double lout_avg = s.ewma_lout();
    // Use Lin as proxy for Lout (correlated in most workloads).
    // For the incoming request, use max(Lin, ewma) to be conservative.
    const double this_lout = std::max(static_cast<double>(r.lin), lout_avg);
    for (int c = 0; c < s.num_clouds(); ++c) {
        double t_pre = s.cloud(c).busy ? s.cloud(c).busy_until : 0.0;
        if (t_pre < t) t_pre = t;
        for (int rid = s.rl_head(Phase::READY_P_PROC, c); rid != -1;
             rid = s.request(rid).next_ready) {
            const Request& q = s.request(rid);
            t_pre += s.table().get(step_of(StepKind::P_PROC), q.lin);
        }
        t_pre += s.table().get(step_of(StepKind::P_PROC), r.lin);
        const int active = s.active_on_cloud(c) + 1;
        int m_cap = 1;
        for (int m = 2; m <= active; ++m) {
            const double dur_m = tg(s, StepKind::D_PROC, m);
            if (tg(s, StepKind::D_PROC, m + 1) - dur_m > dur_m / m) break;
            m_cap = m;
        }
        const int m_actual = m_cap < active ? m_cap : active;
        const int batches = (active + m_actual - 1) / m_actual;
        // T9 floor (load-bearing): the alignment model never understates the
        // no-batching bound — without it hope2 59.108 -> 54.672 (spread
        // pressure erased, D-124 evidence re-reproduced).
        const double t_decode = this_lout * std::max(batches * tg(s, StepKind::D_PROC, m_actual),
                                                 tg(s, StepKind::D_PROC, 1) * active);
        const double eft = t_pre + t_decode;
        if (eft < best_eft - 1e-12) {
            best_eft = eft;
            best = c;
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
#ifdef LOCAL_DIAG
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
#endif
std::pair<int, int> OnePieceChunk::next_piece(const State& s, const Request& r, double t) const {
    (void)s; (void)t;
    assert(r.next_ls < s.cfg().num_layers);
    return {r.next_ls, s.cfg().num_layers};
}
std::pair<int, int> AdaptiveChunk::next_piece(const State& s, const Request& r, double t) const {
    (void)t;
    const int L = s.cfg().num_layers;
    if (r.next_ls > 0) return {r.next_ls, L};
    if (L <= 1) return {0, 1};

    const double full_dur = tg(s, StepKind::P_PROC, r.lin);
    // THEOREM 11 (REVERTED): the starved-gate variant lost t1_chunk
    // 0.359 -> 0.343. Chunking only pays when the local pipeline is full:
    // with the local idle, a single chunk minimizes serial S overhead.
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
    const double S = s.cfg().S;

    // Local average task time
    double local_avg = 2.0 * S + tg(s, StepKind::D_PRE, 1) + tg(s, StepKind::D_POST, 1);
    if (local_avg < 1e-9) local_avg = 1e-9;
    int k_opt = std::max(1, static_cast<int>(std::ceil(full_dur / local_avg)));
    int k_max_s = (S > 1e-9) ? std::max(1, static_cast<int>(std::floor(full_dur / S))) : L;

    int k = std::min({k_opt, k_max_s, L, 2});

    int chunk_size = std::max(1, L / k);
    int le = r.next_ls + chunk_size;
    if (le > L || L - le < chunk_size) le = L;
    return {r.next_ls, le};
}
int SizeOneBatch::group_size(StepKind step, int n_ready, const State& s, double t) const {
    (void)step; (void)n_ready; (void)s; (void)t;
    return 1;
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
    (void)t; (void)alpha_;  // alpha_ unused: marginal values drive the cap
    const Step col = step_of(step);
    const int mmax = max_m_[static_cast<int>(col)];
    if (mmax < 1) return 1;
    // Twist 3: pure-throughput cap — the SLO protection now lives entirely
    // in the max_m_slo scan + the exact crossing tradeoff below (Twist 1).
    int cap = mmax;
    // Cap batch size to prevent TPOT violations if waiting component is live.
    // cost(m) is monotone nondecreasing in m, so the largest m with
    // cost(m) <= SLO2 is found by scanning listed rows, then binary search
    // above the largest listed batch (identical result to a full scan, but
    // avoids O(cap) with cap up to 1<<20, which TLE'd crash_sweep).
    if (s.cfg().w_c > 0.0 && s.cfg().SLO2 > 0.0) {
        auto cost = [&](int m) -> double {
            double t_local = 2.0 * s.cfg().S
                + s.table().get(step_of(StepKind::D_PRE), m)
                + s.table().get(step_of(StepKind::D_POST), m);
            double t_cloud = s.cfg().S
                + s.table().get(step_of(StepKind::D_PROC), std::max(1, m / s.num_clouds()));
            double t_transfer = transfer_time(s.cfg(), static_cast<double>(m));
            return std::max({t_local, t_cloud, t_transfer});
        };
        int max_m_slo = 1;
        // LOW-LOUT REGIME: If Lout is small (e.g., 1), TPOT is structurally 0.
        // SLO2 is irrelevant. The batch period directly impacts TDR.
        const bool low_lout_regime = (s.ewma_lout() < 2.5);
        const double slo_thresh = low_lout_regime ? s.cfg().SLO1 * 0.8 : s.cfg().SLO2;
        const int M = std::max({s.table().max_batch_for(step_of(StepKind::D_PRE)),
                                s.table().max_batch_for(step_of(StepKind::D_PROC)),
                                s.table().max_batch_for(step_of(StepKind::D_POST))});
        const int scan_hi = std::min(cap, M);
        for (int m = 2; m <= scan_hi; ++m) {
            if (cost(m) <= slo_thresh + 1e-9) max_m_slo = m;
        }
        if (cap > M) {
            int lo = M + 1, hi = cap;
            while (lo <= hi) {
                const int mid = (lo + hi) / 2;
                if (cost(mid) <= slo_thresh + 1e-9) {
                    if (mid > max_m_slo) max_m_slo = mid;
                    lo = mid + 1;
                } else hi = mid - 1;
            }
        }
        // Twist 1: the real crossing decision — cap > max_m_slo (SLO clamp
        // would apply) is the only case with an actual SLO2/throughput
        // tradeoff. Testing cap < max_m_slo instead would be SLO-safe
        // (delta_tpot=0) and vacuous: the clamp above already guarantees
        // cap <= max_m_slo, so the crossing never broke SLO2.
        if (cap >= max_m_slo) {
            const Config& cg = s.cfg();
            const auto& oc = s.oracle();

            double V_tp = oc.marginal_tp_v(cg.w_tp, cg.tp_base, cg.tp_UB);
            double V_tpot = (cg.K > 1)
                ? oc.marginal_tpot_v(cg.w_c, cg.dist_base, cg.SLO1, cg.SLO2)
                : cg.w_c / (cg.SLO2 * std::max(cg.dist_base, 1.0));

            if (V_tpot <= 1e-9 && cg.dist_base <= 0.0 && oc.excess_tpot(cg.SLO2) > 0.0)
                V_tpot = 1000.0 * cg.w_c;

            if (V_tpot > 1e-9) {
                double tpot_cap = cost(cap);
                double tpot_slo = cost(max_m_slo);
                double delta_tpot = std::max(0.0, tpot_cap - cg.SLO2) - std::max(0.0, tpot_slo - cg.SLO2);
                double delta_tp = (cap / tpot_cap) - (max_m_slo / tpot_slo);

                double tdr_loss = 0.0;
                const int pipe = s.count_phase(Phase::WAIT_INPUT_DOWN) + s.count_phase(Phase::P_PROC_RUNNING);
                if (pipe > 0 && oc.count_tdr > 0) {
                    const double dt_local =
                         s.table().get(step_of(StepKind::D_PRE),  cap)
                        + s.table().get(step_of(StepKind::D_POST), cap)
                        - s.table().get(step_of(StepKind::D_PRE),  max_m_slo)
                        - s.table().get(step_of(StepKind::D_POST), max_m_slo);
                    if (dt_local > 0.0) {
                        const double V_tdr = oc.marginal_tdr_v(cg.w_c, cg.dist_base, cg.SLO1, cg.SLO2);
                        tdr_loss = V_tdr * dt_local * static_cast<double>(pipe) / oc.count_tdr;
                    }
                }

                if (delta_tp * V_tp <= delta_tpot * V_tpot + tdr_loss) {
                    cap = max_m_slo;
                }
            }
        }
    }
    return n_ready < cap ? n_ready : cap;
}
bool NoDelay::hold_dpre(const State& s, double t) const {
    (void)s; (void)t;
    return false;
}
bool SloAwareDelay::hold_dpre(const State& s, double t) const {
    (void)alpha_;
    // Theorem 2: marginal-value delay threshold — only hold D_PRE when the
    // throughput gain of amortizing S strictly outweighs the waiting penalty.
    if (s.multi_token_dpre_count() > 0) return false;

    // TDR CRITICAL OVERRIDE: If TDR is already bleeding, do not hold.
    const bool tdr_critical = (s.cfg().dist_base > 0.0 && s.cfg().w_c > 0.0 &&
        s.oracle().excess_tdr(s.cfg().SLO1) > 0.15 * s.cfg().dist_base);
    if (tdr_critical) return false;

    // BATCH READY OVERRIDE: If we already have 2+ requests, we have a batch.
    const int ready_now = s.count_phase(Phase::READY_D_PRE);
    if (ready_now >= 2) return false;

    if (s.cfg().dist_base <= 0.0) {
        // For cliff tests (binary waiting component), D_PRE must never
        // delay an imminent P_POST — any TDR violation zeroes norm_c.
        // Hold D_PRE only when we have 0 ready and exactly 1 imminent P_POST
        // (waiting to form a pair); otherwise let marginal logic decide.
        const int imminent = s.count_phase(Phase::WAIT_INPUT_DOWN)
                           + s.count_phase(Phase::P_PROC_RUNNING);
        if (imminent > 0 && s.cfg().w_c > 0.0) {
            if (ready_now == 0 && imminent == 1) return true;
            return false;
        }
        return false;
    }
    double next = 1e300;
    if (s.local().busy) next = std::min(next, s.local().busy_until);
    for (int c = 0; c < s.num_clouds(); ++c) {
        if (s.cloud(c).busy) next = std::min(next, s.cloud(c).busy_until);
    }
    if (next > 1e290) return false;
    const int pipe = s.count_phase(Phase::WAIT_INPUT_DOWN) + s.count_phase(Phase::P_PROC_RUNNING);
    if (pipe == 0) return false;
    const int k = s.count_phase(Phase::READY_D_PRE);
    if (k == 0) return false;

    // THEOREM 6: Exact marginal delay physics (sequential pipeline)
    const Config& cg = s.cfg();
    const auto& tb = s.table();
    auto true_period = [&](int m) -> double {
        double t_local = 2.0 * cg.S + tb.get(step_of(StepKind::D_PRE), m)
                      + tb.get(step_of(StepKind::D_POST), m);
        double t_cloud = cg.S + tb.get(step_of(StepKind::D_PROC),
                          std::max(1, m / s.num_clouds()));
        double t_transfer = transfer_time(cg, static_cast<double>(m));
        return std::max({t_local, t_cloud, t_transfer});
    };

    double P_k  = true_period(k);
    double P_1  = true_period(1);
    double P_k1 = true_period(k + 1);
    double delta_P = P_k + P_1 - P_k1;
    if (delta_P <= 0.0) return false;

    double delta_future = P_k1 - P_k;
    double V_tp = s.oracle().marginal_tp_v(cg.w_tp, cg.tp_base, cg.tp_UB);
    double V_tpot = s.oracle().marginal_tpot_v(cg.w_c, cg.dist_base, cg.SLO1, cg.SLO2);
    double V_tdr = s.oracle().marginal_tdr_v(cg.w_c, cg.dist_base, cg.SLO1, cg.SLO2);

    if (V_tpot <= 1e-9 && V_tdr <= 1e-9) return true;

    const double tp = s.oracle().projected_tp();
    const double makespan = s.oracle().last_finish - s.oracle().earliest_arrival;
    if (tp <= 0.0 || makespan <= 1e-9) return false;

    double wait_time = next - t;
    double gain = V_tp * tp * delta_P / makespan;
    double tdr_loss = 0.0;
    if (V_tdr > 1e-9 && s.oracle().count_tdr > 0) {
        tdr_loss = V_tdr * wait_time * static_cast<double>(pipe) / s.oracle().count_tdr;
    }

    double loss = V_tp * tp * wait_time / makespan + V_tpot * delta_future + tdr_loss;
    return gain > loss;
}
}  // namespace icpc2026
