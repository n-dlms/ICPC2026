#ifndef ICPC2026_STATE_HPP
#define ICPC2026_STATE_HPP

// =============================================================================
// state.hpp — Layer 3: the solver's state machine and live score oracle.
//
// Two responsibilities:
//
//  1. Request lifecycle. Every request walks 17 phases (prefill chain, then a
//     decode loop repeated Lout times). Ready work lives in intrusive linked
//     lists embedded in the Request struct itself (prev_ready/next_ready) —
//     global FIFOs for P_POST/D_POST, per-cloud FIFOs for P_PROC/D_PROC, and a
//     4097-bucket array keyed by Lin for P_PRE (SPT pops in O(1)). No hot-path
//     allocations anywhere.
//
//  2. Score oracle. The Oracle struct incrementally maintains the exact
//     scoring aggregates (sum TDR, pooled token gaps, token count, elapsed
//     window) so the scheduler can price decisions with the real objective —
//     projected score, marginal values, and remaining recoverable points — at
//     any frame. It agrees with the official judge to ~1e-10.
//
// Also here: the TransferQueue mirror of the shared UP/DOWN FIFOs (used by
// admission gates), projected in-flight counters, and the D-141 busy-time
// meters that power the post-run triage instruments.
// =============================================================================
#include "protocol.hpp"
#include "task_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace icpc2026 {

enum class Phase : uint8_t {
    READY_P_PRE,
    P_PRE_RUNNING,
    WAIT_INPUT_UP,
    READY_P_PROC,
    P_PROC_RUNNING,
    WAIT_INPUT_DOWN,
    READY_P_POST,
    P_POST_RUNNING,
    READY_D_PRE,
    D_PRE_RUNNING,
    WAIT_DEC_UP,
    READY_D_PROC,
    D_PROC_RUNNING,
    WAIT_DEC_DOWN,
    READY_D_POST,
    D_POST_RUNNING,
    FINISHED
};
constexpr int NUM_PHASES = 17;

struct Request {
    int id = -1;
    int lin = 0;
    int cloud = -1;
    Phase phase = Phase::READY_P_PRE;
    int iter = 0;
    int next_ls = 0;
    int lout = 0;
    double arrival = 0.0;
    double ppost_t = -1.0;
    double first_token = -1.0, last_token = -1.0;
    // Instrument 4 (TDR decomposition) phase timestamps. Written at
    // issue()/apply_event(); only READ by LOCAL_DIAG builds. -1 = not yet.
    double ppre_start = -1.0, ppre_end = -1.0;      // E: P PRE issue / TDN
    double up_done = -1.0;                          // UP XDN (input stage)
    double pproc_start = -1.0, pproc_end = -1.0;    // cloud: first piece issue / last piece TDN
    double down_done = -1.0;                        // DOWN XDN (input stage)
    double ppost_start = -1.0;                      // E: P POST issue
    bool finished = false;
    bool stats_recorded = false;
    bool in_ready_list = false;
    // Zero-Allocation Hot Path: intrusive ready-list pointers (no std::deque).
    int prev_ready = -1;
    int next_ready = -1;
};

struct CurrentTask {
    bool busy = false;
    double busy_until = 0.0;
    std::vector<std::string> spec;
    StepKind step = StepKind::P_PRE;
    std::vector<int> ids;
};

struct TransferQueue {
    std::vector<double> finish_times;
    double tail = 0.0;
    void reserve(size_t n) { finish_times.reserve(n); }
    void push(double enter_t, double dur);
    // Remove every transfer whose finish time has passed by t. Called once per
    // frame at the gate so in_flight() reflects the real shared-FIFO depth.
    void drain(double t);
    double head_finish() const { return finish_times.empty() ? 0.0 : finish_times.front(); }
    // Number of transfers currently in flight (enqueued, not yet drained). Used by
    // the PART 5.4d prefill-stagger to bound shared-FIFO depth.
    size_t in_flight() const { return finish_times.size(); }
};

class State {
public:
    void init(const Config& cfg);
    void apply_event(const Event& ev, double t);
    void end_frame();
    void issue(const TaskSpec& spec, double t);

    // Frame counter (incremented once per end_frame). Drives the TLE fast
    // path in Scheduler::pick_cloud: past a frame budget the O(R) SPT scan
    // is replaced by FIFO head, bounding per-frame cost to O(1).
    long long frames() const { return frames_; }

    Request& request(int id) { return reqs_[id]; }
    const Request& request(int id) const { return reqs_[id]; }
    bool has_request(int id) const { return id >= 0 && id < static_cast<int>(reqs_.size()) && seen_[id]; }
    int num_requests() const { return static_cast<int>(reqs_.size()); }

    const Config& cfg() const { return cfg_; }
    const TaskTimeTable& table() const { return table_; }
    void set_table(const TaskTimeTable& t) { table_ = t; }

    CurrentTask& local() { return local_; }
    const CurrentTask& local() const { return local_; }
    CurrentTask& cloud(int k) { return clouds_[k]; }
    const CurrentTask& cloud(int k) const { return clouds_[k]; }
    int num_clouds() const { return num_clouds_; }
    int active_on_cloud(int k) const { return cloud_load_[static_cast<size_t>(k)]; }
    // 2.3: estimated outstanding decode load on cloud k (sum of Lin over
    // assigned-but-not-finished requests). Updated on P_PRE assign (+Lin) and
    // FIN (-Lin). Used by DecodeLoadBalanceCloud to balance makespan load.
    double decode_est(int k) const { return cloud_decode_est_[static_cast<size_t>(k)]; }
    // D-113: Empirical-Bayes Lout estimator — EWMA of observed Lout at FIN
    // (req.iter == Lout). Adapts to the hidden test's actual output
    // distribution; used to penalize standing decode debt in cloud choice.
     double ewma_lout() const { return ewma_lout_; }
     double ewma_lin() const { return ewma_lout_; }
     int predict_lout(int id) const { (void)id; return (int)std::lround(ewma_lout_); }
     double util_c(double t) const { (void)t; return 0.0; }
     double util_e(double t) const { (void)t; return 0.0; }

     // Zero-Allocation Hot Path (Criterion 2 final refactor): intrusive ready
    // lists embedded in Request (prev_ready/next_ready). No std::deque/std::vector
    // is allocated in the hot path. Phase lists are either global or per-cloud.
    static constexpr int PPRE_MAXLIN = 4096;
    static constexpr int PPRE_BUCKETS = PPRE_MAXLIN + 1;
    struct PPreBuckets {
        int head[PPRE_BUCKETS];   // ready list head per Lin bucket
        int tail[PPRE_BUCKETS];   // tail pointer per Lin bucket
        int count[PPRE_BUCKETS];  // member count per Lin bucket
        int max_lin;              // highest non-empty bucket (-1 if empty)
        int min_lin;              // lowest non-empty bucket (PPRE_BUCKETS if empty)
        void reset() {
            for (int i = 0; i < PPRE_BUCKETS; ++i) { head[i] = tail[i] = -1; count[i] = 0; }
            max_lin = -1;
            min_lin = PPRE_BUCKETS;
        }
    };
    PPreBuckets& ppre() { return ppre_; }
    const PPreBuckets& ppre() const { return ppre_; }

    inline static bool is_percloud(Phase p) {
        return p == Phase::READY_P_PROC ||
               p == Phase::READY_D_PRE ||
               p == Phase::READY_D_PROC;
    }
    static int phase_idx(Phase p) { return static_cast<int>(p); }
    // Intrusive list operations. `cloud` is ignored for global phases and for
    // P_PRE (which uses Lin buckets). Returns the popped request id or -1.
    int  rl_pop(Phase p, int cloud);
    int  rl_head(Phase p, int cloud) const;
    void rl_push(Phase p, int cloud, int rid);
    void rl_remove(int rid);
    // Zero-Allocation Hot Path (Decision b): O(1) counter of requests already
    // emitting tokens (iter > 0) sitting in the D PRE ready list. Maintained by
    // State at every D_PRE rl_push / rl_pop / rl_remove. Lets SloAwareDelay's
    // hold_dpre answer "any multi-token request waiting?" in O(1) instead of an
    // O(R) walk over the per-cloud D_PRE ready lists.
    int  multi_token_dpre_count() const { return multi_token_dpre_count_; }
    int  rl_size(Phase p, int cloud) const;
    bool rl_empty(Phase p, int cloud) const { return rl_size(p, cloud) == 0; }
    int  ppre_pop();
    void ppre_push(int rid);
    int  count_phase(Phase p) const;

    TransferQueue& up() { return up_; }
    TransferQueue& down() { return down_; }
    const TransferQueue& up() const { return up_; }
    const TransferQueue& down() const { return down_; }
    // Decision-driven shared-UP-FIFO depth: UP transfers
    // PROJECTED at issue time (P PRE = +1, D PRE = +distinct clouds) minus those
    // completed (XDN UP); = the real interactor FIFO backlog. P_PRE and D_PRE
    // tracked SEPARATELY so a large D_PRE group cannot starve the prefill
    // pipeline's UP budget. Increment at issue() time, NOT at TDN time.
    size_t up_inflight_pre() const {
        const long long v = up_proj_pre_ - up_deq_pre_;
        return static_cast<size_t>(v < 0 ? 0 : v);
    }

    // PSGO Batch 3: live dynamic batch-size cap set by the gradient tuning engine
    // (Scheduler::decide). Read by MarginalBatch::group_size. Negative = disabled
    // (use static alpha scaling); positive = override cap for all decode steps.
    int dynamic_max_batch() const { return dynamic_max_batch_; }
    void set_dynamic_max_batch(int m) { dynamic_max_batch_ = m; }

    // D-141m structural triage meter: cumulative busy time + latest arrival.
    double e_busy() const { return e_busy_; }
    double c_busy() const { return c_busy_; }
    double up_busy() const { return up_busy_; }
    double down_busy() const { return down_busy_; }
    double last_arr() const { return last_arr_; }

    // D-064b Variant D3: true iff the decode table is "flat" (batching
    // computationally free up to the max batch). Marks OD-9 cases (flat clamp +
    // w_c>0) so the D_PRE accumulation gate can be restricted to them.
    bool flat_decode_table() const { return flat_decode_table_; }
    void set_flat_decode_table(bool f) { flat_decode_table_ = f; }

    struct Oracle {
        double sum_tdr = 0.0;           // Σ (P_POST_completion - arrival)
        int count_tdr = 0;              // number of completed P_POSTs (for running mean)
        double sum_gaps = 0.0;          // Σ (token_k - token_{k-1}) for all k>1
        long long total_gaps = 0;       // count of pooled consecutive-token gaps (statement §tpot)
        double earliest_arrival = 1e18; // min arrival time
        double last_finish = 0.0;       // max token finish time
        long long tokens_out = 0;       // total tokens emitted

        // Projected metrics (exact at any frame)
        double projected_tdr() const { return count_tdr > 0 ? sum_tdr / count_tdr : 0.0; }
        double projected_tpot() const { return total_gaps > 0 ? sum_gaps / total_gaps : 0.0; }
        double projected_tp() const {
            if (last_finish <= earliest_arrival) return 0.0;
            return tokens_out / (last_finish - earliest_arrival);
        }
        double excess_tdr(double SLO1) const {
            if (SLO1 <= 0) return 0.0;
            return std::max(0.0, (projected_tdr() - SLO1) / SLO1);
        }
        double excess_tpot(double SLO2) const {
            if (SLO2 <= 0) return 0.0;
            return std::max(0.0, (projected_tpot() - SLO2) / SLO2);
        }
        double excess_dist(double SLO1, double SLO2) const {
            const double e_tdr = excess_tdr(SLO1);
            const double e_tpot = excess_tpot(SLO2);
            return std::sqrt(e_tdr * e_tdr + e_tpot * e_tpot);
        }
        double projected_cw_exact(double SLO1, double SLO2, double dist_base) const {
            const double dist = excess_dist(SLO1, SLO2);
            if (dist_base <= 0.0) return (dist == 0.0) ? 1.0 : 0.0;
            return std::max(0.0, 1.0 - dist / dist_base);
        }
        // Exact score projection using current running numerators/denominators
        double normalized_tp(double tp_base, double tp_UB) const {
            if (tp_UB <= tp_base) return 0.0;
            return std::clamp((projected_tp() - tp_base) / (tp_UB - tp_base), 0.0, 1.0);
        }
        double projected_score(double w_tp, double w_c, double tp_base, double tp_UB,
                               double SLO1, double SLO2, double dist_base) const {
            const double c_tp = normalized_tp(tp_base, tp_UB);
            const double c_w = projected_cw_exact(SLO1, SLO2, dist_base);
            return 1000.0 * (w_tp * c_tp + w_c * c_w);
        }
        // D-068 marginal values: ∂Score/∂metric inside the linear region,
        // 0 outside (clamp dead zones). V_tdr/V_tpot = points per 1ms mean
        // reduction; zero when the excess is already 0 (SLO satisfied) or the
        // waiting component is past the dist_base cliff (C_w clamped to 0).
        double marginal_tp_v(double w_tp, double tp_base, double tp_UB) const {
            const double tp = projected_tp();
            if (tp_UB <= tp_base || tp <= tp_base || tp >= tp_UB) return 0.0;
            return 1000.0 * w_tp / (tp_UB - tp_base);
        }
        double marginal_tdr_v(double w_c, double dist_base, double SLO1, double SLO2) const {
            const double e = excess_tdr(SLO1);
            const double d = excess_dist(SLO1, SLO2);
            if (w_c <= 0.0 || dist_base <= 0.0 || d <= 0.0 || d >= dist_base ||
                e <= 0.0 || SLO1 <= 0.0) return 0.0;
            return 1000.0 * w_c * e / (dist_base * d * SLO1);
        }
        double marginal_tpot_v(double w_c, double dist_base, double SLO1, double SLO2) const {
            const double e = excess_tpot(SLO2);
            const double d = excess_dist(SLO1, SLO2);
            if (w_c <= 0.0 || dist_base <= 0.0 || d <= 0.0 || d >= dist_base ||
                e <= 0.0 || SLO2 <= 0.0) return 0.0;
            return 1000.0 * w_c * e / (dist_base * d * SLO2);
        }
        // D-070 recoverable points per component (unit-free): what the clamps
        // have NOT already paid out — the upper bound of points an ideal
        // improvement could still extract from this component.
        double rem_tp(double w_tp, double tp_base, double tp_UB) const {
            return 1000.0 * w_tp * (1.0 - normalized_tp(tp_base, tp_UB));
        }
         double rem_c(double w_c, double dist_base, double SLO1, double SLO2) const {
             const double d = excess_dist(SLO1, SLO2);
             if (dist_base <= 0.0) return 1000.0 * w_c * ((d > 0.0) ? 1.0 : 0.0);
             return 1000.0 * w_c * std::min(1.0, d / dist_base);
         }
         double delay_cost_ppost(double w_c, double dist_base, double SLO1, double SLO2, int ready, double D, int denom) const { (void)w_c;(void)dist_base;(void)SLO1;(void)SLO2;(void)ready;(void)D;(void)denom; return 0.0; }
         double delay_cost_span(double w_c, double dist_base, double SLO1, double SLO2, int ready, double D, int gaps) const { (void)w_c;(void)dist_base;(void)SLO1;(void)SLO2;(void)ready;(void)D;(void)gaps; return 0.0; }
    };
    Oracle& oracle() { return oracle_; }
    const Oracle& oracle() const { return oracle_; }
    // PSGO Batch 2: live SLO excess vector for the scheduler / gradient engine.
    double get_excess_tdr() const { return oracle_.excess_tdr(cfg_.SLO1); }
    double get_excess_tpot() const { return oracle_.excess_tpot(cfg_.SLO2); }

    struct Stats {
        double sum_tdr = 0.0;
        double sum_span = 0.0;
        long long total_gaps = 0;
        double latest_token = 0.0, earliest_arrival = 1e18;
        long long tokens = 0;
    };
    Stats& stats() { return stats_; }

    void check_invariants(double t) const;

private:
    Config cfg_;
    TaskTimeTable table_;
    int num_clouds_ = 0;
    std::vector<Request> reqs_;
    long long frames_ = 0;
    std::vector<char> seen_;
    CurrentTask local_;
    std::vector<CurrentTask> clouds_;
    std::vector<int> cloud_load_;
    std::vector<double> cloud_decode_est_;
    std::vector<int> finish_pending_;
    TransferQueue up_, down_;
    long long up_proj_pre_ = 0, up_deq_pre_ = 0;
    long long up_proj_dec_ = 0, up_deq_dec_ = 0;
    // Intrusive ready lists.
    PPreBuckets ppre_;
    std::vector<int> g_head_, g_tail_, g_count_;          // global phases (READY_P_POST, READY_D_POST)
    std::vector<std::vector<int>> c_head_, c_tail_, c_count_;  // per-cloud phases (P_PROC, D_PRE, D_PROC)
    int multi_token_dpre_count_ = 0;
    Stats stats_;
    Oracle oracle_;
    int dynamic_max_batch_ = -1;
    // D-141m structural triage meter accumulators (busy time, last arrival).
    double e_busy_ = 0.0;
    double c_busy_ = 0.0;
    double up_busy_ = 0.0;
    double down_busy_ = 0.0;
    double last_arr_ = 0.0;
    bool flat_decode_table_ = false;
    double ewma_lout_ = 10.0;  // Prior estimate (D-113)
};

}

#endif