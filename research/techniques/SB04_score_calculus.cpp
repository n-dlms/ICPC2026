// SB04 — runtime score calculus: exact marginals of the verified score function.
// Fragment (header-style, namespace sb04) + verification driver under SB04_DRIVER.
// Derivations, audit table, constants mapping and size ledger:
//   research/techniques/SB04_score_calculus.md
// Finite-difference proof (>=1000 randomized states, <1e-9 in-region):
//   research/techniques/SB04_check.py
//
// Score (problem.md:425-495, verified):
//   Score = 1000*(w_tp*clamp(tp; tp_base, tp_UB) + w_c*wait)
//   tp = N/E, E = last final-token time - earliest ARR
//   tdr = (1/R) * sum_i (ppost_i - arr_i)
//   tpot = sum_i (e_last_i - e_first_i) / G,  G = sum_i (Lout_i - 1)  (tpot=0 if G=0)
//   dist = sqrt(excess_tdr^2 + excess_tpot^2), excess_x = max(0,(x-SLO)/SLO)
//   wait = dist_base>0 ? max(0, 1 - dist/dist_base) : (dist==0 ? 1 : 0)
//
// All functions are pure in (SCfg, SAgg). SAgg carries PROJECTED FINAL aggregates;
// mid-test the caller supplies estimates (N_hat, E_hat, R_hat, G_hat) — the formula
// shapes are exact, the projection quality is the caller's.

#ifndef SB04_SCORE_CALCULUS_HPP
#define SB04_SCORE_CALCULUS_HPP

#include <algorithm>
#include <cmath>
#include <vector>

namespace sb04 {

struct SCfg {                      // the scoring input line
    double w_tp = 0.0, w_c = 1.0;  // w_tp + w_c = 1 (either may be 0)
    double tp_base = 0.0, tp_UB = 1.0;  // tp_UB > tp_base guaranteed
    double SLO1 = 1.0, SLO2 = 1.0;      // > 0 guaranteed
    double dist_base = 0.0;             // >= 0
};

struct SAgg {                      // running aggregates projected to finals
    double E = 1.0;                // final elapsed (last final token - earliest ARR)
    double N = 1.0;                // final total tokens = sum Lout
    double R = 1.0;                // final request count
    double tdr = 0.0;              // final pooled mean tdr
    double tpot = 0.0;             // final pooled mean gap (0 iff G == 0)
    double G = 0.0;                // final gap count = sum (Lout_i - 1)
};

struct ScoreCalc {
    SCfg c;
    SAgg a;

    // ---------- exact score components ----------
    double tp() const { return a.E > 0.0 ? a.N / a.E : 0.0; }
    double tpCmp() const {
        if (c.tp_UB <= c.tp_base) return 0.0;
        return std::clamp((tp() - c.tp_base) / (c.tp_UB - c.tp_base), 0.0, 1.0);
    }
    double e1() const { return std::max(0.0, (a.tdr - c.SLO1) / c.SLO1); }
    double e2() const {
        return a.G > 0.0 ? std::max(0.0, (a.tpot - c.SLO2) / c.SLO2) : 0.0;
    }
    double dist() const { return std::sqrt(e1() * e1() + e2() * e2()); }
    double wait() const {
        if (c.dist_base <= 0.0) return dist() == 0.0 ? 1.0 : 0.0;
        return std::max(0.0, 1.0 - dist() / c.dist_base);
    }
    double score() const { return 1000.0 * (c.w_tp * tpCmp() + c.w_c * wait()); }

    // ---------- V_E: points per ms of LAST-final-token reduction ----------
    // Linear region tp_base <= tp < tp_UB, taken in the INCREASE direction
    // (decisions reduce E, i.e. raise tp): at tp == tp_base any reduction enters
    // the ramp (full slope), at/above tp_UB the clamp pays nothing more.
    double V_E() const {
        if (c.w_tp <= 0.0 || a.E <= 0.0 || c.tp_UB <= c.tp_base) return 0.0;
        const double x = tp();
        if (!(x >= c.tp_base && x < c.tp_UB)) return 0.0;
        return 1000.0 * c.w_tp * a.N / ((c.tp_UB - c.tp_base) * a.E * a.E);
    }

    // ---------- V_P: tdr channel ----------
    // Points per ms of POOLED-MEAN tdr reduction (interior of the wait ramp:
    // w_c>0, dist_base>0, excess_tdr>0, dist<=dist_base; equality included for
    // the recovery direction — one-sided at the cliff edge dist==dist_base).
    double V_P_mean() const {
        if (c.w_c <= 0.0 || c.dist_base <= 0.0) return 0.0;
        const double e = e1(), d = dist();
        if (e <= 0.0 || d <= 0.0 || d > c.dist_base) return 0.0;
        return 1000.0 * c.w_c * e / (c.dist_base * d * c.SLO1);
    }
    // Points per ms of ONE request's P POST reduction (the pooled mean moves by
    // 1/R per request-ms; R = FINAL request count, not completions so far).
    double V_P() const { return a.R > 0.0 ? V_P_mean() / a.R : 0.0; }
    // One-sided cost of DELAYING one request's P POST by 1 ms. dist==0 is a
    // REGION (tdr<=SLO1 AND tpot<=SLO2), not a point: inside it the
    // infinitesimal cost is 0 (slack absorbs), EXCEPT on the knife edge
    // tdr==SLO1 (with tpot<=SLO2) where any increase bleeds at the full rate
    // 1000*w_c/(dist_base*SLO1*R). Finite moves out of the slack are priced
    // exactly by delayCostPpost below — that is the form decisions need.
    double V_P_up() const {
        if (c.w_c <= 0.0 || c.dist_base <= 0.0 || a.R <= 0.0) return 0.0;
        const double e = e1(), d = dist();
        if (e > 0.0) return d < c.dist_base ? V_P() : 0.0;
        if (d > 0.0) return 0.0;   // e2 carries dist; the tdr axis is flat
        return a.tdr >= c.SLO1
             ? 1000.0 * c.w_c / (c.dist_base * c.SLO1 * a.R) : 0.0;
    }

    // ---------- V_S: tpot channel ----------
    double V_S_mean() const {   // per ms of pooled-mean tpot reduction
        if (c.w_c <= 0.0 || c.dist_base <= 0.0 || a.G <= 0.0) return 0.0;
        const double e = e2(), d = dist();
        if (e <= 0.0 || d <= 0.0 || d > c.dist_base) return 0.0;
        return 1000.0 * c.w_c * e / (c.dist_base * d * c.SLO2);
    }
    // Points per ms of ONE request's SPAN (e_last - e_first) reduction.
    // d(tpot)/d(span_i) = 1/G for every i (the (Lout_i-1)/G weight appears only
    // when pricing a per-gap stretch of ALL of i's remaining gaps).
    double V_S() const { return a.G > 0.0 ? V_S_mean() / a.G : 0.0; }
    double V_S_up() const {     // one-sided cost of a 1-ms span increase
        if (c.w_c <= 0.0 || c.dist_base <= 0.0 || a.G <= 0.0) return 0.0;
        const double e = e2(), d = dist();
        if (e > 0.0) return d < c.dist_base ? V_S() : 0.0;
        if (d > 0.0) return 0.0;   // e1 carries dist; the tpot axis is flat
        return a.tpot >= c.SLO2
             ? 1000.0 * c.w_c / (c.dist_base * c.SLO2 * a.G) : 0.0;
    }

    // ---------- exchange rates ----------
    // d(dist)/d(mean tdr): interior e/(dist*SLO); 0 when e1==0 and e2>0 (the
    // norm gradient points along the live axis only); 1/SLO at the corner
    // dist==0 (directional). Same structure for tpot.
    double dDist_dTdr() const {
        const double e = e1(), d = dist();
        if (d > 0.0) return e > 0.0 ? e / (d * c.SLO1) : 0.0;
        return 1.0 / c.SLO1;
    }
    double dDist_dTpot() const {
        if (a.G <= 0.0) return 0.0;
        const double e = e2(), d = dist();
        if (d > 0.0) return e > 0.0 ? e / (d * c.SLO2) : 0.0;
        return 1.0 / c.SLO2;
    }
    // Along the iso-dist circle: delta tpot (ms) that keeps dist constant under
    // a delta tdr (ms): dtpot = -isoTpotPerTdr * dtdr. Valid when e1,e2>0.
    double isoTpotPerTdr() const {
        if (e2() <= 0.0) return 1e300;
        return (e1() * c.SLO2) / (e2() * c.SLO1);
    }
    // Sacrifice rate: how many single-request ppost-ms equal 1 ms of global E.
    double V_P_over_V_E() const {
        if (c.w_tp <= 0.0) return 0.0;
        return V_P() / V_E();
    }
    // tdr-ms vs span-ms exchange per single request.
    double V_P_over_V_S() const {
        const double e1v = e1(), e2v = e2();
        if (e2v <= 0.0 || a.R <= 0.0) return 0.0;
        return (e1v * c.SLO2 * a.G) / (e2v * c.SLO1 * a.R);
    }

    // ---------- composite action marginal ----------
    // An action that simultaneously reduces E by dE, ONE ppost by dP and ONE
    // span by dS (all >= 0 = improvements) is worth, to first order:
    double gain(double dE, double dP, double dS) const {
        return V_E() * dE + V_P() * dP + V_S() * dS;
    }
    // Same for a pure delay action (dE/dP/dS >= 0 of added time):
    double delayCost(double dE, double dP, double dS) const {
        return V_E() * dE + V_P_up() * dP + V_S_up() * dS;
    }
    // EXACT FINITE forms (no linearization): delaying n requests' P POSTs by
    // delta each moves the pooled tdr mean by exactly n*delta/R — the score
    // difference is exact in every region, kinks included (this is the object
    // hold/admission decisions should use; it prices crossing SLO1 out of the
    // slack, which the infinitesimal rates cannot see).
    double delayCostPpost(int n, double delta) const {
        if (c.w_c <= 0.0 || c.dist_base <= 0.0 || a.R <= 0.0 || n <= 0 || delta <= 0.0)
            return 0.0;
        const double t2 = a.tdr + static_cast<double>(n) * delta / a.R;
        const double x = std::max(0.0, (t2 - c.SLO1) / c.SLO1);
        const double d2 = std::sqrt(x * x + e2() * e2());
        const double w1 = std::max(0.0, 1.0 - d2 / c.dist_base);
        return 1000.0 * c.w_c * (wait() - w1);
    }
    // Growing n requests' spans by delta each moves pooled tpot by n*delta/G.
    double delayCostSpan(int n, double delta) const {
        if (c.w_c <= 0.0 || c.dist_base <= 0.0 || a.G <= 0.0 || n <= 0 || delta <= 0.0)
            return 0.0;
        const double t2 = a.tpot + static_cast<double>(n) * delta / a.G;
        const double y = std::max(0.0, (t2 - c.SLO2) / c.SLO2);
        const double d2 = std::sqrt(e1() * e1() + y * y);
        const double w1 = std::max(0.0, 1.0 - d2 / c.dist_base);
        return 1000.0 * c.w_c * (wait() - w1);
    }
    // Interior tokens carry exactly 0: moving token j (1<j<Lout_i, not the
    // global last token) changes gap_{j-1} by +D and gap_j by -D (span sum
    // telescopes), touches neither tdr nor N nor T_last. No function needed.

    // ---------- cliff (dist_base == 0): jumps, not gradients ----------
    // Reducing ONE ppost by d flips wait 0->1 iff it drives the pooled tdr mean
    // to <= SLO1 while tpot is already <= SLO2 (dist must hit exactly 0).
    double cliffPpost(double d) const {
        if (c.dist_base > 0.0 || c.w_c <= 0.0 || d <= 0.0 || a.R <= 0.0) return 0.0;
        if (a.tdr > c.SLO1 && a.tdr - d / a.R <= c.SLO1 && a.tpot <= c.SLO2)
            return 1000.0 * c.w_c;
        return 0.0;
    }
    // Reducing ONE request's span by d flips wait 0->1 iff it drives the pooled
    // tpot mean to <= SLO2 while tdr is already <= SLO1.
    double cliffSpan(double d) const {
        if (c.dist_base > 0.0 || c.w_c <= 0.0 || d <= 0.0 || a.G <= 0.0) return 0.0;
        if (a.tpot > c.SLO2 && a.tpot - d / a.G <= c.SLO2 && a.tdr <= c.SLO1)
            return 1000.0 * c.w_c;
        return 0.0;
    }

    // ---------- per-request coefficients (unknown-Lout forms) ----------
    // Coefficient of request i's NEXT-token delay in pooled tpot:
    //   iter >= 1: exactly 1/G (a pool member always has another token coming,
    //              so the delay opens one new gap or moves e_last — 1 ms either
    //              way);
    //   iter == 0: the delayed token is e_first; span changes by -delay (gain!)
    //              but only if Lout_i >= 2 (else no gaps); posterior-expected
    //              coefficient p_ge2 * 1/G with p_ge2 = P(Lout>=2 | bucket).
    double nextGapCoef(int iter, double p_ge2) const {
        if (a.G <= 0.0) return 0.0;
        return (iter >= 1 ? 1.0 : p_ge2) / a.G;
    }
    // Coefficient of stretching EVERY remaining gap of request i by 1 ms
    // (e.g. a slower round cadence for the rest of its decode): the classic
    // (Lout_i - 1)/G weight, posterior-expected over the hidden Lout.
    double stretchCoef(int iter, double lout_hat) const {
        if (a.G <= 0.0) return 0.0;
        return std::max(0.0, lout_hat - iter) / a.G;
    }
    // First-token parking gain rate (dead zone L4): delaying e_first of i by
    // 1 ms gains V_S * 1 ms worth of span compression (expected over Lout>=2).
    double parkCoef(int iter, double p_ge2) const {
        if (iter != 0 || a.G <= 0.0) return 0.0;
        return p_ge2 / a.G;
    }

    // ---------- critical request (max-coupling of E) ----------
    // E = max_i F_i. Advancing the projected-last request i* by dAdv lowers E
    // by min(dAdv, F_star - F_2nd) (F_2nd = max over the others); advancing any
    // other request lowers E by 0. i* is re-projected each frame:
    //   F_i = now + remaining chain (solo prefill rem + UP/DOWN tails + queue)
    //       + (Lout_hat_i - iter_i) * P_gap(pool)      [decode-phase members]
    static double dE_effect(double dAdv, bool is_star, double f_star, double f_2nd) {
        if (!is_star || dAdv <= 0.0) return 0.0;
        return std::min(dAdv, std::max(0.0, f_star - f_2nd));
    }

    // ---------- constants -> formulas (C-map; see doc section 5) ----------
    // C2  pick_local P_POST-vs-P_PRE arbitration (replaces mv_tdr>0 && a_i>0):
    //     exact finite difference. kappa in [0,1] = P(the prefill's S+q_j chain
    //     delay reaches its own ppost): 0 when the target cloud/link is idle
    //     enough to absorb it, 1 when fully backlogged.
    bool runPpostFirst(double a_i, double q_j, double S, double kappa) const {
        return V_P_up() * ((a_i + S) - (S + q_j) * kappa) > 0.0;
    }
    // C3/C10 admission under dist risk (replaces 0.5*dist_base headroom and the
    // K-scaled inflight bases): admit iff the wait-side loss of the projected
    // dist growth stays under the E-side gain (shadow price form; loss is 0 on
    // the dist_base==0 cliff unless the jump conditions of cliffPpost fire).
    double waitLoss(double d_dist) const {
        if (c.dist_base <= 0.0) return 0.0;
        const double w0 = wait();
        const double d1 = dist() + d_dist;
        const double w1 = std::max(0.0, 1.0 - d1 / c.dist_base);
        return 1000.0 * c.w_c * (w0 - w1);
    }
    bool admitInflight(double d_dist, double d_e_gain_ms) const {
        return waitLoss(d_dist) <= V_E() * d_e_gain_ms;
    }
    // C4  exact "tpot structurally void" detector (replaces ewma_lout < 2.5):
    //     G_final == 0 <=> every request has Lout == 1 <=> tpot == 0 forever.
    bool noGapsEver() const { return a.G <= 0.0; }
    // C5  tdr slack in request-ms (replaces the SLO1*0.8 round-period cap):
    //     the total P POST delay the pool may still absorb inside SLO1.
    double tdrBudgetMs() const { return std::max(0.0, c.SLO1 - a.tdr) * a.R; }
    // C7  straggler isolation (replaces Lin >= 256): isolate a big prefill iff
    //     the co-located ppost damage exceeds the isolation's own E/star cost.
    //     phi in [0,1] = fraction of pproc(lin) that would actually block the
    //     co-located actives (cloud-busy overlap).
    bool isolateStraggler(double pproc_lin, int co_active, double phi,
                          double own_star_cost_ms) const {
        return V_P_up() * pproc_lin * static_cast<double>(co_active) * phi
             > V_E() * own_star_cost_ms + V_P_up() * own_star_cost_ms;
    }
    // C9  near-tie band (replaces the fixed 1.05 PoTC band): the EFT estimate
    //     noise is dproc*sigma(lout_hat); ties within that band carry no signal.
    double potcBand(double dproc_star, double sigma_lout) const {
        return dproc_star * sigma_lout;
    }
};

// ---------- piecewise-linear helpers (table-shaped inputs) ----------
struct PLTab {   // one decode step's duration table (sorted rows, m -> ms)
    std::vector<std::pair<int, double> > rows;
    double at(int m) const {
        if (rows.empty()) return 0.0;
        if (m <= rows.front().first) return rows.front().second;
        if (m >= rows.back().first) return rows.back().second;
        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            if (m >= rows[i].first && m <= rows[i + 1].first) {
                if (rows[i + 1].first == rows[i].first) return rows[i].second;
                const double f = static_cast<double>(m - rows[i].first) /
                                 static_cast<double>(rows[i + 1].first - rows[i].first);
                return rows[i].second + f * (rows[i + 1].second - rows[i].second);
            }
        }
        return rows.back().second;
    }
};

// ---------- three-regime batch law ----------
// Per-member waiting cost in the thin regime with fill wait q/(2*lam) and
// amortized round overhead f(q)/q (Deb-Serfozo threshold structure; the
// threshold serves when pool < q* and all resources hold better work).
// On a piecewise-linear table f(m) = c0 + c1*m per segment, the EOQ law holds
// PER SEGMENT: d/dq [q/(2*lam) + c0/q + c1] = 0  =>  q = sqrt(2*lam*c0).
struct SegPL { double m1, m2, c0, c1; };   // f(m) = c0 + c1*m on [m1, m2]

inline double segQ(const SegPL& s, double lam) {
    if (s.c0 <= 0.0) return s.m1;
    return std::clamp(std::sqrt(2.0 * lam * s.c0), s.m1, s.m2);
}
inline double segW(const SegPL& s, double lam, double q) {
    return q / (2.0 * lam) + (s.c0 + s.c1 * q) / q;
}
// Continuous thin-pool argmin over all segments (exact for piecewise-linear f).
inline double qThin(const std::vector<SegPL>& segs, double lam) {
    double best = 1.0, bw = 1e300;
    for (size_t i = 0; i < segs.size(); ++i) {
        const double q = segQ(segs[i], lam), w = segW(segs[i], lam, q);
        if (w < bw) { bw = w; best = q; }
    }
    return best;
}
inline double fOf(const std::vector<SegPL>& segs, double q) {
    for (size_t i = 0; i < segs.size(); ++i)
        if (q >= segs[i].m1 && q <= segs[i].m2)
            return segs[i].c0 + segs[i].c1 * q;
    return segs.empty() ? 0.0 : segs.back().c0 + segs.back().c1 * segs.back().m2;
}
// Critical load: per-member capacity q/f(q) must cover lam:
//   lam*(f0 + a*q)/q <= 1  =>  q >= lam*f0/(1 - lam*a)   (needs lam*a < 1).
inline double qCrit(double lam, double f0, double a_slope) {
    if (lam * a_slope >= 1.0) return 1e300;
    return lam * f0 / (1.0 - lam * a_slope);
}

// ---------- round-period physics ----------
// P_pipe: resource-bottleneck period (pipelined rounds) — the THROUGHPUT/E law.
// P_chain: same-cohort round trip — the floor on a member's inter-token gap.
// P_gap: expected member gap = max(chain, ceil(pool/m) * pipe): rounds for a
//    member to reappear times the bottleneck cadence, never below its own chain.
//    THIS is the quantity the SLO2/tpot cap binds on (not P_pipe alone).
struct RoundCost {
    double S = 0.0, lat = 0.0, bper = 0.0;   // bper = 8*bytes_per_token/(bw*1e6)
    int K = 1;
    PLTab dpre, dproc, dpost;
    double xup(int m) const {   // one round's UP leg: K FIFO entries (per cloud)
        return static_cast<double>(K) * lat + bper * static_cast<double>(m);
    }
    double xdown(int m) const { return lat + bper * static_cast<double>(m); }
    double Ppipe(int m) const {
        const int km = std::max(1, (m + K - 1) / K);
        return std::max(std::max(2.0 * S + dpre.at(m) + dpost.at(m),
                                 S + dproc.at(km)),
                        std::max(xup(m), xdown(m)));
    }
    double Pchain(int m) const {
        const int km = std::max(1, (m + K - 1) / K);
        return 3.0 * S + dpre.at(m) + dproc.at(km) + dpost.at(m)
             + xup(m) + xdown(m);
    }
    double Pgap(int m, int pool) const {
        const double rot = std::ceil(static_cast<double>(std::max(1, pool)) /
                                     static_cast<double>(std::max(1, m)));
        return std::max(Pchain(m), rot * Ppipe(m));
    }
    // m_tp = largest m with P_gap(m,pool) <= SLO2 (tpot feasibility, per-gap
    // sufficient condition). P_gap is piecewise-linear but NOT monotone on
    // V-shaped tables, and the max feasible m is a threshold CROSSING between
    // breakpoints — so the exact answer needs the full integer scan (<= 4096
    // lookups; run once at startup and cache, like MarginalBatch::precompute).
    // (A per-segment convex refinement — max of linears is convex between
    // breakpoints; binary-search the right crossing in each feasible segment
    // — is the O(rows log) production form if a per-frame call is ever needed.)
    int mTP(double SLO2, int pool, int mmax) const {
        const int hi = std::max(1, std::min(mmax, pool));
        int best = 0;
        for (int m = 1; m <= hi; ++m)
            if (Pgap(m, pool) <= SLO2 && m > best) best = m;
        return best;
    }
    // C10 in-flight window (replaces max(2,K)/100/(2+2K)/(/2+1)): UP direction
    // saturates at A* in-flight tokens: A* = (P_round - K*lat)/bper.
    double inflightAstar(double p_round) const {
        if (bper <= 0.0) return 1e300;
        return std::max(1.0, (p_round - static_cast<double>(K) * lat) / bper);
    }
};

// ---------- regime detector + final batch size ----------
// rho(q) = lam*f(q)/q is the offered load at batch q.
//   thin      : rho(qThin) < 1        -> q = clamp(qThin, 1, pool)
//   critical  : rho(qThin) >= 1, lam*a < 1 -> q = clamp(max(qThin, qCrit), 1, pool)
//   saturated : lam*a >= 1 (unstable alone) or pool cap binds -> q = min(pool, m_tp)
struct BatchLaw {
    int regime = 0;        // 0 thin, 1 critical, 2 saturated
    double q = 1.0;        // chosen batch size (>=1, <=pool)
};

inline BatchLaw qBatch(double lam, const std::vector<SegPL>& segs, double f0,
                       double a_slope, int pool, int m_tp) {
    BatchLaw bl;
    const double qt = qThin(segs, lam);
    const double rho = lam * fOf(segs, qt) / qt;
    const int pc = std::max(1, pool);
    if (rho < 1.0) {
        bl.regime = 0;
        bl.q = std::clamp(qt, 1.0, static_cast<double>(pc));
    } else if (lam * a_slope < 1.0) {
        bl.regime = 1;
        bl.q = std::clamp(std::max(qt, qCrit(lam, f0, a_slope)),
                          1.0, static_cast<double>(pc));
    } else {
        bl.regime = 2;
        bl.q = static_cast<double>(m_tp > 0 ? std::min(pc, m_tp) : pc);
    }
    return bl;
}

}  // namespace sb04

#endif  // SB04_SCORE_CALCULUS_HPP

// ============================ verification driver ============================
#ifdef SB04_DRIVER
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        char mode = 'A';
        ss >> mode;
        if (mode == 'A') {
            sb04::SCfg cg; sb04::SAgg ag; double d;
            int ndly = 0;
            double ddly = 0.0;
            ss >> cg.w_tp >> cg.w_c >> cg.tp_base >> cg.tp_UB >> cg.SLO1
               >> cg.SLO2 >> cg.dist_base
               >> ag.E >> ag.N >> ag.R >> ag.tdr >> ag.tpot >> ag.G >> d
               >> ndly >> ddly;
            sb04::ScoreCalc sc; sc.c = cg; sc.a = ag;
            std::printf("%.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g "
                        "%.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g "
                        "%.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                sc.tp(), sc.tpCmp(), sc.e1(), sc.e2(), sc.dist(), sc.wait(),
                sc.score(), sc.V_E(), sc.V_P_mean(), sc.V_P(), sc.V_P_up(),
                sc.V_S_mean(), sc.V_S(), sc.V_S_up(), sc.dDist_dTdr(),
                sc.dDist_dTpot(), sc.isoTpotPerTdr(), sc.V_P_over_V_E(),
                sc.V_P_over_V_S(), sc.cliffPpost(d), sc.cliffSpan(d),
                sc.delayCostPpost(ndly, ddly), sc.delayCostSpan(ndly, ddly));
        } else if (mode == 'C') {
            double dAdv, fStar, f2;
            int isStar;
            ss >> dAdv >> isStar >> fStar >> f2;
            std::printf("%.17g\n",
                sb04::ScoreCalc::dE_effect(dAdv, isStar != 0, fStar, f2));
        } else if (mode == 'D') {
            sb04::SAgg ag;
            int iter;
            double pGe2, loutHat;
            ss >> ag.G >> iter >> pGe2 >> loutHat;
            sb04::ScoreCalc sc; sc.a = ag;
            std::printf("%.17g %.17g %.17g\n", sc.nextGapCoef(iter, pGe2),
                        sc.stretchCoef(iter, loutHat), sc.parkCoef(iter, pGe2));
        } else if (mode == 'B') {
            double lam, f0, as, SLO2, S, lat, bper;
            int pool, mmax, K, n;
            ss >> lam >> f0 >> as >> pool >> mmax >> SLO2 >> S >> lat >> bper
               >> K;
            sb04::RoundCost rc;
            rc.S = S; rc.lat = lat; rc.bper = bper; rc.K = K;
            for (int t = 0; t < 3; ++t) {
                if (!(ss >> n)) n = 0;
                for (int i = 0; i < n; ++i) {
                    int m; double dur;
                    ss >> m >> dur;
                    if (t == 0) rc.dpre.rows.push_back(std::make_pair(m, dur));
                    else if (t == 1) rc.dproc.rows.push_back(std::make_pair(m, dur));
                    else rc.dpost.rows.push_back(std::make_pair(m, dur));
                }
            }
            // segments from the chain table f(m) = 3S + dpre(m) + dproc(ceil(m/K))
            // + dpost(m) + xup + xdown evaluated on 1..mmax breakpoints:
            std::vector<sb04::SegPL> segs;
            {
                std::vector<std::pair<int, double> > pts;
                for (int m = 1; m <= mmax; ++m)
                    pts.push_back(std::make_pair(m, rc.Pchain(m)));
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    if (pts[i + 1].first == pts[i].first) continue;
                    const double c1 = (pts[i + 1].second - pts[i].second) /
                                      static_cast<double>(pts[i + 1].first - pts[i].first);
                    const double c0 = pts[i].second - c1 * pts[i].first;
                    sb04::SegPL s;
                    s.m1 = static_cast<double>(pts[i].first);
                    s.m2 = static_cast<double>(pts[i + 1].first);
                    s.c0 = c0; s.c1 = c1;
                    segs.push_back(s);
                }
            }
            const int mtp = rc.mTP(SLO2, pool, mmax);
            const sb04::BatchLaw bl = sb04::qBatch(lam, segs, f0, as, pool, mtp);
            const int qi = static_cast<int>(bl.q + 0.5);
            std::printf("%.17g %.17g %d %.17g %d %.17g %.17g %.17g %.17g\n",
                        sb04::qThin(segs, lam), sb04::qCrit(lam, f0, as),
                        bl.regime, bl.q, mtp, rc.Ppipe(qi), rc.Pchain(qi),
                        rc.Pgap(qi, pool), rc.inflightAstar(rc.Pgap(qi, pool)));
        }
    }
    return 0;
}
#endif  // SB04_DRIVER
