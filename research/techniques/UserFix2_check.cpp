// Property/unit check for the round-2 MarginalBatch::group_size fix
// (standalone mirror, P2_brute_check.cpp style).
// Mirrors icpc2026::MarginalBatch::group_size AS LANDED IN ROUND 2:
//   - w_c<=0 : exhaustive argmin of S_Popt[m]/m (first strict min wins)
//   - w_c>0  : greedy gain-vs-loss walk over consecutive m, break at first
//              non-positive increment; TDR side priced by the LOCAL COMPUTER
//              BLOCKAGE 2S+dpre(m)+dpost(m) (round-2 correction), tpot side by
//              S_Popt[m]; dist_base>0 gradient loss, dist_base==0 binary cliff.
// Properties:
//   (a) w_c<=0 result == independent exhaustive argmin of S_Popt[m]/m
//   (b) w_c>0 greedy == true cumulative-objective argmax whenever the
//       objective is strictly unimodal; PLUS measured divergence rate of
//       break-early vs exhaustive best under the NEW projection
//   (c) dist_base==0: once dist_m>0, further growth costs ZERO waiting-loss;
//       the only charged step is the healthy->violated crossing (== 1000*w_c)
//   (d) TDR projection monotone nondecreasing in m (local blockage grows)
// Compile: g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
#include <bits/stdc++.h>
using namespace std;

static constexpr int MM = 4096;

struct Col {  // piecewise-linear, nondecreasing, strictly positive on [1,MM]
    vector<int> bs;
    vector<double> vs;
    double get(int m) const {
        if (bs.empty()) return 1.0;
        if (m <= bs.front()) return vs.front();
        if (m >= bs.back()) return vs.back();
        int lo = 0, hi = (int)bs.size();
        while (lo < hi) { int mid = (lo + hi) / 2; if (bs[mid] <= m) lo = mid + 1; else hi = mid; }
        int ih = lo, il = lo - 1;
        if (bs[il] == m) return vs[il];
        double f = double(m - bs[il]) / double(bs[ih] - bs[il]);
        return vs[il] + f * (vs[ih] - vs[il]);
    }
};

struct Env {
    double S = 1.0, lat = 1.0, beta = 0.0;
    int K = 1;
    Col dpre, dpost, dproc;
    double S_Popt[MM + 1];
    int mmax_dpre = 1 << 20;
    void build() {
        for (int m = 1; m <= MM; ++m) {
            double b = 1e300;
            int km = K < m ? K : m;
            if (km < 1) km = 1;
            for (int k = 1; k <= km; ++k) {
                double tl = 2 * S + dpre.get(m) + dpost.get(m);
                int c = (m + k - 1) / k;
                double tc = S + dproc.get(c);
                double tx = k * lat + beta * m;
                double p = tl;
                if (tc > p) p = tc;
                if (tx > p) p = tx;
                if (p < b) b = p;
            }
            S_Popt[m] = b;
        }
        S_Popt[0] = 0.0;
    }
    // MarginalBatch::precompute climb rule for the D_PRE column.
    void cap_dpre() {
        const int mx = MM;  // largest listed batch (generated tables list up to MM)
        const bool cr = (K > 1);
        const double la = cr ? lat : 0.0;
        int m = 1;
        while (m < mx) {
            double dm = dpre.get(m), dm1 = dpre.get(m + 1);
            double avg = (S + 2 * la + dm) / m, mg = dm1 - dm;
            if (mg <= avg) ++m; else break;
        }
        // listed-pairs argmin over the same breakpoints (all listed here)
        int am = 1; double ba = 1e300;
        for (size_t i = 0; i < dpre.bs.size(); ++i) {
            double a = (S + 2 * la + dpre.vs[i]) / dpre.bs[i];
            if (a < ba - 1e-12) { ba = a; am = dpre.bs[i]; }
        }
        int cap = m > am ? m : am;
        if (cap >= mx) cap = 1 << 20;
        mmax_dpre = cap;
    }
    inline double local_block(int m) const {
        return 2.0 * S + dpre.get(m) + dpost.get(m);
    }
};

struct Scoring {
    double SLO1 = 100, SLO2 = 100, tp_UB = 10, tp_base = 1, dist_base = 1.0;
    double w_tp = 0.5, w_c = 0.5;
};

// Exact mirror of the round-2 group_size internals for one consecutive pair.
// Returns the waiting-loss charged at that step and exposes dist values.
struct StepView { double dist_m, dist_m1, loss_c; };
static StepView greedy_step(const Env& e, const Scoring& sc, double current_tdr, int m) {
    StepView v{};
    const double tp_m = double(m) / e.S_Popt[m], tp_m1 = double(m + 1) / e.S_Popt[m + 1];
    (void)tp_m; (void)tp_m1;
    const double lb_m = e.local_block(m), lb_m1 = e.local_block(m + 1);
    const double proj_tdr_m = current_tdr + lb_m, proj_tdr_m1 = current_tdr + lb_m1;
    const double e_tdr_m = max(0.0, (proj_tdr_m - sc.SLO1) / sc.SLO1);
    const double e_tdr_m1 = max(0.0, (proj_tdr_m1 - sc.SLO1) / sc.SLO1);
    const double e_tpot_m = max(0.0, (e.S_Popt[m] - sc.SLO2) / sc.SLO2);
    const double e_tpot_m1 = max(0.0, (e.S_Popt[m + 1] - sc.SLO2) / sc.SLO2);
    v.dist_m = sqrt(e_tdr_m * e_tdr_m + e_tpot_m * e_tpot_m);
    v.dist_m1 = sqrt(e_tdr_m1 * e_tdr_m1 + e_tpot_m1 * e_tpot_m1);
    const double delta_dist = v.dist_m1 - v.dist_m;
    if (sc.dist_base > 0.0) v.loss_c = 1000.0 * sc.w_c * delta_dist / sc.dist_base;
    else {
        if (v.dist_m == 0.0 && v.dist_m1 > 0.0) v.loss_c = 1000.0 * sc.w_c;
        else v.loss_c = 0.0;
    }
    return v;
}

static double gain_tp_of(const Env& e, const Scoring& sc, int m) {
    const double delta_tp = double(m + 1) / e.S_Popt[m + 1] - double(m) / e.S_Popt[m];
    return (sc.tp_UB > sc.tp_base) ? 1000.0 * sc.w_tp * delta_tp / (sc.tp_UB - sc.tp_base) : 0.0;
}

// Full mirror of MarginalBatch::group_size (round-2 form).
static int group_size_mirror(const Env& e, const Scoring& sc, int n_ready,
                             vector<StepView>* trace) {
    const int mmax = e.mmax_dpre;
    if (mmax < 1) return 1;
    int pool = n_ready;
    if (pool > mmax) pool = mmax;
    if (pool > 4096) pool = 4096;
    if (pool < 1) pool = 1;
    if (sc.w_c <= 0.0) {  // branch (a) — verified separately against exhaustive
        int best_m = 1; double best_tp = e.S_Popt[1];
        for (int m = 2; m <= pool; ++m) {
            double tp_cost = e.S_Popt[m] / double(m);
            if (tp_cost < best_tp - 1e-12) { best_tp = tp_cost; best_m = m; }
        }
        return best_m;
    }
    const double current_tdr = 0.0;  // running mean folded into the tests below
    int best_m = 1;
    for (int m = 1; m < pool; ++m) {
        const double gain_tp = gain_tp_of(e, sc, m);
        StepView v = greedy_step(e, sc, current_tdr, m);
        if (trace) trace->push_back(v);
        if (gain_tp > v.loss_c + 1e-9) best_m = m + 1; else break;
    }
    return best_m;
}

// True cumulative objective (the quantity the greedy approximates).
static double objective_at(const Env& e, const Scoring& sc, int m) {
    const double G = (sc.tp_UB > sc.tp_base)
        ? 1000.0 * sc.w_tp / (sc.tp_UB - sc.tp_base) : 0.0;
    const double tp_gain = G * (double(m) / e.S_Popt[m] - double(1) / e.S_Popt[1]);
    if (sc.dist_base <= 0.0) return tp_gain;  // cliff handled by (c), not graded here
    auto distf = [&](int k) {
        double et = max(0.0, (k * 0.0 + e.local_block(k) - sc.SLO1) / sc.SLO1);
        double ep = max(0.0, (e.S_Popt[k] - sc.SLO2) / sc.SLO2);
        return sqrt(et * et + ep * ep);
    };
    const double L = 1000.0 * sc.w_c / sc.dist_base;
    return tp_gain - L * (distf(m) - distf(1));
}

static Col gen_col(mt19937_64& rng, bool allow_flat) {
    uniform_real_distribution<double> U(0.0, 1.0);
    Col c;
    int n = 1 + int(U(rng) * 7);           // 1..7 listed rows
    set<int> pts{1};
    while ((int)pts.size() < n) pts.insert(1 + int(U(rng) * MM));
    if (!allow_flat || U(rng) < 0.8) pts.insert(MM);
    double base = 0.05 + U(rng) * 40.0;
    double grow = 0.02 + U(rng) * 3.0;     // sublinear-to-linear growth
    double prev = base;
    for (int p : pts) {
        double val = base + grow * log(1.0 + double(p)) * (0.4 + 0.6 * U(rng));
        if (val < prev + 1e-6) val = prev + 1e-6;   // enforce nondecreasing & distinct-ish
        c.bs.push_back(p);
        c.vs.push_back(val);
        prev = val;
    }
    return c;
}

int main() {
    const int CONFIGS = 20000;
    mt19937_64 rng(20260826ULL);

    long long n_a = 0, fail_a = 0;
    long long n_b_unimodal = 0, fail_b = 0;
    long long n_b_graded = 0, divergent_index = 0, divergent_value = 0;
    long long n_c = 0, fail_c = 0;
    long long n_d = 0, fail_d = 0;

    uniform_real_distribution<double> U(0.0, 1.0);
    exponential_distribution<double> E(1.0);

    for (int ci = 0; ci < CONFIGS; ++ci) {
        Env e;
        e.S = 0.01 + E(rng) * 2.0; if (e.S > 500) e.S = 500;
        e.K = 1 + int(U(rng) * 8); if (e.K > 8) e.K = 8;
        e.lat = 0.001 + E(rng) * 0.5; if (e.lat > 50) e.lat = 50;
        e.beta = U(rng) < 0.5 ? 0.0 : 0.0005 + U(rng) * 0.05;
        e.dpre = gen_col(rng, true);
        e.dpost = gen_col(rng, true);
        e.dproc = gen_col(rng, false);
        e.build();
        e.cap_dpre();

        Scoring sc;
        sc.w_tp = U(rng) < 0.15 ? 0.0 : U(rng);
        sc.w_c = U(rng) < 0.15 ? 0.0 : U(rng);
        if (sc.w_tp + sc.w_c < 1e-6) sc.w_c = 1.0;
        sc.SLO1 = 0.5 + E(rng) * 30.0; if (sc.SLO1 > 1e9) sc.SLO1 = 1e9;
        sc.SLO2 = 0.5 + E(rng) * 30.0; if (sc.SLO2 > 1e9) sc.SLO2 = 1e9;
        sc.tp_base = U(rng) * 2.0;
        sc.tp_UB = sc.tp_base + (U(rng) < 0.1 ? 0.0 : 0.01 + E(rng) * 5.0);
        sc.dist_base = (U(rng) < 0.35) ? 0.0 : (0.01 + E(rng) * 1.5);
        const int n_ready = 1 + int(U(rng) * 2000);

        // ---- (d) TDR projection monotone nondecreasing in m -----------------
        ++n_d;
        for (int m = 1; m < MM; ++m) {
            if (!(e.local_block(m + 1) >= e.local_block(m) - 1e-12)) { ++fail_d; break; }
        }
        // spot-check the mirror's step view uses (m, m+1) in the right slots:
        {
            StepView v = greedy_step(e, sc, 5.0, 7);
            double expect = sqrt(
                pow(max(0.0, (5.0 + e.local_block(7) - sc.SLO1) / sc.SLO1), 2) +
                pow(max(0.0, (e.S_Popt[7] - sc.SLO2) / sc.SLO2), 2));
            if (fabs(v.dist_m - expect) > 1e-12) ++fail_d;
        }

        // ---- (a) w_c<=0 branch == exhaustive argmin S_Popt[m]/m --------------
        if (sc.w_c <= 0.0 && sc.w_tp >= 0.0) {
            ++n_a;
            int got = group_size_mirror(e, sc, n_ready, nullptr);
            int pool = n_ready; if (pool > e.mmax_dpre) pool = e.mmax_dpre;
            if (pool > 4096) pool = 4096;
            if (pool < 1) pool = 1;
            int want = 1; double bw = e.S_Popt[1] / 1.0;
            for (int m = 2; m <= pool; ++m) {
                double c = e.S_Popt[m] / double(m);
                if (c < bw - 1e-12) { bw = c; want = m; }
            }
            // equal-cost tolerance: got must achieve the minimal cost
            double cg = e.S_Popt[got] / double(got);
            if (got != want && !(cg <= bw + 1e-12)) ++fail_a;
        }

        // ---- (b) greedy vs true cumulative-objective argmax ------------------
        if (sc.w_c > 0.0 && sc.dist_base > 0.0) {
            int pool = n_ready; if (pool > e.mmax_dpre) pool = e.mmax_dpre;
            if (pool > 4096) pool = 4096;
            if (pool < 1) pool = 1;
            vector<double> obj(pool + 1);
            for (int m = 1; m <= pool; ++m) obj[m] = objective_at(e, sc, m);
            // strict unimodality: one + -> - sign flip, increments bounded away from 0
            vector<double> inc(pool);
            bool strict = (pool >= 1);
            int flips = 0;
            for (int m = 1; m < pool && strict; ++m) {
                inc[m] = obj[m + 1] - obj[m];
                if (fabs(inc[m]) <= 1e-6) strict = false;
                else {
                    bool pos = inc[m] > 0;
                    if (m > 1) {
                        bool prevpos = inc[m - 1] > 0;
                        if (!prevpos && pos) strict = false;  // - -> + : not unimodal
                        if (prevpos && !pos) ++flips;
                    }
                }
            }
            if (strict && flips <= 1) {
                ++n_b_unimodal;
                int got = group_size_mirror(e, sc, n_ready, nullptr);
                int want = 1; double bv = obj[1];
                for (int m = 2; m <= pool; ++m) if (obj[m] > bv + 1e-12) { bv = obj[m]; want = m; }
                if (got != want) {
                    ++fail_b;
                    fprintf(stderr, "B-MISMATCH cfg=%d got=%d want=%d obj_got=%.9g obj_want=%.9g\n",
                            ci, got, want, obj[got], obj[want]);
                }
            }
            // divergence measurement (ALL configs, new projection): break-early
            // greedy vs exhaustive best-by-value
            ++n_b_graded;
            int got = group_size_mirror(e, sc, n_ready, nullptr);
            int want = 1; double bv = obj[1];
            for (int m = 2; m <= pool; ++m) if (obj[m] > bv + 1e-12) { bv = obj[m]; want = m; }
            if (got != want) ++divergent_index;
            if (obj[got] < obj[want] - 1e-6) ++divergent_value;
        }

        // ---- (c) dist_base==0 cliff semantics --------------------------------
        if (sc.dist_base == 0.0 && sc.w_c > 0.0) {
            ++n_c;
            int pool = n_ready; if (pool > e.mmax_dpre) pool = e.mmax_dpre;
            if (pool > 4096) pool = 4096;
            if (pool < 1) pool = 1;
            vector<StepView> tr;
            int got = group_size_mirror(e, sc, n_ready, &tr);
            (void)got;
            bool seen_violated = false;
            for (const StepView& v : tr) {
                if (v.dist_m > 0.0) {
                    seen_violated = true;
                    // once violated: further growth must cost ZERO waiting-loss
                    if (v.loss_c != 0.0) { ++fail_c; break; }
                } else if (v.dist_m == 0.0 && v.dist_m1 > 0.0) {
                    // the single crossing must charge exactly the cliff value
                    if (fabs(v.loss_c - 1000.0 * sc.w_c) > 1e-12) { ++fail_c; break; }
                    if (seen_violated) { ++fail_c; break; }  // cannot re-cross after violation
                } else {
                    if (v.loss_c != 0.0) { ++fail_c; break; }
                }
            }
        }
    }

    printf("CONFIGS=%d\n", CONFIGS);
    printf("(a) w_c<=0 exhaustive-argmin checks: %lld run, %lld FAILED\n", n_a, fail_a);
    printf("(b) unimodal-equality: %lld strict-unimodal graded, %lld FAILED\n", n_b_unimodal, fail_b);
    printf("(b) divergence measure (all w_c>0,dist_base>0): %lld run, index-divergent %lld (%.2f%%), value-worse %lld (%.2f%%)\n",
           n_b_graded, divergent_index, 100.0 * divergent_index / max<long long>(1, n_b_graded),
           divergent_value, 100.0 * divergent_value / max<long long>(1, n_b_graded));
    printf("(c) dist_base==0 cliff-loss checks: %lld run, %lld FAILED\n", n_c, fail_c);
    printf("(d) blockage-monotonicity + slot-order checks: %lld run, %lld FAILED\n", n_d, fail_d);

    const bool ok = (fail_a + fail_b + fail_c + fail_d) == 0;
    printf("USERFIX2_CHECK: %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}
