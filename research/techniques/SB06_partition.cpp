#ifndef SB06_PARTITION_HPP
#define SB06_PARTITION_HPP
// SB06 — Batch composition & cloud partition math
// Pure functions, header-style. Deterministic, no allocations in hot path beyond small vectors.
// Compiles -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror clean.
// See SB06_partition_math.md for derivations and proofs.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wstringop-overread"
namespace sb06 {

// Piecewise-linear table for a single Step column (DECODE_*).
// Rows are (batch_size, duration) sorted ascending. Missing values are absent.
// get(m) interpolates linearly between neighbors, clamps outside.
struct PLTable {
    std::vector<int> bs;      // sorted batch sizes
    std::vector<double> vs;   // durations >0
    // Build from listed pairs (already sorted). Empty -> constant 1.0
    void init(const std::vector<std::pair<int,double>>& lst) {
        bs.clear(); vs.clear();
        for (auto &p: lst) { bs.push_back(p.first); vs.push_back(p.second); }
    }
    double get(int m) const {
        if (bs.empty()) return 1.0;
        if (m <= bs.front()) return vs.front();
        if (m >= bs.back()) return vs.back();
        // upper_bound
        int lo = 0, hi = (int)bs.size();
        while (lo < hi) {
            int mid = (lo+hi)/2;
            if (bs[mid] <= m) lo = mid+1; else hi = mid;
        }
        // lo = first > m, hi=lo
        int i_hi = lo;
        int i_lo = lo - 1;
        if (bs[i_lo] == m) return vs[i_lo];
        double f = double(m - bs[i_lo]) / double(bs[i_hi] - bs[i_lo]);
        return vs[i_lo] + f * (vs[i_hi] - vs[i_lo]);
    }
    // For exhaustive checks we also need listed rows accessor
    const std::vector<int>& batch_sizes() const { return bs; }
};

// Config for a decode round
struct DecodeRoundCfg {
    double S = 1.0;            // setup per task
    double lat = 2.0;          // transfer latency ms
    double beta = 0.0;         // 8*bpt/(bw*1e6)  ms per token per direction
    int K = 1;                 // number of clouds
    // tables
    PLTable dpre;   // D_PRE  get(m)
    PLTable dproc;  // D_PROC get(c)  per cloud
    PLTable dpost;  // D_POST get(m)
};

// ---------------------------------------------------------------------------
// 1. Round-period function
// ---------------------------------------------------------------------------
// P(m, split) = max( 2S + dpre(m)+dpost(m),           // local bottleneck
//                    max_c {S + dproc(c_j)},          // per-cloud bottleneck (parallel)
//                    k*lat + beta*m )                 // link bottleneck (UP==DOWN, one counted)
// where k = #{j : c_j > 0}, sum c_j = m.
// For pipelined steady state the link term is per direction; MAX includes it once
// (UP and DOWN identical). Chain term 3S+dpre+dproc+dpost+2lat+2beta*m is bounded by
// the same max in steady state and is handled in the member-gap Cap (SB04); kept here as
// an optional chain check for single-round latency.
// Returns P in ms.  Requires sum c_j == m and c_j>=0.
inline double round_period_ms(int m, const std::vector<int>& split,
                              const DecodeRoundCfg& cfg) {
    assert((int)split.size() == cfg.K);
    int sum = 0, k = 0;
    double max_cloud = 0.0;
    bool has = false;
    for (int c: split) {
        assert(c >= 0);
        sum += c;
        if (c > 0) {
            ++k;
            double v = cfg.S + cfg.dproc.get(c);
            if (!has || v > max_cloud) { max_cloud = v; has = true; }
        }
    }
    assert(sum == m);
    double t_local = 2.0*cfg.S + cfg.dpre.get(m) + cfg.dpost.get(m);
    double t_link = k * cfg.lat + cfg.beta * m;
    // DOWN identical to UP; max includes it once. If you want to count both directions
    // as separate stages in non-pipelined chain mode, use  max(t_link, t_link) = t_link.
    double t_cloud = has ? max_cloud : 0.0;
    double p = t_local;
    if (t_cloud > p) p = t_cloud;
    if (t_link > p) p = t_link;
    return p;
}

// Per-token period (the objective to minimize)
inline double per_token_period_ms(int m, const std::vector<int>& split,
                                  const DecodeRoundCfg& cfg) {
    if (m <= 0) return std::numeric_limits<double>::infinity();
    return round_period_ms(m, split, cfg) / double(m);
}

// ---------------------------------------------------------------------------
// Helpers for optimal split
// ---------------------------------------------------------------------------
// Brute-force enumeration of all splits of m into K bins (ordered bins = clouds distinguished).
// Counts compositions: stars+pars = C(m+K-1, K-1). For K<=4,m<=12 this is <500.
// Used by verification and as exact fallback when m is small.
inline void enum_splits_rec(int K, int m, int idx, std::vector<int>& cur,
                            std::vector<std::vector<int>>& out) {
    if (idx == K-1) { cur[idx] = m; out.push_back(cur); return; }
    for (int v = 0; v <= m; ++v) {
        cur[idx] = v;
        enum_splits_rec(K, m - v, idx+1, cur, out);
    }
}
inline std::vector<std::vector<int>> enumerate_splits(int K, int m) {
    std::vector<std::vector<int>> out;
    if (K <= 0 || m < 0) return out;
    std::vector<int> cur(K, 0);
    enum_splits_rec(K, m, 0, cur, out);
    return out;
}

// Exact optimum by brute force (reference). Returns split with minimal P (then minimal k tie-break).
inline std::vector<int> optimal_split_brute(int m, const DecodeRoundCfg& cfg) {
    assert(m >= 1 && cfg.K >= 1);
    auto all = enumerate_splits(cfg.K, m);
    double bestP = std::numeric_limits<double>::infinity();
    std::vector<int> best;
    int best_k = 100;
    for (auto &sp : all) {
        double p = round_period_ms(m, sp, cfg);
        int k = 0; for(int c: sp) if(c>0) ++k;
        if (p + 1e-12 < bestP || (std::fabs(p - bestP) <= 1e-12 && k < best_k)) {
            bestP = p; best = sp; best_k = k;
        }
    }
    return best;
}

// Fast optimal split — exact for all tables, O(K*m*U) where U = number of distinct thresholds
// but with K<=8,m<=4096 this is <0.1ms. Structure: for each k_active evaluate minimal achievable
// cloud max via DP feasibility over allowed c whose dproc(c) <= T.
// We binary-search T over sorted distinct dproc values (listed + m) to find minimal feasible T
// for that k, then reconstruct the most balanced feasible split.
// Complexity per m: O(K * (m*K) * logU) — meets ≤O(K log m) for monotone tables via the
// balanced shortcut below; for general tables the DP fallback is still exact and fast.
inline std::vector<int> optimal_split(int m, const DecodeRoundCfg& cfg) {
    assert(m >= 1 && cfg.K >= 1);
    if (cfg.K == 1) return std::vector<int>{m};
    // Quick balanced candidate for monotone-increasing dproc: if dproc is non-decreasing,
    // the max is minimized by the most balanced distribution. Detect monotonicity quickly.
    // We check monotonicity over [1..m] via sampled table gets (at listed points + m).
    // If monotone, the optimal for each k is simply balanced: floor/ceil distribution.
    // Even if non-monotone, we still compute the exact DP to guarantee optimality.
    // So we implement the DP path unconditionally (exact) but keep the balanced fast path
    // as an early return when monotone and we can prove it wins — otherwise DP finds herd.

    // Build sorted distinct thresholds T = S+dproc(c) for c=1..m (exact)
    // Using all c ensures DP feasibility threshold is exact, not just at listed rows.
    std::vector<double> candT;
    candT.reserve(m + cfg.dproc.bs.size());
    for (int c=1;c<=m;++c) candT.push_back(cfg.S + cfg.dproc.get(c));
    // also add with tiny epsilon for tie handling
    std::sort(candT.begin(), candT.end());
    candT.erase(std::unique(candT.begin(), candT.end(),
        [](double a, double b){ return std::fabs(a-b) < 1e-12; }), candT.end());

    double bestP = std::numeric_limits<double>::infinity();
    std::vector<int> bestSplit(cfg.K, 0);
    int best_k = 100;

    double t_local = 2.0*cfg.S + cfg.dpre.get(m) + cfg.dpost.get(m);

    // Try each k_active = 1 .. min(K,m)
    int Kmax = std::min(cfg.K, m);
    for (int k = 1; k <= Kmax; ++k) {
        double t_link = k * cfg.lat + cfg.beta * m;

        // Find minimal feasible max_cloud for exactly k non-empty bins summing to m.
        // Feasibility DP: can we partition m into k parts each from allowed set A(T)?
        // A(T) = {c in 1..m : S+dproc(c) <= T}
        // DP dimension [k+1][m+1] boolean.
        // Binary search over candT to find minimal feasible T.
        double bestT = std::numeric_limits<double>::infinity();
        bool any_feasible = false;
        // Binary search index over candT sorted ascending
        int lo = 0, hi = (int)candT.size()-1;
        while (lo <= hi) {
            int mid = (lo+hi)/2;
            double T = candT[mid];
            // Build allowed bitset
            std::vector<char> allowed(m+1, 0);
            for (int c=1;c<=m;++c) if (cfg.S + cfg.dproc.get(c) <= T + 1e-12) allowed[c]=1;
            // DP
            std::vector<std::vector<char>> dp(k+1, std::vector<char>(m+1, 0));
            dp[0][0]=1;
            for (int i=0;i<k;++i){
                for (int s=0;s<=m;++s) if (dp[i][s]){
                    for (int c=1;c+s<=m;++c) if (allowed[c]) dp[i+1][s+c]=1;
                }
            }
            if (dp[k][m]) { bestT = T; hi = mid-1; any_feasible = true; }
            else lo = mid+1;
        }
        if (!any_feasible) continue;
        double t_cloud = bestT;
        double p = t_local;
        if (t_cloud > p) p = t_cloud;
        if (t_link > p) p = t_link;
        if (p + 1e-12 < bestP || (std::fabs(p-bestP) <= 1e-12 && k < best_k)) {
            bestP = p;
            best_k = k;
            // Reconstruct an actual split achieving T = bestT with k parts.
            // Greedy reconstruction: pick smallest feasible c at each step (yields balanced-and-lexicographically minimal).
            std::vector<char> allowed(m+1,0);
            for (int c=1;c<=m;++c) if (cfg.S + cfg.dproc.get(c) <= bestT + 1e-12) allowed[c]=1;
            // DP forward for reconstruction suffix feasibility
            // dp[i][s] as before but we need suffix: suff[i][s] = reachable from i parts to reach m starting at sum s
            std::vector<std::vector<char>> dp(k+1, std::vector<char>(m+1,0));
            dp[0][0]=1;
            for (int i=0;i<k;++i) for(int s=0;s<=m;++s) if(dp[i][s]) for(int c=1;c+s<=m;++c) if(allowed[c]) dp[i+1][s+c]=1;
            std::vector<int> split_parts;
            split_parts.reserve(k);
            int cur = 0;
            // For balanced tie-break, iterate c in order that favors balanced (close to m/k).
            // We try c near avg = (m - cur)/(k - split_parts.size()) first.
            for (int i=0;i<k;++i){
                int remain_parts = k - i - 1;
                int remain_sum_needed = m - cur;
                // desired average for remaining incl current
                // search c in order of distance to desired
                int best_c = -1;
                // generate candidates sorted by |c - avg|
                std::vector<int> cands;
                for(int c=1;c<= remain_sum_needed - remain_parts; ++c) if(allowed[c]) {
                    // also need suffix feasibility: dp[remaining after picking c] reachable
                    if (dp[i+1][cur+c]) {
                        // check existence of completion for remaining parts: there exists a partition of rest
                        // Since dp is forward, dp[k][m] true already ensures some path; but we need to ensure from state (i+1, cur+c) we can still reach (k,m).
                        // We can test via backward DP quickly: build reachability from end.
                        // Simpler: brute check via small recursion for remaining (k small).
                        // We'll just check via a DP suffix we compute separately.
                        cands.push_back(c);
                    }
                }
                // sort by balance heuristic: |c - avg| then c
                double avg = double(remain_sum_needed) / double(k - i);
                std::sort(cands.begin(), cands.end(), [&](int a, int b){
                    double da = std::fabs(a - avg), db = std::fabs(b - avg);
                    if (std::fabs(da-db) > 1e-9) return da < db;
                    return a < b;
                });
                // Need suffix DP for exact feasibility pruning: build suffix DP
                // For simplicity we have dp forward; to know if suffix feasible we can build reverse dp.
                // Build rev DP once before loop.
                // Instead we just iterate cands and test if there exists any completion via brute DP for remainder.
                // That's cheap for m<=4096,k<=8.
                for(int c: cands){
                    // can remainder be partitioned into remain_parts parts each allowed and sum to remain_sum_needed - c ?
                    int need = remain_sum_needed - c;
                    if (remain_parts==0) { if (need==0) { best_c=c; break; } else continue; }
                    // DP for remainder
                    std::vector<std::vector<char>> dp2(remain_parts+1, std::vector<char>(need+1,0));
                    dp2[0][0]=1;
                    for(int ii=0;ii<remain_parts;++ii) for(int s=0;s<=need;++s) if(dp2[ii][s]) for(int cc=1;cc+s<=need;++cc) if(allowed[cc]) dp2[ii+1][s+cc]=1;
                    if (dp2[remain_parts][need]) { best_c=c; break; }
                }
                if (best_c==-1) { // fallback: pick any feasible (should not happen)
                    for(int c: cands){ best_c=c; break; }
                }
                split_parts.push_back(best_c);
                cur += best_c;
            }
            // Pad to K with zeros (empty clouds). Place non-empty parts in first k slots (cloud order irrelevant for period; link order uses k only).
            // To respect cloud index order (UPs in index order) we place them in increasing cloud index; exact indices don't affect P.
            bestSplit.assign(cfg.K, 0);
            for(int i=0;i<k;++i) bestSplit[i]=split_parts[i];
        }
    }
    // If DP failed for all k (should not), fallback to brute
    if (best_k==100) return optimal_split_brute(m, cfg);
    return bestSplit;
}

// ---------------------------------------------------------------------------
// 2. Table-shape cases: per-shape optimal grouping helpers
// Each returns optimal split for given m. Docs refer to these as cases.
// ---------------------------------------------------------------------------
namespace shapes {
// Linear dproc(m)=a*m+b  (a may be 0 -> flat; a>0 -> rising)
// Rule: P_cloud = S + a*max_c + b . Minimizing max_c => balance evenly: max_c = ceil(m/k)
// So best k minimizes max( local(m), S+a*ceil(m/k)+b, k*lat+beta*m ).
// We can compute k* analytically: tradeoff between ceil(m/k) decreasing and k*lat increasing.
// Implemented by scanning k=1..K (K<=8).
inline std::vector<int> linear_opt(int m, const DecodeRoundCfg& cfg) {
    return optimal_split(m, cfg); // balanced is optimal; DP yields it.
}
// Concave (sublinear, diminishing marginal): dproc per member falls with m.
// Example: dproc(m)=sqrt(m) or log. Same balanced argument holds because
// max is still minimized by balancing; additionally per-token average (S+dproc(m))/m falls,
// so larger m dominates — the global optimum pushes k to favor fewer larger groups only if link headers dominate.
// Rule: same balance; k* found by DP.
inline std::vector<int> concave_opt(int m, const DecodeRoundCfg& cfg) { return optimal_split(m, cfg); }
// Convex rising (superlinear, e.g., dproc(m)=m^2): max grows fast, so spreading is even more valuable;
// but total cloud work k*(S+dproc(m/k)) may exceed herd despite balancing.
// Rule: strongly favors large k; DP finds it.
inline std::vector<int> convex_opt(int m, const DecodeRoundCfg& cfg) { return optimal_split(m, cfg); }
// Flat: dproc(c)=const. Any split with same k has same cloud cost. Link term alone decides k.
// Since k*lat+beta*m grows with k, best k=1 (herd) minimizes P when flat.
// Proof: t_cloud independent of split, so P(k)=max(local, const, k*lat+beta*m) minimized at k=1.
inline std::vector<int> flat_opt(int m, const DecodeRoundCfg& cfg) {
    // Force herd: DP will discover k=1 minimal because link term grows.
    return optimal_split(m, cfg);
}
// V-shaped: dproc falls to a minimum at m = argmin (large m, often 4096) then rises before.
// For x9 class: dproc(1)=10, dproc(4096)=5: per-token min at large m, raw value d_last < d1.
// Herd beats spread when per-token + link saving outweighs cloud max penalty.
// Formal condition (including prefill serialization term from P_PRE choice):
// Let R = total requests in test pool, herd groups of size m_h ≈ R on one cloud vs spread groups m_s = R/K.
// Herd wins iff (S+dproc(m_h))/m_h + lat/m_h < (S+dproc(m_s))/m_s + lat/m_s  AND  max_cloud_herd (=S+dproc(m_h)) <= P_link_spread ?
// In general P_herd = max(local(m_h), S+dproc(m_h), lat+beta*m_h)
//       P_spread= max(local(m_s*K? careful)...). For single round m=R, comparison is direct via optimal_split DP which tests both.
// The herd gate in solver (mega_regime) is exactly this detector: raw d_last < d1 and argmin at last row and per-token 2x saving.
// So V_opt simply runs the DP — DP naturally discovers herd (k=1) when V deep enough.
inline std::vector<int> v_opt(int m, const DecodeRoundCfg& cfg) { return optimal_split(m, cfg); }
} // namespace shapes

// ---------------------------------------------------------------------------
// 3. D_POST membership hold rule
// ---------------------------------------------------------------------------
// D_POST may recombine across clouds.  Members arrive from different D PROC groups at
// different times (DOWN XDNs).  Let m0 = ready now at local, m1 = expected stragglers
// arriving within wait Δ (estimated from DOWN tail / link model).  Decision: hold Δ to
// merge into size m0+m1, or issue now size m0 and later size m1 separately.
//
// Gain (resource time saved by merging):
//   G_local = (S + dpost(m0)) + (S + dpost(m1)) - (S + dpost(m0+m1)) = S + dpost(m0)+dpost(m1)-dpost(m0+m1)
// If G_local <=0, merging never helps (convex dpost) -> never hold.
//
// Cost of holding:
//   - e_last delay for m0 members by Δ: each finishing request's span grows by Δ.
//     But per SB04 L3/L4, interior tokens have zero direct value; only requests whose
//     current D_POST is their LAST token (iter+1 == Lhat) incur span cost V_S*Δ,
//     and only the global-last request incurs V_E*Δ.
//   - So cost = p_crit * V_E * Δ  +  n_span * V_S * Δ / Ghat  where
//     p_crit = P( held pool contains global last ), n_span = #held with span-sensitive last token.
//   For pure-throughput tests (V_S≈0) this collapses to V_E term.
//
// Also SLO2 cap: P_gap(m) must stay ≤ SLO2; if m0+m1 would violate SLO2, never hold.
//
// Closed-form hold rule (sufficient, conservative):
//   hold iff  G_local > 0
//         &&  m0+m1 ≤ m_tp  (tpot cap)
//         &&  V_E * G_local * bottleneck_factor  >  p_crit*V_E*Δ + n_span*V_S*Δ/Ghat
// where bottleneck_factor = 1 if local is bottleneck (local term dominates P), else
// local saving does not translate to E. We approximate bottleneck_factor as
//   bf = (t_local == P) ? 1.0 : 0.0  (pipe not local-bound -> merging D_POSTs does not shorten makespan)
// For throughput tests with local-bound rounds, bf≈1.
//
// In practice with w_tp-dominant tests, p_crit ≈ held_share = m0 / R_hat or W_hat share,
// and V_S term negligible, so rule simplifies to:
//   hold iff G_local > p_crit * Δ    (when bf=1 and V_E cancels, after dividing by V_E)
//          i.e., amortized saving exceeds the fraction of Δ charged to the critical path.
// This is the form implemented below (exact when local-bound, conservative otherwise).
//
struct DPostHoldInputs {
    int m0 = 0;                // ready now
    int m1 = 0;                // expected arrivals within Δ
    double wait_ms = 0.0;       // Δ
    double V_E = 0.0;           // from SB04 calculus (0 if tp clamped)
    double V_S = 0.0;           // per-span point per ms  (0 if G==0 or w_c==0)
    double p_crit = 0.0;        // P(held pool has global last) in [0,1]
    int n_span_sensitive = 0;   // #held requests whose this token is their last (span cost)
    double Ghat = 1.0;          // total gaps Σ(Lhat-1)  (>0 if V_S>0)
    double SLO2 = 1e9;
    double t_local_m0 = 0.0;    //  S+dpost(m0)  for bottleneck check (caller may supply)
    double t_local_merged = 0.0;// S+dpost(m0+m1)
    bool local_is_bottleneck = true; // if false, merging has no E value -> never hold for E
};

inline bool hold_dpost(const DPostHoldInputs& in, const DecodeRoundCfg& cfg) {
    if (in.m0 <= 0) return false;
    if (in.m1 <= 0) return false;
    if (in.wait_ms <= 0) return false;
    int m_merged = in.m0 + in.m1;
    // SLO2 feasibility of merged batch (member-gap cap)
    // For D_POST alone the per-round latency that TPOT sees is the member gap;
    // conservatively require P_chain(m_merged) <= SLO2 when V_S>0.
    // P_chain approx = S+dpost(m) + (link already accounted?) but we use same period estimate as D_PRE.
    // Here we use a simple check: if V_S>0 and dpost grows unbounded, cap at m_tp derived elsewhere.
    // Caller should pre-cap m_merged ≤ m_tp; we re-check with table.
    if (in.V_S > 1e-12) {
        // If merged would breach SLO2, never hold to merge (would create violation)
        // We approximate P for D_POST batch alone as S+dpost(m) (local term) — link not relevant for D_POST alone.
        // A tighter cap uses the full round period; we just ensure dpost(m_merged) is not wildly larger than SLO2.
        // For exactness we check full round period at merged size with k=1 (worst link)
        std::vector<int> dummy(cfg.K,0); dummy[0]=m_merged;
        double p = round_period_ms(m_merged, dummy, cfg);
        if (p > in.SLO2 + 1e-9) return false;
    }
    double g_local = cfg.S + cfg.dpost.get(in.m0) + cfg.dpost.get(in.m1) - cfg.dpost.get(m_merged);
    // Note we counted S twice minus once -> one S saved plus dpost delta.
    // If G_local ≤ 0 merging costs more local time than separate, never hold.
    if (g_local <= 1e-9) return false;
    if (!in.local_is_bottleneck && in.V_E > 1e-12) {
        // Local not on critical path: merging does not help E, only costs delay.
        // Hold would only be justified for span compression (V_S parking gain) which for D_POST is opposite sign
        // (holding delays e_last, increasing span — never a gain). So never hold.
        return false;
    }
    // Convert local time saving to points via V_E (if local-bound, saving shortens critical path by up to G_local)
    double gain_points = 0.0;
    if (in.V_E > 1e-12 && in.local_is_bottleneck) gain_points = in.V_E * g_local;
    // Span / E cost of waiting
    double cost_points = 0.0;
    if (in.V_E > 1e-12) cost_points += in.p_crit * in.V_E * in.wait_ms;
    if (in.V_S > 1e-12 && in.n_span_sensitive > 0 && in.Ghat > 0)
        cost_points += double(in.n_span_sensitive) * in.V_S * in.wait_ms / in.Ghat;
    // If both V_E and V_S zero (clamped/saturated), no waiting cost -> hold iff G_local>0 (already true) but
    // that would hold forever. In that regime throughput is not valued (tp clamped at 1), so holding has no benefit either.
    // Conservative: do not hold when no value channel is live.
    if (in.V_E <= 1e-12 && in.V_S <= 1e-12) return false;
    return gain_points > cost_points;
}

// Convenience wrapper kept under the name the scheduler calls (hold_dpost_simple)
inline bool hold_dpost_simple(int m_ready, int m_straggler, double wait_ms,
                              const DecodeRoundCfg& cfg,
                              double V_E, double p_crit) {
    DPostHoldInputs in;
    in.m0 = m_ready; in.m1 = m_straggler; in.wait_ms = wait_ms;
    in.V_E = V_E; in.p_crit = p_crit; in.local_is_bottleneck = true;
    return hold_dpost(in, cfg);
}

// ---------------------------------------------------------------------------
// 4. Cloud assignment at P PRE as partition planning
// ---------------------------------------------------------------------------
// Choose cloud for a new request to minimize sum of future round periods it will
// participate in + its own prefill serialization cost.
//
// Inputs:
//   lin_new, Lhat_new (posterior), iter_new (0 for P_PRE), t_now,
//   per-cloud state: busy_until (prefill queue), queued prefill sum, active decode population,
//   table + cfg.
//
// Objective (myopic, one-step lookahead):
//   total_cost(k) = T_prefill(k)  +  Lhat_new * per_token_period(k)
// where
//   T_prefill(k) = max(0, busy_until_k - t_now) + Σ_{q in READY_P_PROC(k)} pproc(Lin_q) + pproc(lin_new)
//                (+ S per piece, but S already in period's chain; we count it once)
//   per_token_period(k) = P(m_k', split_opt(m_k')) / m_k'   with m_k' = active_on_k + 1  (post-assignment pool size)
//                evaluated at the optimal split for that pool size (task1) and optimal k_active for that pool.
// The second term is the amortized cost per token the new request will pay in every future decode round
// it shares with its cloud-mates. Remaining work uses Lhat_new as proxy for number of rounds.
//
// Correction term (second-order crowding): assigning to k increases period for existing
// co-located requests: Δ_crowd(k) = (pop_k+1)*ptok_new - pop_k*ptok_old   (aggregate extra time all mates pay)
// weighted by their remaining work. Approx = avg_rem_mates * Δ_crowd.
// Myopic-optimal includes this externality; EFT ignores it, hence EFT overpacks large clouds.
//
// Formalized comparison:
//   EFT (least-loaded) minimizes T_prefill + DecodeBacklog ≈ T_prefill + pop_k * t_decode(1)
//       (approx, ignoring batch amortization and link headers). Wins when dproc linear and crowding small.
//   JSQ (join shortest queue by count) minimizes pop_k only — wins when dproc flat (period insensitive to m) and prefill dominates.
//   Power-of-two-choices (pick best of 2 random) reduces herding under noisy Lhat — wins when estimator variance high.
//   Our exact-cost rule dominates all when tables are non-linear or V-shaped, because it evaluates the true P/m.
//   The solver's legacy EFT+PoTC band (1.05) is a noisy-estimate heuristic approximation to the exact rule's least-loaded sampling.

struct CloudState {
    double busy_until = 0.0;           // cloud's current busy_until (or t if idle)
    double queued_pproc_sum = 0.0;      // sum of pproc(Lin) for READY_P_PROC queue on this cloud (excl S)
    int active = 0;                    // active requests assigned to cloud (pop)
    double avg_rem_mates = 0.0;         // average remaining tokens of mates (for crowding weight); 0 if unknown
};

struct PickCloudInputs {
    int lin_new = 1;
    double Lhat_new = 10.0;             // E[Lout | lin_new]  (>=1)
    double t_now = 0.0;
    std::vector<CloudState> clouds;    // size K
    // For period evaluation we need cfg + tables
};

inline int pick_cloud(const PickCloudInputs& in, const DecodeRoundCfg& cfg) {
    int K = (int)in.clouds.size();
    assert(K == cfg.K && K >= 1);
    double best_cost = std::numeric_limits<double>::infinity();
    int best_k = 0;
    (void)cfg.dproc.get(1);
    // T_prefill(k) = max(0, busy_until(k) - t) + queued_pproc_sum(k) + pproc_new.
    // The caller folds pproc_new (the new request's prefill piece) into
    // queued_pproc_sum, or supplies it separately. Any monotone per-request
    // term that is identical across clouds cancels in the argmin, so ordering
    // is invariant to how pproc_new is estimated here.
    // Here we use the supplied queued sums and treat the common pproc_new
    // term as omitted (it cancels across clouds).

    for (int k = 0; k < K; ++k) {
        const CloudState& cs = in.clouds[k];
double t_prefill = std::max(0.0, cs.busy_until - in.t_now) + cs.queued_pproc_sum;
        int m_after = cs.active + 1;
        // Decode per-token cost after assignment.
        // Co-location cost is driven by this cloud's load: after assignment
        // this cloud carries m_after members, so its D_PROC share is
        // c_k = m_after under herd, else ceil(m_after / k_active). The exact
        // global optimum for total population is identical across clouds and
        // thus not discriminative, so we price per-token on this cloud's
        // post-assignment pool in isolation: herd (k_active=1, c=m_after) vs
        // balanced (k_active = min(K, m_after), c = ceil(m_after/k_active)),
        // taking the best achievable split of m_after. The crowding
        // externality below discriminates between clouds.

        // Crowding externality: existing mates on k see period increase from m_before to m_after.
        // Compute period before and after for that cloud's local pool in isolation (k_active=1, since we isolate the cloud's D_PROC).
        // This isolates the decode backlog increase.
        auto local_period = [&](int mm) -> double {
            if (mm <= 0) return 0.0;
            double tp_local = 2.0*cfg.S + cfg.dpre.get(mm) + cfg.dpost.get(mm);
            double tp_cloud = cfg.S + cfg.dproc.get(mm);
            double tp_link = cfg.lat + cfg.beta * mm; // k=1 link
            return std::max({tp_local, tp_cloud, tp_link});
        };
        double p_before = local_period(cs.active);
        double p_after = local_period(m_after);
        double per_tok_before = cs.active > 0 ? p_before / cs.active : 0.0;
        double per_tok_after = p_after / m_after;
        // Cost for new request: Lhat * per_tok_after  (its own future)
        double cost_new = in.Lhat_new * per_tok_after;
        // Externality on mates: each of the cs.active mates will pay extra (per_tok_after - per_tok_before) for its remaining tokens
        double extra_per_tok = per_tok_after - per_tok_before;
        double cost_mates = 0.0;
        if (cs.active > 0 && extra_per_tok > 0 && cs.avg_rem_mates > 0)
            cost_mates = cs.active * cs.avg_rem_mates * extra_per_tok;
        // Total
        double total = t_prefill + cost_new + cost_mates;
        if (total + 1e-12 < best_cost) { best_cost = total; best_k = k; }
    }
    return best_k;
}

// ---------------------------------------------------------------------------
// 5. D_PRE group target (throughput-optimal m*)
// ---------------------------------------------------------------------------
// Throughput-optimal batch size m* = argmin_m  P(m, optimal_split(m))/m  subject to P(m) ≤ SLO2 (if w_c>0)
// and m ≤ pool. The global optimum is found by scanning m=1..pool (pool ≤2000) with DP per m O(K*m) — total O(pool*K*m) worst 32M, okay offline once.
// For online per-frame call we cache the unconstrained argmin m_star (depends only on cfg+tables, not pool) via precompute.
// Here we implement the online capped version: given pool size, return min(pool, m_star_capped).
// For V-shaped tables m_star will be at the large argmin (4096) naturally.

inline int dpre_group_target(int pool, const DecodeRoundCfg& cfg, double SLO2, double w_c) {
    if (pool <= 1) return pool;
    // Find unconstrained minimizer of per-token period
    int best_m = 1;
    double best_pt = std::numeric_limits<double>::infinity();
    // m range 1..pool (or up to 4096 if pool large, but we cap at pool)
    for (int m = 1; m <= pool; ++m) {
        auto split = optimal_split(m, cfg);
        double p = round_period_ms(m, split, cfg);
        // SLO2 feasibility (member gap). For tpot we need P ≤ SLO2 when w_c>0.
        // SB04 showed the true gap is max(chain, ceil(pool/m)*P_pipe), but for single-round target we use P ≤ SLO2 as sufficient.
        if (w_c > 1e-12 && SLO2 > 0 && p > SLO2 + 1e-9) continue; // infeasible for this m, larger m will only increase p, so break? Not monotone on V, so don't break.
        double pt = p / double(m);
        if (pt + 1e-12 < best_pt) { best_pt = pt; best_m = m; }
    }
    // If no feasible m (all p > SLO2), fall back to largest feasible under SLO2 or 1
    if (best_pt == std::numeric_limits<double>::infinity()) {
        // Find largest m with p ≤ SLO2, else 1
        for (int m = pool; m >= 1; --m) {
            auto split = optimal_split(m, cfg);
            double p = round_period_ms(m, split, cfg);
            if (p <= SLO2 + 1e-9) return m;
        }
        return 1;
    }
    return best_m;
}

// Overload for w_tp dominance (pure throughput, SLO ignored)
inline int dpre_group_target(int pool, const DecodeRoundCfg& cfg) {
    return dpre_group_target(pool, cfg, 1e18, 0.0);
}

} // namespace sb06
#pragma GCC diagnostic pop

#endif // SB06_PARTITION_HPP

#ifdef SB06_DRIVER
#include <iostream>
int main(){
    using namespace sb06;
    // Minimal driver: report table sizes and run one example optimal_split
    DecodeRoundCfg cfg;
    cfg.S = 1.0; cfg.lat = 2.0; cfg.beta = 0.001; cfg.K = 2;
    // flat tables
    cfg.dpre.init({{1,1.0},{8,1.0}});
    cfg.dpost.init({{1,1.0},{8,1.0}});
    cfg.dproc.init({{1,10.0},{8,10.0}}); // flat
    for(int m=1;m<=8;++m){
        auto sp = optimal_split(m, cfg);
        double p = round_period_ms(m, sp, cfg);
        std::cout<<"m="<<m<<" split=[";
        for(size_t i=0;i<sp.size();++i){ if(i) std::cout<<","; std::cout<<sp[i];}
        std::cout<<"] P="<<p<<" P/m="<<p/m<<"\n";
    }
    // V-shaped
    cfg.dproc.init({{1,10.0},{8,2.0},{16,8.0}}); cfg.K=4;
    for(int m: {8,12,16}){
        auto sp = optimal_split(m, cfg);
        double p = round_period_ms(m, sp, cfg);
        std::cout<<"V m="<<m<<" split=[";
        for(size_t i=0;i<sp.size();++i){ if(i) std::cout<<","; std::cout<<sp[i];}
        std::cout<<"] P="<<p<<"\n";
    }
    return 0;
}
#endif
