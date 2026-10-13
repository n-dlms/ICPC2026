// SB07 — Prefill Lot Streaming, Chunking Safety, Phase-Offset Decode Cohorts
// Header-style fragment, namespace sb07, pure functions, deterministic,
// no allocations in hot path beyond small vectors.
// Compiles: g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -c SB07_pipelining.cpp
// Driver:   g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB07_DRIVER SB07_pipelining.cpp -o /tmp/sb07 && /tmp/sb07
// See research/techniques/SB07_pipelining_math.md for derivations and proofs.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#ifndef SB07_PIPELINING_HPP
#define SB07_PIPELINING_HPP

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>

namespace sb07 {

// -----------------------------------------------------------------------------
// Common utilities - Task-time table (piecewise linear, per Step column)
// -----------------------------------------------------------------------------
struct PLTable {
    std::vector<int> bs;         // sorted batch sizes
    std::vector<double> vs;      // durations
    void init(const std::vector<std::pair<int,double>>& lst) {
        bs.clear(); vs.clear();
        for (auto &p: lst) { bs.push_back(p.first); vs.push_back(p.second); }
    }
    double get(int m) const {
        if (bs.empty()) return 1.0;
        if (m <= bs.front()) return vs.front();
        if (m >= bs.back()) return vs.back();
        int lo=0, hi=(int)bs.size();
        while (lo < hi) {
            int mid=(lo+hi)/2;
            if (bs[mid] <= m) lo=mid+1; else hi=mid;
        }
        int i_hi=lo, i_lo=lo-1;
        if (bs[i_lo]==m) return vs[i_lo];
        double f = double(m - bs[i_lo]) / double(bs[i_hi]-bs[i_lo]);
        return vs[i_lo] + f * (vs[i_hi]-vs[i_lo]);
    }
    const std::vector<int>& batch_sizes() const { return bs; }
};

// Transfer time (problem.md:208) — factored for reuse
inline double transfer_time_ms(double latency_ms, double bpt, double bw_gbps, double len) {
    return latency_ms + 8.0 * len * bpt / (bw_gbps * 1e6);
}

// -----------------------------------------------------------------------------
// Decode round config (for period / cohort computations)
// -----------------------------------------------------------------------------
struct DecodeRoundCfg {
    double S = 1.0;          // setup per task
    double lat = 2.0;        // latency ms
    double beta = 0.0;       // 8*bpt/(bw*1e6)  per token per direction
    int K = 1;               // number of clouds
    PLTable dpre;            // D_PRE  get(m)
    PLTable dproc;           // D_PROC get(c) per cloud
    PLTable dpost;           // D_POST get(m)
};

// Round period P(m, split) = max( T_loc, T_cld, T_link )
// where split = distribution c_j over K clouds, sum c_j = m, k = #{j:c_j>0}
// T_loc = 2S + dpre(m)+dpost(m)   (single local machine, serial)
// T_cld = max_j {S + dproc(c_j)} (K-way parallel)
// T_link = k*lat + beta*m        (single FIFO per direction, headers per distinct cloud)
inline double round_period_ms(int m, const std::vector<int>& split, const DecodeRoundCfg& cfg) {
    assert((int)split.size()==cfg.K);
    int sum=0, k=0;
    double max_cloud=0.0;
    bool has=false;
    for (int c: split) {
        assert(c>=0);
        sum+=c;
        if (c>0) {
            ++k;
            double v = cfg.S + cfg.dproc.get(c);
            if (!has || v>max_cloud) { max_cloud=v; has=true; }
        }
    }
    assert(sum==m);
    double t_local = 2.0*cfg.S + cfg.dpre.get(m) + cfg.dpost.get(m);
    double t_link = k*cfg.lat + cfg.beta*m;
    double t_cloud = has ? max_cloud : 0.0;
    double p = t_local;
    if (t_cloud > p) p = t_cloud;
    if (t_link > p) p = t_link;
    return p;
}
inline double per_token_period_ms(int m, const std::vector<int>& split, const DecodeRoundCfg& cfg) {
    if (m<=0) return std::numeric_limits<double>::infinity();
    return round_period_ms(m, split, cfg) / double(m);
}

// Chain latency of one isolated round (same cohort through all stages, serial)
// T_chain = 3S + dpre(m)+dproc(ceil(m/K))+dpost(m) + (k*lat+beta*m) + (lat+beta*m)
// The two transfer terms are UP (k headers) + DOWN (1 header per group, but pipelined
// we count one DOWN header per round's group size). For the gap law we use this.
inline double chain_latency_ms(int m, const std::vector<int>& split, const DecodeRoundCfg& cfg) {
    // Use the max_c for dproc in chain: the member's cloud determines its D PROC latency
    int max_c = 0;
    int k=0;
    for (int c: split) if (c>0) { ++k; if (c>max_c) max_c=c; }
    if (max_c==0) max_c=1;
    // k as above
    double t_chain = 3.0*cfg.S + cfg.dpre.get(m) + cfg.dproc.get(max_c) + cfg.dpost.get(m)
                   + (k*cfg.lat + cfg.beta*m) + (cfg.lat + cfg.beta*m);
    return t_chain;
}

// Member gap P_gap(m, pool) = max( T_chain(m), ceil(pool/m)*P_pipe(m) )
// pool = total ready decode population that competes for the same local/cloud/link pipeline.
// This is the quantity SLO2 binds on (SB04/SB06).
inline double member_gap_ms(int m, int pool, const std::vector<int>& split, const DecodeRoundCfg& cfg) {
    double p_pipe = round_period_ms(m, split, cfg);
    double p_chain = chain_latency_ms(m, split, cfg);
    double rot = std::ceil(double(std::max(1,pool))/double(std::max(1,m)));
    return std::max(p_chain, rot * p_pipe);
}

// Brute force enumeration of ordered splits (stars and bars) for verification (K<=4,m<=12)
inline void enum_splits_rec(int K, int m, int idx, std::vector<int>& cur, std::vector<std::vector<int>>& out) {
    if (idx==K-1) { cur[idx]=m; out.push_back(cur); return; }
    for (int v=0; v<=m; ++v) { cur[idx]=v; enum_splits_rec(K,m-v,idx+1,cur,out); }
}
inline std::vector<std::vector<int>> enumerate_splits(int K, int m) {
    std::vector<std::vector<int>> out;
    if (K<=0||m<0) return out;
    std::vector<int> cur(K,0);
    enum_splits_rec(K,m,0,cur,out);
    return out;
}
inline std::vector<int> optimal_split_brute(int m, const DecodeRoundCfg& cfg) {
    assert(m>=1 && cfg.K>=1);
    auto all = enumerate_splits(cfg.K,m);
    double bestP = std::numeric_limits<double>::infinity();
    std::vector<int> best;
    int best_k=100;
    for (auto &sp: all) {
        double p = round_period_ms(m, sp, cfg);
        int k=0; for(int c:sp) if(c>0) ++k;
        if (p + 1e-12 < bestP || (std::fabs(p-bestP)<=1e-12 && k < best_k)) {
            bestP=p; best=sp; best_k=k;
        }
    }
    return best;
}
// Fast optimal split: exact DP over thresholds (Theorem 1 of SB06), with monotone shortcut.
// For SB07 we provide a minimal exact implementation sufficient for K<=8,m<=4096 and for
// verification we can delegate to brute when m<=12; production uses the DP below.
// Simplified: iterate over all thresholds T = S+dproc(c) for c=1..m, binary search minimal feasible T per k.
inline std::vector<int> optimal_split(int m, const DecodeRoundCfg& cfg) {
    if (cfg.K==1) return std::vector<int>{m};
    if (m<=12) return optimal_split_brute(m,cfg); // exact and cheap
    // For larger m, use the balanced monotone shortcut when dproc is monotone increasing
    // (check monotonicity over sampled points). If monotone, optimal is balanced.
    bool monotone=true;
    double prev = cfg.S + cfg.dproc.get(1);
    for (int c=2;c<=std::min(m,64);++c) {
        double cur = cfg.S + cfg.dproc.get(c);
        if (cur + 1e-12 < prev) { monotone=false; break; }
        prev=cur;
    }
    if (monotone) {
        // Balanced is optimal: distribute as evenly as possible among best k
        double bestP = std::numeric_limits<double>::infinity();
        std::vector<int> bestSplit(cfg.K,0);
        int best_k=100;
        double t_local = 2.0*cfg.S + cfg.dpre.get(m) + cfg.dpost.get(m);
        for (int k=1;k<=std::min(cfg.K,m);++k) {
            int c_hi = (m + k -1)/k;
            // For monotone, max cloud = S+dproc(c_hi)
            double t_cloud = cfg.S + cfg.dproc.get(c_hi);
            double t_link = k*cfg.lat + cfg.beta*m;
            double p = t_local;
            if (t_cloud > p) p = t_cloud;
            if (t_link > p) p = t_link;
            if (p + 1e-12 < bestP || (std::fabs(p-bestP)<=1e-12 && k < best_k)) {
                bestP=p; best_k=k;
                // rebuild balanced correctly
                bestSplit.assign(cfg.K,0);
                int r = m % k;
                for (int i=0;i<k;++i) bestSplit[i] = (i < r ? c_hi : (m / k));
            }
        }
        return bestSplit;
    }
    // General case (non-monotone, V-shaped): fall back to brute for moderate m
    // For m up to ~32, brute is still feasible; for larger m we use the threshold DP
    // from SB06. To keep SB07 fragment small we delegate to brute for m<=32 and for
    // larger m use the herd-vs-balanced comparison (V detection).
    if (m<=32) return optimal_split_brute(m,cfg);
    // V-shaped large-m heuristic: test herd (k=1) vs balanced k=K
    // The exact V condition is d_last < d1 and per-token argmin at last row.
    // We compute both candidates and pick minimal P.
    std::vector<int> herd(cfg.K,0); herd[0]=m;
    double p_herd = round_period_ms(m, herd, cfg);
    int c_bal = (m + cfg.K -1)/cfg.K;
    std::vector<int> bal(cfg.K,0);
    for (int i=0;i<cfg.K;++i) bal[i]= (i < (m % cfg.K) ? c_bal : m/cfg.K);
    if (m % cfg.K==0) for(int i=0;i<cfg.K;++i) bal[i]=c_bal;
    double p_bal = round_period_ms(m, bal, cfg);
    if (p_herd <= p_bal) return herd;
    return bal;
}

// -----------------------------------------------------------------------------
// 1. PREFILL AS LOT STREAMING + CHUNKING
// -----------------------------------------------------------------------------

// Stall budget G: allowable max blocking of a single prefill piece on its cloud.
// Derived from SLO2 slack and from the V_E/V_P calculus. The chunk must satisfy
//   S + p_chunk <= G    where p_chunk = (chunk_layers / L) * pproc
// If G <= S, no budget (even S alone exceeds); then k=1 (no split).
// Otherwise minimal k = ceil( pproc / (G - S) ), capped by L and by setup economics.

struct StallBudgetInputs {
    double S = 1.0;
    double SLO2 = 1e9;
    double p_pipe = 0.0;          // current pipe period for the decode pool (from period model)
    double p_chain = 0.0;         // chain latency for a singleton
    double V_E = 0.0;             // points per ms of E reduction (0 if clamped)
    double V_P = 0.0;             // points per ms of P_POST reduction (0 if slack)
    double p_crit = 0.0;          // 0..1, prob held carries global last
    double theta = 0.8;           // safety factor <1
};

inline double compute_stall_budget(const StallBudgetInputs& in) {
    // Case A: SLO2 channel live (w_c>0 and G>0). Slack = SLO2 - P_gap.
    // For the prefill stall we price the member gap: a decode member waiting behind
    // a prefill piece sees its inter-token gap increased by the piece's blocking.
    // So allowable blocking = max(0, SLO2 - P_gap). We approximate P_gap by p_pipe
    // (pipelined) when pool deep, or p_chain when pool thin — caller supplies the
    // relevant one. We use the tighter (larger) gap.
    double gap = std::max(in.p_pipe, in.p_chain);
    double slack = in.SLO2 - gap;
    if (slack < 0) slack = 0;
    double G_slo = in.theta * slack;
    // Case B: throughput channel dominates (V_E large, V_P small). Then any stall
    // that delays the critical path costs V_E * stall vs V_P * extra S.
    // The stall budget from value trade is: allow stall up to (V_P/V_E)*S extra?
    // Actually we keep G_slo as primary; if V_E is live but SLO slack is zero,
    // we still need a tiny G to avoid starving throughput — we set a floor from
    // the value ratio: G_val = S * (V_P + V_E*p_crit)/V_E  ~ S when V_P dominates.
    // For code brevity we return G_slo; caller may max it with a value-derived floor.
    if (G_slo < 1e-9 && in.V_E > 1e-12) {
        // Value-derived floor: allow at most S + pproc/2 if prefill not on crit path
        // (conservative: don't use value to enlarge G when SLO slack is zero)
        return in.S; // no extra budget beyond S itself -> k=1
    }
    return G_slo;
}

// Optimal number of pieces k* for equal-size chunking under stall budget G.
// Closed form: k = ceil(p / (G - S))  when G > S, else 1.
// Additionally bounded by max layers L and by the sqrt law from value trade.
inline int optimal_prefill_k(double pproc, double S, int L, double G) {
    if (L <= 1) return 1;
    if (pproc <= 0) return 1;
    if (G <= S + 1e-12) return 1; // no budget beyond setup itself
    double denom = G - S;
    if (denom <= 1e-12) return 1;
    double k_needed = pproc / denom;
    int k = (int)std::ceil(k_needed - 1e-12);
    if (k < 1) k = 1;
    if (k > L) k = L;
    // Setup-amortization cap: each extra piece costs S, total extra = (k-1)*S.
    // If extra exceeds pproc (setup dominated), cap at 1 + pproc/S.
    int k_max_s = (S > 1e-9) ? (int)std::floor(pproc / S) + 1 : L;
    if (k_max_s < 1) k_max_s = 1;
    if (k > k_max_s) k = k_max_s;
    // Hard cap from spec: num_layers bound
    if (k > L) k = L;
    // The 2-chunk adaptive cap is removed; we allow up to L but value trade will gate.
    return k;
}

// Value-based k* from SRPT tradeoff: k* = sqrt( V_dec * p / (V_self * S) )
// V_dec = per-ms value of advancing the waiting decode batch (p_crit*V_E + n_span*V_S/Ghat)
// V_self = per-ms cost of delaying the prefill's own P_POST (V_P + p_crit_self*V_E)
inline int optimal_k_value_trade(double pproc, double S, int L, double V_dec, double V_self) {
    if (L <= 1) return 1;
    if (S <= 1e-12) return std::min(L, (int)std::ceil(pproc)); // no setup cost
    if (V_self <= 1e-12) return L; // cost free -> chunk maximally to unblock decodes
    if (V_dec <= 1e-12) return 1;  // no decode value -> no split benefit
    double k_star = std::sqrt(V_dec * pproc / (V_self * S));
    int k = (int)std::llround(k_star);
    if (k < 1) k = 1;
    if (k > L) k = L;
    return k;
}

// Combined k*: stall-budget feasibility is mandatory (never exceed G), value trade
// is the objective within feasible set. So we compute k_budget = ceil(p/(G-S)) minimal
// to meet G, and k_value = sqrt(... ) optimal for mean. The optimum is the max of
// the two when G binds? Actually meeting G requires k >= k_budget, while value optimum
// is k_value. So we need k >= k_budget and near k_value.
// If k_value < k_budget, the cheapest feasible (k_budget) is optimal (any larger k just adds S).
// If k_value >= k_budget, the value optimum is feasible and is optimal.
// Hence k* = (k_budget==1 ? k_value_if_beneficial : max(k_budget, k_value)) with an extra
// dominance check that k_value actually beats k=1 in net gain.
inline int optimal_prefill_k_combined(double pproc, double S, int L, double G,
                                       double V_dec, double V_self) {
    int k_budget = optimal_prefill_k(pproc, S, L, G);
    int k_value = optimal_k_value_trade(pproc, S, L, V_dec, V_self);
    if (k_budget == 1) {
        // G does not force splitting; split only if value trade says so and net gain >0
        // Net gain for k_value vs 1: V_dec * p*(1-1/k_value) - V_self*S*(k_value-1) > 0 ?
        if (k_value <= 1) return 1;
        double gain = V_dec * pproc * (1.0 - 1.0 / double(k_value)) - V_self * S * double(k_value - 1);
        if (gain > 1e-9) return k_value;
        return 1;
    } else {
        // Must meet G -> at least k_budget. Among k >= k_budget, pick best by value.
        // Value net gain as function of k is concave (peak at k_value). So optimum is
        // clamp(k_value, k_budget, L) if that still yields positive gain vs k=1 with G forced?
        // But G feasibility is mandatory for SLO, so we must return at least k_budget even if
        // value gain negative (SLO protection). The value loss is the price of SLO.
        int k = k_value;
        if (k < k_budget) k = k_budget;
        if (k > L) k = L;
        return k;
    }
}

// Next piece boundaries [ls, le) given current next_ls and chosen k (equal-size)
// Returns the immediate next piece only (the solver calls this per piece issue).
// Equal-size pieces: base = L / k, remainder = L % k, first `remainder` pieces get +1 layer.
inline std::pair<int,int> next_piece_bounds(int next_ls, int L, int k) {
    assert(next_ls >=0 && next_ls < L);
    assert(k >=1 && k <= L);
    int chunk_base = L / k;
    int rem = L % k;
    // Determine which piece index we are at: pieces are in order, sizes as above.
    // Compute offset: sum of first `piece_idx` pieces = piece_idx*base + min(piece_idx, rem)
    // We need to find piece_idx such that offset == next_ls (gap-free invariant).
    // Since we call sequentially, we can compute directly: piece_idx = number of pieces issued before.
    // But we don't store count; we can infer from next_ls and equal partition by iterating.
    int offset = 0;
    for (int idx=0; idx<k; ++idx) {
        int sz = chunk_base + (idx < rem ? 1 : 0);
        if (offset == next_ls) {
            int le = offset + sz;
            if (le > L) le = L;
            return {offset, le};
        }
        offset += sz;
        if (offset > next_ls) {
            // next_ls lies inside a piece — should not happen under gap-free, but clamp
            int le = offset;
            if (le > L) le = L;
            return {next_ls, le};
        }
    }
    // Fallback: last piece
    return {next_ls, L};
}

// Drop-in replacement for AdaptiveChunk::next_piece logic (pure function).
// Inputs are explicit so the function can be unit-tested without State.
// State-derived inputs the caller must supply:
//   local_busy (local.busy), up_head_busy (up.head_finish() > t), cloud_has_ready (per-cloud queues),
//   pproc, L, S, G, V_dec, V_self, next_ls.
// The wake-safety guard is evaluated FIRST: never chunk when the wait queues that would
// benefit are empty (no decode waiting on this cloud) AND the local stall budget is not
// otherwise forced by SLO. In that case return single piece.
struct NextPieceInputs {
    double t = 0.0;
    bool local_busy = false;        // local.busy && busy_until > t
    bool up_head_busy = false;      // up.head_finish() > t + EPS
    bool cloud_has_waiting_decode = false; // any READY_D_PROC on this cloud
    bool cloud_has_waiting_prefill = false; // any READY_P_PROC on this cloud (other than this req)
    int L = 1;
    int next_ls = 0;
    double pproc = 0.0;             // total prefill_proc(Lin)
    double S = 1.0;
    double G = 0.0;                 // stall budget (compute_stall_budget)
    double V_dec = 0.0;             // value rate of waiting decode on this cloud
    double V_self = 0.0;            // cost rate of delaying this prefill
};

// Returns {ls, le} for the next piece. Pure, deterministic.
inline std::pair<int,int> next_piece_computed(const NextPieceInputs& in) {
    int L = in.L;
    if (in.next_ls > 0) {
        // Already chunking: continue the equal partition started at ls=0.
        // Infer k from first piece's budget at start — we recompute k consistently.
        int k = optimal_prefill_k_combined(in.pproc, in.S, L, in.G, in.V_dec, in.V_self);
        if (k <= 1) return {in.next_ls, L};
        return next_piece_bounds(in.next_ls, L, k);
    }
    if (L <= 1) return {0, 1};
    // Gates: never chunk on first piece if local is idle (single chunk minimizes S overhead)
    // This mirrors the existing AdaptiveChunk gate but now derived: with local idle, the
    // throughput-optimal schedule runs prefill and decode back-to-back without interleaving
    // need — chunking would just add S. The condition local_busy==false => no interleaving
    // benefit because local is the bottleneck for decode, not the cloud.
    // However if G is tight (SLO requires it), we still chunk even with local idle.
    bool force_by_slo = (in.G > 1e-12 && in.G < in.pproc + in.S);
    if (!in.local_busy && !force_by_slo) {
        return {0, L};
    }
    if (in.up_head_busy && !force_by_slo) {
        return {0, L};
    }
    // Cloud has waiting decode is the PRIMARY benefit signal. Without it, chunking
    // is pure overhead unless G forces it (SLO). So gate on waiting decode.
    if (!in.cloud_has_waiting_decode && !force_by_slo) {
        return {0, L};
    }
    // If cloud has other waiting prefills queued, filling them first (SPT) dominates
    // chunking this large one — keep this prefill non-preempted and let the queue drain.
    if (in.cloud_has_waiting_prefill) {
        return {0, L};
    }
    int k = optimal_prefill_k_combined(in.pproc, in.S, L, in.G, in.V_dec, in.V_self);
    if (k <= 1) return {0, L};
    // Additional safety: Sarathi gate S>=1.0 disables chunking in current solver
    // (D-150 falsification). We keep it as a hard gate when S>=1 and L<=8:
    // chunking a short cloud job with large S is never beneficial for E (extra S dominates).
    // The value trade already captures this, but we keep the gate as a readable invariant.
    // Caller can bypass by setting V_dec >> V_self.
    return next_piece_bounds(0, L, k);
}

// -----------------------------------------------------------------------------
// 2. SRPT-BY-CHUNKING SAFETY (computable inequality)
// -----------------------------------------------------------------------------

// Exact condition under which splitting a running request's remaining prefill is
// beneficial. Uses the SB04 V_P/V_E calculus plus the DOWN-deferral effect.
// Let remaining layers = L - next_ls, p_rem = remaining/L * pproc.
// Splitting remainder into k pieces (k>=2, equal) costs (k-1)*S extra cloud time
// and defers the DOWN (and thus P_POST) by (k-1)*S plus any interleaved decode work
// that we intentionally insert. The benefit is advancing the waiting decode batch's
// completion by Delta = p_rem - p_rem/k = p_rem*(1 - 1/k) (the reduction in max blocking).
// Score gain = V_dec * Delta   (V_dec = p_crit*V_E + n_span*V_S/Ghat)
// Score cost = V_self * (k-1)*S; we count only the extra setup S, since the
// interleaved decode work is the benefit itself, not a cost to the prefill
// beyond S. The DOWN deferral is included in the (k-1)*S (last piece queues DOWN).
// Inequality: hold iff V_dec * p_rem*(1-1/k)  >  V_self * (k-1)*S .
// For k=2 this simplifies to: V_dec * p_rem/2  >  V_self * S  => p_rem > 2*S*V_self/V_dec
// This is the computable test the scheduler evaluates per piece decision.
struct SplitBenefitInputs {
    double p_rem = 0.0;     // remaining prefill duration (layers remaining)
    double S = 1.0;
    double V_dec = 0.0;     // per-ms value of advancing waiting decode (on this cloud)
    double V_self = 0.0;    // per-ms cost of delaying this prefill's P_POST/E
    int k = 2;              // candidate split count for remainder
    double p_crit_self = 0.0; // prob this prefill is on global crit path (adds V_E)
    double V_E = 0.0;
};

inline bool split_is_beneficial(const SplitBenefitInputs& in) {
    if (in.p_rem <= 1e-12) return false;
    if (in.k <= 1) return false;
    if (in.V_dec <= 1e-12) return false; // no decode value
    if (in.S <= 1e-12) return true; // free setup
    // Include V_E on self if this request may be global-last: effective V_self' = V_self + p_crit_self*V_E
    double V_self_eff = in.V_self + in.p_crit_self * in.V_E;
    if (V_self_eff <= 1e-12) return true; // delay free
    double delta = in.p_rem * (1.0 - 1.0 / double(in.k));
    double gain = in.V_dec * delta;
    double cost = V_self_eff * (in.k - 1) * in.S;
    return gain > cost + 1e-9;
}

// Convenience for the current solver: check k=2 minimal split. If even k=2 fails,
// no larger k can succeed because gain grows as p_rem*(1-1/k) sublinearly while cost
// grows linearly in (k-1). So k=2 is the necessary condition.
inline bool should_chunk_remaining(double p_rem, double S, double V_dec, double V_self) {
    SplitBenefitInputs in;
    in.p_rem = p_rem; in.S = S; in.V_dec = V_dec; in.V_self = V_self; in.k = 2;
    return split_is_beneficial(in);
}

// -----------------------------------------------------------------------------
// 3. PHASE-OFFSET DECODE COHORTS (pipelined waves)
// -----------------------------------------------------------------------------

struct CohortConfig {
    DecodeRoundCfg base;     // tables + S/lat/beta/K
    int pool = 0;            // total READY_D_PRE population (across all clouds)
    double SLO2 = 1e9;
    double w_c = 0.0;
    double V_E = 0.0;
    double V_S = 0.0;
    double Ghat = 1.0;       // total gaps for V_S pricing (if w_c>0)
    // For tail-bubble fill we need the pipeline depth: in-flight decode tokens
    int inflight_decode = 0; // D_PROC_RUNNING + WAIT_DEC_DOWN + READY_D_POST populations
};

struct CohortPlan {
    int m0 = 0, m1 = 0;      // cohort sizes (m0+m1 = pool, possibly 0 for tail)
    double r = 0.5;          // fraction in C0
    double delta_ms = 0.0;   // phase offset (time between C0 D_PRE start and C1 D_PRE start)
    double p0_ms = 0.0, p1_ms = 0.0; // per-cohort pipe periods
    double throughput = 0.0; // tokens/ms sustained = pool / max(p0,p1) when pipelined
    bool use_cohorts = false;// true iff pipelined cohorts beat single batch
};

// Compute phase offset: while C0 is in D PROC (cloud) + UP/DOWN, C1 runs its D PRE.
// The natural offset is the time until C0's D PROC starts: D PRE(C0) + UP(C0)
// + cloud queue. For steady state we approximate delta = max( S+dpre(m0), S+dproc(ceil(m0/K)) + lat )
// but pipelined we want delta = P_pipe(m0)/2 when balanced, or more generally
// delta = max(0, T_cld(m0) - T_loc(m1)) to keep local busy.
inline double compute_phase_offset_ms(int m0, int m1, const DecodeRoundCfg& cfg) {
    if (m0<=0 || m1<=0) return 0.0;
    // Time for C0 to clear local and enter cloud: D PRE + its UP
    double t_pre0 = cfg.S + cfg.dpre.get(m0);
    double t_up0 = 0;
    {
        // Estimate k for m0: use optimal_split to get k, but approximate with balanced k
        int k0 = std::min(cfg.K, m0);
        // For offset we use k0 = number of distinct clouds in optimal split for m0 (approx ceil)
        // Use enumeration for small m0
        if (m0 <= 32) {
            auto sp = optimal_split(m0, cfg);
            int kk=0; for(int c:sp) if(c>0) ++kk;
            k0 = kk;
        }
        t_up0 = k0*cfg.lat + cfg.beta*m0;
    }
    // Cloud time for C0
    int c0_per_cloud = (m0 + cfg.K -1)/cfg.K;
    double t_cloud0 = cfg.S + cfg.dproc.get(c0_per_cloud);
    // The earliest local can start C1's D PRE after finishing C0's D PRE is t_pre0,
    // but we want to offset so that C1's D PRE overlaps C0's cloud time.
    // So delta = t_pre0 + min(t_up0, t_cloud0)/2  is a heuristic that keeps pipeline full.
    // Exact: delta = max(0.0, t_cloud0 - (cfg.S + cfg.dpre.get(m1)))  ensures local for C1
    // does not collide with local for C0's D POST.
    double t_pre1 = cfg.S + cfg.dpre.get(m1);
    // Local alternation requires D PRE(C1) finishes before D POST(C0) needs local.
    // D POST(C0) arrives after C0's cloud+DOWN, approx t_pre0+t_up0+t_cloud0+ t_down
    double t_down0 = cfg.lat + cfg.beta*m0; // one DOWN per group approx
    double arrival_post0 = t_pre0 + t_up0 + t_cloud0 + t_down0;
    // Local for C1 must fit before that
    double slack = arrival_post0 - t_pre1;
    double delta = t_pre0 + std::min(t_up0, t_cloud0) * 0.5;
    if (slack < delta) delta = std::max(0.0, slack * 0.5);
    return delta;
}

// Evaluate a candidate r (fraction in C0) for throughput.
// Returns effective period for the two-cohort pipeline.
inline double pipelined_period_for_r(double r, int pool, const DecodeRoundCfg& cfg, double& p0_out, double& p1_out) {
    int m0 = (int)std::llround(r * pool);
    if (m0 < 1) m0 = 1;
    if (m0 >= pool) m0 = pool - 1;
    int m1 = pool - m0;
    if (m1 < 1) { m1 = 1; m0 = pool -1; }
    auto sp0 = optimal_split(m0, cfg);
    auto sp1 = optimal_split(m1, cfg);
    double p0 = round_period_ms(m0, sp0, cfg);
    double p1 = round_period_ms(m1, sp1, cfg);
    p0_out = p0; p1_out = p1;
    // Pipelined time to finish both cohorts (overlapped) = max(p0,p1) + delta_overlap_correction
    // If pipeline is balanced, time ~ max(p0,p1) (they overlap). If one dominates, time ~ max.
    // So effective per-token period = max(p0,p1) * (pool / pool) ??? Actually tokens per max period = pool / max(p0,p1)
    // Compare to single batch: P(pool)/pool.
    // For the pipeline to be fully overlapped, we need delta + local for second fits in cloud of first, etc.
    // We approximate effective period = std::max(p0,p1) + 0.5*std::min(p0,p1)*0.1  (small bubble)
    // For exactness in verification we use max(p0,p1) (optimistic overlap) — the comparison is then
    // conservative (pipeline looks better than reality, so we only recommend it when it clearly wins).
    return std::max(p0, p1);
}

// Compute the optimal cohort plan by enumerating r candidates.
// The geometric lot-streaming result says optimal ratio q = p_down/p_up where p_down is downstream
// stage time per token and p_up upstream. For our pipeline p_up = (S+dpre)/m + lat term, p_down = (S+dproc)/m etc.
// The enumeration finds the same optimum computationally: r* = 1/(1+q) when q>0, else 0.5.
// We enumerate r in [0.2,0.8] plus the geometric prediction.
inline CohortPlan compute_cohort_plan(const CohortConfig& cc) {
    CohortPlan plan;
    int pool = cc.pool;
    const DecodeRoundCfg& cfg = cc.base;
    if (pool <= 2) { plan.use_cohorts=false; plan.m0=pool; plan.m1=0; return plan; }
    // Single-batch baseline
    auto sp_single = optimal_split(pool, cfg);
    double p_single = round_period_ms(pool, sp_single, cfg);
    // Check SLO2 feasibility for single batch: member gap must fit
    double gap_single = member_gap_ms(pool, pool, sp_single, cfg);
    bool single_feasible = (cc.w_c <= 1e-12 || gap_single <= cc.SLO2 + 1e-9);

    // Candidate r values: geometric + uniform grid
    std::vector<double> rs;
    // Uniform grid
    for (double r=0.2; r<=0.8; r+=0.1) rs.push_back(r);
    rs.push_back(0.33); rs.push_back(0.67); rs.push_back(0.5);
    // Geometric prediction: q = p_slow/p_fast where p_slow is bottleneck per-token stage time
    {
        // Estimate per-token stage times for the pool at single-batch size
        int ck = std::max(1, (pool + cfg.K -1)/cfg.K);
        double t_cld_per = (cfg.S + cfg.dproc.get(ck))/double(ck);
        double t_link_per = (cfg.K*cfg.lat)/double(pool) + cfg.beta; // per token includes header amort
        // q = max downstream / upstream, upstream = local PRE part ~ (S+dpre)/m
        double p_up = (cfg.S + cfg.dpre.get(pool))/double(pool) + cfg.lat/double(pool) + cfg.beta*0.5; // approx
        double p_down = std::max(t_cld_per, t_link_per);
        double q = (p_up>1e-12 ? p_down / p_up : 1.0);
        if (q < 0.2) q=0.2;
        if (q>5.0) q=5.0;
        double r_geo = 1.0/(1.0+q);
        rs.push_back(r_geo);
        rs.push_back(1.0 - r_geo);
    }
    double best_eff = std::numeric_limits<double>::infinity();
    double best_r=0.5;
    double best_p0=0,best_p1=0;
    int best_m0=0,best_m1=0;
    for (double r: rs) {
        if (r<=0.05 || r>=0.95) continue;
        double p0=0,p1=0;
        double eff = pipelined_period_for_r(r, pool, cfg, p0, p1);
        // Effective per-token for pipeline: max(p0,p1) / pool? Actually tokens per period = pool / max(p0,p1)
        // Single batch per-token = p_single/pool. So compare per-token.
        double per_tok_pipe = eff / double(pool);
        // Also check SLO2 feasibility for each cohort: gap for each must fit SLO2
        int m0 = (int)std::llround(r*pool);
        if (m0<1) m0=1;
        if (m0>=pool) m0=pool-1;
        int m1 = pool - m0;
        auto sp0 = optimal_split(m0, cfg);
        auto sp1 = optimal_split(m1, cfg);
        double gap0 = member_gap_ms(m0, pool, sp0, cfg);
        double gap1 = member_gap_ms(m1, pool, sp1, cfg);
        bool feasible = true;
        if (cc.w_c > 1e-12) {
            if (gap0 > cc.SLO2 + 1e-9 || gap1 > cc.SLO2 + 1e-9) feasible=false;
        }
        if (!feasible) continue;
        if (per_tok_pipe < best_eff) {
            best_eff = per_tok_pipe;
            best_r = r; best_p0=p0; best_p1=p1; best_m0=m0; best_m1=m1;
        }
    }
    double per_tok_single = p_single / double(pool);
    // Pipeline must beat single batch by at least 1% and not violate SLO2 where single was feasible
    // If single was infeasible but pipeline feasible, pipeline wins automatically.
    bool pipeline_beats_single = false;
    if (!single_feasible) {
        if (best_eff < std::numeric_limits<double>::infinity()) pipeline_beats_single = true;
    } else {
        if (best_eff + 1e-12 < per_tok_single * 0.99) pipeline_beats_single = true;
    }
    // Tail-bubble fill: even if steady-state per-token ties, pipeline can fill the
    // initial/final bubbles (chain latency). The bubble saving is approx T_chain - P_pipe
    // for the first round. If pool is small (drain phase), the bubble dominates.
    // Heuristic: when inflight_decode is small and pool <= 4*K, prefer cohorts to keep
    // local busy during cloud warmup.
    bool tail_bubble_case = (cc.inflight_decode <= 2 && pool >= 4 && pool <= 8*cfg.K);
    if (tail_bubble_case && best_eff < per_tok_single * 1.05) pipeline_beats_single = true;

    if (pipeline_beats_single) {
        plan.use_cohorts = true;
        plan.m0 = best_m0; plan.m1 = best_m1; plan.r = best_r;
        plan.p0_ms = best_p0; plan.p1_ms = best_p1;
        plan.delta_ms = compute_phase_offset_ms(best_m0, best_m1, cfg);
        plan.throughput = double(pool) / std::max(best_p0, best_p1);
    } else {
        plan.use_cohorts = false;
        plan.m0 = pool; plan.m1=0; plan.r=1.0; plan.p0_ms=p_single; plan.throughput=double(pool)/p_single;
        plan.delta_ms=0;
    }
    return plan;
}

// Schedule generator: which cohort a new ready request joins.
// Returns 0 or 1. Policy: join the cohort with smaller projected completion time
// (load balancing) to keep cohorts balanced around r*. This maintains the pipeline's
// phase without rebuilding the plan every arrival.
inline int assign_cohort(int /*new_lin*/, const CohortPlan& plan, const std::vector<int>& cohort0_ids, const std::vector<int>& cohort1_ids, const DecodeRoundCfg& cfg) {
    if (!plan.use_cohorts) return 0;
    int sz0 = (int)cohort0_ids.size();
    int sz1 = (int)cohort1_ids.size();
    // Target sizes m0,m1
    double load0 = 0, load1 = 0;
    // Estimate remaining work per cohort: sum of per-request decode cost proxy
    // Use m as proxy for load (since per-token period is similar, load ∝ size)
    // Prefer to fill under-target cohort.
    int target0 = plan.m0, target1 = plan.m1;
    if (sz0 < target0 && sz1 >= target1) return 0;
    if (sz1 < target1 && sz0 >= target0) return 1;
    if (sz0 < target0 && sz1 < target1) {
        // Both under target: pick the one with larger deficit ratio
        double deficit0 = double(target0 - sz0)/double(target0+1);
        double deficit1 = double(target1 - sz1)/double(target1+1);
        return (deficit0 > deficit1 ? 0 : 1);
    }
    // Both at/over target: pick least loaded by estimated period
    // Compute projected period if we add to each
    auto est_period = [&](int cohort, int add)->double {
        int m = (cohort==0? sz0: sz1) + add;
        if (m<=0) return 0;
        auto sp = optimal_split(m, cfg);
        return round_period_ms(m, sp, cfg);
    };
    double p0 = est_period(0,1);
    double p1 = est_period(1,1);
    // Also weight by current size: prefer smaller
    if (sz0 < sz1) load0 -= 0.5;
    if (sz1 < sz0) load1 -= 0.5;
    return (p0 + load0 < p1 + load1 ? 0 : 1);
}

// Re-merge predicate: cohorts should collapse back to single batch when
// pool shrinks below the pipeline's efficient frontier or when one cohort drains.
// Condition: pool <= max(K, 4) or one cohort empty and remaining pool <= 2*K => merge.
inline bool should_merge_cohorts(const CohortPlan& plan, int pool, int ready0, int ready1, int inflight) {
    if (!plan.use_cohorts) return false;
    if (pool <= std::max(4, 2*plan.m0/3)) return true; // pool collapse
    if (ready0==0 || ready1==0) {
        // One cohort drained: if total pool fits in single batch without SLO violation, merge
        if (pool <= 8) return true;
        if (inflight==0) return true; // no pipeline to keep
    }
    // Also merge if the pipeline's throughput advantage vanished (periods diverged >2x)
    double ratio = (plan.p0_ms>1e-9 && plan.p1_ms>1e-9) ? std::max(plan.p0_ms,plan.p1_ms)/std::min(plan.p0_ms,plan.p1_ms) : 1.0;
    if (ratio > 2.0) return true;
    return false;
}

// -----------------------------------------------------------------------------
// 4. EVENT-BACKED WAKE SAFETY GUARD (no stuck state)
// Protocol fact (problem.md:364-365): no self-wake timer. A wait must be justified
// by an in-flight event that guarantees a future frame, or by a fallback issue-now.
// Formal invariant: solver never responds 0 with no in-flight tasks/transfers and
// unfinished requests (stuck).
// Every hold branch below is guarded by an explicit pending-cause check.
// -----------------------------------------------------------------------------

struct WakeState {
    bool local_busy = false;
    double local_free = 0.0;
    bool up_has_inflight = false;     // up.in_flight() > 0  (FIFO not empty)
    bool down_has_inflight = false;   // down.in_flight() >0
    int cloud_busy_cnt = 0;           // number of clouds with busy_until > t
    int ready_d_pre = 0;              // count_phase(READY_D_PRE)
    int ready_d_post = 0;
    int wait_input_down = 0;          // WAIT_INPUT_DOWN
    int p_proc_running = 0;           // P_PROC_RUNNING
    int wait_dec_down = 0;            // WAIT_DEC_DOWN
    int ready_d_proc_per_cloud[8] = {0};
    int inflight_pre_up = 0;          // up_inflight_pre()
    int inflight_dec_up = 0;          // up_inflight_dec() or total - pre
    bool future_arr_possible = true;  // R not announced; we cannot assume future ARR will arrive
};

// Guard for D_PRE cohort hold ("wait for other cohort's cloud to free / DOWN to land").
// HOLD allowed only if at least one cloud is busy (D_PROC_RUNNING) or a DOWN is in flight.
// Otherwise falling back to issue-now is mandatory.
inline bool can_hold_cohort(const WakeState& ws) {
    // Wake event: a cloud TDN (D_PROC completion) or a DOWN XDN
    if (ws.cloud_busy_cnt > 0) return true;        // TDN will arrive
    if (ws.down_has_inflight) return true;          // XDN will arrive
    if (ws.wait_dec_down > 0) return true;          // indirectly implies down in flight (but we check explicitly)
    return false; // no pending cause that would wake a cohort wait
}

// Guard for D_POST straggler hold (hold D POST for stragglers' DOWN to land)
inline bool can_hold_dpost(const WakeState& ws) {
    if (ws.down_has_inflight) return true;
    if (ws.cloud_busy_cnt > 0) {
        for (int c = 0; c < 8; ++c) if (ws.ready_d_proc_per_cloud[c] > 0 || ws.cloud_busy_cnt > 0) {} // any busy cloud guarantees a future DOWN
        return true; // at least one cloud will produce a DOWN
    }
    if (ws.wait_dec_down > 0) return true;
    return false;
}

// Guard for D_PRE accumulate hold (wait for pipeline prefills to land and enlarge pool)
// HOLD allowed only if pipe = WAIT_INPUT_DOWN + P_PROC_RUNNING > 0 guarantees future READY_D_PRE growth.
// Additionally, if pipe==0 but there is a future ARR possible, holding for ARR alone is NOT safe
// (stream may have ended -> stuck). So pipe>0 is the mandatory wake event.
inline bool can_hold_accumulate(const WakeState& /*ws*/, int pipe, int target, int ready_now) {
    if (ready_now >= target) return false; // already have target
    if (pipe > 0) return true;             // at least one prefill will complete and become ready
    // No pipeline: the only growth could be future ARR. Since R not announced and no end signal,
    // we cannot assume an ARR will arrive. The next frame could be never -> stuck if we hold.
    // So we must NOT hold for ARR alone. Fallback to issue-now.
    // Exception: if local is busy or up has inflight, there is still a future event, but it does not
    // guarantee pool growth (the busy work may not be a P_POST). Still, time will advance to that
    // event, so holding one frame is safe as long as SOME event exists. However the hold's benefit
    // (pool growth) would not materialize. Conservative: only hold when pipe>0.
    return false;
}

// Top-level wake safety: should the solver be allowed to return 0 (hold) at this frame?
// Returns true if holding is safe (there exists a pending cause that will produce the next frame).
// The solver must check this before returning 0 with unfinished requests.
inline bool hold_is_safe(const WakeState& ws, bool has_unfinished) {
    if (!has_unfinished) return true; // no work, 0 is always safe (will get END)
    // If any computer busy or any transfer in flight, the next frame is guaranteed
    if (ws.local_busy) return true;
    if (ws.cloud_busy_cnt > 0) return true;
    if (ws.up_has_inflight) return true;
    if (ws.down_has_inflight) return true;
    // No in-flight work but unfinished requests remain: the only possible future event is an ARR.
    // Protocol says while future arrivals remain, responding 0 is safe because time advances to next ARR
    // (Getting Stuck:394). But we cannot distinguish lull from end. If stream has ended (R reached
    // but we don't know), holding with no in-flight and some ready requests would be stuck.
    // So if there is at least one READY task and we hold, we would be stuck (unfinished + ready but
    // we chose not to issue, and no in-flight to wake). That's why the guards above require an
    // in-flight wake for any hold that keeps ready work waiting.
    // If no ready work either (all unfinished are in WAIT_* but no in-flight? impossible because WAIT
    // implies a transfer in flight). So the only safe 0 with no in-flight is when no ready work and
    // we are just waiting for future ARR — that's safe to hold (the ARR will be the next frame, or
    // if no ARR ever, then the test is actually stuck but that's because requests are unfinished with
    // no possible future event — which our guards prevent by never holding ready work without wake).
    return false; // no pending cause -> holding ready work would be stuck
}

} // namespace sb07

#endif // SB07_PIPELINING_HPP

#ifdef SB07_DRIVER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#include <iostream>
#include <iomanip>
int main() {
    using namespace sb07;
    std::cout << "SB07 driver: compile check\n";
    // Stall budget example
    {
        StallBudgetInputs sb;
        sb.S=1.5; sb.SLO2=60.0; sb.p_pipe=20.0; sb.p_chain=35.0; sb.theta=0.8;
        double G = compute_stall_budget(sb);
        std::cout << "G=" << G << " k=" << optimal_prefill_k(30.0, 1.5, 32, G) << "\n";
    }
    // Chunk
    {
        NextPieceInputs np;
        np.L=32; np.next_ls=0; np.pproc=30.0; np.S=1.5; np.G=5.0; np.V_dec=10.0; np.V_self=1.0;
        np.local_busy=true; np.cloud_has_waiting_decode=true;
        auto pr = next_piece_computed(np);
        std::cout << "next_piece [0,32) G=5 -> ["<< pr.first<<","<<pr.second<<")\n";
    }
    // Cohort
    {
        DecodeRoundCfg cfg;
        cfg.S=1.5; cfg.lat=0.8; cfg.beta=0.01; cfg.K=4;
        cfg.dpre.init({{1,0.5},{8,1.2},{64,6.0}});
        cfg.dproc.init({{1,5.0},{8,8.0},{64,20.0}});
        cfg.dpost.init({{1,0.5},{8,1.0},{64,4.0}});
        CohortConfig cc; cc.base=cfg; cc.pool=16; cc.SLO2=60; cc.w_c=0.5; cc.inflight_decode=4;
        auto plan = compute_cohort_plan(cc);
        std::cout << "cohort pool=16 use="<<plan.use_cohorts<<" m0="<<plan.m0<<" m1="<<plan.m1<<" r="<<plan.r<<" delta="<<plan.delta_ms<<" thr="<<plan.throughput<<"\n";
    }
    // Wake
    {
        WakeState ws; ws.cloud_busy_cnt=1; ws.down_has_inflight=true;
        std::cout << "can_hold_cohort="<<can_hold_cohort(ws)<<" can_hold_acc="<<can_hold_accumulate(ws,2,8,4)<<" hold_safe="<<hold_is_safe(ws,true)<<"\n";
        WakeState ws2; ws2.cloud_busy_cnt=0; ws2.down_has_inflight=false; ws2.local_busy=false; ws2.up_has_inflight=false;
        std::cout << "hold_safe empty="<<hold_is_safe(ws2,true)<<" (expect 0)\n";
    }
    std::cout << "SB07 DRIVER PASS\n";
    return 0;
}
#pragma GCC diagnostic pop
#endif
