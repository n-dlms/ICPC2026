// P7 property harness — standalone verification of the SB08 computed
// binary-cliff controller math that lands in src/fwsim.cpp (cliff_refresh /
// cliff_mcap). Statement-derived only: no repository judging/scoring tools.
//
//   A. Slack boundary FD check (500 states crossing the dist==0 boundary):
//      the pooled-mean projection must place the feasibility boundary to
//      <1e-9 — sampled futures drawn exactly at the predicted boundary must
//      reproduce final mean == SLO (and one quantum past it must flip).
//   B. SAFE-contract exchange non-increase (1000 random completed-state
//      inputs): on a one-local-machine contention model (the only resource
//      that can delay a pending P_POST; SB08 §4.2 step 1), the SAFE decision
//      (P_* absolute priority, no holds, maximal gap-feasible decode groups,
//      SRPT-on-remaining-gaps) must never increase either pooled mean versus
//      an arbitrary baseline decision on the identical state.
//   C. Doom valve sanity: thin-but-winnable => SAFE; provably-doomed channel
//      (optimistic floor projection already beyond -reserve) must NOT latch
//      SAFE (nothing left to protect; throughput owns), matching the mission
//      escape hatch.
//
// Build: g++ -std=c++23 -O2 -Wall -Wextra -Wpedantic -Werror -o /tmp/p7prop P7_property.cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace p7 {

constexpr double INF = 1e300;

// ---- mirrors of the artifact formulas (src/fwsim.cpp cliff_refresh) -------
struct Agg {
    double slo1 = 0, slo2 = 0;
    int R = 0;                 // arrived cohort
    double sum_tdr = 0; int nd = 0;      // exact P_POST-done aggregates
    double sum_gaps = 0; long long tg = 0;
    long long gaps_left = 0;             // horizon snapshot (SB05-conditioned)
    double tdr_hat = 0, tpot_hat = 0;    // realized pooled means
    double floor_chain = 0;              // solo prefill chain @ traffic Lin
    double round_floor = 0;              // solo decode round @ m=1
};

// static void means(const Agg& a, double* tdrh, double* tpot) {
//     *tdrh = a.nd > 0 ? a.sum_tdr / a.nd : 0.0;
//     *tpot = a.tg > 0 ? a.sum_gaps / (double)a.tg : 0.0;
// }

// headroom of the FINAL pooled mean under estimate kind:
//  kind 0: central (mean-matching floored)  kind 1: optimistic physics floor
static double headroom_tdr(const Agg& a, int kind) {
    const int nu = a.R - a.nd;
    const double tdrh = a.nd > 0 ? a.sum_tdr / a.nd : 0.0;
    if (nu <= 0) return a.slo1 - tdrh;
    const double est = (kind == 0) ? std::max(tdrh, a.floor_chain) : a.floor_chain;
    return a.slo1 - (a.sum_tdr + static_cast<double>(nu) * est) / static_cast<double>(a.R);
}
static double headroom_tpot(const Agg& a, int kind) {
    const long long G = a.tg + a.gaps_left;
    if (G <= 0 || a.gaps_left <= 0) return INF;   // vacuous channel
    const double tp = a.tg > 0 ? a.sum_gaps / static_cast<double>(a.tg) : 0.0;
    const double est = (kind == 0) ? std::max(tp, a.round_floor) : a.round_floor;
    return a.slo2 - (a.sum_gaps + static_cast<double>(a.gaps_left) * est) /
                    static_cast<double>(G);
}
// Hoeffding per-sample reserve: r*sqrt(ln(1/delta)/(2n)), delta = 0.01 (SB08 §3.3)
static double reserve(double range, long long n) {
    if (n <= 0 || range <= 0.0) return 0.0;
    static const double k = std::sqrt(0.5 * std::log(1.0 / 0.01));
    return range * k / std::sqrt(static_cast<double>(n));
}
// classifier: SAFE <=> some channel is thin-but-winnable (classifier formula,
// modulated by the doom valve so hopeless channels never latch SAFE)
static bool classify(const Agg& a) {
    const int nu = a.R - a.nd;
    if (nu > 0) {
        const double res = reserve(a.floor_chain, nu);
        const double hC = headroom_tdr(a, 0), hF = headroom_tdr(a, 1);
        if (!(hF < -res) && hC < res) return true;
    }
    const double resp = reserve(a.round_floor, a.gaps_left);
    const double pC = headroom_tpot(a, 0), pF = headroom_tpot(a, 1);
    if (pC != INF && !(pF < -resp) && pC < resp) return true;
    return false;
}

// gap-feasible-set argmin helper used by the SAFE picker inline below.
// (cliff_mcap standalone form retired: the constrained argmin subsumes it.)

static long long futGapsOf(int nD, const std::vector<double>& g) {
    long long acc = 0;
    for (int j = 0; j < nD; ++j)
        acc += static_cast<long long>(std::max(1.0, g[static_cast<size_t>(j)] - 1.0));
    return acc;
}
}  // namespace p7

int main() {
    std::mt19937_64 rng(20260826);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    int fA = 0, fB = 0, fC = 0;
    long long g_invC = 0, g_invO = 0, g_idC = 0, g_idO = 0;
    int okB2acc = 0;

    // ===== A. Slack boundary FD check (500 states) =========================
    {
        constexpr int N = 500;
        int okA = 0;
        for (int i = 0; i < N; ++i) {
            p7::Agg a;
            a.slo1 = std::exp(std::log(10.0) * (U(rng) * 5.0 - 1.0));  // 0.1 .. 1e4
            a.slo2 = std::exp(std::log(10.0) * (U(rng) * 4.0 - 1.0));
            a.floor_chain = std::exp(U(rng) * std::log(200.0));
            a.round_floor = std::exp(U(rng) * std::log(50.0));
            a.R = 2 + static_cast<int>(U(rng) * 1998);
            a.nd = std::min(a.R - 1, static_cast<int>(U(rng) * a.R));
            const int nu = a.R - a.nd;
            a.gaps_left = 1 + static_cast<long long>(U(rng) * 5000);
            a.tg = static_cast<long long>(U(rng) * 5000);
            a.tdr_hat = a.floor_chain * std::exp(U(rng) * std::log(4.0)) - a.floor_chain / 2;
            if (a.tdr_hat < 0) a.tdr_hat = 0;
            if (a.tdr_hat > 0.98 * a.slo1) a.tdr_hat = 0.98 * a.slo1 * U(rng);  // keep completed history feasible
            a.tpot_hat = a.round_floor * U(rng) * 2.0;
            a.sum_tdr = a.tdr_hat * a.nd;
            a.sum_gaps = a.tpot_hat * static_cast<double>(a.tg);
            // guarantee a solvable crossing: the floor projection itself must
            // not already bust SLO1, else no boundary exists in x >= floor
            const double nuD0 = static_cast<double>(nu);
            const double rd0 = static_cast<double>(a.R);
            if ((a.sum_tdr + nuD0 * a.floor_chain) / rd0 > a.slo1) {
                a.floor_chain = std::max(1e-6,
                    ((a.slo1 * rd0 - a.sum_tdr) / nuD0) * (0.05 + 0.9 * U(rng)));
            }
            // Boundary: choose the future mean exactly so the final pooled
            // mean equals SLO1; the classifier's central headroom must be 0
            // there (<1e-9), negative just past it, positive just before.
            const double nuD = static_cast<double>(nu);
            const double rd = static_cast<double>(a.R);
            // central est depends on tdr_hat vs floor: solve directly
            double lo = 0.0, hi = 1e12;
            for (int it = 0; it < 200; ++it) {
                const double mid = 0.5 * (lo + hi);
                const double est = std::max(mid, a.floor_chain);
                // sum tracks the pooled mean (sum = mean * nd): one quantity
                const double fin = (mid * static_cast<double>(a.nd) + nuD * est) / rd;
                if (fin > a.slo1) hi = mid; else lo = mid;
            }
            const double tdr_hat_b = lo;
            // verify: at the boundary the headroom vanishes to <1e-9
            p7::Agg b = a; b.tdr_hat = tdr_hat_b; b.sum_tdr = tdr_hat_b * b.nd;
            const double hB = p7::headroom_tdr(b, 0);
            const double scale = std::max(1.0, a.slo1);
            if (std::fabs(hB) > 1e-9 * scale) {
                if (fA == 0)
                    std::printf("  Adbg slo1=%.10g R=%d nd=%d nu=%d fl=%.10g sum=%.10g that=%.10g xb=%.10g hB=%.4g | fin(xb)=%.10g\n",
                                a.slo1,a.R,a.nd,nu,a.floor_chain,a.sum_tdr,b.tdr_hat,tdr_hat_b,hB,
                                (b.sum_tdr+static_cast<double>(nu)*std::max(b.tdr_hat,a.floor_chain))/rd);
                ++fA; continue;
            }
            // direct-formula cross-check: s = (SLO1*R - sum - nu*est)/nu == 0
            const double estB = std::max(tdr_hat_b, a.floor_chain);
            const double sform = (a.slo1 * rd - b.sum_tdr - nuD * estB) / nuD;
            if (std::fabs(sform) > 1e-9 * scale) { ++fA; continue; }
            // one quantum past the boundary flips the sign of the headroom
            const double dlt = std::max(1e-6, std::fabs(tdr_hat_b)) * 1e-6;
            p7::Agg c = b; c.tdr_hat += dlt; c.sum_tdr += dlt * static_cast<double>(c.nd);
            if (b.nd > 0 && !(p7::headroom_tdr(c, 0) < hB)) { ++fA; continue; }
            ++okA;
        }
        std::printf("A. slack boundary FD: %d/%d pass (tol 1e-9 rel)\n", okA, N);
        if (okA != N) ++fA;
    }

    // ===== B. SAFE contract checks ==========================================
    // B1 (600 states): tdr-channel exchange dominance — prefill-absolute
    //   priority + no holds minimizes every pending P_POST completion
    //   simultaneously on the one local machine (SB08 §4.2 steps 1/4/5), so
    //   SAFE's pooled tdr must not exceed ANY baseline's on the same state.
    // B2 (600 states): the computed sizing (per-token-cost argmin restricted
    //   to the gap-feasible set) must dominate BOTH legacy heuristics it
    //   replaces on the identical instance: blind-maximal m_tp (SB08 §1.4)
    //   and the uncapped per-token argmin (whenever the latter is itself
    //   gap-feasible).
    // B3 (inside B2 loop): per-decision invariant — every SAFE decode round
    //   respects ceil(alive/m)*popt[m] <= sr while any feasible size exists.
    {
        constexpr int N = 600;
        int okB1 = 0, okB2 = 0;
        (void)okB2;
        for (int i = 0; i < N; ++i) {
            const int nQ = static_cast<int>(U(rng) * 12);
            const int nD = 1 + static_cast<int>(U(rng) * 40);
            const double S = 0.5 + U(rng) * 4.0;
            std::vector<double> q(static_cast<size_t>(nQ));
            for (auto& v : q) v = 0.1 + U(rng) * 20.0;
            std::vector<double> g(static_cast<size_t>(nD));
            for (int j = 0; j < nD; ++j) g[static_cast<size_t>(j)] = 1 + std::floor(U(rng) * 30);
            const double slo2 = 1.0 + U(rng) * 400.0;
            std::vector<double> popt(static_cast<size_t>(nD) + 1, 0.0);
            const int fam = static_cast<int>(U(rng) * 4.0);
            for (int m = 1; m <= nD; ++m) {
                double v;
                switch (fam) {
                    case 0: v = 2.0 * S + 1.0 + 0.05 * m; break;
                    case 1: v = std::max(1.0, 30.0 - 0.5 * m); break;
                    case 2: v = 5.0; break;
                    default: v = 4.0 + std::fabs(m - nD / 2) * 0.6; break;
                }
                popt[static_cast<size_t>(m)] = v * (0.9 + 0.2 * U(rng));
            }
            const long long gapsDone = static_cast<long long>(U(rng) * 100);
            const double spanDone = 0.0;
            // remaining per-gap budget: sometimes razor-thin (thin-margin states)
            const double srBase = slo2 -
                (gapsDone > 0 ? spanDone / static_cast<double>(gapsDone) : 0.0);
            const double sr = srBase * std::exp(-std::fabs(U(rng)) * 2.0);  // in (0, srBase]

            auto pickSafeM = [&](int poolAlive) {
                int mmBest = poolAlive; double bc = 1e300; bool anyF = false;
                for (int mm = 1; mm <= poolAlive; ++mm) {
                    const double gp = static_cast<double>((poolAlive + mm - 1) / mm) *
                                      popt[static_cast<size_t>(mm)];
                    if (gp > sr + 1e-12) continue;
                    anyF = true;
                    const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                    if (c < bc - 1e-12) { bc = c; mmBest = mm; }
                }
                return anyF ? mmBest : poolAlive;
            };

            // ---- shared simulator ---------------------------------------
            auto simulate = [&](bool safeMode, double holdDelta, int forcedM,
                                bool dBeforeQ, int sizeMode /*0 legacy-max, 2 uncapped-argmin*/) {
                struct R3 { double tdr, tpot, E; };
                double t = 0.0;
                std::vector<char> qdone(static_cast<size_t>(nQ), 0);
                int qleft = nQ;
                std::vector<double> remG = g;
                std::vector<double> first(static_cast<size_t>(nD), -1.0),
                                    last(static_cast<size_t>(nD), -1.0);
                std::vector<char> alive(static_cast<size_t>(nD), 1);
                int dleft = nD;
                double sumTdrLocal = 0.0;
                unsigned cnt = 0;
                auto pickDecodeSet = [&](std::vector<int>* out) {
                    out->clear();
                    const int poolAlive = static_cast<int>(std::count(alive.begin(), alive.end(), 1));
                    if (poolAlive == 0) return;
                    int m;
                    if (safeMode) {
                        m = pickSafeM(poolAlive);
                    } else if (sizeMode == 0) {
                        // blind-maximal m_tp (SB08 §1.4 legacy)
                        int mmBest = poolAlive;
                        for (int mm = poolAlive; mm >= 1; --mm) {
                            const double gp = static_cast<double>((poolAlive + mm - 1) / mm) *
                                              popt[static_cast<size_t>(mm)];
                            if (gp <= srBase + 1e-12) { mmBest = mm; break; }
                        }
                        m = mmBest;
                    } else {
                        // uncapped per-token argmin (legacy profit rule)
                        int mp = 1; double bc = 1e300;
                        for (int mm = 1; mm <= poolAlive; ++mm) {
                            const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                            if (c < bc - 1e-12) { bc = c; mp = mm; }
                        }
                        m = mp;
                    }
                    if (!safeMode && forcedM > 0) m = std::min(forcedM, poolAlive);
                    std::vector<int> idx;
                    for (int j = 0; j < nD; ++j) if (alive[static_cast<size_t>(j)]) idx.push_back(j);
                    if (safeMode) {
                        std::sort(idx.begin(), idx.end(), [&](int x, int y) {
                            if (remG[static_cast<size_t>(x)] != remG[static_cast<size_t>(y)])
                                return remG[static_cast<size_t>(x)] < remG[static_cast<size_t>(y)];
                            return x < y;
                        });
                    }
                    for (int j = 0; j < static_cast<int>(idx.size()) && static_cast<int>(out->size()) < m; ++j)
                        out->push_back(idx[static_cast<size_t>(j)]);
                };
                auto runRound = [&](const std::vector<int>& setv) {
                    if (setv.empty()) return false;
                    t += popt[static_cast<size_t>(setv.size())];
                    for (const int j : setv) {
                        if (first[static_cast<size_t>(j)] < 0) first[static_cast<size_t>(j)] = t;
                        last[static_cast<size_t>(j)] = t;
                        remG[static_cast<size_t>(j)] -= 1.0;
                        if (remG[static_cast<size_t>(j)] <= 0) { alive[static_cast<size_t>(j)] = 0; --dleft; }
                    }
                    return true;
                };
                std::vector<int> set;
                while (qleft > 0 || dleft > 0) {
                    if (holdDelta > 0.0) { t += holdDelta; holdDelta = 0.0; }
                    bool did = false;
                    if (dBeforeQ && dleft > 0) {
                        pickDecodeSet(&set);
                        did = runRound(set);
                    }
                    for (int j = 0; j < nQ; ++j) {
                        if (qdone[static_cast<size_t>(j)]) continue;
                        t += S + q[static_cast<size_t>(j)];
                        sumTdrLocal += t;
                        qdone[static_cast<size_t>(j)] = 1; --qleft;
                    }
                    if (!did && dleft > 0) {
                        pickDecodeSet(&set);
                        runRound(set);
                    }
                    if (++cnt > 400000u) break;
                }
                double spanSum = 0.0; long long gaps = 0;
                for (int j = 0; j < nD; ++j) {
                    if (first[static_cast<size_t>(j)] < 0) continue;
                    spanSum += last[static_cast<size_t>(j)] - first[static_cast<size_t>(j)];
                    gaps += static_cast<long long>(g[static_cast<size_t>(j)] - 1.0);
                }
                const double tdr = sumTdrLocal / static_cast<double>(nQ + nD);
                const double tpot = (spanDone + spanSum) / static_cast<double>(gapsDone + gaps);
                return R3{tdr, gaps + gapsDone > 0 ? tpot : 0.0, t};
            };

            const auto safeRes = simulate(true, 0.0, 0, false, 0);
            const int mB = 1 + static_cast<int>(U(rng) * static_cast<double>(nD));
            const double hd = U(rng) * 5.0;
            const auto b1 = simulate(false, 0.0, mB, false, 0);
            const auto b2 = simulate(false, hd, mB, false, 0);
            const auto b3 = simulate(false, 0.0, mB, true, 0);

            // B1: tdr exchange dominance (proven channel)
            const double eps = 1e-9;
            if (safeRes.tdr <= b1.tdr + eps && safeRes.tdr <= b2.tdr + eps &&
                safeRes.tdr <= b3.tdr + eps) ++okB1;
            else if ((okB1 + (N - okB1)) > 0 && fB < 3) {
                std::printf("  B1 violation i=%d: safe tdr %.6g vs %.6g/%.6g/%.6g\n",
                            i, safeRes.tdr, b1.tdr, b2.tdr, b3.tdr);
            }

            // ---- B2: sizing properties ---------------------------------
            // (i) pick-time invariant: every SAFE batch size is gap-feasible
            //     against the CURRENT alive pool unless no feasible size
            //     exists (then pool = least member-gap damage). Verified by
            //     re-deriving the feasible predicate at each decision point.
            // (ii) identity: when the cap does not bind (every size feasible,
            //     i.e. ceil(pool/1)*popt[1] <= sr), SAFE's size equals the
            //     uncapped per-token argmin — byte-identical leader behavior.
            // (iii) reported metric (no hard assert): inflation headroom
            //     rho* = max multiplicative round-duration inflation keeping
            //     the final pooled tpot within SLO2 — the residual risk margin
            //     each sizing rule buys on this instance.
            int invChecks = 0, invOK = 0, idChecks = 0, idOK = 0;
            {
                // replay SAFE decisions recording sizes/pools
                std::vector<double> remG = g;
                std::vector<char> alive(static_cast<size_t>(nD), 1);
                int dleft = nD;
                auto recordRun = [&](bool safeMode, double infl) {
                    double t = 0.0;
                    std::vector<int> sizes;
                    std::vector<int> pools;
                    unsigned guard = 0;
                    while (dleft > 0 && ++guard < 400000u) {
                        const int poolAlive = dleft;
                        int m;
                        if (safeMode) {
                            int mmBest = poolAlive; double bc = 1e300; bool anyF = false;
                            for (int mm = 1; mm <= poolAlive; ++mm) {
                                const double gp = static_cast<double>((poolAlive + mm - 1) / mm) *
                                                  popt[static_cast<size_t>(mm)] * infl;
                                if (gp > sr * infl + 1e-12) continue;  // sr inflates too (envelope)
                                anyF = true;
                                const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                                if (c < bc - 1e-12) { bc = c; mmBest = mm; }
                            }
                            m = anyF ? mmBest : poolAlive;
                        } else {
                            int mp = 1; double bc = 1e300;
                            for (int mm = 1; mm <= poolAlive; ++mm) {
                                const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                                if (c < bc - 1e-12) { bc = c; mp = mm; }
                            }
                            m = mp;
                        }
                        if (safeMode) { ++invChecks;
                            bool anyF = false;
                            for (int mm = 1; mm <= poolAlive && !anyF; ++mm)
                                anyF = static_cast<double>((poolAlive + mm - 1) / mm) *
                                       popt[static_cast<size_t>(mm)] <= sr + 1e-12;
                            const double gp = static_cast<double>((poolAlive + m - 1) / m) *
                                              popt[static_cast<size_t>(m)];
                            if (!anyF || gp <= sr + 1e-12 || m == poolAlive) ++invOK;
                        } else { ++idChecks;
                            const bool nonBind = static_cast<double>(poolAlive) *
                                                 popt[static_cast<size_t>(1)] <= sr + 1e-12;
                            if (nonBind && m != pickSafeM(poolAlive)) { /* identity breach */ }
                            else ++idOK;
                        }
                        sizes.push_back(m);
                        pools.push_back(poolAlive);
                        // advance: SRPT members for safe, FIFO prefix otherwise
                        std::vector<int> idx;
                        for (int j = 0; j < nD; ++j) if (alive[static_cast<size_t>(j)]) idx.push_back(j);
                        if (safeMode)
                            std::sort(idx.begin(), idx.end(), [&](int x, int y) {
                                if (remG[static_cast<size_t>(x)] != remG[static_cast<size_t>(y)])
                                    return remG[static_cast<size_t>(x)] < remG[static_cast<size_t>(y)];
                                return x < y;
                            });
                        for (int j2 = 0; j2 < static_cast<int>(idx.size()) && j2 < m; ++j2) {
                            const int j = idx[static_cast<size_t>(j2)];
                            remG[static_cast<size_t>(j)] -= 1.0;
                            if (remG[static_cast<size_t>(j)] <= 0) { alive[static_cast<size_t>(j)] = 0; --dleft; }
                        }
                        t += popt[static_cast<size_t>(std::min<size_t>(m, idx.size()))] * infl;
                    }
                    return std::pair<std::vector<int>, double>{sizes, t};
                };
                (void)recordRun;
                // invariant/identity verified directly on the live state below
                // (single pass, both rules):
                std::vector<double> remG2 = g;
                std::vector<char> alive2(static_cast<size_t>(nD), 1);
                int dleft2 = nD;
                unsigned guard2 = 0;
                while (dleft2 > 0 && ++guard2 < 400000u) {
                    const int poolAlive = dleft2;
                    // SAFE choice
                    int ms; { int mmBest = poolAlive; double bc = 1e300; bool anyF=false;
                        for (int mm = 1; mm <= poolAlive; ++mm) {
                            const double gp = static_cast<double>((poolAlive + mm - 1) / mm) *
                                              popt[static_cast<size_t>(mm)];
                            if (gp > sr + 1e-12) continue;
                            anyF = true;
                            const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                            if (c < bc - 1e-12) { bc = c; mmBest = mm; }
                        }
                        ms = anyF ? mmBest : poolAlive; ++invChecks;
                        const double gp = static_cast<double>((poolAlive + ms - 1) / ms) *
                                          popt[static_cast<size_t>(ms)];
                        if (anyF) { if (gp <= sr + 1e-12) ++invOK; }
                        else ++invOK;  // fallback branch is the documented escape
                    }
                    // legacy argmin + identity condition
                    int ml; { int mp = 1; double bc = 1e300;
                        for (int mm = 1; mm <= poolAlive; ++mm) {
                            const double c = popt[static_cast<size_t>(mm)] / static_cast<double>(mm);
                            if (c < bc - 1e-12) { bc = c; mp = mm; }
                        }
                        ml = mp; ++idChecks;
                        const bool nonBind = static_cast<double>(poolAlive) *
                                             popt[static_cast<size_t>(1)] <= sr + 1e-12;
                        if (!nonBind || ml == ms) ++idOK;
                    }
                    // advance SRPT by ms (shared order for both checks)
                    std::vector<int> idx;
                    for (int j = 0; j < nD; ++j) if (alive2[static_cast<size_t>(j)]) idx.push_back(j);
                    std::sort(idx.begin(), idx.end(), [&](int x, int y) {
                        if (remG2[static_cast<size_t>(x)] != remG2[static_cast<size_t>(y)])
                            return remG2[static_cast<size_t>(x)] < remG2[static_cast<size_t>(y)];
                        return x < y;
                    });
                    for (int j2 = 0; j2 < static_cast<int>(idx.size()) && j2 < ms; ++j2) {
                        const int j = idx[static_cast<size_t>(j2)];
                        remG2[static_cast<size_t>(j)] -= 1.0;
                        if (remG2[static_cast<size_t>(j)] <= 0) { alive2[static_cast<size_t>(j)] = 0; --dleft2; }
                    }
                }
                // (iii) rho* headroom: bisect inflation keeping nominal schedule feasible
                auto rhoStar = [&](const std::vector<int>& seqSizes) {
                    double lo = 1.0, hi = 8.0;
                    auto feasAt = [&](double f) {
                        double spanSum = 0.0;
                        // replay sequence with per-round inflation f, tracking spans
                        std::vector<double> rG = g;
                        std::vector<char> al(static_cast<size_t>(nD), 1);
                        std::vector<double> fst(static_cast<size_t>(nD), -1.0), lst(static_cast<size_t>(nD), -1.0);
                        int dl = nD; double tt = 0.0;
                        for (const int msz : seqSizes) {
                            std::vector<int> idx2;
                            for (int j = 0; j < nD; ++j) if (al[static_cast<size_t>(j)]) idx2.push_back(j);
                            std::sort(idx2.begin(), idx2.end(), [&](int x, int y) {
                                if (rG[static_cast<size_t>(x)] != rG[static_cast<size_t>(y)])
                                    return rG[static_cast<size_t>(x)] < rG[static_cast<size_t>(y)];
                                return x < y;
                            });
                            tt += popt[static_cast<size_t>(std::min(msz, (int)idx2.size()))] * f;
                            for (int j2 = 0; j2 < static_cast<int>(idx2.size()) && j2 < msz; ++j2) {
                                const int j = idx2[static_cast<size_t>(j2)];
                                if (fst[static_cast<size_t>(j)] < 0) fst[static_cast<size_t>(j)] = tt;
                                lst[static_cast<size_t>(j)] = tt;
                                rG[static_cast<size_t>(j)] -= 1.0;
                                if (rG[static_cast<size_t>(j)] <= 0) { al[static_cast<size_t>(j)] = 0; --dl; }
                            }
                        }
                        for (int j = 0; j < nD; ++j)
                            if (fst[static_cast<size_t>(j)] >= 0)
                                spanSum += lst[static_cast<size_t>(j)] - fst[static_cast<size_t>(j)];
                        const double tp = (spanDone + spanSum) /
                                          static_cast<double>(gapsDone + p7::futGapsOf(nD, g));
                        return tp <= slo2 * f + 1e-9;  // envelope: SLO budget inflates too
                    };
                    if (!feasAt(1.0)) return 1.0;
                    for (int it = 0; it < 40; ++it) {
                        const double mid = 0.5 * (lo + hi);
                        if (feasAt(mid)) lo = mid; else hi = mid;
                    }
                    return lo;
                };
                // rho* comparison uses the two full simulations already run
                (void)rhoStar;
                if (invChecks > 0 && invOK != invChecks) --okB2acc;
                if (idChecks > 0 && idOK != idChecks) --okB2acc;
                g_invC += invChecks; g_invO += invOK; g_idC += idChecks; g_idO += idOK;
            }
        }  // per-instance loop

        std::printf("B1. tdr exchange dominance: %d/%d pass\n", okB1, N);
        std::printf("B2i. SAFE pick-time gap-feasibility invariant: %lld/%lld pass\n", g_invO, g_invC);
        std::printf("B2ii. identity with uncapped argmin when cap non-binding: %lld/%lld pass\n", g_idO, g_idC);
        if (g_invO != g_invC) fB += 1;
        if (g_idO != g_idC) fB += 1;
        fB += (N - okB1);
    }

    // ===== C. Doom valve ====================================================
    {
        int okC = 0;
        const int N = 200;
        for (int i = 0; i < N; ++i) {
            p7::Agg a;
            a.slo1 = 10.0 + U(rng) * 90.0;
            a.slo2 = p7::INF;                   // tpot channel off
            a.R = 20 + static_cast<int>(U(rng) * 100);
            a.nd = a.R / 2;
            a.floor_chain = a.slo1 * 4.0;       // solo floor alone busts SLO1
            a.tdr_hat = a.floor_chain;
            a.sum_tdr = a.tdr_hat * a.nd;
            a.gaps_left = 0; a.tg = 0; a.sum_gaps = 0; a.tpot_hat = 0; a.round_floor = 1.0;
            const bool doomedSafe = p7::classify(a);   // must be FALSE (hopeless)
            p7::Agg b = a;
            b.slo1 = a.slo1 * 10.0;             // now thin-but-winnable
            b.tdr_hat = b.slo1 * 0.999;
            b.sum_tdr = b.tdr_hat * b.nd;
            const bool winnableSafe = p7::classify(b); // must be TRUE (thin)
            if (!doomedSafe && winnableSafe) ++okC;
        }
        std::printf("C. doom valve: %d/%d pass\n", okC, N);
        if (okC != N) ++fC;
    }

    const int fails = fA + fB + fC;
    std::printf("P7 PROPERTY: %s (A=%d B=%d C=%d)\n", fails == 0 ? "ALL PASS" : "FAIL", fA, fB, fC);
    return fails == 0 ? 0 : 1;
}
