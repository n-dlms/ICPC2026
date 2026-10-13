#ifndef SB08_ENDGAME_CLIFF_HPP
#define SB08_ENDGAME_CLIFF_HPP
// SB08 — Endgame / Tail Policy + Binary-Cliff Risk Math
// Pure functions, header-style. Deterministic, no allocations, no unordered_*.
// Compiles -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror clean.
// Derivations, controller spec, proofs, size ledger:
//   research/techniques/SB08_endgame_cliff.md
// Validation vs direct score recomputation on >=1e5 adversarial trials:
//   research/techniques/SB08_check.py

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace sb08 {

// ---------------------------------------------------------------------------
// Config and score primitives (mirrors problem.md 425-495, SB04 calculus)
// ---------------------------------------------------------------------------
struct SCfg {
    double w_tp = 0.0;
    double w_c = 1.0;
    double tp_base = 0.0;
    double tp_UB = 1.0;
    double SLO1 = 1.0;
    double SLO2 = 1.0;
    double dist_base = 0.0; // 0 => binary cliff
};

// ---------------------------------------------------------------------------
// 1. TDR slack  (Task 3 — per-future-request budget to keep tdr <= SLO1)
// ---------------------------------------------------------------------------
// R = final request count (closed system: n_done + n_undone).
// sum_tdr_done = Σ_{done} (ppost_i - arr_i)  exact (P_POST times known).
// sum_est_tdr_undone = Σ_{undone} E[ ppost_i - arr_i | current state ]  (from SB05 / SB04 projection).
// n_undone = number of undone requests (pending + active).
// Returns: per-undone-request slack (ms) that remains before the FINAL pooled
// tdr mean would exceed SLO1.  Positive => still feasible in expectation,
// negative => expected violation already, zero => knife edge.
// Formula (Task 3 spec): s_tdr = (SLO1*R - sum_done - sum_est) / n_undone
// If n_undone == 0: returns +inf if feasible (or exactly at SLO), -inf if already violating.
// All inputs in ms; R, n_undone non-negative.
inline double tdr_slack(double SLO1, int R, double sum_tdr_done,
                        double sum_est_tdr_undone, int n_undone) {
    if (n_undone <= 0) {
        double total = sum_tdr_done + sum_est_tdr_undone;
        double budget = SLO1 * static_cast<double>(R);
        if (total <= budget) return std::numeric_limits<double>::infinity();
        return -std::numeric_limits<double>::infinity();
    }
    double budget = SLO1 * static_cast<double>(R);
    double remaining = budget - sum_tdr_done - sum_est_tdr_undone;
    return remaining / static_cast<double>(n_undone);
}

// ---------------------------------------------------------------------------
// 2. TPOT slack  (pooled-gap version, Task 3)
// ---------------------------------------------------------------------------
// sum_spans_done = Σ_{done tokens where G_done>0} (e_last - e_first)  exact.
// gaps_done = Σ_{done requests} (Lout_i - 1)  exact (0 if all Lout=1).
// sum_est_spans_undone = Σ_{undone} E[ span_i | state ]  (SB05 posterior: (Lhat-1)*P_gap_estimate or measured).
// est_gaps_undone = Σ_{undone} E[ Lout_i - 1 | state ] = Ghat_undone  (from SB05, >=0).
// total_gaps = gaps_done + est_gaps_undone  (projected final G).
// total_spans_proj = sum_done + sum_est.
// Returns: per-remaining-gap slack (ms per gap) before final pooled tpot mean would exceed SLO2.
// Closed form: s_tpot = (SLO2 * G_total - S_total_proj) / G_undone
// If G_undone == 0 (all Lout=1 or no gaps remain): returns +inf if tpot<=SLO2 else -inf (structure, not estimate).
// If G_total == 0 (no gaps at all in test): tpot ≡ 0 by definition (problem.md 457) => +inf.
inline double tpot_slack(double SLO2,
                         double sum_spans_done, long long gaps_done,
                         double sum_est_spans_undone, long long est_gaps_undone) {
    long long G_total = gaps_done + est_gaps_undone;
    if (G_total <= 0) {
        // No gaps ever => tpot is defined 0, always ≤ SLO2 (SLO2>0 guaranteed)
        return std::numeric_limits<double>::infinity();
    }
    if (est_gaps_undone <= 0) {
        // No future gaps, feasibility depends only on done means
        double tpot_done = gaps_done > 0 ? sum_spans_done / static_cast<double>(gaps_done) : 0.0;
        if (tpot_done <= SLO2) return std::numeric_limits<double>::infinity();
        return -std::numeric_limits<double>::infinity();
    }
    double total_spans = sum_spans_done + sum_est_spans_undone;
    double budget = SLO2 * static_cast<double>(G_total);
    double remaining = budget - total_spans;
    return remaining / static_cast<double>(est_gaps_undone);
}

// ---------------------------------------------------------------------------
// 3. Concentration margins — Hoeffding + Bennett (Task 3)
// ---------------------------------------------------------------------------
// We have independent (conditional on state) bounded contributions from each
// undone request.  Let X_i = contribution of request i to the pooled sum
// (tdr_i for tdr, span_i for tpot).  E[X_i]=mu_i, Var[X_i]=sigma_i^2,
// X_i ∈ [a_i, b_i] with range r_i = b_i - a_i.
// Hoeffding for S = Σ X_i :  P(S - E[S] ≥ t) ≤ exp( -2 t^2 / Σ r_i^2 )
// Bennett (sharper when variances small):  let V = Σ sigma_i^2, h = max r_i,
//   P(S - E[S] ≥ t) ≤ exp( - V/h^2 * [ (1+ h t / V) ln(1+ h t / V) - h t / V ] )
// Both are one-sided upper tails; we reserve that much slack.
//
// For tdr:  t = n_undone * s_tdr  is the total budget remaining. Violation
// occurs when Σ(X_i - mu_i) > t.  So we need t >= hoeffding_quantile(δ).
//
// Helpers return the *required reserve* t_δ such that P( violation by > t_δ ) ≤ δ.
// The caller does:  margin = slack_total - t_δ .  margin ≥ 0 ⇒ safe at 1-δ.
// If delta ≤0 or ≥1, or Σr²==0, returns 0.
inline double hoeffding_reserve_tdr(int n_undone, double range_per_req, double delta) {
    if (n_undone <= 0 || range_per_req <= 0.0 || delta <= 0.0 || delta >= 1.0) return 0.0;
    double sum_r2 = static_cast<double>(n_undone) * range_per_req * range_per_req;
    // t = sqrt( (sum_r2 /2) * ln(1/delta) )
    return std::sqrt(0.5 * sum_r2 * std::log(1.0 / delta));
}
inline double hoeffding_reserve_tpot(long long est_gaps_undone, double range_per_gap, double delta) {
    if (est_gaps_undone <= 0 || range_per_gap <= 0.0 || delta <= 0.0 || delta >= 1.0) return 0.0;
    double sum_r2 = static_cast<double>(est_gaps_undone) * range_per_gap * range_per_gap;
    return std::sqrt(0.5 * sum_r2 * std::log(1.0 / delta));
}
// Bennett reserve (one-sided). V = total variance, h = max per-request range.
// Returns t_δ solving bound == δ (via numeric inversion by binary search, monotone).
inline double bennett_reserve(double V, double h, double delta) {
    if (V <= 0.0 || h <= 0.0 || delta <= 0.0 || delta >= 1.0) return 0.0;
    // Upper search: Hoeffding t is valid upper bound for Bennett too, so 2*Hoeffding is safe cap
    double lo = 0.0;
    double hi = std::sqrt(0.5 * (h*h * 1000) * std::log(1.0/delta)) + 10*h; // generous
    // Increase hi until bound(hi) <= delta
    auto bennett_bound = [&](double t) -> double {
        double y = h * t / V;
        // g(y) = (1+y) ln(1+y) - y
        double g = (1.0 + y) * std::log(1.0 + y) - y;
        return std::exp(- (V / (h*h)) * g);
    };
    // Expand hi if needed
    for (int k=0;k<80;++k) {
        if (bennett_bound(hi) <= delta) break;
        hi *= 2.0;
        if (hi > 1e12) break;
    }
    for (int it=0; it<80; ++it) {
        double mid = 0.5*(lo+hi);
        if (bennett_bound(mid) <= delta) hi = mid;
        else lo = mid;
    }
    return hi;
}
// Convenience: per-undone-request / per-gap reserve
inline double hoeffding_reserve_per_req_tdr(int n_undone, double range_per_req, double delta) {
    if (n_undone <= 0) return 0.0;
    double t = hoeffding_reserve_tdr(n_undone, range_per_req, delta);
    return t / static_cast<double>(n_undone);
}
inline double hoeffding_reserve_per_gap_tpot(long long est_gaps_undone, double range_per_gap, double delta) {
    if (est_gaps_undone <=0) return 0.0;
    double t = hoeffding_reserve_tpot(est_gaps_undone, range_per_gap, delta);
    return t / static_cast<double>(est_gaps_undone);
}

// ---------------------------------------------------------------------------
// 4. Binary-cliff classifier (Task 3-4) — replaces threshold guessing
// ---------------------------------------------------------------------------
enum class CliffMode { NORMAL = 0, SAFE = 1 };

struct CliffInputs {
    double SLO1 = 30.0;
    double SLO2 = 15.0;
    int R = 0; // final count (or current estimate)
    double sum_tdr_done = 0.0;
    double sum_est_tdr_undone = 0.0;
    int n_undone = 0;
    double sum_spans_done = 0.0;
    long long gaps_done = 0;
    double sum_est_spans_undone = 0.0;
    long long est_gaps_undone = 0;
    // Concentration parameters
    double range_tdr_per_req_ms = 0.0;  // max - min possible tdr_i contribution (for Hoeffding)
    double range_span_per_gap_ms = 0.0; // max gap length
    double V_tdr = 0.0;   // total variance of undone tdr contributions (for Bennett, optional)
    double V_tpot = 0.0;  // total variance of undone gap contributions
    double h_tdr = 0.0;   // max deviation for Bennett (range)
    double h_tpot = 0.0;
    double delta = 0.01;  // target failure probability (1 - delta confidence)
    double eps_ms = 0.5;  // safety margin hysteresis (ms)
};

inline CliffMode classify_cliff(const CliffInputs& in) {
    // If not a cliff test (dist_base !=0) this function is not used; but we
    // implement the same logic with the same margins (conservative).
    double s_tdr = tdr_slack(in.SLO1, in.R, in.sum_tdr_done, in.sum_est_tdr_undone, in.n_undone);
    double s_tpot = tpot_slack(in.SLO2, in.sum_spans_done, in.gaps_done,
                               in.sum_est_spans_undone, in.est_gaps_undone);
    // If no future gaps/requests, slack is ±inf — handle infinities
    bool tdr_inf_pos = std::isinf(s_tdr) && s_tdr > 0;
    bool tpot_inf_pos = std::isinf(s_tpot) && s_tpot > 0;
    bool tdr_violation_expected = (s_tdr < 0.0);
    bool tpot_violation_expected = (s_tpot < 0.0);
    if (tdr_violation_expected || tpot_violation_expected) {
        // Expected violation already => SAFE (even more conservative)
        return CliffMode::SAFE;
    }
    // Compute required reserves
    double reserve_tdr = 0.0;
    double reserve_tpot = 0.0;
    // Prefer Bennett when variances supplied, else Hoeffding
    bool has_bennett_tdr = (in.V_tdr > 0.0 && in.h_tdr > 0.0);
    bool has_bennett_tpot = (in.V_tpot > 0.0 && in.h_tpot > 0.0);
    if (has_bennett_tdr) {
        double t = bennett_reserve(in.V_tdr, in.h_tdr, in.delta);
        reserve_tdr = in.n_undone > 0 ? t / static_cast<double>(in.n_undone) : 0.0;
    } else {
        reserve_tdr = hoeffding_reserve_per_req_tdr(in.n_undone, in.range_tdr_per_req_ms, in.delta);
    }
    if (has_bennett_tpot) {
        double t = bennett_reserve(in.V_tpot, in.h_tpot, in.delta);
        reserve_tpot = in.est_gaps_undone > 0 ? t / static_cast<double>(in.est_gaps_undone) : 0.0;
    } else {
        reserve_tpot = hoeffding_reserve_per_gap_tpot(in.est_gaps_undone, in.range_span_per_gap_ms, in.delta);
    }
    // If slack pools are infinite, treat reserve as zero for that channel
    double eff_slack_tdr = tdr_inf_pos ? std::numeric_limits<double>::infinity() : s_tdr;
    double eff_slack_tpot = tpot_inf_pos ? std::numeric_limits<double>::infinity() : s_tpot;
    double margin_tdr = tdr_inf_pos ? std::numeric_limits<double>::infinity() : (s_tdr - reserve_tdr);
    double margin_tpot = tpot_inf_pos ? std::numeric_limits<double>::infinity() : (s_tpot - reserve_tpot);
    (void)eff_slack_tdr; (void)eff_slack_tpot;
    double margin = std::min(margin_tdr, margin_tpot);
    if (margin < in.eps_ms) return CliffMode::SAFE;
    return CliffMode::NORMAL;
}

// Legacy threshold-free wrappers kept under the names the scheduler calls
inline double tdr_slack_legacy(double SLO1, int R, double sum_done, double sum_est, int n_undone) {
    return tdr_slack(SLO1, R, sum_done, sum_est, n_undone);
}
inline double tpot_slack_legacy(double SLO2, double sum_done, long long gaps_done,
                                double sum_est, long long est_gaps) {
    return tpot_slack(SLO2, sum_done, gaps_done, sum_est, est_gaps);
}

// ---------------------------------------------------------------------------
// 5. Tail de-fragmentation & drain — endgame merge rule (Tasks 1,2)
// ---------------------------------------------------------------------------
// At drain, ready pool is shrinking; singleton rounds waste 2S+dpre(1)+dpost(1) per token.
// Hold stragglers to coalesce into a final mega-round iff amortization gain exceeds
// the delay cost on the critical chain priced with exact SB04 coefficients.
// This replaces the ladder/heuristic `dpre_acc_skip` and `hold_dpre` logic.
//
// Gain (local time saved by merging m0 now + m1 later into one m0+m1 round):
//   G_loc = (S + dpre(m0)+dpost(m0)) + (S + dpre(m1)+dpost(m1)) - (S + dpre(m0+m1)+dpost(m0+m1))
//         = S + dpre(m0)+dpost(m0) + dpre(m1)+dpost(m1) - dpre(m0+m1)-dpost(m0+m1)
// Transfer headers (k*lat) cancel only if same-cloud count k unchanged; for cross-cloud
// merges they add k_merge*lat vs k0*lat+k1*lat. We include k in the full period check
// for SLO2 feasibility but G_loc as defined is the local-machine saving (the bottleneck
// that translates to E via V_E). When local is NOT bottleneck, G_loc does not become E.
//
// Cost of holding by wait_ms:
//   C = p_crit * V_E * wait_ms  +  n_span * V_S * wait_ms / Ghat
// where p_crit = P(held pool contains the global-last token) ≈ held_share of remaining work,
//       n_span = #held requests whose current D_POST is their LAST token (span grows),
//       V_E = 1000*w_tp*N/((tp_UB-tp_base)*E^2) in linear region else 0,
//       V_S = 1000*w_c*eps2/(dist_base*dist*SLO2*Ghat)  (per-span coefficient 1/Ghat already in V_S),
//             so the cost term is n_span * V_S * wait; SB04 defines V_S = V_S_mean/Ghat.
//             Our SB04 includes the 1/Ghat in V_S itself. We keep both forms explicit:
//             if caller passes V_S as per-span (already /Ghat), then cost = n_span*V_S*wait.
//             If caller passes V_S_mean, then cost = n_span*V_S_mean*wait/Ghat.
//             This function assumes V_S is per-span (V_S, not V_S_mean) to match SB04::V_S().
//             Adjust by Ghat division when caller has V_S_mean.
//
// Hold iff:  local_is_bottleneck ? V_E*G_loc > C  (+ optional SLO2 feasibility)
//            else  never hold for E (G_loc not on critical path) — span-only case degenerates.
//

struct EndgameMergeInputs {
    int m0 = 0;                // ready now
    int m1 = 0;                // expected arrivals within wait_ms (pipeline or straggler forecast)
    double wait_ms = 0.0;      // estimated wait to fill merged group
    double V_E = 0.0;          // SB04 V_E (0 if tp clamped)
    double p_crit = 0.0;       // share of global-last probability in held pool [0,1]
    double V_S = 0.0;          // per-span V_S (= V_S_mean / Ghat) — 0 if w_c==0 or Ghat==0
    int n_span = 0;            // #held requests whose span is live (last-token sensitivity)
    double Ghat = 1.0;         // total gaps (unused if V_S already per-span; kept for caller variance form)
    double S = 1.0;            // setup cost
    // table callbacks — caller supplies dpre/dpost lookups via function pointers or doubles
    // For pure-function testing we pass pre-evaluated durations:
    double dpre_m0 = 0.0, dpre_m1 = 0.0, dpre_merged = 0.0;
    double dpost_m0 = 0.0, dpost_m1 = 0.0, dpost_merged = 0.0;
    bool local_is_bottleneck = true; // true iff T_loc == P(m_merged) (local on critical path)
    double SLO2 = 1e18;        // for SOP feasibility (optional)
    double P_gap_merged = 0.0; // estimated member gap at merged size; 0 => skip check
};

inline bool endgame_merge(const EndgameMergeInputs& in) {
    if (in.m0 <= 0 || in.m1 <= 0) return false;
    if (in.wait_ms <= 0.0) return false;
    // Gain: local machine time saved by one merged round vs two singleton rounds
    double G_loc = in.S + in.dpre_m0 + in.dpost_m0 + in.dpre_m1 + in.dpost_m1
                 - in.dpre_merged - in.dpost_merged;
    if (G_loc <= 1e-12) return false;
    // SLO2 feasibility: merged member gap must fit within SLO2 if w_c live
    if (in.V_S > 1e-12 && in.P_gap_merged > in.SLO2 + 1e-9) return false;
    if (!in.local_is_bottleneck) {
        // Local saving does not translate to E; holding only stretches spans
        // Unless V_E==0 and we have span-parking gain (delaying e_first shrinks span) —
        // but endgame_merge concerns READY_D_PRE/D_POST with iter>0, so no parking gain.
        return false;
    }
    if (in.V_E <= 1e-12 && in.V_S <= 1e-12) return false; // no valued channel, never hold
    double gain_points = 0.0;
    if (in.V_E > 1e-12) gain_points = in.V_E * G_loc;
    double cost_points = 0.0;
    if (in.V_E > 1e-12) cost_points += in.p_crit * in.V_E * in.wait_ms;
    if (in.V_S > 1e-12 && in.n_span > 0) cost_points += static_cast<double>(in.n_span) * in.V_S * in.wait_ms;
    // Alternative caller form: V_S_mean passed as V_S and Ghat>1 => caller should pre-divide; we support both:
    // If Ghat is suspiciously large and V_S looks like V_S_mean (detect via magnitude heuristic), divide. We do NOT auto-adjust; caller contract is V_S per-span.
    return gain_points > cost_points;
}

// Convenience overload with the scheduler's call signature: bool endgame_merge(...)
inline bool endgame_merge_simple(int m0, int m1, double wait_ms,
                                 double V_E, double p_crit,
                                 double V_S, int n_span,
                                 double S,
                                 double dpre_m0, double dpre_m1, double dpre_merged,
                                 double dpost_m0, double dpost_m1, double dpost_merged) {
    EndgameMergeInputs in;
    in.m0 = m0; in.m1 = m1; in.wait_ms = wait_ms;
    in.V_E = V_E; in.p_crit = p_crit; in.V_S = V_S; in.n_span = n_span;
    in.S = S;
    in.dpre_m0 = dpre_m0; in.dpre_m1 = dpre_m1; in.dpre_merged = dpre_merged;
    in.dpost_m0 = dpost_m0; in.dpost_m1 = dpost_m1; in.dpost_merged = dpost_merged;
    in.local_is_bottleneck = true;
    return endgame_merge(in);
}

// ---------------------------------------------------------------------------
// 6. Drain ordering + maximal-group helpers (Task 1)
// ---------------------------------------------------------------------------
// Critical-path ordering: longest remaining path = solo Remaining Chain.
//
// Remaining chain for a request at state (iter, next_ls, phase):
//   chain = remaining prefill (if any) + UP tail + DOWN tail + cloud queue
//         + (Lhat - iter) * P_gap(pool)    for decode-phase
//         + (Lhat - iter) * 0  for finished (0)
// Order requests by descending chain (Lawler / Smith's rule on single machine
// with chains: WSPT on remaining work).  This helper computes the scalar.
//
// We provide a pure helper that *ranks* two requests given their chains;
// the scheduler uses this to sort the ready pool and to pick the star (critical)
// request whose chain = max (the E-bottleneck).  Graham bound: list scheduling
// by this order attains makespan ≤ (2 - 1/K) * OPT on identical parallel machines
// with chains (Graham 1966); with our FIFO and setup the bound degrades gracefully
// but the *policy* (critical first) remains optimal among list rules.
inline int drain_pick_star(const std::vector<double>& chains) {
    if (chains.empty()) return -1;
    int best = 0;
    double mx = chains[0];
    for (size_t i=1;i<chains.size();++i) if (chains[i] > mx) { mx = chains[i]; best = (int)i; }
    return best;
}
// Maximal-group rule: in closed-system drain, amortized per-token period
// C(m)=P(m)/m is non-increasing in m up to the throughput optimum m* (SB01 §1.4
// EOQ / SB06 optimal_split). So as pool shrinks, optimal m = min(pool, m_star)
// when local/throughput dominates, but capped by tpot feasibility m_tp(slo) when
// waiting channel live.  The helper caps to m_tp via passed P_gap check.
inline int drain_max_group(int pool, int m_star, int m_tp, bool w_c_live) {
    if (pool <= 0) return 0;
    int cap = m_star;
    if (w_c_live && m_tp > 0) cap = std::min(cap, m_tp);
    if (cap <= 0) cap = 1;
    return std::min(pool, cap);
}
// When to break batching discipline entirely (Task 1, "maximal-group rule + break"):
// If the per-token period at m=pool exceeds the per-token period of issuing
// singletons back-to-back due to chain dominance (T_chain > ceil(pool/m)*P_pipe
// for all m>1) or SLO infeasibility at any m>1, break to singleton discipline.
inline bool drain_should_break_to_singletons(int pool, double P_pool, double P_1,
                                             double SLO2, bool w_c_live) {
    if (pool <= 1) return false;
    // Compare per-token: pool batch vs singleton
    double per_pool = P_pool / static_cast<double>(pool);
    double per_one  = P_1 / 1.0;
    bool pool_worse = (per_pool > per_one + 1e-12);
    bool slo_break = (w_c_live && P_pool > SLO2 + 1e-9 && P_1 <= SLO2 + 1e-9);
    return pool_worse || slo_break;
}

} // namespace sb08

#endif // SB08_ENDGAME_CLIFF_HPP

#ifdef SB08_DRIVER
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
int main() {
    std::cout << std::fixed << std::setprecision(12);
    std::string line;
    // Modes:
    // T SLO1 R sum_done sum_est n_undone          -> tdr_slack
    // P SLO2 sum_spans gaps_done sum_est est_gaps -> tpot_slack
    // C <CliffInputs line>                        -> classify (see SB08_check.py)
    // M m0 m1 wait V_E p_crit V_S n_span S dpre0 dpre1 dprem dpost0 dpost1 dpostm bottleneck SLO2 Pgap -> endgame_merge 0/1
    // H n range delta                             -> hoeffding reserve
    // B V h delta                                 -> bennett reserve
    // Z                                           -> sizeof checks
    while (std::getline(std::cin, line)) {
        if(line.empty()) continue;
        std::istringstream ss(line);
        char mode=' ';
        ss>>mode;
        if(mode=='T'){
            double SLO1, sum_done, sum_est; int R, n_undone;
            ss>>SLO1>>R>>sum_done>>sum_est>>n_undone;
            double s = sb08::tdr_slack(SLO1,R,sum_done,sum_est,n_undone);
            std::cout<< std::setprecision(17) << s << "\n";
        } else if(mode=='P'){
            double SLO2, sum_spans, sum_est; long long gaps, est_gaps;
            ss>>SLO2>>sum_spans>>gaps>>sum_est>>est_gaps;
            double s = sb08::tpot_slack(SLO2,sum_spans,gaps,sum_est,est_gaps);
            std::cout<< std::setprecision(17) << s << "\n";
        } else if(mode=='H'){
            int n; double rng, delta;
            ss>>n>>rng>>delta;
            double r = sb08::hoeffding_reserve_tdr(n, rng, delta);
            std::cout<< std::setprecision(17) << r << "\n";
        } else if(mode=='B'){
            double V,h,delta;
            ss>>V>>h>>delta;
            double r = sb08::bennett_reserve(V,h,delta);
            std::cout<< std::setprecision(17) << r << "\n";
        } else if(mode=='M'){
            int m0,m1,n_span; double wait, V_E,p_crit,V_S,S,dpre0,dpre1,dprem,dpost0,dpost1,dpostm;
            int bottleneck; double SLO2,Pgap;
            ss>>m0>>m1>>wait>>V_E>>p_crit>>V_S>>n_span>>S>>dpre0>>dpre1>>dprem>>dpost0>>dpost1>>dpostm>>bottleneck>>SLO2>>Pgap;
            sb08::EndgameMergeInputs in;
            in.m0=m0; in.m1=m1; in.wait_ms=wait; in.V_E=V_E; in.p_crit=p_crit; in.V_S=V_S; in.n_span=n_span;
            in.S=S; in.dpre_m0=dpre0; in.dpre_m1=dpre1; in.dpre_merged=dprem;
            in.dpost_m0=dpost0; in.dpost_m1=dpost1; in.dpost_merged=dpostm;
            in.local_is_bottleneck = (bottleneck!=0);
            in.SLO2=SLO2; in.P_gap_merged=Pgap;
            bool b = sb08::endgame_merge(in);
            std::cout<<(b?1:0)<<"\n";
        } else if(mode=='C'){
            sb08::CliffInputs in;
            // Expect: SLO1 SLO2 R sum_tdr_done sum_est_tdr n_undone sum_spans gaps_done sum_est_spans est_gaps range_tdr range_gap Vtdr htdr Vtpot htpot delta eps
            ss>>in.SLO1>>in.SLO2>>in.R>>in.sum_tdr_done>>in.sum_est_tdr_undone>>in.n_undone
              >>in.sum_spans_done>>in.gaps_done>>in.sum_est_spans_undone>>in.est_gaps_undone
              >>in.range_tdr_per_req_ms>>in.range_span_per_gap_ms
              >>in.V_tdr>>in.h_tdr>>in.V_tpot>>in.h_tpot>>in.delta>>in.eps_ms;
            auto m = sb08::classify_cliff(in);
            std::cout<<(m==sb08::CliffMode::SAFE?1:0)<<"\n";
        } else if(mode=='Z'){
            std::cout<< sizeof(sb08::CliffInputs) << " " << sizeof(sb08::EndgameMergeInputs) << "\n";
        }
        std::cout.flush();
    }
    return 0;
}
#endif
